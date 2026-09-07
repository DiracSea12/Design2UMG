// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEffectMaterialBuilder.h"
#include "Design2UMGLog.h"

#include "Factories/MaterialFactoryNew.h"
#include "HAL/FileManager.h"
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

namespace
{
	TStrongObjectPtr<UMaterial> GForegroundBlurRetainerMaterial;
	TStrongObjectPtr<UMaterial> GGeometryMaskRetainerMaterial;
	TStrongObjectPtr<UMaterial> GInnerShadowRetainerMaterial;

	// Per-unique-geometry instances that were rooted AND saved to /Game. They are
	// recoverable via LoadObject, so the root is only an in-session crutch; the
	// ledger lets the generator drop it once the owning asset save completed.
	TArray<TWeakObjectPtr<UMaterialInstanceConstant>> GRootedGeometryMaskInstances;

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
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(MaterialPackage, Material, *MaterialFileName, SaveArgs))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save geometry mask material package: %s"), *MaterialFileName);
	}

	GGeometryMaskRetainerMaterial.Reset(Material);
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

	UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
		CreatePackage(*PackageName),
		*InstanceName,
		RF_Public | RF_Standalone);
	if (!Instance)
	{
		return BaseMaterial;
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
	Instance->AddToRoot();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Instance->GetOutermost(), Instance, *PackageFileName, SaveArgs))
	{
		// Unsaved instance keeps its root (old behavior): no disk copy exists to reload from.
		UE_LOG(LogDesign2UMG, Warning, TEXT("Failed to save geometry mask material instance: %s"), *PackageFileName);
		return Instance;
	}

	GRootedGeometryMaskInstances.Add(Instance);
	return Instance;
}

void Design2UMGEffectMaterialBuilder::ReleaseRootedGeometryMaskInstances()
{
	for (const TWeakObjectPtr<UMaterialInstanceConstant>& WeakInstance : GRootedGeometryMaskInstances)
	{
		if (UMaterialInstanceConstant* Instance = WeakInstance.Get())
		{
			Instance->RemoveFromRoot();
		}
	}

	GRootedGeometryMaskInstances.Empty();
}
