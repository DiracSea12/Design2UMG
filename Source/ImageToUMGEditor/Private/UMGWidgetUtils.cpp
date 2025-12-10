// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGWidgetUtils.h"
#include "ImageToUMGLog.h"

#include "Components/Widget.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/RichTextBlock.h"
#include "Components/CircularThrobber.h"
#include "Components/Throbber.h"
#include "Components/ExpandableArea.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/BackgroundBlur.h"
#include "Components/InvalidationBox.h"
#include "Components/RetainerBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/NamedSlot.h"
#include "Components/SafeZone.h"
#include "Components/SafeZoneSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/ListView.h"
#include "Components/TileView.h"
#include "Components/TreeView.h"
#include "Components/MenuAnchor.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Components/ContentWidget.h"
#include "Blueprint/WidgetTree.h"

// ============================================================================
// 静态映射表
// ============================================================================

namespace
{
	// Widget 类型映射表
	const TMap<FString, UClass*>& GetWidgetClassMap()
	{
		static const TMap<FString, UClass*> Map = []() {
			TMap<FString, UClass*> M;
			#define REG(Name) M.Add(TEXT(#Name), U##Name::StaticClass())
			// 容器类
			REG(CanvasPanel); REG(VerticalBox); REG(HorizontalBox); REG(Overlay);
			REG(ScrollBox); REG(GridPanel); REG(UniformGridPanel); REG(WrapBox);
			REG(WidgetSwitcher); REG(SafeZone);
			// 包装类
			REG(Border); REG(SizeBox); REG(ScaleBox); REG(BackgroundBlur);
			REG(InvalidationBox); REG(RetainerBox); REG(ExpandableArea); REG(NamedSlot);
			// 交互类
			REG(Button); REG(CheckBox); REG(Slider); REG(SpinBox);
			// 文本类
			REG(TextBlock); REG(RichTextBlock); REG(EditableTextBox); REG(MultiLineEditableTextBox);
			// 显示类
			REG(Image); REG(ProgressBar); REG(Throbber); REG(CircularThrobber); REG(Spacer);
			// 列表类
			REG(ListView); REG(TileView); REG(TreeView);
			// 其他
			REG(MenuAnchor); REG(EditableText); REG(MultiLineEditableText);
			#undef REG
			// 别名
			M.Add(TEXT("ComboBox"), UComboBoxString::StaticClass());
			M.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());
			return M;
		}();
		return Map;
	}

	// 对齐映射表
	const TMap<FString, EHorizontalAlignment>& GetHAlignMap()
	{
		static const TMap<FString, EHorizontalAlignment> Map = {
			{TEXT("Left"), HAlign_Left}, {TEXT("Center"), HAlign_Center},
			{TEXT("Right"), HAlign_Right}, {TEXT("Fill"), HAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, EVerticalAlignment>& GetVAlignMap()
	{
		static const TMap<FString, EVerticalAlignment> Map = {
			{TEXT("Top"), VAlign_Top}, {TEXT("Center"), VAlign_Center},
			{TEXT("Bottom"), VAlign_Bottom}, {TEXT("Fill"), VAlign_Fill}
		};
		return Map;
	}

	const TMap<FString, ESlateVisibility>& GetVisibilityMap()
	{
		static const TMap<FString, ESlateVisibility> Map = {
			{TEXT("Visible"), ESlateVisibility::Visible},
			{TEXT("Hidden"), ESlateVisibility::Hidden},
			{TEXT("Collapsed"), ESlateVisibility::Collapsed},
			{TEXT("HitTestInvisible"), ESlateVisibility::HitTestInvisible},
			{TEXT("SelfHitTestInvisible"), ESlateVisibility::SelfHitTestInvisible}
		};
		return Map;
	}

	// 颜色名称映射
	const TMap<FString, FLinearColor>& GetNamedColors()
	{
		static const TMap<FString, FLinearColor> Map = {
			{TEXT("white"), FLinearColor::White},
			{TEXT("black"), FLinearColor::Black},
			{TEXT("red"), FLinearColor::Red},
			{TEXT("green"), FLinearColor::Green},
			{TEXT("blue"), FLinearColor::Blue},
			{TEXT("yellow"), FLinearColor::Yellow},
			{TEXT("gray"), FLinearColor::Gray},
			{TEXT("grey"), FLinearColor::Gray},
			{TEXT("transparent"), FLinearColor::Transparent}
		};
		return Map;
	}
}

// ============================================================================
// Widget 创建（统一入口）
// ============================================================================

UWidget* FUMGWidgetUtils::CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget, const TMap<FString, UTexture2D*>* TextureMap)
{
	UClass* WidgetClass = ResolveWidgetClass(Node.Type);
	if (!WidgetClass)
	{
		UE_LOG(LogImageToUMG, Warning, TEXT("Unknown widget type: %s"), *Node.Type);
		return nullptr;
	}

	FName WidgetName = SanitizeWidgetName(Node.Name, Node.Type);
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (!NewWidget)
	{
		UE_LOG(LogImageToUMG, Error, TEXT("Failed to construct widget: %s"), *Node.Name);
		return nullptr;
	}

	// 添加到父容器并应用 Slot 属性
	if (ParentWidget)
	{
		if (UPanelSlot* Slot = ParentWidget->AddChild(NewWidget))
		{
			ApplySlotProperties(Slot, Node.Slot);
		}
	}

	ApplyWidgetProperties(NewWidget, Node.Properties);

	// 如果是 Image 控件且提供了 TextureMap，尝试绑定纹理
	if (TextureMap)
	{
		if (UImage* ImageWidget = Cast<UImage>(NewWidget))
		{
			if (const FString* ImageRef = Node.Properties.Find(TEXT("ImageRef")))
			{
				if (UTexture2D* const* FoundTexture = TextureMap->Find(*ImageRef))
				{
					if (*FoundTexture)
					{
						ImageWidget->SetBrushFromTexture(*FoundTexture);
					}
				}
			}
		}
	}

	// 递归处理子控件
	if (UPanelWidget* AsPanel = Cast<UPanelWidget>(NewWidget))
	{
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CreateWidgetRecursive(WidgetTree, Child, AsPanel, TextureMap);
		}
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(NewWidget))
	{
		if (Node.Children.Num() > 0)
		{
			if (UWidget* ChildWidget = CreateWidgetRecursive(WidgetTree, Node.Children[0], nullptr, TextureMap))
			{
				Content->SetContent(ChildWidget);
			}
			if (Node.Children.Num() > 1)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("ContentWidget '%s' ignoring %d extra children"), *Node.Name, Node.Children.Num() - 1);
			}
		}
	}

	return NewWidget;
}

