// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "Design2UMGLog.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Hash/CityHash.h"
#include "RenderingThread.h"
#include "TextureResource.h"

// --- Path cache (file-local) ---
struct FPathPoint
{
	float X = 0.0f;
	float Y = 0.0f;
};

void SampleCubicBezier(
	const FPathPoint& P0, const FPathPoint& P1,
	const FPathPoint& P2, const FPathPoint& P3,
	TArray<FPathPoint>& OutPoints, int32 Segments = 16);
void SampleQuadBezier(
	const FPathPoint& P0, const FPathPoint& P1, const FPathPoint& P2,
	TArray<FPathPoint>& OutPoints, int32 Segments = 12);
void SampleArc(
	const FPathPoint& From, float Rx, float Ry, float Rotation,
	bool bLargeArc, bool bSweep, const FPathPoint& To,
	TArray<FPathPoint>& OutPoints, int32 Segments = 24);
void ScanlineFill(
	const TArray<TArray<FPathPoint>>& SubPaths,
	TArray<FColor>& OutPixels,
	int32 Width, int32 Height,
	FColor FillColor,
	bool bEvenOdd);

namespace
{
	// Cache entry keyed by a 64-bit CityHash of the full parameter signature. The signature is
	// stored alongside the texture so a hit requires an exact match: key collisions degrade to
	// a miss instead of silently returning another parameter set's texture.
	struct FPathCacheEntry
	{
		TWeakObjectPtr<UTexture2D> Texture;
		FString Signature;
	};

