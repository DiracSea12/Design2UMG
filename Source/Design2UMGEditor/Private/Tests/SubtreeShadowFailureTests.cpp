// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "UMGGenerator.h"
#include "UMGPropertyKeys.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialShared.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubtreeShadowKernelBoundTest,
	"Design2UMG.Converter.SubtreeShadowKernelBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSubtreeShadowKernelBoundTest::RunTest(const FString&)
{
	for (const float Blur : {0.0f, 1.0f, 4.0f, 12.0f, 32.0f, 100.0f, 10000.0f})
	{
		const auto Kernel = Design2UMGEffectMaterialBuilder::MakeSubtreeShadowKernel(Blur * 0.5f);
		TestEqual(TEXT("Unaligned tap grid reports the conservative fetch bound"),
			Kernel.TextureSampleCount, FMath::Square(2 * Kernel.Radius + 1));
		TestTrue(TEXT("Fixed per-layer texture fetch budget"), Kernel.TextureSampleCount <= 4225);
		TestTrue(TEXT("Three-sigma extent retained"), FMath::IsNearlyEqual(Kernel.Radius * Kernel.Step, FMath::CeilToFloat(Blur * 1.5f)));
		if (Blur <= 10.0f) TestEqual(TEXT("Small kernels retain pixel spacing"), Kernel.Step, 1.0f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubtreeShadowPreparationFailureTest,
	"Design2UMG.Converter.SubtreeShadowPreparationFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubtreeShadowPreparationFailureTest::RunTest(const FString&)
{
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_Original");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Name = TEXT("OriginalRoot");
	Data.RootWidget.Bounds.W = 200;
	Data.RootWidget.Bounds.H = 100;
	const FString PackagePath = TEXT("/Temp/Design2UMGShadowFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UWidgetBlueprint* Existing = FUMGGenerator::BuildWidgetBlueprint(Data, PackagePath);
	if (!TestNotNull(TEXT("Original blueprint exists"), Existing)) return false;
	UWidgetTree* OriginalTree = Existing->WidgetTree;
	UWidget* OriginalRoot = OriginalTree->RootWidget;
	Data.RootWidget.Type = TEXT("RetainerBox");
	Data.RootWidget.Effect.EffectMaterial = UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
	Data.RootWidget.Bounds.W = 0;
	AddExpectedError(TEXT("has no valid capture size"), EAutomationExpectedErrorFlags::Contains, 1);
	FDesign2UMGGenerationOutput Output;
	UWidgetBlueprint* Result = FUMGGenerator::BuildWidgetBlueprint(Data, PackagePath, nullptr, &Output, nullptr, Existing);
	TestNull(TEXT("Invalid shadow capture is reported as failed generation"), Result);
	TestTrue(TEXT("Resource preparation reason reaches the structured import report"),
		Output.MappingDiagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind == TEXT("resource_preparation_failed") && Diagnostic.Message.Contains(TEXT("OriginalRoot"))
				&& Diagnostic.Message.Contains(TEXT("has no valid capture size"));
		}));
	TestEqual(TEXT("Failure preserves the existing WidgetTree"), Existing->WidgetTree.Get(), OriginalTree);
	TestEqual(TEXT("Failure preserves the original root object"), Existing->WidgetTree->RootWidget.Get(), OriginalRoot);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSubtreeShadowShaderFailureTest,
	"Design2UMG.Converter.SubtreeShadowShaderFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSubtreeShadowShaderFailureTest::RunTest(const FString&)
{
	const FGuid Nonce = FGuid::NewGuid();
	FUMGWidgetNode Source;
	Source.Name = TEXT("ShaderFailure_") + Nonce.ToString(EGuidFormats::Digits);
	Source.Type = TEXT("RetainerBox");
	Source.Bounds.W = 200.0f;
	Source.Bounds.H = 100.0f;
	Source.Effect.EffectMaterial = UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
	FUMGBoxShadowLayerData Layer;
	Layer.Offset = FVector2D(1 + (Nonce.A & 0xffff), 1 + (Nonce.B & 0xffff));
	Layer.Color = FLinearColor::White;
	Layer.Blur = 0.0f;
	Source.Shadow.BoxShadows.Add(Layer);
	FUMGWidgetNode Prepared = Source;
	FString Error;
	if (!TestTrue(TEXT("Create isolated valid shadow material"), Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(Prepared, Error))) return false;
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *Prepared.Effect.EffectMaterial.GetValue());
	if (!TestNotNull(TEXT("Load isolated material"), Material)) return false;
	UMaterialExpressionCustom* Shader = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression)) { Shader = Custom; break; }
	}
	if (!TestNotNull(TEXT("Find generated shader expression"), Shader)) return false;
	const FString OriginalCode = Shader->Code;
	ON_SCOPE_EXIT
	{
		// Only this test's unique material is touched; its saved package already has valid code.
		Shader->Code = OriginalCode;
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		Material->EnsureIsComplete();
	};
	AddExpectedError(TEXT("D2U_ExpectedMissingShaderSymbol"), EAutomationExpectedErrorFlags::Contains, 0);
	Shader->Code = TEXT("return D2U_ExpectedMissingShaderSymbol;");
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
	Material->EnsureIsComplete();
	const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
	if (!TestTrue(TEXT("Fault injection actually causes shader compilation errors"), Resource && !Resource->GetCompileErrors().IsEmpty())) return false;
	TestFalse(TEXT("Shader compilation failure is not reported as successful preparation"),
		Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(Source, Error));
	TestTrue(TEXT("Failure identifies affected node and compiler reason"), Error.Contains(Source.Name) && Error.Contains(TEXT("D2U_ExpectedMissingShaderSymbol")));
	TestEqual(TEXT("Failed preparation retains unresolved effect for diagnosis"), Source.Effect.EffectMaterial.GetValue(), UMGPropertyKeys::RetainerSubtreeShadowEffectRef);
	TestEqual(TEXT("Failed preparation retains source shadow layers"), Source.Shadow.BoxShadows.Num(), 1);
	return true;
}

#endif
