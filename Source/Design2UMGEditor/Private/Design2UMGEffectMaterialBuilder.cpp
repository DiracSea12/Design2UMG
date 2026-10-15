// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEffectMaterialBuilder.h"

#include "Design2UMGMaterialAccess.h"
#include "Misc/EngineVersionComparison.h"
#include "Design2UMGLog.h"

#include "Factories/MaterialFactoryNew.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#if !UE_VERSION_OLDER_THAN(5, 2, 0)
#include "MaterialDomain.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FeedbackContext.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParsing.h"
#include "UMGWidgetNodeAccess.h"
#include "Misc/SecureHash.h"
#include "MaterialShared.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	bool ValidateCompiledSubtreeMaterial(UMaterial* Material, const FString& NodeName, FString& OutError)
	{
		const FMaterialResource* Resource = Material->GetMaterialResource(
#if UE_VERSION_OLDER_THAN(5, 7, 0)
			GMaxRHIFeatureLevel
#else
			GMaxRHIShaderPlatform
#endif
		);
		if (Resource && !Resource->GetCompileErrors().IsEmpty())
		{
			OutError = FString::Printf(TEXT("Subtree shadow '%s' material '%s' shader compilation failed: %s"),
				*NodeName, *Material->GetPathName(), *FString::Join(Resource->GetCompileErrors(), TEXT("; ")));
			return false;
		}
		if (!Resource || !Resource->GetGameThreadShaderMap() || !Resource->GetGameThreadShaderMap()->IsValidForRendering())
		{
			OutError = FString::Printf(TEXT("Subtree shadow '%s' material '%s' has no renderable shader map after compilation"),
				*NodeName, *Material->GetPathName());
			return false;
		}
		return true;
	}

	TStrongObjectPtr<UMaterial> GForegroundBlurRetainerMaterial;
	TStrongObjectPtr<UMaterial> GGeometryMaskRetainerMaterial;
	TStrongObjectPtr<UMaterial> GInnerShadowRetainerMaterial;


	bool SaveGeneratedMaterialAsset(UObject* Asset, const FString& FileName)
	{
		UPackage* Package = Asset->GetOutermost();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, Asset, *FileName, SaveArgs)) return true;

		// A failed new asset must not be returned by LoadObject on the next attempt.
		Asset->RemoveFromRoot();
		Asset->ClearFlags(RF_Public | RF_Standalone);
		Asset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		Asset->MarkAsGarbage();
		Package->SetDirtyFlag(false);
		return false;
	}

	// Fallback UV step for the retainer blur when no node size can be resolved at bind time.
	// Same value and provenance as InvalidNodeBlurUVStepComponent in VisualMapper.cpp.
	constexpr float BlurUVStepFallbackComponent = 1.0f / 512.0f;

	int32 QuantizeMaterialValue(float Value)
	{
		return FMath::RoundToInt(Value * 1000.0f);
	}

	FString BuildGeometryMaskInstanceName(
		EUMGGeometryMaskShape Shape,
		const FVector2D& Size,
		const FVector4& CornerRadii)
	{
		return FString::Printf(
			TEXT("D2U_GeometryMask_%d_%d_%d_%d_%d_%d_%d"),
			Shape == EUMGGeometryMaskShape::Ellipse ? 0 : 1,
			QuantizeMaterialValue(Size.X),
			QuantizeMaterialValue(Size.Y),
			QuantizeMaterialValue(CornerRadii.X),
			QuantizeMaterialValue(CornerRadii.Y),
			QuantizeMaterialValue(CornerRadii.Z),
			QuantizeMaterialValue(CornerRadii.W));
	}

	void SetupExpression(UMaterialExpressionTextureObjectParameter* Expression, const FName& Name)
	{
		Expression->ParameterName = Name;
		Expression->SetDefaultTexture();
		Expression->SamplerType = SAMPLERTYPE_Color;
	}

	void SetupExpression(UMaterialExpressionVectorParameter* Expression, const FName& Name)
	{
		Expression->ParameterName = Name;
		Expression->DefaultValue = FLinearColor(BlurUVStepFallbackComponent, BlurUVStepFallbackComponent, 0.0f, 0.0f);
	}

	void SetupExpression(UMaterialExpressionTextureCoordinate* Expression, const FName& Name)
	{
	}

	void SetupExpression(UMaterialExpressionScalarParameter* Expression, const FName& Name)
	{
		Expression->ParameterName = Name;
		Expression->DefaultValue = 0.5f;
	}

	template<typename TExpression>
	TExpression* AddCustomInput(UMaterial* Material, UMaterialExpressionCustom* CustomNode, const FName& InputName)
	{
		TExpression* Expression = NewObject<TExpression>(Material);
		SetupExpression(Expression, InputName);
		Design2UMGMaterialAccess::AddExpression(*Material, *Expression);

		FCustomInput Input;
		Input.InputName = InputName;
		Input.Input.Expression = Expression;
		CustomNode->Inputs.Add(Input);
		return Expression;
	}

	UMaterialExpressionComponentMask* AddComponentMask(
		UMaterial* Material,
		UMaterialExpression* Source,
		bool bR,
		bool bG,
		bool bB,
		bool bA)
	{
		UMaterialExpressionComponentMask* Mask = NewObject<UMaterialExpressionComponentMask>(Material);
		Mask->R = bR;
		Mask->G = bG;
		Mask->B = bB;
		Mask->A = bA;
		Mask->Input.Connect(0, Source);
		Design2UMGMaterialAccess::AddExpression(*Material, *Mask);
		return Mask;
	}
}