	FString MakeCacheSignature(
		const FString& Geometry,
		int32 W,
		int32 H,
		const TOptional<FLinearColor>& FillColor,
		const FString& Rule,
		const TOptional<FLinearColor>& StrokeColor,
		float StrokeWidth,
		const FString& StrokeCap,
		const FString& StrokeJoin)
	{
		const auto FormatColor = [](const TOptional<FLinearColor>& Color) -> FString
		{
			return Color.IsSet()
				? FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), Color->R, Color->G, Color->B, Color->A)
				: FString(TEXT("unset"));
		};
		return FString::Printf(
			TEXT("%s|%d|%d|%s|%s|%s|%.4f|%s|%s"),
			*Geometry, W, H,
			*FormatColor(FillColor), *Rule,
			*FormatColor(StrokeColor), StrokeWidth,
			*StrokeCap, *StrokeJoin);
	}

	uint64 MakeCacheKey(const FString& Signature)
	{
		return CityHash64(reinterpret_cast<const char*>(*Signature), Signature.Len() * sizeof(TCHAR));
	}

	TMap<uint64, FPathCacheEntry> GPathCache;
	int32 GCacheHits = 0;
	int32 GCacheMisses = 0;
	bool bHasPendingTextureUpdates = false;
	// Upper bound on cached path textures so repeated rasterization with varying shape
	// parameters cannot grow the cache (and its rooted UTexture2Ds) without limit.
	// EvictStale() first drops entries whose texture is already GC'd, then removes
	// arbitrary map entries while over capacity - there is no recency tracking, so
	// overflow eviction is unordered, only bounded. 256 is a fixed round capacity
	// chosen when the cache was introduced, not a measured optimum.
	constexpr int32 GMaxCacheEntries = 256;

	int64 CountFilledPixels(const TArray<FColor>& Pixels)
	{
		int64 Count = 0;
		for (const FColor& Pixel : Pixels)
		{
			if (Pixel.A > 0)
			{
				++Count;
			}
		}
		return Count;
	}

	bool HasVisibleColor(const TOptional<FLinearColor>& Color)
	{
		return Color.IsSet() && Color->A > KINDA_SMALL_NUMBER;
	}

	bool IsClosedPath(const TArray<FPathPoint>& Path)
	{
		if (Path.Num() < 3)
		{
			return false;
		}

		const FPathPoint& First = Path[0];
		const FPathPoint& Last = Path.Last();
		return FMath::Abs(First.X - Last.X) <= KINDA_SMALL_NUMBER
			&& FMath::Abs(First.Y - Last.Y) <= KINDA_SMALL_NUMBER;
	}

	void DrawFilledCircle(
		TArray<FColor>& InOutPixels,
		int32 Width,
		int32 Height,
		const FVector2D& Center,
		float Radius,
		const FColor& Color)
	{
		if (Radius <= 0.0f || Color.A == 0)
		{
			return;
		}

		const float RadiusSq = Radius * Radius;
		const int32 MinX = FMath::Max(0, FMath::FloorToInt(Center.X - Radius - 1.0f));
		const int32 MaxX = FMath::Min(Width - 1, FMath::CeilToInt(Center.X + Radius + 1.0f));
		const int32 MinY = FMath::Max(0, FMath::FloorToInt(Center.Y - Radius - 1.0f));
		const int32 MaxY = FMath::Min(Height - 1, FMath::CeilToInt(Center.Y + Radius + 1.0f));

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2D Sample(X + 0.5f, Y + 0.5f);
				if ((Sample - Center).SizeSquared() <= RadiusSq)
				{
					InOutPixels[Y * Width + X] = Color;
				}
			}
		}
	}

	bool IsPointInsideStrokeSegment(
		const FVector2D& Point,
		const FVector2D& Start,
		const FVector2D& End,
		float Radius,
		bool bRoundCap,
		bool bSquareCap)
	{
		const FVector2D Segment = End - Start;
		const float SegmentLengthSq = Segment.SizeSquared();
		if (SegmentLengthSq <= KINDA_SMALL_NUMBER)
		{
			return (Point - Start).SizeSquared() <= Radius * Radius;
		}

		if (bSquareCap)
		{
			const FVector2D Direction = Segment.GetSafeNormal();
			const FVector2D ExtendedStart = Start - Direction * Radius;
			const FVector2D ExtendedEnd = End + Direction * Radius;
			const FVector2D ExtendedSegment = ExtendedEnd - ExtendedStart;
			const float ExtendedLengthSq = ExtendedSegment.SizeSquared();
			const float T = FMath::Clamp(FVector2D::DotProduct(Point - ExtendedStart, ExtendedSegment) / ExtendedLengthSq, 0.0f, 1.0f);
			const FVector2D ClosestPoint = ExtendedStart + ExtendedSegment * T;
			return (Point - ClosestPoint).SizeSquared() <= Radius * Radius;
		}

		const float RawT = FVector2D::DotProduct(Point - Start, Segment) / SegmentLengthSq;
		if (!bRoundCap && (RawT < 0.0f || RawT > 1.0f))
		{
			return false;
		}

		const float T = bRoundCap ? FMath::Clamp(RawT, 0.0f, 1.0f) : RawT;
		const FVector2D ClosestPoint = Start + Segment * T;
		return (Point - ClosestPoint).SizeSquared() <= Radius * Radius;
	}

	void RasterizeStroke(
		const TArray<TArray<FPathPoint>>& SubPaths,
		TArray<FColor>& InOutPixels,
		int32 Width,
		int32 Height,
		const FColor& StrokeColor,
		float StrokeWidth,
		const FString& StrokeCap,
		const FString& StrokeJoin)
	{
		if (StrokeColor.A == 0 || StrokeWidth <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float Radius = StrokeWidth * 0.5f;
		const bool bRoundCap = StrokeCap.Equals(TEXT("round"), ESearchCase::IgnoreCase);
		const bool bSquareCap = StrokeCap.Equals(TEXT("square"), ESearchCase::IgnoreCase);
		const bool bRoundJoin = StrokeJoin.Equals(TEXT("round"), ESearchCase::IgnoreCase);

		for (const TArray<FPathPoint>& Path : SubPaths)
		{
			if (Path.Num() < 2)
			{
				continue;
			}

			const bool bClosed = IsClosedPath(Path);
			const int32 LastPointIndex = Path.Num() - 1;
			for (int32 Index = 0; Index < LastPointIndex; ++Index)
			{
				const FVector2D Start(Path[Index].X, Path[Index].Y);
				const FVector2D End(Path[Index + 1].X, Path[Index + 1].Y);
				if ((End - Start).SizeSquared() <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const int32 MinX = FMath::Max(0, FMath::FloorToInt(FMath::Min(Start.X, End.X) - Radius - 1.0f));
				const int32 MaxX = FMath::Min(Width - 1, FMath::CeilToInt(FMath::Max(Start.X, End.X) + Radius + 1.0f));
				const int32 MinY = FMath::Max(0, FMath::FloorToInt(FMath::Min(Start.Y, End.Y) - Radius - 1.0f));
				const int32 MaxY = FMath::Min(Height - 1, FMath::CeilToInt(FMath::Max(Start.Y, End.Y) + Radius + 1.0f));

				for (int32 Y = MinY; Y <= MaxY; ++Y)
				{
					for (int32 X = MinX; X <= MaxX; ++X)
					{
						const FVector2D Sample(X + 0.5f, Y + 0.5f);
						if (IsPointInsideStrokeSegment(Sample, Start, End, Radius, bRoundCap, bSquareCap))
						{
							InOutPixels[Y * Width + X] = StrokeColor;
						}
					}
				}
			}

			if (bRoundJoin)
			{
				const int32 JoinStartIndex = bClosed ? 0 : 1;
				// Closed paths duplicate the first point as their last entry (see IsClosedPath),
				// so all distinct vertices are already covered by 0..Last-1 and the end index
				// needs no closed/open distinction.
				const int32 JoinEndIndex = LastPointIndex - 1;
				for (int32 Index = JoinStartIndex; Index <= JoinEndIndex; ++Index)
				{
					DrawFilledCircle(
						InOutPixels,
						Width,
						Height,
						FVector2D(Path[Index].X, Path[Index].Y),
						Radius,
						StrokeColor);
				}
			}
		}
	}

	void EvictStale()
	{
		for (auto It = GPathCache.CreateIterator(); It; ++It)
		{
			if (!It->Value.Texture.IsValid())
				It.RemoveCurrent();
		}
		if (GPathCache.Num() > GMaxCacheEntries)
		{
			int32 ToRemove = GPathCache.Num() - GMaxCacheEntries;
			for (auto It = GPathCache.CreateIterator(); ToRemove > 0 && It; ++It, --ToRemove)
			{
				if (UTexture2D* Tex = It->Value.Texture.Get())
					Tex->RemoveFromRoot();
				It.RemoveCurrent();
			}
		}
	}

	FAutoConsoleCommand GCacheStatsCmd(
		TEXT("Design2UMG.CacheStats"),
		TEXT("Print path rasterization cache statistics"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			int32 Total = GCacheHits + GCacheMisses;
			float Rate = Total > 0 ? 100.0f * GCacheHits / Total : 0.0f;
			UE_LOG(LogDesign2UMG, Display,
				TEXT("PathCache: Entries=%d, Hits=%d, Misses=%d, HitRate=%.1f%%"),
				GPathCache.Num(), GCacheHits, GCacheMisses, Rate);
		}));

	FAutoConsoleCommand GCacheClearCmd(
		TEXT("Design2UMG.CacheClear"),
		TEXT("Clear path rasterization cache"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FDesignPathRasterizer::ClearCache();
			UE_LOG(LogDesign2UMG, Log, TEXT("DesignPathRasterizer: cache cleared"));
		}));
}

