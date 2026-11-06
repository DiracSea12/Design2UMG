// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEffectMaterialBuilder.h"
#include "PencilShaderMaterials.h"
#include "PencilShaderSdf.h"
#include "PreBuildResourceNormalizer.h"
#include "DesignPathRasterizer.h"
#include "DesignImageResourceLimits.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Components/RetainerBox.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

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
#include "UMGWidgetTypes.h"
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
	FString InnerShadowPaintCode(int32 BlendMode, const TCHAR* BaseExpression)
	{
	FString Code = TEXT(R"(
// Cross-shaped 5-tap alpha blur: center 75/255 plus four taps at 45/255 each (sum = 1).
float2 Step = max(BlurUVStep.rg, float2(0.0, 0.0));
float2 Offset = InnerShadowOffset.rg;
float4 Base = )");
	Code += BaseExpression;
	Code += TEXT(R"(;
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
)");
	if (BlendMode == 0)
		Code += TEXT("float3 OutRgb = lerp(Base.rgb, InnerShadowColor.rgb * Base.a, ShadowMask);\n");
	else
	{
		// Blend only the inset paint against the original object, retaining its coverage.
		Code += FString::Printf(TEXT("FD2UBlend blend; float3 ObjectColor=Base.a>0?Base.rgb/Base.a:float3(0,0,0); float3 OutRgb=blend.Mix(float4(InnerShadowColor.rgb*ShadowMask,ShadowMask),float4(ObjectColor,1),%d).rgb*Base.a;\n"), BlendMode);
	}
	return Code;
	}


	void AddInnerShadowInputs(UMaterial* Material, UMaterialExpressionCustom* InnerShadowNode)
	{
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
	int32 PaintMode = 0;
	if (!PencilShaderMaterials::BlendModeIndex(Root.Render.PaintBlendMode.Get(FString()), PaintMode, OutError)) return false;
	const bool InnerShadow = Root.Effect.EffectColor.IsSet();
	int32 InnerMode = 0;
	if (InnerShadow && !PencilShaderMaterials::BlendModeIndex(Root.Effect.InnerShadowBlendMode.Get(FString()), InnerMode, OutError)) return false;
	bool Context = PaintMode != 0 || InnerShadow;
	for (const auto& Layer : Root.Shadow.BoxShadows)
	{
		int32 Mode = 0;
		if (!PencilShaderMaterials::BlendModeIndex(Layer.BlendMode, Mode, OutError)) return false;
		Context |= Mode != 0;
	}
	FString Code = TEXT("// Subtree shadow: all layers sample the original composed alpha.\nfloat4 Base = Texture.SampleLevel(TextureSampler, UV, 0);\nfloat4 Shadow = float4(0,0,0,0);\n");
	if (Context) Code = PencilShaderMaterials::BlendLibrary() + TEXT("FD2UBlend blend; float4 Base=Texture.SampleLevel(TextureSampler,UV,0); float2 BUV=float2(dot(float3(UV,1),BackgroundU.xyz),dot(float3(UV,1),BackgroundV.xyz)); float4 Destination=all(BUV>=0)&&all(BUV<=1)?D2UBackground.SampleLevel(D2UBackgroundSampler,saturate(BUV),0):float4(0,0,0,0); float4 Shadow=Destination; float PaintAlpha=0;\n");
	for (int32 Index = Root.Shadow.BoxShadows.Num() - 1; Index >= 0; --Index)
	{
		const FUMGBoxShadowLayerData& Layer = Root.Shadow.BoxShadows[Index];
		// Retainer effects use NoGamma and composite captured sRGB pixels.
		const FLinearColor DisplayColor = Layer.Color.ToFColor(true).ReinterpretAsLinear();
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
		if (Context)
		{
			int32 Mode = 0;
			PencilShaderMaterials::BlendModeIndex(Layer.BlendMode, Mode, OutError);
			Code += FString::Printf(TEXT("float A=saturate(Alpha*%.9f); Shadow=blend.Mix(float4(float3(%.9f,%.9f,%.9f)*A,A),Shadow,%d); PaintAlpha=A+PaintAlpha*(1-A); }\n"), Layer.Color.A, DisplayColor.R, DisplayColor.G, DisplayColor.B, Mode);
		}
		else
		Code += FString::Printf(TEXT("float A=saturate(Alpha*%.9f); Shadow=float4(float3(%.9f,%.9f,%.9f)*A+Shadow.rgb*(1-A),A+Shadow.a*(1-A)); }\n"),
			Layer.Color.A, DisplayColor.R, DisplayColor.G, DisplayColor.B);
	}
	if (InnerShadow)
	{
		Code += FString::Printf(TEXT("float A=Base.a+PaintAlpha*(1-Base.a); float4 Composed=blend.Mix(Base,Shadow,%d); float4 Fill=float4(Composed.rgb-Shadow.rgb*(1-Base.a),Base.a);\n{\n"), PaintMode);
		Code += InnerShadowPaintCode(InnerMode, TEXT("Fill"));
		Code += TEXT("Composed.rgb+=OutRgb-Base.rgb;\n}\nreturn float4(Composed.rgb-Destination.rgb*(1-A),A);");
	}
	else if (PaintMode != 0)
		Code += FString::Printf(TEXT("float A=Base.a+PaintAlpha*(1-Base.a); float4 Composed=blend.Mix(Base,Shadow,%d); return float4(Composed.rgb-Destination.rgb*(1-A),A);"), PaintMode);
	else
		Code += Context ? TEXT("float A=Base.a+PaintAlpha*(1-Base.a); return float4(Base.rgb+Shadow.rgb*(1-Base.a)-Destination.rgb*(1-A),A);")
		: TEXT("return float4(Base.rgb+Shadow.rgb*(1-Base.a),Base.a+Shadow.a*(1-Base.a));");
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
		if (InnerShadow) AddInnerShadowInputs(Material, Shader);
		if (Context)
		{
			auto* Background = AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("D2UBackground"));
			Background->Texture = PencilShaderSdf::SamplerDefault(OutError);
			if (!Background->Texture) return false;
			Background->SamplerType = SAMPLERTYPE_LinearColor;
			auto* U = AddCustomInput<UMaterialExpressionVectorParameter>(Material, Shader, TEXT("BackgroundU"));
			U->ParameterName = TEXT("D2UBackgroundU"); U->DefaultValue = FLinearColor(1,0,0,0);
			auto* V = AddCustomInput<UMaterialExpressionVectorParameter>(Material, Shader, TEXT("BackgroundV"));
			V->ParameterName = TEXT("D2UBackgroundV"); V->DefaultValue = FLinearColor(0,1,0,0);
		}
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
	Root.Render.PaintBlendMode.Reset();
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

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial(int32 BlendMode)
{
	if (BlendMode == 0 && GInnerShadowRetainerMaterial.IsValid())
	{
		return GInnerShadowRetainerMaterial.Get();
	}
	FString Code = InnerShadowPaintCode(BlendMode, TEXT("Texture.SampleLevel(TextureSampler, UV, 0)"));
	Code += TEXT("return float4(OutRgb, Base.a);\n");
	if (BlendMode != 0) Code = PencilShaderMaterials::BlendLibrary() + Code;
	const FString MaterialName = BlendMode == 0 ? TEXT("D2U_InsetShadowRetainer")
		: TEXT("D2U_InsetShadowRetainer_") + FMD5::HashAnsiString(*Code);
	const FString MaterialPackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + MaterialName;
	const FString MaterialObjectPath = MaterialPackageName + TEXT(".") + MaterialName;
	if (UMaterial* ExistingMaterial = LoadObject<UMaterial>(nullptr, *MaterialObjectPath))
	{
		Design2UMGMaterialAccess::RecompileForRendering(*ExistingMaterial);
		if (BlendMode == 0) GInnerShadowRetainerMaterial.Reset(ExistingMaterial);
		return ExistingMaterial;
	}

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}
	UPackage* MaterialPackage = CreatePackage(*MaterialPackageName);
	if (!MaterialPackage) return nullptr;

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		MaterialPackage,
		FName(*MaterialName),
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
	InnerShadowNode->Code = MoveTemp(Code);
	Design2UMGMaterialAccess::AddExpression(*Material, *InnerShadowNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, InnerShadowNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, InnerShadowNode, TEXT("UV"));
	AddInnerShadowInputs(Material, InnerShadowNode);

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

	if (BlendMode == 0) GInnerShadowRetainerMaterial.Reset(Material);
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
		{
			int32 BlendMode = 0;
			FString Error;
			if (!PencilShaderMaterials::BlendModeIndex(Effect.InnerShadowBlendMode.Get(FString()), BlendMode, Error))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Error);
				return nullptr;
			}
			Parent = GetOrCreateInnerShadowRetainerMaterial(BlendMode);
		}
		else if (Reference == UMGPropertyKeys::RetainerSourceContourEffectRef)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Source contour dependencies must be prepared before binding the native retainer"));
			return nullptr;
		}
		else if (Reference == UMGPropertyKeys::RetainerGeometryMaskEffectRef)
		{
			if (Effect.GeometryMaskPaths.IsSet())
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Source contour dependencies must be prepared before binding the native retainer"));
				return nullptr;
			}
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
	if (Effect.GeometryMaskSize.IsSet() && !Effect.GeometryMaskPaths.IsSet())
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

