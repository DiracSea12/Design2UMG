#include "Design2UMGPrototypeAnimationSource.h"
#include "Design2UMGPrototypeStepGraphAssembly.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignJsonUtils.h"
#include "FigmaComponentDependencies.h"
#include "FigmaParser.h"
#include "FigmaParserStyling.h"
#include "VisualMapper.h"
#include "UObject/Class.h"
#include "UMGValueParsing.h"

namespace
{
	bool ReadLocalSize(const FDesignNode& Node, FVector2D& Size, FString& Error)
	{
		const auto Raw = Node.Source().Prototype.AnimationSource;
		const auto Local = DesignJsonUtils::GetObject(Raw, TEXT("size"));
		const auto Bounds = DesignJsonUtils::GetObject(Raw, TEXT("absoluteBoundingBox"));
		double Width = 0, Height = 0;
		if (!(Local ? (Local->TryGetNumberField(TEXT("x"), Width) && Local->TryGetNumberField(TEXT("y"), Height))
			: (Bounds && Bounds->TryGetNumberField(TEXT("width"), Width) && Bounds->TryGetNumberField(TEXT("height"), Height)))
			|| !FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width < 0 || Height < 0)
		{
			Error = TEXT("property animation requires real finite nonnegative source size");
			return false;
		}
		Size = FVector2D(Width, Height);
		return true;
	}

	bool ReadLocalGeometry(const FDesignNode& Node, const FVector2D& Size, FVector2D& Center, double& Angle, FString& Error)
	{
		const auto Raw = Node.Source().Prototype.AnimationSource;
		Center = FVector2D(Node.Transform.X, Node.Transform.Y) + Size * 0.5;
		Angle = Node.Transform.Rotation;
		const auto Matrix = DesignJsonUtils::GetArray(Raw, TEXT("relativeTransform"));
		if (!Matrix)
		{
			if (!FMath::IsNearlyZero(Angle))
			{
				Error = TEXT("rotation requires source-local size and relativeTransform, not rotated bounding-box guesses");
				return false;
			}
			return true;
		}
		if (Matrix->Num() != 2 || (*Matrix)[0]->Type != EJson::Array || (*Matrix)[1]->Type != EJson::Array
			|| (*Matrix)[0]->AsArray().Num() != 3 || (*Matrix)[1]->AsArray().Num() != 3)
		{
			Error = TEXT("invalid source relativeTransform");
			return false;
		}
		const auto& First = (*Matrix)[0]->AsArray();
		const auto& Second = (*Matrix)[1]->AsArray();
		double Values[6];
		for (int32 Column = 0; Column < 3; ++Column)
		{
			if (!First[Column]->TryGetNumber(Values[Column]) || !Second[Column]->TryGetNumber(Values[Column + 3])
				|| !FMath::IsFinite(Values[Column]) || !FMath::IsFinite(Values[Column + 3]))
			{
				Error = TEXT("relativeTransform requires finite numeric entries");
				return false;
			}
		}
		const FVector2D AxisX(Values[0], Values[3]);
		const FVector2D AxisY(Values[1], Values[4]);
		if (!FMath::IsNearlyEqual(AxisX.Size(), 1.0) || !FMath::IsNearlyEqual(AxisY.Size(), 1.0)
			|| !FMath::IsNearlyZero(FVector2D::DotProduct(AxisX, AxisY))
			|| Values[0] * Values[4] - Values[1] * Values[3] < 0)
		{
			Error = TEXT("shear, affine scale or reflection cannot be silently reduced to rotation");
			return false;
		}
		Angle = FMath::RadiansToDegrees(FMath::Atan2(Values[3], Values[0]));
		if (!FMath::IsNearlyZero(Angle) && !Raw->HasField(TEXT("size")))
		{
			Error = TEXT("rotated source has no local size");
			return false;
		}
		Center = FVector2D(Values[2], Values[5]) + AxisX * Size.X * 0.5 + AxisY * Size.Y * 0.5;
		return true;
	}

	bool EqualFields(const TSharedPtr<FJsonObject>& Start, const TSharedPtr<FJsonObject>& End,
		const TSet<FString>& Excluded, FString& Error)
	{
		TSet<FString> Keys;
		for (const auto& Pair : Start->Values) Keys.Add(FString(Pair.Key));
		for (const auto& Pair : End->Values) Keys.Add(FString(Pair.Key));
		for (const FString& Key : Keys)
		{
			if (Excluded.Contains(Key)) continue;
			const auto Left = Start->TryGetField(Key);
			const auto Right = End->TryGetField(Key);
			if (!Left || !Right || !FJsonValue::CompareEqual(*Left, *Right))
			{
				Error = FString::Printf(TEXT("unsupported change to '%s'"), *Key);
				return false;
			}
		}
		return true;
	}

	bool MatchInactiveMotion(const TSharedPtr<FJsonObject>& Start, const TSharedPtr<FJsonObject>& End, FString& Error)
	{
		for (const auto& Node : { Start, End })
		{
			for (const TCHAR* Field : { TEXT("animationStyles"), TEXT("animations"), TEXT("manualKeyframeTracks") })
			{
				const auto Value = Node->TryGetField(Field);
				if (Value && !((Value->Type == EJson::Array && Value->AsArray().IsEmpty())
					|| (Value->Type == EJson::Object && Value->AsObject()->Values.IsEmpty())))
				{
					Error = FString::Printf(TEXT("Motion tracks in '%s' are not supported by widget property transitions"), Field);
					return false;
				}
			}
		}
		const auto Left = Start->TryGetField(TEXT("timelines"));
		const auto Right = End->TryGetField(TEXT("timelines"));
		if (!Left && !Right) return true;
		if (!Left || !Right || Left->Type != EJson::Array || Right->Type != EJson::Array
			|| Left->AsArray().Num() != Right->AsArray().Num())
		{
			Error = TEXT("unsupported change to 'timelines'");
			return false;
		}
		for (int32 Index = 0; Index < Left->AsArray().Num(); ++Index)
		{
			const auto LeftTimeline = Left->AsArray()[Index]->AsObject();
			const auto RightTimeline = Right->AsArray()[Index]->AsObject();
			// An inactive timeline identifies the containing frame; its duration and other data still match.
			if (!LeftTimeline || !RightTimeline || !EqualFields(LeftTimeline, RightTimeline, { TEXT("id") }, Error))
			{
				Error = TEXT("unsupported change to 'timelines' beyond frame identity");
				return false;
			}
		}
		return true;
	}

	bool EqualObjectExcept(const TSharedPtr<FJsonObject>& Start, const TSharedPtr<FJsonObject>& End,
		const TCHAR* Field, const TCHAR* ExcludedField)
	{
		auto Left = DesignJsonUtils::GetObject(Start, Field);
		auto Right = DesignJsonUtils::GetObject(End, Field);
		if (!Left) Left = MakeShared<FJsonObject>();
		if (!Right) Right = MakeShared<FJsonObject>();
		FString Ignored;
		return EqualFields(Left, Right, { FString(ExcludedField) }, Ignored);
	}

