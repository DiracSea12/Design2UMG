// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGEditorModule.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "ImageToUMGSettingsCustomization.h"
#include "PropertyEditorModule.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "ImageToUMGSettings.h"
#include "HttpAIClient.h"
#include "ScriptAIClient.h"
#include "ImageEncoder.h"
#include "SImageToUMGPreviewWindow.h"
#include "ImageAssetProcessor.h"
#include "UMGWidgetUtils.h"
#include "Misc/MessageDialog.h"

#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Async/Async.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogImageToUMG);

#define LOCTEXT_NAMESPACE "FImageToUMGEditorModule"

// 并发请求保护
static TAtomic<bool> bAnalysisInProgress(false);

// Script AI Client 单例（避免重复创建）
static TSharedPtr<FScriptAIClient> GScriptAIClient;

class FImageToUMGCommands : public TCommands<FImageToUMGCommands>
{
public:
	FImageToUMGCommands() : TCommands<FImageToUMGCommands>(TEXT("ImageToUMG"), LOCTEXT("ImageToUMG", "Image To UMG"), NAME_None, FAppStyle::GetAppStyleSetName()) {}
	virtual void RegisterCommands() override
	{
		UI_COMMAND(GenerateFromImage, "Generate UMG from Image", "Select an image file and generate a Widget Blueprint", EUserInterfaceActionType::Button, FInputChord());
	}
	TSharedPtr<FUICommandInfo> GenerateFromImage;
};

void FImageToUMGEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UImageToUMGSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FImageToUMGSettingsCustomization::MakeInstance)
	);

	FImageToUMGCommands::Register();
	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(FImageToUMGCommands::Get().GenerateFromImage,
		FExecuteAction::CreateRaw(this, &FImageToUMGEditorModule::OnGenerateFromFile));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		// Tools 菜单
		UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (Tools) Tools->FindOrAddSection("ImageToUMG").AddMenuEntryWithCommandList(FImageToUMGCommands::Get().GenerateFromImage, PluginCommands);

		// Texture2D 右键菜单
		UToolMenu* Ctx = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Texture2D");
		if (!Ctx) return;

		Ctx->FindOrAddSection("GetAssetActions").AddMenuEntry("ImageToUMG_Generate",
			LOCTEXT("GenUMG", "Generate UMG Widget"),
			LOCTEXT("GenUMGTip", "Open preview window to generate Widget Blueprint"),
			FSlateIcon(),
			FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& MenuContext)
			{
				auto* AC = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!AC || AC->SelectedAssets.Num() == 0) return;

				UTexture2D* Tex = Cast<UTexture2D>(AC->SelectedAssets[0].GetAsset());
				if (!Tex) return;

				TArray<uint8> Data;
				FString Fmt;
				int32 W, H;
				if (!FImageEncoder::GetImageDataFromTexture(Tex, Data, Fmt, W, H)) return;

				FString OutputPath = FPackageName::GetLongPackagePath(AC->SelectedAssets[0].PackageName.ToString());
				FString AssetName = AC->SelectedAssets[0].AssetName.ToString();

				// 直接打开预览窗口，不调用 AI
				if (FModuleManager::Get().IsModuleLoaded("ImageToUMGEditor"))
				{
					FImageToUMGEditorModule& Module = FModuleManager::GetModuleChecked<FImageToUMGEditorModule>("ImageToUMGEditor");
					Module.OpenPreviewWindowDirect(Tex, OutputPath, AssetName, Data, FIntPoint(W, H));
				}
			})
		);
	}));
}

void FImageToUMGEditorModule::ShutdownModule()
{
	// 清理临时纹理
	TempTextureHolder.Reset();

	UToolMenus::UnRegisterStartupCallback(this);
	FImageToUMGCommands::Unregister();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UImageToUMGSettings::StaticClass()->GetFName());
	}
}

void FImageToUMGEditorModule::OnGenerateFromFile()
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!Platform) return;

	TArray<FString> Files;
	if (!Platform->OpenFileDialog(nullptr, TEXT("Select UI Screenshot"), FPaths::ProjectDir(), TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"), EFileDialogFlags::None, Files) || Files.Num() == 0)
		return;

	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Files[0])) return;

	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat Fmt = Mod.DetectImageFormat(Data.GetData(), Data.Num());
	TSharedPtr<IImageWrapper> Wrap = Mod.CreateImageWrapper(Fmt);
	if (!Wrap.IsValid() || !Wrap->SetCompressed(Data.GetData(), Data.Num())) return;

	int32 W = Wrap->GetWidth(), H = Wrap->GetHeight();
	if (W == 0 || H == 0) return;

	const UImageToUMGSettings* S = UImageToUMGSettings::Get();
	FString AssetName = FPaths::GetBaseFilename(Files[0]);

	// 创建临时 Texture2D 用于预览窗口显示源图
	UTexture2D* TempTex = nullptr;
	TArray64<uint8> RawData;
	if (Wrap->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		TempTex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (TempTex)
		{
			void* TextureData = TempTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
			TempTex->GetPlatformData()->Mips[0].BulkData.Unlock();
			TempTex->UpdateResource();
			// 持有临时纹理防止GC
			TempTextureHolder.Reset(TempTex);
		}
	}

	// 直接打开预览窗口，不调用 AI
	OpenPreviewWindowDirect(TempTex, S->DefaultOutputPath, AssetName, Data, FIntPoint(W, H));
}

