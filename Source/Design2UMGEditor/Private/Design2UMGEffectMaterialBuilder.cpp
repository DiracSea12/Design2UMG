// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEffectMaterialBuilder.h"
#include "Design2UMGLog.h"

#include "Factories/MaterialFactoryNew.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "MaterialDomain.h"
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
#include "Misc/SecureHash.h"
#include "MaterialShared.h"

namespace
{
	bool ValidateCompiledSubtreeMaterial(UMaterial* Material, const FString& NodeName, FString& OutError)
	{
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
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

#if WITH_DEV_AUTOMATION_TESTS
	bool GFailGeometryMaskSaveOnce = false;
#endif

	bool SaveGeometryMaskAsset(UObject* Asset, const FString& FileName)
	{
		UPackage* Package = Asset->GetOutermost();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bForceFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
		bForceFailure = GFailGeometryMaskSaveOnce;
		GFailGeometryMaskSaveOnce = false;
#endif
		if (!bForceFailure && UPackage::SavePackage(Package, Asset, *FileName, SaveArgs)) return true;

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
		Material->GetExpressionCollection().AddExpression(Expression);

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
		Material->GetExpressionCollection().AddExpression(Mask);
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
	FString Code = TEXT("// Subtree shadow v1: all layers sample the original composed alpha.\nfloat4 Base = Texture.SampleLevel(TextureSampler, UV, 0);\nfloat4 Shadow = float4(0,0,0,0);\n");
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
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (!Resource || !Resource->GetGameThreadShaderMap() || !Resource->GetGameThreadShaderMap()->IsValidForRendering())
		{
			Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		}
		Material->EnsureIsComplete();
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
		Material->GetExpressionCollection().AddExpression(Shader);
		AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, Shader, TEXT("Texture"));
		AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, Shader, TEXT("UV"));
		Material->GetEditorOnlyData()->EmissiveColor.Expression = AddComponentMask(Material, Shader, true, true, true, false);
		Material->GetEditorOnlyData()->Opacity.Expression = AddComponentMask(Material, Shader, false, false, false, true);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		Material->EnsureIsComplete();
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

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		GetTransientPackageAsObject(),
		TEXT("D2U_ForegroundBlurRetainer"),
		RF_Transient,
		nullptr,
		GWarn));
	if (!Material)
	{
		return nullptr;
	}

	Material->bIsPreviewMaterial = true;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->MaterialDomain = EMaterialDomain::MD_UI;
	Material->BlendMode = EBlendMode::BLEND_AlphaComposite;

	UMaterialExpressionCustom* BlurNode = NewObject<UMaterialExpressionCustom>(Material);
	BlurNode->OutputType = ECustomMaterialOutputType::CMOT_Float4;
	BlurNode->Code = TEXT(R"(
// Cross-shaped 5-tap blur: center 75/255 plus four taps at 45/255 each (sum = 1).
float2 Step = max(BlurUVStep.rg, float2(0.00001, 0.00001));
float4 Result = Texture.SampleLevel(TextureSampler, UV, 0) * (75.0 / 255.0);
Result += Texture.SampleLevel(TextureSampler, UV + float2(Step.x, 0.0), 0) * (45.0 / 255.0);
Result += Texture.SampleLevel(TextureSampler, UV - float2(Step.x, 0.0), 0) * (45.0 / 255.0);
Result += Texture.SampleLevel(TextureSampler, UV + float2(0.0, Step.y), 0) * (45.0 / 255.0);
Result += Texture.SampleLevel(TextureSampler, UV - float2(0.0, Step.y), 0) * (45.0 / 255.0);
return Result;
)");
	Material->GetExpressionCollection().AddExpression(BlurNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, BlurNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, BlurNode, TEXT("UV"));
	AddCustomInput<UMaterialExpressionVectorParameter>(Material, BlurNode, TEXT("BlurUVStep"));

	UMaterialExpressionComponentMask* EmissiveMask = AddComponentMask(Material, BlurNode, true, true, true, false);
	UMaterialExpressionComponentMask* OpacityMask = AddComponentMask(Material, BlurNode, false, false, false, true);

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	EditorOnlyData->EmissiveColor.Expression = EmissiveMask;
	EditorOnlyData->Opacity.Expression = OpacityMask;

	Material->AddToRoot();
	Material->PreEditChange(nullptr);
	Material->PostEditChange();

	GForegroundBlurRetainerMaterial.Reset(Material);
	return Material;
}