	bool MatchFills(const TSharedPtr<FJsonObject>& Start, const TSharedPtr<FJsonObject>& End,
		bool bCanChangeColor, bool& bChanged, FString& Error)
	{
		const auto Left = Start->TryGetField(TEXT("fills"));
		const auto Right = End->TryGetField(TEXT("fills"));
		bChanged = (Left || Right) && (!Left || !Right || !FJsonValue::CompareEqual(*Left, *Right));
		if (!bChanged) return true;
		const auto LeftFills = DesignJsonUtils::GetArray(Start, TEXT("fills"));
		const auto RightFills = DesignJsonUtils::GetArray(End, TEXT("fills"));
		const auto HasVisiblePaint = [](const TArray<TSharedPtr<FJsonValue>>* Paints)
		{
			return Paints && Paints->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
			{ return DesignJsonUtils::GetBool(Value->AsObject(), TEXT("visible"), true); });
		};
		if (LeftFills && RightFills && !HasVisiblePaint(LeftFills) && !HasVisiblePaint(RightFills))
		{
			bChanged = false;
			return true;
		}
		if (bCanChangeColor && LeftFills && RightFills && LeftFills->Num() == 1 && RightFills->Num() == 1)
		{
			const auto LeftPaint = (*LeftFills)[0]->AsObject();
			const auto RightPaint = (*RightFills)[0]->AsObject();
			if (LeftPaint && RightPaint && DesignJsonUtils::GetString(LeftPaint, TEXT("type")) == TEXT("SOLID")
				&& DesignJsonUtils::GetString(RightPaint, TEXT("type")) == TEXT("SOLID")
				&& EqualFields(LeftPaint, RightPaint, { TEXT("color"), TEXT("opacity"), TEXT("boundVariables") }, Error)
				&& EqualObjectExcept(LeftPaint, RightPaint, TEXT("boundVariables"), TEXT("color"))) return true;
		}
		Error = TEXT("only a single unchanged solid paint type can change color");
		return false;
	}

	bool MatchSurfacePaints(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right, bool bText, bool& bChanged, FString& Error)
	{
		const auto LeftValue = Left->TryGetField(TEXT("fills"));
		const auto RightValue = Right->TryGetField(TEXT("fills"));
		bChanged = (LeftValue || RightValue) && (!LeftValue || !RightValue || !FJsonValue::CompareEqual(*LeftValue, *RightValue));
		if (!bChanged) return true;
		const auto A = DesignJsonUtils::GetArray(Left, TEXT("fills"));
		const auto B = DesignJsonUtils::GetArray(Right, TEXT("fills"));
		if (!A || !B) { Error = TEXT("text paint stack is missing"); return false; }
		TArray<TSharedPtr<FJsonObject>> VisibleA, VisibleB;
		for (const auto& Value : *A) { auto Paint = Value->AsObject(); if (Paint && DesignJsonUtils::GetBool(Paint, TEXT("visible"), true)) VisibleA.Add(Paint); }
		for (const auto& Value : *B) { auto Paint = Value->AsObject(); if (Paint && DesignJsonUtils::GetBool(Paint, TEXT("visible"), true)) VisibleB.Add(Paint); }
		if (VisibleA.IsEmpty() && VisibleB.IsEmpty()) { bChanged = false; return true; }
		const auto Supported = [bText](const TSharedPtr<FJsonObject>& Paint)
		{
			const FString Type = DesignJsonUtils::GetString(Paint, TEXT("type"));
			return Type == TEXT("SOLID") || Type == TEXT("GRADIENT_LINEAR") || (bText && (Type == TEXT("GRADIENT_RADIAL") || Type == TEXT("GRADIENT_ANGULAR")));
		};
		for (const auto& Paint : VisibleA) if (!Supported(Paint)) { Error = TEXT("unsupported source text paint"); return false; }
		for (const auto& Paint : VisibleB) if (!Supported(Paint)) { Error = TEXT("unsupported destination text paint"); return false; }
		for (int32 Index = 0; Index < FMath::Min(VisibleA.Num(), VisibleB.Num()); ++Index)
		{
			if (DesignJsonUtils::GetString(VisibleA[Index], TEXT("type")) != DesignJsonUtils::GetString(VisibleB[Index], TEXT("type")))
			{ Error = TEXT("text paint layers cannot be uniquely paired"); return false; }
			FString PaintError;
			const bool bSolid = DesignJsonUtils::GetString(VisibleA[Index], TEXT("type")) == TEXT("SOLID");
			if (!EqualFields(VisibleA[Index], VisibleB[Index], bSolid
				? TSet<FString>{ TEXT("opacity"), TEXT("color"), TEXT("boundVariables") }
				: TSet<FString>{ TEXT("opacity"), TEXT("gradientStops") }, PaintError))
			{ Error = TEXT("text paint properties cannot be interpolated"); return false; }
			if (!bSolid)
			{
				const auto StopsA = DesignJsonUtils::GetArray(VisibleA[Index], TEXT("gradientStops"));
				const auto StopsB = DesignJsonUtils::GetArray(VisibleB[Index], TEXT("gradientStops"));
				if (!StopsA || !StopsB || StopsA->Num() != StopsB->Num()) { Error = TEXT("animated gradient stop topology changes"); return false; }
				for (int32 Stop = 0; Stop < StopsA->Num(); ++Stop)
					if (!EqualFields((*StopsA)[Stop]->AsObject(), (*StopsB)[Stop]->AsObject(), { TEXT("color"), TEXT("boundVariables") }, PaintError))
					{ Error = TEXT("animated gradient stop positions change"); return false; }
			}
		}
		return true;
	}

	bool HasInstanceOverride(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right,
		const TSharedPtr<FJsonObject>& Properties, const TCHAR* Field)
	{
		const auto LeftRefs = DesignJsonUtils::GetObject(Left, TEXT("componentPropertyReferences"));
		const auto RightRefs = DesignJsonUtils::GetObject(Right, TEXT("componentPropertyReferences"));
		const FString Key = DesignJsonUtils::GetString(LeftRefs, Field);
		if (Key.IsEmpty() || Key != DesignJsonUtils::GetString(RightRefs, Field)) return false;
		const auto Property = DesignJsonUtils::GetObject(Properties, *Key);
		const auto SourceValue = Left->TryGetField(Field);
		const auto OverrideValue = Property ? Property->TryGetField(TEXT("value")) : nullptr;
		return SourceValue && OverrideValue && FJsonValue::CompareEqual(*SourceValue, *OverrideValue);
	}

	TSharedPtr<FJsonObject> BuildSourceSubtree(const FDesignNode& Node)
	{
		const auto Source = Node.Source().Prototype.AnimationSource;
		if (!Source) return nullptr;
		auto Result = MakeShared<FJsonObject>();
		Result->Values = Source->Values;
		if (!Node.Children.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Children;
			for (const auto& Child : Node.Children)
			{
				const auto ChildSource = Child ? BuildSourceSubtree(*Child) : nullptr;
				if (!ChildSource) return nullptr;
				Children.Add(MakeShared<FJsonValueObject>(ChildSource));
			}
			Result->SetArrayField(TEXT("children"), Children);
		}
		return Result;
	}

	bool ApplyInstanceProperties(const TSharedPtr<FJsonObject>& Node, const TSharedPtr<FJsonObject>& Properties, FString& Error)
	{
		if (!Node) return false;
		const auto References = DesignJsonUtils::GetObject(Node, TEXT("componentPropertyReferences"));
		for (const TCHAR* Field : { TEXT("characters"), TEXT("visible"), TEXT("mainComponent") })
		{
			const FString Key = DesignJsonUtils::GetString(References, Field);
			const auto Property = DesignJsonUtils::GetObject(Properties, *Key);
			const auto Value = Property ? Property->TryGetField(TEXT("value")) : nullptr;
			if (!Value) continue;
			const FString Type = DesignJsonUtils::GetString(Property, TEXT("type"));
			if ((FString(Field) == TEXT("characters") && Type != TEXT("TEXT"))
				|| (FString(Field) == TEXT("visible") && Type != TEXT("BOOLEAN"))
				|| FString(Field) == TEXT("mainComponent"))
			{
				Error = TEXT("unsupported component property override");
				return false;
			}
			Node->SetField(Field, Value);
			const auto Plugin = DesignJsonUtils::GetObject(Node, TEXT("design2umgPluginData"));
			if (Plugin) Plugin->SetField(Field, Value);
			if (FString(Field) == TEXT("characters") && Plugin)
			{
				const auto Segments = DesignJsonUtils::GetArray(Plugin, TEXT("styledTextSegments"));
				if (Segments && !Segments->IsEmpty())
				{
					if (Segments->Num() != 1 || !(*Segments)[0]->AsObject() || Value->Type != EJson::String)
					{
						Error = TEXT("cannot apply text override to nonuniform styled segments");
						return false;
					}
					auto Segment = (*Segments)[0]->AsObject();
					Segment->SetStringField(TEXT("characters"), Value->AsString());
					Segment->SetNumberField(TEXT("start"), 0);
					Segment->SetNumberField(TEXT("end"), Value->AsString().Len());
				}
			}
		}
		const auto Children = DesignJsonUtils::GetArray(Node, TEXT("children"));
		if (Children)
			for (const auto& Child : *Children)
				if (!Child || !ApplyInstanceProperties(Child->AsObject(), Properties, Error)) return false;
		return true;
	}

	bool ApplyInstanceLayout(const FDesignNode& Instance, const FDesignNode* Main,
		const FDesignNode& Target, const TSharedPtr<FJsonObject>& Copy, FString& Error)
	{
		const auto Actual = Instance.Source().Prototype.AnimationSource;
		const auto Overrides = DesignJsonUtils::GetArray(Actual, TEXT("overrides"));
		TSet<FString> RootOverrides;
		if (Overrides) for (const auto& Value : *Overrides)
		{
			const auto Item = Value->AsObject();
			if (DesignJsonUtils::GetString(Item, TEXT("id")) != DesignJsonUtils::GetString(Actual, TEXT("id"))) continue;
			if (const auto Fields = DesignJsonUtils::GetArray(Item, TEXT("overriddenFields")))
				for (const auto& Field : *Fields) RootOverrides.Add(Field->AsString());
		}
		const auto SameField = [](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, const TCHAR* Field)
		{
			const auto X = A->TryGetField(Field), Y = B->TryGetField(Field);
			return (!X && !Y) || (X && Y && FJsonValue::CompareEqual(*X, *Y));
		};
		const auto Transfer = [](const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To, const TCHAR* Field)
		{
			if (const auto Value = From->TryGetField(Field)) To->SetField(Field, Value);
			else To->RemoveField(Field);
		};
		bool bPreserveExpandedLayout = false;
		if (RootOverrides.Contains(TEXT("width")) || RootOverrides.Contains(TEXT("height")))
		{
			FVector2D ActualSize, MainSize, TargetSize;
			if (!Main || !ReadLocalSize(Instance, ActualSize, Error) || !ReadLocalSize(*Main, MainSize, Error)
				|| !ReadLocalSize(Target, TargetSize, Error))
			{ Error = TEXT("cannot resolve explicit instance size against its main and target definitions"); return false; }
			bPreserveExpandedLayout = (RootOverrides.Contains(TEXT("width")) && ActualSize.X != MainSize.X && MainSize.X == TargetSize.X)
				|| (RootOverrides.Contains(TEXT("height")) && ActualSize.Y != MainSize.Y && MainSize.Y == TargetSize.Y);
		}
		// An instance owns placement in its parent, not the variant's component-canvas position.
		const auto TransferPlacement = [&](const TSharedPtr<FJsonObject>& From, const TSharedPtr<FJsonObject>& To)
		{
			for (const TCHAR* Field : {TEXT("relativeTransform"), TEXT("x"), TEXT("y"), TEXT("rotation"), TEXT("constraints"), TEXT("layoutPositioning"), TEXT("layoutAlign"), TEXT("layoutGrow")})
				Transfer(From, To, Field);
		};
		if (bPreserveExpandedLayout)
		{
			TFunction<bool(const FDesignNode&, const FDesignNode&, const FDesignNode&, const TSharedPtr<FJsonObject>&, bool)> Project;
			Project = [&](const FDesignNode& Expanded, const FDesignNode& Base, const FDesignNode& Destination,
				const TSharedPtr<FJsonObject>& Output, bool bRoot)
			{
				const auto A = Base.Source().Prototype.AnimationSource;
				const auto B = Destination.Source().Prototype.AnimationSource;
				const auto E = Expanded.Source().Prototype.AnimationSource;
				if (!A || !B || !E || Base.Children.Num() != Destination.Children.Num() || Base.Children.Num() != Expanded.Children.Num())
				{ Error = TEXT("instance size override requires matching expanded layout subtrees"); return false; }
				FVector2D BaseSize, DestinationSize;
				if (!ReadLocalSize(Base, BaseSize, Error) || !ReadLocalSize(Destination, DestinationSize, Error)
					|| !BaseSize.Equals(DestinationSize))
				{ Error = TEXT("cannot preserve expanded instance size across changed definition dimensions"); return false; }
				for (const TCHAR* Field : {TEXT("size"), TEXT("layoutMode"), TEXT("layoutWrap"), TEXT("primaryAxisSizingMode"), TEXT("counterAxisSizingMode"),
					TEXT("primaryAxisAlignItems"), TEXT("counterAxisAlignItems"), TEXT("counterAxisAlignContent"), TEXT("layoutSizingHorizontal"),
					TEXT("layoutSizingVertical"), TEXT("itemSpacing"), TEXT("counterAxisSpacing"), TEXT("paddingLeft"), TEXT("paddingRight"),
					TEXT("paddingTop"), TEXT("paddingBottom"), TEXT("layoutGrow"), TEXT("layoutAlign"), TEXT("layoutPositioning"),
					TEXT("constraints"), TEXT("cornerRadius"), TEXT("rectangleCornerRadii"), TEXT("cornerSmoothing"),
					TEXT("effects"), TEXT("strokes"), TEXT("strokeWeight"), TEXT("strokeAlign"),
					TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")})
				{
					const auto AP = DesignJsonUtils::GetObject(A, TEXT("design2umgPluginData"));
					const auto BP = DesignJsonUtils::GetObject(B, TEXT("design2umgPluginData"));
					if (!SameField(A, B, Field) || (AP && BP && !SameField(AP, BP, Field)))
					{ Error = FString::Printf(TEXT("cannot preserve expanded instance size across changed definition layout/geometry '%s'"), Field); return false; }
				}
				if (!bRoot && (!SameField(A, B, TEXT("relativeTransform")) || Base.Transform.X != Destination.Transform.X
					|| Base.Transform.Y != Destination.Transform.Y || Base.Transform.Rotation != Destination.Transform.Rotation))
				{ Error = TEXT("cannot preserve expanded instance size across changed child placement"); return false; }
				if (!bRoot && (Base.Name != Destination.Name || Base.Name != Expanded.Name || Base.Type != Destination.Type || Base.Type != Expanded.Type))
				{ Error = TEXT("instance size override requires matching child identity and parent index"); return false; }
				for (const TCHAR* Field : {TEXT("size"), TEXT("width"), TEXT("height"), TEXT("absoluteBoundingBox"), TEXT("absoluteRenderBounds"),
					TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")})
					Transfer(E, Output, Field);
				TransferPlacement(E, Output);
				const auto EP = DesignJsonUtils::GetObject(E, TEXT("design2umgPluginData"));
				const auto OP = DesignJsonUtils::GetObject(Output, TEXT("design2umgPluginData"));
				if (EP && OP)
				{
					for (const TCHAR* Field : {TEXT("size"), TEXT("width"), TEXT("height"), TEXT("absoluteBoundingBox"), TEXT("absoluteRenderBounds"),
					TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")})
						Transfer(EP, OP, Field);
					TransferPlacement(EP, OP);
				}
				const auto Children = DesignJsonUtils::GetArray(Output, TEXT("children"));
				for (int32 Index = 0; Index < Base.Children.Num(); ++Index)
					if (!Children || !Project(*Expanded.Children[Index], *Base.Children[Index], *Destination.Children[Index], (*Children)[Index]->AsObject(), false)) return false;
				return true;
			};
			if (!Project(Instance, *Main, Target, Copy, true)) return false;
		}
		else
		{
			const auto Bounds = DesignJsonUtils::GetObject(Actual, TEXT("absoluteBoundingBox"));
			const auto OutputBounds = DesignJsonUtils::GetObject(Copy, TEXT("absoluteBoundingBox"));
			if (Bounds && OutputBounds)
			{
				const double DX = DesignJsonUtils::GetNumber(Bounds, TEXT("x")) - DesignJsonUtils::GetNumber(OutputBounds, TEXT("x"));
				const double DY = DesignJsonUtils::GetNumber(Bounds, TEXT("y")) - DesignJsonUtils::GetNumber(OutputBounds, TEXT("y"));
				TFunction<void(const TSharedPtr<FJsonObject>&)> MoveBounds = [&](const TSharedPtr<FJsonObject>& Node)
				{
					for (const auto& Fields : {Node, DesignJsonUtils::GetObject(Node, TEXT("design2umgPluginData"))})
						for (const TCHAR* Field : {TEXT("absoluteBoundingBox"), TEXT("absoluteRenderBounds")})
							if (const auto Output = DesignJsonUtils::GetObject(Fields, Field))
							{
								Output->SetNumberField(TEXT("x"), DesignJsonUtils::GetNumber(Output, TEXT("x")) + DX);
								Output->SetNumberField(TEXT("y"), DesignJsonUtils::GetNumber(Output, TEXT("y")) + DY);
							}
					if (const auto Children = DesignJsonUtils::GetArray(Node, TEXT("children")))
						for (const auto& Child : *Children) MoveBounds(Child->AsObject());
				};
				MoveBounds(Copy);
			}
		}
		TransferPlacement(Actual, Copy);
		const auto ActualPlugin = DesignJsonUtils::GetObject(Actual, TEXT("design2umgPluginData"));
		const auto TargetPlugin = DesignJsonUtils::GetObject(Copy, TEXT("design2umgPluginData"));
		if (ActualPlugin && TargetPlugin) TransferPlacement(ActualPlugin, TargetPlugin);
		return true;
	}

	void PlanStateReplacement(const FDesignNode& Start, const FDesignNode& End,
		FUMGPrototypeTransitionData& Transition)
	{
		Transition.bStateInstant = Transition.Type.IsEmpty();
		FUMGPrototypeWidgetAnimationTarget& Outgoing = Transition.WidgetTargets.AddDefaulted_GetRef();
		Outgoing.NodeId = Transition.SourceStateWidgetId;
		Outgoing.DestinationNodeId = Transition.TargetStateWidgetId;
		Outgoing.bEndPresent = false;
		Outgoing.bOpacity = true;
		Outgoing.StartOpacity = Start.Transform.Opacity;
		Outgoing.EndOpacity = 0;
		FUMGPrototypeWidgetAnimationTarget& Incoming = Transition.WidgetTargets.AddDefaulted_GetRef();
		Incoming.NodeId = Transition.TargetStateWidgetId;
		Incoming.bStartPresent = false;
		Incoming.bOpacity = true;
		Incoming.StartOpacity = 0;
		Incoming.EndOpacity = End.Transform.Opacity;
	}

	bool ReadCenterInAncestor(const FDesignNode& Node, const FDesignNode& Ancestor,
		const TMap<const FDesignNode*, const FDesignNode*>& Parents, FVector2D& Out, FString& Error)
	{
		FVector2D Size;
		if (!ReadLocalSize(Node, Size, Error)) return false;
		Out = Size * 0.5;
		const FDesignNode* Current = &Node;
		while (Current && Current != &Ancestor)
		{
			const auto Matrix = DesignJsonUtils::GetArray(Current->Source().Prototype.AnimationSource, TEXT("relativeTransform"));
			if (!Matrix || Matrix->Num() != 2 || (*Matrix)[0]->Type != EJson::Array || (*Matrix)[1]->Type != EJson::Array
				|| (*Matrix)[0]->AsArray().Num() != 3 || (*Matrix)[1]->AsArray().Num() != 3)
			{ Error = TEXT("drag direction requires source-relative transforms to its page or state root"); return false; }
			double M[6];
			for (int32 Row = 0; Row < 2; ++Row)
				for (int32 Column = 0; Column < 3; ++Column)
					if (!(*Matrix)[Row]->AsArray()[Column]->TryGetNumber(M[Row * 3 + Column]) || !FMath::IsFinite(M[Row * 3 + Column]))
					{ Error = TEXT("drag direction requires finite source-relative transforms"); return false; }
			Out = FVector2D(M[0] * Out.X + M[1] * Out.Y + M[2], M[3] * Out.X + M[4] * Out.Y + M[5]);
			Current = Parents.FindRef(Current);
		}
		if (Current != &Ancestor) { Error = TEXT("drag trigger is outside its source page or state"); return false; }
		return true;
	}

	bool EquivalentPaintStacks(const TArray<TSharedPtr<FJsonValue>>* Background,
		const TArray<TSharedPtr<FJsonValue>>* Fills)
	{
		if (!Background || !Fills) return false;
        if (Background->Num() != Fills->Num()) return false;
        for (const auto* Stack : { Background, Fills })
            for (const auto& Value : *Stack)
            {
                const auto Paint = Value->AsObject();
                if (!Paint) return false;
                const FString Type = DesignJsonUtils::GetString(Paint, TEXT("type"));
                if (DesignJsonUtils::GetBool(Paint, TEXT("visible"), true)
                    && Type != TEXT("SOLID") && Type != TEXT("IMAGE")
                    && Type != TEXT("GRADIENT_LINEAR") && Type != TEXT("GRADIENT_RADIAL")
                    && Type != TEXT("GRADIENT_ANGULAR") && Type != TEXT("GRADIENT_DIAMOND")) return false;
            }
        const auto BackgroundPaints = FigmaParserInternal::ParseFills(*Background);
        const auto FillPaints = FigmaParserInternal::ParseFills(*Fills);
        if (BackgroundPaints.Num() != FillPaints.Num()) return false;
        for (int32 Index = 0; Index < BackgroundPaints.Num(); ++Index)
        {
            const auto& A = BackgroundPaints[Index];
            const auto& B = FillPaints[Index];
            // Compare what the paint parser draws, including handle geometry and resolved stop colors.
            if (!FDesignFill::StaticStruct()->CompareScriptStruct(&A, &B, 0)) return false;
        }
		return true;
	}

	bool HasEquivalentPaintAlias(const TSharedPtr<FJsonObject>& Node, const TCHAR* Field)
	{
		const auto Background = DesignJsonUtils::GetArray(Node, Field);
		const auto Fills = DesignJsonUtils::GetArray(Node, TEXT("fills"));
		if (!Fills) return false;
		return !Background ? !Node->HasField(Field) : EquivalentPaintStacks(Background, Fills);
	}

	bool ResolvedVectorNetworkPaintsEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
	{
		const auto A = DesignJsonUtils::GetObject(Left, TEXT("vectorNetwork"));
		const auto B = DesignJsonUtils::GetObject(Right, TEXT("vectorNetwork"));
		FString Error;
		if (!A || !B || !EqualFields(A, B, {TEXT("regions")}, Error)) return false;
		const auto RegionsA = DesignJsonUtils::GetArray(A, TEXT("regions"));
		const auto RegionsB = DesignJsonUtils::GetArray(B, TEXT("regions"));
		if (!RegionsA || !RegionsB || RegionsA->IsEmpty() || RegionsA->Num() != RegionsB->Num()) return false;
		for (int32 Index = 0; Index < RegionsA->Num(); ++Index)
		{
			const auto RegionA = (*RegionsA)[Index]->AsObject();
			const auto RegionB = (*RegionsB)[Index]->AsObject();
			if (!RegionA || !RegionB || !EqualFields(RegionA, RegionB, {TEXT("fills"), TEXT("fillStyleId")}, Error)
				|| !EquivalentPaintStacks(DesignJsonUtils::GetArray(RegionA, TEXT("fills")), DesignJsonUtils::GetArray(Left, TEXT("fills")))
				|| !EquivalentPaintStacks(DesignJsonUtils::GetArray(RegionB, TEXT("fills")), DesignJsonUtils::GetArray(Right, TEXT("fills")))) return false;
		}
		return true;
	}

    struct FNormalizedVectorSegment
    {
        TCHAR Command;
        TArray<double> Coordinates;
    };

    bool NormalizeVectorPath(FString Path, const FVector2D& Size, TArray<FNormalizedVectorSegment>& Out)
    {
        for (const TCHAR* Command : {TEXT("M"), TEXT("L"), TEXT("C"), TEXT("Z")})
            Path.ReplaceInline(Command, *(FString(TEXT(" ")) + Command + TEXT(" ")));
        Path.ReplaceInline(TEXT(","), TEXT(" "));
        TArray<FString> Tokens;
        Path.ParseIntoArrayWS(Tokens);
        TArray<double> ContourStart;
        for (int32 Index = 0; Index < Tokens.Num();)
        {
            const FString& Token = Tokens[Index++];
            if (Token.Len() != 1 || !FCString::Strchr(TEXT("MLCZ"), Token[0])) return false;
            FNormalizedVectorSegment Segment{Token[0], {}};
            const int32 Count = Segment.Command == 'C' ? 6 : Segment.Command == 'Z' ? 0 : 2;
            if (Index + Count > Tokens.Num()) return false;
            for (int32 Coordinate = 0; Coordinate < Count; ++Coordinate)
            {
                double Value;
                if (!LexTryParseString(Value, *Tokens[Index++]) || !FMath::IsFinite(Value)) return false;
                Segment.Coordinates.Add(Value / (Coordinate % 2 == 0 ? Size.X : Size.Y));
            }
            if (Segment.Command == 'M') ContourStart = Segment.Coordinates;
            if (Segment.Command == 'Z' && !Out.IsEmpty() && Out.Last().Command == 'L' && ContourStart.Num() == 2
                && FMath::IsNearlyEqual(Out.Last().Coordinates[0], ContourStart[0], 0.00001)
                && FMath::IsNearlyEqual(Out.Last().Coordinates[1], ContourStart[1], 0.00001)) Out.Pop();
            Out.Add(MoveTemp(Segment));
        }
        return !Out.IsEmpty();
    }

    bool NormalizedVectorPathEqual(const FString& Left, const FString& Right, const FVector2D& StartSize, const FVector2D& EndSize)
    {
        TArray<FNormalizedVectorSegment> A, B;
        if (!NormalizeVectorPath(Left, StartSize, A) || !NormalizeVectorPath(Right, EndSize, B) || A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (A[Index].Command != B[Index].Command || A[Index].Coordinates.Num() != B[Index].Coordinates.Num()) return false;
            for (int32 Coordinate = 0; Coordinate < A[Index].Coordinates.Num(); ++Coordinate)
                // Figma's SVG export rounds coordinates to about six significant digits.
                if (!FMath::IsNearlyEqual(A[Index].Coordinates[Coordinate], B[Index].Coordinates[Coordinate], 0.00001)) return false;
        }
        return true;
    }

	bool HasNativeBoxContour(const FDesignNode& Node, const FVector2D& Size)
	{
		const auto& Paths = Node.Shape.PaintGeometry.FillPaths;
		return Paths.IsSet() && Paths->Num() == 1 && (*Paths)[0].FillRule.Equals(TEXT("nonzero"), ESearchCase::IgnoreCase)
			&& NormalizedVectorPathEqual((*Paths)[0].Path, TEXT("M0 0L1 0L1 1L0 1Z"), Size, FVector2D(1, 1));
	}

    bool NormalizedVectorValueEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B,
        const FVector2D& StartSize, const FVector2D& EndSize, bool bPaths, bool bCoordinates = false)
    {
        if (!A || !B || A->Type != B->Type) return false;
        if (A->Type == EJson::Array)
        {
            const auto& Left = A->AsArray(); const auto& Right = B->AsArray();
            if (Left.Num() != Right.Num()) return false;
            for (int32 Index = 0; Index < Left.Num(); ++Index)
                if (!NormalizedVectorValueEqual(Left[Index], Right[Index], StartSize, EndSize, bPaths, bCoordinates)) return false;
            return true;
        }
        if (A->Type == EJson::Object)
        {
            const auto Left = A->AsObject(); const auto Right = B->AsObject();
            if (Left->Values.Num() != Right->Values.Num()) return false;
            for (const auto& Field : Left->Values)
            {
                const auto Other = Right->TryGetField(Field.Key);
                if (!Other || Field.Value->Type != Other->Type) return false;
                if (bPaths && (Field.Key == TEXT("path") || Field.Key == TEXT("data")))
                {
                    if (Field.Value->Type != EJson::String || !NormalizedVectorPathEqual(Field.Value->AsString(), Other->AsString(), StartSize, EndSize)) return false;
                }
                else if (bCoordinates && (Field.Key == TEXT("x") || Field.Key == TEXT("y")))
                {
                    if (Field.Value->Type != EJson::Number) return false;
                    const bool bX = Field.Key == TEXT("x");
                    if (!FMath::IsNearlyEqual(Field.Value->AsNumber() / (bX ? StartSize.X : StartSize.Y),
                        Other->AsNumber() / (bX ? EndSize.X : EndSize.Y), UE_DOUBLE_SMALL_NUMBER)) return false;
                }
                else if (!NormalizedVectorValueEqual(Field.Value, Other, StartSize, EndSize, bPaths,
                    Field.Key == TEXT("vertices") || Field.Key == TEXT("tangentStart") || Field.Key == TEXT("tangentEnd"))) return false;
            }
            return true;
        }
        return FJsonValue::CompareEqual(*A, *B);
    }

	bool VectorStrokeMirrorEqual(const TSharedPtr<FJsonValue>& Canonical, const TSharedPtr<FJsonValue>& Mirror)
	{
		if (!Canonical || !Mirror || Canonical->Type != EJson::Array || Mirror->Type != EJson::Array) return false;
		const auto& Paths = Canonical->AsArray(); const auto& Copies = Mirror->AsArray();
		if (Paths.Num() != Copies.Num()) return false;
		for (int32 Index = 0; Index < Paths.Num(); ++Index)
		{
			const auto Path = Paths[Index]->AsObject(), Copy = Copies[Index]->AsObject();
			if (!Path || !Copy) return false;
			for (const auto& Object : {Path, Copy})
				if (Object->HasField(TEXT("path")) == Object->HasField(TEXT("data"))
					|| (Object->HasField(TEXT("windingRule")) && Object->TryGetField(TEXT("windingRule"))->Type != EJson::String)) return false;
			for (const auto& Object : {Path, Copy})
				for (const auto& Field : Object->Values)
					if (Field.Key != TEXT("path") && Field.Key != TEXT("data") && Field.Key != TEXT("windingRule")) return false;
			const auto ReadPath = [](const TSharedPtr<FJsonObject>& Object)
			{
				return DesignJsonUtils::GetString(Object, Object->HasField(TEXT("path")) ? TEXT("path") : TEXT("data"));
			};
			const FString Outline = ReadPath(Path);
			if (Outline.IsEmpty() || Outline != ReadPath(Copy)
				|| DesignJsonUtils::GetString(Path, TEXT("windingRule")) != DesignJsonUtils::GetString(Copy, TEXT("windingRule"))) return false;
		}
		return true;
	}


	bool NormalizedVectorCenterEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right,
		const FVector2D& StartSize, const FVector2D& EndSize,
		const TSharedPtr<FJsonObject>& LeftPaints, const TSharedPtr<FJsonObject>& RightPaints)
	{
		bool bHasCenter = false;
		for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("vectorPaths")})
		{
			const auto A = Left->TryGetField(Field), B = Right->TryGetField(Field);
			if (!A && !B) continue;
			if (!A || !B || !NormalizedVectorValueEqual(A, B, StartSize, EndSize, true)) return false;
			bHasCenter |= A->Type == EJson::Array && !A->AsArray().IsEmpty();
		}
		const auto A = DesignJsonUtils::GetObject(Left, TEXT("vectorNetwork"));
		const auto B = DesignJsonUtils::GetObject(Right, TEXT("vectorNetwork"));
		if (!A && !B) return bHasCenter;
		if (!A || !B || A->Values.Num() != B->Values.Num()) return false;
		for (const auto& Field : A->Values)
		{
			if (Field.Key == TEXT("regions")) continue;
			const auto Other = B->TryGetField(Field.Key);
			if (!Other || !NormalizedVectorValueEqual(Field.Value, Other, StartSize, EndSize, false, Field.Key == TEXT("vertices"))) return false;
		}
		const auto RegionsA = DesignJsonUtils::GetArray(A, TEXT("regions"));
		const auto RegionsB = DesignJsonUtils::GetArray(B, TEXT("regions"));
		if (!RegionsA || !RegionsB || RegionsA->Num() != RegionsB->Num()) return false;
		for (int32 Index = 0; Index < RegionsA->Num(); ++Index)
		{
			const auto RegionA = (*RegionsA)[Index]->AsObject(), RegionB = (*RegionsB)[Index]->AsObject();
			FString Error;
			if (!RegionA || !RegionB || !EqualFields(RegionA, RegionB, {TEXT("fills"), TEXT("fillStyleId")}, Error)
				|| !EquivalentPaintStacks(DesignJsonUtils::GetArray(RegionA, TEXT("fills")), DesignJsonUtils::GetArray(LeftPaints, TEXT("fills")))
				|| !EquivalentPaintStacks(DesignJsonUtils::GetArray(RegionB, TEXT("fills")), DesignJsonUtils::GetArray(RightPaints, TEXT("fills")))) return false;
		}
		const auto Vertices = DesignJsonUtils::GetArray(A, TEXT("vertices"));
		return bHasCenter || (Vertices && !Vertices->IsEmpty());
	}

    bool NormalizedVectorGeometryEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right,
        const FVector2D& StartSize, const FVector2D& EndSize)
    {
        bool bHasGeometry = false;
        for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")})
        {
            const auto A = Left->TryGetField(Field), B = Right->TryGetField(Field);
            if (!A && !B) continue;
            if (!A || !B) return false;
            if (FCString::Strcmp(Field, TEXT("vectorNetwork")) == 0)
            {
                if (!NormalizedVectorCenterEqual(Left, Right, StartSize, EndSize, Left, Right)) return false;
            }
            else if (!NormalizedVectorValueEqual(A, B, StartSize, EndSize, true)) return false;
            bHasGeometry |= A->Type == EJson::Object || (A->Type == EJson::Array && !A->AsArray().IsEmpty());
        }
        return bHasGeometry;
    }


	bool MatchTree(const FDesignNode& Start, const FDesignNode& End, const FDesignNode& RuntimeEnd, bool bRoot,
		bool bDestinationPage, bool bSamePageSize, TArray<FUMGPrototypeWidgetAnimationTarget>& Targets,
		TArray<FUMGPrototypeLayerOrder>& LayerOrders, TArray<FUMGPrototypeAddedLayer>& AddedLayers,
		const FString& InstanceScopeId, const TSharedPtr<FJsonObject>& InstanceProperties, bool bSelfAlignmentAffectsLayout,
		bool& bFixedViewportLayout, TArray<FUMGPrototypeLayoutTarget>& LayoutTargets, TArray<FUMGPrototypeLocalEndpoint>& LocalEndpoints, TArray<FUMGPrototypeSharedCapture>& SharedCaptures, FString& Error)
	{
		const auto Left = Start.Source().Prototype.AnimationSource;
		const auto Right = End.Source().Prototype.AnimationSource;
		if (!Left || !Right)
		{
			Error = TEXT("missing Figma source node fields");
			return false;
		}
		LayoutTargets.Add({Start.Id, RuntimeEnd.Id, FString()});
		const auto ShadowEffects = [](const TSharedPtr<FJsonObject>& Node)
		{
			TArray<TSharedPtr<FJsonValue>> Shadows;
			const auto Effects = DesignJsonUtils::GetArray(Node, TEXT("effects"));
			if (Effects) for (const auto& Value : *Effects)
			{
				const auto Effect = Value->AsObject();
				const FString Type = DesignJsonUtils::GetString(Effect, TEXT("type"));
				if (DesignJsonUtils::GetBool(Effect, TEXT("visible"), true)
					&& (Type == TEXT("DROP_SHADOW") || Type == TEXT("INNER_SHADOW"))) Shadows.Add(Value);
			}
			return MakeShared<FJsonValueArray>(Shadows);
		};
		const bool bShadowChange = !FJsonValue::CompareEqual(*ShadowEffects(Left), *ShadowEffects(Right));
		const bool bLeaf = Start.Children.IsEmpty() && End.Children.IsEmpty();
		const auto ConsumeShadowEffects = [&](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, TSet<FString>& Consumed)
		{
			if (!bShadowChange) return true;
			const auto OtherEffects = [](const TSharedPtr<FJsonObject>& Node)
			{
				TArray<TSharedPtr<FJsonValue>> Other;
				if (const auto Effects = DesignJsonUtils::GetArray(Node, TEXT("effects"))) for (const auto& Value : *Effects)
				{
					const FString Type = DesignJsonUtils::GetString(Value->AsObject(), TEXT("type"));
					if (Type != TEXT("DROP_SHADOW") && Type != TEXT("INNER_SHADOW")) Other.Add(Value);
				}
				return MakeShared<FJsonValueArray>(Other);
			};
			if (!FJsonValue::CompareEqual(*OtherEffects(A), *OtherEffects(B)))
			{ Error = TEXT("unsupported non-shadow effect change"); return false; }
			Consumed.Add(TEXT("effects"));
			return true;
		};
		const FString LeftType = DesignJsonUtils::GetString(Left, TEXT("type"));
		const FString RightType = DesignJsonUtils::GetString(Right, TEXT("type"));
		const bool bComponentPair = bRoot && (LeftType == TEXT("INSTANCE") || LeftType == TEXT("COMPONENT"))
			&& (RightType == TEXT("INSTANCE") || RightType == TEXT("COMPONENT"));
		if (!bComponentPair && LeftType != RightType)
		{
			Error = TEXT("different node types cannot be property-animated");
			return false;
		}
		TSet<FString> Excluded = { TEXT("id"), TEXT("name"), TEXT("type"), TEXT("locked"), TEXT("absoluteBoundingBox"),
			TEXT("absoluteRenderBounds"), TEXT("relativeTransform"), TEXT("size"), TEXT("rotation"), TEXT("opacity"),
			TEXT("fills"), TEXT("interactions"), TEXT("componentId"), TEXT("componentSetId"), TEXT("componentSetKey"),
			TEXT("mainComponentKey"), TEXT("componentProperties"), TEXT("variantProperties"), TEXT("overrides"),
			TEXT("componentPropertyDefinitions"), TEXT("variantGroupProperties"), TEXT("styles"),
			TEXT("transitionNodeID"), TEXT("transitionDuration"), TEXT("transitionEasing"),
			TEXT("design2umgPluginData"), TEXT("animationStyles"), TEXT("animations"), TEXT("manualKeyframeTracks"), TEXT("timelines") };
		if (bDestinationPage && bSamePageSize) Excluded.Add(TEXT("constraints"));
		if (!bSelfAlignmentAffectsLayout) Excluded.Add(TEXT("layoutAlign"));
		TSet<FString> LayoutFields = { TEXT("layoutMode"), TEXT("primaryAxisSizingMode"), TEXT("counterAxisSizingMode"),
			TEXT("primaryAxisAlignItems"), TEXT("counterAxisAlignItems"), TEXT("counterAxisAlignContent"), TEXT("layoutSizingHorizontal"),
			TEXT("layoutSizingVertical"), TEXT("layoutGrow"), TEXT("itemSpacing"), TEXT("counterAxisSpacing"), TEXT("paddingLeft"),
			TEXT("paddingRight"), TEXT("paddingTop"), TEXT("paddingBottom"), TEXT("layoutWrap"), TEXT("layoutPositioning") };
		if (bSelfAlignmentAffectsLayout) LayoutFields.Add(TEXT("layoutAlign"));
		const auto ConsumeLayout = [&](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, TSet<FString>& Consumed)
		{
			for (const FString& Field : LayoutFields)
			{
				const auto X = A->TryGetField(Field), Y = B->TryGetField(Field);
				if ((X || Y) && (!X || !Y || !FJsonValue::CompareEqual(*X, *Y))) bFixedViewportLayout = true;
				Consumed.Add(Field);
			}
			if (Start.LayoutData.bReversePaintOrder || End.LayoutData.bReversePaintOrder)
				Consumed.Add(TEXT("itemReverseZIndex"));
		};
		ConsumeLayout(Left, Right, Excluded);
		if (!ConsumeShadowEffects(Left, Right, Excluded)) return false;
		const auto FieldDiffers = [&](const TCHAR* Name)
		{
			const auto A = Left->TryGetField(Name);
			const auto B = Right->TryGetField(Name);
			return (A || B) && (!A || !B || !FJsonValue::CompareEqual(*A, *B));
		};
		FVector2D StartSize, EndSize;
		if (!ReadLocalSize(Start, StartSize, Error) || !ReadLocalSize(End, EndSize, Error)) return false;
		const bool bResize = !StartSize.Equals(EndSize);
		// Rectangular surface bounds and corners own their native endpoint contour.
		const auto HasUnsmoothedCorners = [](const TSharedPtr<FJsonObject>& Node)
		{
			return FMath::IsNearlyZero(DesignJsonUtils::GetNumber(Node, TEXT("cornerSmoothing")))
				&& FMath::IsNearlyZero(DesignJsonUtils::GetNumber(DesignJsonUtils::GetObject(Node, TEXT("design2umgPluginData")), TEXT("cornerSmoothing")));
		};
		const bool bRoundedSurfacePair = (Start.Type == EDesignNodeType::Frame || Start.Type == EDesignNodeType::Rectangle) && Start.Type == End.Type;
		const bool bEmptyGroupCorners = Start.Type == EDesignNodeType::Group && End.Type == EDesignNodeType::Group
			&& Start.Visual.Fills.IsEmpty() && End.Visual.Fills.IsEmpty()
			&& !Start.Visual.Stroke.HasStroke() && !End.Visual.Stroke.HasStroke()
			&& Start.Visual.Effects.IsEmpty() && End.Visual.Effects.IsEmpty()
			&& !Start.LayoutData.ExplicitClip.Get(false) && !End.LayoutData.ExplicitClip.Get(false)
			&& Start.Visual.CornerRadius.ToVector4() == FVector4(0, 0, 0, 0) && End.Visual.CornerRadius.ToVector4() == FVector4(0, 0, 0, 0)
			&& Start.Visual.CornerRadius.Smoothing == 0 && End.Visual.CornerRadius.Smoothing == 0;
		const auto ConsumeEmptyGroupCorners = [&](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, TSet<FString>& Consumed)
		{
			if (!bEmptyGroupCorners || !A || !B) return;
			for (const TCHAR* Field : {TEXT("cornerRadius"), TEXT("rectangleCornerRadii"), TEXT("cornerSmoothing")})
			{
				bool bZero = true;
				for (const auto& Object : {A, B})
				{
					const auto Value = Object->TryGetField(Field);
					if (!Value) continue;
					if (FCString::Strcmp(Field, TEXT("rectangleCornerRadii")) == 0)
					{
						if (Value->Type != EJson::Array || Value->AsArray().Num() != 4) { bZero = false; break; }
						for (const auto& Radius : Value->AsArray())
						{ double Number; if (!Radius->TryGetNumber(Number) || Number != 0) bZero = false; }
					}
					else { double Number; if (!Value->TryGetNumber(Number) || Number != 0) bZero = false; }
				}
				if (bZero) Consumed.Add(Field);
			}
		};
		ConsumeEmptyGroupCorners(Left, Right, Excluded);
		const bool bSmoothingChange = bRoundedSurfacePair && Start.Visual.CornerRadius.Smoothing != End.Visual.CornerRadius.Smoothing;
		const bool bClipChange = bRoundedSurfacePair && Start.LayoutData.ExplicitClip.Get(false) != End.LayoutData.ExplicitClip.Get(false);
		if (bRoundedSurfacePair)
			for (const FDesignNode* Surface : {&Start, &End})
				if (VisualMapper::RequiresSourceContour(*Surface, true))
				{
					FUMGEffectData Contour;
					if (!VisualMapper::BuildSourceContour(*Surface, Contour, Error)) return false;
				}

		const bool bSurfaceCornerChange = bRoundedSurfacePair
			&& !Start.Visual.CornerRadius.ToVector4().Equals(End.Visual.CornerRadius.ToVector4());
		const bool bRoundedSurfaceResize = bRoundedSurfacePair && bResize
			&& (Start.Visual.CornerRadius.HasRadius() || End.Visual.CornerRadius.HasRadius());
		const bool bSurfaceFillContour = bRoundedSurfacePair
			&& ((LeftType == TEXT("FRAME") && RightType == TEXT("FRAME") && HasUnsmoothedCorners(Left) && HasUnsmoothedCorners(Right)
					&& !VisualMapper::RequiresSourceContour(Start, true) && !VisualMapper::RequiresSourceContour(End, true))
				|| (Start.Type == EDesignNodeType::Frame && End.Type == EDesignNodeType::Frame
					&& HasUnsmoothedCorners(Left) && HasUnsmoothedCorners(Right)
					&& HasNativeBoxContour(Start, StartSize) && HasNativeBoxContour(End, EndSize)
					&& NormalizedVectorGeometryEqual(Left, Right, StartSize, EndSize))
				|| bSurfaceCornerChange || bRoundedSurfaceResize || bSmoothingChange || bClipChange);
		const auto LeftPlugin = DesignJsonUtils::GetObject(Left, TEXT("design2umgPluginData"));
		const auto RightPlugin = DesignJsonUtils::GetObject(Right, TEXT("design2umgPluginData"));
		const auto LeftPluginContour = LeftPlugin ? LeftPlugin->TryGetField(TEXT("fillGeometry")) : nullptr;
		const auto RightPluginContour = RightPlugin ? RightPlugin->TryGetField(TEXT("fillGeometry")) : nullptr;
		const bool bPluginContourChange = (LeftPluginContour || RightPluginContour)
			&& (!LeftPluginContour || !RightPluginContour || !FJsonValue::CompareEqual(*LeftPluginContour, *RightPluginContour));
		const bool bSurfaceEndpointChange = bSurfaceCornerChange || bSmoothingChange || bClipChange
			|| (bRoundedSurfaceResize && (VisualMapper::RequiresSourceContour(Start, true) || VisualMapper::RequiresSourceContour(End, true)
				|| FieldDiffers(TEXT("fillGeometry")) || bPluginContourChange));
		const auto ConsumeCornerMirrors = [&](const TSharedPtr<FJsonObject>& Canonical, const TSharedPtr<FJsonObject>& Fields,
			const TSharedPtr<FJsonObject>& OtherFields, const FDesignNode& Node, TSet<FString>& Consumed)
		{
			if (!bRoundedSurfacePair || !Fields) return true;
			const auto CanonicalCorners = DesignJsonUtils::GetArray(Canonical, TEXT("rectangleCornerRadii"));
			double Uniform = 0;
			const bool bUniform = Canonical->TryGetNumberField(TEXT("cornerRadius"), Uniform);
			const FVector4 Typed = Node.Visual.CornerRadius.ToVector4();
			const TCHAR* Names[] = {TEXT("topLeftRadius"), TEXT("topRightRadius"), TEXT("bottomRightRadius"), TEXT("bottomLeftRadius")};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const auto Value = Fields->TryGetField(Names[Index]);
				const auto OtherValue = OtherFields ? OtherFields->TryGetField(Names[Index]) : nullptr;
				if (Value && OtherValue && FJsonValue::CompareEqual(*Value, *OtherValue)) continue;
				double Mirror = 0, Radius = Uniform;
				if (!Fields->HasField(Names[Index])) continue;
				if (!Fields->TryGetNumberField(Names[Index], Mirror))
				{ Error = FString::Printf(TEXT("invalid rounded surface corner mirror '%s'"), Names[Index]); return false; }
				const bool bCanonical = CanonicalCorners && CanonicalCorners->Num() == 4
					? (*CanonicalCorners)[Index]->TryGetNumber(Radius) : bUniform;
				if (!bCanonical || static_cast<float>(Mirror) != static_cast<float>(Radius)
					|| static_cast<float>(Radius) != static_cast<float>(Typed[Index]))
				{ Error = FString::Printf(TEXT("rounded surface corner mirror '%s' has no matching canonical native corner"), Names[Index]); return false; }
				Consumed.Add(Names[Index]);
			}
			return true;
		};
		TSet<FString> LeftCornerMirrors, RightCornerMirrors;
		if (!ConsumeCornerMirrors(Left, Left, Right, Start, LeftCornerMirrors)
			|| !ConsumeCornerMirrors(Right, Right, Left, End, RightCornerMirrors)) return false;
		for (const FString& Field : LeftCornerMirrors)
			if (!Right->HasField(Field) || RightCornerMirrors.Contains(Field)) Excluded.Add(Field);
		for (const FString& Field : RightCornerMirrors)
			if (!Left->HasField(Field) || LeftCornerMirrors.Contains(Field)) Excluded.Add(Field);
		if (bRoundedSurfacePair)
		{
			Excluded.Add(TEXT("cornerSmoothing"));
			Excluded.Add(TEXT("clipsContent"));
		}
		if (bSurfaceFillContour) Excluded.Add(TEXT("fillGeometry"));
		if (bSurfaceCornerChange)
			for (const TCHAR* Field : {TEXT("cornerRadius"), TEXT("rectangleCornerRadii")}) Excluded.Add(Field);
        const auto ConsumePaintAliases = [](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, TSet<FString>& Consumed)
        {
            if (!DesignJsonUtils::GetArray(A, TEXT("fills")) || !DesignJsonUtils::GetArray(B, TEXT("fills"))) return;
            // Resolved fills own painting; these deprecated color/style fields are not parser inputs.
            for (const TCHAR* Field : { TEXT("backgroundColor"), TEXT("fillStyleId"), TEXT("backgroundStyleId") }) Consumed.Add(Field);
            for (const TCHAR* Field : { TEXT("background"), TEXT("backgrounds") })
                if (HasEquivalentPaintAlias(A, Field) && HasEquivalentPaintAlias(B, Field)) Consumed.Add(Field);
        };
        ConsumePaintAliases(Left, Right, Excluded);
		const bool bSingleSolid = Start.Visual.Fills.Num() == 1 && End.Visual.Fills.Num() == 1
			&& Start.Visual.Fills[0].IsColor() && End.Visual.Fills[0].IsColor();
		const bool bText = LeftType == TEXT("TEXT");
		bool bFillsChanged = false;
		// Native contour coverage owns its fill; a baked contour cannot consume a bare brush tint.
		const bool bNativeCoveragePaint = bSingleSolid && !Start.Visual.Stroke.HasStroke() && !End.Visual.Stroke.HasStroke()
			&& (!Start.Shape.PaintGeometry.StrokePaths.IsSet() || Start.Shape.PaintGeometry.StrokePaths->IsEmpty())
			&& (!End.Shape.PaintGeometry.StrokePaths.IsSet() || End.Shape.PaintGeometry.StrokePaths->IsEmpty())
			&& (VisualMapper::RequiresSourceContour(Start, true) || VisualMapper::RequiresSourceContour(End, true)
				|| ((Start.Type == EDesignNodeType::Path || Start.Type == EDesignNodeType::Polygon)
					&& Start.Shape.PaintGeometry.FillPaths.IsSet() && !Start.Shape.PaintGeometry.FillPaths->IsEmpty()
					&& End.Shape.PaintGeometry.FillPaths.IsSet() && !End.Shape.PaintGeometry.FillPaths->IsEmpty()));
		const bool bSurfacePaint = !bText && (!bSingleSolid || bNativeCoveragePaint);
		if (bSurfacePaint && !bNativeCoveragePaint && (Start.Type == EDesignNodeType::Path || Start.Type == EDesignNodeType::Polygon || Start.Type == EDesignNodeType::Line))
		{
			const auto A = Left->TryGetField(TEXT("fills")), B = Right->TryGetField(TEXT("fills"));
			if ((A || B) && (!A || !B || !FJsonValue::CompareEqual(*A, *B)))
			{ Error = TEXT("animated multi-paint vector coverage is not represented by the native rectangle/ellipse surface"); return false; }
		}
		if (!((bText || bSurfacePaint) ? MatchSurfacePaints(Left, Right, bText, bFillsChanged, Error)
			: MatchFills(Left, Right, bSingleSolid, bFillsChanged, Error))) return false;
		bool bTextAppearanceChange = bText && !FMath::IsNearlyEqual(Start.Text.LineHeight, End.Text.LineHeight);
		if (bText)
		{
			const auto LeftStyle = DesignJsonUtils::GetObject(Left, TEXT("style"));
			const auto RightStyle = DesignJsonUtils::GetObject(Right, TEXT("style"));
			if (!LeftStyle || !RightStyle)
			{
				if (LeftStyle || RightStyle) { Error = TEXT("text style is missing from one endpoint"); return false; }
			}
			else
			{
				TSet<FString> StyleConsumed = { TEXT("fontSize"), TEXT("letterSpacing"), TEXT("lineHeightPx") };
				for (const TCHAR* Field : {TEXT("fontFamily"), TEXT("fontStyle"), TEXT("fontPostScriptName"), TEXT("fontWeight"), TEXT("textAlignHorizontal"), TEXT("textAlignVertical"),
					TEXT("lineHeightUnit"), TEXT("lineHeightPercent"), TEXT("lineHeightPercentFontSize")})
				{
					const auto A = LeftStyle->TryGetField(Field), B = RightStyle->TryGetField(Field);
					bTextAppearanceChange |= (A || B) && (!A || !B || !FJsonValue::CompareEqual(*A, *B));
					StyleConsumed.Add(Field);
				}
				if (!EqualFields(LeftStyle, RightStyle, StyleConsumed, Error)) return false;
			}
			const double LeftFont = DesignJsonUtils::GetNumber(LeftStyle, TEXT("fontSize"));
			const double RightFont = DesignJsonUtils::GetNumber(RightStyle, TEXT("fontSize"));
			const double LeftLine = DesignJsonUtils::GetNumber(LeftStyle, TEXT("lineHeightPx"));
			const double RightLine = DesignJsonUtils::GetNumber(RightStyle, TEXT("lineHeightPx"));
			if (LeftStyle && (LeftFont <= 0 || RightFont <= 0 || (!bTextAppearanceChange && !FMath::IsNearlyEqual(LeftLine / LeftFont, RightLine / RightFont, 0.001))))
			{ Error = TEXT("text line-height is not preserved by the native font change"); return false; }
			Excluded.Add(TEXT("style"));
			Excluded.Add(TEXT("fillGeometry"));
			Excluded.Add(TEXT("strokeGeometry"));
		}
		const bool bTextOverride = LeftType == TEXT("TEXT") && HasInstanceOverride(Left, Right, InstanceProperties, TEXT("characters"));
		const bool bEquivalentVectorGeometry = LeftType == TEXT("VECTOR")
			&& StartSize.X > 0 && StartSize.Y > 0 && EndSize.X > 0 && EndSize.Y > 0
			&& NormalizedVectorGeometryEqual(Left, Right, StartSize, EndSize);
		if (bEquivalentVectorGeometry)
			for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")}) Excluded.Add(Field);
        const auto HasNativeStrokePaint = [](const FDesignNode& Node, const TSharedPtr<FJsonObject>& Raw)
        {
            const auto& Stroke = Node.Visual.Stroke;
            if (!Stroke.HasStroke() || !Stroke.Fill.IsSet() || !Stroke.Fill->bEnabled
                || (!Stroke.Fill->BlendMode.IsEmpty() && Stroke.Fill->BlendMode != TEXT("NORMAL"))) return false;
            const auto Paints = DesignJsonUtils::GetArray(Raw, TEXT("strokes"));
            if (!Paints) return false;
            int32 VisibleCount = 0;
            for (const auto& Paint : *Paints)
                VisibleCount += DesignJsonUtils::GetBool(Paint->AsObject(), TEXT("visible"), true) ? 1 : 0;
            return VisibleCount == 1 && (Stroke.Fill->IsColor()
                || (Stroke.Fill->Type == EDesignFillType::Gradient && Stroke.Fill->Gradient.GradientType == EDesignGradientType::Linear));
        };
        const bool bStrokePaintChanged = FieldDiffers(TEXT("strokes")) && !bResize
            && !FieldDiffers(TEXT("fillGeometry")) && !FieldDiffers(TEXT("strokeGeometry"))
            && !FieldDiffers(TEXT("vectorPaths")) && !FieldDiffers(TEXT("vectorNetwork"))
            && HasNativeStrokePaint(Start, Left) && HasNativeStrokePaint(End, Right);
		const auto IsNativeStrokeEndpoint = [&](const FDesignNode& Node, const TSharedPtr<FJsonObject>& Raw)
		{
			const auto Paints = DesignJsonUtils::GetArray(Raw, TEXT("strokes"));
			if (!Paints) return false;
			const bool bVisible = Paints->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Paint)
			{ return DesignJsonUtils::GetBool(Paint->AsObject(), TEXT("visible"), true); });
			return bVisible ? HasNativeStrokePaint(Node, Raw) && Node.Visual.Stroke.Thickness.bUniform
				: !Node.Visual.Stroke.HasStroke();
		};
		const bool bStrokePresenceChanged = bRoundedSurfacePair
			&& (Start.Visual.Stroke.HasStroke() != End.Visual.Stroke.HasStroke())
			&& IsNativeStrokeEndpoint(Start, Left) && IsNativeStrokeEndpoint(End, Right)
			&& (bSurfaceFillContour || !FieldDiffers(TEXT("fillGeometry")))
			&& !FieldDiffers(TEXT("vectorPaths")) && !FieldDiffers(TEXT("vectorNetwork"));
		const auto HasSupportedVectorStroke = [&](const FDesignNode& Node, const TSharedPtr<FJsonObject>& Raw)
		{
			const auto SupportedFields = [](const TSharedPtr<FJsonObject>& Fields)
			{
				if (!Fields) return true;
				const FString Cap = DesignJsonUtils::GetString(Fields, TEXT("strokeCap"), TEXT("NONE"));
				const FString Join = DesignJsonUtils::GetString(Fields, TEXT("strokeJoin"), TEXT("MITER"));
				const FString Align = DesignJsonUtils::GetString(Fields, TEXT("strokeAlign"), TEXT("INSIDE"));
				const auto Dashes = DesignJsonUtils::GetArray(Fields, TEXT("dashPattern"));
				return (!Fields->HasField(TEXT("dashPattern")) || (Dashes && Dashes->IsEmpty()))
					&& (Cap == TEXT("NONE") || Cap == TEXT("ROUND") || Cap == TEXT("SQUARE"))
					&& (Join == TEXT("MITER") || Join == TEXT("ROUND") || Join == TEXT("BEVEL"))
					&& (Align == TEXT("CENTER") || Align == TEXT("INSIDE") || Align == TEXT("OUTSIDE"));
			};
			return HasNativeStrokePaint(Node, Raw) && Node.Visual.Stroke.Thickness.bUniform
				&& Node.Visual.Stroke.DashPattern.IsEmpty() && Node.Shape.PaintGeometry.StrokePaths.IsSet()
				&& !Node.Shape.PaintGeometry.StrokePaths->IsEmpty() && SupportedFields(Raw)
				&& SupportedFields(DesignJsonUtils::GetObject(Raw, TEXT("design2umgPluginData")));
		};
		const bool bFixedVectorEndpoint = LeftType == TEXT("VECTOR") && RightType == TEXT("VECTOR") && bResize
			&& StartSize.X > 0 && StartSize.Y > 0 && EndSize.X > 0 && EndSize.Y > 0
			&& HasSupportedVectorStroke(Start, Left) && HasSupportedVectorStroke(End, Right)
			&& Start.Visual.Stroke.Thickness.Uniform == End.Visual.Stroke.Thickness.Uniform
			&& NormalizedVectorCenterEqual(Left, Right, StartSize, EndSize, Left, Right);
		if (bFixedVectorEndpoint)
			for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")}) Excluded.Add(Field);
		const bool bStrokeSizeEndpoint = bRoundedSurfacePair && bResize
			&& HasNativeStrokePaint(Start, Left) && HasNativeStrokePaint(End, Right)
			&& Start.Visual.Stroke.Thickness.bUniform && End.Visual.Stroke.Thickness.bUniform
			&& (bSurfaceFillContour || !FieldDiffers(TEXT("fillGeometry")))
			&& !FieldDiffers(TEXT("vectorPaths")) && !FieldDiffers(TEXT("vectorNetwork"));
		const bool bStrokeEndpointChanged = bStrokePaintChanged || bStrokePresenceChanged || bStrokeSizeEndpoint || bFixedVectorEndpoint;
		if (bStrokeEndpointChanged)
		{
			Excluded.Add(TEXT("strokes"));
			Excluded.Add(TEXT("strokeStyleId"));
		}
		if (bStrokePresenceChanged || bStrokeSizeEndpoint || bFixedVectorEndpoint)
			for (const TCHAR* Field : {TEXT("strokeWeight"), TEXT("strokeGeometry")}) Excluded.Add(Field);
		const bool bGeometryReplacement = !bStrokeEndpointChanged && !bEquivalentVectorGeometry && !bRoot && !bDestinationPage && !bText && !bTextOverride && !InstanceScopeId.IsEmpty()
			&& Start.Children.IsEmpty() && End.Children.IsEmpty()
			&& ((!bSurfaceFillContour && FieldDiffers(TEXT("fillGeometry"))) || FieldDiffers(TEXT("strokeGeometry")));
		if (bGeometryReplacement)
		{
			Excluded.Add(TEXT("fillGeometry"));
			Excluded.Add(TEXT("strokeGeometry"));
		}
		if (bComponentPair || LeftType == TEXT("INSTANCE")) Excluded.Add(TEXT("exposedInstances"));
		if (bTextOverride)
		{
			Excluded.Add(TEXT("characters"));
			Excluded.Add(TEXT("fillGeometry"));
			Excluded.Add(TEXT("strokeGeometry"));
		}
		if (HasInstanceOverride(Left, Right, InstanceProperties, TEXT("visible"))) Excluded.Add(TEXT("visible"));
		if (bFillsChanged && EqualObjectExcept(Left, Right, TEXT("boundVariables"), TEXT("fills"))) Excluded.Add(TEXT("boundVariables"));
		if (!MatchInactiveMotion(Left, Right, Error) || !EqualFields(Left, Right, Excluded, Error)) return false;
		if (LeftPlugin || RightPlugin)
		{
			if (!LeftPlugin || !RightPlugin)
			{
				Error = TEXT("missing paired Figma plugin source data");
				return false;
			}
			// Identity, actions and local geometry already have explicit consumers; other raw properties must match.
			TSet<FString> PluginExcluded = { TEXT("id"), TEXT("name"), TEXT("type"), TEXT("isAsset"), TEXT("locked"), TEXT("reactions"),
				TEXT("absoluteBoundingBox"), TEXT("absoluteRenderBounds"), TEXT("absoluteTransform"), TEXT("relativeTransform"),
				TEXT("size"), TEXT("x"), TEXT("y"), TEXT("width"), TEXT("height"), TEXT("rotation"), TEXT("opacity"),
				TEXT("fills"), TEXT("animationStyles"), TEXT("animations"), TEXT("manualKeyframeTracks"), TEXT("timelines") };
			// This is a Dev Mode layout suggestion; actual layoutMode and layout fields remain compared.
			PluginExcluded.Add(TEXT("inferredAutoLayout"));
			if (!bSelfAlignmentAffectsLayout) PluginExcluded.Add(TEXT("layoutAlign"));
			ConsumeLayout(LeftPlugin, RightPlugin, PluginExcluded);
			ConsumeEmptyGroupCorners(LeftPlugin, RightPlugin, PluginExcluded);
			if (!ConsumeShadowEffects(LeftPlugin, RightPlugin, PluginExcluded)) return false;
            ConsumePaintAliases(LeftPlugin, RightPlugin, PluginExcluded);
            if (bSurfaceFillContour) PluginExcluded.Add(TEXT("fillGeometry"));
			if (bRoundedSurfacePair)
			{
				if (static_cast<float>(DesignJsonUtils::GetNumber(LeftPlugin, TEXT("cornerSmoothing"))) == Start.Visual.CornerRadius.Smoothing
					&& static_cast<float>(DesignJsonUtils::GetNumber(RightPlugin, TEXT("cornerSmoothing"))) == End.Visual.CornerRadius.Smoothing)
					PluginExcluded.Add(TEXT("cornerSmoothing"));
				if (DesignJsonUtils::GetBool(LeftPlugin, TEXT("clipsContent"), false) == Start.LayoutData.ExplicitClip.Get(false)
					&& DesignJsonUtils::GetBool(RightPlugin, TEXT("clipsContent"), false) == End.LayoutData.ExplicitClip.Get(false))
					PluginExcluded.Add(TEXT("clipsContent"));
			}

			if (bSurfaceCornerChange)
				for (const TCHAR* Field : {TEXT("cornerRadius"), TEXT("rectangleCornerRadii")}) PluginExcluded.Add(Field);
			LeftCornerMirrors.Reset(); RightCornerMirrors.Reset();
			if (!ConsumeCornerMirrors(Left, LeftPlugin, RightPlugin, Start, LeftCornerMirrors)
				|| !ConsumeCornerMirrors(Right, RightPlugin, LeftPlugin, End, RightCornerMirrors)) return false;
			for (const FString& Field : LeftCornerMirrors)
				if (!RightPlugin->HasField(Field) || RightCornerMirrors.Contains(Field)) PluginExcluded.Add(Field);
			for (const FString& Field : RightCornerMirrors)
				if (!LeftPlugin->HasField(Field) || LeftCornerMirrors.Contains(Field)) PluginExcluded.Add(Field);

			if (bDestinationPage && bSamePageSize) PluginExcluded.Add(TEXT("constraints"));
			if (bText)
			{
				for (const TCHAR* Field : { TEXT("fontSize"), TEXT("letterSpacing"), TEXT("fillGeometry"), TEXT("strokeGeometry") }) PluginExcluded.Add(Field);
				const auto ConsumeTextMirrors = [&](const TSharedPtr<FJsonObject>& Fields, const TSharedPtr<FJsonObject>& Other,
					const TSharedPtr<FJsonObject>& Canonical, TSet<FString>& Consumed)
				{
					for (const TCHAR* Field : {TEXT("fontWeight"), TEXT("textAlignHorizontal"), TEXT("textAlignVertical"), TEXT("fontName"), TEXT("lineHeight")})
					{
						const auto Value = Fields->TryGetField(Field), Opposite = Other->TryGetField(Field);
						if ((!Value && !Opposite) || (Value && Opposite && FJsonValue::CompareEqual(*Value, *Opposite))) continue;
						if (!Value || !Canonical) { Error = TEXT("missing uniform native text mirror"); return false; }
						bool bSame = false;
						if (FCString::Strcmp(Field, TEXT("fontName")) == 0)
						{
							const auto Font = Value->AsObject();
							bSame = Font && DesignJsonUtils::GetString(Font, TEXT("family")) == DesignJsonUtils::GetString(Canonical, TEXT("fontFamily"))
								&& DesignJsonUtils::GetString(Font, TEXT("style")) == DesignJsonUtils::GetString(Canonical, TEXT("fontStyle"));
						}
						else if (FCString::Strcmp(Field, TEXT("lineHeight")) == 0)
						{
							const auto Height = Value->AsObject();
							const FString Unit = DesignJsonUtils::GetString(Height, TEXT("unit"));
							const FString CanonicalUnit = DesignJsonUtils::GetString(Canonical, TEXT("lineHeightUnit"));
							bSame = Unit == TEXT("AUTO") ? CanonicalUnit == TEXT("AUTO") || CanonicalUnit == TEXT("INTRINSIC_%")
								: Unit == TEXT("PIXELS") && CanonicalUnit == TEXT("PIXELS")
									&& DesignJsonUtils::GetNumber(Height, TEXT("value")) == DesignJsonUtils::GetNumber(Canonical, TEXT("lineHeightPx"));
						}
						else
						{
							const auto Actual = Canonical->TryGetField(Field);
							bSame = Actual && FJsonValue::CompareEqual(*Value, *Actual);
						}
						if (!bSame) { Error = FString::Printf(TEXT("text mirror '%s' has no canonical native owner"), Field); return false; }
						Consumed.Add(Field);
					}
					return true;
				};
				if (bTextAppearanceChange)
					if (!ConsumeTextMirrors(LeftPlugin, RightPlugin, DesignJsonUtils::GetObject(Left, TEXT("style")), PluginExcluded)
						|| !ConsumeTextMirrors(RightPlugin, LeftPlugin, DesignJsonUtils::GetObject(Right, TEXT("style")), PluginExcluded)) return false;
				const auto A = DesignJsonUtils::GetArray(LeftPlugin, TEXT("styledTextSegments"));
				const auto B = DesignJsonUtils::GetArray(RightPlugin, TEXT("styledTextSegments"));
				if (A && B && A->Num() == 1 && B->Num() == 1 && (*A)[0]->AsObject() && (*B)[0]->AsObject()
					&& !FJsonValue::CompareEqual(*LeftPlugin->TryGetField(TEXT("styledTextSegments")), *RightPlugin->TryGetField(TEXT("styledTextSegments"))))
				{
					const auto LeftSegment = (*A)[0]->AsObject();
					const auto RightSegment = (*B)[0]->AsObject();
					TSet<FString> SegmentExcluded = { TEXT("fontSize"), TEXT("letterSpacing"), TEXT("fills"), TEXT("fillStyleId") };
					if (bTextAppearanceChange)
						if (!ConsumeTextMirrors(LeftSegment, RightSegment, DesignJsonUtils::GetObject(Left, TEXT("style")), SegmentExcluded)
							|| !ConsumeTextMirrors(RightSegment, LeftSegment, DesignJsonUtils::GetObject(Right, TEXT("style")), SegmentExcluded)) return false;
					if (bTextOverride)
						for (const TCHAR* Field : { TEXT("characters"), TEXT("start"), TEXT("end") }) SegmentExcluded.Add(Field);
					if (!EqualFields(LeftSegment, RightSegment, SegmentExcluded, Error)) return false;
					const auto LeftFills = LeftPlugin->TryGetField(TEXT("fills"));
					const auto RightFills = RightPlugin->TryGetField(TEXT("fills"));
					const auto LeftSegmentFills = LeftSegment->TryGetField(TEXT("fills"));
					const auto RightSegmentFills = RightSegment->TryGetField(TEXT("fills"));
					if (!LeftFills || !RightFills || !LeftSegmentFills || !RightSegmentFills
						|| !FJsonValue::CompareEqual(*LeftFills, *LeftSegmentFills)
						|| !FJsonValue::CompareEqual(*RightFills, *RightSegmentFills))
					{ Error = TEXT("text segment paints differ from the uniform node paint"); return false; }
					PluginExcluded.Add(TEXT("styledTextSegments"));
				}
			}
			if (bComponentPair)
			{
				for (const TCHAR* Field : { TEXT("documentationLinks"), TEXT("scaleFactor"), TEXT("overrides"),
					TEXT("componentProperties"), TEXT("key"), TEXT("descriptionMarkdown"), TEXT("description"),
					TEXT("mainComponent"), TEXT("masterComponent"), TEXT("variantProperties"), TEXT("instances"),
					TEXT("exposedInstances"), TEXT("remote"), TEXT("componentPropertyReferences"), TEXT("isExposedInstance") })
					PluginExcluded.Add(Field);
			}
			if (LeftType == TEXT("INSTANCE"))
			{
				PluginExcluded.Add(TEXT("exposedInstances"));
				PluginExcluded.Add(TEXT("overrides"));
			}
			if (bFixedVectorEndpoint)
			{
				for (const auto& Pair : {TPair<TSharedPtr<FJsonObject>, TSharedPtr<FJsonObject>>(Left, LeftPlugin),
					TPair<TSharedPtr<FJsonObject>, TSharedPtr<FJsonObject>>(Right, RightPlugin)})
				{
					const auto Mirror = Pair.Value->TryGetField(TEXT("strokeGeometry"));
					const auto Canonical = Pair.Key->TryGetField(TEXT("strokeGeometry"));
					if (Mirror && !VectorStrokeMirrorEqual(Canonical, Mirror))
					{ Error = TEXT("vector stroke outline mirror differs from its canonical native paint"); return false; }
				}
				if (!NormalizedVectorCenterEqual(LeftPlugin, RightPlugin, StartSize, EndSize, Left, Right))
				{ Error = TEXT("vector plugin center geometry or region paint is not equivalent"); return false; }
				for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")}) PluginExcluded.Add(Field);
			}
			if (bEquivalentVectorGeometry && NormalizedVectorGeometryEqual(LeftPlugin, RightPlugin, StartSize, EndSize))
				for (const TCHAR* Field : {TEXT("fillGeometry"), TEXT("strokeGeometry"), TEXT("vectorPaths"), TEXT("vectorNetwork")}) PluginExcluded.Add(Field);
			if (bGeometryReplacement)
			{
				PluginExcluded.Add(TEXT("fillGeometry"));
				PluginExcluded.Add(TEXT("strokeGeometry"));
			}
			if (bEquivalentVectorGeometry && ResolvedVectorNetworkPaintsEqual(LeftPlugin, RightPlugin))
				PluginExcluded.Add(TEXT("vectorNetwork"));
			if (bStrokeEndpointChanged)
			{
				// ParseStroke consumes canonical paints; these mirrors do not own rendering.
				PluginExcluded.Add(TEXT("strokes"));
				PluginExcluded.Add(TEXT("strokeStyleId"));
			}
			if (bStrokePresenceChanged || bStrokeSizeEndpoint || bFixedVectorEndpoint)
			{
				PluginExcluded.Add(TEXT("strokeGeometry"));
				for (const TCHAR* Field : {TEXT("strokeWeight"), TEXT("strokeTopWeight"), TEXT("strokeRightWeight"), TEXT("strokeBottomWeight"), TEXT("strokeLeftWeight")})
				{
					bool bMatches = true;
					for (const auto& Pair : {TPair<TSharedPtr<FJsonObject>, TSharedPtr<FJsonObject>>(Left, LeftPlugin),
						TPair<TSharedPtr<FJsonObject>, TSharedPtr<FJsonObject>>(Right, RightPlugin)})
					{
						double Mirror = 0, Canonical = 0;
						if (Pair.Value->HasField(Field) && (!Pair.Key->TryGetNumberField(TEXT("strokeWeight"), Canonical)
							|| !Pair.Value->TryGetNumberField(Field, Mirror) || Mirror != Canonical)) bMatches = false;
					}
					if (bMatches) PluginExcluded.Add(Field);
				}
			}
			if (bTextOverride)
			{
				PluginExcluded.Add(TEXT("characters"));
				PluginExcluded.Add(TEXT("fillGeometry"));
				PluginExcluded.Add(TEXT("strokeGeometry"));
			}
			if (bFillsChanged && EqualObjectExcept(LeftPlugin, RightPlugin, TEXT("boundVariables"), TEXT("fills"))) PluginExcluded.Add(TEXT("boundVariables"));
			if (HasInstanceOverride(LeftPlugin, RightPlugin, InstanceProperties, TEXT("visible"))) PluginExcluded.Add(TEXT("visible"));
			bool bPluginFillsChanged = false;
			if (!MatchInactiveMotion(LeftPlugin, RightPlugin, Error) || !EqualFields(LeftPlugin, RightPlugin, PluginExcluded, Error)
				|| !((bText || bSurfacePaint) ? MatchSurfacePaints(LeftPlugin, RightPlugin, bText, bPluginFillsChanged, Error)
					: MatchFills(LeftPlugin, RightPlugin, bSingleSolid && bFillsChanged, bPluginFillsChanged, Error))) return false;
		}
		if (bGeometryReplacement)
		{
			const auto Same = [&](const TCHAR* Name)
			{
				const auto A = Left->TryGetField(Name);
				const auto B = Right->TryGetField(Name);
				return (!A && !B) || (A && B && FJsonValue::CompareEqual(*A, *B));
			};
			const auto Raw = BuildSourceSubtree(End);
			if (!Raw || !Same(TEXT("size")) || !Same(TEXT("relativeTransform"))
				|| !FMath::IsNearlyEqual(Start.Transform.Opacity, End.Transform.Opacity) || bFillsChanged)
			{
				Error = TEXT("changed geometry cannot share this native dissolve");
				return false;
			}
			FUMGPrototypeAddedLayer& Added = AddedLayers.AddDefaulted_GetRef();
			Added.ParentNodeId = Start.Id;
			Added.NodeId = InstanceScopeId + TEXT("::") + End.Id;
			Added.DestinationNodeId = End.Id;
			Added.SourceNodeJson = DesignJsonUtils::ToCondensedString(Raw);
			Added.bReplaceSource = true;
			FUMGPrototypeWidgetAnimationTarget& Outgoing = Targets.AddDefaulted_GetRef();
			Outgoing.NodeId = Start.Id;
			Outgoing.DestinationNodeId = End.Id;
			Outgoing.bEndPresent = false;
			Outgoing.bOpacity = true;
			Outgoing.StartOpacity = Start.Transform.Opacity;
			Outgoing.EndOpacity = 0;
			FUMGPrototypeWidgetAnimationTarget& Incoming = Targets.AddDefaulted_GetRef();
			Incoming.NodeId = Added.NodeId;
			Incoming.DestinationNodeId = End.Id;
			Incoming.bStartPresent = false;
			Incoming.bOpacity = true;
			Incoming.StartOpacity = 0;
			Incoming.EndOpacity = End.Transform.Opacity;
			return true;
		}
		FUMGPrototypeWidgetAnimationTarget Target;
		Target.NodeId = Start.Id;
		Target.DestinationNodeId = End.Id;
		Target.DestinationWidgetId = RuntimeEnd.Id;
		bool bRounded = !FMath::IsNearlyZero(DesignJsonUtils::GetNumber(Left, TEXT("cornerRadius")));
		if (const auto Corners = DesignJsonUtils::GetArray(Left, TEXT("rectangleCornerRadii")))
			for (const auto& Corner : *Corners) bRounded |= !FMath::IsNearlyZero(Corner->AsNumber());
		if (bResize && !bText && (Start.Type != EDesignNodeType::Rectangle || Start.Children.Num() > 0
			|| bRounded || (Start.Visual.Fills.Num() > 0 && (Start.Visual.Fills.Num() != 1 || !Start.Visual.Fills[0].IsColor()))
			|| (DesignJsonUtils::GetArray(Left, TEXT("strokes")) && DesignJsonUtils::GetArray(Left, TEXT("strokes"))->Num() > 0)
			|| (DesignJsonUtils::GetArray(Left, TEXT("effects")) && DesignJsonUtils::GetArray(Left, TEXT("effects"))->Num() > 0)))
		{
			bFixedViewportLayout = true;
		}
		if (Start.Transform.bFlipX != End.Transform.bFlipX || Start.Transform.bFlipY != End.Transform.bFlipY
			|| !Start.Transform.TransformOrigin.Equals(End.Transform.TransformOrigin))
		{
			Error = TEXT("flip or transform-origin changes are unsupported");
			return false;
		}
		if ((StartSize.X == 0 && EndSize.X != 0) || (StartSize.Y == 0 && EndSize.Y != 0))
		{
			Error = TEXT("a zero source axis cannot be represented by native render scale");
			return false;
		}
		Target.Scale = bText ? FVector2D(1, 1) : FVector2D(StartSize.X == 0 ? 1 : EndSize.X / StartSize.X,
			StartSize.Y == 0 ? 1 : EndSize.Y / StartSize.Y);
		Target.bTextBoxChange = bText && bResize;
		Target.bFontSize = bText && !FMath::IsNearlyEqual(Start.Text.FontSize, End.Text.FontSize);
		Target.bLetterSpacing = bText && !FMath::IsNearlyEqual(Start.Text.LetterSpacing.Get(0), End.Text.LetterSpacing.Get(0));
		Target.bTextPaintChange = bText && bFillsChanged;
		Target.bSurfacePaintChange = bSurfacePaint && bFillsChanged && !bSurfaceEndpointChange && !bStrokeEndpointChanged;
		const auto StartMatrix = Left->TryGetField(TEXT("relativeTransform"));
		const auto EndMatrix = Right->TryGetField(TEXT("relativeTransform"));
		const bool bIdenticalLocalGeometry = !bResize && ((StartMatrix && EndMatrix
			&& FJsonValue::CompareEqual(*StartMatrix, *EndMatrix))
			|| (!StartMatrix && !EndMatrix && FMath::IsNearlyEqual(Start.Transform.X, End.Transform.X)
				&& FMath::IsNearlyEqual(Start.Transform.Y, End.Transform.Y)
				&& FMath::IsNearlyEqual(Start.Transform.Rotation, End.Transform.Rotation)));
		if (bResize && !bText && !bFixedViewportLayout && (!Start.Transform.Width.IsFixed() || !Start.Transform.Height.IsFixed()
			|| !End.Transform.Width.IsFixed() || !End.Transform.Height.IsFixed()
			|| Start.Transform.Width.Value < 0 || Start.Transform.Height.Value < 0
			|| End.Transform.Width.Value < 0 || End.Transform.Height.Value < 0))
		{
			Error = TEXT("geometry animation requires explicit fixed nonnegative sizes");
			return false;
		}
		FVector2D FixedTranslation;
		const bool bConstantFixedPose = [&]()
		{
			if (bIdenticalLocalGeometry || !bFixedViewportLayout || StartSize != EndSize
				|| !Left->HasField(TEXT("size")) || !Right->HasField(TEXT("size"))) return false;
			const auto A = DesignJsonUtils::GetArray(Left, TEXT("relativeTransform"));
			const auto B = DesignJsonUtils::GetArray(Right, TEXT("relativeTransform"));
			if (!A || !B || A->Num() != 2 || B->Num() != 2) return false;
			double Values[6];
			for (int32 Row = 0; Row < 2; ++Row)
			{
				if ((*A)[Row]->Type != EJson::Array || (*B)[Row]->Type != EJson::Array
					|| (*A)[Row]->AsArray().Num() != 3 || (*B)[Row]->AsArray().Num() != 3) return false;
				for (int32 Column = 0; Column < 3; ++Column)
				{
					double Other;
					if (!(*A)[Row]->AsArray()[Column]->TryGetNumber(Values[Row*3+Column])
						|| !(*B)[Row]->AsArray()[Column]->TryGetNumber(Other)
						|| !FMath::IsFinite(Values[Row*3+Column]) || !FMath::IsFinite(Other)) return false;
					if (Column < 2 && Values[Row*3+Column] != Other) return false;
					if (Column == 2) FixedTranslation[Row] = Other - Values[Row*3+Column];
				}
			}
			const FVector2D AxisX(Values[0], Values[3]), AxisY(Values[1], Values[4]);
			return FMath::IsNearlyEqual(AxisX.Size(), 1.0) && FMath::IsNearlyEqual(AxisY.Size(), 1.0)
				&& FMath::IsNearlyZero(FVector2D::DotProduct(AxisX, AxisY));
		}();
		if (!bIdenticalLocalGeometry && bConstantFixedPose)
		{
			// The measured Canvas slot moves the unchanged native rotation/flip pose.
			if (!bRoot && !Target.bTextBoxChange) Target.Translation = FixedTranslation;
		}
		else if (!bIdenticalLocalGeometry)
		{
			FVector2D StartCenter, EndCenter;
			double StartAngle = 0, EndAngle = 0;
			if (!ReadLocalGeometry(Start, StartSize, StartCenter, StartAngle, Error)
				|| !ReadLocalGeometry(End, EndSize, EndCenter, EndAngle, Error)) return false;
			if (!FMath::IsNearlyEqual(static_cast<float>(StartAngle), Start.Transform.Rotation)
				|| (bDestinationPage && !FMath::IsNearlyEqual(static_cast<float>(EndAngle), End.Transform.Rotation)))
			{
				Error = TEXT("native static rotation differs from the source matrix; property animation cannot hide that mismatch");
				return false;
			}
			Target.Rotation = EndAngle - StartAngle;
			if (!bRoot)
			{
				if (!Target.bTextBoxChange) Target.Translation = EndCenter - StartCenter;
			}
			else Target.Translation = Start.Transform.TransformOrigin * (EndSize - StartSize);
		}
		Target.StartOpacity = Start.Transform.Opacity;
		Target.EndOpacity = End.Transform.Opacity;
		Target.bOpacity = !FMath::IsNearlyEqual(Target.StartOpacity, Target.EndOpacity);
		if (bFillsChanged && !bText && !bSurfacePaint && !bSurfaceEndpointChange && !bStrokeEndpointChanged)
		{
			Target.StartColor = FUMGValueParser::ParseColor(Start.Visual.Fills[0].SolidColor);
			Target.EndColor = FUMGValueParser::ParseColor(End.Visual.Fills[0].SolidColor);
			Target.bColor = true;
		}
		if ((bLeaf && bShadowChange) || bSurfaceEndpointChange || bStrokeEndpointChanged || bTextAppearanceChange || bResize || !Target.Translation.IsNearlyZero() || !FMath::IsNearlyZero(Target.Rotation) || Target.bOpacity || Target.bColor || Target.bFontSize || Target.bLetterSpacing || Target.bTextPaintChange || Target.bSurfacePaintChange)
		{
			Targets.Add(Target);
		}

		const auto BaseChildKey = [](const TSharedPtr<FDesignNode>& Child) -> FString
		{
			if (!Child || Child->Name.IsEmpty()) return FString();
			const FString& SourceType = Child->Source().SourceNodeType;
			return SourceType.IsEmpty()
				? FString::Printf(TEXT("%d|%s"), static_cast<int32>(Child->Type), *Child->Name)
				: SourceType + TEXT("|") + Child->Name;
		};
		TSet<FString> DuplicateKeys;
		for (const FDesignNode* Parent : {&Start, &End})
		{
			TSet<FString> Seen;
			for (const auto& Child : Parent->Children)
			{
				const FString Key = BaseChildKey(Child);
				if (Seen.Contains(Key)) DuplicateKeys.Add(Key);
				Seen.Add(Key);
			}
		}
		const auto ChildKey = [&](const TSharedPtr<FDesignNode>& Child, int32 ParentIndex) -> FString
		{
			const FString Key = BaseChildKey(Child);
			if (Key.IsEmpty() || !DuplicateKeys.Contains(Key)) return Key;
			// Figma matches repeated names by their index within an already matched parent.
			return Key + FString::Printf(TEXT("|%d"), ParentIndex);
		};
        TArray<const FDesignNode*> RuntimeChildren;
        for (int32 Index = 0; Index < End.Children.Num(); ++Index)
        {
            const FDesignNode* RuntimeChild = nullptr;
            for (int32 RuntimeIndex = 0; RuntimeIndex < RuntimeEnd.Children.Num(); ++RuntimeIndex)
            {
                const auto& Child = RuntimeEnd.Children[RuntimeIndex];
                if (ChildKey(Child, RuntimeIndex) != ChildKey(End.Children[Index], Index)) continue;
                if (RuntimeChild)
                { Error = TEXT("target state child has no unique generated counterpart"); return false; }
                RuntimeChild = Child.Get();
            }
            if (!RuntimeChild)
            { Error = TEXT("target state child has no unique generated counterpart"); return false; }
            RuntimeChildren.Add(RuntimeChild);
        }
		TMap<FString, int32> DestinationIndices;
		for (int32 Index = 0; Index < End.Children.Num(); ++Index)
		{
			if (!RuntimeChildren[Index]->Transform.bEnabled) continue;
			const FString Key = ChildKey(End.Children[Index], Index);
			if (Key.IsEmpty() || DestinationIndices.Contains(Key))
			{
				Error = FString::Printf(TEXT("ambiguous child identity under '%s' (%s) -> '%s' (%s): destination '%s'"),
					*Start.Name, *Start.Id, *End.Name, *End.Id, End.Children[Index] ? *End.Children[Index]->Name : TEXT("null"));
				return false;
			}
			DestinationIndices.Add(Key, Index);
		}
		TSet<FString> SourceKeys;
		TArray<int32> PairedIndices;
		PairedIndices.Init(INDEX_NONE, Start.Children.Num());
		bool bLayoutReordered = false;
		int32 PreviousDestination = INDEX_NONE;
		bool bPaintOrderChanged = false;
		for (int32 Index = 0; Index < Start.Children.Num(); ++Index)
		{
			if (Start.Children[Index] && !Start.Children[Index]->Transform.bEnabled) continue;
			const FString Key = ChildKey(Start.Children[Index], Index);
			const int32* DestinationIndex = DestinationIndices.Find(Key);
			if (Key.IsEmpty() || SourceKeys.Contains(Key))
			{
				Error = FString::Printf(TEXT("ambiguous child identity under '%s' (%s) -> '%s' (%s): source '%s'"),
					*Start.Name, *Start.Id, *End.Name, *End.Id, Start.Children[Index] ? *Start.Children[Index]->Name : TEXT("null"));
				return false;
			}
			SourceKeys.Add(Key);
			if (!DestinationIndex)
			{
				if (Start.LayoutData.Layout != EDesignLayout::None && !Start.Children[Index]->LayoutData.bAbsoluteLayoutPositioning)
				{
					bFixedViewportLayout = true;
				}
				FUMGPrototypeWidgetAnimationTarget& Removed = Targets.AddDefaulted_GetRef();
				Removed.NodeId = Start.Children[Index]->Id;
				Removed.bEndPresent = false;
				Removed.bOpacity = true;
				Removed.StartOpacity = Start.Children[Index]->Transform.Opacity;
				Removed.EndOpacity = 0;
				LayoutTargets.Add({Removed.NodeId, FString(), FString()});
				continue;
			}
			PairedIndices[Index] = *DestinationIndex;
			bLayoutReordered |= *DestinationIndex < PreviousDestination;
			PreviousDestination = *DestinationIndex;
			bPaintOrderChanged |= Start.LayoutData.ChildPaintRank(Index, Start.Children.Num())
				!= End.LayoutData.ChildPaintRank(*DestinationIndex, End.Children.Num());
		}
		int32 FirstFlow = INDEX_NONE;
		int32 LastFlow = INDEX_NONE;
		if (End.LayoutData.Layout != EDesignLayout::None)
		{
			for (int32 Index = 0; Index < End.Children.Num(); ++Index)
			{
				if (RuntimeChildren[Index]->Transform.bEnabled && !End.Children[Index]->LayoutData.bAbsoluteLayoutPositioning)
				{
					const int32 Rank = End.LayoutData.ChildPaintRank(Index, End.Children.Num());
					FirstFlow = FirstFlow == INDEX_NONE ? Rank : FMath::Min(FirstFlow, Rank);
					LastFlow = FMath::Max(LastFlow, Rank);
				}
			}
		}
		for (int32 Index = 0; Index < End.Children.Num(); ++Index)
		{
			const auto& Child = End.Children[Index];
			if (!RuntimeChildren[Index]->Transform.bEnabled) continue;
			if (SourceKeys.Contains(ChildKey(Child, Index))) continue;
			const auto Raw = BuildSourceSubtree(*RuntimeChildren[Index]);
			if (InstanceScopeId.IsEmpty() || !Raw)
			{
				Error = TEXT("new transition child requires a native transition scope and source");
				return false;
			}
			const int32 PaintRank = End.LayoutData.ChildPaintRank(Index, End.Children.Num());
			if (End.LayoutData.Layout != EDesignLayout::None && !Child->LayoutData.bAbsoluteLayoutPositioning)
				bFixedViewportLayout = true;
			if (FirstFlow != INDEX_NONE && PaintRank > FirstFlow && PaintRank < LastFlow && !bFixedViewportLayout)
			{
				Error = TEXT("absolute transition layer interleaved with flow children cannot preserve paint order");
				return false;
			}
			FUMGPrototypeAddedLayer& Added = AddedLayers.AddDefaulted_GetRef();
			Added.ParentNodeId = Start.Id;
			Added.NodeId = InstanceScopeId + TEXT("::") + Child->Id;
			Added.DestinationNodeId = Child->Id;
			Added.SourceNodeJson = DesignJsonUtils::ToCondensedString(Raw);
			Added.RelativePosition = FVector2D(Child->Transform.X, Child->Transform.Y);
			Added.DestinationChildIndex = Index;
			Added.EndIndex = PaintRank;
			Added.bBehindFlow = FirstFlow != INDEX_NONE && PaintRank < FirstFlow;
			Added.bFlowChild = End.LayoutData.Layout != EDesignLayout::None && !Child->LayoutData.bAbsoluteLayoutPositioning;
			FUMGPrototypeWidgetAnimationTarget& AddedTarget = Targets.AddDefaulted_GetRef();
			AddedTarget.NodeId = Added.NodeId;
			AddedTarget.DestinationNodeId = Child->Id;
			AddedTarget.bStartPresent = false;
			AddedTarget.bOpacity = true;
			AddedTarget.StartOpacity = 0;
			AddedTarget.EndOpacity = Child->Transform.Opacity;
			AddedTarget.DestinationWidgetId = RuntimeChildren[Index]->Id;
			LayoutTargets.Add({Added.NodeId, RuntimeChildren[Index]->Id, FString()});
		}
		if ((bLayoutReordered || bPaintOrderChanged) && (Start.LayoutData.Layout != EDesignLayout::None || End.LayoutData.Layout != EDesignLayout::None))
		{
			bFixedViewportLayout = true;
		}
		for (int32 Index = 0; Index < Start.Children.Num(); ++Index)
		{
			const auto& SourceChild = Start.Children[Index];
			if (PairedIndices[Index] == INDEX_NONE) continue;
			const auto& TargetChild = End.Children[PairedIndices[Index]];
			const int32 StartRank = Start.LayoutData.ChildPaintRank(Index, Start.Children.Num());
			const int32 EndRank = End.LayoutData.ChildPaintRank(PairedIndices[Index], End.Children.Num());
			if (StartRank != EndRank)
			{
				FUMGPrototypeLayerOrder& Order = LayerOrders.AddDefaulted_GetRef();
				Order.ParentNodeId = Start.Id;
				Order.NodeId = SourceChild->Id;
				Order.DestinationNodeId = TargetChild->Id;
				Order.StartIndex = StartRank;
				Order.EndIndex = EndRank;
			}
			const FDesignNode* RuntimeChild = RuntimeChildren[PairedIndices[Index]];
			if (!MatchTree(*SourceChild, *TargetChild, *RuntimeChild, false, bDestinationPage, bSamePageSize, Targets, LayerOrders, AddedLayers, InstanceScopeId, InstanceProperties,
				Start.LayoutData.Layout != EDesignLayout::None || End.LayoutData.Layout != EDesignLayout::None,
				bFixedViewportLayout, LayoutTargets, LocalEndpoints, SharedCaptures, Error))
			{
				if (!Error.Contains(TEXT("[source node ")))
					Error += FString::Printf(TEXT(" [source node '%s' (%s, %s) -> '%s' (%s, %s)]"),
						*SourceChild->Name, *SourceChild->Id, *SourceChild->Source().SourceNodeType,
						*TargetChild->Name, *TargetChild->Id, *TargetChild->Source().SourceNodeType);
				return false;
			}
		}
		const auto HasActiveEffect = [](const TSharedPtr<FJsonObject>& Node)
		{
			const auto Effects = DesignJsonUtils::GetArray(Node, TEXT("effects"));
			return Effects && Effects->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
				{ return DesignJsonUtils::GetBool(Value->AsObject(), TEXT("visible"), true); });
		};
		const bool bEffectEndpoint = bShadowChange || bClipChange || (bStrokeEndpointChanged && !bRoundedSurfacePair && !bFixedVectorEndpoint)
			|| ((bSurfaceEndpointChange || bStrokeEndpointChanged || bTextAppearanceChange)
				&& (Start.LayoutData.ExplicitClip.Get(false) || End.LayoutData.ExplicitClip.Get(false)
					|| HasActiveEffect(Left) || HasActiveEffect(Right)));
		const bool bCaptureEndpoint = !bLeaf && bEffectEndpoint;
		const bool bLeafEndpoint = bLeaf && bEffectEndpoint;
		if (bCaptureEndpoint && (bTextAppearanceChange || bFixedVectorEndpoint))
		{ Error = TEXT("local native paint endpoint with effects or clip requires a shared native capture"); return false; }
		if (bCaptureEndpoint)
		{
			if (!bRoundedSurfacePair)
			{ Error = TEXT("shared capture requires a supported native surface owner"); return false; }
			SharedCaptures.Add({Start.Id, RuntimeEnd.Id});
			bFixedViewportLayout = true;
		}
		if (bLeafEndpoint || bSurfaceEndpointChange || bStrokeEndpointChanged || bTextAppearanceChange)
		{
			if (bTextAppearanceChange && (!Start.Children.IsEmpty() || !End.Children.IsEmpty() || !Start.Text.TextRuns.IsEmpty() || !End.Text.TextRuns.IsEmpty()))
			{ Error = TEXT("local text endpoint requires a native leaf"); return false; }
			FUMGPrototypeLocalEndpoint& Endpoint = LocalEndpoints.AddDefaulted_GetRef();
			Endpoint.NodeId = Start.Id;
			Endpoint.DestinationWidgetId = RuntimeEnd.Id;
			Endpoint.bText = bText;
			Endpoint.bFixedPaintSize = bFixedVectorEndpoint || bCaptureEndpoint || bLeafEndpoint;
			bFixedViewportLayout = true;
		}

		return true;
	}

	struct FStateContext
	{
		FString InstanceId;
		FString StateId;
	};

	void ScopeStateTree(const TSharedPtr<FDesignNode>& Node, const FString& Prefix,
		const FStateContext& Context, const FString& PageId,
		TMap<FString, FStateContext>& Contexts, TMap<FString, FString>& NodeToPage)
	{
		if (!Node) return;
		Node->Id = Prefix + Node->Id;
		Contexts.Add(Node->Id, Context);
		NodeToPage.Add(Node->Id, PageId);
		for (const auto& Child : Node->Children)
		{
			FStateContext ChildContext = Context;
			if (Child && Child->Source().ComponentInstance.bIsInstance)
				ChildContext = { Prefix + Child->Id, Child->Source().ComponentInstance.ComponentId };
			ScopeStateTree(Child, Prefix, ChildContext, PageId, Contexts, NodeToPage);
		}
	}

	void ConfigureCurve(FUMGPrototypeTransitionData& Transition, const FString& TriggerWidgetId,
		const FString& DurationUnit, TArray<FString>& Warnings)
	{
		if (!Transition.Type.IsEmpty() && DurationUnit != TEXT("MILLISECONDS"))
			Transition.Diagnostic = TEXT("transition duration unit is unverified; require producer-normalized design2umgPrototypeDurationUnit=MILLISECONDS");
		if (Transition.Diagnostic.IsEmpty() && Transition.Type.IsEmpty())
		{
			// An instant state has no interpolation curve.
		}
		else if (Transition.Diagnostic.IsEmpty() && Transition.EasingType == TEXT("CUSTOM_SPRING"))
		{
			TSharedPtr<FJsonObject> Easing;
			DesignJsonUtils::TryParseObject(Transition.EasingJson, Easing);
			const auto Spring = DesignJsonUtils::GetObject(Easing, TEXT("easingFunctionSpring"));
			double Mass = 0, Stiffness = 0, Damping = 0, Velocity = 0;
			const bool bHasVelocity = Spring && Spring->HasField(TEXT("initialVelocity"));
			if (!Spring || !Spring->TryGetNumberField(TEXT("mass"), Mass)
				|| !Spring->TryGetNumberField(TEXT("stiffness"), Stiffness)
				|| !Spring->TryGetNumberField(TEXT("damping"), Damping)
				|| (bHasVelocity && !Spring->TryGetNumberField(TEXT("initialVelocity"), Velocity))
				|| !FMath::IsFinite(Mass) || !FMath::IsFinite(Stiffness)
				|| !FMath::IsFinite(Damping) || !FMath::IsFinite(Velocity)
				|| Mass <= 0 || Stiffness <= 0 || Damping < 0)
			{
				Transition.Diagnostic = TEXT("unsupported easing: invalid custom spring parameters");
			}
			else
			{
				Transition.bSpring = true;
				Transition.SpringMass = Mass;
				Transition.SpringStiffness = Stiffness;
				Transition.SpringDamping = Damping;
				Transition.SpringInitialVelocity = Velocity;
				Transition.bSpringVelocityDefaulted = !bHasVelocity;
				if (!bHasVelocity)
					Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s': spring initial velocity not exported; assuming 0"), *TriggerWidgetId));
			}
		}
		else if (Transition.Diagnostic.IsEmpty() && Transition.EasingType != TEXT("LINEAR"))
		{
			TSharedPtr<FJsonObject> Easing;
			DesignJsonUtils::TryParseObject(Transition.EasingJson, Easing);
			const auto Curve = DesignJsonUtils::GetObject(Easing, TEXT("easingFunctionCubicBezier"));
			double FirstX = 0, FirstY = 0, SecondX = 0, SecondY = 0;
			bool bKnownCurve = true;
			if (Transition.EasingType == TEXT("EASE_IN")) { FirstX = 0.42; SecondX = 1; SecondY = 1; }
			else if (Transition.EasingType == TEXT("EASE_OUT")) { SecondX = 0.58; SecondY = 1; }
			else if (Transition.EasingType == TEXT("EASE_IN_AND_OUT")) { FirstX = 0.42; SecondX = 0.58; SecondY = 1; }
			else bKnownCurve = Transition.EasingType == TEXT("CUSTOM_CUBIC_BEZIER") && Curve
				&& Curve->TryGetNumberField(TEXT("x1"), FirstX) && Curve->TryGetNumberField(TEXT("y1"), FirstY)
				&& Curve->TryGetNumberField(TEXT("x2"), SecondX) && Curve->TryGetNumberField(TEXT("y2"), SecondY);
			if (!bKnownCurve
				|| !FMath::IsFinite(FirstX) || !FMath::IsFinite(FirstY) || !FMath::IsFinite(SecondX) || !FMath::IsFinite(SecondY)
				|| FirstX < 0 || SecondX > 1 || FirstX > SecondX
				|| (FirstX == 0 && FirstY != 0) || (SecondX == 1 && SecondY != 1))
			{
				Transition.Diagnostic = TEXT("unsupported easing: use LINEAR, EASE_IN/OUT/IN_AND_OUT, or an explicit nonvertical, nonoverlapping CUSTOM_CUBIC_BEZIER; no sampled approximation");
			}
			else
			{
				Transition.bCubicBezier = true;
				Transition.CubicBezier = FVector4(FirstX, FirstY, SecondX, SecondY);
			}
		}
	}

	void IndexTree(const TSharedPtr<FDesignNode>& Node, const FDesignNode* Parent,
		TMap<FString, const FDesignNode*>& Nodes, TMap<const FDesignNode*, const FDesignNode*>& Parents)
	{
		if (!Node) return;
		Nodes.Add(Node->Id, Node.Get());
		Parents.Add(Node.Get(), Parent);
		for (const auto& Child : Node->Children) IndexTree(Child, Node.Get(), Nodes, Parents);
	}
}