TMap<FString, UTexture2D*> FUMGWidgetUtils::BuildTextureMap(const TArray<FProcessedImageAsset>& Assets)
{
	TMap<FString, UTexture2D*> TextureMap;
	for (const FProcessedImageAsset& Asset : Assets)
	{
		if (Asset.Texture)
		{
			TextureMap.Add(Asset.Id, Asset.Texture);
		}
	}
	return TextureMap;
}

// ============================================================================
// Widget 类型解析
// ============================================================================

UClass* FUMGWidgetUtils::ResolveWidgetClass(const FString& TypeName)
{
	if (UClass* const* Found = GetWidgetClassMap().Find(TypeName))
	{
		return *Found;
	}

	// Fallback: 动态查找
	FString ClassName = FString::Printf(TEXT("U%s"), *TypeName);
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (FoundClass && FoundClass->IsChildOf(UWidget::StaticClass()))
	{
		return FoundClass;
	}

	return nullptr;
}

// ============================================================================
// 颜色解析（简化版：只支持 #RRGGBB 和颜色名称）
// ============================================================================

FLinearColor FUMGWidgetUtils::ParseColor(const FString& ColorStr)
{
	FString Trimmed = ColorStr.TrimStartAndEnd();

	if (Trimmed.StartsWith(TEXT("#")))
	{
		return FColor::FromHex(Trimmed);
	}

	if (const FLinearColor* Named = GetNamedColors().Find(Trimmed.ToLower()))
	{
		return *Named;
	}

	return FLinearColor::White;
}

// ============================================================================
// 对齐方式解析
// ============================================================================

EHorizontalAlignment FUMGWidgetUtils::ParseHAlign(const FString& Str)
{
	const EHorizontalAlignment* Found = GetHAlignMap().Find(Str);
	return Found ? *Found : HAlign_Fill;
}

EVerticalAlignment FUMGWidgetUtils::ParseVAlign(const FString& Str)
{
	const EVerticalAlignment* Found = GetVAlignMap().Find(Str);
	return Found ? *Found : VAlign_Fill;
}

ESlateVisibility FUMGWidgetUtils::ParseVisibility(const FString& Str)
{
	const ESlateVisibility* Found = GetVisibilityMap().Find(Str);
	return Found ? *Found : ESlateVisibility::Visible;
}

// ============================================================================
// 几何解析
// ============================================================================

FVector2D FUMGWidgetUtils::ParseVector2D(const FString& Str, FVector2D Default)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 2)
	{
		return FVector2D(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd())
		);
	}
	return Default;
}

