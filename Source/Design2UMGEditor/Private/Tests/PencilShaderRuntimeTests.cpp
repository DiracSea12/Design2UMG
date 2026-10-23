// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/WidgetTree.h"
#include "Design2UMGWidgetMetadata.h"
#include "PencilShaderMaterials.h"
#include "PencilShaderRuntimeBindings.h"
#include "PreBuildResourceNormalizer.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "Components/NamedSlot.h"
#include "Components/RetainerBox.h"
#include "Design2UMGConverter.h"
#include "Design2UMGMaterialAccess.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "PenParser.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "TextureCompiler.h"
#include "UMGGenerator.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"

namespace
{
	struct FSdfInstance
	{
		TStrongObjectPtr<UUserWidget> Root;
		TSharedPtr<SWidget> Slate;
		URetainerBox* Capture = nullptr;
		UImage* Geometry = nullptr;
		UWidget* Alpha = nullptr;
		UMaterialInstanceDynamic* Effect = nullptr;
		UMaterialInstanceDynamic* Fill = nullptr;
	};

	UTextureRenderTarget2D* SdfTarget(UMaterialInstanceDynamic& Material)
	{
		return Cast<UTextureRenderTarget2D>(Material.K2_GetTextureParameterValue(TEXT("D2USdf")));
	}

