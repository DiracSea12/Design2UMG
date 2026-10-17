// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignPathRasterizer.h"
#include "UObject/ObjectKey.h"
#include "ProceduralShapeGenerator.h"
#include "Design2UMGLog.h"
#include "DesignResourceResolver.h"
#include "UMGWidgetNodeAccess.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Hash/CityHash.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "Serialization/MemoryWriter.h"

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
		TArray<uint8> Signature;
	};

	TArray<uint8> MakeCacheSignature(FDesignPathRasterRequest Request, FGuid SourceImageId, FObjectKey SourceObject)
	{
		auto& Path = Request.Path;
		FString Geometry = Path.Geometry.Get(FString());
		int32 Width = Request.Size.X, Height = Request.Size.Y;
		auto FillColor = Path.FillColor, StrokeColor = Path.StrokeColor;
		FString Rule = Path.FillRule.Get(DesignPathRasterDefaults::FillRule);
		FString StrokeCap = Path.StrokeCap.Get(DesignPathRasterDefaults::StrokeCap);
		FString StrokeJoin = Path.StrokeJoin.Get(DesignPathRasterDefaults::StrokeJoin);
		float StrokeWidth = Path.StrokeThickness.Get(0.0f);
		FString StrokeGeometry = Path.StrokeGeometry.Get(FString());
		const auto* FillPaint = Request.FillPaint.IsSet() ? &Request.FillPaint.GetValue() : nullptr;
		const auto* StrokePaint = Request.StrokePaint.IsSet() ? &Request.StrokePaint.GetValue() : nullptr;
		const auto* FillImage = Request.FillImage.IsSet() ? &Request.FillImage.GetValue() : nullptr;

		TArray<uint8> Signature;
		FMemoryWriter Writer(Signature);
		Writer << Geometry << Width << Height << FillColor << Rule << StrokeColor
			<< StrokeWidth << StrokeCap << StrokeJoin << Path.CanvasSize;
		bool bHasImage = FillImage != nullptr;
		Writer << bHasImage;
		if (FillImage)
		{
			auto Image = *FillImage;
			Writer << Image.TexturePath << Image.Mode << SourceImageId << SourceObject;
			bool bHasTransform = Image.Transform.IsSet();
			Writer << bHasTransform;
			if (bHasTransform)
			{
				auto Transform = Image.Transform.GetValue();
				Writer << Transform.A << Transform.B << Transform.TX << Transform.C << Transform.D << Transform.TY;
			}
		}
		FString StrokePath = StrokeGeometry;
		Writer << StrokePath;
		for (const auto* Paint : {FillPaint, StrokePaint})
		{
			bool bHasPaint = Paint != nullptr;
			Writer << bHasPaint;
			if (Paint)
			{
				auto SourceWidth = Paint->WidthOverride;
				auto SourceHeight = Paint->HeightOverride;
				Writer << SourceWidth << SourceHeight;
				auto Gradient = Paint->Gradient;
				Writer << Gradient.Type << Gradient.Rotation << Gradient.StartPoint << Gradient.EndPoint << Gradient.Center << Gradient.Size;
				int32 StopCount = Gradient.Stops.Num();
				Writer << StopCount;
				for (auto& Stop : Gradient.Stops) Writer << Stop.Position << Stop.Color;
			}
		}
		return Signature;
	}

	uint64 MakeCacheKey(const TArray<uint8>& Signature)
	{
		return CityHash64(reinterpret_cast<const char*>(Signature.GetData()), Signature.Num());
	}

	TMap<uint64, FPathCacheEntry> GPathCache;
	int32 GCacheHits = 0;
	int32 GCacheMisses = 0;
	bool bHasPendingTextureUpdates = false;
	// Retain at most one raster budget (256 MiB of BGRA8), also bounded by entry count.
	// Eviction is unordered; a miss merely regenerates the same deterministic texture.
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
		bool bCircularCap,
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
			const float SegmentLength = FMath::Sqrt(SegmentLengthSq);
			const FVector2D Direction = Segment / SegmentLength;
			const FVector2D Relative = Point - Start;
			const float Along = FVector2D::DotProduct(Relative, Direction);
			const float Across = FVector2D::CrossProduct(Direction, Relative);
			return Along >= -Radius && Along <= SegmentLength + Radius && FMath::Abs(Across) <= Radius;
		}

		const float RawT = FVector2D::DotProduct(Point - Start, Segment) / SegmentLengthSq;
		if (!bCircularCap && (RawT < 0.0f || RawT > 1.0f))
		{
			return false;
		}

		const float T = bCircularCap ? FMath::Clamp(RawT, 0.0f, 1.0f) : RawT;
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
		const bool bCircularCap = StrokeCap.Equals(TEXT("round"), ESearchCase::IgnoreCase);
		const bool bSquareCap = StrokeCap.Equals(TEXT("square"), ESearchCase::IgnoreCase);
		const bool bCircularJoin = StrokeJoin.Equals(TEXT("round"), ESearchCase::IgnoreCase);

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

				const FVector2D Direction = (End - Start).GetSafeNormal();
				const float BoundsRadius = bSquareCap ? Radius * (FMath::Abs(Direction.X) + FMath::Abs(Direction.Y)) : Radius;
				const int32 MinX = FMath::Max(0, FMath::FloorToInt(FMath::Min(Start.X, End.X) - BoundsRadius - 1.0f));
				const int32 MaxX = FMath::Min(Width - 1, FMath::CeilToInt(FMath::Max(Start.X, End.X) + BoundsRadius + 1.0f));
				const int32 MinY = FMath::Max(0, FMath::FloorToInt(FMath::Min(Start.Y, End.Y) - BoundsRadius - 1.0f));
				const int32 MaxY = FMath::Min(Height - 1, FMath::CeilToInt(FMath::Max(Start.Y, End.Y) + BoundsRadius + 1.0f));

				for (int32 Y = MinY; Y <= MaxY; ++Y)
				{
					for (int32 X = MinX; X <= MaxX; ++X)
					{
						const FVector2D Sample(X + 0.5f, Y + 0.5f);
						if (IsPointInsideStrokeSegment(Sample, Start, End, Radius, bCircularCap, bSquareCap))
						{
							InOutPixels[Y * Width + X] = StrokeColor;
						}
					}
				}
			}

			if (bCircularJoin)
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

	void MakeCacheRoom(int64 IncomingPixels)
	{
		for (auto It = GPathCache.CreateIterator(); It; ++It)
		{
			if (!It->Value.Texture.IsValid()) It.RemoveCurrent();
		}
		int64 Pixels = FDesignPathRasterizer::GetCachedPixelCount();
		for (auto It = GPathCache.CreateIterator(); It
			&& (GPathCache.Num() >= GMaxCacheEntries || Pixels > FDesignPathRasterizer::MaxRasterPixels - IncomingPixels); ++It)
		{
			UTexture2D* Texture = It->Value.Texture.Get();
			Pixels -= static_cast<int64>(Texture->GetSizeX()) * Texture->GetSizeY();
			Texture->RemoveFromRoot();
			It.RemoveCurrent();
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
	bool ReadFloat(const TCHAR*& P, float& OutValue)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r')) ++P;
		const TCHAR* Start = P;
		if (*P == TEXT('-') || *P == TEXT('+')) ++P;
		bool bHasDigit = false;
		while (*P >= TEXT('0') && *P <= TEXT('9')) { bHasDigit = true; ++P; }
		if (*P == TEXT('.'))
		{
			++P;
			while (*P >= TEXT('0') && *P <= TEXT('9')) { bHasDigit = true; ++P; }
		}
		if (!bHasDigit) return false;
		if (*P == TEXT('e') || *P == TEXT('E'))
		{
			++P;
			if (*P == TEXT('-') || *P == TEXT('+')) ++P;
			if (*P < TEXT('0') || *P > TEXT('9')) return false;
			while (*P >= TEXT('0') && *P <= TEXT('9')) ++P;
		}
		OutValue = FCString::Atof(Start);
		return P > Start && FMath::IsFinite(OutValue);
	}

	bool ReadFlag(const TCHAR*& P, bool& OutValue)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r')) ++P;
		if (*P != TEXT('0') && *P != TEXT('1')) return false;
		OutValue = *P++ == TEXT('1');
		return true;
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
	OutSubPaths.Reset();
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

	const auto Fail = [&]()
	{
		OutSubPaths.Reset();
		OutBounds.Init();
		return false;
	};

	while (*P)
	{
		while (*P == TEXT(' ') || *P == TEXT(',') || *P == TEXT('\t') || *P == TEXT('\n') || *P == TEXT('\r'))
			++P;
		if (!*P) break;

		const TCHAR* IterationStart = P;
		TCHAR Cmd = *P;
		if (!IsCommandChar(Cmd)) return Fail();
		++P;

		bool bRelative = (Cmd >= TEXT('a') && Cmd <= TEXT('z'));
		TCHAR CmdUpper = bRelative ? (Cmd - 32) : Cmd;

		if ((LastCmd == 0 && CmdUpper != TEXT('M')) || (CmdUpper != TEXT('Z') && !HasMoreNumbers(P))) return Fail();

		auto Abs = [&](float V, float Base) -> float { return bRelative ? Base + V : V; };
		const auto ReadPoint = [&](FPathPoint& Point)
		{
			float X, Y;
			if (!ReadFloat(P, X) || !ReadFloat(P, Y)) return false;
			Point = {Abs(X, Current.X), Abs(Y, Current.Y)};
			return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
		};

		switch (CmdUpper)
		{
		case TEXT('M'):
		{
			float X;
			if (!ReadFloat(P, X)) return Fail();
			float Y;
			if (!ReadFloat(P, Y)) return Fail();
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
				if (!ReadFloat(P, X)) return Fail();
				if (!ReadFloat(P, Y)) return Fail();
				AddPoint(Abs(X, Current.X), Abs(Y, Current.Y));
			}
			LastCmd = TEXT('M');
			break;
		}
		case TEXT('L'):
			while (HasMoreNumbers(P))
			{
				float X;
				if (!ReadFloat(P, X)) return Fail();
				float Y;
				if (!ReadFloat(P, Y)) return Fail();
				AddPoint(Abs(X, Current.X), Abs(Y, Current.Y));
			}
			LastCmd = TEXT('L');
			break;
		case TEXT('H'):
			while (HasMoreNumbers(P))
			{
				float X;
				if (!ReadFloat(P, X)) return Fail();
				AddPoint(Abs(X, Current.X), Current.Y);
			}
			LastCmd = TEXT('H');
			break;
		case TEXT('V'):
			while (HasMoreNumbers(P))
			{
				float Y;
				if (!ReadFloat(P, Y)) return Fail();
				AddPoint(Current.X, Abs(Y, Current.Y));
			}
			LastCmd = TEXT('V');
			break;
		case TEXT('C'):
			while (HasMoreNumbers(P))
			{
				FPathPoint P1;
				if (!ReadPoint(P1)) return Fail();
				FPathPoint P2;
				if (!ReadPoint(P2)) return Fail();
				FPathPoint P3;
				if (!ReadPoint(P3)) return Fail();
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
				FPathPoint P2;
				if (!ReadPoint(P2)) return Fail();
				FPathPoint P3;
				if (!ReadPoint(P3)) return Fail();
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
				FPathPoint P1;
				if (!ReadPoint(P1)) return Fail();
				FPathPoint P2;
				if (!ReadPoint(P2)) return Fail();
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
				FPathPoint P2;
				if (!ReadPoint(P2)) return Fail();
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
				float Rx;
				if (!ReadFloat(P, Rx)) return Fail();
				float Ry;
				if (!ReadFloat(P, Ry)) return Fail();
				float Rot;
				if (!ReadFloat(P, Rot)) return Fail();
				bool bLarge;
				if (!ReadFlag(P, bLarge)) return Fail();
				bool bSweep;
				if (!ReadFlag(P, bSweep)) return Fail();
				FPathPoint To;
				if (!ReadPoint(To)) return Fail();
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
			return Fail();
		}
		if (P <= IterationStart) return Fail();
	}

	for (const auto& Path : OutSubPaths)
	{
		for (const FPathPoint& Point : Path)
		{
			if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y)) return Fail();
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
	OutPixels.SetNumZeroed(static_cast<int32>(static_cast<int64>(Width) * Height));

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

bool FDesignPathRasterizer::IsRasterSizeAllowed(int64 Width, int64 Height)
{
	return DesignImageResources::FImageResourceLimits().AllowsDimensions(Width, Height);
}

bool FDesignPathRasterizer::TryGetRasterSize(float Width, float Height, FIntPoint& OutSize)
{
	OutSize = FIntPoint::ZeroValue;
	constexpr int32 Supersample = DesignPathRasterDefaults::Supersample;
	// Check before float-to-int conversion or supersampling multiplication.
	if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width <= 0.0f || Height <= 0.0f
		|| Width > MaxRasterDimension / Supersample || Height > MaxRasterDimension / Supersample)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignPathRasterizer: invalid or excessive path texture size %.3fx%.3f"), Width, Height);
		return false;
	}
	const int64 RasterWidth = static_cast<int64>(FMath::Max(1, FMath::RoundToInt(Width))) * Supersample;
	const int64 RasterHeight = static_cast<int64>(FMath::Max(1, FMath::RoundToInt(Height))) * Supersample;
	if (!IsRasterSizeAllowed(RasterWidth, RasterHeight))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignPathRasterizer: path texture exceeds the pixel budget (%lldx%lld)"), RasterWidth, RasterHeight);
		return false;
	}
	OutSize = FIntPoint(static_cast<int32>(RasterWidth), static_cast<int32>(RasterHeight));
	return true;
}

