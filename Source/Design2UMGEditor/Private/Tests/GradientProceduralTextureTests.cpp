// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "ProceduralShapeGenerator.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"

#include "Engine/Texture2D.h"

namespace
{
	bool ReadGeneratedPixel(UTexture2D* Texture, const int32 X, const int32 Y, FColor& OutPixel)
	{
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() <= 0)
		{
			return false;
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		if (X < 0 || Y < 0 || X >= Mip.SizeX || Y >= Mip.SizeY)
		{
			return false;
		}

		const void* LockedData = Mip.BulkData.LockReadOnly();
		if (!LockedData)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		const FColor* Pixels = static_cast<const FColor*>(LockedData);
		OutPixel = Pixels[Y * Mip.SizeX + X];
		Mip.BulkData.Unlock();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientProceduralLinearMultiStopTest,
	"Design2UMG.Gradient.ProceduralLinearMultiStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientProceduralLinearMultiStopTest::RunTest(const FString& Parameters)
{
	FUMGPropertyBag Properties;
	Properties.Add(UMGPropertyKeys::WidthOverride, TEXT("9"));
	Properties.Add(UMGPropertyKeys::HeightOverride, TEXT("9"));
	Properties.Add(UMGPropertyKeys::MaterialParams,
		TEXT("{\"Rotation\":45.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":0.5000,\"Color\":\"#00FF00FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}"));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(TEXT("gradientlinear"), Properties, GetTransientPackage());
	TestNotNull(TEXT("Linear multi-stop procedural texture is generated"), Texture);
	if (!Texture)
	{
		return false;
	}
	TestEqual(TEXT("Generated width"), Texture->GetSizeX(), 9);
	TestEqual(TEXT("Generated height"), Texture->GetSizeY(), 9);

	FColor StartPixel;
	FColor MiddlePixel;
	FColor EndPixel;
	TestTrue(TEXT("Read start pixel"), ReadGeneratedPixel(Texture, 0, 0, StartPixel));
	TestTrue(TEXT("Read middle pixel"), ReadGeneratedPixel(Texture, 4, 4, MiddlePixel));
	TestTrue(TEXT("Read end pixel"), ReadGeneratedPixel(Texture, 8, 8, EndPixel));
	TestTrue(TEXT("Arbitrary-angle gradient starts near first stop"), StartPixel.R > StartPixel.B && StartPixel.R > StartPixel.G);
	TestTrue(TEXT("Arbitrary-angle gradient samples middle stop"), MiddlePixel.G > MiddlePixel.R && MiddlePixel.G > MiddlePixel.B);
	TestTrue(TEXT("Arbitrary-angle gradient ends near last stop"), EndPixel.B > EndPixel.R && EndPixel.B > EndPixel.G);

	FProceduralShapeGenerator::ReleaseTransientTextures();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientProceduralRadialGeometryTest,
	"Design2UMG.Gradient.ProceduralRadialGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientProceduralRadialGeometryTest::RunTest(const FString& Parameters)
{
	FUMGPropertyBag Properties;
	Properties.Add(UMGPropertyKeys::WidthOverride, TEXT("17"));
	Properties.Add(UMGPropertyKeys::HeightOverride, TEXT("17"));
	Properties.Add(UMGPropertyKeys::MaterialParams,
		TEXT("{\"CenterX\":0.2500,\"CenterY\":0.5000,\"SizeWidth\":0.5000,\"SizeHeight\":1.2500,\"Rotation\":90.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":0.5000,\"Color\":\"#00FF00FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}"));

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(TEXT("radialgradient"), Properties, GetTransientPackage());
	TestNotNull(TEXT("Radial procedural texture is generated"), Texture);
	if (!Texture)
	{
		return false;
	}
	TestEqual(TEXT("Generated width"), Texture->GetSizeX(), 17);
	TestEqual(TEXT("Generated height"), Texture->GetSizeY(), 17);

	FColor CenterPixel;
	FColor EdgePixel;
	TestTrue(TEXT("Read center pixel"), ReadGeneratedPixel(Texture, 4, 8, CenterPixel));
	TestTrue(TEXT("Read edge pixel"), ReadGeneratedPixel(Texture, 16, 8, EdgePixel));
	TestTrue(TEXT("Radial center samples first stop"), CenterPixel.R > CenterPixel.G && CenterPixel.R > CenterPixel.B);
	TestTrue(TEXT("Radial size and center push far edge toward last stop"), EdgePixel.B > EdgePixel.R && EdgePixel.B > EdgePixel.G);

	FProceduralShapeGenerator::ReleaseTransientTextures();
	return true;
}

#endif // WITH_AUTOMATION_WORKER