Design2UMGEffectMaterialBuilder::FSubtreeShadowKernel Design2UMGEffectMaterialBuilder::MakeSubtreeShadowKernel(float Sigma)
{
	// Keep the full three-sigma extent instead of truncating wide shadows. A
	// wider kernel changes sample spacing, never the output radius or opacity.
	constexpr int32 MaxRadius = 32;
	const float Extent = FMath::CeilToFloat(Sigma * 3.0f);
	const int32 Radius = static_cast<int32>(FMath::Min(Extent, static_cast<float>(MaxRadius)));
	const int32 SamplesPerAxis = 2 * Radius + 1; // Full discrete kernel, including both endpoints.
	return {Radius, Radius > 0 ? Extent / Radius : 1.0f, FMath::Square(SamplesPerAxis)};
}

bool Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(FUMGWidgetNode& Root, FString& OutError)
{
	for (FUMGWidgetNode& Child : Root.Children)
	{
		if (!PrepareSubtreeShadowMaterials(Child, OutError)) return false;
	}
	if (Root.Effect.EffectMaterial.Get(FString()) != UMGPropertyKeys::RetainerSubtreeShadowEffectRef) return true;
	if (Root.Bounds.W <= 0.0f || Root.Bounds.H <= 0.0f)
	{
		OutError = FString::Printf(TEXT("Subtree shadow '%s' has no valid capture size"), *Root.Name);
		return false;
	}
	FString Code = TEXT("// Subtree shadow: all layers sample the original composed alpha.\nfloat4 Base = Texture.SampleLevel(TextureSampler, UV, 0);\nfloat4 Shadow = float4(0,0,0,0);\n");
	for (int32 Index = Root.Shadow.BoxShadows.Num() - 1; Index >= 0; --Index)
	{
		const FUMGBoxShadowLayerData& Layer = Root.Shadow.BoxShadows[Index];
		if (!FMath::IsNearlyZero(Layer.Spread))
		{
			OutError = FString::Printf(TEXT("Subtree shadow '%s' layer %d uses spread, which is not yet represented"), *Root.Name, Index);
			return false;
		}
		Code += FString::Printf(TEXT("{ float2 Shifted = UV - float2(%.9f,%.9f); float Alpha = 0;\n"),
			Layer.Offset.X / Root.Bounds.W, Layer.Offset.Y / Root.Bounds.H);
		const float Sigma = Layer.Blur * 0.5f;
		if (!FMath::IsFinite(Sigma) || Sigma < 0.0f || Sigma > MAX_flt / 3.0f)
		{
			OutError = FString::Printf(TEXT("Subtree shadow '%s' layer %d has invalid blur"), *Root.Name, Index);
			return false;
		}
		if (Sigma < 0.5f)
		{
			Code += TEXT("if (all(Shifted >= 0) && all(Shifted <= 1)) Alpha = Texture.SampleLevel(TextureSampler, Shifted, 0).a;\n");
		}
		else
		{
			const FSubtreeShadowKernel Kernel = MakeSubtreeShadowKernel(Sigma);
			const int32 Radius = Kernel.Radius;
			const float GaussianDenominator = 2.0f * FMath::Square(Sigma / Kernel.Step);
			// Evaluate the discrete kernel in one arithmetic order for every texel.
			// Packing taps into bilinear samples is mathematically equivalent but changes
			// floating-point rounding at alpha quantization boundaries.
			Code += FString::Printf(TEXT(
				"float WeightSum=0; [loop] for(int y=-%d;y<=%d;++y) { [loop] for(int x=-%d;x<=%d;++x) { float Weight=exp(-float(x*x+y*y)/%.9f); float2 P=Shifted+float2(x*%.9f,y*%.9f); if(all(P>=0)&&all(P<=1)) Alpha+=Texture.SampleLevel(TextureSampler,P,0).a*Weight; WeightSum+=Weight; }} Alpha/=WeightSum;\n"),
				Radius, Radius, Radius, Radius, GaussianDenominator,
				Kernel.Step / Root.Bounds.W, Kernel.Step / Root.Bounds.H);
		}
		Code += FString::Printf(TEXT("float A=saturate(Alpha*%.9f); Shadow=float4(float3(%.9f,%.9f,%.9f)*A+Shadow.rgb*(1-A),A+Shadow.a*(1-A)); }\n"),
			Layer.Color.A, Layer.Color.R, Layer.Color.G, Layer.Color.B);
	}
	Code += TEXT("return float4(Base.rgb+Shadow.rgb*(1-Base.a),Base.a+Shadow.a*(1-Base.a));");
	const FString Name = TEXT("D2U_SubtreeShadow_") + FMD5::HashAnsiString(*Code);
	const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	// Independent editor processes share this content-addressed asset. Hold the
	// lock across the first load and save so a waiting process loads the winner.
	const FString LockName = TEXT("Design2UMG_Shadow_") + FMD5::HashAnsiString(*FPaths::ConvertRelativePathToFull(Filename));
	FSystemWideCriticalSection AssetLock(LockName, FTimespan::FromMinutes(2));
	if (!AssetLock.IsValid())
	{
		OutError = TEXT("Timed out acquiring shared subtree shadow material: ") + ObjectPath;
		return false;
	}
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
	if (Material)
	{
		const FMaterialResource* Resource = Material->GetMaterialResource(
#if UE_VERSION_OLDER_THAN(5, 7, 0)
			GMaxRHIFeatureLevel
#else
			GMaxRHIShaderPlatform
#endif
		);
		if (!Resource || !Resource->GetGameThreadShaderMap() || !Resource->GetGameThreadShaderMap()->IsValidForRendering())
		{
			Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
		}
		Design2UMGMaterialAccess::WaitForCompilation(*Material);
		if (!ValidateCompiledSubtreeMaterial(Material, Root.Name, OutError)) return false;
	}
	if (!Material)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Material = Cast<UMaterial>(GetMutableDefault<UMaterialFactoryNew>()->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn));
		if (!Material)
		{
			OutError = TEXT("Failed to create subtree shadow material: ") + ObjectPath;
			return false;
		}
		Material->MaterialDomain = EMaterialDomain::MD_UI;
		Material->BlendMode = EBlendMode::BLEND_AlphaComposite;
		Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		UMaterialExpressionCustom* Shader = NewObject<UMaterialExpressionCustom>(Material);
		Shader->OutputType = ECustomMaterialOutputType::CMOT_Float4;
		Shader->Code = Code;
		Design2UMGMaterialAccess::AddExpression(*Material, *Shader);
		AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("Texture"));
		AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, Shader, TEXT("UV"));
		Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = AddComponentMask(Material, Shader, true, true, true, false);
		Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = AddComponentMask(Material, Shader, false, false, false, true);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Design2UMGMaterialAccess::CacheShadersForRendering(*Material);
		Design2UMGMaterialAccess::WaitForCompilation(*Material);
		if (!ValidateCompiledSubtreeMaterial(Material, Root.Name, OutError))
		{
			Material->ClearFlags(RF_Public | RF_Standalone);
			Material->MarkAsGarbage();
			return false;
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Material, *Filename, Args))
		{
			OutError = TEXT("Failed to persist subtree shadow material: ") + Filename;
			Material->ClearFlags(RF_Public | RF_Standalone);
			Material->MarkAsGarbage();
			return false;
		}
	}
	Root.Effect.EffectMaterial = ObjectPath;
	Root.Shadow.BoxShadows.Reset();
	return true;
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateForegroundBlurRetainerMaterial()
{
	if (GForegroundBlurRetainerMaterial.IsValid())
	{
		return GForegroundBlurRetainerMaterial.Get();
	}
	constexpr TCHAR MaterialPackageName[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_LayerBlurRetainer");
	constexpr TCHAR MaterialObjectPath[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_LayerBlurRetainer.D2U_LayerBlurRetainer");
	if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, MaterialObjectPath))
	{
		Design2UMGMaterialAccess::RecompileForRendering(*ExistingMaterial);
		GForegroundBlurRetainerMaterial.Reset(ExistingMaterial);
		return ExistingMaterial;
	}

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}
	UPackage* MaterialPackage = CreatePackage(MaterialPackageName);
	if (!MaterialPackage) return nullptr;

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		MaterialPackage,
		TEXT("D2U_LayerBlurRetainer"),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));
	if (!Material)
	{
		return nullptr;
	}

	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->MaterialDomain = EMaterialDomain::MD_UI;
	Material->BlendMode = EBlendMode::BLEND_AlphaComposite;

	UMaterialExpressionCustom* BlurNode = NewObject<UMaterialExpressionCustom>(Material);
	BlurNode->OutputType = ECustomMaterialOutputType::CMOT_Float4;
	BlurNode->Code = TEXT(R"(
// Cross-shaped 5-tap blur: center 75/255 plus four taps at 45/255 each (sum = 1).
float2 Step = max(BlurUVStep.rg, float2(0.0, 0.0));
float2 Taps[5] = { float2(0.0, 0.0), float2(Step.x, 0.0), float2(-Step.x, 0.0), float2(0.0, Step.y), float2(0.0, -Step.y) };
float4 Result = float4(0.0, 0.0, 0.0, 0.0);
[unroll] for (int Tap = 0; Tap < 5; ++Tap)
{
    float2 SampleUV = UV + Taps[Tap];
    // Unpainted space outside the retained subtree contributes transparent color.
    float Inside = all(SampleUV >= float2(0.0, 0.0)) && all(SampleUV <= float2(1.0, 1.0)) ? 1.0 : 0.0;
    float Weight = Tap == 0 ? (75.0 / 255.0) : (45.0 / 255.0);
    Result += Texture.SampleLevel(TextureSampler, SampleUV, 0) * Inside * Weight;
}
return Result;
)");
	Design2UMGMaterialAccess::AddExpression(*Material, *BlurNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, BlurNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, BlurNode, TEXT("UV"));
	AddCustomInput<UMaterialExpressionVectorParameter>(Material, BlurNode, TEXT("BlurUVStep"));

	UMaterialExpressionComponentMask* EmissiveMask = AddComponentMask(Material, BlurNode, true, true, true, false);
	UMaterialExpressionComponentMask* OpacityMask = AddComponentMask(Material, BlurNode, false, false, false, true);

	Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = EmissiveMask;
	Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = OpacityMask;

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Design2UMGMaterialAccess::RecompileForRendering(*Material);
	const FString MaterialFileName = FPackageName::LongPackageNameToFilename(
		MaterialPackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MaterialFileName), true);
	if (!SaveGeneratedMaterialAsset(Material, MaterialFileName))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save foreground blur material package: %s"), *MaterialFileName);
		return nullptr;
	}

	GForegroundBlurRetainerMaterial.Reset(Material);
	return Material;
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial()
{
	if (GInnerShadowRetainerMaterial.IsValid())
	{
		return GInnerShadowRetainerMaterial.Get();
	}
	constexpr TCHAR MaterialPackageName[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_InsetShadowRetainer");
	constexpr TCHAR MaterialObjectPath[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_InsetShadowRetainer.D2U_InsetShadowRetainer");
	if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, MaterialObjectPath))
	{
		Design2UMGMaterialAccess::RecompileForRendering(*ExistingMaterial);
		GInnerShadowRetainerMaterial.Reset(ExistingMaterial);
		return ExistingMaterial;
	}

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}
	UPackage* MaterialPackage = CreatePackage(MaterialPackageName);
	if (!MaterialPackage) return nullptr;

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		MaterialPackage,
		TEXT("D2U_InsetShadowRetainer"),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));
	if (!Material)
	{
		return nullptr;
	}

	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->MaterialDomain = EMaterialDomain::MD_UI;
	Material->BlendMode = EBlendMode::BLEND_AlphaComposite;

	UMaterialExpressionCustom* InnerShadowNode = NewObject<UMaterialExpressionCustom>(Material);
	InnerShadowNode->OutputType = ECustomMaterialOutputType::CMOT_Float4;
	InnerShadowNode->Code = TEXT(R"(
// Cross-shaped 5-tap alpha blur: center 75/255 plus four taps at 45/255 each (sum = 1).
float2 Step = max(BlurUVStep.rg, float2(0.0, 0.0));
float2 Offset = InnerShadowOffset.rg;
float4 Base = Texture.SampleLevel(TextureSampler, UV, 0);
float2 Taps[5] = { float2(0.0, 0.0), float2(Step.x, 0.0), float2(-Step.x, 0.0), float2(0.0, Step.y), float2(0.0, -Step.y) };
float ShiftedAlpha = 0.0;
[unroll] for (int Tap = 0; Tap < 5; ++Tap)
{
    float2 SampleUV = UV - Offset + Taps[Tap];
    // The translated cutout is transparent outside its bounds, not edge-clamped.
    float Inside = all(SampleUV >= float2(0.0, 0.0)) && all(SampleUV <= float2(1.0, 1.0)) ? 1.0 : 0.0;
    float Weight = Tap == 0 ? (75.0 / 255.0) : (45.0 / 255.0);
    ShiftedAlpha += Texture.SampleLevel(TextureSampler, SampleUV, 0).a * Inside * Weight;
}
float ShadowMask = saturate(Base.a - ShiftedAlpha) * saturate(InnerShadowStrength);
float3 OutRgb = lerp(Base.rgb, InnerShadowColor.rgb * Base.a, ShadowMask);
return float4(OutRgb, Base.a);
)");
	Design2UMGMaterialAccess::AddExpression(*Material, *InnerShadowNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, InnerShadowNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, InnerShadowNode, TEXT("UV"));
	UMaterialExpressionVectorParameter* BlurStepParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("BlurUVStep"));
	BlurStepParam->DefaultValue = FLinearColor(BlurUVStepFallbackComponent, BlurUVStepFallbackComponent, 0.0f, 0.0f);
	UMaterialExpressionVectorParameter* ShadowColorParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("InnerShadowColor"));
	ShadowColorParam->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);
	UMaterialExpressionVectorParameter* ShadowOffsetParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("InnerShadowOffset"));
	ShadowOffsetParam->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	UMaterialExpressionScalarParameter* ShadowStrengthParam =
		AddCustomInput<UMaterialExpressionScalarParameter>(Material, InnerShadowNode, TEXT("InnerShadowStrength"));
	ShadowStrengthParam->DefaultValue = 0.5f;

	UMaterialExpressionComponentMask* EmissiveMask = AddComponentMask(Material, InnerShadowNode, true, true, true, false);
	UMaterialExpressionComponentMask* OpacityMask = AddComponentMask(Material, InnerShadowNode, false, false, false, true);

	Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = EmissiveMask;
	Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = OpacityMask;

	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Design2UMGMaterialAccess::RecompileForRendering(*Material);
	const FString MaterialFileName = FPackageName::LongPackageNameToFilename(
		MaterialPackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MaterialFileName), true);
	if (!SaveGeneratedMaterialAsset(Material, MaterialFileName))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save inner shadow material package: %s"), *MaterialFileName);
		return nullptr;
	}

	GInnerShadowRetainerMaterial.Reset(Material);
	return Material;
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial()
{
	if (GGeometryMaskRetainerMaterial.IsValid())
	{
		return GGeometryMaskRetainerMaterial.Get();
	}

	// RetainerBox stores the material reference on the generated Widget Blueprint. A
	// transient material is dropped when that asset is reloaded before rendering.
	// Keep one shared project asset so the reference survives the generation cycle.
	constexpr TCHAR MaterialPackageName[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_GeometryMaskRetainerV15");
	constexpr TCHAR MaterialObjectPath[] = TEXT("/Game/Design2UMG/GeneratedMaterials/D2U_GeometryMaskRetainerV15.D2U_GeometryMaskRetainerV15");
	if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, MaterialObjectPath))
	{
		Design2UMGMaterialAccess::RecompileForRendering(*ExistingMaterial);
		GGeometryMaskRetainerMaterial.Reset(ExistingMaterial);
		return ExistingMaterial;
	}

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}

	UPackage* MaterialPackage = CreatePackage(MaterialPackageName);
	if (!MaterialPackage)
	{
		return nullptr;
	}

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		MaterialPackage,
		TEXT("D2U_GeometryMaskRetainerV15"),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));
	if (!Material)
	{
		return nullptr;
	}

	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->MaterialDomain = EMaterialDomain::MD_UI;
	Material->BlendMode = EBlendMode::BLEND_AlphaComposite;

	UMaterialExpressionCustom* MaskNode = NewObject<UMaterialExpressionCustom>(Material);
	MaskNode->OutputType = ECustomMaterialOutputType::CMOT_Float4;
	MaskNode->Code = TEXT(R"(
	float2 Step = max(fwidth(UV), float2(0.00001, 0.00001));
	float4 Base = Texture.SampleLevel(TextureSampler, UV, 0);
	float4 CrossBlur = Base * 0.5;
	CrossBlur += Texture.SampleLevel(TextureSampler, UV + float2(Step.x, 0.0), 0) * 0.125;
	CrossBlur += Texture.SampleLevel(TextureSampler, UV - float2(Step.x, 0.0), 0) * 0.125;
	CrossBlur += Texture.SampleLevel(TextureSampler, UV + float2(0.0, Step.y), 0) * 0.125;
	CrossBlur += Texture.SampleLevel(TextureSampler, UV - float2(0.0, Step.y), 0) * 0.125;
	float4 Filtered = saturate(Base + 0.15 * (Base - CrossBlur));

float Mask = 1.0;
if (MaskShape < 0.5)
{
	float2 Position = UV * 2.0 - 1.0;
	float Distance = length(Position) - 1.0;
	Mask = Distance <= 0.5 * fwidth(Distance) ? 1.0 : 0.0;
}
else
{
	float2 Size = max(MaskSize.rg, float2(1.0, 1.0));
	float2 Position = UV * Size - Size * 0.5;
	float MaxRadius = min(Size.x, Size.y) * 0.5;
	float4 Radii = clamp(
		float4(MaskCornerRadiiTop.rg, MaskCornerRadiiBottom.rg),
		0.0,
		MaxRadius);
	float Radius = Position.x <= 0.0
		? (Position.y <= 0.0 ? Radii.x : Radii.w)
		: (Position.y <= 0.0 ? Radii.y : Radii.z);
	float2 Q = abs(Position) - (Size * 0.5 - Radius);
	float Distance = length(max(Q, 0.0)) + min(max(Q.x, Q.y), 0.0) - Radius;
	float Edge = max(0.5 * fwidth(Distance), 0.5);
	Mask = 1.0 - smoothstep(-Edge, Edge, Distance);
}
return Filtered * Mask;
)");
	Design2UMGMaterialAccess::AddExpression(*Material, *MaskNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, MaskNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, MaskNode, TEXT("UV"));
	UMaterialExpressionScalarParameter* ShapeParam =
		AddCustomInput<UMaterialExpressionScalarParameter>(Material, MaskNode, TEXT("MaskShape"));
	ShapeParam->DefaultValue = 0.0f;
	UMaterialExpressionVectorParameter* SizeParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, MaskNode, TEXT("MaskSize"));
	SizeParam->DefaultValue = FLinearColor(1.0f, 1.0f, 0.0f, 0.0f);
	UMaterialExpressionVectorParameter* TopRadiiParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, MaskNode, TEXT("MaskCornerRadiiTop"));
	TopRadiiParam->DefaultValue = FLinearColor::Transparent;
	UMaterialExpressionVectorParameter* BottomRadiiParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, MaskNode, TEXT("MaskCornerRadiiBottom"));
	BottomRadiiParam->DefaultValue = FLinearColor::Transparent;

	UMaterialExpressionComponentMask* MaskedRgb = AddComponentMask(Material, MaskNode, true, true, true, false);
	UMaterialExpressionComponentMask* MaskedAlpha = AddComponentMask(Material, MaskNode, false, false, false, true);

	Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = MaskedRgb;
	Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = MaskedAlpha;

	Material->AddToRoot();
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Design2UMGMaterialAccess::RecompileForRendering(*Material);

	const FString MaterialFileName = FPackageName::LongPackageNameToFilename(
		MaterialPackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MaterialFileName), true);
	if (!SaveGeneratedMaterialAsset(Material, MaterialFileName))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save geometry mask material package: %s"), *MaterialFileName);
		return nullptr;
	}

	GGeometryMaskRetainerMaterial.Reset(Material);
	Material->RemoveFromRoot();
	return Material;
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial(
	EUMGGeometryMaskShape Shape,
	const FVector2D& Size,
	const FVector4& CornerRadii)
{
	UMaterialInterface* BaseMaterial = GetOrCreateGeometryMaskRetainerMaterial();
	if (!BaseMaterial)
	{
		return nullptr;
	}

	const FString InstanceName = BuildGeometryMaskInstanceName(Shape, Size, CornerRadii);
	const FString PackageName = FString::Printf(
		TEXT("/Game/Design2UMG/GeneratedMaterials/GeometryMaskInstances/%s"),
		*InstanceName);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *InstanceName);
	if (UMaterialInstanceConstant* ExistingInstance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath))
	{
		Design2UMGMaterialAccess::RecompileForRendering(*ExistingInstance);
		return ExistingInstance;
	}

	TStrongObjectPtr<UMaterialInstanceConstant> Instance(NewObject<UMaterialInstanceConstant>(
		CreatePackage(*PackageName),
		*InstanceName,
		RF_Public | RF_Standalone));
	if (!Instance)
	{
		return nullptr;
	}

	Instance->SetParentEditorOnly(BaseMaterial, false);
	Instance->SetScalarParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("MaskShape")),
		Shape == EUMGGeometryMaskShape::Ellipse ? 0.0f : 1.0f);
	Instance->SetVectorParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("MaskSize")),
		FLinearColor(Size.X, Size.Y, 0.0f, 0.0f));
	Instance->SetVectorParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("MaskCornerRadiiTop")),
		FLinearColor(CornerRadii.X, CornerRadii.Y, 0.0f, 0.0f));
	Instance->SetVectorParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("MaskCornerRadiiBottom")),
		FLinearColor(CornerRadii.Z, CornerRadii.W, 0.0f, 0.0f));
	Instance->PostEditChange();
	Design2UMGMaterialAccess::RecompileForRendering(*Instance);

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
	if (!SaveGeneratedMaterialAsset(Instance.Get(), PackageFileName))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save geometry mask material instance: %s"), *PackageFileName);
		return nullptr;
	}

	return Instance.Get();
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateRetainerEffectMaterial(
	const FUMGEffectData& Effect, UMaterialInterface* ExistingMaterial)
{
	UMaterialInterface* Parent = ExistingMaterial;
	if (Effect.EffectMaterial.IsSet())
	{
		const FString& Reference = Effect.EffectMaterial.GetValue();
		if (Reference.IsEmpty()) return nullptr;
		if (Reference == UMGPropertyKeys::RetainerForegroundBlurEffectRef)
			Parent = GetOrCreateForegroundBlurRetainerMaterial();
		else if (Reference == UMGPropertyKeys::RetainerInnerShadowEffectRef)
			Parent = GetOrCreateInnerShadowRetainerMaterial();
		else if (Reference == UMGPropertyKeys::RetainerGeometryMaskEffectRef)
		{
			Parent = Effect.GeometryMaskShape.IsSet() && Effect.GeometryMaskSize.IsSet()
				&& Effect.GeometryMaskCornerRadii.IsSet()
				? GetOrCreateGeometryMaskRetainerMaterial(Effect.GeometryMaskShape.GetValue(),
					Effect.GeometryMaskSize.GetValue(), Effect.GeometryMaskCornerRadii.GetValue())
				: GetOrCreateGeometryMaskRetainerMaterial();
		}
		else
		{
			Parent = LoadObject<UMaterialInterface>(nullptr, *Reference);
			if (!Parent) Parent = FindObject<UMaterialInterface>(nullptr, *Reference);
		}
	}
	if (!Parent) return nullptr;

	TArray<TPair<FName, float>> Scalars;
	TArray<TPair<FName, FLinearColor>> Vectors;
	if (Effect.EffectBlurUVStep.IsSet())
	{
		const FVector2D& Step = Effect.EffectBlurUVStep.GetValue();
		Vectors.Emplace(TEXT("BlurUVStep"), FLinearColor(Step.X, Step.Y, 0.0f, 0.0f));
	}
	if (Effect.EffectColor.IsSet())
	{
		const FLinearColor Color = FUMGValueParser::ParseColor(
			Design2UMGWidgetNodeAccess::SerializeWidgetColor(Effect.EffectColor.GetValue()));
		Vectors.Emplace(TEXT("InnerShadowColor"), Color);
		Scalars.Emplace(TEXT("InnerShadowStrength"), Color.A);
	}
	if (Effect.EffectOffset.IsSet())
	{
		const FVector2D& Offset = Effect.EffectOffset.GetValue();
		Vectors.Emplace(TEXT("InnerShadowOffset"), FLinearColor(Offset.X, Offset.Y, 0.0f, 0.0f));
	}
	if (Effect.GeometryMaskShape.IsSet())
		Scalars.Emplace(TEXT("MaskShape"), Effect.GeometryMaskShape.GetValue() == EUMGGeometryMaskShape::Ellipse ? 0.0f : 1.0f);
	if (Effect.GeometryMaskSize.IsSet())
	{
		const FVector2D& Size = Effect.GeometryMaskSize.GetValue();
		Vectors.Emplace(TEXT("MaskSize"), FLinearColor(Size.X, Size.Y, 0.0f, 0.0f));
	}
	if (Effect.GeometryMaskCornerRadii.IsSet())
	{
		const FVector4& Radii = Effect.GeometryMaskCornerRadii.GetValue();
		Vectors.Emplace(TEXT("MaskCornerRadiiTop"), FLinearColor(Radii.X, Radii.Y, 0.0f, 0.0f));
		Vectors.Emplace(TEXT("MaskCornerRadiiBottom"), FLinearColor(Radii.Z, Radii.W, 0.0f, 0.0f));
	}
	bool bMatches = true;
	for (const auto& Parameter : Scalars)
	{
		float Value = 0.0f;
		bMatches &= Parent->GetScalarParameterValue(FMaterialParameterInfo(Parameter.Key), Value) && Value == Parameter.Value;
	}
	for (const auto& Parameter : Vectors)
	{
		FLinearColor Value;
		bMatches &= Parent->GetVectorParameterValue(FMaterialParameterInfo(Parameter.Key), Value) && Value == Parameter.Value;
	}
	if (bMatches) return Parent;

	// Parameter identity uses values, not process-local FName indices or rounded dimensions.
	TArray<uint8> Identity;
	FMemoryWriter Writer(Identity);
	FString ParentPath = Parent->GetPathName();
	Writer << ParentPath;
	int32 ScalarCount = Scalars.Num(), VectorCount = Vectors.Num();
	Writer << ScalarCount << VectorCount;
	for (auto& Parameter : Scalars)
	{
		FString Name = Parameter.Key.ToString();
		Writer << Name << Parameter.Value;
	}
	for (auto& Parameter : Vectors)
	{
		FString Name = Parameter.Key.ToString();
		Writer << Name << Parameter.Value;
	}
	FSHAHash Hash;
	FSHA1::HashBuffer(Identity.GetData(), Identity.Num(), Hash.Hash);
	const FString Name = TEXT("D2U_Retainer_") + Hash.ToString();
	const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/RetainerInstances/") + Name;
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	if (UMaterialInstanceConstant* Existing = FindObject<UMaterialInstanceConstant>(nullptr, *ObjectPath))
		return Existing;
	if (FPackageName::DoesPackageExist(PackageName))
		return LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);

	TStrongObjectPtr<UMaterialInstanceConstant> Instance(NewObject<UMaterialInstanceConstant>(
		CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone));
	if (!Instance) return nullptr;
	Instance->SetParentEditorOnly(Parent, false);
	for (const auto& Parameter : Scalars)
		Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Parameter.Key), Parameter.Value);
	for (const auto& Parameter : Vectors)
		Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Parameter.Key), Parameter.Value);
	Instance->PostEditChange();
	Design2UMGMaterialAccess::CacheShadersForRendering(*Instance);
	Design2UMGMaterialAccess::WaitForCompilation(*Instance);
	const FString FileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), true);
	if (!SaveGeneratedMaterialAsset(Instance.Get(), FileName)) return nullptr;
	return Instance.Get();
}

bool Design2UMGEffectMaterialBuilder::PrepareRetainerEffectMaterials(
	FUMGWidgetNode& Root, TArray<TStrongObjectPtr<UMaterialInterface>>& Materials, FString& OutError)
{
	for (FUMGWidgetNode& Child : Root.Children)
	{
		if (!PrepareRetainerEffectMaterials(Child, Materials, OutError)) return false;
	}
	FUMGEffectData& Effect = Root.Effect;
	if (!Effect.EffectMaterial.IsSet() || Effect.EffectMaterial.GetValue().IsEmpty()) return true;
	UMaterialInterface* Material = GetOrCreateRetainerEffectMaterial(Effect);
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Failed to create or persist retainer effect material for '%s'"), *Root.Name);
		return false;
	}
	Materials.Emplace(Material);
	Effect.EffectMaterial = Material->GetPathName();
	return true;
}