bool FDesignPathRasterizer::TryBuildRequest(const FUMGWidgetNode& Node, FDesignPathRasterRequest& OutRequest,
	const TMap<FString, UTexture2D*>* TextureMap)
{
	OutRequest = FDesignPathRasterRequest();
	if (Node.Path.Geometry.Get(FString()).IsEmpty()) return false;
	const FVector2D CanvasSize = Node.Path.CanvasSize.Get(FVector2D(
		Node.WidgetStyle.WidthOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize)),
		Node.WidgetStyle.HeightOverride.Get(static_cast<float>(DesignPathRasterDefaults::DefaultPixelSize))));
	if (!TryGetRasterSize(CanvasSize.X, CanvasSize.Y, OutRequest.Size)) return false;
	OutRequest.Path = Node.Path;
	auto DefaultIfEmpty = [](TOptional<FString>& Value, const TCHAR* Default)
	{
		if (Value.Get(FString()).IsEmpty()) Value = FString(Default);
	};
	DefaultIfEmpty(OutRequest.Path.FillRule, DesignPathRasterDefaults::FillRule);
	DefaultIfEmpty(OutRequest.Path.StrokeCap, DesignPathRasterDefaults::StrokeCap);
	DefaultIfEmpty(OutRequest.Path.StrokeJoin, DesignPathRasterDefaults::StrokeJoin);
	FUMGProceduralResourceRequest FillPaint;
	if (!Node.Brush.Gradient.Stops.IsEmpty()
		&& Design2UMGWidgetNodeProceduralResourceAccess::TryBuildProceduralResourceRequest(Node, FillPaint))
	{
		if (Node.Path.CanvasSize.IsSet())
		{
			FillPaint.WidthOverride = CanvasSize.X;
			FillPaint.HeightOverride = CanvasSize.Y;
		}
		OutRequest.FillPaint = MoveTemp(FillPaint);
		if (OutRequest.Path.FillColor.IsSet() && !Node.Path.StrokeGradient.HasAny()) OutRequest.Path.FillColor = FLinearColor::White;
	}
	if (Node.Path.StrokeGradient.HasAny())
	{
		FUMGProceduralResourceRequest StrokePaint;
		StrokePaint.Gradient = Node.Path.StrokeGradient;
		StrokePaint.MaterialRef = Design2UMGWidgetNodeAccess::GetGradientMaterialRef(StrokePaint.Gradient);
		StrokePaint.WidthOverride = CanvasSize.X;
		StrokePaint.HeightOverride = CanvasSize.Y;
		OutRequest.StrokePaint = MoveTemp(StrokePaint);
	}
	// Persisted path colors are byte-precision; preview must prepare the same paint.
	for (auto* Color : {&OutRequest.Path.FillColor, &OutRequest.Path.StrokeColor})
		if (Color->IsSet()) *Color = FLinearColor::FromSRGBColor(Color->GetValue().ToFColor(true));
	if (!OutRequest.FillPaint.IsSet() && !Node.Brush.Image.TexturePath.Get(FString()).IsEmpty())
	{
		OutRequest.FillImage = Node.Brush.Image;
		if (TextureMap) OutRequest.FillTexture = TextureMap->FindRef(Node.Brush.Image.TexturePath.GetValue());
	}
	return true;
}

