// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenFileImportFactory.h"
#include "PenToUMGLog.h"
#include "PenParser.h"
#include "PenMCPBridge.h"
#include "PenRemoteImageImporter.h"
#include "DesignSourceRegistry.h"
#include "SPenToUMGPreviewWindow.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGWidgetUtils.h"
#include "PenToUMGSettings.h"

#include "WidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PenFileImportFactory"

// ── Frame 选择器数据 ──

struct FPenFrameInfo
{
	FString Id;
	FString Name;
	float Width;
	float Height;
};

// ── 前向声明内部函数 ──

namespace PenImportInternal
{
	static const TCHAR* DefaultOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

	/** 提取顶层 frame 列表 */
	static TArray<FPenFrameInfo> ExtractTopFrames(const FPenDocument& Document);

	/** 执行导入（单 frame 快速路径） */
	static void ImportSingleFrame(const FString& FilePath, const FString& OutputPath);

	/** 执行导入（多 frame 选择路径） */
	static void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath);

	/** 打开预览窗口 */
	static bool OpenPreview(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 生成 Widget Blueprint + 渲染 PNG */
	static void GenerateAndRender(const FUMGWidgetData& Data, const FString& OutputPath);

	/** 从编辑后的 JSON 生成 */
	static void RunGenerateFromJson(const FString& EditedJson, const FString& OutputPath);

	/** 弹出 Frame 选择器对话框 */
	static void ShowFrameSelector(const FString& FilePath, const TArray<FPenFrameInfo>& Frames, const FString& OutputPath);
}

// ── UFactory 实现 ──

UPenFileImportFactory::UPenFileImportFactory()
{
	bEditorImport = true;
	bText = false;
	SupportedClass = UWidgetBlueprint::StaticClass();
	Formats.Add(TEXT("pen;Pencil Design File"));
}

bool UPenFileImportFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("pen"), ESearchCase::IgnoreCase);
}

UObject* UPenFileImportFactory::FactoryCreateFile(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn,
	bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	// 解析文档，检查顶层 frame 数量
	FPenDocument Document;
	if (!FPenParser::ParseFromFile(Filename, Document))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to parse %s"), *Filename);
		return nullptr;
	}

	TArray<FPenFrameInfo> Frames = PenImportInternal::ExtractTopFrames(Document);

	if (Frames.Num() == 0)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: No frames found in %s"), *Filename);
		return nullptr;
	}

	const FString OutputPath = InParent ? InParent->GetPathName() : FString(PenImportInternal::DefaultOutputPath);

	if (Frames.Num() == 1)
	{
		// 单 frame 快速路径
		PenImportInternal::ImportSingleFrame(Filename, OutputPath);
	}
	else
	{
		// 多 frame → 弹出选择器
		PenImportInternal::ShowFrameSelector(Filename, Frames, OutputPath);
	}

	// UFactory 返回 nullptr，实际生成在预览确认后异步完成
	bOutOperationCanceled = false;
	return nullptr;
}

// ── 内部实现 ──

namespace PenImportInternal
{

TArray<FPenFrameInfo> ExtractTopFrames(const FPenDocument& Document)
{
	TArray<FPenFrameInfo> Result;
	for (const auto& Child : Document.Children)
	{
		if (!Child.IsValid() || Child->Type != EPenNodeType::Frame)
			continue;

		FPenFrameInfo Info;
		Info.Id = Child->Id;
		Info.Name = Child->Name.IsEmpty() ? Child->Id : Child->Name;
		Info.Width = Child->Width.IsFixed() ? Child->Width.Value : 0.0f;
		Info.Height = Child->Height.IsFixed() ? Child->Height.Value : 0.0f;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

void ImportSingleFrame(const FString& FilePath, const FString& OutputPath)
{
	FUMGWidgetData Data;
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to read file: %s"), *FilePath);
		return;
	}
	if (!FDesignSourceRegistry::Convert(TEXT("Pen"), JsonStr, Data))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Convert failed: %s"), *FilePath);
		return;
	}

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
	}

	if (!OpenPreview(Data, OutputPath))
	{
		GenerateAndRender(Data, OutputPath);
	}
}

