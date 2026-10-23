// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignShaderData.h"
#include "Dom/JsonObject.h"
TSharedRef<FJsonObject> DesignShaderJson::Serialize(const FDesignShaderData& Shader)
{
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("Url"), Shader.Url);
    Object->SetStringField(TEXT("BlendMode"), Shader.BlendMode);
    Object->SetNumberField(TEXT("Width"), Shader.Size.X);
    Object->SetNumberField(TEXT("Height"), Shader.Size.Y);
    Object->SetNumberField(TEXT("Opacity"), Shader.Opacity);
    Object->SetBoolField(TEXT("FillPaint"), Shader.bFillPaint);
    TArray<TSharedPtr<FJsonValue>> Capture;
    for(double V:{double(Shader.CaptureInsets.X),double(Shader.CaptureInsets.Y),double(Shader.CaptureInsets.Z),double(Shader.CaptureInsets.W)})Capture.Add(MakeShared<FJsonValueNumber>(V));
    Object->SetArrayField(TEXT("CaptureInsets"),Capture);
    auto Uniforms = MakeShared<FJsonObject>();
    TArray<FString> Names; Shader.Uniforms.GetKeys(Names); Names.Sort();
    for (const auto& Name : Names)
    {
        const auto& V = Shader.Uniforms[Name];
        if (V.Kind == FDesignShaderValue::EKind::String) Uniforms->SetStringField(Name,V.String);
        else if (V.Kind == FDesignShaderValue::EKind::Boolean) Uniforms->SetBoolField(Name,V.Boolean);
        else if (V.Numbers.Num()==1) Uniforms->SetNumberField(Name,V.Numbers[0]);
        else
        {
            TArray<TSharedPtr<FJsonValue>> Array;
            for(double Number:V.Numbers) Array.Add(MakeShared<FJsonValueNumber>(Number));
            Uniforms->SetArrayField(Name,Array);
        }
    }
    Object->SetObjectField(TEXT("Uniforms"),Uniforms);
    return Object;
}
bool DesignShaderJson::Parse(const TSharedPtr<FJsonObject>& Object, FDesignShaderData& Out)
{
    if (!Object.IsValid() || !Object->TryGetStringField(TEXT("Url"),Out.Url)) return false;
    Object->TryGetStringField(TEXT("BlendMode"),Out.BlendMode);
    Object->TryGetNumberField(TEXT("Width"),Out.Size.X);
    Object->TryGetNumberField(TEXT("Height"),Out.Size.Y);
    Object->TryGetNumberField(TEXT("Opacity"),Out.Opacity);
    Out.bFillPaint = false;
    Object->TryGetBoolField(TEXT("FillPaint"), Out.bFillPaint);
    const TArray<TSharedPtr<FJsonValue>>* Capture=nullptr;
    if(Object->TryGetArrayField(TEXT("CaptureInsets"),Capture))
    {
        if(Capture->Num()!=4)return false;
        double Values[4];for(int32 I=0;I<4;++I)if(!(*Capture)[I]->TryGetNumber(Values[I]))return false;
        Out.CaptureInsets=FVector4(Values[0],Values[1],Values[2],Values[3]);
    }
    const TSharedPtr<FJsonObject>* Uniforms=nullptr;
    if (!Object->TryGetObjectField(TEXT("Uniforms"),Uniforms)) return true;
    Out.Uniforms.Reset();
    for(const auto& Pair:(*Uniforms)->Values)
    {
        FDesignShaderValue V;
        if(Pair.Value->Type==EJson::String) {V.Kind=FDesignShaderValue::EKind::String;V.String=Pair.Value->AsString();}
        else if(Pair.Value->Type==EJson::Boolean) {V.Kind=FDesignShaderValue::EKind::Boolean;V.Boolean=Pair.Value->AsBool();}
        else if(Pair.Value->Type==EJson::Number) V.Numbers.Add(Pair.Value->AsNumber());
        else if(Pair.Value->Type==EJson::Array)
        {
            for(const auto& Item:Pair.Value->AsArray())
            {double N;if(!Item->TryGetNumber(N)||!FMath::IsFinite(N))return false;V.Numbers.Add(N);}
        }
        else return false;
        Out.Uniforms.Add(FString(*Pair.Key),MoveTemp(V));
    }
    return true;
}
