// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "ProceduralShapeGenerator.h"
#include "UMGPropertyKeys.h"
#include "UMGSchema.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

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
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("gradientlinear");
	Request.WidthOverride = 9.0f;
	Request.HeightOverride = 9.0f;
	Request.MaterialParams = TEXT("{\"Rotation\":45.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":0.5000,\"Color\":\"#00FF00FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}");

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(Request, GetTransientPackage());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientProceduralLinearEndpointsTest,
	"Design2UMG.Gradient.ProceduralLinearEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientProceduralLinearEndpointsTest::RunTest(const FString& Parameters)
{
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("gradientlinear");
	Request.WidthOverride = 9.0f;
	Request.HeightOverride = 9.0f;
	Request.MaterialParams = TEXT("{\"StartX\":0.2500,\"StartY\":0.5000,\"EndX\":0.7500,\"EndY\":0.5000,\"Rotation\":0.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}");

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(Request, GetTransientPackage());
	TestNotNull(TEXT("Linear endpoint procedural texture is generated"), Texture);
	if (!Texture)
	{
		return false;
	}

	FColor BeforeStart;
	FColor AtMiddle;
	FColor AfterEnd;
	TestTrue(TEXT("Read pixel before linear start"), ReadGeneratedPixel(Texture, 1, 4, BeforeStart));
	TestTrue(TEXT("Read pixel at linear midpoint"), ReadGeneratedPixel(Texture, 4, 4, AtMiddle));
	TestTrue(TEXT("Read pixel after linear end"), ReadGeneratedPixel(Texture, 7, 4, AfterEnd));
	TestTrue(TEXT("Pixels before P0 keep the first stop"), BeforeStart.R > 245 && BeforeStart.B < 10);
	TestTrue(TEXT("Pixel halfway between P0 and P1 blends both stops"), AtMiddle.R > 80 && AtMiddle.B > 80);
	TestTrue(TEXT("Pixels after P1 keep the last stop"), AfterEnd.B > 245 && AfterEnd.R < 10);

	FProceduralShapeGenerator::ReleaseTransientTextures();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientProceduralLinearEndpointsAspectRatioTest,
	"Design2UMG.Gradient.ProceduralLinearEndpointsAspectRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientProceduralLinearEndpointsAspectRatioTest::RunTest(const FString& Parameters)
{
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("gradientlinear");
	Request.WidthOverride = 33.0f;
	Request.HeightOverride = 5.0f;
	Request.MaterialParams = TEXT("{\"StartX\":0.0000,\"StartY\":0.0000,\"EndX\":1.0000,\"EndY\":1.0000,\"Rotation\":45.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}");

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(Request, GetTransientPackage());
	TestNotNull(TEXT("Non-square linear endpoint texture is generated"), Texture);
	if (!Texture)
	{
		return false;
	}

	FColor Pixel;
	TestTrue(TEXT("Read non-square gradient sample"), ReadGeneratedPixel(Texture, 24, 0, Pixel));
	TestTrue(TEXT("Endpoint projection accounts for texture aspect ratio"), Pixel.B > Pixel.R);

	FProceduralShapeGenerator::ReleaseTransientTextures();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGradientProceduralRadialGeometryTest,
	"Design2UMG.Gradient.ProceduralRadialGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGradientProceduralRadialGeometryTest::RunTest(const FString& Parameters)
{
	FUMGProceduralResourceRequest Request;
	Request.MaterialRef = TEXT("radialgradient");
	Request.WidthOverride = 17.0f;
	Request.HeightOverride = 17.0f;
	Request.MaterialParams = TEXT("{\"CenterX\":0.2500,\"CenterY\":0.5000,\"SizeWidth\":0.5000,\"SizeHeight\":1.2500,\"Rotation\":90.0000,\"Stops\":[{\"Pos\":0.0000,\"Color\":\"#FF0000FF\"},{\"Pos\":0.5000,\"Color\":\"#00FF00FF\"},{\"Pos\":1.0000,\"Color\":\"#0000FFFF\"}]}");

	UTexture2D* Texture = FProceduralShapeGenerator::GenerateFromMaterialRef(Request, GetTransientPackage());
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
