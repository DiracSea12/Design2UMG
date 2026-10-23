// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/OutputDevice.h"
#include "DesignJsonUtils.h"
#include "FigmaParser.h"
#include "PenParser.h"
#include "ParserSharedSemantics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
	class FScopedExpectedWarning final : public FOutputDevice
	{
	public:
		FScopedExpectedWarning(FAutomationTestBase& InTest, FString InText, const int32 InOccurrences)
			: Test(InTest), Text(MoveTemp(InText)), Occurrences(InOccurrences)
		{
			Test.AddExpectedError(FString::Printf(TEXT("(?i:%s)"), *Text), EAutomationExpectedErrorFlags::Contains, Occurrences);
			if (GLog) GLog->AddOutputDevice(this);
		}
		~FScopedExpectedWarning()
		{
			if (GLog) GLog->RemoveOutputDevice(this);
			Test.TestEqual(FString::Printf(TEXT("Expected Warning matching '%s'"), *Text), ActualOccurrences, Occurrences);
		}
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName&) override
		{
			const ELogVerbosity::Type MessageVerbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (V && MessageVerbosity == ELogVerbosity::Warning && FString(V).Contains(Text, ESearchCase::IgnoreCase))
			{
				++ActualOccurrences;
			}
		}
	private:
		FAutomationTestBase& Test;
		FString Text;
		int32 Occurrences = 0;
		int32 ActualOccurrences = 0;
	};