FMargin FUMGWidgetUtils::ParseMargin(const FString& Str)
{
	TArray<FString> Parts;
	Str.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 1)
	{
		return FMargin(FCString::Atof(*Parts[0]));
	}
	if (Parts.Num() == 4)
	{
		return FMargin(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()),
			FCString::Atof(*Parts[3].TrimStartAndEnd())
		);
	}
	return FMargin(0);
}

// ============================================================================
// Widget 名称处理
// ============================================================================

FName FUMGWidgetUtils::SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix)
{
	FString Sanitized = SlugStringForValidName(InName);

	if (Sanitized.IsEmpty())
	{
		Sanitized = FallbackPrefix + TEXT("_Widget");
	}

	if (Sanitized.Len() > 0 && FChar::IsDigit(Sanitized[0]))
	{
		Sanitized = TEXT("_") + Sanitized;
	}

	return FName(*Sanitized);
}

// ============================================================================
// Slot 属性应用（函数指针映射表模式）
// ============================================================================

namespace
{
	using FApplySlotFunc = void(*)(UPanelSlot*, const FUMGSlotData&);

	void ApplyCanvasPanelSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UCanvasPanelSlot* CS = CastChecked<UCanvasPanelSlot>(Slot);
		FAnchors Anchors;
		Anchors.Minimum = FVector2D(S.Anchors.Min.X, S.Anchors.Min.Y);
		Anchors.Maximum = FVector2D(S.Anchors.Max.X, S.Anchors.Max.Y);
		CS->SetAnchors(Anchors);
		CS->SetAlignment(FVector2D(S.Alignment.X, S.Alignment.Y));
		CS->SetOffsets(FMargin(S.Offsets.Left, S.Offsets.Top, S.Offsets.Right, S.Offsets.Bottom));
	}

	// VBox/HBox Slot 共用逻辑
	template<typename TSlot>
	void ApplyBoxSlotCommon(TSlot* BoxSlot, const FUMGSlotData& S)
	{
		BoxSlot->SetPadding(S.Padding);
		BoxSlot->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		BoxSlot->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
		if (S.SizeRule == TEXT("Fill"))
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = S.FillWeight;
			BoxSlot->SetSize(Size);
		}
	}

	void ApplyVerticalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UVerticalBoxSlot>(Slot), S);
	}

	void ApplyHorizontalBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		ApplyBoxSlotCommon(CastChecked<UHorizontalBoxSlot>(Slot), S);
	}

	void ApplyOverlaySlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UOverlaySlot* OS = CastChecked<UOverlaySlot>(Slot);
		OS->SetPadding(S.Padding);
		OS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		OS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UGridSlot* GS = CastChecked<UGridSlot>(Slot);
		GS->SetPadding(S.Padding);
		GS->SetRow(S.Row);
		GS->SetColumn(S.Column);
		GS->SetRowSpan(S.RowSpan);
		GS->SetColumnSpan(S.ColumnSpan);
		GS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		GS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyUniformGridSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UUniformGridSlot* UGS = CastChecked<UUniformGridSlot>(Slot);
		UGS->SetRow(S.Row);
		UGS->SetColumn(S.Column);
		UGS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		UGS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyWrapBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWrapBoxSlot* WS = CastChecked<UWrapBoxSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplyScrollBoxSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UScrollBoxSlot* SS = CastChecked<UScrollBoxSlot>(Slot);
		SS->SetPadding(S.Padding);
		SS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
	}

	void ApplyWidgetSwitcherSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		UWidgetSwitcherSlot* WS = CastChecked<UWidgetSwitcherSlot>(Slot);
		WS->SetPadding(S.Padding);
		WS->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		WS->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	void ApplySafeZoneSlot(UPanelSlot* Slot, const FUMGSlotData& S)
	{
		USafeZoneSlot* SZ = CastChecked<USafeZoneSlot>(Slot);
		SZ->SetPadding(S.Padding);
		SZ->SetHorizontalAlignment(FUMGWidgetUtils::ParseHAlign(S.HorizontalAlignment));
		SZ->SetVerticalAlignment(FUMGWidgetUtils::ParseVAlign(S.VerticalAlignment));
	}

	const TMap<UClass*, FApplySlotFunc>& GetSlotApplierMap()
	{
		static const TMap<UClass*, FApplySlotFunc> Map = {
			{UCanvasPanelSlot::StaticClass(), &ApplyCanvasPanelSlot},
			{UVerticalBoxSlot::StaticClass(), &ApplyVerticalBoxSlot},
			{UHorizontalBoxSlot::StaticClass(), &ApplyHorizontalBoxSlot},
			{UOverlaySlot::StaticClass(), &ApplyOverlaySlot},
			{UGridSlot::StaticClass(), &ApplyGridSlot},
			{UUniformGridSlot::StaticClass(), &ApplyUniformGridSlot},
			{UWrapBoxSlot::StaticClass(), &ApplyWrapBoxSlot},
			{UScrollBoxSlot::StaticClass(), &ApplyScrollBoxSlot},
			{UWidgetSwitcherSlot::StaticClass(), &ApplyWidgetSwitcherSlot},
			{USafeZoneSlot::StaticClass(), &ApplySafeZoneSlot},
		};
		return Map;
	}
}