UMaterialInterface* Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial()
{
	if (GInnerShadowRetainerMaterial.IsValid())
	{
		return GInnerShadowRetainerMaterial.Get();
	}

	UMaterialFactoryNew* MaterialFactory = GetMutableDefault<UMaterialFactoryNew>();
	if (!MaterialFactory)
	{
		return nullptr;
	}

	UMaterial* Material = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		GetTransientPackageAsObject(),
		TEXT("D2U_InnerShadowRetainer"),
		RF_Transient,
		nullptr,
		GWarn));
	if (!Material)
	{
		return nullptr;
	}

	Material->bIsPreviewMaterial = true;
	Material->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
	Material->MaterialDomain = EMaterialDomain::MD_UI;
	Material->BlendMode = EBlendMode::BLEND_AlphaComposite;

	UMaterialExpressionCustom* InnerShadowNode = NewObject<UMaterialExpressionCustom>(Material);
	InnerShadowNode->OutputType = ECustomMaterialOutputType::CMOT_Float4;
	InnerShadowNode->Code = TEXT(R"(
// Cross-shaped 5-tap alpha blur: center 75/255 plus four taps at 45/255 each (sum = 1).
float2 Step = max(BlurUVStep.rg, float2(0.00001, 0.00001));
float2 Offset = InnerShadowOffset.rg;
float4 Base = Texture.SampleLevel(TextureSampler, UV, 0);
float ShiftedAlpha = Texture.SampleLevel(TextureSampler, UV - Offset, 0).a * (75.0 / 255.0);
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset + float2(Step.x, 0.0), 0).a * (45.0 / 255.0);
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset - float2(Step.x, 0.0), 0).a * (45.0 / 255.0);
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset + float2(0.0, Step.y), 0).a * (45.0 / 255.0);
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset - float2(0.0, Step.y), 0).a * (45.0 / 255.0);
float ShadowMask = saturate((Base.a - ShiftedAlpha) * 4.0) * saturate(InnerShadowStrength);
float3 OutRgb = lerp(Base.rgb, InnerShadowColor.rgb, ShadowMask);
return float4(OutRgb, Base.a);
)");
	Material->GetExpressionCollection().AddExpression(InnerShadowNode);

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

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	EditorOnlyData->EmissiveColor.Expression = EmissiveMask;
	EditorOnlyData->Opacity.Expression = OpacityMask;

	Material->AddToRoot();
	Material->PreEditChange(nullptr);
	Material->PostEditChange();

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
		ExistingMaterial->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
		ExistingMaterial->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		ExistingMaterial->EnsureIsComplete();
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
	Material->GetExpressionCollection().AddExpression(MaskNode);

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

	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	EditorOnlyData->EmissiveColor.Expression = MaskedRgb;
	EditorOnlyData->Opacity.Expression = MaskedAlpha;

	Material->AddToRoot();
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
	Material->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
	Material->EnsureIsComplete();

	const FString MaterialFileName = FPackageName::LongPackageNameToFilename(
		MaterialPackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(MaterialFileName), true);
	if (!SaveGeometryMaskAsset(Material, MaterialFileName))
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
		ExistingInstance->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
		ExistingInstance->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		ExistingInstance->EnsureIsComplete();
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
	Instance->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Synchronous);
	Instance->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
	Instance->EnsureIsComplete();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
	if (!SaveGeometryMaskAsset(Instance.Get(), PackageFileName))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save geometry mask material instance: %s"), *PackageFileName);
		return nullptr;
	}

	return Instance.Get();
}

#if WITH_DEV_AUTOMATION_TESTS
void Design2UMGEffectMaterialBuilder::ArmGeometryMaskSaveFailureOnce()
{
	GFailGeometryMaskSaveOnce = true;
}
#endif

bool Design2UMGEffectMaterialBuilder::PrepareGeometryMaskMaterials(
	FUMGWidgetNode& Root, TArray<TStrongObjectPtr<UMaterialInterface>>& Materials, FString& OutError)
{
	for (FUMGWidgetNode& Child : Root.Children)
	{
		if (!PrepareGeometryMaskMaterials(Child, Materials, OutError)) return false;
	}
	FUMGEffectData& Effect = Root.Effect;
	if (Effect.EffectMaterial.Get(FString()) != UMGPropertyKeys::RetainerGeometryMaskEffectRef) return true;
	UMaterialInterface* Material = Effect.GeometryMaskShape.IsSet() && Effect.GeometryMaskSize.IsSet() && Effect.GeometryMaskCornerRadii.IsSet()
		? GetOrCreateGeometryMaskRetainerMaterial(Effect.GeometryMaskShape.GetValue(), Effect.GeometryMaskSize.GetValue(), Effect.GeometryMaskCornerRadii.GetValue())
		: GetOrCreateGeometryMaskRetainerMaterial();
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Failed to create or persist geometry mask material for '%s'"), *Root.Name);
		return false;
	}
	Materials.Emplace(Material);
	Effect.EffectMaterial = Material->GetPathName();
	return true;
}