	bool ObserveSdfInstance(FAutomationTestBase& Test, FSdfInstance& Instance)
	{
		TArray<UObject*> Objects;
		GetObjectsWithOuter(Instance.Root.Get(), Objects, true);
		int32 Surfaces = 0;
		for (UObject* Object : Objects)
		{
			if (auto* Widget = Cast<UWidget>(Object); Widget && Widget->GetFName() == TEXT("AlphaBlock")) Instance.Alpha = Widget;
			if (auto* Image = Cast<UImage>(Object); Image && Image->GetFName() == TEXT("GeometryFill_fill0")) Instance.Geometry = Image;
			if (auto* Capture = Cast<URetainerBox>(Object)) Instance.Capture = Capture;
			if (auto* Surface = Cast<UUserWidget>(Object))
			{
				UFunction* Paint = Surface->FindFunction(TEXT("OnPaint"));
				if (!Test.TestTrue(TEXT("Generated Surface owns its actual OnPaint"), Paint && Paint->GetOuterUClass() == Surface->GetClass())
					|| !Test.TestTrue(TEXT("Surface directly derives from the engine UserWidget"), Surface->GetClass()->GetSuperClass() == UUserWidget::StaticClass())
					|| !Test.TestFalse(TEXT("Generated Surface uses the non-Designer path"), Surface->IsDesignTime())) return false;
				++Surfaces;
			}
		}
		if (!Test.TestEqual(TEXT("Fill and effect each generate a native Surface"), Surfaces, 2)
			|| !Test.TestNotNull(TEXT("Captured source alpha widget exists"), Instance.Alpha)
			|| !Test.TestNotNull(TEXT("Generated geometry shader image exists"), Instance.Geometry)
			|| !Test.TestNotNull(TEXT("Effect uses a native Retainer"), Instance.Capture)) return false;
		UWidget* Contents[] = {Instance.Geometry, Instance.Capture};
		for (UWidget* Content : Contents)
		{
			const UNamedSlot* Slot = Cast<UNamedSlot>(Content->GetParent());
			const UUserWidget* Surface = Slot ? Slot->GetTypedOuter<UUserWidget>() : nullptr;
			if (!Test.TestTrue(TEXT("Runtime fill/effect content is attached to its actual Surface named slot"),
				Slot && Slot->GetContent() == Content && Surface && Surface != Instance.Root.Get()
				&& Surface->IsIn(Instance.Root.Get()) && Surface->GetContentForSlot(Slot->GetFName()) == Content)) return false;
		}
		Instance.Effect = Instance.Capture->GetEffectMaterial();
		Instance.Fill = Instance.Geometry->GetDynamicMaterial();
		if (!Test.TestNotNull(TEXT("Effect has a native instance material"), Instance.Effect)
			|| !Test.TestNotNull(TEXT("Fill has a native instance material"), Instance.Fill)) return false;
		UTexture* Mask = Instance.Fill->K2_GetTextureParameterValue(TEXT("GeometryMask"));
		if (!Test.TestNotNull(TEXT("Fill reads its geometry mask, not the object capture"), Mask)) return false;
		if (auto* Texture = Cast<UTexture2D>(Mask))
		{
		    FTextureCompilingManager::Get().FinishCompilation({Texture});
		    Test.AddInfo(FString::Printf(TEXT("SDF geometry mask=%s size=%dx%d sourceFormat=%d"),
		        *Texture->GetPathName(), Texture->GetSizeX(), Texture->GetSizeY(), int32(Texture->Source.GetFormat())));
		}
		for (UObject* Object : Objects)
			if (auto* Material = Cast<UMaterialInstanceDynamic>(Object))
			{
				Design2UMGMaterialAccess::WaitForCompilation(*Material);
				Material->GetMaterial()->RecacheUniformExpressions(true);
			}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilSdfSavedRuntimeTest,
	"Design2UMG.Resources.SdfSavedRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilSdfSavedRuntimeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("SDF test has the real editor world"), World)) return false;
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Design2UMG/SdfSavedRuntime") / Id);
	if (!TestTrue(TEXT("Create owned SDF shader directory"), IFileManager::Get().MakeDirectory(*Directory, true))) return false;
	const FString Fill = TEXT("precision mediump float;\n/** @resolution */ uniform vec2 Extent;\n/** @sdf */ uniform sampler2D DistanceField;\nvoid main(){float d=texture2D(DistanceField,gl_FragCoord.xy/Extent).r;gl_FragColor=vec4(vec3(step(0.0,d)),1.0);}");
	const FString Effect = TEXT("precision mediump float;\n/** @resolution */ uniform vec2 Extent;\n/** @sdf */ uniform sampler2D DistanceField;\nuniform sampler2D Source;\nvoid main(){vec2 uv=gl_FragCoord.xy/Extent;float d=texture2D(DistanceField,uv).r;gl_FragColor=vec4(vec3(step(0.0,d)),texture2D(Source,uv).a);}");
	if (!TestTrue(TEXT("Write owned fill shader"), FFileHelper::SaveStringToFile(Fill, *(Directory / TEXT("fill.glsl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		|| !TestTrue(TEXT("Write owned object shader"), FFileHelper::SaveStringToFile(Effect, *(Directory / TEXT("effect.glsl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	FDesignDocument Document;
	const FString Source = TEXT(R"({"version":"2.21","children":[{"id":"root","name":"SdfHost","type":"frame","width":128,"height":128,"layout":"none","clip":true,
"effect":{"type":"shader","url":"effect.glsl","uniforms":{"Source":"pencil:object"}},"children":[
{"id":"alpha","name":"AlphaBlock","type":"rectangle","width":64,"height":128,"fill":"#ffffffff"},
{"id":"geometry","name":"GeometryFill","type":"ellipse","x":80,"y":80,"width":32,"height":32,"fill":{"type":"shader","url":"fill.glsl"}}]}]})");
	const FString SourceFile = Directory / TEXT("source.pen");
	if (!TestTrue(TEXT("Write owned Pencil source"), FFileHelper::SaveStringToFile(Source, *SourceFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		|| !TestTrue(TEXT("Parse actual Pencil SDF source file"), FPenParser::ParseDocumentFile(SourceFile, Document))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* GeometryFill = nullptr;
	TFunction<void(const FUMGWidgetNode&)> FindGeometryFill = [&](const FUMGWidgetNode& Node)
	{
	    if (Node.Name == TEXT("GeometryFill_fill0")) GeometryFill = &Node;
	    for (const FUMGWidgetNode& Child : Node.Children) FindGeometryFill(Child);
	};
	FindGeometryFill(Data.RootWidget);
	if (!TestNotNull(TEXT("Converted ellipse shader paint exists"), GeometryFill)
	    || !TestEqual(TEXT("Ellipse geometry radius reaches the shader fill mask"),
	        GeometryFill->Brush.OutlineCornerRadius.Get(0.0f), 16.0f)) return false;
	UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, TEXT("/Game/Design2UMG/_TestOutput/SdfSavedRuntime/WBP_") + Id);
	if (!TestNotNull(TEXT("Production generator saves the SDF widget"), Blueprint)) return false;
	const FString AssetPath = Blueprint->GetPathName();
	TArray<UPackage*> Packages{Blueprint->GetOutermost()};
	TArray<UObject*> Templates;
	GetObjectsWithOuter(Blueprint, Templates, true);
	for (UObject* Object : Templates)
		if (auto* Surface = Cast<UUserWidget>(Object))
			if (auto* SurfaceBlueprint = Cast<UWidgetBlueprint>(Surface->GetClass()->ClassGeneratedBy)) Packages.AddUnique(SurfaceBlueprint->GetOutermost());
	if (!TestEqual(TEXT("Saved root and both Surface packages participate in reload"), Packages.Num(), 3)) return false;
	TArray<FString> PackageNames;
	for (UPackage* Package : Packages) PackageNames.Add(Package->GetName());
	FText UnloadError;
	const bool bUnloaded = UPackageTools::UnloadPackages(Packages, UnloadError);
	if (!TestTrue(FString::Printf(TEXT("Unload saved SDF assets: %s"), *UnloadError.ToString()), bUnloaded)) return false;
	for (const FString& Name : PackageNames)
		if (!TestNull(FString::Printf(TEXT("SDF package is absent before actual disk reload: %s"), *Name),
            FindObject<UPackage>(nullptr, *Name))) return false;
	Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
	if (!TestNotNull(TEXT("Reload SDF root from disk"), Blueprint)
		|| !TestNotNull(TEXT("Reloaded SDF generated class exists"), Blueprint->GeneratedClass.Get())) return false;

	FSdfInstance A, B;
	TStrongObjectPtr<UTextureRenderTarget2D> Output(FWidgetRenderer::CreateTargetFor(FVector2D(128), TF_Nearest, true));
	if (!TestNotNull(TEXT("Create native output target"), Output.Get())) return false;
	ON_SCOPE_EXIT
	{
		for (FSdfInstance* Instance : {&A, &B})
			if (Instance->Root.IsValid()) { Instance->Root->ReleaseSlateResources(true); Instance->Slate.Reset(); }
		FlushRenderingCommands();
	};
	for (FSdfInstance* Instance : {&A, &B})
	{
		Instance->Root.Reset(CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(Blueprint->GeneratedClass.Get())));
		if (!TestNotNull(TEXT("Create saved native SDF instance"), Instance->Root.Get())
			|| !TestFalse(TEXT("Root is not a Designer preview"), Instance->Root->IsDesignTime())) return false;
		Instance->Slate = Instance->Root->TakeWidget();
		if (!ObserveSdfInstance(*this, *Instance)) return false;
	}
	FWidgetRenderer Renderer(true);
	const auto Draw = [&](FSdfInstance& Instance, int32 Size)
	{
		Instance.Capture->RequestRender();
		Renderer.DrawWidget(Output.Get(), Instance.Slate.ToSharedRef(), FVector2D(Size), 1.f / 60.f, false);
		FlushRenderingCommands();
	};
	const auto Read = [&](UTextureRenderTarget2D* Target, int32 X, int32 Y)
	{
		return UKismetRenderingLibrary::ReadRenderTargetRawPixel(World, Target, X, Y, false);
	};
	Draw(A, 128); Draw(B, 128);
	UTextureRenderTarget2D* AEffect = SdfTarget(*A.Effect);
	UTextureRenderTarget2D* BEffect = SdfTarget(*B.Effect);
	UTextureRenderTarget2D* AFill = SdfTarget(*A.Fill);
	UTextureRenderTarget2D* BFill = SdfTarget(*B.Fill);
	for (UTextureRenderTarget2D* Target : {AEffect, BEffect, AFill, BFill})
		if (!TestNotNull(TEXT("Production OnPaint binds a real D2USdf RT"), Target)
			|| !TestEqual(TEXT("SDF preserves floating-point channels"), Target->GetFormat(), PF_FloatRGBA)) return false;
	const TSet<UTextureRenderTarget2D*> Targets{AEffect, BEffect, AFill, BFill};
	if (!TestEqual(TEXT("SDF RTs are private to each instance and fill/effect"), Targets.Num(), 4)) return false;
	if (!TestTrue(TEXT("SDF targets belong to their actual generated instances"),
		AEffect->IsIn(A.Root.Get()) && AFill->IsIn(A.Root.Get()) && BEffect->IsIn(B.Root.Get()) && BFill->IsIn(B.Root.Get()))) return false;
	if (!TestEqual(TEXT("Effect SDF follows capture width"), AEffect->SizeX, 128)
		|| !TestEqual(TEXT("Geometry SDF follows its independent fill width"), AFill->SizeX, 32)) return false;
	UTextureRenderTarget2D* Alpha = Cast<UTextureRenderTarget2D>(A.Effect->K2_GetTextureParameterValue(TEXT("D2UObject")));
	if (!TestNotNull(TEXT("Effect uses live object alpha, not the fill mask"), Alpha)) return false;
	const FLinearColor Before = Read(AEffect, 16, 32), Other = Read(BEffect, 16, 32);
	const FLinearColor OtherFill = Read(BFill, 16, 16);
	const FLinearColor FillInside = Read(AFill, 16, 16), FillOutside = Read(AFill, 1, 1);
	AddInfo(FString::Printf(TEXT("SDF initial effect=%dx%d fill=%dx%d sourceAlpha=%g signs=(%g,%g) fillSigns=(%g,%g)"),
		AEffect->SizeX, AEffect->SizeY, AFill->SizeX, AFill->SizeY, Read(Alpha, 16, 32).A, Before.R, Other.R, FillInside.R, FillOutside.R));
	if (!TestTrue(TEXT("Captured alpha is initially opaque at the probe"), Read(Alpha, 16, 32).A > 0.5f)
		|| !TestTrue(TEXT("Initial live-object distances are valid and inside"), FMath::IsFinite(Before.R) && Before.R > 0 && Other.R > 0)
		|| !TestTrue(TEXT("Fill SDF signs follow geometry, independent of object alpha"), FMath::IsFinite(FillInside.R) && FMath::IsFinite(FillOutside.R) && FillInside.R > 0 && FillOutside.R < 0)) return false;
	AddInfo(FString::Printf(TEXT("SDF initial other fill=%dx%d value=%s"), BFill->SizeX, BFill->SizeY, *OtherFill.ToString()));
	A.Alpha->SetRenderTranslation(FVector2D(64, 0));
	Draw(A, 128);
	const FLinearColor Changed = Read(AEffect, 16, 32);
	const FLinearColor OtherFillBeforeRepaint = Read(BFill, 16, 16);
	AddInfo(FString::Printf(TEXT("SDF other fill after A change, before B repaint=%s"), *OtherFillBeforeRepaint.ToString()));
	AddInfo(FString::Printf(TEXT("SDF changed sourceAlpha=%g distance=%g untouched=%g"), Read(Alpha, 16, 32).A, Changed.R, Read(BEffect, 16, 32).R));
	if (!TestTrue(TEXT("Actual captured source alpha changes"), Read(Alpha, 16, 32).A < 0.5f)
		|| !TestTrue(TEXT("The same production paint updates SDF after alpha changes"), FMath::IsFinite(Changed.R) && Changed.R < 0)
		|| !TestEqual(TEXT("Other instance stored SDF is not modified"), Read(BEffect, 16, 32), Other)) return false;
	Draw(B, 128);
	const FLinearColor OtherFillAfterRepaint = Read(BFill, 16, 16);
	AddInfo(FString::Printf(TEXT("SDF other fill after B repaint=%dx%d value=%s"), BFill->SizeX, BFill->SizeY, *OtherFillAfterRepaint.ToString()));
	if (!TestTrue(TEXT("Other instance retains its own source after repaint"), Read(BEffect, 16, 32).R > 0)) return false;
	Draw(A, 96);
	AddInfo(FString::Printf(TEXT("SDF resized capture=%dx%d SDF=%dx%d distance=%g other=%dx%d"),
		Alpha->SizeX, Alpha->SizeY, AEffect->SizeX, AEffect->SizeY, Read(AEffect, 16, 32).R, BEffect->SizeX, BEffect->SizeY));
	if (!TestTrue(TEXT("Resize updates the existing per-instance SDF target"), SdfTarget(*A.Effect) == AEffect && AEffect->SizeX == 96 && AEffect->SizeY == 96)
		|| !TestTrue(TEXT("Actual object capture also resizes"), Alpha->SizeX == 96 && Alpha->SizeY == 96)
		|| !TestTrue(TEXT("Resized capture still supplies current signed data"), FMath::IsFinite(Read(AEffect, 16, 32).R) && Read(AEffect, 16, 32).R < 0)
		|| !TestTrue(TEXT("Resize does not change the other instance target"), BEffect->SizeX == 128 && BEffect->SizeY == 128 && SdfTarget(*B.Effect) == BEffect)
		|| !TestEqual(TEXT("Other instance geometry SDF also stays unchanged"), Read(BFill, 16, 16), OtherFill)) return false;
	AddInfo(FString::Printf(TEXT("Saved SDF asset=%s world=%s gameWorld=%d Designer=0 effect=%s fill=%s before=%g changed=%g fillSigns=(%g,%g) resized=%dx%d"),
		*AssetPath, *World->GetName(), World->IsGameWorld(), *AEffect->GetPathName(), *AFill->GetPathName(), Before.R, Changed.R, FillInside.R, FillOutside.R, AEffect->SizeX, AEffect->SizeY));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilNativeBackgroundBindingsTest,
    "Design2UMG.Resources.PencilNativeBackgroundBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilNativeBackgroundBindingsTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("Native background bindings editor world"), World)) return false;
    FString Error;
    UMaterial *Composite = nullptr, *ConsumerMaterial = nullptr, *Identity = nullptr, *Opacity = nullptr;
    if (!TestTrue(TEXT("Prepare native background materials"),
        PencilShaderMaterials::PrepareBackgroundMaterials(Composite, ConsumerMaterial, Identity, Opacity, Error)))
    {
        AddError(Error);
        return false;
    }
    TStrongObjectPtr<UWidgetBlueprint> Blueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("NativeBackgroundBindings")),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Native background parent blueprint"), Blueprint.Get())) return false;
    Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint.Get(), TEXT("WidgetTree"));
    auto* Group = Blueprint->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SourceGroup"));
    auto* Prefix = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RealPrefix"));
    auto* Consumer = Blueprint->WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("NativeConsumer"));
    auto* Source = Blueprint->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RealSource"));
    Group->SetRenderOpacity(.6f);
    Source->SetRenderOpacity(.7f);
    Source->SetVisibility(ESlateVisibility::Collapsed);
    Consumer->SetEffectMaterial(ConsumerMaterial);
    Consumer->SetTextureParameter(TEXT("D2USource"));
    Consumer->SetRetainRendering(true);
    Consumer->SetContent(Source);
    Group->AddChildToOverlay(Prefix);
    Group->AddChildToOverlay(Consumer);
    Blueprint->WidgetTree->RootWidget = Group;
    Design2UMGWidgetMetadata::SetBackgroundCaptureInsets(Group, FMargin(0));
    Design2UMGWidgetMetadata::SetBackgroundCaptureInsets(Prefix, FMargin(3, 2, 5, 4));
    TArray<FString> Names;
    if (!TestTrue(TEXT("Generate real native paint boundaries"), PencilShaderRuntimeBindings::Prepare(Blueprint.Get(), Error, &Names)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Native source object retained"), Source->GetName(), FString(TEXT("RealSource")));
    TestEqual(TEXT("Source opacity moved to one final native material"), Source->GetRenderOpacity(), 1.f);
    TestEqual(TEXT("Group opacity moved to one final native material"), Group->GetRenderOpacity(), 1.f);
    TestEqual(TEXT("Original collapsed state retained"), Source->GetVisibility(), ESlateVisibility::Collapsed);
    auto* RootProducer = Cast<UUserWidget>(Blueprint->WidgetTree->RootWidget);
    if (!TestNotNull(TEXT("Native final group producer"), RootProducer)) return false;
    auto* PaintState = FindFProperty<FObjectPropertyBase>(RootProducer->GetClass(), TEXT("PaintState"));
    auto* GroupOpacity = FindFProperty<FFloatProperty>(RootProducer->GetClass(), TEXT("SourceOpacity"));
    if (!TestNotNull(TEXT("Producer has native material paint state"), PaintState)
        || !TestNotNull(TEXT("Producer holds source group opacity"), GroupOpacity)) return false;
    TestNull(TEXT("Producer paint-state reference is initialized by instance lifecycle"), PaintState->GetObjectPropertyValue_InContainer(RootProducer));
    TestEqual(TEXT("Final group material retains original source opacity"), GroupOpacity->GetPropertyValue_InContainer(RootProducer), .6f);

    TestTrue(TEXT("Capture metadata consumed"),
        Design2UMGWidgetMetadata::PackageMetadata(Prefix).GetValue(Prefix,
            Design2UMGWidgetMetadata::BackgroundCaptureInsetsKey).IsEmpty());
    int32 Boundaries = 0;
    int32 SourceOccurrences = 0;
    TArray<UUserWidget*> Markers;
    Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        SourceOccurrences += Widget == Source;
        if (auto* User = Cast<UUserWidget>(Widget))
            if (FindFProperty<FObjectPropertyBase>(User->GetClass(), TEXT("D2UBackgroundBoundary")))
            {
                ++Boundaries;
                Markers.Add(User);
                TestNull(TEXT("Empty paint markers do not expose a Surface slot"), User->GetContentForSlot(TEXT("Surface")));
            }
    });
    TestEqual(TEXT("One before and one after paint marker"), Boundaries, 2);
    TestEqual(TEXT("One actual native source"), SourceOccurrences, 1);
    int32 FrameDependentWrites = 0;
    for (UUserWidget* Marker : Markers)
    {
        auto* MarkerBlueprint = Cast<UWidgetBlueprint>(Marker->GetClass()->ClassGeneratedBy);
        if (!TestNotNull(TEXT("Paint marker retains its generated graph"), MarkerBlueprint)) return false;
        for (UEdGraph* Graph : MarkerBlueprint->FunctionGraphs)
            if (Graph->GetFName() == TEXT("OnPaint"))
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
                        TestNotNull(TEXT("Native OnPaint Context is an entry input"), Entry->FindPin(TEXT("Context")));
                    if (auto* Result = Cast<UK2Node_FunctionResult>(Node))
                        TestNull(TEXT("Native OnPaint has no Context result output"), Result->FindPin(TEXT("Context")));
                    if (auto* Write = Cast<UK2Node_CallFunction>(Node))
                    {
                        auto* Parameter = Write->FindPin(TEXT("ParameterName"));
                        auto* Value = Write->FindPin(TEXT("Value"));
                        if (Write->FunctionReference.GetMemberName() != TEXT("SetScalarParameterValue")
                            || !Parameter || Parameter->DefaultValue != TEXT("D2USourceOpacity")
                            || !Value || Value->LinkedTo.IsEmpty()) continue;
                        ++FrameDependentWrites;
                        auto* Input = Write->GetExecPin();
                        auto* Read = Input && Input->LinkedTo.Num() == 1
                            ? Cast<UK2Node_CallFunction>(Input->LinkedTo[0]->GetOwningNode()) : nullptr;
                        auto* ReadParameter = Read ? Read->FindPin(TEXT("ParameterName")) : nullptr;
                        TestTrue(TEXT("Native frame getter executes before its opacity consumer"), Read
                            && Read->FunctionReference.GetMemberName() == TEXT("K2_GetVectorParameterValue")
                            && ReadParameter && ReadParameter->DefaultValue == TEXT("D2UPaintFrame")
                            && Input->LinkedTo[0] == Read->GetThenPin());
                    }
                }
    }
    TestTrue(TEXT("Generated paint graphs exercise frame-dependent opacity writes"), FrameDependentWrites > 0);
    const int32 BindingCount = Blueprint->Bindings.Num();
    TestTrue(TEXT("Native source visibility uses generated getter bindings"), BindingCount >= 3);
    if (!TestTrue(TEXT("Repeated preparation keeps native boundary references"),
        PencilShaderRuntimeBindings::Prepare(Blueprint.Get(), Error, &Names)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Repeated preparation does not duplicate visibility bindings"), Blueprint->Bindings.Num(), BindingCount);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get(), EBlueprintCompileOptions::SkipGarbageCollection);
    if (!TestTrue(TEXT("Parent compiles before any pre-existing skeleton source property is required"), Blueprint->Status == BS_UpToDate))
        return false;
    TStrongObjectPtr<UUserWidget> Instance(CreateWidget<UUserWidget>(World,
        TSubclassOf<UUserWidget>(Blueprint->GeneratedClass.Get())));
    if (!TestNotNull(TEXT("Instantiate real generated parent"), Instance.Get())) return false;
    UWidget* RealSource = Instance->WidgetTree->FindWidget(TEXT("RealSource"));
    if (!TestNotNull(TEXT("Instantiated original native source"), RealSource)) return false;
    TestTrue(TEXT("Instance retains a native Border rather than a plugin widget"), RealSource->GetClass() == UBorder::StaticClass());
    TestTrue(TEXT("Instance native source is not the blueprint template"), RealSource != Source);
    for (UUserWidget* Template : Markers)
    {
        auto* Marker = Cast<UUserWidget>(Instance->WidgetTree->FindWidget(Template->GetFName()));
        if (!TestNotNull(TEXT("Instantiated paint boundary"), Marker)) return false;
        auto* Property = FindFProperty<FObjectPropertyBase>(Marker->GetClass(), TEXT("D2UBackgroundBoundary"));
        TestTrue(TEXT("Boundary consumer reference remaps to its real parent instance"),
            Property->GetObjectPropertyValue_InContainer(Marker) == Instance->WidgetTree->FindWidget(TEXT("NativeConsumer")));
    }
    const TSharedRef<SWidget> InstanceSlate = Instance->TakeWidget();
    auto* InstanceProducer = Cast<UUserWidget>(Instance->WidgetTree->RootWidget);
    auto* NativePaintState = InstanceProducer
        ? Cast<UMaterialInstanceDynamic>(PaintState->GetObjectPropertyValue_InContainer(InstanceProducer)) : nullptr;
    if (!TestNotNull(TEXT("Instance lifecycle establishes native paint state"), NativePaintState)) return false;
    TestEqual(TEXT("Unpainted native state excludes earlier frames"),
        NativePaintState->K2_GetVectorParameterValue(TEXT("D2UPaintFrame")), FLinearColor(-1, -1, -1, -1));

    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString ShaderFile = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ShaderTests") / (Id + TEXT(".glsl"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShaderFile), true);
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*ShaderFile); };
    if (!TestTrue(TEXT("Context object shader fixture writes"), FFileHelper::SaveStringToFile(TEXT(
        "precision mediump float;\nuniform sampler2D RawObject;\n"
        "void main(){gl_FragColor=texture2D(RawObject,vec2(0.5));}"), *ShaderFile,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
    FUMGWidgetNode Node;
    Node.Type = TEXT("RetainerBox");
    Node.Bounds.W = 32;
    Node.Bounds.H = 24;
    FDesignShaderData Shader;
    Shader.Url = ShaderFile;
    Shader.Size = FVector2D(32, 24);
    FDesignShaderValue Object;
    Object.Kind = FDesignShaderValue::EKind::String;
    Object.String = TEXT("pencil:object");
    Shader.Uniforms.Add(TEXT("RawObject"), Object);
    Node.Effect.Shader = Shader;
    if (!TestTrue(TEXT("Prepare production context object material"), FPreBuildResourceNormalizer::NormalizeGeneratedTextures(
        Node, TEXT("/Game/Design2UMG/Automation/NativeObjectContext_") + Id, TEXT("NativeObjectContext"), Error)))
    {
        AddError(Error);
        return false;
    }
    auto* ObjectMaterial = LoadObject<UMaterialInterface>(nullptr, *Node.Effect.EffectMaterial.Get(FString()));
    if (!TestNotNull(TEXT("Production object material"), ObjectMaterial)) return false;
    UTexture* UnusedCapture = nullptr;
    TestFalse(TEXT("Object shader does not sample the carrier capture"),
        ObjectMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("D2UNativeEmptyCapture")), UnusedCapture));
    TStrongObjectPtr<UWidgetBlueprint> ObjectBlueprint(Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UUserWidget::StaticClass(), GetTransientPackage(), *FString(TEXT("NativeObjectContext_") + Id),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass())));
    if (!TestNotNull(TEXT("Object context parent blueprint"), ObjectBlueprint.Get())) return false;
    UWidgetTree* ObjectTree = ObjectBlueprint->WidgetTree;
    auto* ObjectConsumer = ObjectTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("ObjectConsumer"));
    auto* ObjectShader = ObjectTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("ObjectShader"));
    auto* ActualObject = ObjectTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ActualObject"));
    ActualObject->SetPadding(FMargin(16, 12));
    ObjectShader->SetEffectMaterial(ObjectMaterial);
    ObjectShader->SetTextureParameter(TEXT("D2UObject"));
    ObjectShader->SetContent(ActualObject);
    ObjectConsumer->SetEffectMaterial(ConsumerMaterial);
    ObjectConsumer->SetTextureParameter(TEXT("D2USource"));
    ObjectConsumer->SetContent(ObjectShader);
    ObjectTree->RootWidget = ObjectConsumer;
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    ObjectBlueprint->ForEachSourceWidget([&](UWidget* Widget)
    {
        ObjectBlueprint->OnVariableAdded(Widget->GetFName());
    });
    const auto OriginalWidgetGuids = ObjectBlueprint->WidgetVariableNameToGuidMap;