void ImportWithTargetNode(const FString& FilePath, const FString& TargetNodeId, const FString& OutputPath)
{
	// 读取原始 JSON，走 ConvertFromMCP 路径复用 TargetNodeId 过滤
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Failed to read file: %s"), *FilePath);
		return;
	}

	// .pen 文件的 children 数组就是 NodesJson，variables 部分作为 VarsJson
	// 但 ConvertFromMCP 期望 NodesJson 是纯数组，VarsJson 是对象
	// .pen 文件是完整文档，需要提取 children 和 variables
	TSharedPtr<FJsonObject> DocObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, DocObj) || !DocObj.IsValid())
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: JSON parse failed: %s"), *FilePath);
		return;
	}

	// 提取 children 数组为 JSON 字符串
	FString NodesJson;
	if (DocObj->HasField(TEXT("children")))
	{
		const TArray<TSharedPtr<FJsonValue>>& ChildrenArr = DocObj->GetArrayField(TEXT("children"));
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&NodesJson);
		FJsonSerializer::Serialize(ChildrenArr, Writer);
	}

	// 提取 variables/themes/fonts 为 JSON 对象字符串
	TSharedPtr<FJsonObject> VarsObj = MakeShared<FJsonObject>();
	if (DocObj->HasField(TEXT("variables")))
		VarsObj->SetField(TEXT("variables"), DocObj->TryGetField(TEXT("variables")));
	if (DocObj->HasField(TEXT("themes")))
		VarsObj->SetField(TEXT("themes"), DocObj->TryGetField(TEXT("themes")));
	if (DocObj->HasField(TEXT("fonts")))
		VarsObj->SetField(TEXT("fonts"), DocObj->TryGetField(TEXT("fonts")));

	FString VarsJson;
	TSharedRef<TJsonWriter<>> VarsWriter = TJsonWriterFactory<>::Create(&VarsJson);
	FJsonSerializer::Serialize(VarsObj.ToSharedRef(), VarsWriter);

	FUMGWidgetData Data;
	if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, Data, TargetNodeId))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: ConvertFromMCP failed for node %s"), *TargetNodeId);
		return;
	}

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(Data, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
	}

	if (!OpenPreview(Data, OutputPath))
	{
		GenerateAndRender(Data, OutputPath);
	}
}

void RunGenerateFromJson(const FString& EditedJson, const FString& OutputPath)
{
	FUMGWidgetData EditedData;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(EditedJson, EditedData, &ParseError))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: JSON parse failed: %s"), *ParseError);
		return;
	}

	FString RemoteImageReport;
	FPenRemoteImageImporter::ResolveRemoteImages(EditedData, &RemoteImageReport);
	if (!RemoteImageReport.IsEmpty())
	{
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *RemoteImageReport);
	}

	GenerateAndRender(EditedData, OutputPath);
}

void GenerateAndRender(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const FString FinalPath = OutputPath.IsEmpty() ? FString(DefaultOutputPath) : OutputPath;
	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenFileImport: Widget Blueprint generation failed"));
		return;
	}
	UE_LOG(LogPenToUMG, Log, TEXT("PenFileImport: Generated %s"), *WBP->GetPathName());
}

// ── 序列化辅助（与 EditorModule 中相同模式） ──

static TSharedRef<FJsonObject> SerializeAnchorLocal(const FUMGAnchorData& A)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), A.X);
	J->SetNumberField(TEXT("Y"), A.Y);
	return J;
}

static TSharedRef<FJsonObject> SerializeOffsetsLocal(const FUMGOffsetsData& O)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("Left"), O.Left);
	J->SetNumberField(TEXT("Top"), O.Top);
	J->SetNumberField(TEXT("Right"), O.Right);
	J->SetNumberField(TEXT("Bottom"), O.Bottom);
	return J;
}

static TSharedRef<FJsonObject> SerializeAnchorsLocal(const FUMGAnchorsData& A)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Min"), SerializeAnchorLocal(A.Min));
	J->SetObjectField(TEXT("Max"), SerializeAnchorLocal(A.Max));
	return J;
}

static TSharedRef<FJsonObject> SerializeSlotLocal(const FUMGSlotData& S)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Anchors"), SerializeAnchorsLocal(S.Anchors));
	J->SetObjectField(TEXT("Alignment"), SerializeAnchorLocal(S.Alignment));
	J->SetObjectField(TEXT("Offsets"), SerializeOffsetsLocal(S.Offsets));

	const bool bHasPadding =
		!FMath::IsNearlyEqual(S.Padding.Left, 0.0f) ||
		!FMath::IsNearlyEqual(S.Padding.Top, 0.0f) ||
		!FMath::IsNearlyEqual(S.Padding.Right, 0.0f) ||
		!FMath::IsNearlyEqual(S.Padding.Bottom, 0.0f);
	if (bHasPadding)
	{
		J->SetStringField(TEXT("Padding"), FString::Printf(
			TEXT("%.2f,%.2f,%.2f,%.2f"),
			S.Padding.Left, S.Padding.Top, S.Padding.Right, S.Padding.Bottom));
	}

	if (S.HorizontalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("HorizontalAlignment"), S.HorizontalAlignment);
	if (S.VerticalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("VerticalAlignment"), S.VerticalAlignment);
	if (S.SizeRule != TEXT("Auto"))
		J->SetStringField(TEXT("SizeRule"), S.SizeRule);
	if (!FMath::IsNearlyEqual(S.FillWeight, 1.0f))
		J->SetNumberField(TEXT("FillWeight"), S.FillWeight);
	if (S.Row != 0)
		J->SetNumberField(TEXT("Row"), S.Row);
	if (S.Column != 0)
		J->SetNumberField(TEXT("Column"), S.Column);
	if (S.RowSpan != 1)
		J->SetNumberField(TEXT("RowSpan"), S.RowSpan);
	if (S.ColumnSpan != 1)
		J->SetNumberField(TEXT("ColumnSpan"), S.ColumnSpan);

	return J;
}

