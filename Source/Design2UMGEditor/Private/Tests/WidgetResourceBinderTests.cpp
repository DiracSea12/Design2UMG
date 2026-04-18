// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "WidgetResourceBinder.h"
#include "UMGSchema.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

#if WITH_AUTOMATION_WORKER

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderTexturePath,
	"Design2UMG.WidgetResourceBinder.TexturePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderTexturePath::RunTest(const FString& Parameters)
{
	// 无效 MaterialRef + 有效 TexturePath：binder 应 fallback 到 texture 路径并成功绑定
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	Node.Brush.MaterialRef = TEXT("__invalid_shape_that_does_not_exist__");
	Node.Brush.bHasMaterialRef = true;
	Node.Brush.TexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse");

	UImage* ImageWidget = NewObject<UImage>();
	TMap<FString, UTexture2D*> TextureMap;

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, &TextureMap);

	// MaterialRef 生成失败后应 fallback 到 TexturePath，返回 true
	TestTrue(TEXT("Fallback to TexturePath when MaterialRef invalid"), bBound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderMaterialRefSuccess,
	"Design2UMG.WidgetResourceBinder.MaterialRefSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderMaterialRefSuccess::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	// 使用已知的程序化形状类型
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.bHasMaterialRef = true;

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	// roundedrect 是合法的 MaterialRef，应成功绑定并返回 true
	TestTrue(TEXT("MaterialRef 'roundedrect' binds successfully"), bBound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetResourceBinderMaterialRefPriority,
	"Design2UMG.WidgetResourceBinder.MaterialRefPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetResourceBinderMaterialRefPriority::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Type = TEXT("Image");
	Node.Name = TEXT("TestImage");
	// 同时设置 MaterialRef 和 TexturePath
	Node.Brush.MaterialRef = TEXT("roundedrect");
	Node.Brush.bHasMaterialRef = true;
	Node.Brush.TexturePath = TEXT("/Engine/EngineMaterials/DefaultDiffuse");

	UImage* ImageWidget = NewObject<UImage>();

	const bool bBound = FWidgetResourceBinder::BindImageResource(ImageWidget, Node, nullptr);

	// MaterialRef 优先：roundedrect 成功时应返回 true，且不走 texture 路径
	TestTrue(TEXT("MaterialRef takes priority over TexturePath"), bBound);

	// 验证 brush 来自程序化纹理（ImageSize 非零）
	const FSlateBrush Brush = ImageWidget->GetBrush();
	TestTrue(TEXT("Brush ImageSize set by material path"), Brush.ImageSize.X > 0.0f && Brush.ImageSize.Y > 0.0f);

	return true;
}

#endif // WITH_AUTOMATION_WORKER