#endif
    if (!TestTrue(TEXT("Generate context object native boundaries"),
        PencilShaderRuntimeBindings::Prepare(ObjectBlueprint.Get(), Error)))
    {
        AddError(Error);
        return false;
    }
    auto* Carrier = Cast<UBorder>(ObjectShader->GetContent());
    if (!TestNotNull(TEXT("Object shader retains a native Border carrier"), Carrier)) return false;
    TestTrue(TEXT("Carrier is visible to native Retainer effect painting"), Carrier->TakeWidget()->GetVisibility().IsVisible());
    TestEqual(TEXT("Carrier does not intercept original object input"), Carrier->GetVisibility(), ESlateVisibility::HitTestInvisible);
    TestEqual(TEXT("Carrier adds no layout padding"), Carrier->GetPadding(), FMargin(0));
    TestEqual(TEXT("Carrier contributes transparent capture pixels"), Carrier->GetBrushColor(), FLinearColor::Transparent);
    int32 ActualObjects = 0;
    ObjectTree->ForEachWidget([&](UWidget* Widget) { ActualObjects += Widget == ActualObject; });
    TestEqual(TEXT("Object context retains one real interactive object"), ActualObjects, 1);
    auto* ObjectRootProducer = Cast<UUserWidget>(ObjectTree->RootWidget);
    auto* ObjectGroup = ObjectRootProducer ? Cast<UOverlay>(ObjectRootProducer->GetContentForSlot(TEXT("Surface"))) : nullptr;
    if (!TestNotNull(TEXT("Object context keeps shared native layout overlay"), ObjectGroup)) return false;
    TestTrue(TEXT("Original object producer and shader consumer share the same bounds"),
        ObjectGroup->GetChildrenCount() == 2 && ObjectGroup->GetChildAt(1) == ObjectConsumer
        && Cast<UUserWidget>(ObjectGroup->GetChildAt(0))
        && Cast<UUserWidget>(ObjectGroup->GetChildAt(0))->GetContentForSlot(TEXT("Surface")) == ActualObject);
    for (UWidget* Child : ObjectGroup->GetAllChildren())
    {
        auto* Fill = Cast<UOverlaySlot>(Child->Slot);
        TestTrue(TEXT("Object and shader fill their preserved shared layout bounds"), Fill
            && Fill->GetHorizontalAlignment() == HAlign_Fill && Fill->GetVerticalAlignment() == VAlign_Fill);
    }
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
    for (const auto& Entry : OriginalWidgetGuids)
        TestEqual(TEXT("Native background preparation preserves original widget GUIDs"),
            ObjectBlueprint->WidgetVariableNameToGuidMap.FindRef(Entry.Key), Entry.Value);
    ObjectBlueprint->ForEachSourceWidget([&](UWidget* Widget)
    {
        TestTrue(FString::Printf(TEXT("Native source widget is registered before parent compile: %s"), *Widget->GetName()),
            ObjectBlueprint->WidgetVariableNameToGuidMap.FindRef(Widget->GetFName()).IsValid());
    });
#endif
    FKismetEditorUtilities::CompileBlueprint(ObjectBlueprint.Get(), EBlueprintCompileOptions::SkipGarbageCollection);
    TestTrue(TEXT("Object context parent compiles with const native OnPaint"), ObjectBlueprint->Status == BS_UpToDate);
    return true;
}

#endif