namespace
{
bool EnsurePaintMaterial(UMaterial* Material, const FString& NodeName, FString& Error)
{
    FString CompileErrors, CompileStates;
    if (Design2UMGMaterialAccess::EnsureReadyForRendering(*Material, CompileErrors, CompileStates)) return true;
    Error = FString::Printf(TEXT("Native paint '%s' material '%s' is not ready for rendering: %s; %s"),
        *NodeName, *Material->GetPathName(), *CompileErrors, *CompileStates);
    return false;
}

UTexture2D* PrepareSourceContourTexture(const FUMGWidgetNode& Root, FString& Error,
	DesignImageResources::FImageOperationBudget* Budget)
{
	const auto& Effect = Root.Effect;
	if (!Effect.GeometryMaskPaths.IsSet() || Effect.GeometryMaskPaths->IsEmpty() || !Effect.GeometryMaskSize.IsSet())
	{ Error = TEXT("source contour mask requires paths and a local canvas"); return nullptr; }
	FUMGWidgetNode Coverage;
	Coverage.Type = UMGWidgetTypes::Image;
	Coverage.Id = Root.Id + TEXT("__coverage");
	Coverage.Name = Coverage.Id;
	Coverage.Path.CanvasSize = Effect.GeometryMaskSize.GetValue();
	Coverage.Path.ViewBox = FVector4(0, 0, Effect.GeometryMaskSize->X, Effect.GeometryMaskSize->Y);
	Coverage.Path.Geometry = Effect.GeometryMaskPaths.GetValue()[0].Path;
	Coverage.Path.FillColor = FLinearColor::White;
	FUMGSourcePaintGeometry Source;
	Source.Geometry.FillPaths = Effect.GeometryMaskPaths.GetValue();
	Source.Geometry.StrokePaths = TArray<FDesignGeometryPath>();
	FUMGSurfacePaint White;
	White.Color = FLinearColor::White;
	Source.FillPaints = TArray<FUMGSurfacePaint>{White};
	FUMGSurfacePaint EmptyStroke;
	EmptyStroke.Color = FLinearColor::Transparent;
	Source.StrokePaint = EmptyStroke;
	TArray<uint8> CoverageIdentity;
	FMemoryWriter CoverageWriter(CoverageIdentity);
	FVector2D Canvas = Effect.GeometryMaskSize.GetValue();
	CoverageWriter << Canvas << Source;
	FSHAHash CoverageHash;
	FSHA1::HashBuffer(CoverageIdentity.GetData(), CoverageIdentity.Num(), CoverageHash.Hash);
	Coverage.Name = TEXT("Coverage_") + CoverageHash.ToString();
	Coverage.Path.SourcePaintGeometry = MoveTemp(Source);
	Coverage.Effect.GeometryMaskPaths = Effect.GeometryMaskPaths;
	if (!FPreBuildResourceNormalizer::NormalizePathTextures(Coverage,
		TEXT("/Game/Design2UMG/GeneratedTextures/SourceContours"), TEXT("SourceContour"), &Error, Budget)) return nullptr;
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Coverage.Brush.Image.TexturePath.Get(FString()));
	if (!Texture) { Error = TEXT("source contour coverage texture was not persisted"); return nullptr; }
	return Texture;
}

