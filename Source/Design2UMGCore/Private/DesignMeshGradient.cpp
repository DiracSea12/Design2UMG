// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "DesignMeshGradient.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/Archive.h"

namespace
{
    TSharedRef<FJsonValue> VectorValue(const FVector2D& V)
    {
        return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y)});
    }
    bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector2D& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        double X, Y;
        if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 2
            || !(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y)
            || !FMath::IsFinite(X) || !FMath::IsFinite(Y)) return false;
        Out = FVector2D(X, Y);
        return true;
    }
}

TSharedRef<FJsonObject> DesignMeshGradientJson::Serialize(const FDesignMeshGradient& Mesh)
{
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("Columns"), Mesh.Columns);
    Object->SetNumberField(TEXT("Rows"), Mesh.Rows);
    Object->SetNumberField(TEXT("Opacity"), Mesh.Opacity);
    TArray<TSharedPtr<FJsonValue>> Vertices;
    for (const auto& Vertex : Mesh.Vertices)
    {
        auto V = MakeShared<FJsonObject>();
        V->SetField(TEXT("Position"), VectorValue(Vertex.Position));
        V->SetField(TEXT("Left"), VectorValue(Vertex.LeftHandle));
        V->SetField(TEXT("Right"), VectorValue(Vertex.RightHandle));
        V->SetField(TEXT("Top"), VectorValue(Vertex.TopHandle));
        V->SetField(TEXT("Bottom"), VectorValue(Vertex.BottomHandle));
        V->SetStringField(TEXT("Color"), Vertex.Color.ToHex());
        Vertices.Add(MakeShared<FJsonValueObject>(V));
    }
    Object->SetArrayField(TEXT("Vertices"), Vertices);
    return Object;
}

bool DesignMeshGradientJson::Parse(const TSharedPtr<FJsonObject>& Object, FDesignMeshGradient& OutMesh)
{
    FDesignMeshGradient Mesh;
    const TArray<TSharedPtr<FJsonValue>>* Vertices = nullptr;
    if (!Object.IsValid() || !Object->TryGetNumberField(TEXT("Columns"), Mesh.Columns)
        || !Object->TryGetNumberField(TEXT("Rows"), Mesh.Rows)
        || !Object->TryGetNumberField(TEXT("Opacity"), Mesh.Opacity) || !FMath::IsFinite(Mesh.Opacity)
        || !Object->TryGetArrayField(TEXT("Vertices"), Vertices)) return false;
    for (const auto& Value : *Vertices)
    {
        const TSharedPtr<FJsonObject>* V = nullptr;
        FString Color;
        FDesignMeshGradientVertex Vertex;
        if (!Value->TryGetObject(V) || !V || !V->IsValid()
            || !ReadVector(*V, TEXT("Position"), Vertex.Position)
            || !ReadVector(*V, TEXT("Left"), Vertex.LeftHandle)
            || !ReadVector(*V, TEXT("Right"), Vertex.RightHandle)
            || !ReadVector(*V, TEXT("Top"), Vertex.TopHandle)
            || !ReadVector(*V, TEXT("Bottom"), Vertex.BottomHandle)
            || !(*V)->TryGetStringField(TEXT("Color"), Color) || Color.Len() != 8) return false;
        Vertex.Color = FColor::FromHex(Color);
        Mesh.Vertices.Add(Vertex);
    }
    if (!Mesh.IsValid()) return false;
    OutMesh = MoveTemp(Mesh);
    return true;
}

FString DesignMeshGradientJson::SerializeIdentity(const FDesignMeshGradient& Mesh)
{
    FString Result;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
    FJsonSerializer::Serialize(Serialize(Mesh), Writer);
    return Result;
}

void FDesignMeshGradient::WriteSignature(FArchive& Ar) const
{
    int32 ColumnCount = Columns, RowCount = Rows, Count = Vertices.Num();
    float PaintOpacity = Opacity;
    Ar << ColumnCount << RowCount << PaintOpacity << Count;
    for (auto Vertex : Vertices)
        Ar << Vertex.Position << Vertex.LeftHandle << Vertex.RightHandle << Vertex.TopHandle << Vertex.BottomHandle << Vertex.Color;
}