namespace
{
	float ParseFloat(const TCHAR*& P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		return FCString::Atof(P);
	}

	void AdvancePastNumber(const TCHAR*& P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		if (*P == TEXT('-') || *P == TEXT('+'))
			++P;
		bool bHasDot = false;
		while ((*P >= TEXT('0') && *P <= TEXT('9')) || (*P == TEXT('.') && !bHasDot))
		{
			if (*P == TEXT('.')) bHasDot = true;
			++P;
		}
		// scientific notation
		if (*P == TEXT('e') || *P == TEXT('E'))
		{
			++P;
			if (*P == TEXT('-') || *P == TEXT('+')) ++P;
			while (*P >= TEXT('0') && *P <= TEXT('9')) ++P;
		}
	}

	float ReadFloat(const TCHAR*& P)
	{
		float V = ParseFloat(P);
		AdvancePastNumber(P);
		return V;
	}

	bool ReadFlag(const TCHAR*& P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		bool V = (*P == TEXT('1'));
		if (*P == TEXT('0') || *P == TEXT('1'))
			++P;
		return V;
	}

	bool IsCommandChar(TCHAR C)
	{
		return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'));
	}

	bool IsNumberStart(TCHAR C)
	{
		return (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-') || C == TEXT('+') || C == TEXT('.');
	}

	bool HasMoreNumbers(const TCHAR* P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		return *P && !IsCommandChar(*P) && IsNumberStart(*P);
	}
}

bool ParsePathData(const FString& Data, TArray<TArray<FPathPoint>>& OutSubPaths, FBox2D& OutBounds)
{
	OutBounds.Init();
	const TCHAR* P = *Data;
	FPathPoint Current = {0, 0};
	FPathPoint Start = {0, 0};
	FPathPoint LastControl = {0, 0}; // S/T commands mirror the previous control point.
	TCHAR LastCmd = 0;
	TArray<FPathPoint>* ActivePath = nullptr;

	auto EnsurePath = [&]()
	{
		if (!ActivePath)
		{
			ActivePath = &OutSubPaths.AddDefaulted_GetRef();
			ActivePath->Add(Current);
		}
	};

	auto AddPoint = [&](float X, float Y)
	{
		Current = {X, Y};
		EnsurePath();
		ActivePath->Add(Current);
		OutBounds += FVector2D(X, Y);
	};

	bool bLastRelative = false;

	while (*P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		if (!*P) break;

		TCHAR Cmd = *P;
		if (!IsCommandChar(Cmd))
		{
			// Implicit lineto inherits the previous command's relative/absolute mode.
			if (ActivePath)
			{
				float X = ReadFloat(P);
				float Y = ReadFloat(P);
				if (bLastRelative) { X += Current.X; Y += Current.Y; }
				AddPoint(X, Y);
				continue;
			}
			++P;
			continue;
		}
		++P;

		bool bRelative = (Cmd >= TEXT('a') && Cmd <= TEXT('z'));
		bLastRelative = bRelative;
		TCHAR CmdUpper = bRelative ? (Cmd - 32) : Cmd;

		auto Abs = [&](float V, float Base) -> float { return bRelative ? Base + V : V; };

		switch (CmdUpper)
		{
		case TEXT('M'):
		{
			float X = ReadFloat(P);
			float Y = ReadFloat(P);
			X = Abs(X, Current.X);
			Y = Abs(Y, Current.Y);
			Current = {X, Y};
			Start = Current;
			ActivePath = &OutSubPaths.AddDefaulted_GetRef();
			ActivePath->Add(Current);
			OutBounds += FVector2D(X, Y);
			// subsequent coords are implicit lineto
			while (HasMoreNumbers(P))
			{
				X = ReadFloat(P);
				Y = ReadFloat(P);
				AddPoint(Abs(X, Current.X), Abs(Y, Current.Y));
			}
			LastCmd = TEXT('M');
			break;
		}
		case TEXT('L'):
			while (HasMoreNumbers(P))
			{
				float X = ReadFloat(P);
				float Y = ReadFloat(P);
				AddPoint(Abs(X, Current.X), Abs(Y, Current.Y));
			}
			LastCmd = TEXT('L');
			break;
		case TEXT('H'):
			while (HasMoreNumbers(P))
			{
				float X = ReadFloat(P);
				AddPoint(Abs(X, Current.X), Current.Y);
			}
			LastCmd = TEXT('H');
			break;
		case TEXT('V'):
			while (HasMoreNumbers(P))
			{
				float Y = ReadFloat(P);
				AddPoint(Current.X, Abs(Y, Current.Y));
			}
			LastCmd = TEXT('V');
			break;
		case TEXT('C'):
			while (HasMoreNumbers(P))
			{
				FPathPoint P1 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				FPathPoint P2 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				FPathPoint P3 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				EnsurePath();
				SampleCubicBezier(Current, P1, P2, P3, *ActivePath);
				LastControl = P2;
				Current = P3;
				OutBounds += FVector2D(P3.X, P3.Y);
			}
			LastCmd = TEXT('C');
			break;
		case TEXT('S'):
			while (HasMoreNumbers(P))
			{
				// Mirror previous cubic control point; fallback to current point when no prior C/S.
				FPathPoint P1 = (LastCmd == TEXT('C') || LastCmd == TEXT('S'))
					? FPathPoint{2 * Current.X - LastControl.X, 2 * Current.Y - LastControl.Y}
					: Current;
				FPathPoint P2 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				FPathPoint P3 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				EnsurePath();
				SampleCubicBezier(Current, P1, P2, P3, *ActivePath);
				LastControl = P2;
				Current = P3;
				OutBounds += FVector2D(P3.X, P3.Y);
				LastCmd = TEXT('S');
			}
			break;
		case TEXT('Q'):
			while (HasMoreNumbers(P))
			{
				FPathPoint P1 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				FPathPoint P2 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				EnsurePath();
				SampleQuadBezier(Current, P1, P2, *ActivePath);
				LastControl = P1;
				Current = P2;
				OutBounds += FVector2D(P2.X, P2.Y);
			}
			LastCmd = TEXT('Q');
			break;
		case TEXT('T'):
			while (HasMoreNumbers(P))
			{
				FPathPoint P1 = (LastCmd == TEXT('Q') || LastCmd == TEXT('T'))
					? FPathPoint{2 * Current.X - LastControl.X, 2 * Current.Y - LastControl.Y}
					: Current;
				FPathPoint P2 = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				EnsurePath();
				SampleQuadBezier(Current, P1, P2, *ActivePath);
				LastControl = P1;
				Current = P2;
				OutBounds += FVector2D(P2.X, P2.Y);
				LastCmd = TEXT('T');
			}
			break;
		case TEXT('A'):
			while (HasMoreNumbers(P))
			{
				float Rx = ReadFloat(P);
				float Ry = ReadFloat(P);
				float Rot = ReadFloat(P);
				bool bLarge = ReadFlag(P);
				bool bSweep = ReadFlag(P);
				FPathPoint To = {Abs(ReadFloat(P), Current.X), Abs(ReadFloat(P), Current.Y)};
				EnsurePath();
				SampleArc(Current, Rx, Ry, Rot, bLarge, bSweep, To, *ActivePath);
				Current = To;
				OutBounds += FVector2D(To.X, To.Y);
			}
			LastCmd = TEXT('A');
			break;
		case TEXT('Z'):
			if (ActivePath && ActivePath->Num() > 0)
			{
				ActivePath->Add(Start);
			}
			Current = Start;
			ActivePath = nullptr;
			LastCmd = TEXT('Z');
			break;
		default:
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignPathRasterizer: unsupported SVG command '%c'"), Cmd);
			// Skip trailing numbers so they don't get parsed as implicit lineto
			while (HasMoreNumbers(P))
			{
				ReadFloat(P);
			}
			break;
		}
	}

	return OutSubPaths.Num() > 0;
}

void SampleCubicBezier(
	const FPathPoint& P0, const FPathPoint& P1,
	const FPathPoint& P2, const FPathPoint& P3,
	TArray<FPathPoint>& OutPoints, int32 Segments)
{
	for (int32 i = 1; i <= Segments; ++i)
	{
		float T = static_cast<float>(i) / Segments;
		float U = 1.0f - T;
		float X = U*U*U*P0.X + 3*U*U*T*P1.X + 3*U*T*T*P2.X + T*T*T*P3.X;
		float Y = U*U*U*P0.Y + 3*U*U*T*P1.Y + 3*U*T*T*P2.Y + T*T*T*P3.Y;
		OutPoints.Add({X, Y});
	}
}

void SampleQuadBezier(
	const FPathPoint& P0, const FPathPoint& P1, const FPathPoint& P2,
	TArray<FPathPoint>& OutPoints, int32 Segments)
{
	for (int32 i = 1; i <= Segments; ++i)
	{
		float T = static_cast<float>(i) / Segments;
		float U = 1.0f - T;
		float X = U*U*P0.X + 2*U*T*P1.X + T*T*P2.X;
		float Y = U*U*P0.Y + 2*U*T*P1.Y + T*T*P2.Y;
		OutPoints.Add({X, Y});
	}
}

void SampleArc(
	const FPathPoint& From, float Rx, float Ry, float Rotation,
	bool bLargeArc, bool bSweep, const FPathPoint& To,
	TArray<FPathPoint>& OutPoints, int32 Segments)
{
	// Degenerate cases
	if (FMath::IsNearlyZero(Rx) || FMath::IsNearlyZero(Ry))
	{
		OutPoints.Add(To);
		return;
	}

	Rx = FMath::Abs(Rx);
	Ry = FMath::Abs(Ry);

	float CosR = FMath::Cos(FMath::DegreesToRadians(Rotation));
	float SinR = FMath::Sin(FMath::DegreesToRadians(Rotation));

	// Step 1: compute (x1', y1')
	float Dx = (From.X - To.X) / 2.0f;
	float Dy = (From.Y - To.Y) / 2.0f;
	float X1p = CosR * Dx + SinR * Dy;
	float Y1p = -SinR * Dx + CosR * Dy;

	// Step 2: compute (cx', cy')
	float X1pSq = X1p * X1p;
	float Y1pSq = Y1p * Y1p;
	float RxSq = Rx * Rx;
	float RySq = Ry * Ry;

	// Ensure radii are large enough
	float Lambda = X1pSq / RxSq + Y1pSq / RySq;
	if (Lambda > 1.0f)
	{
		float SqrtL = FMath::Sqrt(Lambda);
		Rx *= SqrtL;
		Ry *= SqrtL;
		RxSq = Rx * Rx;
		RySq = Ry * Ry;
	}

	float Num = FMath::Max(0.0f, RxSq * RySq - RxSq * Y1pSq - RySq * X1pSq);
	float Den = RxSq * Y1pSq + RySq * X1pSq;
	float Sq = (Den > 0.0f) ? FMath::Sqrt(Num / Den) : 0.0f;
	if (bLargeArc == bSweep) Sq = -Sq;

	float Cxp = Sq * Rx * Y1p / Ry;
	float Cyp = -Sq * Ry * X1p / Rx;

	// Step 3: compute (cx, cy)
	float Cx = CosR * Cxp - SinR * Cyp + (From.X + To.X) / 2.0f;
	float Cy = SinR * Cxp + CosR * Cyp + (From.Y + To.Y) / 2.0f;

	// Step 4: compute angles
	auto Angle = [](float Ux, float Uy, float Vx, float Vy) -> float
	{
		float Dot = Ux * Vx + Uy * Vy;
		float Len = FMath::Sqrt((Ux*Ux + Uy*Uy) * (Vx*Vx + Vy*Vy));
		float A = (Len > 0.0f) ? FMath::Acos(FMath::Clamp(Dot / Len, -1.0f, 1.0f)) : 0.0f;
		if (Ux * Vy - Uy * Vx < 0) A = -A;
		return A;
	};

	float Theta1 = Angle(1, 0, (X1p - Cxp) / Rx, (Y1p - Cyp) / Ry);
	float DTheta = Angle((X1p - Cxp) / Rx, (Y1p - Cyp) / Ry, (-X1p - Cxp) / Rx, (-Y1p - Cyp) / Ry);

	if (!bSweep && DTheta > 0) DTheta -= 2 * PI;
	if (bSweep && DTheta < 0) DTheta += 2 * PI;

	for (int32 i = 1; i <= Segments; ++i)
	{
		float T = Theta1 + DTheta * static_cast<float>(i) / Segments;
		float Xp = Rx * FMath::Cos(T);
		float Yp = Ry * FMath::Sin(T);
		float X = CosR * Xp - SinR * Yp + Cx;
		float Y = SinR * Xp + CosR * Yp + Cy;
		OutPoints.Add({X, Y});
	}
}

void ScanlineFill(
	const TArray<TArray<FPathPoint>>& SubPaths,
	TArray<FColor>& OutPixels,
	int32 Width, int32 Height,
	FColor FillColor,
	bool bEvenOdd)
{
	OutPixels.SetNumZeroed(Width * Height);

	struct FEdge
	{
		float X1, Y1, X2, Y2;
		int32 Dir; // +1 downward, -1 upward (for winding)
	};

	TArray<FEdge> Edges;
	for (const auto& Path : SubPaths)
	{
		if (Path.Num() < 2) continue;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			const FPathPoint& A = Path[i];
			const FPathPoint& B = Path[i + 1];
			if (FMath::IsNearlyEqual(A.Y, B.Y)) continue;

			if (A.Y < B.Y)
				Edges.Add({A.X, A.Y, B.X, B.Y, 1});
			else
				Edges.Add({B.X, B.Y, A.X, A.Y, -1});
		}
		// Implicit close connects the last point to the first point.
		const FPathPoint& Last = Path.Last();
		const FPathPoint& First = Path[0];
		if (!FMath::IsNearlyEqual(Last.Y, First.Y))
		{
			if (Last.Y < First.Y)
				Edges.Add({Last.X, Last.Y, First.X, First.Y, 1});
			else
				Edges.Add({First.X, First.Y, Last.X, Last.Y, -1});
		}
	}

