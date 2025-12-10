// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGSettingsCustomization.h"
#include "ImageToUMGSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "ImageToUMGSettings"

TSharedRef<IDetailCustomization> FImageToUMGSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FImageToUMGSettingsCustomization);
}

void FImageToUMGSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// ========== API Settings Category ==========
	IDetailCategoryBuilder& APICategory = DetailBuilder.EditCategory("API Settings");

	// 添加刷新模型按钮
	APICategory.AddCustomRow(LOCTEXT("RefreshModels", "Refresh Models"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RefreshModelsLabel", "Refresh Model Lists"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MaxDesiredWidth(300.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshBtn", "Refresh from API"))
				.ToolTipText(LOCTEXT("RefreshBtnTip", "Fetch available models from the API (requires valid API Key)"))
				.OnClicked_Lambda([]()
				{
					UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
					Settings->RefreshAvailableModels();
					return FReply::Handled();
				})
			]
		];

	// 添加可用模型列表显示
	APICategory.AddCustomRow(LOCTEXT("AvailableModels", "Available Models"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AvailableModelsLabel", "Available Models"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("AvailableModelsTip", "Models fetched from the API. Click 'Refresh from API' to update."))
		]
		.ValueContent()
		.MaxDesiredWidth(400.f)
		[
			SNew(STextBlock)
			.Text_Lambda([]()
			{
				UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
				if (Settings->CachedModels.Num() == 0)
				{
					return FText::FromString(TEXT("(Click 'Refresh from API' to fetch)"));
				}
				// 显示前 5 个模型，如果更多则显示 "..."
				FString ModelList;
				int32 Count = FMath::Min(5, Settings->CachedModels.Num());
				for (int32 i = 0; i < Count; i++)
				{
					if (i > 0) ModelList += TEXT(", ");
					ModelList += Settings->CachedModels[i];
				}
				if (Settings->CachedModels.Num() > 5)
				{
					ModelList += FString::Printf(TEXT(" ... (+%d more)"), Settings->CachedModels.Num() - 5);
				}
				return FText::FromString(ModelList);
			})
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AutoWrapText(true)
		];

	// 添加说明文字
	APICategory.AddCustomRow(LOCTEXT("ModelNote", "Note"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModelNoteText", "Tip: You can manually enter model name in the Model field. For providers without /models API support (e.g. Volcengine), enter the model name from their documentation."))
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];

	// ========== Prompt Settings Category ==========
	TSharedRef<IPropertyHandle> SystemPromptHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UImageToUMGSettings, SystemPrompt));
	IDetailCategoryBuilder& PromptCategory = DetailBuilder.EditCategory("Prompt Settings");

	// 先隐藏原来的 SystemPrompt
	DetailBuilder.HideProperty(SystemPromptHandle);

	// 重新添加 SystemPrompt
	PromptCategory.AddProperty(SystemPromptHandle);

	// 添加重置按钮
	PromptCategory.AddCustomRow(LOCTEXT("ResetPrompt", "Reset Prompt"))
		.NameContent()
		[
			SNullWidget::NullWidget
		]
		.ValueContent()
		.MaxDesiredWidth(200.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ResetPromptBtn", "Reset Prompt to Default"))
			.OnClicked_Lambda([]()
			{
				UImageToUMGSettings* Settings = UImageToUMGSettings::Get();
				Settings->SystemPrompt = UImageToUMGSettings::GetDefaultSystemPrompt();
				Settings->SaveConfig();
				return FReply::Handled();
			})
		];

}

#undef LOCTEXT_NAMESPACE