UMaterialInterface* PrepareSourceContourMaterial(const FUMGWidgetNode& Root, FString& Error,
	DesignImageResources::FImageOperationBudget* Budget)
{
	UTexture2D* Texture = PrepareSourceContourTexture(Root, Error, Budget);
	if (!Texture) return nullptr;
	const FString Code = TEXT("return Texture.SampleLevel(TextureSampler,UV,0)*Coverage.SampleLevel(CoverageSampler,UV,0).a;");
	const FString Identity = Code + Texture->GetPathName();
	const FString Name = TEXT("D2U_SourceContour_") + FMD5::HashAnsiString(*Identity);
	const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSystemWideCriticalSection AssetLock(TEXT("Design2UMG_Contour_") + FMD5::HashAnsiString(*FPaths::ConvertRelativePathToFull(Filename)), FTimespan::FromMinutes(2));
	if (!AssetLock.IsValid()) { Error = TEXT("could not acquire source contour material"); return nullptr; }
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
	if (!Material)
	{
		Material = Cast<UMaterial>(GetMutableDefault<UMaterialFactoryNew>()->FactoryCreateNew(UMaterial::StaticClass(),
			CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone, nullptr, GWarn));
		if (!Material) { Error = TEXT("could not create source contour material"); return nullptr; }
		Material->MaterialDomain = EMaterialDomain::MD_UI;
		Material->BlendMode = BLEND_AlphaComposite;
		Material->SetShadingModel(MSM_Unlit);
		auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
		Shader->Code = Code;
		Shader->OutputType = CMOT_Float4;
		Design2UMGMaterialAccess::AddExpression(*Material, *Shader);
		AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("Texture"));
		AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, Shader, TEXT("UV"));
		AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("Coverage"))->Texture = Texture;
		Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = AddComponentMask(Material, Shader, true, true, true, false);
		Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = AddComponentMask(Material, Shader, false, false, false, true);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		if (!EnsurePaintMaterial(Material, Root.Name, Error)) return nullptr;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		if (!SaveGeneratedMaterialAsset(Material, Filename)) { Error = TEXT("could not persist source contour material"); return nullptr; }
	}
	return EnsurePaintMaterial(Material, Root.Name, Error) ? Material : nullptr;
}

