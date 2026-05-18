// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"
#include "DesignImportUtils.h"
#include "Design2UMGNotifications.h"
#include "FigmaHttpClient.h"
#include "MCPHttpClient.h"
#include "Tests/HttpClientAutomationHooks.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Misc/AutomationTest.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "UMGGeneratorRootHandoff.h"
#include "WidgetBlueprint.h"

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

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

	FString ExtractBuildDependencySection(const FString& Source, const TCHAR* SectionName)
	{
		const int32 SectionStart = Source.Find(SectionName, ESearchCase::CaseSensitive);
		if (SectionStart == INDEX_NONE)
		{
			return FString();
		}

		const TCHAR* NextSectionName =
			FCString::Strstr(SectionName, TEXT("Public")) ? TEXT("PrivateDependencyModuleNames.AddRange") : nullptr;
		const int32 NextSectionStart = NextSectionName
			? Source.Find(NextSectionName, ESearchCase::CaseSensitive, ESearchDir::FromStart, SectionStart + 1)
			: INDEX_NONE;
		if (NextSectionStart == INDEX_NONE)
		{
			return Source.Mid(SectionStart);
		}

		return Source.Mid(SectionStart, NextSectionStart - SectionStart);
	}

	FUMGWidgetData MakeCancelBoundaryWidgetData(const FString& WidgetName)
	{
		FUMGWidgetData Data;
		Data.WidgetName = WidgetName;
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("cancel_root");
		Data.RootWidget.Name = TEXT("CancelRoot");
		Data.RootWidget.Bounds.W = 96.0f;
		Data.RootWidget.Bounds.H = 64.0f;
		return Data;
	}

	struct FScopedGenerationCancelPoint
	{
		explicit FScopedGenerationCancelPoint(const DesignImportUtils::EAutomationGenerationCancelPoint CancelPoint)
		{
			DesignImportUtils::SetAutomationGenerationCancelPoint(CancelPoint);
		}

		~FScopedGenerationCancelPoint()
		{
			DesignImportUtils::ResetAutomationGenerationCancelPoint();
		}
	};

	struct FScopedNotificationCapture
	{
		TArray<FString> Warnings;
		TArray<FString> Successes;

		FScopedNotificationCapture()
		{
			FDesign2UMGNotification::RegisterWarningHandler(
				FDesign2UMGMessageNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnWarning));
			FDesign2UMGNotification::RegisterSuccessHandler(
				FDesign2UMGSuccessNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnSuccess));
		}

		~FScopedNotificationCapture()
		{
			FDesign2UMGNotification::ResetHandlers();
		}

		void OnWarning(const FText& Message)
		{
			Warnings.Add(Message.ToString());
		}

		void OnSuccess(const FText& Message, const FString&)
		{
			Successes.Add(Message.ToString());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDContributorFacingStaticBoundaryTest,
	"Design2UMG.BatchD.ContributorFacingStaticBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDContributorFacingStaticBoundaryTest::RunTest(const FString& Parameters)
{
	FString EditorBuildRules;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Design2UMGEditor.Build.cs"), EditorBuildRules))
	{
		return false;
	}

	FString SchemaFoundationHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGCore/Public/UMGSchemaFoundation.h"), SchemaFoundationHeader))
	{
		return false;
	}

	FString PluginDescriptor;
	if (!LoadPluginFile(*this, TEXT("Design2UMG.uplugin"), PluginDescriptor))
	{
		return false;
	}

	FString ArchitectureDoc;
	if (!LoadPluginFile(*this, TEXT("Docs/Architecture.md"), ArchitectureDoc))
	{
		return false;
	}

	const FString PublicDependencies = ExtractBuildDependencySection(EditorBuildRules, TEXT("PublicDependencyModuleNames.AddRange"));
	const FString PrivateDependencies = ExtractBuildDependencySection(EditorBuildRules, TEXT("PrivateDependencyModuleNames.AddRange"));
	TestTrue(TEXT("Editor Build.cs documents why public dependencies remain public"),
		EditorBuildRules.Contains(TEXT("Public dependencies are limited"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Editor-only asset/build modules stay out of the public dependency surface"),
		PublicDependencies.Contains(TEXT("\"UnrealEd\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"AssetTools\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"HTTP\""), ESearchCase::CaseSensitive)
		|| PublicDependencies.Contains(TEXT("\"Networking\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Editor-only asset/build modules stay private"),
		PrivateDependencies.Contains(TEXT("\"UnrealEd\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"AssetTools\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"HTTP\""), ESearchCase::CaseSensitive)
		&& PrivateDependencies.Contains(TEXT("\"Networking\""), ESearchCase::CaseSensitive));
	TestTrue(TEXT("FBounds is now named by design pixel semantics"),
		SchemaFoundationHeader.Contains(TEXT("struct FDesignPixelBounds"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Generic FBounds type name is not left on the public schema surface"),
		SchemaFoundationHeader.Contains(TEXT("struct FBounds"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Plugin descriptor states the stack is editor-only"),
		PluginDescriptor.Contains(TEXT("editor-only"), ESearchCase::IgnoreCase));
	TestTrue(TEXT("Architecture docs state Design2UMGCore is an editor module, not runtime"),
		ArchitectureDoc.Contains(TEXT("editor module"), ESearchCase::IgnoreCase)
		&& ArchitectureDoc.Contains(TEXT("not a runtime module"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDUMGGeneratorFailureLogStaticTest,
	"Design2UMG.BatchD.UMGGeneratorFailureLogStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDUMGGeneratorFailureLogStaticTest::RunTest(const FString& Parameters)
{
	FString GeneratorSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGGenerator.cpp"), GeneratorSource))
	{
		return false;
	}

	TestFalse(TEXT("Unique-name failure path no longer emits duplicate verbose failure log"),
		GeneratorSource.Contains(TEXT("Failed to find unique name for asset"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Package creation failure path no longer emits duplicate verbose failure log"),
		GeneratorSource.Contains(TEXT("Failed to create package at"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("WidgetBlueprint creation failure path no longer emits duplicate verbose failure log"),
		GeneratorSource.Contains(TEXT("Failed to create Widget Blueprint\""), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Root creation failure path no longer emits duplicate verbose failure log"),
		GeneratorSource.Contains(TEXT("Failed to create root widget for '%s'"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Root creation error keeps package, blueprint, and root context"),
		GeneratorSource.Contains(TEXT("WidgetBlueprint '%s'"), ESearchCase::CaseSensitive)
		&& GeneratorSource.Contains(TEXT("root type '%s'"), ESearchCase::CaseSensitive)
		&& GeneratorSource.Contains(TEXT("root name '%s'"), ESearchCase::CaseSensitive)
		&& GeneratorSource.Contains(TEXT("root id '%s'"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDUMGGeneratorRootHandoffSmokeTest,
	"Design2UMG.BatchD.UMGGeneratorRootHandoffSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDUMGGeneratorRootHandoffSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("root");
	WidgetData.RootWidget.Name = TEXT("Root");
	WidgetData.RootWidget.Bounds.W = 320.0f;
	WidgetData.RootWidget.Bounds.H = 180.0f;

	const UMGGeneratorRootHandoff::FPreparedRoot Prepared =
		UMGGeneratorRootHandoff::PrepareRootForBuild(WidgetData, TEXT("/Game/D2U_Test"), TEXT("WBP_D2U_Test"));

	TestEqual(TEXT("Root handoff keeps the root widget type"), Prepared.Root.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Root handoff derives design size from valid root bounds"), Prepared.DesignSize, FIntPoint(320, 180));
	TestEqual(TEXT("Root handoff preserves root id"), Prepared.Root.Id, FString(TEXT("root")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDImportPlanningSmokeTest,
	"Design2UMG.BatchD.ImportPlanningSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDImportPlanningSmokeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.WidgetName = TEXT("WBP_ImportPlanningSmoke");
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("planning_root");
	WidgetData.RootWidget.Name = TEXT("PlanningRoot");
	WidgetData.RootWidget.Bounds.W = 256.0f;
	WidgetData.RootWidget.Bounds.H = 128.0f;

	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportAnalysisResult Result = DesignImportPlanning::AnalyzeWidgetData(WidgetData, Input);

	TestEqual(TEXT("Import planning copies widget name"), Result.Data.WidgetName, WidgetData.WidgetName);
	TestEqual(TEXT("Import planning copies root type"), Result.Data.RootWidget.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Import planning copies root bounds width"), Result.Data.RootWidget.Bounds.W, 256.0f);
	TestTrue(TEXT("Import planning without image resolution produces no image report"), Result.ImageReport.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDFigmaDialogEmptyInputStaticTest,
	"Design2UMG.BatchD.FigmaDialogEmptyInputStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDFigmaDialogEmptyInputStaticTest::RunTest(const FString& Parameters)
{
	FString MenuOwnerSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/Design2UMGEditorMenuOwner.cpp"), MenuOwnerSource))
	{
		return false;
	}

	const int32 EmptyInputBranch = MenuOwnerSource.Find(TEXT("if (Url.IsEmpty() || Token.IsEmpty())"), ESearchCase::CaseSensitive);
	TestTrue(TEXT("Figma dialog keeps an explicit empty URL/token branch"), EmptyInputBranch != INDEX_NONE);
	if (EmptyInputBranch == INDEX_NONE)
	{
		return false;
	}

	const int32 DestroyWindowCall = MenuOwnerSource.Find(TEXT("RequestDestroyWindow"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EmptyInputBranch);
	const int32 ImportCall = MenuOwnerSource.Find(TEXT("RunFigmaImport(Url, Token)"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EmptyInputBranch);
	const int32 WarningCall = MenuOwnerSource.Find(TEXT("FDesign2UMGNotification::Warning"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EmptyInputBranch);
	const int32 EmptyInputReturn = WarningCall != INDEX_NONE
		? MenuOwnerSource.Find(TEXT("return FReply::Handled();"), ESearchCase::CaseSensitive, ESearchDir::FromStart, WarningCall)
		: INDEX_NONE;

	TestTrue(TEXT("Figma dialog warns the user for empty URL/token"), WarningCall != INDEX_NONE);
	TestTrue(TEXT("Figma dialog empty-input warning happens before closing the window"),
		WarningCall != INDEX_NONE && DestroyWindowCall != INDEX_NONE && WarningCall < DestroyWindowCall);
	TestTrue(TEXT("Figma dialog empty-input warning happens before import starts"),
		WarningCall != INDEX_NONE && ImportCall != INDEX_NONE && WarningCall < ImportCall);
	TestTrue(TEXT("Figma dialog empty-input branch returns before closing or importing"),
		EmptyInputReturn != INDEX_NONE
		&& DestroyWindowCall != INDEX_NONE
		&& ImportCall != INDEX_NONE
		&& EmptyInputReturn < DestroyWindowCall
		&& EmptyInputReturn < ImportCall);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDGenerationTransactionBoundaryStaticTest,
	"Design2UMG.BatchD.GenerationTransactionBoundaryStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDGenerationTransactionBoundaryStaticTest::RunTest(const FString& Parameters)
{
	FString ImportUtilsSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/DesignImportUtils.cpp"), ImportUtilsSource))
	{
		return false;
	}

	FString GeneratorSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/UMGGenerator.cpp"), GeneratorSource))
	{
		return false;
	}

	TestTrue(TEXT("Interactive generation owns one scoped transaction at DesignImportUtils boundary"),
		ImportUtilsSource.Contains(TEXT("FInteractiveGenerationScope"), ESearchCase::CaseSensitive)
		&& ImportUtilsSource.Contains(TEXT("FScopedTransaction"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Interactive progress is gated away from unattended automation"),
		ImportUtilsSource.Contains(TEXT("!FApp::IsUnattended()"), ESearchCase::CaseSensitive)
		&& ImportUtilsSource.Contains(TEXT("FScopedSlowTask"), ESearchCase::CaseSensitive));
	TestFalse(TEXT("Low-level generator does not create nested top-level transactions"),
		GeneratorSource.Contains(TEXT("FScopedTransaction"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Low-level generator marks mutated Blueprint objects for the outer transaction"),
		GeneratorSource.Contains(TEXT("WBP->Modify()"), ESearchCase::CaseSensitive)
		&& GeneratorSource.Contains(TEXT("WBP->WidgetTree->Modify()"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDFigmaHttpCancellationStaticTest,
	"Design2UMG.BatchD.FigmaHttpCancellationStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDFigmaHttpCancellationStaticTest::RunTest(const FString& Parameters)
{
	FString FigmaHttpHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Public/FigmaHttpClient.h"), FigmaHttpHeader))
	{
		return false;
	}

	FString OrchestratorSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/Design2UMGEditorImportOrchestrator.cpp"), OrchestratorSource))
	{
		return false;
	}

	TestTrue(TEXT("Figma HTTP client exposes active request cancellation"),
		FigmaHttpHeader.Contains(TEXT("CancelActiveRequest()"), ESearchCase::CaseSensitive)
		&& FigmaHttpHeader.Contains(TEXT("CancelRequests()"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Figma import session cancellation reaches the HTTP client"),
		OrchestratorSource.Contains(TEXT("FFigmaImportSession::CancelRequests()"), ESearchCase::CaseSensitive)
		&& OrchestratorSource.Contains(TEXT("Client->CancelRequests()"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Figma import service invalidation cancels active sessions"),
		OrchestratorSource.Contains(TEXT("ActiveSession.Pin()->CancelRequests()"), ESearchCase::CaseSensitive)
		&& OrchestratorSource.Contains(TEXT("FigmaService.Invalidate()"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpCancellationStaticTest,
	"Design2UMG.BatchD.MCPHttpCancellationStatic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpCancellationStaticTest::RunTest(const FString& Parameters)
{
	FString MCPHttpHeader;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Public/MCPHttpClient.h"), MCPHttpHeader))
	{
		return false;
	}

	FString MCPHttpSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/MCPHttpClient.cpp"), MCPHttpSource))
	{
		return false;
	}

	FString OrchestratorSource;
	if (!LoadPluginFile(*this, TEXT("Source/Design2UMGEditor/Private/Design2UMGEditorImportOrchestrator.cpp"), OrchestratorSource))
	{
		return false;
	}

	TestTrue(TEXT("MCP HTTP client exposes active request cancellation"),
		MCPHttpHeader.Contains(TEXT("CancelActiveRequest()"), ESearchCase::CaseSensitive)
		&& MCPHttpHeader.Contains(TEXT("CancelRequests()"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("MCP disconnect cancels active async requests and invalidates the session"),
		MCPHttpSource.Contains(TEXT("FMCPHttpClient::Disconnect()"), ESearchCase::CaseSensitive)
		&& MCPHttpSource.Contains(TEXT("CancelRequests();"), ESearchCase::CaseSensitive)
		&& MCPHttpSource.Contains(TEXT("SessionId.Empty();"), ESearchCase::CaseSensitive));
	TestTrue(TEXT("Pencil shutdown reaches MCP request cancellation"),
		OrchestratorSource.Contains(TEXT("Connector->GetClient().CancelRequests()"), ESearchCase::CaseSensitive)
		&& OrchestratorSource.Contains(TEXT("Connector->Shutdown()"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDFigmaHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.FigmaHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDFigmaHttpCancellationBehaviorTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		int32 StartCount = 0;
		int32 CancelCount = 0;
		TFunction<void(bool, const FString&, int32)> Completion;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FFigmaHttpClientAutomationHooks> Hooks = MakeShared<FFigmaHttpClientAutomationHooks>();
	Hooks->StartRequest =
		[HookState](const FString&, const FString&, int32, TFunction<void(bool, const FString&, int32)> OnComplete)
		{
			++HookState->StartCount;
			HookState->Completion = MoveTemp(OnComplete);
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32)
		{
			++HookState->CancelCount;
		};

	SetFigmaHttpClientAutomationHooks(Hooks);

	bool bCallbackCalled = false;
	{
		FFigmaHttpClient Client;
		Client.SetToken(TEXT("phase24-token"));
		Client.GetFile(TEXT("phase24-file"), [&bCallbackCalled](bool, const FString&)
		{
			bCallbackCalled = true;
		});

		TestEqual(TEXT("Figma behavior hook started one request"), HookState->StartCount, 1);
		Client.CancelRequests();
		TestEqual(TEXT("Figma behavior hook observed one cancellation"), HookState->CancelCount, 1);
		TestTrue(TEXT("Figma behavior hook captured completion"), static_cast<bool>(HookState->Completion));

		HookState->Completion(false, TEXT(""), 0);
		TestFalse(TEXT("Cancelled Figma request suppresses late callback"), bCallbackCalled);
	}

	ResetFigmaHttpClientAutomationHooks();

	TSharedRef<FFigmaHttpClientAutomationHooks> FailHooks = MakeShared<FFigmaHttpClientAutomationHooks>();
	FailHooks->StartRequest =
		[](const FString&, const FString&, int32, TFunction<void(bool, const FString&, int32)>)
		{
			return false;
		};
	SetFigmaHttpClientAutomationHooks(FailHooks);

	bool bFailureCalled = false;
	bool bFailureSuccess = true;
	{
		FFigmaHttpClient Client;
		Client.SetToken(TEXT("phase24-token"));
		Client.GetFile(TEXT("phase24-file"), [&bFailureCalled, &bFailureSuccess](bool bSuccess, const FString&)
		{
			bFailureCalled = true;
			bFailureSuccess = bSuccess;
		});
	}

	ResetFigmaHttpClientAutomationHooks();

	TestTrue(TEXT("Synchronous Figma launch failure reports callback"), bFailureCalled);
	TestFalse(TEXT("Synchronous Figma launch failure reports failure"), bFailureSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.MCPHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpCancellationBehaviorTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		TMap<FString, TFunction<void(bool, const FString&, int32, const FString&)>> PendingByMethod;
		int32 CancelCount = 0;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FMCPHttpClientAutomationHooks> Hooks = MakeShared<FMCPHttpClientAutomationHooks>();
	Hooks->StartAsyncRequest =
		[HookState](const FString& Method, const FString&, int32, TFunction<void(bool, const FString&, int32, const FString&)> OnComplete)
		{
			HookState->PendingByMethod.Add(Method, MoveTemp(OnComplete));
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32)
		{
			++HookState->CancelCount;
		};

	SetMCPHttpClientAutomationHooks(Hooks);

	FMCPHttpClient Client;
	bool bConnectCalled = false;
	bool bToolCallbackCalled = false;
	Client.ConnectAsync(TEXT("http://phase24.test"), [&bConnectCalled](bool bSuccess)
	{
		bConnectCalled = bSuccess;
	});

	TestTrue(TEXT("MCP behavior hook captured initialize"), HookState->PendingByMethod.Contains(TEXT("initialize")));
	HookState->PendingByMethod[TEXT("initialize")](true, TEXT("{}"), 200, TEXT("phase24-session"));
	TestTrue(TEXT("MCP behavior hook captured notifications/initialized"), HookState->PendingByMethod.Contains(TEXT("notifications/initialized")));
	HookState->PendingByMethod[TEXT("notifications/initialized")](true, TEXT(""), 202, TEXT(""));
	TestTrue(TEXT("Async MCP connect completes"), bConnectCalled);

	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(), [&bToolCallbackCalled](bool, const FString&)
	{
		bToolCallbackCalled = true;
	});

	TestTrue(TEXT("MCP behavior hook captured tools/call"), HookState->PendingByMethod.Contains(TEXT("tools/call")));
	Client.CancelRequests();
	TestEqual(TEXT("MCP behavior hook observed one cancellation"), HookState->CancelCount, 1);

	const FString ToolResponse =
		TEXT("event: message\n")
		TEXT("data: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"phase24\"}]}}\n\n");
	HookState->PendingByMethod[TEXT("tools/call")](true, ToolResponse, 200, TEXT(""));
	TestFalse(TEXT("Cancelled MCP async request suppresses late callback"), bToolCallbackCalled);

	ResetMCPHttpClientAutomationHooks();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDGenerationCancelBoundaryBehaviorTest,
	"Design2UMG.BatchD.GenerationCancelBoundaryBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDGenerationCancelBoundaryBehaviorTest::RunTest(const FString& Parameters)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/Phase23CancelBoundary");
	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FScopedGenerationCancelPoint CancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::BeforeBuild);
		UWidgetBlueprint* CancelledBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelBeforeMutation")),
			OutputDir);

		TestNull(TEXT("Pre-mutation cancel returns null"), CancelledBlueprint);
		TestEqual(TEXT("Pre-mutation cancel emits one warning"), Notifications.Warnings.Num(), 1);
		TestEqual(TEXT("Pre-mutation cancel emits no success"), Notifications.Successes.Num(), 0);
		TestNull(TEXT("Pre-mutation cancel does not create Blueprint asset"),
			FindObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Design2UMG/_TestOutput/Phase23CancelBoundary/WBP_Phase23CancelBeforeMutation.WBP_Phase23CancelBeforeMutation")));
	}

	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FScopedGenerationCancelPoint CancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::AfterBuildBeforeSave);
		UWidgetBlueprint* CompletedBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelAfterMutation")),
			OutputDir);

		TestNotNull(TEXT("Post-mutation cancel completes generation"), CompletedBlueprint);
		if (CompletedBlueprint)
		{
			TestTrue(TEXT("Post-mutation cancel leaves a coherent saved Blueprint package"),
				!CompletedBlueprint->GetOutermost()->IsDirty());
			TestTrue(TEXT("Post-mutation cancel generated a valid WidgetTree"), CompletedBlueprint->WidgetTree != nullptr);
		}
		TestEqual(TEXT("Post-mutation cancel emits one warning"), Notifications.Warnings.Num(), 1);
		TestEqual(TEXT("Post-mutation cancel emits no normal success summary"), Notifications.Successes.Num(), 0);
	}

	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		UWidgetBlueprint* CompletedBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelNoSeam")),
			OutputDir);

		TestNotNull(TEXT("No cancel seam keeps generation path working"), CompletedBlueprint);
		TestEqual(TEXT("No cancel seam emits no warning"), Notifications.Warnings.Num(), 0);
		TestEqual(TEXT("No cancel seam emits normal success summary"), Notifications.Successes.Num(), 1);
	}

	DeleteGeneratedAssetsAtPath(OutputDir);
	return true;
}
