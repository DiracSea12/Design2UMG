// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignSchema.h"
#include "Design2UMGLocalFigmaImport.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "Design2UMGTestUtils.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_AUTOMATION_WORKER
using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;

namespace
{
	bool WriteTinyPng(const FString& PngPath)
	{
		static const uint8 TinyPng[] = {
			137,80,78,71,13,10,26,10,
			0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,
			0,0,0,13,73,68,65,84,120,156,99,248,255,255,63,0,5,254,2,254,65,13,16,55,
			0,0,0,0,73,69,78,68,174,66,96,130
		};

		TArray<uint8> Bytes;
		Bytes.Append(TinyPng, UE_ARRAY_COUNT(TinyPng));
		return FFileHelper::SaveArrayToFile(Bytes, *PngPath);
	}

	bool WriteTestPng(const FString& PngPath, const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		if (Pixels.Num() != Width * Height)
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			return false;
		}

		const TArray64<uint8>& Compressed = Wrapper->GetCompressed(100);
		TArray<uint8> Bytes;
		Bytes.Append(Compressed.GetData(), Compressed.Num());
		return FFileHelper::SaveArrayToFile(Bytes, *PngPath);
	}

	bool ReadTestPng(const FString& PngPath, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *PngPath))
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
		{
			return false;
		}

		TArray64<uint8> RawData;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			return false;
		}

		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		OutPixels.SetNumUninitialized(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), OutPixels.Num() * sizeof(FColor));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaTargetNodeAndImageMappingWorkflowTest,
	"Design2UMG.BatchImport.FigmaTargetNodeAndImageMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaTargetNodeAndImageMappingWorkflowTest::RunTest(const FString& Parameters)
{
	const FString FixtureJsonPath = GetFixturePath(TEXT("FigmaComplex.json"));
	if (FixtureJsonPath.IsEmpty() || !FPaths::FileExists(FixtureJsonPath))
	{
		AddWarning(FString::Printf(TEXT("Figma fixture not found, skipping: %s"), *FixtureJsonPath));
		return true;
	}

	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaTargetNode"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	TestTrue(TEXT("Copy figma fixture to temp location"), IFileManager::Get().Copy(*TempJsonPath, *FixtureJsonPath, true, true) == COPY_OK);
	TestTrue(TEXT("Write local avatar image"), WriteTinyPng(FPaths::Combine(TempImagesDir, TEXT("img_avatar_001.png"))));
	TestTrue(TEXT("Write local card image"), WriteTinyPng(FPaths::Combine(TempImagesDir, TEXT("img_card_bg_002.png"))));

	FDesignDocument Document;
	FString ErrorMessage;
	TestTrue(TEXT("Load targeted local Figma document"), Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage, TEXT("50:7")));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}

	TestEqual(TEXT("Targeted document should keep one root child"), Document.Children.Num(), 1);
	if (Document.Children.Num() != 1 || !Document.Children[0].IsValid())
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TestEqual(TEXT("Targeted root id preserved"), Document.Children[0]->Id, TEXT("50:7"));

	TSet<FString> ImageRefs;
	FFigmaParser::CollectImageRefs(Document, ImageRefs);
	TestEqual(TEXT("Filtered node keeps one mapped image ref"), ImageRefs.Num(), 1);
	TestTrue(TEXT("Mapped image ref should become local png path"),
		ImageRefs.Contains(FPaths::ConvertRelativePathToFull(FPaths::Combine(TempImagesDir, TEXT("img_card_bg_002.png")))));

	FUMGWidgetData Data = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
	TestFalse(TEXT("Filtered figma target yields widget name"), Data.WidgetName.IsEmpty());

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalVectorExplicitClearWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalVectorExplicitClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalVectorExplicitClearWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaVectorExplicitClear"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString VectorPngPath = FPaths::Combine(TempImagesDir, TEXT("vec_vector_clear.png"));
	TArray<FColor> Pixels;
	Pixels.Init(FColor::White, 64);
	TestTrue(TEXT("Write local vector image"), WriteTestPng(VectorPngPath, Pixels, 8, 8));

	const FString Json = MakeFigmaDocJson(
		MakeLeafNode(TEXT("vector_clear"), TEXT("Vector"), TEXT("VECTOR"), 0, 0, 8, 8,
			TEXT(",\"fills\":[]")));
	TestTrue(TEXT("Write local vector json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local vector fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0 || !Document.Children[0].IsValid())
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestFalse(TEXT("Injected vector fill clears explicit fill state"), Node->Visual.bFillExplicitClear);
	TestEqual(TEXT("Injected vector has one image fill"), Node->Visual.Fills.Num(), 1);
	if (Node->Visual.Fills.Num() == 1)
	{
		TestTrue(TEXT("Injected vector fill maps to local png"), FPaths::IsSamePath(Node->Visual.Fills[0].Image.Url, VectorPngPath));
	}

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLuminosityBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLuminosityBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLuminosityBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLuminosityBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_lum.png"));
	const TArray<FColor> Pixels = {
		FColor(192, 192, 192, 255),
		FColor(224, 224, 224, 255),
		FColor(128, 128, 128, 128),
		FColor(255, 255, 255, 0),
	};
	TestTrue(TEXT("Write test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("lum_node"), TEXT("LumNode"), TEXT("FRAME"), 0, 0, 120, 80, FString(),
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
					 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.9254902,\"g\":0.9019608,\"b\":0.94117647,\"a\":1}},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"img_lum\",\"scaleMode\":\"FIT\",\"scalingFactor\":0.5}"
					 "]"),
				*MakeBoundsJson(0, 0, 120, 80))));
	TestTrue(TEXT("Write local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma luminosity fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const TSharedPtr<FDesignNode>& Node = Document.Children[0];
	TestTrue(TEXT("Local document keeps both fills"), Node->Visual.Fills.Num() >= 2);
	if (Node->Visual.Fills.Num() < 2)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FDesignFill& ImageFill = Node->Visual.Fills[1];
	TestTrue(TEXT("Luminosity fill is rewritten to local png path"), FPaths::FileExists(ImageFill.Image.Url));
	TestTrue(TEXT("Luminosity fill now points at derived png"), !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(ImageFill.Image.Url), FPaths::ConvertRelativePathToFull(SourcePngPath)));
	TestTrue(TEXT("Luminosity fill blend mode baked to normal"), ImageFill.BlendMode == TEXT("NORMAL"));
	TestTrue(TEXT("Luminosity fill opacity baked into derived png"), FMath::IsNearlyEqual(ImageFill.Image.Opacity, 1.0f));
	TestTrue(TEXT("Luminosity fill scaling factor cleared"), !ImageFill.Image.ScalingFactor.IsSet());

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLuminosityBakeScalingFactorWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLuminosityBakeScalingFactor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLuminosityBakeScalingFactorWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLuminosityBakeScaling"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_lum.png"));
	const TArray<FColor> Pixels = {
		FColor(0, 0, 0, 0),
		FColor(255, 255, 255, 255),
		FColor(64, 128, 255, 255),
		FColor(255, 64, 128, 128),
	};
	TestTrue(TEXT("Write scaling test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = MakeFigmaDocJson(FString::Printf(TEXT("%s,%s"),
		*MakeContainerNode(TEXT("lum_half"), TEXT("LumHalf"), TEXT("FRAME"), 0, 0, 120, 80, FString(),
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
					 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.9254902,\"g\":0.9019608,\"b\":0.94117647,\"a\":1}},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"img_lum\",\"scaleMode\":\"FIT\",\"scalingFactor\":0.5}"
					 "]"),
				*MakeBoundsJson(0, 0, 120, 80))),
		*MakeContainerNode(TEXT("lum_full"), TEXT("LumFull"), TEXT("FRAME"), 140, 0, 120, 80, FString(),
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
					 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.9254902,\"g\":0.9019608,\"b\":0.94117647,\"a\":1}},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"LUMINOSITY\",\"imageRef\":\"img_lum\",\"scaleMode\":\"FIT\",\"scalingFactor\":1.0}"
					 "]"),
				*MakeBoundsJson(140, 0, 120, 80)))));
	TestTrue(TEXT("Write scaling local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma scaling fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() < 2)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FString HalfPath = Document.Children[0]->Visual.Fills.Num() > 1 ? Document.Children[0]->Visual.Fills[1].Image.Url : FString();
	const FString FullPath = Document.Children[1]->Visual.Fills.Num() > 1 ? Document.Children[1]->Visual.Fills[1].Image.Url : FString();
	TestTrue(TEXT("Half-scale derived png exists"), !HalfPath.IsEmpty() && FPaths::FileExists(HalfPath));
	TestTrue(TEXT("Full-scale derived png exists"), !FullPath.IsEmpty() && FPaths::FileExists(FullPath));
	TestTrue(TEXT("FIT luminosity bake should reuse the same derived path"), !HalfPath.IsEmpty() && !FullPath.IsEmpty() && FPaths::IsSamePath(HalfPath, FullPath));
	if (HalfPath.IsEmpty() || FullPath.IsEmpty() || !FPaths::IsSamePath(HalfPath, FullPath))
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TArray<uint8> HalfBytes;
	TArray<uint8> FullBytes;
	TestTrue(TEXT("Load half-scale derived png"), FFileHelper::LoadFileToArray(HalfBytes, *HalfPath));
	TestTrue(TEXT("Load full-scale derived png"), FFileHelper::LoadFileToArray(FullBytes, *FullPath));
	TestTrue(TEXT("FIT luminosity bake should keep identical baked image content"), HalfBytes == FullBytes);

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalBlendModeBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalBlendModeBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalBlendModeBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaBlendModeBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("img_blend.png"));
	const TArray<FColor> Pixels = {
		FColor(128, 128, 128, 255)
	};
	TestTrue(TEXT("Write blend test png"), WriteTestPng(SourcePngPath, Pixels, 1, 1));

	const auto MakeBlendNodeJson = [](const TCHAR* Id, const TCHAR* Name, double X, const TCHAR* BlendMode, const TCHAR* ScaleMode)
	{
		return MakeContainerNode(Id, Name, TEXT("FRAME"), X, 0, 120, 80, FString(),
			FString::Printf(
				TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
					 "{\"type\":\"SOLID\",\"visible\":true,\"blendMode\":\"NORMAL\",\"color\":{\"r\":0.2509804,\"g\":0.2509804,\"b\":0.2509804,\"a\":1}},"
					 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"%s\",\"imageRef\":\"img_blend\",\"scaleMode\":\"%s\"}"
					 "]"),
				*MakeBoundsJson(X, 0, 120, 80), BlendMode, ScaleMode));
	};
	const FString Json = MakeFigmaDocJson(FString::Printf(TEXT("%s,%s,%s,%s"),
		*MakeBlendNodeJson(TEXT("mul_node"), TEXT("Multiply"), 0, TEXT("MULTIPLY"), TEXT("FIT")),
		*MakeBlendNodeJson(TEXT("screen_node"), TEXT("Screen"), 140, TEXT("SCREEN"), TEXT("FIT")),
		*MakeBlendNodeJson(TEXT("overlay_node"), TEXT("Overlay"), 280, TEXT("OVERLAY"), TEXT("FIT")),
		*MakeBlendNodeJson(TEXT("stretch_screen_node"), TEXT("Stretch Screen"), 420, TEXT("SCREEN"), TEXT("STRETCH"))));
	TestTrue(TEXT("Write blend local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma blend fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() < 4)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	auto ValidateBlendNode = [this](const TSharedPtr<FDesignNode>& Node, uint8 ExpectedGray, const TCHAR* Label)
	{
		const FString KeepsFillsLabel = FString::Printf(TEXT("%s keeps both fills"), Label);
		TestTrue(*KeepsFillsLabel, Node->Visual.Fills.Num() >= 2);
		if (Node->Visual.Fills.Num() < 2)
		{
			return false;
		}

		const FDesignFill& ImageFill = Node->Visual.Fills[1];
		const FString DerivedExistsLabel = FString::Printf(TEXT("%s derived png exists"), Label);
		const FString BlendNormalLabel = FString::Printf(TEXT("%s blend mode baked to normal"), Label);
		TestTrue(*DerivedExistsLabel, FPaths::FileExists(ImageFill.Image.Url));
		TestTrue(*BlendNormalLabel, ImageFill.BlendMode == TEXT("NORMAL"));

		TArray<FColor> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		const FString DecodeLabel = FString::Printf(TEXT("%s derived png decodes"), Label);
		TestTrue(*DecodeLabel, ReadTestPng(ImageFill.Image.Url, DecodedPixels, Width, Height));
		if (DecodedPixels.Num() == 0)
		{
			return false;
		}

		const FColor Pixel = DecodedPixels[0];
		const FString PixelLabel = FString::Printf(TEXT("%s blended pixel matches expected gray"), Label);
		return TestTrue(*PixelLabel,
			FMath::Abs(Pixel.R - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.G - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.B - ExpectedGray) <= 2);
	};

	const bool bMultiplyOk = ValidateBlendNode(Document.Children[0], 32, TEXT("Multiply"));
	const bool bScreenOk = ValidateBlendNode(Document.Children[1], 160, TEXT("Screen"));
	const bool bOverlayOk = ValidateBlendNode(Document.Children[2], 64, TEXT("Overlay"));
	const bool bStretchScreenOk = ValidateBlendNode(Document.Children[3], 160, TEXT("Stretch Screen"));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return bMultiplyOk && bScreenOk && bOverlayOk && bStretchScreenOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenLocalBlendModeBakeWorkflowTest,
	"Design2UMG.BatchImport.PenLocalBlendModeBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenLocalBlendModeBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("PenBlendModeBake"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempRoot, true);

	const FString TempPenPath = FPaths::Combine(TempRoot, TEXT("design.pen"));
	const FString SourcePngPath = FPaths::Combine(TempRoot, TEXT("img_blend.png"));
	const TArray<FColor> Pixels = {
		FColor(128, 128, 128, 255)
	};
	TestTrue(TEXT("Write pen blend test png"), WriteTestPng(SourcePngPath, Pixels, 1, 1));

	FString EscapedImagePath = FPaths::ConvertRelativePathToFull(SourcePngPath);
	EscapedImagePath.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	const FString Json = FString::Printf(TEXT(R"({
		"children": [
			{
				"id": "mul_node",
				"type": "frame",
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "MULTIPLY", "url": "%s", "mode": "fit"}
				]
			},
			{
				"id": "screen_node",
				"type": "frame",
				"x": 140,
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "SCREEN", "url": "%s", "mode": "fit"}
				]
			},
			{
				"id": "overlay_node",
				"type": "frame",
				"x": 280,
				"width": 120,
				"height": 80,
				"fill": [
					{"type": "color", "blendMode": "NORMAL", "color": "#404040"},
					{"type": "image", "blendMode": "OVERLAY", "url": "%s", "mode": "fit"}
				]
			}
		]
	})"), *EscapedImagePath, *EscapedImagePath, *EscapedImagePath);
	TestTrue(TEXT("Write pen blend fixture"), FFileHelper::SaveStringToFile(Json, *TempPenPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = DesignImportPipeline::LoadPenDocument(TempPenPath, FString(), Document, &ErrorMessage);
	TestTrue(TEXT("Load pen blend fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	FUMGWidgetData Data = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
	TestTrue(TEXT("Pen blend conversion keeps all top-level nodes"), Document.Children.Num() == 3 && Data.RootWidget.Children.Num() == 3);
	if (Document.Children.Num() < 3)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	const FString FullSourcePath = FPaths::ConvertRelativePathToFull(SourcePngPath);
	auto ValidateBlendNode = [this, &FullSourcePath](const TSharedPtr<FDesignNode>& Node, const uint8 ExpectedGray, const TCHAR* Label)
	{
		const FString KeepsFillsLabel = FString::Printf(TEXT("%s keeps both fills"), Label);
		TestTrue(*KeepsFillsLabel, Node.IsValid() && Node->Visual.Fills.Num() >= 2);
		if (!Node.IsValid() || Node->Visual.Fills.Num() < 2)
		{
			return false;
		}

		const FDesignFill& ImageFill = Node->Visual.Fills[1];
		const FString DerivedExistsLabel = FString::Printf(TEXT("%s derived png exists"), Label);
		const FString DerivedPathLabel = FString::Printf(TEXT("%s derived png differs from source"), Label);
		const FString BlendNormalLabel = FString::Printf(TEXT("%s blend mode baked to normal"), Label);
		const FString OpacityLabel = FString::Printf(TEXT("%s image opacity baked into texture"), Label);
		const FString ScalingFactorLabel = FString::Printf(TEXT("%s scaling factor cleared"), Label);
		TestTrue(*DerivedExistsLabel, FPaths::FileExists(ImageFill.Image.Url));
		TestTrue(*DerivedPathLabel, !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(ImageFill.Image.Url), FullSourcePath));
		TestTrue(*BlendNormalLabel, ImageFill.BlendMode == TEXT("NORMAL"));
		TestTrue(*OpacityLabel, FMath::IsNearlyEqual(ImageFill.Image.Opacity, 1.0f));
		TestTrue(*ScalingFactorLabel, !ImageFill.Image.ScalingFactor.IsSet());

		TArray<FColor> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		const FString DecodeLabel = FString::Printf(TEXT("%s derived png decodes"), Label);
		TestTrue(*DecodeLabel, ReadTestPng(ImageFill.Image.Url, DecodedPixels, Width, Height));
		if (DecodedPixels.Num() == 0)
		{
			return false;
		}

		const FColor Pixel = DecodedPixels[0];
		const FString PixelLabel = FString::Printf(TEXT("%s blended pixel matches expected gray"), Label);
		return TestTrue(*PixelLabel,
			FMath::Abs(Pixel.R - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.G - ExpectedGray) <= 2
			&& FMath::Abs(Pixel.B - ExpectedGray) <= 2);
	};

	const bool bMultiplyOk = ValidateBlendNode(Document.Children[0], 32, TEXT("Pen Multiply"));
	const bool bScreenOk = ValidateBlendNode(Document.Children[1], 160, TEXT("Pen Screen"));
	const bool bOverlayOk = ValidateBlendNode(Document.Children[2], 64, TEXT("Pen Overlay"));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return bMultiplyOk && bScreenOk && bOverlayOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaLocalLayerBlurBakeWorkflowTest,
	"Design2UMG.BatchImport.FigmaLocalLayerBlurBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaLocalLayerBlurBakeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("FigmaLayerBlurBake"));
	const FString TempImagesDir = FPaths::Combine(TempRoot, TEXT("images"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*TempImagesDir, true);

	const FString TempJsonPath = FPaths::Combine(TempRoot, TEXT("design.json"));
	const FString SourcePngPath = FPaths::Combine(TempImagesDir, TEXT("avatar.png"));
	const TArray<FColor> Pixels = {
		FColor(32, 64, 160, 255),
		FColor(64, 128, 224, 255),
		FColor(160, 96, 32, 255),
		FColor(224, 192, 160, 255),
	};
	TestTrue(TEXT("Write blur test png"), WriteTestPng(SourcePngPath, Pixels, 2, 2));

	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("root_frame"), TEXT("RootFrame"), TEXT("FRAME"), 0, 0, 160, 120,
			MakeLeafNode(TEXT("blur_avatar"), TEXT("BlurAvatar"), TEXT("ELLIPSE"), 24, 20, 26, 26,
				FString::Printf(
					TEXT(",\"absoluteRenderBounds\":%s,\"fills\":["
						 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"NORMAL\",\"imageRef\":\"avatar\",\"scaleMode\":\"FIT\"},"
						 "{\"type\":\"IMAGE\",\"visible\":true,\"blendMode\":\"NORMAL\",\"imageRef\":\"avatar\",\"scaleMode\":\"FILL\"}"
						 "],\"effects\":[{\"type\":\"LAYER_BLUR\",\"visible\":true,\"blendMode\":\"NORMAL\",\"radius\":21.75}]"),
					*MakeBoundsJson(2.25, -1.75, 69.5, 69.5))),
			FString::Printf(TEXT(",\"absoluteRenderBounds\":%s"), *MakeBoundsJson(0, 0, 160, 120))));
	TestTrue(TEXT("Write blur local figma json"), FFileHelper::SaveStringToFile(Json, *TempJsonPath));

	FDesignDocument Document;
	FString ErrorMessage;
	const bool bLoaded = Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(TempJsonPath, Document, ErrorMessage);
	TestTrue(TEXT("Load local figma blur fixture"), bLoaded);
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (!bLoaded || Document.Children.Num() == 0)
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TFunction<TSharedPtr<FDesignNode>(const TArray<TSharedPtr<FDesignNode>>&)> FindBlurAvatar =
		[&FindBlurAvatar](const TArray<TSharedPtr<FDesignNode>>& Nodes) -> TSharedPtr<FDesignNode>
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->Id == TEXT("blur_avatar"))
			{
				return Node;
			}
			if (TSharedPtr<FDesignNode> Found = FindBlurAvatar(Node->Children))
			{
				return Found;
			}
		}
		return nullptr;
	};

	const TSharedPtr<FDesignNode> Avatar = FindBlurAvatar(Document.Children);
	TestTrue(TEXT("Find blur avatar node"), Avatar.IsValid());
	if (!Avatar.IsValid())
	{
		IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
		return false;
	}

	TestTrue(TEXT("Blur avatar keeps both fills"), Avatar->Visual.Fills.Num() == 2);
	TestTrue(TEXT("Blur avatar width stays in layout space"), FMath::IsNearlyEqual(Avatar->Transform.Width.Value, 26.0f, 0.01f));
	TestTrue(TEXT("Blur avatar height stays in layout space"), FMath::IsNearlyEqual(Avatar->Transform.Height.Value, 26.0f, 0.01f));
	TestTrue(TEXT("Blur avatar X stays in layout space"), FMath::IsNearlyEqual(Avatar->Transform.X, 24.0f, 0.01f));
	TestTrue(TEXT("Blur avatar Y stays in layout space"), FMath::IsNearlyEqual(Avatar->Transform.Y, 20.0f, 0.01f));
	TestTrue(TEXT("Blur effect should be baked out"), Avatar->Visual.Effects.Num() == 0);
	TestTrue(TEXT("First blurred fill rewrites to derived png"), !Avatar->Visual.Fills[0].Image.Url.IsEmpty() && FPaths::FileExists(Avatar->Visual.Fills[0].Image.Url));
	TestTrue(TEXT("Second blurred fill rewrites to derived png"), !Avatar->Visual.Fills[1].Image.Url.IsEmpty() && FPaths::FileExists(Avatar->Visual.Fills[1].Image.Url));
	TestTrue(TEXT("Blur bake should not reuse original source path"), !FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Avatar->Visual.Fills[0].Image.Url), FPaths::ConvertRelativePathToFull(SourcePngPath)));

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

#endif // WITH_AUTOMATION_WORKER