void Design2UMGPrototypeAnimationSource::Resolve(const FDesignDocument& Document,
	FUMGPrototypeNavigationData& Navigation, TArray<FString>& Warnings)
{
	TMap<FString, const FDesignNode*> Nodes;
	TMap<const FDesignNode*, const FDesignNode*> Parents;
	for (const auto& Child : Document.Children) IndexTree(Child, nullptr, Nodes, Parents);
	FigmaComponentDependencies::FCatalog Catalog;
	FString CatalogError;
	TArray<FDesignDocument> Definitions;
	if (!Document.SourceDocument().ComponentDependenciesJson.IsEmpty()
		&& FigmaComponentDependencies::Parse(Document.SourceDocument().ComponentDependenciesJson, Catalog, CatalogError))
	{
		Definitions.Reserve(Catalog.Definitions.Num());
		for (const auto& Pair : Catalog.Definitions)
		{
			if (Nodes.Contains(Pair.Value.NodeId)) continue;
			auto Root = MakeShared<FJsonObject>();
			Root->SetObjectField(TEXT("document"), Pair.Value.Document);
			FDesignDocument Parsed;
			if (FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Root), Parsed))
			{
				Definitions.Add(MoveTemp(Parsed));
				for (const auto& Child : Definitions.Last().Children) IndexTree(Child, nullptr, Nodes, Parents);
			}
		}
	}
	TMap<FString, FStateContext> StateContexts;
	TMap<FString, int32> HostIndices;
	TArray<TSharedPtr<FDesignNode>> StateRoots;
	TSet<FString> SeenBindings;
	TSet<FString> CollectedTriggerIds;
	TMap<FString, int32> PageIndices;
	for (int32 PageIndex = 0; PageIndex < Navigation.PageIds.Num(); ++PageIndex)
		PageIndices.Add(Navigation.PageIds[PageIndex], PageIndex);
	for (int32 BindingIndex = 0; BindingIndex < Navigation.ClickBindings.Num(); ++BindingIndex)
	{
		const FUMGPrototypeNavigationBinding Current = Navigation.ClickBindings[BindingIndex];
		if (Current.Steps.Num() != 1 || Current.Steps[0].Kind != EUMGPrototypeNavigationStepKind::AnimateWidget) continue;
		FStateContext Context;
		if (const FStateContext* Scoped = StateContexts.Find(Current.TriggerWidgetId)) Context = *Scoped;
		else
		{
			const FDesignNode* Instance = Nodes.FindRef(Current.TriggerWidgetId);
			while (Instance && !Instance->Source().ComponentInstance.bIsInstance) Instance = Parents.FindRef(Instance);
			if (Instance) Context = { Instance->Id, Instance->Source().ComponentInstance.ComponentId };
		}
		if (Context.StateId.IsEmpty())
		{
			Navigation.ClickBindings[BindingIndex].Steps[0].Transition.Diagnostic = TEXT("state action requires a real instance and its component id");
			continue;
		}
		if (!HostIndices.Contains(Context.InstanceId))
		{
			const int32 HostIndex = Navigation.StateHosts.AddDefaulted();
			HostIndices.Add(Context.InstanceId, HostIndex);
			auto& Host = Navigation.StateHosts[HostIndex];
			Host.HostWidgetId = Context.InstanceId + TEXT("__state");
			Host.InstanceWidgetId = Context.InstanceId;
			Host.InitialStateId = Context.StateId;
			auto& Initial = Host.States.AddDefaulted_GetRef();
			Initial.StateId = Context.StateId;
			Initial.WidgetId = Context.InstanceId;
		}
		StateContexts.Add(Current.TriggerWidgetId, Context);
		const int32 HostIndex = HostIndices.FindChecked(Context.InstanceId);
		const FString TargetId = Current.Steps[0].TargetPageId;
		if (Navigation.StateHosts[HostIndex].States.ContainsByPredicate([&TargetId](const FUMGPrototypeStateEntry& Entry) { return Entry.StateId == TargetId; })) continue;
		const FDesignNode* Target = Nodes.FindRef(TargetId);
		const FDesignNode* Instance = Nodes.FindRef(Context.InstanceId);
		TSharedPtr<FJsonObject> Properties;
		if (Instance) DesignJsonUtils::TryParseObject(Instance->Source().ComponentInstance.ComponentPropertiesJson, Properties);
		const auto Source = Target ? BuildSourceSubtree(*Target) : nullptr;
		TSharedPtr<FJsonObject> Copy;
		FString Error;
		FDesignDocument Parsed;
		if (!Source || !DesignJsonUtils::TryParseObject(DesignJsonUtils::ToCondensedString(Source), Copy)
			|| !ApplyInstanceProperties(Copy, Properties, Error)
			|| (Instance && !ApplyInstanceLayout(*Instance, Nodes.FindRef(Instance->Source().ComponentInstance.ComponentId), *Target, Copy, Error)))
		{
			Navigation.ClickBindings[BindingIndex].Steps[0].Transition.Diagnostic = Error.IsEmpty() ? TEXT("cannot construct reachable target state") : Error;
			continue;
		}
		auto Envelope = MakeShared<FJsonObject>();
		Envelope->SetObjectField(TEXT("document"), Copy);
		if (!FFigmaParser::ParseDocument(DesignJsonUtils::ToCondensedString(Envelope), Parsed)
			|| Parsed.Children.Num() != 1 || !Parsed.Children[0])
		{
			Navigation.ClickBindings[BindingIndex].Steps[0].Transition.Diagnostic = TEXT("cannot parse reachable target state");
			continue;
		}
		FUMGPrototypeStateEntry& Entry = Navigation.StateHosts[HostIndex].States.AddDefaulted_GetRef();
		Entry.StateId = TargetId;
		Entry.WidgetId = Context.InstanceId + TEXT("::") + TargetId;
		Entry.SourceNodeJson = DesignJsonUtils::ToCondensedString(Copy);
		const auto Root = Parsed.Children[0];
		TMap<FString, FString> NodeToPage;
		ScopeStateTree(Root, Context.InstanceId + TEXT("::"), { Context.InstanceId, TargetId }, Current.SourcePageId, StateContexts, NodeToPage);
		StateRoots.Add(Root);
		IndexTree(Root, nullptr, Nodes, Parents);
		Design2UMGPrototypeStepGraphAssembly::GatherNavigationBindings(Root, NodeToPage, PageIndices,
			Navigation.ClickBindings, CollectedTriggerIds, SeenBindings, Warnings);
	}
	for (auto& Binding : Navigation.ClickBindings)
	{
		for (auto& Step : Binding.Steps)
			ConfigureCurve(Step.Transition, Binding.TriggerWidgetId, Document.SourceDocument().PrototypeDurationUnit, Warnings);
		if (Binding.Steps.Num() != 1) continue;
		auto& Step = Binding.Steps[0];
		auto& Transition = Step.Transition;
		const bool bState = Step.Kind == EUMGPrototypeNavigationStepKind::AnimateWidget;
		const bool bStateReplacement = bState && (Transition.Type.IsEmpty() || Transition.Type == TEXT("DISSOLVE"));
		FStateContext InferredContext;
		const FStateContext* Context = StateContexts.Find(Binding.TriggerWidgetId);
		if (!Context)
		{
			const FDesignNode* Parent = Nodes.FindRef(Binding.TriggerWidgetId);
			while (Parent && !HostIndices.Contains(Parent->Id)) Parent = Parents.FindRef(Parent);
			if (Parent)
			{
				InferredContext = { Parent->Id, Navigation.StateHosts[HostIndices.FindChecked(Parent->Id)].InitialStateId };
				Context = &InferredContext;
			}
		}
		const FUMGPrototypeStateHost* Host = Context && HostIndices.Contains(Context->InstanceId)
			? &Navigation.StateHosts[HostIndices.FindChecked(Context->InstanceId)] : nullptr;
		const FUMGPrototypeStateEntry* SourceState = Host ? Host->States.FindByPredicate([Context](const FUMGPrototypeStateEntry& Entry)
		{ return Entry.StateId == Context->StateId; }) : nullptr;
		const FUMGPrototypeStateEntry* TargetState = Host ? Host->States.FindByPredicate([&Step](const FUMGPrototypeStateEntry& Entry)
		{ return Entry.StateId == Step.TargetPageId; }) : nullptr;
		const FDesignNode* Start = Nodes.FindRef(bState && SourceState ? SourceState->WidgetId : Binding.SourcePageId);
		const FDesignNode* End = Nodes.FindRef(bState && TargetState ? TargetState->WidgetId : Step.TargetPageId);
		if (bState && Context && Host && SourceState)
		{
			Transition.SourceStateId = Context->StateId;
			Transition.InstanceScopeId = Context->InstanceId;
			Transition.StateHostWidgetId = Host->HostWidgetId;
			Transition.SourceStateWidgetId = SourceState->WidgetId;
			if (TargetState)
			{
				Transition.TargetStateId = TargetState->StateId;
				Transition.TargetStateWidgetId = TargetState->WidgetId;
			}
		}

		if (!bState && Transition.Type != TEXT("SMART_ANIMATE"))
		{
			Binding.Transition = Transition;
			if (!Transition.Diagnostic.IsEmpty())
				Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s': %s; action not executed"), *Binding.TriggerWidgetId, *Transition.Diagnostic));
			continue;
		}
		if (!bState && Step.Kind != EUMGPrototypeNavigationStepKind::Navigate)
		{
			Transition.Diagnostic = TEXT("property animation supports only CHANGE_TO and page Navigate; Overlay and other actions are not implemented");
		}
		else if (!Binding.UsesClickTrigger() && !Binding.UsesMouseEnterTrigger()
			&& !Binding.UsesHoverTrigger() && !Binding.UsesPressTrigger() && !Binding.UsesDragTrigger()
			&& !Binding.UsesPageOpenTrigger())
		{
			Transition.Diagnostic = TEXT("property animation supports only click, mouse entry, hover, press, drag and page entry triggers");
		}
		else if (Binding.UsesHoverTrigger() && !bState)
		{
			Transition.Diagnostic = TEXT("hover page navigation cannot preserve the required leave reversal; action not executed");
		}

		if (!Start || !End)
		{
			if (Transition.Diagnostic.IsEmpty()) Transition.Diagnostic = TEXT("missing real start tree or destination component/page definition");
		}
		else if ((!bStateReplacement && Transition.Type != TEXT("SMART_ANIMATE"))
			|| (!Transition.Type.IsEmpty() && !Transition.IsAnimated()))
			Transition.Diagnostic = TEXT("widget property transition requires explicit SMART_ANIMATE and positive duration");
		TSharedPtr<FJsonObject> InstanceProperties;
		if (bState && Context)
			if (const FDesignNode* Instance = Nodes.FindRef(Context->InstanceId))
				DesignJsonUtils::TryParseObject(Instance->Source().ComponentInstance.ComponentPropertiesJson, InstanceProperties);
		if (Transition.Diagnostic.IsEmpty())
		{
			if (bStateReplacement) PlanStateReplacement(*Start, *End, Transition);
			else
			{
				FVector2D StartPageSize, EndPageSize;
				FString SizeError;
				const bool bSamePageSize = !bState && ReadLocalSize(*Start, StartPageSize, SizeError)
					&& ReadLocalSize(*End, EndPageSize, SizeError) && StartPageSize.Equals(EndPageSize);
				if (!bState) Transition.PageTransitionScopeId = Binding.SourcePageId + TEXT("::transition::") + Step.TargetPageId;
				const bool bMatched = MatchTree(*Start, *End, *End, true, !bState, bSamePageSize, Transition.WidgetTargets, Transition.LayerOrders, Transition.AddedLayers, Transition.LayerScopeId(), InstanceProperties, false,
					Transition.bFixedViewportLayout, Transition.LayoutTargets, Transition.LocalEndpoints, Transition.SharedCaptures, Transition.Diagnostic);
				if (!bMatched) { Transition.LocalEndpoints.Reset(); Transition.SharedCaptures.Reset(); }

				if (Binding.UsesDragTrigger() && Transition.Diagnostic.IsEmpty())
					for (const auto& Target : Transition.WidgetTargets)
						if (Target.NodeId == Binding.TriggerWidgetId && Target.bTextBoxChange)
						{
							const FDesignNode* A = Nodes.FindRef(Target.NodeId);
							const FDesignNode* B = Nodes.FindRef(Target.DestinationNodeId);
							FVector2D StartCenter, EndCenter;
							if (!A || !B || !ReadCenterInAncestor(*A, *Start, Parents, StartCenter, Transition.Diagnostic)
								|| !ReadCenterInAncestor(*B, *End, Parents, EndCenter, Transition.Diagnostic))
							{
								if (Transition.Diagnostic.IsEmpty()) Transition.Diagnostic = TEXT("drag trigger source geometry is unavailable");
								break;
							}
							Transition.DragMotionVector = EndCenter - StartCenter;
							Transition.bHasDragMotionVector = !Transition.DragMotionVector.IsNearlyZero();
						}
			}
		}
		if (!Transition.bFixedViewportLayout) Transition.LayoutTargets.Reset();
		if (Transition.WidgetTargets.Num() == 0 && Transition.LayerOrders.Num() == 0 && !Transition.bFixedViewportLayout && Transition.Diagnostic.IsEmpty() && Transition.DissolveReason.IsEmpty())
			Transition.Diagnostic = TEXT("no supported property difference in the paired source trees");
		if (!Transition.Diagnostic.IsEmpty())
		{
			Transition.WidgetTargets.Reset();
			Transition.LayerOrders.Reset();
			Transition.AddedLayers.Reset();
			Warnings.AddUnique(FString::Printf(TEXT("Prototype animation '%s' -> '%s': %s; action not executed"),
				*Binding.TriggerWidgetId, *Step.TargetPageId, *Transition.Diagnostic));
		}
		Binding.Transition = Transition;
	}
}