static TSharedRef<FJsonObject> SerializeBoundsLocal(const FBounds& B)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), B.X);
	J->SetNumberField(TEXT("Y"), B.Y);
	J->SetNumberField(TEXT("W"), B.W);
	J->SetNumberField(TEXT("H"), B.H);
	return J;
}

static TSharedRef<FJsonObject> SerializeWidgetNodeLocal(const FUMGWidgetNode& Node)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("Type"), Node.Type);
	if (!Node.Id.IsEmpty())
		J->SetStringField(TEXT("Id"), Node.Id);
	if (!Node.Name.IsEmpty())
		J->SetStringField(TEXT("Name"), Node.Name);

	const bool bHasBounds =
		!FMath::IsNearlyZero(Node.Bounds.X) || !FMath::IsNearlyZero(Node.Bounds.Y) ||
		!FMath::IsNearlyZero(Node.Bounds.W) || !FMath::IsNearlyZero(Node.Bounds.H);
	if (bHasBounds)
		J->SetObjectField(TEXT("Bounds"), SerializeBoundsLocal(Node.Bounds));

	J->SetObjectField(TEXT("Slot"), SerializeSlotLocal(Node.Slot));

	if (Node.Properties.Num() > 0)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		for (const auto& Pair : Node.Properties)
			Props->SetStringField(Pair.Key, Pair.Value);
		J->SetObjectField(TEXT("Properties"), Props);
	}

	if (Node.Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Node.Children.Num());
		for (const FUMGWidgetNode& Child : Node.Children)
			Arr.Add(MakeShared<FJsonValueObject>(SerializeWidgetNodeLocal(Child)));
		J->SetArrayField(TEXT("Children"), Arr);
	}

	return J;
}

static FString SerializeWidgetDataToJsonLocal(const FUMGWidgetData& Data)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("WidgetName"), Data.WidgetName);
	Root->SetObjectField(TEXT("RootWidget"), SerializeWidgetNodeLocal(Data.RootWidget));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

// ── OpenPreview ──

bool OpenPreview(const FUMGWidgetData& Data, const FString& OutputPath)
{
	if (!FSlateApplication::IsInitialized())
		return false;

	const FString JsonPayload = SerializeWidgetDataToJsonLocal(Data);
	const FString FinalPath = OutputPath.IsEmpty() ? FString(DefaultOutputPath) : OutputPath;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("PenImportPreviewTitle", "PenToUMG Import Preview"))
		.ClientSize(FVector2D(1600.0f, 980.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(true);

	Window->SetContent(
		SNew(SPenToUMGPreviewWindow)
		.OriginalJson(JsonPayload)
		.OutputPath(FinalPath)
		.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
			[](const FString& EditedJson, const FString& ConfirmedPath)
			{
				RunGenerateFromJson(EditedJson, ConfirmedPath);
			}))
		.OnCancelled(FOnPreviewCancelled::CreateLambda(
			[]()
			{
				UE_LOG(LogPenToUMG, Log, TEXT("PenFileImport: Preview cancelled"));
			}))
	);

	FSlateApplication::Get().AddWindow(Window);
	return true;
}

// ── ShowFrameSelector ──

void ShowFrameSelector(const FString& FilePath, const TArray<FPenFrameInfo>& Frames, const FString& OutputPath)
{
	if (!FSlateApplication::IsInitialized())
		return;

	// 构建列表数据（SListView 需要 TSharedPtr）
	TSharedPtr<TArray<TSharedPtr<FPenFrameInfo>>> Items = MakeShared<TArray<TSharedPtr<FPenFrameInfo>>>();
	for (const FPenFrameInfo& F : Frames)
		Items->Add(MakeShared<FPenFrameInfo>(F));

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("FrameSelectorTitle", "Select Frame to Import"))
		.ClientSize(FVector2D(420.0f, 360.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	// 捕获变量供 lambda 使用
	FString CapturedFilePath = FilePath;
	FString CapturedOutputPath = OutputPath;
	TWeakPtr<SWindow> WeakWindow = Window;

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Select one to import:"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SNew(SListView<TSharedPtr<FPenFrameInfo>>)
			.ListItemsSource(Items.Get())
			.OnGenerateRow_Lambda([Items](TSharedPtr<FPenFrameInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
			{
				return SNew(STableRow<TSharedPtr<FPenFrameInfo>>, OwnerTable)
					.Padding(FMargin(4.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s  (%.0f x %.0f)"),
							*Item->Name, Item->Width, Item->Height)))
					];
			})
			.OnMouseButtonDoubleClick_Lambda([CapturedFilePath, CapturedOutputPath, WeakWindow, Items](TSharedPtr<FPenFrameInfo> Item)
			{
				if (Item.IsValid())
				{
					ImportWithTargetNode(CapturedFilePath, Item->Id, CapturedOutputPath);
					if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						Win->RequestDestroyWindow();
				}
			})
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

} // namespace PenImportInternal

#undef LOCTEXT_NAMESPACE