	for (int32 Y = 0; Y < Height; ++Y)
	{
		float ScanY = Y + 0.5f;

		TArray<TPair<float, int32>> Intersections;
		for (const FEdge& E : Edges)
		{
			if (ScanY < E.Y1 || ScanY >= E.Y2) continue;
			float T = (ScanY - E.Y1) / (E.Y2 - E.Y1);
			float IX = E.X1 + T * (E.X2 - E.X1);
			Intersections.Add({IX, E.Dir});
		}

		Intersections.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
		{
			return A.Key < B.Key;
		});

		if (bEvenOdd)
		{
			for (int32 i = 0; i + 1 < Intersections.Num(); i += 2)
			{
				int32 X0 = FMath::Max(0, FMath::CeilToInt(Intersections[i].Key));
				int32 X1 = FMath::Min(Width, FMath::CeilToInt(Intersections[i + 1].Key));
				for (int32 X = X0; X < X1; ++X)
					OutPixels[Y * Width + X] = FillColor;
			}
		}
		else
		{
			int32 Winding = 0;
			float SpanStart = 0.0f;
			for (int32 i = 0; i < Intersections.Num(); ++i)
			{
				int32 PrevWinding = Winding;
				Winding += Intersections[i].Value;

				if (PrevWinding == 0 && Winding != 0)
				{
					SpanStart = Intersections[i].Key;
				}
				else if (PrevWinding != 0 && Winding == 0)
				{
					int32 X0 = FMath::Max(0, FMath::CeilToInt(SpanStart));
					int32 X1 = FMath::Min(Width, FMath::CeilToInt(Intersections[i].Key));
					for (int32 X = X0; X < X1; ++X)
						OutPixels[Y * Width + X] = FillColor;
				}
			}
		}
	}
}

