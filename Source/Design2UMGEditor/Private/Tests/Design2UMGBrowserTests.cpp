// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Design2UMGConverter.h"
#include "DesignBrowserURL.h"
#include "DesignBrowserWidget.h"
#include "DesignSchema.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "PenParser.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGWidgetTreeBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WebBrowser.h"
#include "WidgetBlueprint.h"

namespace
{
	const FUMGWidgetNode* FindBrowser(const FUMGWidgetNode& Node)
	{
		if (Node.Type == TEXT("WebBrowser")) return &Node;
		for (const FUMGWidgetNode& Child : Node.Children)
			if (const FUMGWidgetNode* Found = FindBrowser(Child)) return Found;
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrowserDataFlowTest,
	"Design2UMG.Browser.DataFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrowserDataFlowTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;
	const FString SourceFile = TEXT("G:/Browser Corpus/design.pen");
	if (!TestTrue(TEXT("Parse real browser schema fields"), FPenParser::ParseDocument(TEXT(R"({"version":"2.21","children":[{"id":"page","type":"browser","width":400,"height":300,"rotation":17,"url":"pages/index.html","deviceId":"test-device","zoom":1.25,"scrollX":12,"scrollY":34}]})"), Document, SourceFile))) return false;
	TestTrue(TEXT("Browser has an explicit source type"), Document.Children[0]->Type == EDesignNodeType::Browser);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* Browser = FindBrowser(Data.RootWidget);
	if (!TestNotNull(TEXT("Native browser selected, not a generic frame"), Browser)) return false;
	TestEqual(TEXT("Relative HTML anchored to source document"), Browser->Browser.InitialURL,
		FString(TEXT("file:///G:/Browser%20Corpus/pages/index.html")));
	TestEqual(TEXT("Device identity preserved"), Browser->Browser.DeviceId, FString(TEXT("test-device")));
	TestEqual(TEXT("Zoom preserved"), Browser->Browser.Zoom.GetValue(), 1.25f);
	TestEqual(TEXT("Scroll X preserved"), Browser->Browser.ScrollX.GetValue(), 12.0f);
	TestEqual(TEXT("Scroll Y preserved"), Browser->Browser.ScrollY.GetValue(), 34.0f);
	TestTrue(TEXT("Device and scrolling gaps remain explicit"), Data.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("deviceId/scroll")); }));
	TestEqual(TEXT("Browser viewport uses inverse zoom stretch anchors"), Browser->Slot.Anchors.Max.X, 0.8f);
	TestEqual(TEXT("Browser viewport stretches vertically with its parent"), Browser->Slot.Anchors.Max.Y, 0.8f);
	TestEqual(TEXT("Browser viewport has no fixed-width offset"), Browser->Slot.Offsets.Right, 0.0f);
	TestEqual(TEXT("Browser viewport has no fixed-height offset"), Browser->Slot.Offsets.Bottom, 0.0f);
	TestEqual(TEXT("Zoom is a separate render scale"), Browser->Render.RenderTransformScale.Get(FVector2D::ZeroVector), FVector2D(1.25f));
	TestEqual(TEXT("Zoom is anchored at the viewport origin"), Browser->Render.RenderTransformPivot.Get(FVector2D(0.5f)), FVector2D::ZeroVector);
	TestEqual(TEXT("Source layout width stays unchanged"), Data.RootWidget.Bounds.W, 400.0f);
	TestEqual(TEXT("Source layout height stays unchanged"), Data.RootWidget.Bounds.H, 300.0f);
	TestEqual(TEXT("Source rotation stays on the outer node"), Data.RootWidget.Render.RenderAngle.Get(0.0f), 17.0f);
	TestFalse(TEXT("Viewport does not duplicate source rotation"), Browser->Render.RenderAngle.IsSet());
	FUMGWidgetData Restored;
	FString Error;
	if (!TestTrue(TEXT("Browser payload round-trips through production JSON"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), Restored, &Error))) return false;
	const FUMGWidgetNode* RestoredBrowser = FindBrowser(Restored.RootWidget);
	if (!TestNotNull(TEXT("Browser survives serialization"), RestoredBrowser)) return false;
	TestEqual(TEXT("URL preserved"), RestoredBrowser->Browser.InitialURL, Browser->Browser.InitialURL);
	TestEqual(TEXT("Device preserved"), RestoredBrowser->Browser.DeviceId, Browser->Browser.DeviceId);
	TestEqual(TEXT("Viewport state preserved"), RestoredBrowser->Browser.ScrollY.GetValue(), 34.0f);
	TestEqual(TEXT("Inverse zoom anchors persist"), RestoredBrowser->Slot.Anchors.Max.X, 0.8f);
	const TStrongObjectPtr<UWidgetTree> ZoomTree(NewObject<UWidgetTree>());
	UWidget* ZoomRoot = FUMGWidgetTreeBuilder::CreateWidgetRecursive(ZoomTree.Get(), Restored.RootWidget, nullptr, nullptr);
	if (!TestNotNull(TEXT("Zoom tree is generated"), ZoomRoot)) return false;
	TestTrue(TEXT("Native zoom outer frame clips the magnified page"), ZoomRoot->GetClipping() == EWidgetClipping::ClipToBounds);
	ZoomTree->RootWidget = ZoomRoot;
	UWebBrowser* NativeBrowser = nullptr;
	ZoomTree->ForEachWidget([&NativeBrowser](UWidget* Widget)
	{
		if (UWebBrowser* Page = Cast<UWebBrowser>(Widget)) NativeBrowser = Page;
	});
	if (!TestNotNull(TEXT("Zoom still uses the native browser"), NativeBrowser)) return false;
	TestNotNull(TEXT("Browser zoom uses a native canvas, not a layout-scaled ScaleBox"), Cast<UCanvasPanel>(NativeBrowser->GetParent()));
	const UCanvasPanelSlot* ZoomSlot = Cast<UCanvasPanelSlot>(NativeBrowser->Slot);
	if (!TestNotNull(TEXT("Zoom viewport has a native canvas slot"), ZoomSlot)) return false;
	TestEqual(TEXT("Native anchors resize the viewport with its parent"), ZoomSlot->GetAnchors().Maximum, FVector2D(0.8f));
	TestEqual(TEXT("Native zoom does not scale layout geometry"), NativeBrowser->GetRenderTransform().Scale, FVector2D(1.25f));
	FUMGWidgetNode Copied = *RestoredBrowser;
	TestEqual(TEXT("Value copy keeps browser data"), Copied.Browser.Zoom.GetValue(), 1.25f);
	FUMGWidgetNode Assigned;
	Assigned = Copied;
	TestEqual(TEXT("Value assignment keeps browser data"), Assigned.Browser.DeviceId, Copied.Browser.DeviceId);
	FDesignDocument DefaultZoomDocument;
	if (!TestTrue(TEXT("Parse unit zoom browser"), FPenParser::ParseDocument(TEXT(R"({"version":"2.21","children":[{"id":"unit","type":"browser","width":400,"height":300,"zoom":1}]})"), DefaultZoomDocument))) return false;
	const FUMGWidgetData DefaultZoom = FDesign2UMGConverter::Convert(DefaultZoomDocument, FDesign2UMGConversionSettings::Defaults());
	TestEqual(TEXT("Unit zoom adds no wrapper"), DefaultZoom.RootWidget.Type, FString(TEXT("WebBrowser")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrowserTemplateTest,
	"Design2UMG.Browser.NativeTemplate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrowserTemplateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Local page URI keeps escaped filename, query and fragment"),
		DesignBrowserURL::Resolve(TEXT("pages/a%23b.html?next=https://example.test#section"), TEXT("G:/Browser Corpus/design.pen")),
		FString(TEXT("file:///G:/Browser%20Corpus/pages/a%23b.html?next=https://example.test#section")));
	TestEqual(TEXT("Remote URL is not treated as a file"), DesignBrowserURL::Resolve(TEXT("https://example.test/page?q=1#s")), FString(TEXT("https://example.test/page?q=1#s")));
	const TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>());
	FUMGWidgetNode Node;
	Node.Type = TEXT("WebBrowser");
	Node.Name = TEXT("LivePage");
	Node.Bounds.W = 400.0f;
	Node.Bounds.H = 300.0f;
	Node.Browser.InitialURL = TEXT("data:text/html,<html><body>Live</body></html>");
	Node.Browser.bSupportsTransparency = true;
	Node.Browser.Zoom = 1.0f;
	UWebBrowser* Widget = Cast<UWebBrowser>(FUMGWidgetTreeBuilder::CreateWidgetRecursive(Tree.Get(), Node, nullptr, nullptr));
	if (!TestNotNull(TEXT("Uses the engine native class"), Widget)) return false;
	FStrProperty* URL = FindFProperty<FStrProperty>(UWebBrowser::StaticClass(), TEXT("InitialURL"));
	FBoolProperty* Transparency = FindFProperty<FBoolProperty>(UWebBrowser::StaticClass(), TEXT("bSupportsTransparency"));
	if (!TestNotNull(TEXT("Native persistent URL property"), URL) || !TestNotNull(TEXT("Native persistent transparency property"), Transparency)) return false;
	TestEqual(TEXT("InitialURL persists without opening a browser instance"), URL->GetPropertyValue_InContainer(Widget), Node.Browser.InitialURL);
	TestTrue(TEXT("Native transparency persists"), Transparency->GetPropertyValue_InContainer(Widget));
	const TStrongObjectPtr<UWidgetBlueprint> NoScrollBlueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UUserWidget::StaticClass(), GetTransientPackage(),
		FName(*(TEXT("BrowserBindings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))), BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
	if (!TestNotNull(TEXT("Browser graph fixture uses the native Blueprint factory"), NoScrollBlueprint.Get())) return false;
	if (!TestNotNull(TEXT("Browser graph fixture has the production SelfScope"), NoScrollBlueprint->SkeletonGeneratedClass.Get())) return false;
	const int32 InitialGraphCount = NoScrollBlueprint->UbergraphPages.Num();
	const int32 InitialVariableCount = NoScrollBlueprint->NewVariables.Num();
	UWebBrowser* NaturalBrowser = NoScrollBlueprint->WidgetTree->ConstructWidget<UWebBrowser>(UWebBrowser::StaticClass(), TEXT("NaturalBrowser"));
	if (!TestTrue(TEXT("Source without scroll keeps the natural browser"), DesignBrowserWidget::ApplyTemplate(*NaturalBrowser, Node.Browser))) return false;
	TestEqual(TEXT("Source without scroll adds no execution graph"), NoScrollBlueprint->UbergraphPages.Num(), InitialGraphCount);
	TestEqual(TEXT("Source without scroll adds no state variable"), NoScrollBlueprint->NewVariables.Num(), InitialVariableCount);
	Node.Browser.ScrollX = 137;
	Node.Browser.ScrollY = 211;
	const FName ScrollStateName(*(TEXT("D2U_BrowserScrollDispatched_") + NaturalBrowser->GetName()));
	TestNull(TEXT("New browser member is not in the existing Skeleton"),
		FindFProperty<FProperty>(NoScrollBlueprint->SkeletonGeneratedClass, NaturalBrowser->GetFName()));
	TestNull(TEXT("New scroll state is not in the existing Skeleton"),
		FindFProperty<FProperty>(NoScrollBlueprint->SkeletonGeneratedClass, ScrollStateName));
	if (!TestTrue(TEXT("Scroll graph accepts source types before new members reach the Skeleton"),
		DesignBrowserWidget::ApplyTemplate(*NaturalBrowser, Node.Browser))) return false;
	TestNull(TEXT("Scroll preparation does not prematurely generate the state member"),
		FindFProperty<FProperty>(NoScrollBlueprint->SkeletonGeneratedClass, ScrollStateName));
	for (const auto& Graph : NoScrollBlueprint->UbergraphPages)
	{
		for (const auto& GraphNode : Graph->Nodes)
		{
			if (const auto* Get = Cast<UK2Node_VariableGet>(GraphNode))
				TestNotNull(TEXT("Uncompiled member getter has its typed value pin"), Get->FindPin(Get->GetVarName()));
			if (const auto* Set = Cast<UK2Node_VariableSet>(GraphNode))
				TestNotNull(TEXT("Uncompiled member setter has its typed value pin"), Set->FindPin(Set->GetVarName()));
		}
	}
	Node.Browser.InitialURL = TEXT("G:/Browser Case/index %231.html");
	Node.Browser.bSupportsTransparency.Reset();
	Widget = Cast<UWebBrowser>(FUMGWidgetTreeBuilder::CreateWidgetRecursive(Tree.Get(), Node, nullptr, nullptr));
	if (!TestNotNull(TEXT("Local page uses the native class"), Widget)) return false;
	TestEqual(TEXT("Local path is an escaped file URL"), URL->GetPropertyValue_InContainer(Widget), FString(TEXT("file:///G:/Browser%20Case/index%20%231.html")));
	TestEqual(TEXT("Absent transparency keeps native default"), Transparency->GetPropertyValue_InContainer(Widget), Transparency->GetPropertyValue_InContainer(GetDefault<UWebBrowser>()));
	return true;
}

namespace
{
	// Only the owned HTML reports state through its title; arbitrary imported pages are not modified.
	FString BrowserProbePage(const FString& Token)
	{
		return FString::Printf(TEXT(R"(<!doctype html><html><head><meta charset="utf-8"></head><body style="margin:0"><div style="width:2400px;height:1800px;background:#123456"></div><script>
let resizeCount=0;
const report=()=>{document.title=JSON.stringify({token:'%s',state:'ready',resizes:resizeCount,x:window.scrollX,y:window.scrollY,width:window.innerWidth,height:window.innerHeight,scrollWidth:document.documentElement.scrollWidth,scrollHeight:document.documentElement.scrollHeight});};
window.addEventListener('load',()=>requestAnimationFrame(report));window.addEventListener('scroll',report);window.addEventListener('resize',()=>{++resizeCount;report();});
</script></body></html>)"), *Token);
	}

	class FCheckNativeBrowserNavigation final : public IAutomationLatentCommand
	{
	public:
		FCheckNativeBrowserNavigation(FAutomationTestBase& InTest, UUserWidget* InInstance,
			UWebBrowser* InBrowser, const FString& InSecondURL, const FString& InToken)
			: Test(InTest), Instance(InInstance), Browser(InBrowser), SecondURL(InSecondURL), Token(InToken),
			  Target(FWidgetRenderer::CreateTargetFor(FVector2D(400, 300), TF_Nearest, true)), Renderer(true)
		{
		}

		~FCheckNativeBrowserNavigation() override
		{
			Instance->ReleaseSlateResources(true);
			SlateWidget.Reset();
			FlushRenderingCommands();
		}

		bool Update() override
		{
			if (!SlateWidget.IsValid())
			{
				SlateWidget = Instance->TakeWidget();
				StartedAt = FPlatformTime::Seconds();
			}
			if (!Target.IsValid())
			{
				Test.AddError(TEXT("Native browser render target was not created"));
				return true;
			}
			Renderer.DrawWidget(Target.Get(), SlateWidget.ToSharedRef(), 1.f, FVector2D(400, 300), static_cast<float>(FApp::GetDeltaTime()));
			FlushRenderingCommands();
			const FString Title = Browser->GetTitleText().ToString();
			if (FPlatformTime::Seconds() - StartedAt > 60.0)
			{
				Test.AddError(FString::Printf(TEXT("Native browser timed out in state %d; URL=%s title=%s"),
					static_cast<int32>(State), *Browser->GetUrl(), *Title));
				return true;
			}
			TSharedPtr<FJsonObject> Message;
			if (!Title.StartsWith(TEXT("{")) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Title), Message)
				|| !Message.IsValid()) return false;
			const bool bSecond = State == EState::SecondReady;
			FString PageToken, PageState;
			if (!Message->TryGetStringField(TEXT("token"), PageToken)
				|| PageToken != Token + (bSecond ? TEXT("-second") : TEXT("-first"))
				|| !Message->TryGetStringField(TEXT("state"), PageState)) return false;
			if (PageState != TEXT("ready")) return false;
			const FString URL = Browser->GetUrl();
			if (!URL.StartsWith(TEXT("file:")) || !URL.EndsWith(bSecond ? TEXT("/second.html") : TEXT("/first.html"))) return false;
			double X, Y, Width, Height, ScrollWidth, ScrollHeight, ResizeCount;
			if (!Message->TryGetNumberField(TEXT("x"), X) || !Message->TryGetNumberField(TEXT("y"), Y)
				|| !Message->TryGetNumberField(TEXT("width"), Width) || !Message->TryGetNumberField(TEXT("height"), Height)
				|| !Message->TryGetNumberField(TEXT("scrollWidth"), ScrollWidth)
				|| !Message->TryGetNumberField(TEXT("scrollHeight"), ScrollHeight)
				|| !Message->TryGetNumberField(TEXT("resizes"), ResizeCount))
			{
				Test.AddError(TEXT("Owned browser page returned incomplete viewport/scroll data"));
				return true;
			}
			// CEF starts with a 1x1 placeholder before the first viewport resize.
			if (ResizeCount == 0.0 && Width == 1.0 && Height == 1.0) return false;
			if (!bSecond && (!FMath::IsNearlyEqual(X, 137.0) || !FMath::IsNearlyEqual(Y, 211.0))) return false;
			Test.AddInfo(FString::Printf(TEXT("Native browser page=%s URL=%s scroll=(%g,%g) viewport=%gx%g content=%gx%g"),
				*PageToken, *Browser->GetUrl(), X, Y, Width, Height, ScrollWidth, ScrollHeight));
			bool bValid = Test.TestTrue(TEXT("Owned page has room for the requested scroll"), ScrollWidth - Width > 137 && ScrollHeight - Height > 211);
			const FBoolProperty* Once = FindFProperty<FBoolProperty>(Instance->GetClass(),
				FName(*(TEXT("D2U_BrowserScrollDispatched_") + Browser->GetName())));
			bValid &= Test.TestTrue(TEXT("Source scroll dispatch remains set after navigation"), Once && Once->GetPropertyValue_InContainer(Instance.Get()));
			bValid &= Test.TestTrue(TEXT("Generated initial scroll is applied once; later navigation keeps its natural position"),
				FMath::IsNearlyEqual(X, bSecond ? 0.0 : 137.0) && FMath::IsNearlyEqual(Y, bSecond ? 0.0 : 211.0));
			bValid &= Test.TestTrue(TEXT("Zoom gives CEF the inverse-width viewport"), FMath::IsNearlyEqual(Width, 320.0, 1.0));
			bValid &= Test.TestTrue(TEXT("Zoom gives CEF the inverse-height viewport"), FMath::IsNearlyEqual(Height, 240.0, 1.0));
			bValid &= Test.TestTrue(TEXT("Browser layout remains unscaled"), Browser->GetCachedGeometry().GetLocalSize().Equals(FVector2D(320, 240), 1.0));
			bValid &= Test.TestTrue(TEXT("Zoom is applied by the independent render transform"), Browser->GetRenderTransform().Scale.Equals(FVector2D(1.25), 0.000001));
			if (!bValid || bSecond) return true;
			Browser->LoadURL(SecondURL);
			State = EState::SecondReady;
			StartedAt = FPlatformTime::Seconds();
			return false;
		}

	private:
		enum class EState { FirstReady, SecondReady };
		FAutomationTestBase& Test;
		TStrongObjectPtr<UUserWidget> Instance;
		UWebBrowser* Browser;
		FString SecondURL;
		FString Token;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		FWidgetRenderer Renderer;
		TSharedPtr<SWidget> SlateWidget;
		EState State = EState::FirstReady;
		double StartedAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrowserNativeNavigationTest,
	"Design2UMG.Browser.NativeNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBrowserNativeNavigationTest::RunTest(const FString& Parameters)
{
	const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Design2UMG/BrowserNativeNavigation") / Token);
	if (!TestTrue(TEXT("Create owned HTML directory"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
	const FString FirstHTML = Directory / TEXT("first.html");
	const FString SecondHTML = Directory / TEXT("second.html");
	if (!TestTrue(TEXT("Write first owned page"), FFileHelper::SaveStringToFile(BrowserProbePage(Token + TEXT("-first")), *FirstHTML, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		|| !TestTrue(TEXT("Write second owned page"), FFileHelper::SaveStringToFile(BrowserProbePage(Token + TEXT("-second")), *SecondHTML, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;

	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse native browser source"), FPenParser::ParseDocument(
		TEXT(R"({"version":"2.21","children":[{"id":"page","type":"browser","width":400,"height":300,"url":"first.html","zoom":1.25,"scrollX":137,"scrollY":211}]})"),
		Document, Directory / TEXT("design.pen")))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, TEXT("/Game/Design2UMG/_TestOutput/BrowserNativeNavigation/WBP_") + Token);
	if (!TestNotNull(TEXT("Generate and save native browser blueprint"), Blueprint)) return false;
	const FString AssetPath = Blueprint->GetPathName();
	const FString PackageName = Blueprint->GetOutermost()->GetName();
	FText UnloadError;
	const bool bUnloaded = UPackageTools::UnloadPackages({ Blueprint->GetOutermost() }, UnloadError);
	if (!TestTrue(FString::Printf(TEXT("Unload saved browser asset: %s"), *UnloadError.ToString()), bUnloaded)) return false;
	if (!TestNull(TEXT("Browser package is absent before disk reload"), FindObject<UPackage>(nullptr, *PackageName))) return false;
	Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Reload generated browser asset from disk"), Blueprint)
		|| !TestNotNull(TEXT("Reloaded native browser generated class"), Blueprint->GeneratedClass.Get())) return false;
	for (int32 InstanceIndex = 0; InstanceIndex < 2; ++InstanceIndex)
	{
		TStrongObjectPtr<UUserWidget> Instance(NewObject<UUserWidget>(GetTransientPackage(), Blueprint->GeneratedClass));
		if (!TestTrue(TEXT("Initialize a fresh runtime widget"), Instance->Initialize())) return false;
		UWebBrowser* Browser = nullptr;
		Instance->WidgetTree->ForEachWidget([&Browser](UWidget* Widget) { if (UWebBrowser* Page = Cast<UWebBrowser>(Widget)) Browser = Page; });
		if (!TestNotNull(TEXT("Reloaded asset creates the native browser"), Browser)) return false;
		const UWebBrowser* Template = Cast<UWebBrowser>(Blueprint->WidgetTree->FindWidget(Browser->GetFName()));
		if (!TestNotNull(TEXT("Native browser template name and type survive saved reload"), Template)) return false;
		TestEqual(TEXT("Browser uses exactly the engine class"), Browser->GetClass(), UWebBrowser::StaticClass());
		const FBoolProperty* Once = FindFProperty<FBoolProperty>(Blueprint->GeneratedClass,
			FName(*(TEXT("D2U_BrowserScrollDispatched_") + Browser->GetName())));
		if (!TestNotNull(TEXT("Generated browser owns instance-local one-shot state"), Once)) return false;
		if (!TestFalse(TEXT("Each new instance begins without dispatching source scroll"), Once->GetPropertyValue_InContainer(Instance.Get()))) return false;
		if (!TestFalse(TEXT("Root is not a Designer preview"), Instance->IsDesignTime())
			|| !TestFalse(TEXT("Browser is not a Designer placeholder"), Browser->IsDesignTime())) return false;
		AddInfo(FString::Printf(TEXT("Automatic source-scroll reload instance=%d asset=%s ownedHTML=%s"), InstanceIndex, *AssetPath, *Directory));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckNativeBrowserNavigation(*this, Instance.Get(), Browser, DesignBrowserURL::Resolve(SecondHTML), Token));
	}
	return true;
}

#endif
