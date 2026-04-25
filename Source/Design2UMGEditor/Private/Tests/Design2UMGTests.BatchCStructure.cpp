// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FString GetPluginFilePath(const FString& RelativePath)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetBaseDir(), RelativePath)
			: FString();
	}

	bool LoadPluginFile(FAutomationTestBase& Test, const FString& RelativePath, FString& OutContent)
	{
		const FString AbsolutePath = GetPluginFilePath(RelativePath);
		Test.TestFalse(*FString::Printf(TEXT("%s path resolved"), *RelativePath), AbsolutePath.IsEmpty());
		if (AbsolutePath.IsEmpty())
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("%s exists"), *RelativePath), IFileManager::Get().FileExists(*AbsolutePath));
		if (!IFileManager::Get().FileExists(*AbsolutePath))
		{
			return false;
		}

		Test.TestTrue(*FString::Printf(TEXT("Load %s"), *RelativePath), FFileHelper::LoadFileToString(OutContent, *AbsolutePath));
		return !OutContent.IsEmpty();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCConverterOwnerSliceStructureTest,
	"Design2UMG.BatchC.ConverterOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCConverterOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGConverter.cpp"), MainSource))
	{
		return false;
	}

	FString HelperHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/Design2UMGConverterFlowSupport.h"), HelperHeader))
	{
		return false;
	}

	TestTrue(TEXT("Converter main file includes flow support helper"),
		MainSource.Contains(TEXT("#include \"Design2UMGConverterFlowSupport.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Expanded node resolver no longer lives in converter main file"),
		MainSource.Contains(TEXT("struct FExpandedNodeResolver"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Flow child preparation no longer lives in converter main file"),
		MainSource.Contains(TEXT("struct FFlowChildPreparation"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Flow support helper owns expanded-node resolver"),
		HelperHeader.Contains(TEXT("class FExpandedNodeResolver"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Flow support helper owns flow child preparation"),
		HelperHeader.Contains(TEXT("class FFlowChildPreparation"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCFigmaParserOwnerSliceStructureTest,
	"Design2UMG.BatchC.FigmaParserOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCFigmaParserOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/FigmaParserInternal.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/FigmaParserMetadata.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Figma parser main file includes metadata helper"),
		MainSource.Contains(TEXT("#include \"FigmaParserMetadata.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Semantic hint walker no longer lives in parser main file"),
		MainSource.Contains(TEXT("static bool TryReadSemanticHintFromJsonValue("), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Semantic hint application no longer lives in parser main file"),
		MainSource.Contains(TEXT("static void ApplyFigmaSemanticHints("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Metadata helper owns semantic hint application"),
		HelperSource.Contains(TEXT("ApplyFigmaSemanticHints"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Metadata helper owns render fallback capture"),
		HelperSource.Contains(TEXT("CaptureRenderFallbackMetadata"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCVisualMapperOwnerSliceStructureTest,
	"Design2UMG.BatchC.VisualMapperOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCVisualMapperOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/VisualMapper.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Private/VisualMapperFillTransfer.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Visual mapper main file includes fill transfer helper"),
		MainSource.Contains(TEXT("#include \"VisualMapperFillTransfer.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Gradient helpers no longer live in visual mapper main file"),
		MainSource.Contains(TEXT("namespace GradientHelpers"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Multi-fill wrapper construction no longer lives in visual mapper main file"),
		MainSource.Contains(TEXT("MakeMultiFillOverlayShell"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fill transfer helper owns gradient helpers"),
		HelperSource.Contains(TEXT("namespace GradientHelpers"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Fill transfer helper owns multi-fill wrapper"),
		HelperSource.Contains(TEXT("WrapWithMultiFill"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchCTextPropertyOwnerSliceStructureTest,
	"Design2UMG.BatchC.TextPropertyOwnerSliceStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchCTextPropertyOwnerSliceStructureTest::RunTest(const FString& Parameters)
{
	FString MainSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGTextPropertyApplier.cpp"), MainSource))
	{
		return false;
	}

	FString HelperSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGTextStyleApplication.cpp"), HelperSource))
	{
		return false;
	}

	TestTrue(TEXT("Text property applier main file includes text style helper"),
		MainSource.Contains(TEXT("#include \"UMGTextStyleApplication.h\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Typed font helper no longer lives in text property applier main file"),
		MainSource.Contains(TEXT("void ApplyTypedFontInfo("), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Rich text default style helper no longer lives in text property applier main file"),
		MainSource.Contains(TEXT("void ApplyRichTextDefaultStyle("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Text style helper owns typed font helper"),
		HelperSource.Contains(TEXT("void ApplyTypedFontInfo("), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Text style helper owns rich text style rows helper"),
		HelperSource.Contains(TEXT("void ApplyRichTextStyleRows("), ESearchCase::CaseSensitive));
	return true;
}
