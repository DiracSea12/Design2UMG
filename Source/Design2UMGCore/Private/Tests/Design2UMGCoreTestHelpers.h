// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

// Shared typed widget-property assertion vocabulary and fixed-size node builder for
// Design2UMGCore automation tests (moved verbatim from Design2UMGCorePrivateHelperTests.cpp).

#include "Design2UMGCoreTestNodes.h"

#include "DesignSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeAccess.h"

namespace Design2UMGCoreTests
{
	// Test-only typed assertion vocabulary: reads widget payload fields directly
	// (no production property lookup layer) and renders them through the shared
	// write-side serializers so assertions keep comparing serialized values.
	inline bool TryGetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue)
	{
		OutValue.Reset();
		const FString KeyString(Key);
		if (KeyString == UMGPropertyKeys::BrushColor)
		{
			if (Node.Brush.BrushColor.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.BrushColor.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::Clipping)
		{
			if (Node.Render.Clipping.IsSet())
			{
				OutValue = Node.Render.Clipping.GetValue();
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::DrawAs)
		{
			if (Node.Brush.DrawAs.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeDrawAs(Node.Brush.DrawAs.GetValue());
				return !OutValue.IsEmpty();
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::HeightOverride)
		{
			if (Node.WidgetStyle.HeightOverride.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.HeightOverride.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::MaterialParams)
		{
			// Descriptor-identical precedence: gradient params first, then explicit params.
			if (Node.Brush.Gradient.HasAny())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
				if (!OutValue.IsEmpty())
				{
					return true;
				}
			}
			if (Node.Brush.MaterialParams.IsSet())
			{
				OutValue = Node.Brush.MaterialParams.GetValue();
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::MaterialRef)
		{
			// Descriptor-identical precedence: gradient ref first, then explicit ref.
			if (Node.Brush.Gradient.HasAny())
			{
				OutValue = Design2UMGWidgetNodeAccess::GetGradientMaterialRef(Node.Brush.Gradient);
				return true;
			}
			if (Node.Brush.MaterialRef.IsSet())
			{
				OutValue = Node.Brush.MaterialRef.GetValue();
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::OutlineColor)
		{
			if (Node.Brush.OutlineColor.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetColor(Node.Brush.OutlineColor.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::Padding)
		{
			if (Node.WidgetStyle.Padding.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetMargin(Node.WidgetStyle.Padding.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::PathGeometry)
		{
			if (Node.Path.Geometry.IsSet())
			{
				OutValue = Node.Path.Geometry.GetValue();
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::RenderAngle)
		{
			if (Node.Render.RenderAngle.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderAngle.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::RenderOpacity)
		{
			if (Node.Render.RenderOpacity.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.Render.RenderOpacity.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::RenderTransformPivot)
		{
			if (Node.Render.RenderTransformPivot.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetVector2D(Node.Render.RenderTransformPivot.GetValue());
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::TexturePath)
		{
			if (Node.Brush.Image.TexturePath.IsSet())
			{
				OutValue = Node.Brush.Image.TexturePath.GetValue();
				return true;
			}
			return false;
		}
		if (KeyString == UMGPropertyKeys::WidthOverride)
		{
			if (Node.WidgetStyle.WidthOverride.IsSet())
			{
				OutValue = Design2UMGWidgetNodeAccess::SerializeWidgetFloat(Node.WidgetStyle.WidthOverride.GetValue());
				return true;
			}
			return false;
		}
		return false;
	}

	inline FString GetWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		TryGetWidgetProperty(Node, Key, Value);
		return Value;
	}

	inline bool HasWidgetProperty(const FUMGWidgetNode& Node, const TCHAR* Key)
	{
		FString Value;
		return TryGetWidgetProperty(Node, Key, Value);
	}

	inline TSharedPtr<FDesignNode> MakeFixedNode(const EDesignNodeType Type, const float Width, const float Height)
	{
		return Design2UMGCoreTests::MakeNode(FString(), Type, FDesignSize::Fixed(Width), FDesignSize::Fixed(Height));
	}

}