bool PreparePaintMaterial(FUMGWidgetNode& Root, const TArray<FUMGSurfacePaint>& SourcePaints,
    const TArray<FUMGSurfacePaintTarget>& Targets, const FString& Coverage,
    bool bText, const FVector2D* SurfaceSize, FString& Error, DesignImageResources::FImageOperationBudget* Budget, UTexture2D* SourceCoverage = nullptr)
{
    TArray<TArray<FUMGSurfacePaint>> Stacks;
    Stacks.Add(SourcePaints);
    for (const auto& Target : Targets) Stacks.Add(Target.Paints);
	FUMGWidgetNode Textures;
	TArray<TArray<int32>> TextureIndices;
	for (const auto& Stack : Stacks)
	{
		auto& Indices = TextureIndices.AddDefaulted_GetRef();
		for (const auto& Paint : Stack)
		{
			int32 Index = INDEX_NONE;
			if (Paint.Image.HasAny()) { Error = TEXT("Native paint does not represent image fills"); return false; }
			if (Paint.Gradient.HasAny())
			{
                if (!bText && (Paint.Gradient.Mesh.IsSet()
                    || (Paint.Gradient.Type.IsSet() && !Paint.Gradient.Type->Equals(TEXT("Linear"), ESearchCase::IgnoreCase))))
                { Error = TEXT("Native surface paint animation supports linear gradients only"); return false; }
				Index = Textures.Children.Num();
				auto& Texture = Textures.Children.AddDefaulted_GetRef();
				Texture.Type = TEXT("Image"); Texture.Name = FString::Printf(TEXT("TextPaint%d"), Index);
				Texture.Id = Texture.Name; Texture.Bounds.W = Root.Bounds.W; Texture.Bounds.H = Root.Bounds.H;
				Texture.Brush.Gradient = Paint.Gradient;
			}
			Indices.Add(Index);
		}
	}
	if (!Textures.Children.IsEmpty() && !FPreBuildResourceNormalizer::NormalizeProceduralTextures(Textures,
		TEXT("/Game/Design2UMG/GeneratedTextures"), TEXT("TextPaint"), &Error, Budget)) return false;
	TArray<TObjectPtr<UTexture2D>> PaintTextures;
	for (const auto& Texture : Textures.Children)
	{
		auto* Asset = LoadObject<UTexture2D>(nullptr, *Texture.Brush.Image.TexturePath);
		if (!Asset) { Error = TEXT("Generated paint texture was not persisted"); return false; }
		PaintTextures.Add(Asset);
	}
	const auto Sample = [&](int32 Stack, int32 Layer)
	{
		const auto& Paint = Stacks[Stack][Layer];
		const int32 Texture = TextureIndices[Stack][Layer];
		if (Texture != INDEX_NONE)
			return FString::Printf(TEXT("Paint%d.SampleLevel(Paint%dSampler,UV,0)"), Texture, Texture);
		const FLinearColor C = Paint.Color;
		return FString::Printf(TEXT("float4(%.9g,%.9g,%.9g,%.9g)"), C.R,C.G,C.B,C.A);
	};
	FString Code = PencilShaderMaterials::BlendLibrary() + TEXT("\nFD2UBlend blend; float4 Composed=float4(0,0,0,0);\n");
	for (int32 Stack = 0; Stack < Stacks.Num(); ++Stack)
	{
		Code += Stack == 0 ? TEXT("if(PaintTarget<0) {\n") : FString::Printf(TEXT("else if(PaintTarget<%.1f) {\n"), Stack-0.5f);
		const auto& From = Stacks[0]; const auto& To = Stacks[Stack];
		for (int32 Layer=0; Layer < FMath::Max(From.Num(), To.Num()); ++Layer)
		{
			const FUMGSurfacePaint* A = From.IsValidIndex(Layer) ? &From[Layer] : nullptr;
			const FUMGSurfacePaint* B = To.IsValidIndex(Layer) ? &To[Layer] : nullptr;
			int32 Mode = 0, OtherMode = 0;
			if (!PencilShaderMaterials::BlendModeIndex((A ? A : B)->BlendMode, Mode, Error)
				|| (A && B && (!PencilShaderMaterials::BlendModeIndex(B->BlendMode, OtherMode, Error) || Mode != OtherMode)))
			{ if (Error.IsEmpty()) Error = TEXT("Animated paint blend mode changes are not represented"); return false; }
			if (A && B && A->Gradient.HasAny() != B->Gradient.HasAny())
			{ Error = TEXT("Animated paint fill type changes are not represented"); return false; }
			if (A && B && A->Gradient.HasAny())
			{
				FUMGSurfacePaint Left = *A, Right = *B;
				Left.Color=Right.Color=FLinearColor::White; Left.Opacity=Right.Opacity=1;
				Left.BlendMode.Reset(); Right.BlendMode.Reset();
				for (auto& Stop : Left.Gradient.Stops) Stop.Color = FLinearColor::White;
				for (auto& Stop : Right.Gradient.Stops) Stop.Color = FLinearColor::White;
				TArray<uint8> L,R; FMemoryWriter LW(L), RW(R); Left.WriteSignature(LW); Right.WriteSignature(RW);
				if (L != R) { Error = TEXT("Animated gradient geometry or stop positions change"); return false; }
			}
			const FString Source = A ? Sample(0,Layer) : Sample(Stack,Layer);
			const FString Destination = B ? Sample(Stack,Layer) : Source;
			Code += FString::Printf(TEXT("{ float4 C=lerp(%s,%s,PaintProgress); float A=saturate(C.a*lerp(%.9g,%.9g,PaintProgress)); Composed=blend.Mix(float4(C.rgb*A,A),Composed,%d); }\n"),
				*Source,*Destination,A?A->Opacity:0.0f,B?B->Opacity:0.0f,Mode);
		}
		Code += TEXT("}\n");
	}
	Code += Coverage;
	FString Identity = Code;
    if (SurfaceSize) Identity += FString::Printf(TEXT("|%.9g|%.9g"),SurfaceSize->X,SurfaceSize->Y);
	for (const auto& Texture : PaintTextures) Identity += Texture->GetPathName();
	if (SourceCoverage) Identity += SourceCoverage->GetPathName();
	const FString Name = (bText ? TEXT("D2U_TextPaint_") : TEXT("D2U_SurfacePaint_")) + FMD5::HashAnsiString(*Identity);
	const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
	const FString ObjectPath = PackageName + TEXT(".") + Name;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSystemWideCriticalSection AssetLock(TEXT("Design2UMG_Paint_") + FMD5::HashAnsiString(*FPaths::ConvertRelativePathToFull(Filename)), FTimespan::FromMinutes(2));
	if (!AssetLock.IsValid()) { Error = TEXT("Could not acquire generated paint material"); return false; }
	UMaterial* Material = LoadObject<UMaterial>(nullptr,*ObjectPath);
	if (!Material)
	{
		UPackage* Package = CreatePackage(*PackageName);
		Material = Cast<UMaterial>(GetMutableDefault<UMaterialFactoryNew>()->FactoryCreateNew(UMaterial::StaticClass(),Package,*Name,RF_Public|RF_Standalone,nullptr,GWarn));
		if (!Material) { Error = TEXT("Could not create native paint material"); return false; }
		Material->MaterialDomain = EMaterialDomain::MD_UI; Material->BlendMode = BLEND_AlphaComposite;
		Material->SetShadingModel(MSM_Unlit);
		auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
		Shader->Code = Code; Shader->OutputType = CMOT_Float4; Design2UMGMaterialAccess::AddExpression(*Material,*Shader);
		if (bText) AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material,Shader,TEXT("Texture"));
		AddCustomInput<UMaterialExpressionTextureCoordinate>(Material,Shader,TEXT("UV"));
		if (SourceCoverage) AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material,Shader,TEXT("SourceCoverage"))->Texture = SourceCoverage;
		AddCustomInput<UMaterialExpressionScalarParameter>(Material,Shader,TEXT("PaintTarget"))->DefaultValue=-1;
		AddCustomInput<UMaterialExpressionScalarParameter>(Material,Shader,TEXT("PaintProgress"))->DefaultValue=0;
        if (SurfaceSize)
        {
            AddCustomInput<UMaterialExpressionScalarParameter>(Material,Shader,TEXT("SurfaceWidth"))->DefaultValue=SurfaceSize->X;
            AddCustomInput<UMaterialExpressionScalarParameter>(Material,Shader,TEXT("SurfaceHeight"))->DefaultValue=SurfaceSize->Y;
        }
		for (int32 Index=0; Index<PaintTextures.Num(); ++Index)
		{
			auto* Input=AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material,Shader,*FString::Printf(TEXT("Paint%d"),Index));
			Input->Texture=PaintTextures[Index]; Input->SamplerType=SAMPLERTYPE_Color;
		}
		Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression=AddComponentMask(Material,Shader,true,true,true,false);
		Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression=AddComponentMask(Material,Shader,false,false,false,true);
		Material->PreEditChange(nullptr); Material->PostEditChange();
		if (!EnsurePaintMaterial(Material,Root.Name,Error)) return false;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
		if (!SaveGeneratedMaterialAsset(Material,Filename)) { Error=TEXT("Could not persist native paint material"); return false; }
	}
	if (!EnsurePaintMaterial(Material,Root.Name,Error)) return false;
	if (bText) Root.Effect.EffectMaterial=ObjectPath;
    else Root.Brush.MaterialRef=ObjectPath;
    return true;
}
bool PrepareTextPaintMaterial(FUMGWidgetNode& Root, FString& Error, DesignImageResources::FImageOperationBudget* Budget)
{
    if (!Root.Effect.TextPaints.IsSet()) { Error = TEXT("Native text paint material has no source paints"); return false; }
    return PreparePaintMaterial(Root, Root.Effect.TextPaints.GetValue(),
        Root.Effect.TextPaintTargets.Get(TArray<FUMGSurfacePaintTarget>()),
        TEXT("return Composed*Texture.SampleLevel(TextureSampler,UV,0).a;"), true, nullptr, Error, Budget);
}
}