UTexture2D* FDesignPathRasterizer::Rasterize(
	const FString& Geometry,
	int32 Width,
	int32 Height,
	const TOptional<FLinearColor>& FillColor,
	const FString& FillRule,
	const TOptional<FLinearColor>& StrokeColor,
	float StrokeWidth,
	const FString& StrokeCap,
	const FString& StrokeJoin)
{
	check(IsInGameThread());
	if (Geometry.IsEmpty() || Width <= 0 || Height <= 0)
		return nullptr;

	const bool bHasVisibleFill = HasVisibleColor(FillColor);
	const bool bHasVisibleStroke = HasVisibleColor(StrokeColor) && StrokeWidth > KINDA_SMALL_NUMBER;
	if (!bHasVisibleFill && !bHasVisibleStroke)
	{
		return nullptr;
	}

	// --- Cache lookup ---
	const FString CacheSignature = MakeCacheSignature(Geometry, Width, Height, FillColor, FillRule, StrokeColor, StrokeWidth, StrokeCap, StrokeJoin);
	const uint64 CacheKey = MakeCacheKey(CacheSignature);
	if (FPathCacheEntry* Found = GPathCache.Find(CacheKey))
	{
		if (Found->Signature == CacheSignature && Found->Texture.IsValid())
		{
			++GCacheHits;
			return Found->Texture.Get();
		}
		GPathCache.Remove(CacheKey);
	}
	++GCacheMisses;

	TArray<TArray<FPathPoint>> SubPaths;
	FBox2D Bounds;
	if (!ParsePathData(Geometry, SubPaths, Bounds))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignPathRasterizer: failed to parse path data"));
		return nullptr;
	}

	// Recalculate precise bounds from all sampled points.
	// Bounds from ParsePathData cover only control points and may miss curve bulges.
	Bounds.Init();
	for (const auto& Path : SubPaths)
	{
		for (const auto& Pt : Path)
			Bounds += FVector2D(Pt.X, Pt.Y);
	}

	// Transform path coordinates to pixel space
	FVector2D BoundsSize = Bounds.GetSize();
	if (BoundsSize.X <= 0 || BoundsSize.Y <= 0)
		return nullptr;

	float ScaleX = Width / BoundsSize.X;
	float ScaleY = Height / BoundsSize.Y;
	float Scale = FMath::Min(ScaleX, ScaleY);
	float OffsetX = (Width - BoundsSize.X * Scale) / 2.0f - Bounds.Min.X * Scale;
	float OffsetY = (Height - BoundsSize.Y * Scale) / 2.0f - Bounds.Min.Y * Scale;

	for (auto& Path : SubPaths)
	{
		for (auto& Pt : Path)
		{
			Pt.X = Pt.X * Scale + OffsetX;
			Pt.Y = Pt.Y * Scale + OffsetY;
		}
	}

	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(Width * Height);

	const bool bEvenOdd = FillRule.Equals(TEXT("evenodd"), ESearchCase::IgnoreCase);
	if (bHasVisibleFill)
	{
		ScanlineFill(SubPaths, Pixels, Width, Height, FillColor->ToFColor(true), bEvenOdd);
	}

	// Evenodd fallback heuristic, kept for glyph-style complex paths: multi-subpath vectors
	// (logo/icon glyphs) can arrive with contour winding that our flattened nonzero scanline
	// fill interprets too aggressively, collapsing the glyph's counters/holes and rendering
	// the whole shape as a solid block. When the JSON did not explicitly request evenodd
	// ("FillRule"), the fill is rasterized a second time with evenodd and both coverages are
	// compared; "NonZeroCoverage > EvenOddCoverage * 12 / 10" means nonzero filled more than
	// 1.2x the evenodd area, which for real glyphs indicates the holes were swallowed.
	// The comparison uses integer math (12/10, not 1.2f) on pixel counts.
	// Tightening the 1.2x threshold re-breaks those glyph inputs (solid-bar regression);
	// loosening it toward parity would start flipping legitimate overlapping nonzero fills
	// to evenodd and punch unwanted holes. The cost is one extra ScanlineFill pass (the
	// complex path is rendered twice) on nonzero-filled multi-subpath inputs only.
	if (bHasVisibleFill && !bEvenOdd)
	{
		TArray<FColor> EvenOddPixels;
		ScanlineFill(SubPaths, EvenOddPixels, Width, Height, FillColor->ToFColor(true), true);

		const int64 NonZeroCoverage = CountFilledPixels(Pixels);
		const int64 EvenOddCoverage = CountFilledPixels(EvenOddPixels);
		if (EvenOddCoverage > 0 && NonZeroCoverage > EvenOddCoverage * 12 / 10)
		{
			UE_LOG(LogDesign2UMG, Log,
				TEXT("DesignPathRasterizer: fallback to evenodd fill for complex path (subpaths=%d coverage nonzero=%lld evenodd=%lld)"),
				SubPaths.Num(), NonZeroCoverage, EvenOddCoverage);
			Pixels = MoveTemp(EvenOddPixels);
		}
	}

	if (bHasVisibleStroke)
	{
		const float ScaledStrokeWidth = FMath::Max(1.0f, StrokeWidth * Scale);
		RasterizeStroke(
			SubPaths,
			Pixels,
			Width,
			Height,
			StrokeColor->ToFColor(true),
			ScaledStrokeWidth,
			StrokeCap,
			StrokeJoin);
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
	if (!Texture)
		return nullptr;

	// Do not rename the texture under the widget.
	// CreateBlueprint automatically compiles and deep-copies WidgetTree; RF_Transient texture PlatformData/Mips
	// can be lost on that path and eventually produce "contains no miplevels".
	// Keep the texture under GetTransientPackage() and use AddToRoot to prevent GC.
	// Also use RF_DuplicateTransient to avoid copying empty mip duplicates.
	Texture->AddToRoot();
	Texture->SetFlags(RF_DuplicateTransient);

	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->Filter = TF_Bilinear;
	Texture->NeverStream = true;

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	bHasPendingTextureUpdates = true;

	// --- Cache write ---
	EvictStale();
	GPathCache.Add(CacheKey, FPathCacheEntry{Texture, CacheSignature});

	return Texture;
}

void FDesignPathRasterizer::FlushPendingTextureUpdates()
{
	if (!bHasPendingTextureUpdates)
		return;

	FlushRenderingCommands();
	bHasPendingTextureUpdates = false;
}

void FDesignPathRasterizer::ClearCache()
{
	for (auto& Pair : GPathCache)
	{
		if (UTexture2D* Tex = Pair.Value.Texture.Get())
			Tex->RemoveFromRoot();
	}
	GPathCache.Empty();
	GCacheHits = GCacheMisses = 0;
	bHasPendingTextureUpdates = false;
}


