// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGFontAccess.h"
#include "Design2UMGConverter.h"
#include "Design2UMGCoreTestNodes.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "FontResolver.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "TextMeasurement.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	UFont* MakeSnapshotTestFont(UPackage* Package, const TCHAR* Name, const TCHAR* FileName)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), FileName))) return nullptr;
		UFont* Font = NewObject<UFont>(Package, Name, RF_Transient);
		Font->FontCacheType = EFontCacheType::Runtime;
		UFontFace* Face = NewObject<UFontFace>(Font);
		Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));
		FTypefaceEntry Entry(TEXT("Regular"));
		Entry.Font = FFontData(Face);
		Design2UMGFontAccess::GetMutableCompositeFont(*Font).DefaultTypeface.Fonts.Add(Entry);
		return Font;
	}

	const FUMGWidgetNode* FindSnapshotWidget(const FUMGWidgetNode& Root, const FString& Id)
	{
		if (Root.Id == Id && (Root.Type == TEXT("TextBlock") || Root.Type == TEXT("RichTextBlock"))) return &Root;
		for (const FUMGWidgetNode& Child : Root.Children)
		{
			if (const FUMGWidgetNode* Found = FindSnapshotWidget(Child, Id)) return Found;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignFontSettingsSnapshotIsolationTest,
	"Design2UMG.Resources.Rasterization.FontSettingsSnapshotIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesignFontSettingsSnapshotIsolationTest::RunTest(const FString&)
{
	UDesign2UMGSettings* Global = UDesign2UMGSettings::Get();
	if (!TestNotNull(TEXT("Editor settings available"), Global)) return false;
	const FDesign2UMGFontMappings SavedMappings = Global->ExplicitFontMappings;
	UPackage* Package = CreatePackage(*(TEXT("/Game/Design2UMG/FontSettingsSnapshot_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UFont> FontA(MakeSnapshotTestFont(Package, TEXT("FontA"), TEXT("Roboto-Regular.ttf")));
	TStrongObjectPtr<UFont> FontB(MakeSnapshotTestFont(Package, TEXT("FontB"), TEXT("DroidSansMono.ttf")));
	ON_SCOPE_EXIT
	{
		Global->ExplicitFontMappings = SavedMappings;
		for (UFont* Font : {FontA.Get(), FontB.Get()})
		{
			if (Font) Font->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
		Package->SetDirtyFlag(false);
		FFontResolver::ClearCache();
	};
	if (!TestNotNull(TEXT("Proportional test font"), FontA.Get()) || !TestNotNull(TEXT("Monospace test font"), FontB.Get())) return false;

	const FString Family = TEXT("D2UFontSettingsSnapshotFamily");
	const FString PostScript = TEXT("D2UFontSettingsSnapshotPS");
	Global->ExplicitFontMappings.Add(Family, FontA.Get());
	Global->ExplicitFontMappings.Add(PostScript, FontA.Get());
	const FDesign2UMGConversionSettings Snapshot = FDesign2UMGConversionSettings::FromSettings(Global);
	using Design2UMGCoreTests::MakeNode;
	TSharedPtr<FDesignNode> Root = MakeNode(TEXT("FontSnapshotRoot"), EDesignNodeType::Frame, FDesignSize::Fixed(600), FDesignSize::Fixed(180));
	Root->LayoutData.Layout = EDesignLayout::Horizontal;
	TSharedPtr<FDesignNode> Plain = MakeNode(TEXT("SnapshotPlain"), EDesignNodeType::Text, FDesignSize::Fit(0), FDesignSize::Fit(0));
	Plain->Text.Content = TEXT("iiii Wide WWW snapshot");
	Plain->Text.FontFamily = Family;
	Plain->Text.FontSize = 24.0f;
	Plain->Text.LineHeight = 1.4f;
	Plain->Text.bSourceHalfLeadingBaseline = true;
	TSharedPtr<FDesignNode> Rich = MakeNode(TEXT("SnapshotRich"), EDesignNodeType::Text, FDesignSize::Fit(0), FDesignSize::Fit(0));
	Rich->Text = Plain->Text;
	FDesignTextRun& Run = Rich->Text.TextRuns.AddDefaulted_GetRef();
	Run.Content = Rich->Text.Content;
	Run.FontPostScriptName = PostScript;
	Root->Children = {Plain, Rich};
	FDesignDocument Document;
	Document.Children.Add(Root);
	const FUMGWidgetData Before = FDesign2UMGConverter::Convert(Document, Snapshot);
	const FVector2D MeasureBefore = TextMeasurement::MeasureTextNodeSize(Plain, 0.0f, &Snapshot.ExplicitFontMappings);
	TestTrue(TEXT("Fixture measures a real font"), MeasureBefore.X > 0 && MeasureBefore.Y > 0);

	Global->ExplicitFontMappings[Family] = FontB.Get();
	Global->ExplicitFontMappings[PostScript] = FontB.Get();
	const FUMGWidgetData After = FDesign2UMGConverter::Convert(Document, Snapshot);
	TestEqual(TEXT("Measurement uses the snapshot, not changed global settings"),
		TextMeasurement::MeasureTextNodeSize(Plain, 0.0f, &Snapshot.ExplicitFontMappings), MeasureBefore);
	const FDesign2UMGConversionSettings Changed = FDesign2UMGConversionSettings::FromSettings(Global);
	TestTrue(TEXT("Different explicit font policy changes measurement, proving the fixture is discriminating"),
		!TextMeasurement::MeasureTextNodeSize(Plain, 0.0f, &Changed.ExplicitFontMappings).Equals(MeasureBefore, 0.01));
	TestEqual(TEXT("Supplied mapping wins over current CDO"),
		FFontResolver::FindFontAssetPath(Family, PostScript, &Snapshot.ExplicitFontMappings), FontA->GetPathName());
	const FDesign2UMGFontMappings Empty;
	TestFalse(TEXT("An explicitly empty snapshot does not read the CDO"), FFontResolver::HasExplicitFontMapping(Family, PostScript, &Empty));
	TestTrue(TEXT("Standalone editor lookup still observes current settings"), FFontResolver::HasExplicitFontMapping(Family, PostScript));
	TestEqual(TEXT("Case-insensitive mapping policy is retained"),
		FFontResolver::FindFontAssetPath(Family.ToLower(), PostScript.ToUpper(), &Snapshot.ExplicitFontMappings), FontA->GetPathName());

	for (const FString& Id : {Plain->Id, Rich->Id})
	{
		const FUMGWidgetNode* A = FindSnapshotWidget(Before.RootWidget, Id);
		const FUMGWidgetNode* B = FindSnapshotWidget(After.RootWidget, Id);
		if (!TestNotNull(TEXT("Original converted text exists"), A) || !TestNotNull(TEXT("Repeated converted text exists"), B)) return false;
		TestEqual(TEXT("Converted font path is stable"), B->Text.FontFamily.Get(FString()), A->Text.FontFamily.Get(FString()));
		TestEqual(TEXT("Converted font is the captured asset"), B->Text.FontFamily.Get(FString()), FontA->GetPathName());
		TestEqual(TEXT("Fit-content width remains stable"), B->Bounds.W, A->Bounds.W);
		TestEqual(TEXT("Fit-content height remains stable"), B->Bounds.H, A->Bounds.H);
		TestEqual(TEXT("Line height remains stable"), B->Text.LineHeightPercentage.Get(0), A->Text.LineHeightPercentage.Get(0));
		TestEqual(TEXT("Rich-text row presence remains stable"), B->Text.RichTextStyleRows.IsSet(), A->Text.RichTextStyleRows.IsSet());
		if (A->Text.RichTextStyleRows.IsSet() && B->Text.RichTextStyleRows.IsSet())
		{
			TestTrue(TEXT("Every rich-text row field remains stable"), B->Text.RichTextStyleRows->OrderIndependentCompareEqual(A->Text.RichTextStyleRows.GetValue()));
		}
	}
	TestEqual(TEXT("Source tree is not mutated by sizing"), Plain->Transform.Width.Value, 0.0f);
	return true;
}
#endif