bool Design2UMGEffectMaterialBuilder::PrepareSurfacePaintMaterial(FUMGWidgetNode& Producer,
    const TArray<FUMGSurfacePaint>& SourcePaints, const TArray<FUMGSurfacePaintTarget>& Targets,
    EUMGGeometryMaskShape Shape, const FVector2D& Size, const FVector4& CornerRadii, FString& Error,
    DesignImageResources::FImageOperationBudget* Budget)
{
    FString Coverage;
    if (Shape == EUMGGeometryMaskShape::Rectangle) Coverage = TEXT("return Composed;");
    else if (Shape == EUMGGeometryMaskShape::Ellipse)
        Coverage = TEXT("float Distance=length(UV*2-1)-1; float Edge=max(0.5*fwidth(Distance),0.00001); return Composed*(1-smoothstep(-Edge,Edge,Distance));");
    else
        Coverage = FString::Printf(TEXT("float2 Size=float2(SurfaceWidth,SurfaceHeight); float2 P=UV*Size-Size*0.5; float4 R=clamp(float4(%.9g,%.9g,%.9g,%.9g),0,min(Size.x,Size.y)*0.5); float Radius=P.x<=0?(P.y<=0?R.x:R.w):(P.y<=0?R.y:R.z); float2 Q=abs(P)-(Size*0.5-Radius); float Distance=length(max(Q,0))+min(max(Q.x,Q.y),0)-Radius; float Edge=max(0.5*fwidth(Distance),0.5); return Composed*(1-smoothstep(-Edge,Edge,Distance));"),
            CornerRadii.X, CornerRadii.Y, CornerRadii.Z, CornerRadii.W);
    UTexture2D* SourceCoverage = nullptr;
    if (Producer.Effect.GeometryMaskPaths.IsSet())
    {
        SourceCoverage = PrepareSourceContourTexture(Producer, Error, Budget);
        if (!SourceCoverage) return false;
        Coverage = TEXT("return Composed*SourceCoverage.SampleLevel(SourceCoverageSampler,UV,0).a;");
    }
    return PreparePaintMaterial(Producer, SourcePaints, Targets, Coverage, false, &Size, Error, Budget, SourceCoverage);
}


