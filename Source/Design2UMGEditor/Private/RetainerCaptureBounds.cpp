// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "RetainerCaptureBounds.h"
#include "DesignPathRasterizer.h"
#include "UMGRetainerCapture.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetTypes.h"

namespace
{
FVector2D LayoutSize(const FUMGWidgetNode& Node,const FVector2D& Available)
{
    return FVector2D(Node.WidgetStyle.WidthOverride.Get(Node.Bounds.W>0?Node.Bounds.W:Available.X),
        Node.WidgetStyle.HeightOverride.Get(Node.Bounds.H>0?Node.Bounds.H:Available.Y));
}
FBox2D TransformBounds(const FBox2D& Bounds,const FUMGRenderStyleData& Render,const FVector2D& Size)
{
    const FVector2D Pivot=Render.RenderTransformPivot.Get(FVector2D(0.5,0.5))*Size;
    const FVector2D Scale=Render.RenderTransformScale.Get(FVector2D(1,1));
    FBox2D Result(ForceInit);
    for(int32 Corner=0;Corner<4;++Corner)
    {
        const FVector2D Point((Corner&1)?Bounds.Max.X:Bounds.Min.X,(Corner&2)?Bounds.Max.Y:Bounds.Min.Y);
        Result+=((Point-Pivot)*Scale).GetRotated(Render.RenderAngle.Get(0))+Pivot+Render.RenderTranslation.Get(FVector2D::ZeroVector);
    }
    return Result;
}
FBox2D ClipBounds(const FBox2D& Paint,const FVector2D& Size)
{
    return FBox2D(FVector2D(FMath::Clamp(Paint.Min.X,0.0,Size.X),FMath::Clamp(Paint.Min.Y,0.0,Size.Y)),
        FVector2D(FMath::Clamp(Paint.Max.X,0.0,Size.X),FMath::Clamp(Paint.Max.Y,0.0,Size.Y)));
}
FMargin CaptureGutter(const FBox2D& Bounds,const FVector2D& Size,const FMargin& Existing)
{
    return FMargin(FMath::Max(Existing.Left,FMath::CeilToFloat(FMath::Max(0.0,-Bounds.Min.X))),
        FMath::Max(Existing.Top,FMath::CeilToFloat(FMath::Max(0.0,-Bounds.Min.Y))),
        FMath::Max(Existing.Right,FMath::CeilToFloat(FMath::Max(0.0,Bounds.Max.X-Size.X))),
        FMath::Max(Existing.Bottom,FMath::CeilToFloat(FMath::Max(0.0,Bounds.Max.Y-Size.Y))));
}
FVector2D GutterSize(const FMargin& Gutter){return FVector2D(Gutter.Left+Gutter.Right,Gutter.Top+Gutter.Bottom);}

bool PrepareNode(FUMGWidgetNode& Node,const FVector2D& Available,FBox2D& OutPaint,FString& Error)
{
    const FVector2D Size=LayoutSize(Node,Available);
    const bool IsRetainer=Node.Type==UMGWidgetTypes::RetainerBox;
    const FString Material=Node.Effect.EffectMaterial.Get(FString());
    const bool IsBlur=IsRetainer&&Material==UMGPropertyKeys::RetainerForegroundBlurEffectRef;
    const bool IsShadow=IsRetainer&&Material==UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
    if(IsRetainer&&(Node.Effect.Shader.IsSet()||IsBlur||IsShadow))
    {
        FMargin OldGutter(0);
        if(Node.Effect.Shader.IsSet())
        {
            const FVector4& Insets=Node.Effect.Shader->CaptureInsets;
            OldGutter=FMargin(Insets.X,Insets.Y,Insets.Z,Insets.W);
        }
        else if(Node.Children.Num()==1&&Node.Children[0].Type==UMGWidgetTypes::Border)
        {
            const FMargin Padding=Node.Children[0].WidgetStyle.Padding.Get(FMargin(0));
            if(Node.Slot.Padding==FMargin(-Padding.Left,-Padding.Top,-Padding.Right,-Padding.Bottom))OldGutter=Padding;
        }
        const FVector2D LogicalSize=Node.Effect.Shader.IsSet()?Node.Effect.Shader->Size:Size-GutterSize(OldGutter);
        const FVector2D OldSize=LogicalSize+GutterSize(OldGutter);
        if(Node.Children.Num()!=1||(OldGutter!=FMargin(0)&&Node.Children[0].Children.Num()!=1))
        {Error=TEXT("Retainer capture requires its existing single content surface: ")+Node.Name;return false;}
        FUMGWidgetNode& Content=OldGutter==FMargin(0)?Node.Children[0]:Node.Children[0].Children[0];
        FBox2D Paint;
        if(!PrepareNode(Content,LogicalSize,Paint,Error))return false;
        const bool ContentOutset=Paint.Min.X<0||Paint.Min.Y<0||Paint.Max.X>LogicalSize.X||Paint.Max.Y>LogicalSize.Y;
        if(IsBlur&&ContentOutset)
        {
            const FVector2D Radius=Node.Effect.EffectBlurUVStep.Get(FVector2D::ZeroVector)*OldSize;
            Paint=FBox2D(Paint.Min-Radius,Paint.Max+Radius);
        }
        if(IsShadow)
        {
            const FBox2D ContentPaint=Paint;
            for(const auto& Shadow:Node.Shadow.BoxShadows)
            {
                const double Radius=FMath::CeilToDouble(Shadow.Blur*1.5+FMath::Abs(Shadow.Spread))+1;
                Paint+=FBox2D(ContentPaint.Min+Shadow.Offset-FVector2D(Radius),ContentPaint.Max+Shadow.Offset+FVector2D(Radius));
            }
        }
        const FMargin Gutter=CaptureGutter(Paint,LogicalSize,OldGutter);
        const FVector2D NewSize=LogicalSize+GutterSize(Gutter);
        if(Node.Effect.Shader.IsSet())Node.Effect.Shader->CaptureInsets=FVector4(Gutter.Left,Gutter.Top,Gutter.Right,Gutter.Bottom);
        if(IsBlur&&Gutter!=OldGutter)Node.Effect.EffectBlurUVStep=Node.Effect.EffectBlurUVStep.GetValue()*OldSize/NewSize;
        const FUMGRenderStyleData Render=Node.Render;
        if(OldGutter==FMargin(0))
        {
            Node=UMGRetainerCapture::Wrap(MoveTemp(Node),LogicalSize,Gutter);
            OutPaint=TransformBounds(FBox2D(FVector2D(-Gutter.Left,-Gutter.Top),LogicalSize+FVector2D(Gutter.Right,Gutter.Bottom)),Render,LogicalSize);
        }
        else
        {
            Node.Bounds.W=NewSize.X;Node.Bounds.H=NewSize.Y;
            Node.Slot.Padding=FMargin(-Gutter.Left,-Gutter.Top,-Gutter.Right,-Gutter.Bottom);
            Node.Children[0].WidgetStyle.Padding=Gutter;
            OutPaint=TransformBounds(FBox2D(FVector2D::ZeroVector,NewSize),Render,NewSize);
        }
        return true;
    }
    FBox2D Paint(FVector2D::ZeroVector,Size);
    if(Node.Path.StrokeAlignment.IsSet()&&!Node.Path.Geometry.Get(FString()).IsEmpty())
    {
        FDesignPathRasterRequest Request;
        if(!FDesignPathRasterizer::TryBuildRequest(Node,Request))
        {Error=TEXT("Failed to prepare actual path capture bounds: ")+Node.Name;return false;}
        if(Request.HasExpandedCanvas())
        {
            const FVector2D Ratio(Size.X/Request.PaintSize.X,Size.Y/Request.PaintSize.Y);
            Paint+=FBox2D(-FVector2D(Request.PaintOffset)*Ratio,FVector2D(Request.Size-Request.PaintOffset)*Ratio);
        }
    }
    const FMargin Padding=Node.WidgetStyle.Padding.Get(FMargin(0));
    const FVector2D Inner=Size-GutterSize(Padding);
    const bool IsFlow=Node.Type==UMGWidgetTypes::HorizontalBox||Node.Type==UMGWidgetTypes::VerticalBox;
    for(auto& Child:Node.Children)
    {
        const FVector2D ChildSize=LayoutSize(Child,Inner-GutterSize(Child.Slot.Padding));
        FBox2D ChildPaint;
        if(!PrepareNode(Child,ChildSize,ChildPaint,Error))return false;
        if(!IsFlow)
        {
            FVector2D Offset(Child.Bounds.X+Padding.Left+Child.Slot.Padding.Left,Child.Bounds.Y+Padding.Top+Child.Slot.Padding.Top);
            if(Node.Type==UMGWidgetTypes::Overlay||Node.Type==UMGWidgetTypes::Border||Node.Type==UMGWidgetTypes::SizeBox)
            {
                const FVector2D Spare=Inner-GutterSize(Child.Slot.Padding)-ChildSize;
                if(Child.Slot.HorizontalAlignment==EUMGSlotHorizontalAlignment::Center)Offset.X+=Spare.X*0.5;
                else if(Child.Slot.HorizontalAlignment==EUMGSlotHorizontalAlignment::Right)Offset.X+=Spare.X;
                if(Child.Slot.VerticalAlignment==EUMGSlotVerticalAlignment::Center)Offset.Y+=Spare.Y*0.5;
                else if(Child.Slot.VerticalAlignment==EUMGSlotVerticalAlignment::Bottom)Offset.Y+=Spare.Y;
            }
            Paint+=FBox2D(ChildPaint.Min+Offset,ChildPaint.Max+Offset);
        }
        else
        {
            // Flow placement is resolved by Slate. Retain the existing edge-outset rule,
            // using actual child ink rather than guessing its final flow position.
            Paint+=FBox2D(FVector2D(FMath::Min(0.0,ChildPaint.Min.X),FMath::Min(0.0,ChildPaint.Min.Y)),
                Size+FVector2D(FMath::Max(0.0,ChildPaint.Max.X-ChildSize.X),FMath::Max(0.0,ChildPaint.Max.Y-ChildSize.Y)));
        }
    }
    if(IsRetainer||Node.Render.Clipping.Get(FString())==TEXT("ClipToBounds"))Paint=ClipBounds(Paint,Size);
    OutPaint=TransformBounds(Paint,Node.Render,Size);return true;
}
}

bool RetainerCaptureBounds::Prepare(FUMGWidgetNode& Root,FString& Error)
{
    Error.Reset();FBox2D Paint;return PrepareNode(Root,FVector2D(Root.Bounds.W,Root.Bounds.H),Paint,Error);
}
