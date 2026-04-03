void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver)
{
	if (JsonObj->HasField(TEXT("width")))
	{
		OutNode.Width = ParseSize(JsonObj->TryGetField(TEXT("width")), SizeNodeType, Resolver);
		OutNode.bHasWidth = true;
	}
	if (JsonObj->HasField(TEXT("height")))
	{
		OutNode.Height = ParseSize(JsonObj->TryGetField(TEXT("height")), SizeNodeType, Resolver);
		OutNode.bHasHeight = true;
	}
	if (JsonObj->HasField(TEXT("x")))
		OutNode.bHasX = TryGetResolvedNumberField(JsonObj, TEXT("x"), Resolver, OutNode.X);
	if (JsonObj->HasField(TEXT("y")))
		OutNode.bHasY = TryGetResolvedNumberField(JsonObj, TEXT("y"), Resolver, OutNode.Y);
}

void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties)
{
	if (JsonObj->HasField(TEXT("opacity")))
		OutNode.bHasOpacity = TryGetResolvedNumberField(JsonObj, TEXT("opacity"), Resolver, OutNode.Opacity);
	if (JsonObj->HasField(TEXT("gap")))
		OutNode.bHasGap = TryGetResolvedNumberField(JsonObj, TEXT("gap"), Resolver, OutNode.Gap);
	if (JsonObj->HasField(TEXT("padding")))
	{
		OutNode.Padding = ParsePadding(JsonObj->TryGetField(TEXT("padding")), Resolver);
		OutNode.bHasPadding = true;
	}
	OutNode.bHasPaddingRuntimeWrapDecision = true;
	OutNode.bPaddingRequiresRuntimeWrap =
		OutNode.Layout == EDesignLayout::None
		|| OutNode.Layout == EDesignLayout::Wrap
		|| OutNode.Layout == EDesignLayout::Grid;
	FString JustifyContent;
	if (JsonObj->TryGetStringField(TEXT("justifyContent"), JustifyContent))
		OutNode.JustifyContent = ParseJustifyContent(JustifyContent);
	FString AlignItems;
	if (JsonObj->TryGetStringField(TEXT("alignItems"), AlignItems))
		OutNode.AlignItems = ParseAlignItems(AlignItems);
	if (JsonObj->HasField(TEXT("clip")))
		OutNode.bHasClip = TryGetResolvedBoolField(JsonObj, TEXT("clip"), Resolver, OutNode.bClip);
	if (!bIncludeTextProperties)
		return;
	FString TextAlign;
	if (JsonObj->TryGetStringField(TEXT("textAlign"), TextAlign))
		OutNode.TextAlign = ParseTextAlign(TextAlign);
	TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, OutNode.LineHeight);
	FString TextGrowth;
	if (JsonObj->TryGetStringField(TEXT("textGrowth"), TextGrowth))
		OutNode.TextGrowth = ParseTextGrowth(TextGrowth);
}

void ApplyNodeTextStyleProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	FString FontFamily;
	if (JsonObj->TryGetStringField(TEXT("fontFamily"), FontFamily))
		OutNode.FontFamily = Resolver.ResolveString(FontFamily);
	TryGetResolvedNumberField(JsonObj, TEXT("fontSize"), Resolver, OutNode.FontSize);
	JsonObj->TryGetStringField(TEXT("fontWeight"), OutNode.FontWeight);
	JsonObj->TryGetStringField(TEXT("fontStyle"), OutNode.FontStyle);
	TryGetResolvedBoolField(JsonObj, TEXT("underline"), Resolver, OutNode.bUnderline);
	TryGetResolvedBoolField(JsonObj, TEXT("strikethrough"), Resolver, OutNode.bStrikethrough);
	JsonObj->TryGetStringField(TEXT("href"), OutNode.Href);
}