namespace
{
bool PrepareCaptureEndpoint(FUMGSharedCaptureEndpoint& Endpoint, bool bShadow, FString& Error,
    DesignImageResources::FImageOperationBudget* Budget)
{
    FIntPoint RasterSize;
    if (!FDesignPathRasterizer::TryGetRasterSize(Endpoint.Size.X, Endpoint.Size.Y, RasterSize))
    { Error = TEXT("Shared capture endpoint has invalid native paint size"); return false; }
    Endpoint.CoverageTexture.Reset();
    if (!bShadow && Endpoint.bClipContent && Endpoint.ClipPaths.IsSet())
    {
        FUMGWidgetNode Coverage;
        Coverage.Name = TEXT("SharedCaptureCoverage");
        Coverage.Effect.GeometryMaskPaths = Endpoint.ClipPaths;
        Coverage.Effect.GeometryMaskSize = Endpoint.Size;
        UTexture2D* Texture = PrepareSourceContourTexture(Coverage, Error, Budget);
        if (!Texture) return false;
        Endpoint.CoverageTexture = Texture->GetPathName();
    }
    if (!bShadow || Endpoint.BoxShadows.IsEmpty()) return true;
    for (const auto& Shadow : Endpoint.BoxShadows)
    {
        int32 Blend = 0;
        if (!PencilShaderMaterials::BlendModeIndex(Shadow.BlendMode, Blend, Error)) return false;
        if (Blend != 0 || !FMath::IsNearlyZero(Shadow.Spread)
            || !FMath::IsFinite(Shadow.Blur) || Shadow.Blur < 0 || Shadow.Blur > MAX_flt / 3.0f)
        { Error = TEXT("Shared capture endpoint shadow requires supported normal paint and zero spread"); return false; }
    }
    return true;
}

struct FCaptureTextureInput
{
    FString Name;
    UTexture2D* Texture = nullptr;
};

FString CaptureAlphaSample(const FString& Name, const FVector4& Rect, const TCHAR* Position)
{
    return FString::Printf(TEXT("(all((%s)>=float2(%.17g,%.17g))&&all((%s)<=float2(%.17g,%.17g))?%s.SampleLevel(%sSampler,((%s)-float2(%.17g,%.17g))/float2(%.17g,%.17g),0).a:0)"),
        Position, Rect.X, Rect.Y, Position, Rect.X + Rect.Z, Rect.Y + Rect.W,
        *Name, *Name, Position, Rect.X, Rect.Y, Rect.Z, Rect.W);
}

FString CaptureEndpointCode(const FUMGSharedCaptureEndpoint& Endpoint, int32 Index,
    const TCHAR* Mask, const TCHAR* Shadow, bool bShadow, TArray<FCaptureTextureInput>& Inputs)
{
    const auto TextureInput = [&](const FString& Path, const TCHAR* Role)
    {
        const FString Name = FString::Printf(TEXT("Endpoint%d%s"), Index, Role);
        Inputs.Add({Name, LoadObject<UTexture2D>(nullptr, *Path)});
        return Name;
    };
    FString Code = FString::Printf(TEXT("float %s=1; float4 %s=float4(0,0,0,0);\n"), Mask, Shadow);
    Code += FString::Printf(TEXT("{ float2 LocalP=P-(float2(SurfaceWidth,SurfaceHeight)-float2(%.17g,%.17g))*CapturePivot.xy;\n"), Endpoint.Size.X, Endpoint.Size.Y);
    if (!bShadow && Endpoint.bClipContent)
    {
        if (!Endpoint.CoverageTexture.IsEmpty())
        {
            const FString Name = TextureInput(Endpoint.CoverageTexture, TEXT("Mask"));
            Code += FString::Printf(TEXT("%s=%s;\n"), Mask,
                *CaptureAlphaSample(Name, FVector4(0, 0, Endpoint.Size.X, Endpoint.Size.Y), TEXT("LocalP")));
        }
        else
        {
            Code += FString::Printf(TEXT("{ float2 Size=float2(%.17g,%.17g); float2 Q=LocalP-Size*0.5; float D;\n"), Endpoint.Size.X, Endpoint.Size.Y);
            if (Endpoint.Shape == EUMGGeometryMaskShape::Ellipse)
                Code += TEXT("D=(length(Q/(Size*0.5))-1)*min(Size.x,Size.y)*0.5;\n");
            else
            {
                const FVector4 R = Endpoint.Shape == EUMGGeometryMaskShape::Rectangle ? FVector4(0,0,0,0) : Endpoint.CornerRadii;
                Code += FString::Printf(TEXT("float4 Radii=clamp(float4(%.17g,%.17g,%.17g,%.17g),0,min(Size.x,Size.y)*0.5); float R=Q.x<=0?(Q.y<=0?Radii.x:Radii.w):(Q.y<=0?Radii.y:Radii.z); Q=abs(Q)-(Size*0.5-R); D=length(max(Q,0))+min(max(Q.x,Q.y),0)-R;\n"), R.X,R.Y,R.Z,R.W);
            }
            Code += FString::Printf(TEXT("float Edge=max(0.5*fwidth(D),0.5); %s=(all(LocalP>=0)&&all(LocalP<=Size))?(1-smoothstep(-Edge,Edge,D)):0; }\n"), Mask);
        }
    }
    if (!bShadow || Endpoint.BoxShadows.IsEmpty()) return Code + TEXT("}\n");
    const FString Alpha = TEXT("(all(SampleP>=0)&&all(SampleP<=1)?Texture.SampleLevel(TextureSampler,SampleP,0).a:0)");
    for (int32 Layer = Endpoint.BoxShadows.Num() - 1; Layer >= 0; --Layer)
    {
        const auto& S = Endpoint.BoxShadows[Layer];
        Code += FString::Printf(TEXT("{ float2 Shifted=UV-float2(%.17g,%.17g)/CaptureRect.zw; float A=0;\n"), S.Offset.X, S.Offset.Y);
        const float Sigma = S.Blur * 0.5f;
        if (Sigma < 0.5f) Code += TEXT("float2 SampleP=Shifted; A=") + Alpha + TEXT(";\n");
        else
        {
            const auto Kernel = Design2UMGEffectMaterialBuilder::MakeSubtreeShadowKernel(Sigma);
            Code += FString::Printf(TEXT("float WeightSum=0; [loop] for(int y=-%d;y<=%d;++y) { [loop] for(int x=-%d;x<=%d;++x) { float Weight=exp(-float(x*x+y*y)/%.17g); float2 SampleP=Shifted+float2(x,y)*%.17g/CaptureRect.zw; A+=(%s)*Weight; WeightSum+=Weight; }} A/=WeightSum;\n"),
                Kernel.Radius, Kernel.Radius, Kernel.Radius, Kernel.Radius,
                2.0f * FMath::Square(Sigma / Kernel.Step), Kernel.Step, *Alpha);
        }
        Code += FString::Printf(TEXT("A=saturate(A*%.17g); %s=float4(float3(%.17g,%.17g,%.17g)*A+%s.rgb*(1-A),A+%s.a*(1-A)); }\n"),
            S.Color.A, Shadow, S.Color.R, S.Color.G, S.Color.B, Shadow, Shadow);
    }
    return Code + TEXT("}\n");
}
}

