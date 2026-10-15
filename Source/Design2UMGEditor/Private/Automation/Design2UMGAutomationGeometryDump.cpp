// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationGeometryDump.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Design2UMGLog.h"
#include "Design2UMGAutomationDumpFormat.h"

#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"

using Design2UMGAutomation::RenderFloat;

namespace
{
	TAutoConsoleVariable<bool> CVarDesign2UMGAutomationGeometryDump(
		TEXT("design2umgAutomation.GeometryDump"),
		false,
		TEXT("Enables the geometry golden dump (geometry_dump.json) of the compiled widget tree during automation renders. Also requires a non-empty automation dump path in the render spec."),
		ECVF_Default);

	// Child walk mirrors the structure snapshot's widget-tree walk (UserWidget
	// root, then panel children, then single-content widget) with the same seen-set
	// guard, but without the visibility filter: hidden widgets stay in the dump and
	// simply report geometry_valid=false on the python side.
	void GatherGeometryChildren(const UWidget* Widget, TArray<const UWidget*>& OutChildren)
	{
		OutChildren.Reset();
		if (!Widget)
		{
			return;
		}

		TSet<const UWidget*> Seen;
		auto TryAddChild = [&OutChildren, &Seen](const UWidget* Child)
		{
			if (Child && !Seen.Contains(Child))
			{
				Seen.Add(Child);
				OutChildren.Add(Child);
			}
		};

		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
			{
				TryAddChild(UserWidget->WidgetTree->RootWidget);
			}
		}

		if (const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < PanelWidget->GetChildrenCount(); ++ChildIndex)
			{
				TryAddChild(PanelWidget->GetChildAt(ChildIndex));
			}
			return;
		}

		if (const UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
		{
			TryAddChild(ContentWidget->GetContent());
		}
	}

	void AppendNodeJson(TArray<FString>& Nodes, const UWidget* Widget, const FString& Path)
	{
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2f LocalSize(Geometry.GetLocalSize());
		const FVector2f AbsolutePosition(Geometry.GetAbsolutePosition());

		// An un-laid-out SWidget (NullWidget / collapsed) reports an all-zero
		// geometry that must not masquerade as a real (0,0,0,0) layout result.
		const bool bGeometryValid =
			Widget->GetCachedWidget().IsValid() && (LocalSize.X != 0.0f || LocalSize.Y != 0.0f);

		Nodes.Add(FString::Printf(
			TEXT("{")
			TEXT("\"abs_x\":%s,\"abs_y\":%s,")
			TEXT("\"class\":\"%s\",")
			TEXT("\"geometry_valid\":%s,")
			TEXT("\"h\":%s,")
			TEXT("\"name\":\"%s\",")
			TEXT("\"path\":\"%s\",")
			TEXT("\"w\":%s")
			TEXT("}"),
			*RenderFloat(AbsolutePosition.X),
			*RenderFloat(AbsolutePosition.Y),
			*Design2UMGAutomation::EscapeJsonString(Widget->GetClass()->GetName()),
			bGeometryValid ? TEXT("true") : TEXT("false"),
			*RenderFloat(LocalSize.Y),
			*Design2UMGAutomation::EscapeJsonString(Widget->GetName()),
			*Design2UMGAutomation::EscapeJsonString(Path),
			*RenderFloat(LocalSize.X)));
	}

	void AppendSubtreeJson(TArray<FString>& Nodes, const UWidget* Widget, const FString& Path)
	{
		AppendNodeJson(Nodes, Widget, Path);

		TArray<const UWidget*> Children;
		GatherGeometryChildren(Widget, Children);
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			AppendSubtreeJson(Nodes, Children[ChildIndex], Path + TEXT("/") + FString::FromInt(ChildIndex));
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
FString Design2UMGAutomationGeometryDump::GDesign2UMGAutomationGeometryDumpPath;
#endif

void Design2UMGAutomationGeometryDump::MaybeWriteGeometryDump(const UWidgetTree* WidgetTree)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!CVarDesign2UMGAutomationGeometryDump.GetValueOnGameThread() || GDesign2UMGAutomationGeometryDumpPath.IsEmpty())
	{
		return;
	}

	if (!WidgetTree || !WidgetTree->RootWidget)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("GeometryDump: no widget tree to dump for %s"), *GDesign2UMGAutomationGeometryDumpPath);
		return;
	}

	TArray<FString> Nodes;
	AppendSubtreeJson(Nodes, WidgetTree->RootWidget, TEXT("0"));

	FString Json(TEXT("{\"nodes\":["));
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Index > 0)
		{
			Json += TEXT(",");
		}
		Json += Nodes[Index];
	}
	Json += TEXT("]}\n");

	if (FFileHelper::SaveStringToFile(Json, *GDesign2UMGAutomationGeometryDumpPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("GeometryDump: wrote %s"), *GDesign2UMGAutomationGeometryDumpPath);
	}
	else
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("GeometryDump: failed to write %s"), *GDesign2UMGAutomationGeometryDumpPath);
	}
#endif
}