void FUMGWidgetUtils::ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& S)
{
	if (!Slot) return;
	if (const FApplySlotFunc* Func = GetSlotApplierMap().Find(Slot->GetClass()))
	{
		(*Func)(Slot, S);
	}
}

// ============================================================================
// Widget 属性应用（拆分为独立函数，提高可读性）
// ============================================================================

namespace
{
	#define GET_PROP(Key) P.Find(TEXT(Key))

	void ApplyTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTextBlock* T = CastChecked<UTextBlock>(W);
		if (const FString* V = GET_PROP("Text")) T->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("Color")) T->SetColorAndOpacity(FSlateColor(FUMGWidgetUtils::ParseColor(*V)));
		if (const FString* V = GET_PROP("FontSize"))
		{
			FSlateFontInfo F = T->GetFont();
			F.Size = FCString::Atoi(**V);
			T->SetFont(F);
		}
		if (const FString* V = GET_PROP("Justification"))
		{
			static const TMap<FString, ETextJustify::Type> JustifyMap = {
				{TEXT("Left"), ETextJustify::Left}, {TEXT("Center"), ETextJustify::Center}, {TEXT("Right"), ETextJustify::Right}
			};
			if (const ETextJustify::Type* J = JustifyMap.Find(*V)) T->SetJustification(*J);
		}
	}

	void ApplyButtonProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UButton* B = CastChecked<UButton>(W);
		FButtonStyle S = B->GetStyle();
		if (const FString* V = GET_PROP("BackgroundColor"))
		{
			FLinearColor Color = FUMGWidgetUtils::ParseColor(*V);
			S.Normal.TintColor = FSlateColor(Color);
			S.Hovered.TintColor = FSlateColor(Color * 1.1f);
			S.Pressed.TintColor = FSlateColor(Color * 0.9f);
		}
		if (const FString* V = GET_PROP("HoveredColor")) S.Hovered.TintColor = FSlateColor(FUMGWidgetUtils::ParseColor(*V));
		if (const FString* V = GET_PROP("PressedColor")) S.Pressed.TintColor = FSlateColor(FUMGWidgetUtils::ParseColor(*V));
		B->SetStyle(S);
	}

	void ApplyImageProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UImage* I = CastChecked<UImage>(W);
		// 支持 Color 和 ColorAndOpacity 两种属性名
		const FString* ColorVal = GET_PROP("Color");
		if (!ColorVal) ColorVal = GET_PROP("ColorAndOpacity");
		if (ColorVal) I->SetColorAndOpacity(FUMGWidgetUtils::ParseColor(*ColorVal));

		if (const FString* V = GET_PROP("BrushColor")) I->SetBrushTintColor(FSlateColor(FUMGWidgetUtils::ParseColor(*V)));
		if (const FString* V = GET_PROP("TexturePath"))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, **V);
			if (!Tex) Tex = FindObject<UTexture2D>(nullptr, **V);
			if (Tex) I->SetBrushFromTexture(Tex);
		}
	}

	void ApplyProgressBarProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UProgressBar* PB = CastChecked<UProgressBar>(W);
		if (const FString* V = GET_PROP("Percent")) PB->SetPercent(FCString::Atof(**V));
		if (const FString* V = GET_PROP("FillColor")) PB->SetFillColorAndOpacity(FUMGWidgetUtils::ParseColor(*V));
	}

	void ApplySliderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Value")) CastChecked<USlider>(W)->SetValue(FCString::Atof(**V));
	}

	void ApplyCheckBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("IsChecked")) CastChecked<UCheckBox>(W)->SetIsChecked(V->ToBool());
	}

	void ApplyEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UEditableTextBox* E = CastChecked<UEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) E->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) E->SetHintText(FText::FromString(*V));
	}

	void ApplyMultiLineEditableTextBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMultiLineEditableTextBox* M = CastChecked<UMultiLineEditableTextBox>(W);
		if (const FString* V = GET_PROP("Text")) M->SetText(FText::FromString(*V));
		if (const FString* V = GET_PROP("HintText")) M->SetHintText(FText::FromString(*V));
	}

	void ApplyBorderProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UBorder* Bd = CastChecked<UBorder>(W);
		if (const FString* V = GET_PROP("BrushColor")) Bd->SetBrushColor(FUMGWidgetUtils::ParseColor(*V));
		if (const FString* V = GET_PROP("ContentColor")) Bd->SetContentColorAndOpacity(FUMGWidgetUtils::ParseColor(*V));
		if (const FString* V = GET_PROP("Padding")) Bd->SetPadding(FUMGWidgetUtils::ParseMargin(*V));
	}

	void ApplySizeBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USizeBox* SB = CastChecked<USizeBox>(W);
		if (const FString* V = GET_PROP("WidthOverride")) SB->SetWidthOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("HeightOverride")) SB->SetHeightOverride(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredWidth")) SB->SetMinDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredHeight")) SB->SetMinDesiredHeight(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredWidth")) SB->SetMaxDesiredWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxDesiredHeight")) SB->SetMaxDesiredHeight(FCString::Atof(**V));
	}

	void ApplyScrollBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScrollBox* Sc = CastChecked<UScrollBox>(W);
		if (const FString* V = GET_PROP("Orientation"))
			Sc->SetOrientation(*V == TEXT("Horizontal") ? Orient_Horizontal : Orient_Vertical);
		if (const FString* V = GET_PROP("ScrollBarVisibility"))
		{
			static const TMap<FString, ESlateVisibility> VisMap = {
				{TEXT("Visible"), ESlateVisibility::Visible}, {TEXT("Collapsed"), ESlateVisibility::Collapsed}, {TEXT("Hidden"), ESlateVisibility::Hidden}
			};
			if (const ESlateVisibility* Vis = VisMap.Find(*V)) Sc->SetScrollBarVisibility(*Vis);
		}
	}

	void ApplySpacerProps(UWidget* W, const TMap<FString, FString>& P)
	{
		FVector2D Size(0, 0);
		if (const FString* V = GET_PROP("Width")) Size.X = FCString::Atof(**V);
		if (const FString* V = GET_PROP("Height")) Size.Y = FCString::Atof(**V);
		CastChecked<USpacer>(W)->SetSize(Size);
	}

	void ApplyRichTextBlockProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("Text")) CastChecked<URichTextBlock>(W)->SetText(FText::FromString(*V));
	}

	void ApplyComboBoxStringProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UComboBoxString* CB = CastChecked<UComboBoxString>(W);
		if (const FString* V = GET_PROP("Options"))
		{
			TArray<FString> Opts;
			V->ParseIntoArray(Opts, TEXT(","));
			for (const FString& O : Opts) CB->AddOption(O.TrimStartAndEnd());
		}
		if (const FString* V = GET_PROP("SelectedOption")) CB->SetSelectedOption(*V);
	}

	void ApplySpinBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USpinBox* SpB = CastChecked<USpinBox>(W);
		if (const FString* V = GET_PROP("Value")) SpB->SetValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinValue")) SpB->SetMinValue(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MaxValue")) SpB->SetMaxValue(FCString::Atof(**V));
	}

	void ApplyBackgroundBlurProps(UWidget* W, const TMap<FString, FString>& P)
	{
		if (const FString* V = GET_PROP("BlurStrength")) CastChecked<UBackgroundBlur>(W)->SetBlurStrength(FCString::Atof(**V));
	}

	void ApplyGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UGridPanel* G = CastChecked<UGridPanel>(W);
		if (const FString* V = GET_PROP("ColumnFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetColumnFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
		if (const FString* V = GET_PROP("RowFill"))
		{
			TArray<FString> Fills;
			V->ParseIntoArray(Fills, TEXT(","));
			for (int32 i = 0; i < Fills.Num(); ++i)
			{
				G->SetRowFill(i, FCString::Atof(*Fills[i].TrimStartAndEnd()));
			}
		}
	}

	void ApplyUniformGridPanelProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UUniformGridPanel* UG = CastChecked<UUniformGridPanel>(W);
		if (const FString* V = GET_PROP("SlotPadding")) UG->SetSlotPadding(FUMGWidgetUtils::ParseMargin(*V));
		if (const FString* V = GET_PROP("MinDesiredSlotWidth")) UG->SetMinDesiredSlotWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("MinDesiredSlotHeight")) UG->SetMinDesiredSlotHeight(FCString::Atof(**V));
	}

	void ApplyWrapBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWrapBox* WB = CastChecked<UWrapBox>(W);
		if (const FString* V = GET_PROP("InnerSlotPadding")) WB->SetInnerSlotPadding(FUMGWidgetUtils::ParseVector2D(*V));
		if (const FString* V = GET_PROP("Orientation"))
			WB->SetOrientation(*V == TEXT("Vertical") ? Orient_Vertical : Orient_Horizontal);
	}

	void ApplyScaleBoxProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UScaleBox* SB = CastChecked<UScaleBox>(W);
		if (const FString* V = GET_PROP("Stretch"))
		{
			static const TMap<FString, EStretch::Type> StretchMap = {
				{TEXT("None"), EStretch::None}, {TEXT("Fill"), EStretch::Fill},
				{TEXT("ScaleToFit"), EStretch::ScaleToFit}, {TEXT("ScaleToFitX"), EStretch::ScaleToFitX},
				{TEXT("ScaleToFitY"), EStretch::ScaleToFitY}, {TEXT("ScaleToFill"), EStretch::ScaleToFill},
				{TEXT("UserSpecified"), EStretch::UserSpecified},
				{TEXT("UserSpecifiedWithClipping"), EStretch::UserSpecifiedWithClipping}
			};
			if (const EStretch::Type* S = StretchMap.Find(*V)) SB->SetStretch(*S);
		}
		if (const FString* V = GET_PROP("StretchDirection"))
		{
			static const TMap<FString, EStretchDirection::Type> DirMap = {
				{TEXT("Both"), EStretchDirection::Both}, {TEXT("DownOnly"), EStretchDirection::DownOnly}, {TEXT("UpOnly"), EStretchDirection::UpOnly}
			};
			if (const EStretchDirection::Type* D = DirMap.Find(*V)) SB->SetStretchDirection(*D);
		}
		if (const FString* V = GET_PROP("UserSpecifiedScale")) SB->SetUserSpecifiedScale(FCString::Atof(**V));
	}

	void ApplyWidgetSwitcherProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UWidgetSwitcher* WS = CastChecked<UWidgetSwitcher>(W);
		if (const FString* V = GET_PROP("ActiveWidgetIndex")) WS->SetActiveWidgetIndex(FCString::Atoi(**V));
	}

	void ApplyExpandableAreaProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UExpandableArea* EA = CastChecked<UExpandableArea>(W);
		if (const FString* V = GET_PROP("IsExpanded")) EA->SetIsExpanded(V->ToBool());
	}

	void ApplyListViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		// UListView 在 UE5.7 中没有 SetOrientation/SetEntrySpacing 公开方法
		// 这些属性需要在蓝图中设置或通过继承类配置
	}

	void ApplyTileViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UTileView* TV = CastChecked<UTileView>(W);
		if (const FString* V = GET_PROP("EntryWidth")) TV->SetEntryWidth(FCString::Atof(**V));
		if (const FString* V = GET_PROP("EntryHeight")) TV->SetEntryHeight(FCString::Atof(**V));
	}

	void ApplyTreeViewProps(UWidget* W, const TMap<FString, FString>& P)
	{
		// UTreeView 在 UE5.7 中没有 SetOrientation/SetEntrySpacing 公开方法
		// 这些属性需要在蓝图中设置或通过继承类配置
	}

	void ApplyMenuAnchorProps(UWidget* W, const TMap<FString, FString>& P)
	{
		UMenuAnchor* MA = CastChecked<UMenuAnchor>(W);
		if (const FString* V = GET_PROP("Placement"))
		{
			static const TMap<FString, EMenuPlacement> PlacementMap = {
				{TEXT("MenuPlacement_BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("MenuPlacement_AboveAnchor"), MenuPlacement_AboveAnchor},
				{TEXT("MenuPlacement_ComboBox"), MenuPlacement_ComboBox},
				{TEXT("MenuPlacement_ComboBoxRight"), MenuPlacement_ComboBoxRight},
				{TEXT("MenuPlacement_MenuRight"), MenuPlacement_MenuRight},
				{TEXT("MenuPlacement_MenuLeft"), MenuPlacement_MenuLeft},
				{TEXT("BelowAnchor"), MenuPlacement_BelowAnchor},
				{TEXT("AboveAnchor"), MenuPlacement_AboveAnchor},
			};
			if (const EMenuPlacement* Pl = PlacementMap.Find(*V)) MA->SetPlacement(*Pl);
		}
	}

	void ApplySafeZoneProps(UWidget* W, const TMap<FString, FString>& P)
	{
		USafeZone* SZ = CastChecked<USafeZone>(W);
		// 默认全部填充，根据属性覆盖
		bool bLeft = true, bRight = true, bTop = true, bBottom = true;
		if (const FString* V = GET_PROP("PadLeft")) bLeft = V->ToBool();
		if (const FString* V = GET_PROP("PadRight")) bRight = V->ToBool();
		if (const FString* V = GET_PROP("PadTop")) bTop = V->ToBool();
		if (const FString* V = GET_PROP("PadBottom")) bBottom = V->ToBool();
		SZ->SetSidesToPad(bLeft, bRight, bTop, bBottom);
	}

	#undef GET_PROP

	// 属性应用函数映射表
	using FApplyPropsFunc = void(*)(UWidget*, const TMap<FString, FString>&);
	const TMap<UClass*, FApplyPropsFunc>& GetPropsApplierMap()
	{
		static const TMap<UClass*, FApplyPropsFunc> Map = {
			// 文本/显示类
			{UTextBlock::StaticClass(), &ApplyTextBlockProps},
			{URichTextBlock::StaticClass(), &ApplyRichTextBlockProps},
			{UImage::StaticClass(), &ApplyImageProps},
			{UProgressBar::StaticClass(), &ApplyProgressBarProps},
			{USpacer::StaticClass(), &ApplySpacerProps},
			// 交互类
			{UButton::StaticClass(), &ApplyButtonProps},
			{USlider::StaticClass(), &ApplySliderProps},
			{UCheckBox::StaticClass(), &ApplyCheckBoxProps},
			{UEditableTextBox::StaticClass(), &ApplyEditableTextBoxProps},
			{UMultiLineEditableTextBox::StaticClass(), &ApplyMultiLineEditableTextBoxProps},
			{UComboBoxString::StaticClass(), &ApplyComboBoxStringProps},
			{USpinBox::StaticClass(), &ApplySpinBoxProps},
			// 容器类（CanvasPanel/Overlay 无特殊属性，子控件通过 Slot 配置）
			{UGridPanel::StaticClass(), &ApplyGridPanelProps},
			{UUniformGridPanel::StaticClass(), &ApplyUniformGridPanelProps},
			{UWrapBox::StaticClass(), &ApplyWrapBoxProps},
			{UScrollBox::StaticClass(), &ApplyScrollBoxProps},
			{UWidgetSwitcher::StaticClass(), &ApplyWidgetSwitcherProps},
			// 包装类
			{UBorder::StaticClass(), &ApplyBorderProps},
			{USizeBox::StaticClass(), &ApplySizeBoxProps},
			{UScaleBox::StaticClass(), &ApplyScaleBoxProps},
			{UBackgroundBlur::StaticClass(), &ApplyBackgroundBlurProps},
			{UExpandableArea::StaticClass(), &ApplyExpandableAreaProps},
			// 列表类
			{UListView::StaticClass(), &ApplyListViewProps},
			{UTileView::StaticClass(), &ApplyTileViewProps},
			{UTreeView::StaticClass(), &ApplyTreeViewProps},
			// 其他
			{UMenuAnchor::StaticClass(), &ApplyMenuAnchorProps},
			{USafeZone::StaticClass(), &ApplySafeZoneProps},
		};
		return Map;
	}
}

void FUMGWidgetUtils::ApplyCommonProperties(UWidget* W, const TMap<FString, FString>& Properties)
{
	#define GET_PROP(Key) Properties.Find(TEXT(Key))
	if (const FString* V = GET_PROP("Visibility")) W->SetVisibility(ParseVisibility(*V));
	if (const FString* V = GET_PROP("ToolTipText")) W->SetToolTipText(FText::FromString(*V));
	if (const FString* V = GET_PROP("IsEnabled")) W->SetIsEnabled(V->ToBool());
	if (const FString* V = GET_PROP("RenderOpacity")) W->SetRenderOpacity(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderAngle")) W->SetRenderTransformAngle(FCString::Atof(**V));
	if (const FString* V = GET_PROP("RenderScale")) W->SetRenderScale(ParseVector2D(*V, FVector2D::UnitVector));
	if (const FString* V = GET_PROP("RenderTranslation")) W->SetRenderTranslation(ParseVector2D(*V));
	#undef GET_PROP
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties)
{
	if (!Widget || Properties.Num() == 0) return;

	// 查找并调用类型特定的属性应用函数
	if (const FApplyPropsFunc* Func = GetPropsApplierMap().Find(Widget->GetClass()))
	{
		(*Func)(Widget, Properties);
	}

	ApplyCommonProperties(Widget, Properties);
}

// ============================================================================
// 图像资产绑定
// ============================================================================

void FUMGWidgetUtils::BindImageAssets(FUMGWidgetNode& Node, const TArray<FProcessedImageAsset>& Assets)
{
	// 检查当前节点的 ImageRef 属性
	if (const FString* ImageRef = Node.Properties.Find(TEXT("ImageRef")))
	{
		for (const FProcessedImageAsset& Asset : Assets)
		{
			if (Asset.Id == *ImageRef)
			{
				Node.Properties.Add(TEXT("TexturePath"), Asset.TexturePath);
				break;
			}
		}
	}

	// 递归处理子节点
	for (FUMGWidgetNode& Child : Node.Children)
	{
		BindImageAssets(Child, Assets);
	}
}

void FUMGWidgetUtils::GenerateImageAssetsFromWidgets(FUMGWidgetNode& Node, TArray<FUMGImageAsset>& OutAssets)
{
	// 如果是 Image 类型且没有 ImageRef，根据 Slot 信息生成 ImageAsset
	if (Node.Type.Equals(TEXT("Image"), ESearchCase::IgnoreCase) &&
		!Node.Properties.Contains(TEXT("ImageRef")) &&
		!Node.Properties.Contains(TEXT("TexturePath")))
	{
		FUMGImageAsset Asset;
		Asset.Id = Node.Name;
		Asset.Type = TEXT("Icon");

		// 从 Properties 获取 Widget 的设计尺寸
		int32 WidthOverride = 0, HeightOverride = 0;
		if (const FString* W = Node.Properties.Find(TEXT("WidthOverride")))
			WidthOverride = FCString::Atoi(**W);
		if (const FString* H = Node.Properties.Find(TEXT("HeightOverride")))
			HeightOverride = FCString::Atoi(**H);

		// 当 AI 没有返回 ImageAssets 时，我们无法知道图像在源图中的位置
		// 使用特殊值 -1 表示"使用整个源图"
		// ProcessImageAssetsForPreview 会检测这个值并使用整个源图
		Asset.X = -1;
		Asset.Y = -1;
		Asset.Width = -1;
		Asset.Height = -1;

		// SuggestedWidth/Height 用于指定输出纹理的尺寸
		if (WidthOverride > 0 && HeightOverride > 0)
		{
			Asset.SuggestedWidth = WidthOverride;
			Asset.SuggestedHeight = HeightOverride;
		}
		else
		{
			// 从 Offsets 推断尺寸
			const FUMGSlotData& Slot = Node.Slot;
			int32 W = FMath::RoundToInt(Slot.Offsets.Right - Slot.Offsets.Left);
			int32 H = FMath::RoundToInt(Slot.Offsets.Bottom - Slot.Offsets.Top);
			if (W > 0 && H > 0)
			{
				Asset.SuggestedWidth = W;
				Asset.SuggestedHeight = H;
			}
		}

		if (Asset.SuggestedWidth > 0 && Asset.SuggestedHeight > 0)
		{
			// 给 Widget 添加 ImageRef，以便后续 BindImageAssets 能匹配
			Node.Properties.Add(TEXT("ImageRef"), Asset.Id);
			OutAssets.Add(Asset);
		}
	}

	// 递归处理子节点
	for (FUMGWidgetNode& Child : Node.Children)
	{
		GenerateImageAssetsFromWidgets(Child, OutAssets);
	}
}

namespace
{
	// 递归遍历节点计算最大边界
	void TraverseNodeForDesignSize(const FUMGWidgetNode& Node, FVector2D& MaxBounds)
	{
		const FUMGSlotData& Slot = Node.Slot;

		// 检查是否是点锚点（Min == Max）
		bool bIsPointAnchor = FMath::IsNearlyEqual(Slot.Anchors.Min.X, Slot.Anchors.Max.X) &&
		                      FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Slot.Anchors.Max.Y);

		if (bIsPointAnchor)
		{
			// 位置（Left/Top 是相对于锚点的偏移）
			float PosX = Slot.Offsets.Left;
			float PosY = Slot.Offsets.Top;

			// Widget 尺寸
			float Width = Slot.Offsets.Right - Slot.Offsets.Left;
			float Height = Slot.Offsets.Bottom - Slot.Offsets.Top;

			// 如果有 WidthOverride/HeightOverride，使用它们
			if (const FString* W = Node.Properties.Find(TEXT("WidthOverride")))
				Width = FCString::Atof(**W);
			if (const FString* H = Node.Properties.Find(TEXT("HeightOverride")))
				Height = FCString::Atof(**H);

			// 计算右下角边界 = 位置 + 尺寸
			MaxBounds.X = FMath::Max(MaxBounds.X, FMath::Abs(PosX) + Width);
			MaxBounds.Y = FMath::Max(MaxBounds.Y, FMath::Abs(PosY) + Height);
		}

		// 递归处理子节点
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			TraverseNodeForDesignSize(Child, MaxBounds);
		}
	}
}

FVector2D FUMGWidgetUtils::CalculateDesignSize(const FUMGWidgetNode& RootNode, FVector2D DefaultSize)
{
	FVector2D MaxBounds = DefaultSize;
	TraverseNodeForDesignSize(RootNode, MaxBounds);
	return MaxBounds;
}
