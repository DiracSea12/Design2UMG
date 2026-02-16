// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "Design2UMGLog.h"
#include "HAL/IConsoleManager.h"

// --- Path cache (file-local) ---
namespace
{
	uint32 MakeCacheKey(const FString& Geometry, int32 W, int32 H, FLinearColor C, const FString& Rule)
	{
		uint32 K = GetTypeHash(Geometry);
		K = HashCombine(K, GetTypeHash(W));
		K = HashCombine(K, GetTypeHash(H));
		K = HashCombine(K, GetTypeHash(C.R));
		K = HashCombine(K, GetTypeHash(C.G));
		K = HashCombine(K, GetTypeHash(C.B));
		K = HashCombine(K, GetTypeHash(C.A));
		K = HashCombine(K, GetTypeHash(Rule));
		return K;
	}

	TMap<uint32, TWeakObjectPtr<UTexture2D>> GPathCache;
	int32 GCacheHits = 0;
	int32 GCacheMisses = 0;
	constexpr int32 GMaxCacheEntries = 256;

	void EvictStale()
	{
		for (auto It = GPathCache.CreateIterator(); It; ++It)
		{
			if (!It->Value.IsValid())
				It.RemoveCurrent();
		}
		if (GPathCache.Num() > GMaxCacheEntries)
		{
			int32 ToRemove = GPathCache.Num() - GMaxCacheEntries;
			for (auto It = GPathCache.CreateIterator(); ToRemove > 0 && It; ++It, --ToRemove)
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
			GPathCache.Empty();
			GCacheHits = GCacheMisses = 0;
			UE_LOG(LogDesign2UMG, Log, TEXT("PenPathRasterizer: cache cleared"));
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

bool FPenPathRasterizer::ParsePathData(const FString& Data, TArray<TArray<FPathPoint>>& OutSubPaths, FBox2D& OutBounds)
{
	OutBounds.Init();
	const TCHAR* P = *Data;
	FPathPoint Current = {0, 0};
	FPathPoint Start = {0, 0};
	FPathPoint LastControl = {0, 0}; // S/T 指令需要镜像上一个控制点
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
			// implicit lineto — 继承上一条命令的相对/绝对模式
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
				// 镜像上一个 cubic 控制点；非 C/S 后续则退化为当前点
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
			UE_LOG(LogDesign2UMG, Warning, TEXT("PenPathRasterizer: unsupported SVG command '%c'"), Cmd);
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

void FPenPathRasterizer::SampleCubicBezier(
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

void FPenPathRasterizer::SampleQuadBezier(
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

void FPenPathRasterizer::SampleArc(
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

void FPenPathRasterizer::ScanlineFill(
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
		// Implicit close: last point → first point
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

UTexture2D* FPenPathRasterizer::Rasterize(
	const FString& Geometry,
	int32 Width, int32 Height,
	FLinearColor FillColor,
	const FString& FillRule,
	UObject* Outer)
{
	if (Geometry.IsEmpty() || Width <= 0 || Height <= 0)
		return nullptr;

	// --- Cache lookup ---
	uint32 CacheKey = MakeCacheKey(Geometry, Width, Height, FillColor, FillRule);
	if (TWeakObjectPtr<UTexture2D>* Found = GPathCache.Find(CacheKey))
	{
		if (Found->IsValid())
		{
			++GCacheHits;
			return Found->Get();
		}
		GPathCache.Remove(CacheKey);
	}
	++GCacheMisses;

	TArray<TArray<FPathPoint>> SubPaths;
	FBox2D Bounds;
	if (!ParsePathData(Geometry, SubPaths, Bounds))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("PenPathRasterizer: failed to parse path data"));
		return nullptr;
	}

	// 从所有采样点重新计算精确 bounds（ParsePathData 的 bounds 只含端点，曲线凸出部分会遗漏）
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
	bool bEvenOdd = FillRule.Equals(TEXT("evenodd"), ESearchCase::IgnoreCase);
	ScanlineFill(SubPaths, Pixels, Width, Height, FillColor.ToFColor(true), bEvenOdd);

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
	if (!Texture)
		return nullptr;

	// 不要 Rename 到 widget 下面！CreateBlueprint 会自动编译并深拷贝 WidgetTree，
	// RF_Transient 纹理的 PlatformData/Mips 在拷贝中会丢失（"contains no miplevels"）。
	// 纹理留在 GetTransientPackage() 中，通过 AddToRoot 防止 GC。
	// RF_DuplicateTransient 使 FDuplicateDataWriter 写 nullptr 而非复制，避免空 mip 副本。
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
	FlushRenderingCommands();

	// --- Cache write ---
	EvictStale();
	GPathCache.Add(CacheKey, Texture);

	return Texture;
}
