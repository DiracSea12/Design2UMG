// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "DesignImportPipeline.h"
#include "PenParser.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	FDesignFill MakeImageFill(const FString& Url)
	{
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Image;
		Fill.Image.Url = Url;
		return Fill;
	}

	TSharedPtr<FDesignNode> MakeNode(const FString& Id, const TArray<FDesignFill>& Fills)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Type = EDesignNodeType::Frame;
		Node->Visual.Fills = Fills;
		return Node;
	}
}

// Pure-function coverage of DesignImportPipeline::AnchorRelativeImageFillUrls: relative urls
// anchored at the .pen directory when the file exists, broken links and non-relative urls kept
// as-is, deep child trees visited.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenRelativeImageFillAnchorTest,
	"Design2UMG.PenParser.RelativeImageFillAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenRelativeImageFillAnchorTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("PenRelAnchor"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(TempRoot, TEXT("assets")), true);

	const FString ExistingPng = FPaths::Combine(TempRoot, TEXT("assets"), TEXT("badge.png"));
	TestTrue(TEXT("Write anchor fixture png"), FFileHelper::SaveStringToFile(TEXT("png-bytes"), *ExistingPng));
	const FString ExistingAbsolutePng = FPaths::ConvertRelativePathToFull(FPaths::Combine(TempRoot, TEXT("elsewhere.png")));
	TestFalse(TEXT("Absolute url fixture is absolute"), FPaths::IsRelative(ExistingAbsolutePng));
	TestTrue(TEXT("Write anchor fixture absolute png"), FFileHelper::SaveStringToFile(TEXT("png-bytes"), *ExistingAbsolutePng));

	FDesignDocument Document;
	const TSharedPtr<FDesignNode> Root = MakeNode(TEXT("root"), { FDesignFill::FromColor(TEXT("#404040")), MakeImageFill(TEXT("assets/badge.png")) });
	Root->Children.Add(MakeNode(TEXT("deep_parent"), {}));
	Root->Children[0]->Children.Add(MakeNode(TEXT("deep_child"), { MakeImageFill(TEXT("assets/badge.png")) }));
	Root->Children.Add(MakeNode(TEXT("missing_link"), { MakeImageFill(TEXT("assets/missing.png")) }));
	Root->Children.Add(MakeNode(TEXT("remote"), { MakeImageFill(TEXT("https://example.com/a.png")) }));
	Root->Children.Add(MakeNode(TEXT("absolute"), { MakeImageFill(ExistingAbsolutePng) }));
	Root->Children.Add(MakeNode(TEXT("empty_url"), { MakeImageFill(TEXT("")) }));
	const TSharedPtr<FDesignNode> OverrideFragment = MakeNode(TEXT("ovr_child"), { MakeImageFill(TEXT("assets/badge.png")) });
	Root->Descendants.FindOrAdd(TEXT("replacement")).Replacement = OverrideFragment;
	FDesignNodePatch& Patch = Root->Descendants.FindOrAdd(TEXT("ovr")).Patch;
	Patch.Fills = TArray<FDesignFill>{MakeImageFill(TEXT("assets/badge.png"))};
	Patch.Children = TArray<TSharedPtr<FDesignNode>>{MakeNode(TEXT("patch_child"), {MakeImageFill(TEXT("assets/badge.png"))})};
	Root->RefOverrides = MakeShared<FDesignNodePatch>(Patch);
	Document.Children.Add(Root);

	const FString PenPath = FPaths::Combine(TempRoot, TEXT("design.pen"));
	DesignImportPipeline::AnchorRelativeImageFillUrls(Document, PenPath);

	const FString ExpectedAnchored = FPaths::ConvertRelativePathToFull(ExistingPng);
	TestTrue(TEXT("Root image fill anchored"), Root->Visual.Fills[1].Image.Url == ExpectedAnchored);
	TestTrue(TEXT("Root color fill untouched"), Root->Visual.Fills[0].IsColor());
	TestTrue(TEXT("Deep child image fill anchored"), Root->Children[0]->Children[0]->Visual.Fills[0].Image.Url == ExpectedAnchored);
	TestTrue(TEXT("Broken link kept as authored"), Root->Children[1]->Visual.Fills[0].Image.Url == TEXT("assets/missing.png"));
	TestTrue(TEXT("Remote url untouched"), Root->Children[2]->Visual.Fills[0].Image.Url == TEXT("https://example.com/a.png"));
	TestTrue(TEXT("Absolute url untouched"), Root->Children[3]->Visual.Fills[0].Image.Url == ExistingAbsolutePng);
	TestTrue(TEXT("Empty url untouched"), Root->Children[4]->Visual.Fills[0].Image.Url.IsEmpty());
	TestTrue(TEXT("Descendant replacement anchored"), OverrideFragment->Visual.Fills[0].Image.Url == ExpectedAnchored);

	TestEqual(TEXT("Descendant patch fill anchored"), Patch.Fills.GetValue()[0].Image.Url, ExpectedAnchored);
	TestEqual(TEXT("Descendant patch child anchored"), Patch.Children.GetValue()[0]->Visual.Fills[0].Image.Url, ExpectedAnchored);
	TestEqual(TEXT("Ref root patch fill anchored"), Root->RefOverrides->Fills.GetValue()[0].Image.Url, ExpectedAnchored);
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

