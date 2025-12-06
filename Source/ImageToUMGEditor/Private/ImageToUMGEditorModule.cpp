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

/**
 * AI 分析上下文 - 替代全局变量
 */
struct FAnalyzeContext
{
	TArray<uint8> SourceImageData;    // 原始图片（用于裁剪，保持最高质量）
	TArray<uint8> ImageDataForAI;     // 压缩后图片（用于AI API）
	FIntPoint SourceImageSize;        // 原始尺寸
	FIntPoint AIImageSize;            // AI用尺寸
	FString OutputPath;
	FString AssetName;
	TWeakObjectPtr<UTexture2D> Texture;

	/** 准备AI用图像数据（自动缩放大图） */
	void PrepareForAI(const TArray<uint8>& InSourceData, int32 W, int32 H)
	{
		SourceImageData = InSourceData;
		SourceImageSize = FIntPoint(W, H);

		if (W <= ImageToUMGConstants::MaxAIDimension && H <= ImageToUMGConstants::MaxAIDimension)
		{
			ImageDataForAI = InSourceData;
			AIImageSize = SourceImageSize;
			return;
		}

		float Scale = FMath::Min(
			static_cast<float>(ImageToUMGConstants::MaxAIDimension) / W,
			static_cast<float>(ImageToUMGConstants::MaxAIDimension) / H
		);
		int32 NewW = FMath::RoundToInt(W * Scale);
		int32 NewH = FMath::RoundToInt(H * Scale);

		TArray<uint8> ScaledData;
		if (FImageAssetProcessor::ScaleImageData(InSourceData, W, H, NewW, NewH, ScaledData))
		{
			ImageDataForAI = MoveTemp(ScaledData);
			AIImageSize = FIntPoint(NewW, NewH);
			UE_LOG(LogImageToUMG, Log, TEXT("Image scaled to %dx%d for AI (original: %dx%d)"), NewW, NewH, W, H);
		}
		else
		{
			ImageDataForAI = InSourceData;
			AIImageSize = SourceImageSize;
		}
	}
};

// 核心 AI 调用逻辑
static void AnalyzeAndPreview(TSharedRef<FAnalyzeContext> Context, const FString& Format)
{
	// 并发请求保护：防止用户快速连续触发多次分析
	if (bAnalysisInProgress.Exchange(true))
	{
		FNotificationInfo Info(LOCTEXT("AnalysisInProgress", "Analysis already in progress, please wait..."));
		Info.ExpireDuration = ImageToUMGConstants::NotificationDuration;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const UImageToUMGSettings* S = UImageToUMGSettings::Get();
	if (S->APIKey.IsEmpty())
	{
		bAnalysisInProgress = false;
		FNotificationInfo Info(LOCTEXT("NoAPIKey", "Please configure API Key in Editor Preferences -> Plugins -> Image To UMG"));
		Info.ExpireDuration = ImageToUMGConstants::NotificationDuration;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FNotificationInfo Info(LOCTEXT("Analyzing", "Analyzing image with AI..."));
	Info.bFireAndForget = false;
	auto Notif = FSlateNotificationManager::Get().AddNotification(Info);
	Notif->SetCompletionState(SNotificationItem::CS_Pending);

	TSharedRef<FHttpAIClient> Client = MakeShared<FHttpAIClient>();

	// 使用压缩后的图片发送给 AI（ImageDataForAI），但传递原始图片用于裁剪（SourceImageData）
	Client->AnalyzeImage(Context->ImageDataForAI, Format, Context->AIImageSize.X, Context->AIImageSize.Y,
		FOnAIResponseReceived::CreateLambda([Notif, Context, Client](const FString& Json)
		{
			bAnalysisInProgress = false;
			Notif->SetCompletionState(SNotificationItem::CS_Success);
			Notif->ExpireAndFadeout();

			AsyncTask(ENamedThreads::GameThread, [Json, Context]() {
				if (FModuleManager::Get().IsModuleLoaded("ImageToUMGEditor"))
				{
					// 传递原始图片数据用于裁剪（保持最高质量）
					FModuleManager::GetModuleChecked<FImageToUMGEditorModule>("ImageToUMGEditor")
						.OpenPreviewWindow(Json, Context->Texture.Get(), Context->OutputPath, Context->AssetName,
							Context->SourceImageData, Context->SourceImageSize, Context->AIImageSize);
				}
			});
		}),
		FOnAIRequestFailed::CreateLambda([Notif, Client](const FString& Err)
		{
			bAnalysisInProgress = false;
			Notif->SetText(FText::FromString(FString::Printf(TEXT("AI Error: %s"), *Err)));
			Notif->SetCompletionState(SNotificationItem::CS_Fail);
			Notif->ExpireAndFadeout();

			AsyncTask(ENamedThreads::GameThread, [Err]() {
				FString Message = FString::Printf(
					TEXT("AI Request Failed\n\n%s\n\nCheck the Output Log (LogImageToUMG) for more details."),
					*Err);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
			});
		})
	);
}

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
			LOCTEXT("GenUMGTip", "Use AI to generate Widget Blueprint"),
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

				TSharedRef<FAnalyzeContext> Context = MakeShared<FAnalyzeContext>();
				Context->PrepareForAI(Data, W, H);
				Context->OutputPath = FPackageName::GetLongPackagePath(AC->SelectedAssets[0].PackageName.ToString());
				Context->AssetName = AC->SelectedAssets[0].AssetName.ToString();
				Context->Texture = Tex;

				AnalyzeAndPreview(Context, Fmt);
			})
		);
	}));
}

void FImageToUMGEditorModule::ShutdownModule()
{
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

	TSharedRef<FAnalyzeContext> Context = MakeShared<FAnalyzeContext>();
	Context->PrepareForAI(Data, W, H);
	Context->OutputPath = S->DefaultOutputPath;
	Context->AssetName = FPaths::GetBaseFilename(Files[0]);

	// 创建临时 Texture2D 用于预览窗口显示源图（使用原始尺寸）
	TArray64<uint8> RawData;
	if (Wrap->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		UTexture2D* TempTex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (TempTex)
		{
			void* TextureData = TempTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(TextureData, RawData.GetData(), RawData.Num());
			TempTex->GetPlatformData()->Mips[0].BulkData.Unlock();
			TempTex->UpdateResource();
			Context->Texture = TempTex;
		}
	}

	AnalyzeAndPreview(Context, Fmt == EImageFormat::PNG ? TEXT("png") : TEXT("jpeg"));
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

void FImageToUMGEditorModule::OnPreviewConfirmed(const FString& Json, const FString& OutputPath, const TArray<uint8>& SourceImageData, FIntPoint SourceImageSize, float CoordScale)
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
