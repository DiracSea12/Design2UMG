// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
// Corner construction and space distribution adapted from figma-squircle:
// https://github.com/phamfoo/figma-squircle/tree/45622dc2e9b23d69a44be673d15d1152c50df8d8/src
// Figma geometry: https://www.figma.com/blog/desperately-seeking-squircles/
/*
MIT License
Copyright (c) 2021 Tien Pham
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "DesignRoundedContour.h"

namespace
{
	struct FCorner
	{
		double R = 0, P = 0, A = 0, B = 0, C = 0, D = 0, Arc = 0;
	};

	bool CornersFor(const FVector2D& Size, const FDesignCornerRadius& Source, FCorner (&Out)[4], FString& Error)
	{
		const FVector4 Values = Source.ToVector4();
		if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X <= 0 || Size.Y <= 0
			|| !FMath::IsFinite(Source.Smoothing) || Source.Smoothing < 0 || Source.Smoothing > 1)
		{ Error = TEXT("rounded primitive requires positive dimensions and smoothing in [0,1]"); return false; }
		double Radius[4], Budget[4] = {-1, -1, -1, -1};
		for (int32 I = 0; I < 4; ++I)
		{
			Radius[I] = Values[I];
			if (!FMath::IsFinite(Radius[I]) || Radius[I] < 0)
			{ Error = TEXT("rounded primitive requires finite nonnegative corner radii"); return false; }
		}
		if (Radius[0] == Radius[1] && Radius[1] == Radius[2] && Radius[2] == Radius[3])
			for (int32 I = 0; I < 4; ++I) Budget[I] = FMath::Min(Size.X, Size.Y) * 0.5;
		else
		{
			// Equal radii retain the upstream stable TL, TR, BL, BR allocation order.
			TArray<int32> Order = {0, 1, 3, 2};
			Order.StableSort([&](int32 A, int32 B) { return Radius[A] > Radius[B]; });
			const int32 Horizontal[4] = {1, 0, 3, 2}, Vertical[4] = {3, 2, 1, 0};
			for (int32 I : Order)
			{
				const auto Share = [&](int32 Adjacent, double Length)
				{
					if (Radius[I] == 0 && Radius[Adjacent] == 0) return 0.0;
					return Budget[Adjacent] >= 0 ? Length - Budget[Adjacent] : Length * Radius[I] / (Radius[I] + Radius[Adjacent]);
				};
				Budget[I] = FMath::Min(Share(Horizontal[I], Size.X), Share(Vertical[I], Size.Y));
				Radius[I] = FMath::Min(Radius[I], Budget[I]);
			}
		}
		for (int32 I = 0; I < 4; ++I)
		{
			auto& C = Out[I];
			C.R = FMath::Min(Radius[I], Budget[I]);
			if (C.R == 0) continue;
			const double S = FMath::Min(static_cast<double>(Source.Smoothing), Budget[I] / C.R - 1.0);
			C.P = FMath::Min((1.0 + S) * C.R, Budget[I]);
			C.Arc = FMath::Sin(FMath::DegreesToRadians(45.0 * (1.0 - S))) * C.R * FMath::Sqrt(2.0);
			const double Distance = C.R * FMath::Tan(FMath::DegreesToRadians(22.5 * S));
			C.C = Distance * FMath::Cos(FMath::DegreesToRadians(45.0 * S));
			C.D = C.C * FMath::Tan(FMath::DegreesToRadians(45.0 * S));
			C.B = (C.P - C.Arc - C.C - C.D) / 3.0;
			C.A = 2.0 * C.B;
		}
		return true;
	}

	FString Draw(const FVector2D& Size, const FVector2D& Origin, const FCorner (&Corners)[4])
	{
		FString Path = FString::Printf(TEXT("M%.9g %.9g "), Origin.X + Size.X - Corners[1].P, Origin.Y);
		const auto Corner = [&](const FCorner& C, int32 Quarter)
		{
			if (C.R == 0) return;
			const auto Turn = [&](double X, double Y)
			{
				switch (Quarter)
				{
				case 1: return FVector2D(-Y, X);
				case 2: return FVector2D(-X, -Y);
				case 3: return FVector2D(Y, -X);
				default: return FVector2D(X, Y);
				}
			};
			const auto Cubic = [&](FVector2D A, FVector2D B, FVector2D End)
			{
				Path += FString::Printf(TEXT("c%.9g %.9g %.9g %.9g %.9g %.9g "), A.X, A.Y, B.X, B.Y, End.X, End.Y);
			};
			Cubic(Turn(C.A, 0), Turn(C.A + C.B, 0), Turn(C.A + C.B + C.C, C.D));
			const auto Arc = Turn(C.Arc, C.Arc);
			Path += FString::Printf(TEXT("a%.9g %.9g 0 0 1 %.9g %.9g "), C.R, C.R, Arc.X, Arc.Y);
			Cubic(Turn(C.D, C.C), Turn(C.D, C.B + C.C), Turn(C.D, C.A + C.B + C.C));
		};
		Corner(Corners[1], 0);
		Path += FString::Printf(TEXT("L%.9g %.9g "), Origin.X + Size.X, Origin.Y + Size.Y - Corners[2].P);
		Corner(Corners[2], 1);
		Path += FString::Printf(TEXT("L%.9g %.9g "), Origin.X + Corners[3].P, Origin.Y + Size.Y);
		Corner(Corners[3], 2);
		Path += FString::Printf(TEXT("L%.9g %.9g "), Origin.X, Origin.Y + Corners[0].P);
		Corner(Corners[0], 3);
		return Path + TEXT("Z");
	}
}

bool DesignRoundedContour::Build(const FVector2D& Size, const FDesignCornerRadius& Corners, FDesignGeometryPath& Path, FString& Error)
{
	FCorner Values[4];
	if (!CornersFor(Size, Corners, Values, Error)) return false;
	Path.Path = Draw(Size, FVector2D::ZeroVector, Values);
	Path.FillRule = TEXT("nonzero");
	return true;
}

bool DesignRoundedContour::BuildInsideStroke(const FVector2D& Size, const FDesignCornerRadius& Corners,
	const FDesignStrokeThickness& Thickness, FDesignGeometryPath& Path, FString& Error)
{
	if (Corners.Smoothing != 0 || Thickness.bUniform)
	{ Error = TEXT("independent primitive stroke edges require the supported circular corner junctions"); return false; }
	FCorner Outer[4];
	if (!CornersFor(Size, Corners, Outer, Error)) return false;
	const double Horizontal[4] = {Thickness.Left, Thickness.Right, Thickness.Right, Thickness.Left};
	const double Vertical[4] = {Thickness.Top, Thickness.Top, Thickness.Bottom, Thickness.Bottom};
	FDesignCornerRadius Inner;
	Inner.bUniform = false;
	double R[4];
	for (int32 I = 0; I < 4; ++I)
	{
		if (Outer[I].R > 0 && Horizontal[I] != Vertical[I])
		{ Error = TEXT("primitive edge widths require an unimplemented elliptical corner junction"); return false; }
		R[I] = FMath::Max(0.0, Outer[I].R - Horizontal[I]);
	}
	Inner.TopLeft = static_cast<float>(R[0]); Inner.TopRight = static_cast<float>(R[1]);
	Inner.BottomRight = static_cast<float>(R[2]); Inner.BottomLeft = static_cast<float>(R[3]);
	Path.Path = Draw(Size, FVector2D::ZeroVector, Outer);
	const FVector2D InnerSize(Size.X - Thickness.Left - Thickness.Right, Size.Y - Thickness.Top - Thickness.Bottom);
	if (InnerSize.X > 0 && InnerSize.Y > 0)
	{
		FCorner Inside[4];
		if (!CornersFor(InnerSize, Inner, Inside, Error)) return false;
		Path.Path += TEXT(" ") + Draw(InnerSize, FVector2D(Thickness.Left, Thickness.Top), Inside);
	}
	Path.FillRule = TEXT("evenodd");
	return true;
}
