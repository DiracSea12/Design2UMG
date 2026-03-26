// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEffectMaterialBuilder.h"

#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FeedbackContext.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TStrongObjectPtr<UMaterial> GForegroundBlurRetainerMaterial;
	TStrongObjectPtr<UMaterial> GInnerShadowRetainerMaterial;

	void SetupExpression(UMaterialExpressionTextureObjectParameter* Expression, const FName& Name)
	{
		Expression->ParameterName = Name;
		Expression->SetDefaultTexture();
		Expression->SamplerType = SAMPLERTYPE_Color;
	}

	void SetupExpression(UMaterialExpressionVectorParameter* Expression, const FName& Name)
	{
		Expression->ParameterName = Name;
		Expression->DefaultValue = FLinearColor(1.0f / 512.0f, 1.0f / 512.0f, 0.0f, 0.0f);
	}

	void SetupExpression(UMaterialExpressionTextureCoordinate* Expression, const FName& Name)
	{
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
		GetTransientPackage(),
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
float2 Step = max(BlurUVStep.rg, float2(0.00001, 0.00001));
float4 Result = Texture.SampleLevel(TextureSampler, UV, 0) * 0.29411765;
Result += Texture.SampleLevel(TextureSampler, UV + float2(Step.x, 0.0), 0) * 0.17647059;
Result += Texture.SampleLevel(TextureSampler, UV - float2(Step.x, 0.0), 0) * 0.17647059;
Result += Texture.SampleLevel(TextureSampler, UV + float2(0.0, Step.y), 0) * 0.17647059;
Result += Texture.SampleLevel(TextureSampler, UV - float2(0.0, Step.y), 0) * 0.17647059;
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
		GetTransientPackage(),
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
float2 Step = max(BlurUVStep.rg, float2(0.00001, 0.00001));
float2 Offset = InnerShadowOffset.rg;
float4 Base = Texture.SampleLevel(TextureSampler, UV, 0);
float ShiftedAlpha = Texture.SampleLevel(TextureSampler, UV - Offset, 0).a * 0.29411765;
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset + float2(Step.x, 0.0), 0).a * 0.17647059;
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset - float2(Step.x, 0.0), 0).a * 0.17647059;
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset + float2(0.0, Step.y), 0).a * 0.17647059;
ShiftedAlpha += Texture.SampleLevel(TextureSampler, UV - Offset - float2(0.0, Step.y), 0).a * 0.17647059;
float ShadowMask = saturate((Base.a - ShiftedAlpha) * 4.0) * InnerShadowColor.a;
float3 OutRgb = lerp(Base.rgb, InnerShadowColor.rgb, ShadowMask);
return float4(OutRgb, Base.a);
)");
	Material->GetExpressionCollection().AddExpression(InnerShadowNode);

	AddCustomInput<UMaterialExpressionTextureObjectParameter>(Material, InnerShadowNode, TEXT("Texture"));
	AddCustomInput<UMaterialExpressionTextureCoordinate>(Material, InnerShadowNode, TEXT("UV"));
	UMaterialExpressionVectorParameter* BlurStepParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("BlurUVStep"));
	BlurStepParam->DefaultValue = FLinearColor(1.0f / 512.0f, 1.0f / 512.0f, 0.0f, 0.0f);
	UMaterialExpressionVectorParameter* ShadowColorParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("InnerShadowColor"));
	ShadowColorParam->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);
	UMaterialExpressionVectorParameter* ShadowOffsetParam =
		AddCustomInput<UMaterialExpressionVectorParameter>(Material, InnerShadowNode, TEXT("InnerShadowOffset"));
	ShadowOffsetParam->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

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
