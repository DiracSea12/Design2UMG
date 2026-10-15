// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAutomationTestAdapter.h"
#include "Misc/AutomationTest.h"
#include "UMGJsonParser.h"
#include "UMGSchema.h"
#include "DesignTreeBudget.h"

#if WITH_AUTOMATION_WORKER

// ===========================================================================
// structural all-or-nothing + reuse-output cleanliness + input bounds.
// These tests assert BEHAVIOR and VALUES, not just a boolean false: a failed
// parse must leave the output empty (no partial tree leak) and report a specific
// node/field path so the failed input can never reach resource creation / save.
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserStructuralAllOrNothingTest,
	"Design2UMG.Unit.UMGJsonParser.StructuralAllOrNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserStructuralAllOrNothingTest::RunTest(const FString&)
{
	// A present-but-invalid child (missing Type at index 1) must fail the WHOLE
	// parse; the failing node must never enter the output tree.
	AddExpectedError(TEXT("missing Type field"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("is not a valid object"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("must be an array of objects"), EAutomationExpectedErrorFlags::Contains, 1);

	// Mixed valid + missing-Type child.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"CanvasPanel","Name":"Root","Children":[{"Type":"TextBlock","Name":"Good"},{"Name":"NoType"}]}})");
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Missing-Type child fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Failed parse leaves output empty (no partial leak)"), Data.RootWidget.Type.IsEmpty());
		TestEqual(TEXT("Failed parse leaves no children"), Data.RootWidget.Children.Num(), 0);
		TestTrue(TEXT("Error carries the specific child path"), Error.Contains(TEXT("RootWidget.Children[1]")));
	}

	// Non-object child entry.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"CanvasPanel","Name":"Root","Children":[{"Type":"TextBlock","Name":"Good"}, 42]}})");
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Non-object child fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Failed parse leaves output empty"), Data.RootWidget.Type.IsEmpty());
		TestTrue(TEXT("Error carries the specific child path"), Error.Contains(TEXT("RootWidget.Children[1]")));
	}

	// Deep bad node: a missing-Type leaf four levels down must fail the whole tree
	// and the error must point at the deep path.
	{
		const FString Json = TEXT(R"({
			"RootWidget":{
				"Type":"CanvasPanel","Name":"Root",
				"Children":[{
					"Type":"VerticalBox","Name":"L0",
					"Children":[{
						"Type":"HorizontalBox","Name":"L1",
						"Children":[{
							"Type":"Image","Name":"L2",
							"Children":[{ "Name":"BadLeaf" }]
						}]
					}]
				}]
			}
		})");
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Deep bad node fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Failed parse leaves output empty"), Data.RootWidget.Type.IsEmpty());
		TestTrue(TEXT("Error carries the deep node path"), Error.Contains(TEXT("Children[0].Children[0].Children[0].Children[0]")));
	}

	// Bad Children structure: present but not an array.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"CanvasPanel","Name":"Root","Children":"not-an-array"}})");
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Non-array Children field fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Failed parse leaves output empty"), Data.RootWidget.Type.IsEmpty());
		TestTrue(TEXT("Error reports the malformed Children field"), Error.Contains(TEXT("must be an array")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserReuseCleanlinessTest,
	"Design2UMG.Unit.UMGJsonParser.ReuseCleanliness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserReuseCleanlinessTest::RunTest(const FString&)
{
	// Reuse the SAME output object across two valid parses: the second must fully
	// replace the first (no residue from A).
	const FString JsonA = TEXT(R"({"RootWidget":{"Type":"CanvasPanel","Name":"RootA","Children":[{"Type":"TextBlock","Name":"A1"},{"Type":"Image","Name":"A2"}]},"WidgetName":"WA"})");
	const FString JsonB = TEXT(R"({"RootWidget":{"Type":"VerticalBox","Name":"RootB","Children":[{"Type":"Button","Name":"SecondaryButton"}]},"WidgetName":"WB"})");

	FUMGWidgetData Data;
	FString Error;
	TestTrue(TEXT("First valid parse succeeds"), FUMGJsonParser::ParseJson(JsonA, Data, &Error));
	TestEqual(TEXT("First parse populated the tree"), Data.RootWidget.Children.Num(), 2);
	TestTrue(TEXT("Second valid parse succeeds on reused output"), FUMGJsonParser::ParseJson(JsonB, Data, &Error));
	TestEqual(TEXT("Second parse replaced the widget name"), Data.WidgetName, FString(TEXT("WB")));
	TestEqual(TEXT("Second parse replaced the root type"), Data.RootWidget.Type, FString(TEXT("VerticalBox")));
	TestEqual(TEXT("Second parse left only its own children"), Data.RootWidget.Children.Num(), 1);
	TestEqual(TEXT("No residue from the first parse's children"), Data.RootWidget.Children[0].Name, FString(TEXT("SecondaryButton")));

	// A failed parse followed by a successful parse must clear the stale error.
	AddExpectedError(TEXT("missing Type field"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString JsonInvalid = TEXT(R"({"RootWidget":{"Type":"CanvasPanel","Name":"Root","Children":[{"Name":"NoType"}]}})");
	TestFalse(TEXT("Invalid parse fails"), FUMGJsonParser::ParseJson(JsonInvalid, Data, &Error));
	TestFalse(TEXT("Failed parse populated a stale error"), Error.IsEmpty());
	TestTrue(TEXT("Successful parse then clears the stale error"), FUMGJsonParser::ParseJson(JsonB, Data, &Error));
	TestTrue(TEXT("Error is empty after a successful reuse"), Error.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserNumericBoundariesTest,
	"Design2UMG.Unit.UMGJsonParser.NumericBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserNumericBoundariesTest::RunTest(const FString&)
{
	Design2UMGAutomationTestAdapter::FScopedExpectedMessages ExpectedLogMessages(*this);
	// Large-exponent geometry must be rejected (not narrowed to Inf) while normal
	// negative coordinates are preserved. Bounds fields are optional, so the parse
	// still succeeds; only the offending value is dropped to its default.
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid X"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 1);
	ExpectedLogMessages.Expect(TEXT("FUMGJsonParser: Invalid Phase"), ELogVerbosity::Warning, Design2UMGAutomationTestAdapter::EMessageMatch::Contains, 3);

	// 1e100 is finite as double but past float range -> X is dropped (default 0),
	// while Y=-20 (a negative coordinate) is kept.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Bounds":{"X":1e100,"Y":-20.0,"W":100.0,"H":50.0}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Parse with large-exponent geometry still succeeds"), FUMGJsonParser::ParseJson(Json, Data));
		TestTrue(TEXT("Large-exponent X is not stored as non-finite"), FMath::IsFinite(Data.RootWidget.Bounds.X));
		TestEqual(TEXT("Large-exponent X drops to its default (0)"), Data.RootWidget.Bounds.X, 0.0f);
		TestEqual(TEXT("Negative Y coordinate is preserved"), Data.RootWidget.Bounds.Y, -20.0f);
	}

	// Normal negative coordinates are NOT rejected.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Bounds":{"X":-100.0,"Y":-50.0,"W":200.0,"H":80.0}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Negative coordinates parse successfully"), FUMGJsonParser::ParseJson(Json, Data));
		TestEqual(TEXT("X preserved"), Data.RootWidget.Bounds.X, -100.0f);
		TestEqual(TEXT("Y preserved"), Data.RootWidget.Bounds.Y, -50.0f);
		TestEqual(TEXT("W preserved"), Data.RootWidget.Bounds.W, 200.0f);
		TestEqual(TEXT("H preserved"), Data.RootWidget.Bounds.H, 80.0f);
	}

	// Integer overflow (Phase / PhaseCount past int32) must degrade rather than
	// produce garbage; a valid negative Phase is preserved with its truncation.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Effect":{"Phase":1e100,"PhaseCount":5000000000}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Parse with int overflow still succeeds (degrade, not crash)"), FUMGJsonParser::ParseJson(Json, Data));
		TestFalse(TEXT("Out-of-range Phase/PhaseCount are dropped, not stored"), Data.RootWidget.Effect.RenderingPhase.IsSet());
	}
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Effect":{"Phase":-3,"PhaseCount":4}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Valid Effect parse succeeds"), FUMGJsonParser::ParseJson(Json, Data));
		TestTrue(TEXT("In-range Phase/PhaseCount are preserved"), Data.RootWidget.Effect.RenderingPhase.IsSet());
		if (Data.RootWidget.Effect.RenderingPhase.IsSet())
		{
			TestEqual(TEXT("Negative Phase preserved"), Data.RootWidget.Effect.RenderingPhase->Phase, -3);
			TestEqual(TEXT("Positive PhaseCount preserved"), Data.RootWidget.Effect.RenderingPhase->PhaseCount, 4);
		}
	}

	// Fractional values remain valid when truncation toward zero produces an int32.
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Effect":{"Phase":2147483647.5,"PhaseCount":4}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Phase just past MAX_int32 in the fraction still parses"), FUMGJsonParser::ParseJson(Json, Data));
		TestTrue(TEXT("RenderingPhase 2147483647.5 is kept after truncation"), Data.RootWidget.Effect.RenderingPhase.IsSet());
		if (Data.RootWidget.Effect.RenderingPhase.IsSet())
		{
			TestEqual(TEXT("Phase truncates toward zero to MAX_int32"), Data.RootWidget.Effect.RenderingPhase->Phase, MAX_int32);
		}
	}
	{
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Effect":{"Phase":-2147483648.7,"PhaseCount":4}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Phase just past MIN_int32 in the fraction still parses"), FUMGJsonParser::ParseJson(Json, Data));
		TestTrue(TEXT("RenderingPhase -2147483648.7 is kept after truncation"), Data.RootWidget.Effect.RenderingPhase.IsSet());
		if (Data.RootWidget.Effect.RenderingPhase.IsSet())
		{
			TestEqual(TEXT("Phase truncates toward zero to MIN_int32"), Data.RootWidget.Effect.RenderingPhase->Phase, MIN_int32);
		}
	}
	{
		// Phase only: PhaseCount is deliberately omitted so that "not set" really does
		// mean the Phase value itself was rejected (a valid sibling field would set the
		// whole RenderingPhase and mask the rejection).
		const FString Json = TEXT(R"({"RootWidget":{"Type":"Image","Name":"Root","Effect":{"Phase":2147483648.0}}})");
		FUMGWidgetData Data;
		TestTrue(TEXT("Phase past MAX_int32 after truncation still parses (degrade)"), FUMGJsonParser::ParseJson(Json, Data));
		TestFalse(TEXT("RenderingPhase 2147483648.0 is rejected, not wrapped"), Data.RootWidget.Effect.RenderingPhase.IsSet());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserNormalGenerationNotBudgetRejectedTest,
	"Design2UMG.Unit.UMGJsonParser.NormalGenerationNotBudgetRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserNormalGenerationNotBudgetRejectedTest::RunTest(const FString&)
{
	// A normal-sized generation (1000 nodes, far below the 100000 budget) must
	// parse successfully and not be wrongly rejected by the tree budget.
	constexpr int32 ChildCount = 1000;
	TArray<FString> Children;
	Children.Reserve(ChildCount);
	for (int32 Index = 0; Index < ChildCount; ++Index)
	{
		Children.Add(TEXT("{\"Type\":\"TextBlock\"}"));
	}
	const FString ChildrenJson = FString::Join(Children, TEXT(","));
	const FString Json = FString::Printf(TEXT("{\"RootWidget\":{\"Type\":\"CanvasPanel\",\"Name\":\"Root\",\"Children\":[%s]}}"), *ChildrenJson);

	FUMGWidgetData Data;
	TestTrue(TEXT("Normal-sized generation parses successfully"), FUMGJsonParser::ParseJson(Json, Data));
	TestEqual(TEXT("All generated children are present"), Data.RootWidget.Children.Num(), ChildCount);
	return true;
}

// ===========================================================================
// shared tree-budget boundary (UMG and Pencil use these exact semantics).
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignParserBudgetBoundaryTest,
	"Design2UMG.Unit.ParserBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignParserBudgetBoundaryTest::RunTest(const FString&)
{
	using namespace DesignTreeLimits;

	// Node-count limit: exactly MaxParsedNodeCount entries are accepted, one more
	// trips the budget.
	{
		FDesignParserBudget Budget;
		bool bAllAccepted = true;
		for (int32 Index = 0; Index < MaxParsedNodeCount; ++Index)
		{
			bAllAccepted &= Budget.TryEnterNode(0);
		}
		TestTrue(TEXT("Exactly MaxParsedNodeCount nodes are accepted"), bAllAccepted);
		TestFalse(TEXT("One node past the limit is rejected"), Budget.TryEnterNode(0));
		TestTrue(TEXT("Budget reports exceeded"), Budget.WasExceeded());
	}

	// Depth limit: depth up to MaxNodeDepth is accepted, one past is rejected.
	{
		FDesignParserBudget Budget;
		bool bAllAccepted = true;
		for (int32 Depth = 0; Depth <= MaxNodeDepth; ++Depth)
		{
			bAllAccepted &= Budget.TryEnterNode(Depth);
		}
		TestTrue(TEXT("Depth up to MaxNodeDepth is accepted"), bAllAccepted);
		TestFalse(TEXT("Depth past MaxNodeDepth is rejected"), Budget.TryEnterNode(MaxNodeDepth + 1));
	}

	return true;
}

// ===========================================================================
// the tree budget must be enforced by the REAL parser recursion, not just
// by the budget object in isolation (testing FDesignParserBudget directly cannot
// prove that FUMGJsonParser threads one shared context through every node). These
// drive FUMGJsonParser::ParseJson with trees sitting exactly at and one node past
// each limit, and assert the failure is TOTAL -- empty output, specific error --
// rather than a truncated success.
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserDepthBudgetBoundaryTest,
	"Design2UMG.Unit.UMGJsonParser.TreeDepthBudgetBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserDepthBudgetBoundaryTest::RunTest(const FString&)
{
	using namespace DesignTreeLimits;

	// One chain of nested nodes: the root sits at depth 0, so a chain of
	// (MaxNodeDepth + 1) nodes occupies depths 0..MaxNodeDepth and must parse.
	// One node more reaches depth MaxNodeDepth + 1 and must fail the whole parse.
	const auto MakeChainJson = [](int32 NodeCount)
	{
		FString Json = TEXT("{\"RootWidget\":");
		for (int32 Index = 0; Index < NodeCount; ++Index)
		{
			Json += FString::Printf(TEXT("{\"Type\":\"%s\",\"Name\":\"N%d\",\"Children\":["),
				Index == 0 ? TEXT("CanvasPanel") : TEXT("VerticalBox"), Index);
		}
		for (int32 Index = 0; Index < NodeCount; ++Index)
		{
			Json += TEXT("]}");
		}
		return Json + TEXT("}");
	};

	{
		const FString Json = MakeChainJson(MaxNodeDepth + 1);
		FUMGWidgetData Data;
		TestTrue(TEXT("Depth exactly at the limit parses"), FUMGJsonParser::ParseJson(Json, Data));
	}

	{
		AddExpectedError(TEXT("Design tree budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
		const FString Json = MakeChainJson(MaxNodeDepth + 2);
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Depth one past the limit fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Budget failure leaves the output empty"), Data.RootWidget.Type.IsEmpty());
		TestEqual(TEXT("Budget failure leaves no children"), Data.RootWidget.Children.Num(), 0);
		TestTrue(TEXT("Error names the budget limit"), Error.Contains(TEXT("exceeds the supported budget")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGJsonParserNodeCountBudgetBoundaryTest,
	"Design2UMG.Unit.UMGJsonParser.TreeNodeCountBudgetBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUMGJsonParserNodeCountBudgetBoundaryTest::RunTest(const FString&)
{
	using namespace DesignTreeLimits;

	// Wide-but-shallow: one root plus N siblings, all at depth 1. The root counts as
	// one node, so (MaxParsedNodeCount - 1) siblings fill the budget exactly and one
	// more trips it. This is precisely the shape a per-parent or per-sibling counter
	// would fail to bound.
	const auto MakeWideJson = [](int32 ChildCount)
	{
		TArray<FString> Children;
		Children.Reserve(ChildCount);
		for (int32 Index = 0; Index < ChildCount; ++Index)
		{
			Children.Add(TEXT("{\"Type\":\"TextBlock\"}"));
		}
		return FString::Printf(TEXT("{\"RootWidget\":{\"Type\":\"CanvasPanel\",\"Name\":\"Root\",\"Children\":[%s]}}"),
			*FString::Join(Children, TEXT(",")));
	};

	{
		const FString Json = MakeWideJson(MaxParsedNodeCount - 1);
		FUMGWidgetData Data;
		TestTrue(TEXT("Node count exactly at the limit parses"), FUMGJsonParser::ParseJson(Json, Data));
		TestEqual(TEXT("All children at the limit are present"), Data.RootWidget.Children.Num(), MaxParsedNodeCount - 1);
	}

	{
		AddExpectedError(TEXT("Design tree budget exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
		const FString Json = MakeWideJson(MaxParsedNodeCount);
		FUMGWidgetData Data;
		FString Error;
		TestFalse(TEXT("Node count one past the limit fails the whole parse"), FUMGJsonParser::ParseJson(Json, Data, &Error));
		TestTrue(TEXT("Budget failure leaves the output empty"), Data.RootWidget.Type.IsEmpty());
		TestEqual(TEXT("Budget failure leaves no partial children"), Data.RootWidget.Children.Num(), 0);
		TestTrue(TEXT("Error names the budget limit"), Error.Contains(TEXT("exceeds the supported budget")));
	}

	return true;
}

#endif // WITH_AUTOMATION_WORKER