// Integration coverage: LoadPenDocument (both whole-document and node-filtered entries) anchors
// resource paths against the .pen directory even when an image is missing. Resource
// import diagnoses the missing file; parsing does not create it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenRelativeImageFillAnchorLoadTest,
	"Design2UMG.PenParser.RelativeImageFillAnchorLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenRelativeImageFillAnchorLoadTest::RunTest(const FString& Parameters)
{
	const FString TempRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("WorkflowTemp"), TEXT("PenRelAnchorLoad"));
	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(TempRoot, TEXT("assets")), true);

	const FString ExistingPng = FPaths::Combine(TempRoot, TEXT("assets"), TEXT("app-store-badge.png"));
	TestTrue(TEXT("Write load fixture png"), FFileHelper::SaveStringToFile(TEXT("png-bytes"), *ExistingPng));

	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "badge_node",
				"type": "frame",
				"name": "Component/App Store Badge",
				"width": 186,
				"height": 68,
				"fill": [
					{"type": "image", "url": "assets/app-store-badge.png", "mode": "fit"}
				]
			},
			{
				"id": "broken_node",
				"type": "frame",
				"x": 200,
				"width": 64,
				"height": 64,
				"fill": [
					{"type": "image", "url": "assets/focory.png", "mode": "fit"}
				]
			}
		]
	})");
	const FString PenPath = FPaths::Combine(TempRoot, TEXT("design.pen"));
	TestTrue(TEXT("Write load fixture pen"), FFileHelper::SaveStringToFile(Json, *PenPath));

	const FString ExpectedAnchored = FPaths::ConvertRelativePathToFull(ExistingPng);

	FDesignDocument WholeDocument;
	FString ErrorMessage;
	TestTrue(TEXT("Load whole document"), DesignImportPipeline::LoadPenDocument(PenPath, FString(), WholeDocument, &ErrorMessage));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	if (WholeDocument.Children.Num() == 2)
	{
		TestTrue(TEXT("Whole load anchors existing relative url"), WholeDocument.Children[0]->Visual.Fills[0].Image.Url == ExpectedAnchored);
		TestEqual(TEXT("Whole load resolves missing image against the document directory"),
			WholeDocument.Children[1]->Visual.Fills[0].Image.Url,
			FPaths::ConvertRelativePathToFull(FPaths::Combine(TempRoot, TEXT("assets/focory.png"))));
		TestFalse(TEXT("Whole load does not create the missing image"),
			FPaths::FileExists(WholeDocument.Children[1]->Visual.Fills[0].Image.Url));
	}
	else
	{
		AddError(FString::Printf(TEXT("Expected 2 top-level nodes, got %d"), WholeDocument.Children.Num()));
	}

	FDesignDocument FilteredDocument;
	TestTrue(TEXT("Load filtered document"), DesignImportPipeline::LoadPenDocument(PenPath, TEXT("badge_node"), FilteredDocument, &ErrorMessage));
	if (!ErrorMessage.IsEmpty())
	{
		AddInfo(ErrorMessage);
	}
	const FDesignNode* BadgeNode = nullptr;
	for (const TSharedPtr<FDesignNode>& Child : FilteredDocument.Children)
	{
		if (Child.IsValid() && Child->Id == TEXT("badge_node"))
		{
			BadgeNode = Child.Get();
			break;
		}
	}
	if (BadgeNode)
	{
		const bool bBadgeAnchored = BadgeNode->Visual.Fills.Num() > 0 && BadgeNode->Visual.Fills[0].Image.Url == ExpectedAnchored;
		TestTrue(TEXT("Filtered load anchors existing relative url"), bBadgeAnchored);
	}
	else
	{
		AddError(TEXT("Filtered load produced no badge_node"));
	}

	IFileManager::Get().DeleteDirectory(*TempRoot, false, true);
	return true;
}

#endif
