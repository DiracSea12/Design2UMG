#pragma once

#include "CoreMinimal.h"

class UObject;
class UTexture2D;

class FProceduralShapeGenerator
{
public:
	static UTexture2D* GenerateFromMaterialRef(const FString& MaterialRef, const TMap<FString, FString>& Properties, UObject* Outer = nullptr);
	static UTexture2D* ApplyAffineCrop(UTexture2D* Source, float A, float B, float TX, float C, float D, float TY, int32 OutW, int32 OutH);
	static void ReleaseTransientTextures();
	static void FlushPendingTextureUpdates();
};