void FImageToUMGEditorModule::OpenPreviewWindow(const FString& Json, UTexture2D* Tex, const FString& Path, const FString& Name,
	const TArray<uint8>& ImageData, FIntPoint ImageSize, FIntPoint AIImageSize)
{
	const UImageToUMGSettings* S = UImageToUMGSettings::Get();
	FString OutPath = S->DefaultOutputPath.IsEmpty() ? Path : S->DefaultOutputPath;

	TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(LOCTEXT("Preview", "ImageToUMG Preview"))
		.ClientSize(FVector2D(ImageToUMGConstants::PreviewWindowWidth, ImageToUMGConstants::PreviewWindowHeight))
		.SupportsMinimize(true).SupportsMaximize(true);

	Win->SetContent(SNew(SImageToUMGPreviewWindow)
		.OriginalJson(Json).SourceTexture(Tex)
		.OutputPath(OutPath / FString::Printf(TEXT("WBP_%s"), *Name))
		.SourceImageData(ImageData)
		.SourceImageSize(ImageSize)
		.AIImageSize(AIImageSize)
		.OnConfirmed(FOnPreviewConfirmed::CreateRaw(this, &FImageToUMGEditorModule::OnPreviewConfirmed)));

	FSlateApplication::Get().AddWindow(Win);
}

void FImageToUMGEditorModule::OpenPreviewWindowDirect(UTexture2D* Tex, const FString& Path, const FString& Name,
	const TArray<uint8>& ImageData, FIntPoint ImageSize)
{
	// 追踪窗口打开次数
	static int32 OpenCount = 0;
	OpenCount++;
	UE_LOG(LogImageToUMG, Verbose, TEXT("[EditorModule] OpenPreviewWindowDirect #%d - 纹理: %s, 尺寸: %dx%d, ImageData: %d bytes"),
		OpenCount,
		Tex ? *Tex->GetName() : TEXT("NULL"),
		ImageSize.X, ImageSize.Y,
		ImageData.Num());
	const UImageToUMGSettings* S = UImageToUMGSettings::Get();
	FString OutPath = S->DefaultOutputPath.IsEmpty() ? Path : S->DefaultOutputPath;

	// 默认 JSON 模板（包含提示文本，避免空容器被解析器拒绝）
	FString DefaultJson = TEXT(R"({
  "WidgetName": "WBP_)") + Name + TEXT(R"(",
  "RootWidget": {
    "Type": "Overlay",
    "Name": "Root",
    "Children": [
      {
        "Type": "TextBlock",
        "Name": "PlaceholderText",
        "Properties": {
          "Text": "Click 'Send to AI' or edit JSON manually",
          "Color": "#888888",
          "FontSize": 24
        },
        "Slot": {
          "HAlign": "Center",
          "VAlign": "Center"
        }
      }
    ]
  },
  "ImageAssets": []
})");

	TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(LOCTEXT("PreviewDirect", "ImageToUMG Preview"))
		.ClientSize(FVector2D(ImageToUMGConstants::PreviewWindowWidth, ImageToUMGConstants::PreviewWindowHeight))
		.SupportsMinimize(true).SupportsMaximize(true);

	Win->SetContent(SNew(SImageToUMGPreviewWindow)
		.OriginalJson(DefaultJson).SourceTexture(Tex)
		.OutputPath(OutPath / FString::Printf(TEXT("WBP_%s"), *Name))
		.SourceImageData(ImageData)
		.SourceImageSize(ImageSize)
		.AIImageSize(ImageSize)  // 初始时 AI 尺寸等于源图尺寸
		.OnConfirmed(FOnPreviewConfirmed::CreateRaw(this, &FImageToUMGEditorModule::OnPreviewConfirmed))
		.OnRequestAIAnalysis(FOnPreviewRequestAI::CreateRaw(this, &FImageToUMGEditorModule::OnRequestAIAnalysis)));

	FSlateApplication::Get().AddWindow(Win);
}