bool Design2UMGEffectMaterialBuilder::PrepareSharedCaptureMaterial(FUMGWidgetNode& Root, FString& Error,
    DesignImageResources::FImageOperationBudget* Budget)
{
    if (Root.Type != UMGWidgetTypes::RetainerBox || !Root.Effect.SharedCapture.IsSet())
    { Error = TEXT("Shared capture requires a native Retainer and typed endpoints"); return false; }
    DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
    if (!Budget) Budget = &LocalBudget;
    const bool bShadow = Root.Effect.SharedCapture->bSubtreeShadow;
    auto& Capture = Root.Effect.SharedCapture.GetValue();
    if (!PrepareCaptureEndpoint(Capture.Source, bShadow, Error, Budget)) return false;
    for (auto& Target : Capture.Targets) if (!PrepareCaptureEndpoint(Target, bShadow, Error, Budget)) return false;
    TArray<FCaptureTextureInput> Inputs;
    FString Code = TEXT("float2 P=CaptureRect.xy+UV*CaptureRect.zw;\n");
    Code += CaptureEndpointCode(Capture.Source, 0, TEXT("SourceMask"), TEXT("SourceShadow"), bShadow, Inputs);
    Code += TEXT("float TargetMask=SourceMask; float4 TargetShadow=SourceShadow;\n");
    for (int32 I = 0; I < Capture.Targets.Num(); ++I)
    {
        Code += FString::Printf(TEXT("if(abs(PaintTarget-%d)<0.5) {\n"), I);
        Code += CaptureEndpointCode(Capture.Targets[I], I + 1, TEXT("Mask"), TEXT("Shadow"), bShadow, Inputs);
        Code += TEXT("TargetMask=Mask; TargetShadow=Shadow; }\n");
    }
    Code += bShadow ? TEXT("float4 Base=Texture.SampleLevel(TextureSampler,UV,0); float4 Shadow=lerp(SourceShadow,TargetShadow,saturate(PaintProgress)); return float4(Base.rgb+Shadow.rgb*(1-Base.a),Base.a+Shadow.a*(1-Base.a));")
        : TEXT("return Texture.SampleLevel(TextureSampler,UV,0)*lerp(SourceMask,TargetMask,saturate(PaintProgress));");
    FString Identity = Code;
    for (const auto& Input : Inputs)
    {
        if (!Input.Texture) { Error = TEXT("Shared capture endpoint texture could not be loaded"); return false; }
        Identity += Input.Texture->GetPathName();
    }
    const FString Name = TEXT("D2U_SharedCapture_") + FMD5::HashAnsiString(*Identity);
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/") + Name;
    const FString ObjectPath = PackageName + TEXT(".") + Name;
    const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    FSystemWideCriticalSection AssetLock(TEXT("Design2UMG_SharedCapture_") + FMD5::HashAnsiString(*FPaths::ConvertRelativePathToFull(Filename)), FTimespan::FromMinutes(2));
    if (!AssetLock.IsValid()) { Error = TEXT("Could not acquire shared capture material"); return false; }
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
    if (!Material)
    {
        Material = Cast<UMaterial>(GetMutableDefault<UMaterialFactoryNew>()->FactoryCreateNew(UMaterial::StaticClass(),
            CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone, nullptr, GWarn));
        if (!Material) { Error = TEXT("Could not create shared capture material"); return false; }
        Material->MaterialDomain = EMaterialDomain::MD_UI;
        Material->BlendMode = BLEND_AlphaComposite;
        Material->SetShadingModel(MSM_Unlit);
        auto* Shader = NewObject<UMaterialExpressionCustom>(Material);
        Shader->Code = Code;
        Shader->OutputType = CMOT_Float4;
        Design2UMGMaterialAccess::AddExpression(*Material, *Shader);
        AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("Texture"));
        AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, Shader, TEXT("UV"));
        auto* Rect = AddCustomInput<UMaterialExpressionVectorParameter>(Material, Shader, TEXT("CaptureRect"));
        // Vector parameters expose RGB at output 0 and RGBA at output 5.
        Shader->Inputs.Last().Input.OutputIndex = 5;
        Rect->DefaultValue = FLinearColor(0, 0, Capture.Source.Size.X, Capture.Source.Size.Y);
        AddCustomInput<UMaterialExpressionVectorParameter>(Material, Shader, TEXT("CapturePivot"))->DefaultValue = FLinearColor(0.5f,0.5f,0,0);
        AddCustomInput<UMaterialExpressionScalarParameter>(Material, Shader, TEXT("SurfaceWidth"))->DefaultValue = Capture.Source.Size.X;
        AddCustomInput<UMaterialExpressionScalarParameter>(Material, Shader, TEXT("SurfaceHeight"))->DefaultValue = Capture.Source.Size.Y;
        AddCustomInput<UMaterialExpressionScalarParameter>(Material, Shader, TEXT("PaintProgress"))->DefaultValue = 0;
        AddCustomInput<UMaterialExpressionScalarParameter>(Material, Shader, TEXT("PaintTarget"))->DefaultValue = 0;
        for (const auto& Input : Inputs)
            AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, *Input.Name)->Texture = Input.Texture;
        Material->GetExpressionInputForProperty(MP_EmissiveColor)[0].Expression = AddComponentMask(Material, Shader, true, true, true, false);
        Material->GetExpressionInputForProperty(MP_Opacity)[0].Expression = AddComponentMask(Material, Shader, false, false, false, true);
        Material->PreEditChange(nullptr); Material->PostEditChange();
        if (!EnsurePaintMaterial(Material, Root.Name, Error)) return false;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        if (!SaveGeneratedMaterialAsset(Material, Filename)) { Error = TEXT("Could not persist shared capture material"); return false; }
    }
    if (!EnsurePaintMaterial(Material, Root.Name, Error)) return false;
    Root.Effect.EffectMaterial = ObjectPath;
    Root.Effect.TextureParameter = TEXT("Texture");
    Root.Effect.bRetainRendering = true;
    return true;
}

