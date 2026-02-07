// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGEditorModule.h"
#include "ImageToUMGLog.h"
#include "ImageToUMGConstants.h"
#include "ImageToUMGSettingsCustomization.h"
#include "PropertyEditorModule.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "ImageToUMGSettings.h"
#include "SImageToUMGPreviewWindow.h"
#include "UMGWidgetUtils.h"
#include "PenMCPBridge.h"
#include "PenImporter.h"
#include "VisualComparator.h"
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

	// PenMCPBridge 控制台入口
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ImageToUMG.PenConvert"),
		TEXT("从MCP JSON文件转换: ImageToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("用法: ImageToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId]"));
				return;
			}
			FString NodesStr, VarsStr;
			if (!FFileHelper::LoadFileToString(NodesStr, *Args[0]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[0]);
				return;
			}
			if (!FFileHelper::LoadFileToString(VarsStr, *Args[1]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[1]);
				return;
			}
			FString TargetId = Args.Num() > 2 ? Args[2] : TEXT("");
			FUMGWidgetData Data;
			if (FPenMCPBridge::ConvertFromMCP(NodesStr, VarsStr, Data, TargetId))
			{
				UE_LOG(LogImageToUMG, Log, TEXT("[OK] PenConvert 成功, Root children: %d"), Data.RootWidget.Children.Num());
				for (int32 i = 0; i < Data.RootWidget.Children.Num(); ++i)
				{
					const auto& C = Data.RootWidget.Children[i];
					UE_LOG(LogImageToUMG, Log, TEXT("  [%d] %s (%s)"), i, *C.Name, *C.Type);
				}

				// 生成 Widget Blueprint
				FString PkgPath = TEXT("/Game/ImageToUMG/PenConvert_Output");
				UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, PkgPath);
				if (WBP)
				{
					UE_LOG(LogImageToUMG, Log, TEXT("[OK] Widget Blueprint 已生成: %s"), *WBP->GetPathName());

					// 渲染截图
					TArray<uint8> PngData;
					FIntPoint Size;
					if (FUMGWidgetUtils::RenderWidgetToPng(WBP, FIntPoint(400, 300), PngData, Size))
					{
						FString OutPng = FPaths::ProjectSavedDir() / TEXT("ImageToUMG/rendered.png");
						FFileHelper::SaveArrayToFile(PngData, *OutPng);
						UE_LOG(LogImageToUMG, Log, TEXT("[OK] 渲染截图已保存: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
					}
					else
					{
						UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] 渲染截图失败"));
					}
				}
				else
				{
					UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] Widget Blueprint 生成失败"));
				}
			}
			else
			{
				UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] PenConvert 失败"));
			}
		}),
		ECVF_Default
	);

	// VisualComparator 控制台入口
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ImageToUMG.PenCompare"),
		TEXT("对比两张PNG: ImageToUMG.PenCompare <design.png> <rendered.png> [threshold]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("用法: ImageToUMG.PenCompare <design.png> <rendered.png> [threshold=0.85]"));
				return;
			}
			TArray<uint8> PngA, PngB;
			if (!FFileHelper::LoadFileToArray(PngA, *Args[0]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[0]);
				return;
			}
			if (!FFileHelper::LoadFileToArray(PngB, *Args[1]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[1]);
				return;
			}
			float Threshold = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.85f;
			FVisualCompareReport Report = FVisualComparator::CompareImages(PngA, PngB, Threshold);
			UE_LOG(LogImageToUMG, Log, TEXT("=== VisualComparator 结果 ==="));
			UE_LOG(LogImageToUMG, Log, TEXT("SSIM: %.4f"), Report.SSIM);
			UE_LOG(LogImageToUMG, Log, TEXT("DiffPixelRatio: %.4f"), Report.DiffPixelRatio);
			UE_LOG(LogImageToUMG, Log, TEXT("Pass: %s (threshold=%.2f)"), Report.bPassThreshold ? TEXT("YES") : TEXT("NO"), Threshold);
		}),
		ECVF_Default
	);

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(FImageToUMGCommands::Get().GenerateFromImage,
		FExecuteAction::CreateRaw(this, &FImageToUMGEditorModule::OnGenerateFromFile));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
	{
		// Tools 菜单
		UToolMenu* Tools = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (Tools) Tools->FindOrAddSection("ImageToUMG").AddMenuEntryWithCommandList(FImageToUMGCommands::Get().GenerateFromImage, PluginCommands);

		// Texture2D 右键菜单已移除（imgtoumg pipeline 已清理）
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
	// AI image analysis pipeline has been removed (imgtoumg cleanup).
	// Use PenConvert workflow instead.
	OnError(TEXT("AI image analysis is no longer available. Please use the PenConvert workflow instead."));
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

	// v2.2: 设置源图尺寸（用于ViewportMapping）
	Data.SourceSize = SourceImageSize;

	// Image asset processing removed (imgtoumg cleanup).
	// PenConvert workflow handles asset binding differently.

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
