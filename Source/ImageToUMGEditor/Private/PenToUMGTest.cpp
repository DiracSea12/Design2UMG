// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGTest.h"
#include "PenTypeMapper.h"
#include "PenParser.h"
#include "PenVariableResolver.h"
#include "PenToUMGConverter.h"

void FPenToUMGTest::LogResult(const FString& TestName, bool bPassed)
{
	if (bPassed)
		UE_LOG(LogTemp, Log, TEXT("[PASS] %s"), *TestName);
	else
		UE_LOG(LogTemp, Error, TEXT("[FAIL] %s"), *TestName);
}

bool FPenToUMGTest::RunAllTests()
{
	bool bAllPassed = true;
	bAllPassed &= TestParseColor();
	bAllPassed &= TestParseSize();
	bAllPassed &= TestVariableResolver();
	bAllPassed &= TestNodeConversion();
	bAllPassed &= TestLayoutConversion();

	UE_LOG(LogTemp, Log, TEXT("PenToUMG Tests: %s"), bAllPassed ? TEXT("ALL PASSED") : TEXT("SOME FAILED"));
	return bAllPassed;
}

bool FPenToUMGTest::TestParseColor()
{
	bool bPassed = true;

	// #RGB
	FLinearColor C1 = FPenTypeMapper::ParseColor(TEXT("#F00"));
	bPassed &= FMath::IsNearlyEqual(C1.R, 1.0f) && FMath::IsNearlyEqual(C1.G, 0.0f);

	// #RRGGBB
	FLinearColor C2 = FPenTypeMapper::ParseColor(TEXT("#00FF00"));
	bPassed &= FMath::IsNearlyEqual(C2.G, 1.0f);

	// #RRGGBBAA
	FLinearColor C3 = FPenTypeMapper::ParseColor(TEXT("#0000FF80"));
	bPassed &= FMath::IsNearlyEqual(C3.B, 1.0f) && FMath::IsNearlyEqual(C3.A, 0.5f, 0.01f);

	LogResult(TEXT("ParseColor"), bPassed);
	return bPassed;
}

bool FPenToUMGTest::TestParseSize()
{
	bool bPassed = true;

	FString Json = TEXT(R"({"version":"1","children":[{"id":"test","type":"frame","width":100,"height":"fill_container"}]})");
	FPenDocument Doc;
	FPenParser::ParseDocument(Json, Doc);

	if (Doc.Children.Num() > 0)
	{
		auto& Node = Doc.Children[0];
		bPassed &= Node->Width.Mode == EPenSizeMode::Fixed && Node->Width.Value == 100.f;
		bPassed &= Node->Height.Mode == EPenSizeMode::FillContainer;
	}
	else
		bPassed = false;

	LogResult(TEXT("ParseSize"), bPassed);
	return bPassed;
}

bool FPenToUMGTest::TestVariableResolver()
{
	bool bPassed = true;

	TMap<FString, FPenVariable> Vars;
	FPenVariable Var;
	Var.Name = TEXT("primary");
	Var.Type = EPenVariableType::Color;
	Var.Value = TEXT("#FF0000");
	Vars.Add(Var.Name, Var);

	FPenVariableResolver Resolver(Vars);
	FString Resolved = Resolver.ResolveColor(TEXT("$primary"));
	bPassed &= Resolved == TEXT("#FF0000");

	LogResult(TEXT("VariableResolver"), bPassed);
	return bPassed;
}

bool FPenToUMGTest::TestNodeConversion()
{
	bool bPassed = true;

	FString Json = TEXT(R"({"version":"1","children":[{"id":"btn","type":"frame","layout":"horizontal","children":[{"id":"txt","type":"text","content":"Hello"}]}]})");
	FPenDocument Doc;
	FPenParser::ParseDocument(Json, Doc);

	FUMGWidgetData Data = FPenToUMGConverter::Convert(Doc);
	bPassed &= Data.RootWidget.Children.Num() > 0;
	if (bPassed)
	{
		auto& Frame = Data.RootWidget.Children[0];
		bPassed &= Frame.Type == TEXT("HorizontalBox");
		bPassed &= Frame.Children.Num() > 0 && Frame.Children[0].Type == TEXT("TextBlock");
	}

	LogResult(TEXT("NodeConversion"), bPassed);
	return bPassed;
}

bool FPenToUMGTest::TestLayoutConversion()
{
	bool bPassed = true;

	FString Json = TEXT(R"({"version":"1","children":[{"id":"container","type":"frame","layout":"vertical","gap":10,"children":[{"id":"a","type":"frame"},{"id":"b","type":"frame"}]}]})");
	FPenDocument Doc;
	FPenParser::ParseDocument(Json, Doc);

	FUMGWidgetData Data = FPenToUMGConverter::Convert(Doc);
	if (Data.RootWidget.Children.Num() > 0)
	{
		auto& Container = Data.RootWidget.Children[0];
		// 第二个子元素应该有 gap padding
		if (Container.Children.Num() >= 2)
			bPassed &= Container.Children[1].Slot.Padding.Top == 10.f;
	}

	LogResult(TEXT("LayoutConversion"), bPassed);
	return bPassed;
}