#endif

	FString FormatTransform(const FDesignImageTransform& Value)
	{
		return FString::Printf(TEXT("%g,%g,%g,%g,%g,%g"), Value.A, Value.B, Value.TX, Value.C, Value.D, Value.TY);
	}

	FString BuildMatrix(const TArray<FString>& Components)
	{
		return FString::Printf(TEXT("[[%s,%s,%s],[%s,%s,%s]]"), *Components[0], *Components[1], *Components[2], *Components[3], *Components[4], *Components[5]);
	}

	void CheckSourceMatrix(FAutomationTestBase& Test, bool bFigma, const FString& Matrix, const FString& Expected)
	{
		const FString Json = bFigma
			? FString::Printf(TEXT(R"({"document":{"id":"document","type":"DOCUMENT","children":[{"id":"page","type":"CANVAS","children":[{"id":"image","name":"Image","type":"RECTANGLE","absoluteBoundingBox":{"x":0,"y":0,"width":100,"height":50},"fills":[{"type":"IMAGE","imageRef":"image-resource","scaleMode":"CROP","imageTransform":%s}]}]}]}})"), *Matrix)
			: FString::Printf(TEXT(R"({"version":"2.14","children":[{"id":"image","type":"rectangle","width":100,"height":50,"fill":{"type":"image","url":"image.png","mode":"crop","imageTransform":%s}}]})"), *Matrix);

		FDesignDocument Document;
		Test.TestTrue(TEXT("Optional invalid transform does not reject the document"), bFigma ? FFigmaParser::ParseDocument(Json, Document) : FPenParser::ParseDocument(Json, Document));
		if (!Test.TestEqual(TEXT("One image node parsed"), Document.Children.Num(), 1))
		{
			return;
		}
		const FDesignNode& Node = *Document.Children[0];
		if (!Test.TestEqual(TEXT("Image fill survives"), Node.Visual.Fills.Num(), 1))
		{
			return;
		}
		const FDesignFill& Fill = Node.Visual.Fills[0];
		Test.TestTrue(TEXT("Fill remains an image"), Fill.Type == EDesignFillType::Image);
		Test.TestEqual(FString::Printf(TEXT("%s matrix %s"), bFigma ? TEXT("Figma") : TEXT("Pencil"), *Matrix), FormatTransform(Fill.Image.Transform), Expected);
	}

	void CheckSourceMatrices(FAutomationTestBase& Test, bool bFigma)
	{
		CheckSourceMatrix(Test, bFigma, TEXT("[[2,0.25,3],[0.5,5,6]]"), TEXT("2,0.25,3,0.5,5,6"));
		CheckSourceMatrix(Test, bFigma, TEXT("[[0,0,0],[0,0,0]]"), TEXT("0,0,0,0,0,0"));
		const TCHAR* InvalidComponents[] = { TEXT("\"bad\""), TEXT("true"), TEXT("null"), TEXT("{}"), TEXT("[]"), TEXT("1e39"), TEXT("\"1e+\""), TEXT("\"NaN\"") };
		const TCHAR* InvalidShapes[] = { TEXT("null"), TEXT("false"), TEXT("{}"), TEXT("\"matrix\""), TEXT("[]"), TEXT("[null,[0,1,0]]"), TEXT("[[1,0],[0,1,0]]"), TEXT("[[1,0,0],[0,1,0,2]]"), TEXT("[[1,0,0],[0,1,0],[0,0,1]]") };
		const FString ExpectedWarning = bFigma ? TEXT("FigmaParser: Invalid imageTransform") : TEXT("PenParser: Invalid imageTransform");
		const int32 ExpectedWarningCount = UE_ARRAY_COUNT(InvalidComponents) * 6 + UE_ARRAY_COUNT(InvalidShapes);
#if UE_VERSION_OLDER_THAN(5, 2, 0)
		FScopedExpectedWarning WarningScope(Test, ExpectedWarning, ExpectedWarningCount);
#elif UE_VERSION_OLDER_THAN(5, 4, 0)
		Test.AddExpectedMessage(ExpectedWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, ExpectedWarningCount);
#else
		Test.AddExpectedMessagePlain(ExpectedWarning, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, ExpectedWarningCount);
#endif
		for (const TCHAR* Invalid : InvalidComponents)
		{
			for (int32 Index = 0; Index < 6; ++Index)
			{
				TArray<FString> Components{ TEXT("2"), TEXT("3"), TEXT("5"), TEXT("7"), TEXT("11"), TEXT("13") };
				Components[Index] = Invalid;
				CheckSourceMatrix(Test, bFigma, BuildMatrix(Components), TEXT("1,0,0,0,1,0"));
			}
		}
		for (const TCHAR* Invalid : InvalidShapes)
		{
			CheckSourceMatrix(Test, bFigma, Invalid, TEXT("1,0,0,0,1,0"));
		}
		if (!bFigma)
		{
			CheckSourceMatrix(Test, false, TEXT("[[\"+2.5e0\",0,\"-.25\"],[0,1,0]]"), TEXT("2.5,0,-0.25,0,1,0"));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageMatrixAtomicParsingTest,
	"Design2UMG.NumericParsing.SourceMatrixAtomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageMatrixAtomicParsingTest::RunTest(const FString& Parameters)
{
	FDesignImageTransform Expected;
	Expected.A = 7; Expected.B = 11; Expected.TX = 13;
	Expected.C = 17; Expected.D = 19; Expected.TY = 23;
	for (const TCHAR* Input : { TEXT("[[2,3,5],[7,11,\"bad\"]]"), TEXT("[[2,3,1e39],[7,11,13]]"), TEXT("[null,[7,11,13]]"), TEXT("[[2,3],[7,11,13]]") })
	{
		TArray<TSharedPtr<FJsonValue>> Matrix;
		TestTrue(TEXT("Test matrix JSON loads"), DesignJsonUtils::TryParseArray(Input, Matrix));
		FDesignImageTransform Actual = Expected;
		TestFalse(TEXT("Matrix success requires every finite numeric component"), ParserSharedSemantics::TryParseImageTransformMatrix(Matrix,
			[](const TSharedPtr<FJsonValue>& Value, float& Out) { return Value->Type == EJson::Number && Value->TryGetNumber(Out); }, Actual));
		TestEqual(TEXT("Rejected matrix leaves every output component unchanged"), FormatTransform(Actual), FormatTransform(Expected));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenImageMatrixParsingTest,
	"Design2UMG.NumericParsing.PenImageMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenImageMatrixParsingTest::RunTest(const FString& Parameters)
{
	CheckSourceMatrices(*this, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaImageMatrixParsingTest,
	"Design2UMG.NumericParsing.FigmaImageMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaImageMatrixParsingTest::RunTest(const FString& Parameters)
{
	CheckSourceMatrices(*this, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSourceSlotMetadataTest,
	"Design2UMG.Unit.ParserShared.SourceSlotMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSourceSlotMetadataTest::RunTest(const FString&)
{
	for (const int32 Count : {0, 2})
	{
		FDesignNode Node;
		ParserSharedSemantics::AddSourceSlotMetadata(Node, Count);
		TestEqual(TEXT("Slot presence is preserved"), Node.Metadata.FindRef(TEXT("source.slot.present")), FString(TEXT("true")));
		TestEqual(TEXT("The caller's count is preserved"), Node.Metadata.FindRef(TEXT("source.slot.count")), FString::FromInt(Count));
		TestEqual(TEXT("Only an empty slot receives the empty hint"), Node.Metadata.Contains(TEXT("source.slot.empty")), Count == 0);
	}
	return true;
}

#endif