bool Design2UMGEffectMaterialBuilder::FinalizeSharedCaptureFrame(UWidget* Owner,
    const FVector4& CaptureRect, const FVector2D& Pivot, FString& Error)
{
    FIntPoint PixelSize;
    auto* Retainer = Cast<URetainerBox>(Owner);
    if (!Retainer || !FMath::IsFinite(CaptureRect.X) || !FMath::IsFinite(CaptureRect.Y)
        || !DesignImageResources::GetGeneratedTextureLimits().TryGetPixelSize(CaptureRect.Z, CaptureRect.W, PixelSize))
    { Error = TEXT("Shared capture frame exceeds the existing generated-resource limits"); return false; }
    UMaterialInterface* Parent = const_cast<UMaterialInterface*>(Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer));
    float Progress = 0;
    FLinearColor DefaultRect;
    if (!Parent || !Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CaptureRect")), DefaultRect)
        || !Parent->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PaintProgress")), Progress))
    { Error = TEXT("Shared capture frame has no prepared native effect material"); return false; }
    const FLinearColor MeasuredFrame(CaptureRect.X, CaptureRect.Y, CaptureRect.Z, CaptureRect.W);
    FLinearColor DefaultPivot;
    const FLinearColor MeasuredPivot(Pivot.X, Pivot.Y, 0, 0);
    if (DefaultRect == MeasuredFrame && Parent->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CapturePivot")), DefaultPivot)
        && DefaultPivot == MeasuredPivot) return true;
    TArray<uint8> Identity;
    FMemoryWriter Writer(Identity);
    FString ParentPath = Parent->GetPathName();
    FVector4 Frame = CaptureRect;
    FVector2D OwnerPivot = Pivot;
    Writer << ParentPath << Frame << OwnerPivot;
    FSHAHash Hash;
    FSHA1::HashBuffer(Identity.GetData(), Identity.Num(), Hash.Hash);
    const FString Name = TEXT("D2U_CaptureFrame_") + Hash.ToString();
    const FString PackageName = TEXT("/Game/Design2UMG/GeneratedMaterials/RetainerInstances/") + Name;
    const FString ObjectPath = PackageName + TEXT(".") + Name;
    const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    FSystemWideCriticalSection AssetLock(TEXT("Design2UMG_CaptureFrame_") + FMD5::HashAnsiString(*FPaths::ConvertRelativePathToFull(Filename)), FTimespan::FromMinutes(2));
    if (!AssetLock.IsValid()) { Error = TEXT("Could not acquire shared capture frame material"); return false; }
    UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
    if (!Instance)
    {
        Instance = NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
        Instance->SetParentEditorOnly(Parent, false);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CaptureRect")), FLinearColor(Frame.X, Frame.Y, Frame.Z, Frame.W));
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CapturePivot")), MeasuredPivot);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("PaintProgress")), 0);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("PaintTarget")), 0);
        Instance->PostEditChange();
        Design2UMGMaterialAccess::CacheShadersForRendering(*Instance);
        Design2UMGMaterialAccess::WaitForCompilation(*Instance);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        if (!SaveGeneratedMaterialAsset(Instance, Filename)) { Error = TEXT("Could not persist shared capture frame"); return false; }
    }
    Retainer->SetEffectMaterial(Instance);
    return true;
}

bool Design2UMGEffectMaterialBuilder::PrepareRetainerEffectMaterials(
	FUMGWidgetNode& Root, TArray<TStrongObjectPtr<UMaterialInterface>>& Materials, FString& OutError,
	DesignImageResources::FImageOperationBudget* Budget)
{
	for (FUMGWidgetNode& Child : Root.Children)
	{
		if (!PrepareRetainerEffectMaterials(Child, Materials, OutError, Budget)) return false;
	}
	FUMGEffectData& Effect = Root.Effect;
    if (Effect.SharedCapture.IsSet())
    {
        if (!PrepareSharedCaptureMaterial(Root, OutError, Budget)) return false;
        const FString Path = Root.Type == UMGWidgetTypes::Image ? Root.Brush.MaterialRef.Get(FString()) : Effect.EffectMaterial.Get(FString());
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
        if (!Material) { OutError = TEXT("Shared capture material was not persisted"); return false; }
        Materials.Emplace(Material);
        return true;
    }

    if (Root.Brush.MaterialRef.Get(FString()) == TEXT("native-surface-paints"))
    {
        if (!Effect.SurfacePaints.IsSet() || !Effect.GeometryMaskShape.IsSet()
            || !Effect.GeometryMaskSize.IsSet() || !Effect.GeometryMaskCornerRadii.IsSet()
            || (Root.Type != TEXT("Image") && Root.Type != TEXT("Border")))
        { OutError = TEXT("Native surface paint requires an Image or Border with typed paints and fixed geometry"); return false; }
        if (!PrepareSurfacePaintMaterial(Root, Effect.SurfacePaints.GetValue(),
            Effect.SurfacePaintTargets.Get(TArray<FUMGSurfacePaintTarget>()), Effect.GeometryMaskShape.GetValue(),
            Effect.GeometryMaskSize.GetValue(), Effect.GeometryMaskCornerRadii.GetValue(), OutError, Budget)) return false;
        UMaterialInterface* SurfaceMaterial = LoadObject<UMaterialInterface>(nullptr, *Root.Brush.MaterialRef.GetValue());
        if (!SurfaceMaterial) { OutError = TEXT("Native surface paint material was not persisted"); return false; }
        Materials.Emplace(SurfaceMaterial);
    }
	if (Effect.EffectMaterial.Get(FString()) == UMGPropertyKeys::RetainerSourceContourEffectRef)
	{
		UMaterialInterface* Material = PrepareSourceContourMaterial(Root, OutError, Budget);
		if (!Material) return false;
		Materials.Emplace(Material);
		Effect.EffectMaterial = Material->GetPathName();
		Effect.GeometryMaskShape.Reset();
		Effect.GeometryMaskSize.Reset();
		Effect.GeometryMaskCornerRadii.Reset();
		return true;
	}
	if (!Effect.EffectMaterial.IsSet() || Effect.EffectMaterial.GetValue().IsEmpty()) return true;
	if (Effect.EffectMaterial.GetValue() == TEXT("native-text-paints") && !PrepareTextPaintMaterial(Root,OutError,Budget)) return false;
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