void FImageToUMGEditorModule::OnRequestAIAnalysis(const TArray<uint8>& ImageData, FIntPoint ImageSize, TFunction<void(const FString&)> OnSuccess, TFunction<void(const FString&)> OnError)
{
	// 并发请求保护
	if (bAnalysisInProgress.Exchange(true))
	{
		OnError(TEXT("Analysis already in progress, please wait..."));
		return;
	}

	const UImageToUMGSettings* S = UImageToUMGSettings::Get();
	if (S->APIKey.IsEmpty())
	{
		bAnalysisInProgress = false;
		OnError(TEXT("Please configure API Key in Editor Preferences -> Plugins -> Image To UMG"));
		return;
	}

	// 准备 AI 用图像数据（自动缩放大图）
	TArray<uint8> ImageDataForAI;
	FIntPoint AIImageSize;
	FImageAssetProcessor::PrepareImageForAI(ImageData, ImageSize, ImageToUMGConstants::MaxAIDimension, ImageDataForAI, AIImageSize);

	// 检查图片大小限制
	if (ImageDataForAI.Num() > ImageToUMGConstants::MaxImageSizeForAI)
	{
		bAnalysisInProgress = false;
		const float SizeMB = ImageDataForAI.Num() / (1024.0f * 1024.0f);
		const float MaxMB = ImageToUMGConstants::MaxImageSizeForAI / (1024.0f * 1024.0f);
		OnError(FString::Printf(TEXT("Image too large: %.2f MB (max %.0f MB).\nPlease use a smaller image or reduce resolution."), SizeMB, MaxMB));
		return;
	}

	// 根据设置选择客户端
	if (S->bUseScriptClient)
	{
		// 使用 Python 脚本客户端
		if (!GScriptAIClient.IsValid())
		{
			GScriptAIClient = MakeShared<FScriptAIClient>();
		}

		GScriptAIClient->AnalyzeImage(ImageDataForAI, AIImageSize.X, AIImageSize.Y,
			FOnScriptAIComplete::CreateLambda([OnSuccess](const FString& Json)
			{
				bAnalysisInProgress = false;
				AsyncTask(ENamedThreads::GameThread, [Json, OnSuccess]() {
					OnSuccess(Json);
				});
			}),
			FOnScriptAIError::CreateLambda([OnError](const FString& Err)
			{
				bAnalysisInProgress = false;
				AsyncTask(ENamedThreads::GameThread, [Err, OnError]() {
					OnError(Err);
				});
			})
		);
	}
	else
	{
		// 使用 HTTP 客户端
		TSharedRef<FHttpAIClient> Client = MakeShared<FHttpAIClient>();

		Client->AnalyzeImage(ImageDataForAI, TEXT("png"), AIImageSize.X, AIImageSize.Y,
			FOnAIResponseReceived::CreateLambda([OnSuccess, Client](const FString& Json)
			{
				bAnalysisInProgress = false;
				AsyncTask(ENamedThreads::GameThread, [Json, OnSuccess]() {
					OnSuccess(Json);
				});
			}),
			FOnAIRequestFailed::CreateLambda([OnError, Client](const FString& Err)
			{
				bAnalysisInProgress = false;
				AsyncTask(ENamedThreads::GameThread, [Err, OnError]() {
					OnError(Err);
				});
			})
		);
	}
}

void FImageToUMGEditorModule::OnPreviewConfirmed(const FString& Json, const FString& OutputPath, const TArray<uint8>& SourceImageData, FIntPoint SourceImageSize, FVector2D CoordScale)
{
	FUMGWidgetData Data;
	FString Error;
	if (!FUMGJsonParser::ParseJson(Json, Data, &Error))
	{
		FNotificationInfo Info(FText::FromString(Error));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FString PkgPath = FPackageName::GetLongPackagePath(OutputPath);
	FString WName = FPackageName::GetLongPackageAssetName(OutputPath);
	if (!WName.IsEmpty()) Data.WidgetName = WName;

	// 处理图像资产并绑定到 Widget 节点
	TArray<FUMGImageAsset>& ImageAssets = Data.ImageAssets;

	// 兜底：AI没有返回ImageAssets时，从Widget节点的位置信息生成
	if (ImageAssets.Num() == 0)
	{
		FUMGWidgetUtils::GenerateImageAssetsFromWidgets(Data.RootWidget, ImageAssets);
	}

	if (ImageAssets.Num() > 0 && SourceImageData.Num() > 0)
	{
		const UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
		TArray<FProcessedImageAsset> ProcessedAssets;
		int32 ProcessedCount = FImageAssetProcessor::ProcessImageAssets(
			ImageAssets,
			SourceImageData,
			Settings->ImageAssetsOutputPath,
			ProcessedAssets,
			CoordScale
		);

		if (ProcessedCount > 0)
		{
			UE_LOG(LogImageToUMG, Log, TEXT("Processed %d/%d image assets"), ProcessedCount, ImageAssets.Num());
			FUMGWidgetUtils::BindImageAssets(Data.RootWidget, ProcessedAssets);
		}

		// 部分或全部失败时警告用户
		int32 FailedCount = ImageAssets.Num() - ProcessedCount;
		if (FailedCount > 0)
		{
			FNotificationInfo WarnInfo(FText::Format(
				LOCTEXT("ImageAssetWarning", "Warning: {0} of {1} image assets failed to process. Widget will be created without these images."),
				FText::AsNumber(FailedCount), FText::AsNumber(ImageAssets.Num())));
			WarnInfo.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(WarnInfo);
		}
	}

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, PkgPath);

	FNotificationInfo Info(WBP
		? FText::FromString(FString::Printf(TEXT("Created: %s"), *WBP->GetPathName()))
		: LOCTEXT("Failed", "Failed to generate Widget Blueprint"));
	Info.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	if (WBP && UImageToUMGSettings::Get()->bAutoOpenAfterGeneration)
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(WBP);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FImageToUMGEditorModule, ImageToUMGEditor)