UTexture2D* FDesignPathRasterizer::Rasterize(const FDesignPathRasterRequest& Request, FString* OutError,
	DesignImageResources::FImageOperationBudget* Budget)
{
	const FUMGPathData& PathData = Request.Path;
	const FString Geometry = PathData.Geometry.Get(FString());
	const int32 Width = Request.Size.X, Height = Request.Size.Y;
	const auto& FillColor = PathData.FillColor;
	const auto& StrokeColor = PathData.StrokeColor;
	const FString FillRule = PathData.FillRule.Get(DesignPathRasterDefaults::FillRule);
	const FString StrokeCap = PathData.StrokeCap.Get(DesignPathRasterDefaults::StrokeCap);
	const FString StrokeJoin = PathData.StrokeJoin.Get(DesignPathRasterDefaults::StrokeJoin);
	const float StrokeWidth = PathData.StrokeThickness.Get(0.0f);
	const FString StrokeGeometry = PathData.StrokeGeometry.Get(FString());
	const auto* FillPaint = Request.FillPaint.IsSet() ? &Request.FillPaint.GetValue() : nullptr;
	const auto* StrokePaint = Request.StrokePaint.IsSet() ? &Request.StrokePaint.GetValue() : nullptr;
	const auto* FillImage = Request.FillImage.IsSet() ? &Request.FillImage.GetValue() : nullptr;
	UTexture2D* FillTexture = Request.FillTexture;

	check(IsInGameThread());
	if (OutError) OutError->Reset();
	if (Geometry.IsEmpty()) return nullptr;
	if (!IsRasterSizeAllowed(Width, Height))
	{
		if (OutError) *OutError = TEXT("Invalid or excessive path raster dimensions");
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignPathRasterizer: invalid or excessive raster size %dx%d (max dimension %d, max pixels %lld)"),
			Width, Height, MaxRasterDimension, MaxRasterPixels);
		return nullptr;
	}

	const bool bHasVisibleFill = HasVisibleColor(FillColor);
	const bool bHasVisibleStroke = HasVisibleColor(StrokeColor) && StrokeWidth > KINDA_SMALL_NUMBER;
	if (!bHasVisibleFill && !bHasVisibleStroke)
	{
		return nullptr;
	}

	UTexture2D* SourceImage = nullptr;
	if (FillImage)
	{
		SourceImage = FillTexture ? FillTexture : DesignResourceResolver::LoadTextureFromPath(FillImage->TexturePath.Get(FString()), DesignResourceResolver::ELoadMode::LoadThenFind);
		if (!SourceImage)
		{
			if (OutError) *OutError = TEXT("Path fill image is not available");
			return nullptr;
		}
	}
	// --- Cache lookup ---
	const TArray<uint8> CacheSignature = MakeCacheSignature(Request,
		SourceImage ? SourceImage->Source.GetId() : FGuid(), FObjectKey(SourceImage));
	const uint64 CacheKey = MakeCacheKey(CacheSignature);
	if (FPathCacheEntry* Found = GPathCache.Find(CacheKey))
	{
		if (Found->Signature == CacheSignature && Found->Texture.IsValid())
		{
			++GCacheHits;
			return Found->Texture.Get();
		}
		if (UTexture2D* Texture = Found->Texture.Get()) Texture->RemoveFromRoot();
		GPathCache.Remove(CacheKey);
	}
	++GCacheMisses;

	TArray<TArray<FPathPoint>> SubPaths;
	FBox2D Bounds;
	if (!ParsePathData(Geometry, SubPaths, Bounds))
	{
		if (OutError) *OutError = TEXT("Failed to parse path data");
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
	// Centerlines may have a collapsed axis. Reserve stroke thickness on that axis;
	// fill-only zero-area geometry remains a valid, transparent raster.
	const float CollapsedExtent = bHasVisibleStroke ? StrokeWidth : 1.0f;
	if (BoundsSize.X == 0)
	{
		Bounds.Min.X -= CollapsedExtent * 0.5f;
		BoundsSize.X = CollapsedExtent;
	}
	if (BoundsSize.Y == 0)
	{
		Bounds.Min.Y -= CollapsedExtent * 0.5f;
		BoundsSize.Y = CollapsedExtent;
	}

	float ScaleX = Width / BoundsSize.X;
	float ScaleY = Height / BoundsSize.Y;
	float Scale = FMath::Min(ScaleX, ScaleY);
	float OffsetX = (Width - BoundsSize.X * Scale) / 2.0f - Bounds.Min.X * Scale;
	float OffsetY = (Height - BoundsSize.Y * Scale) / 2.0f - Bounds.Min.Y * Scale;
	if (PathData.CanvasSize.IsSet() || StrokePaint || (FillPaint && !bHasVisibleFill && bHasVisibleStroke))
	{
		// Typed paint coordinates belong to the authored shape box, not its ink bounds.
		const FUMGProceduralResourceRequest* PaintBounds = StrokePaint ? StrokePaint : FillPaint;
		Scale = Width / (PathData.CanvasSize.IsSet() ? PathData.CanvasSize->X : PaintBounds->WidthOverride.Get(static_cast<float>(Width)));
		OffsetX = OffsetY = 0.0f;
	}

	for (auto& Path : SubPaths)
	{
		for (auto& Pt : Path)
		{
			Pt.X = Pt.X * Scale + OffsetX;
			Pt.Y = Pt.Y * Scale + OffsetY;
		}
	}

	TArray<TArray<FPathPoint>> StrokePaths;
	if (!StrokeGeometry.IsEmpty())
	{
		FBox2D StrokeBounds;
		if (!ParsePathData(StrokeGeometry, StrokePaths, StrokeBounds))
		{
			if (OutError) *OutError = TEXT("Failed to parse stroke geometry");
			return nullptr;
		}
		for (auto& Path : StrokePaths)
			for (auto& Point : Path)
			{
				Point.X = Point.X * Scale + OffsetX;
				Point.Y = Point.Y * Scale + OffsetY;
			}
	}
	const auto& StrokeContours = StrokePaths.IsEmpty() ? SubPaths : StrokePaths;

	// Cache hits allocate nothing. Charge misses before allocating the raster buffer.
	DesignImageResources::FImageOperationBudget LocalBudget(DesignImageResources::GetGeneratedTextureLimits());
	if (!Budget) Budget = &LocalBudget;
	FString BudgetError;
	if (!Budget->ConsumeDecodedPixels(static_cast<int64>(Width) * Height, BudgetError))
	{
		if (OutError) *OutError = BudgetError;
		return nullptr;
	}

	MakeCacheRoom(static_cast<int64>(Width) * Height);
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(static_cast<int32>(static_cast<int64>(Width) * Height));

	const bool bEvenOdd = FillRule.Equals(TEXT("evenodd"), ESearchCase::IgnoreCase);
	if (bHasVisibleFill)
	{
		ScanlineFill(SubPaths, Pixels, Width, Height, FillColor->ToFColor(true), bEvenOdd);
	}

	// For implicit nonzero fills with multiple contours, compare an evenodd raster.
	// Select evenodd when nonzero coverage exceeds it by 20%, preserving glyph holes.
	// Explicit fill rules bypass this extra rasterization.
	if (bHasVisibleFill && FillRule.IsEmpty())
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

	if (bHasVisibleStroke && !StrokePaint)
	{
		const float ScaledStrokeWidth = FMath::Max(1.0f, StrokeWidth * Scale);
		RasterizeStroke(
			StrokeContours,
			Pixels,
			Width,
			Height,
			StrokeColor->ToFColor(true),
			ScaledStrokeWidth,
			StrokeCap,
			StrokeJoin);
	}

	const auto PaintMask = [&](TArray<FColor>& Mask, const FUMGProceduralResourceRequest& SourcePaint) -> UTexture2D*
	{
		FUMGProceduralResourceRequest Paint = SourcePaint;
		Paint.WidthOverride = static_cast<float>(Width);
		Paint.HeightOverride = static_cast<float>(Height);
		UTexture2D* Painted = FProceduralShapeGenerator::GenerateFromMaterialRef(Paint, nullptr, OutError, Budget);
		if (!Painted) return nullptr;
		FProceduralShapeGenerator::FlushPendingTextureUpdates();
		const FColor* Colors = static_cast<const FColor*>(Painted->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		for (int32 Index = 0; Index < Mask.Num(); ++Index)
		{
			const FColor Tint = Mask[Index];
			const uint8 Coverage = Tint.A;
			Mask[Index] = Colors[Index];
			if (StrokePaint)
			{
				FLinearColor Tinted = FLinearColor::FromSRGBColor(Colors[Index]) * FLinearColor::FromSRGBColor(Tint);
				Tinted.A = Colors[Index].A / 255.0f;
				Mask[Index] = Tinted.ToFColor(true);
			}
			Mask[Index].A = static_cast<uint8>((static_cast<uint32>(Mask[Index].A) * Coverage + 127) / 255);
		}
		Painted->GetPlatformData()->Mips[0].BulkData.Unlock();
		FProceduralShapeGenerator::ReleaseTransientTexture(Painted);
		return Painted;
	};
	UTexture2D* Texture = FillPaint ? PaintMask(Pixels, *FillPaint) : nullptr;
	if (FillPaint && !Texture) return nullptr;
	if (SourceImage)
	{
		const auto Transform = FillImage->Mode.Get(FString()) == TEXT("Crop")
			? FillImage->Transform.Get(FUMGVisualBrushData::FImageTransformData()) : FUMGVisualBrushData::FImageTransformData();
		Texture = FProceduralShapeGenerator::ApplyAffineCrop(SourceImage, Transform.A, Transform.B, Transform.TX,
			Transform.C, Transform.D, Transform.TY, Width, Height, OutError, Budget);
		if (!Texture) return nullptr;
		FProceduralShapeGenerator::FlushPendingTextureUpdates();
		const FColor* ImagePixels = static_cast<const FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.LockReadOnly());
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			Pixels[Index] = (FLinearColor::FromSRGBColor(ImagePixels[Index]) * FLinearColor::FromSRGBColor(Pixels[Index])).ToFColor(true);
		}
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		FProceduralShapeGenerator::ReleaseTransientTexture(Texture);
	}

	if (StrokePaint && bHasVisibleStroke)
	{
		if (!Budget->ConsumeDecodedPixels(static_cast<int64>(Width) * Height, BudgetError))
		{
			if (OutError) *OutError = BudgetError;
			return nullptr;
		}
		TArray<FColor> StrokePixels;
		StrokePixels.SetNumZeroed(Pixels.Num());
		RasterizeStroke(StrokeContours, StrokePixels, Width, Height, StrokeColor->ToFColor(true),
			FMath::Max(1.0f, StrokeWidth * Scale), StrokeCap, StrokeJoin);
		UTexture2D* PaintedStroke = PaintMask(StrokePixels, *StrokePaint);
		if (!PaintedStroke) return nullptr;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			const FLinearColor Background = FLinearColor::FromSRGBColor(Pixels[Index]);
			const FLinearColor Foreground = FLinearColor::FromSRGBColor(StrokePixels[Index]);
			const float Alpha = Foreground.A + Background.A * (1.0f - Foreground.A);
			FLinearColor Combined = Foreground * Foreground.A + Background * (Background.A * (1.0f - Foreground.A));
			if (Alpha > 0.0f) Combined /= Alpha;
			Combined.A = Alpha;
			Pixels[Index] = Combined.ToFColor(true);
		}
		if (!Texture) Texture = PaintedStroke;
	}
	if (!Texture) Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
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

int64 FDesignPathRasterizer::GetCachedPixelCount()
{
	int64 Pixels = 0;
	for (const auto& Pair : GPathCache)
	{
		if (const UTexture2D* Texture = Pair.Value.Texture.Get())
			Pixels += static_cast<int64>(Texture->GetSizeX()) * Texture->GetSizeY();
	}
	return Pixels;
}
