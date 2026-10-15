// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "DesignOptionalValues.h"
#include "DesignSchemaFoundation.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UMGSchemaSourceExtensions.h"
#include "UObject/Class.h"

#include <type_traits>

namespace
{
enum class EOptionalFixtureState : uint8
{
	Unset,
	SetDefault,
	SetNonDefault
};

const TCHAR* LexToString(const EOptionalFixtureState State)
{
	switch (State)
	{
	case EOptionalFixtureState::Unset:
		return TEXT("unset");
	case EOptionalFixtureState::SetDefault:
		return TEXT("set-default");
	case EOptionalFixtureState::SetNonDefault:
		return TEXT("set-nondefault");
	default:
		return TEXT("unknown");
	}
}

template <typename OwnerType, typename ConfigureType, typename VerifyType>
void VerifyCopyAndSerializationPaths(
	FAutomationTestBase& Test,
	const TCHAR* OwnerName,
	ConfigureType&& Configure,
	VerifyType&& Verify)
{
	const EOptionalFixtureState States[] = {
		EOptionalFixtureState::Unset,
		EOptionalFixtureState::SetDefault,
		EOptionalFixtureState::SetNonDefault};

	for (const EOptionalFixtureState State : States)
	{
		OwnerType Source;
		Configure(Source, State);
		Verify(Source, State, FString::Printf(TEXT("%s source %s"), OwnerName, LexToString(State)));

		OwnerType NativeCopy;
		Configure(NativeCopy, EOptionalFixtureState::SetNonDefault);
		NativeCopy = Source;
		Verify(NativeCopy, State, FString::Printf(TEXT("%s native copy %s"), OwnerName, LexToString(State)));

		OwnerType ReflectedCopy;
		Configure(ReflectedCopy, EOptionalFixtureState::SetNonDefault);
		OwnerType::StaticStruct()->CopyScriptStruct(&ReflectedCopy, &Source);
		Verify(ReflectedCopy, State, FString::Printf(TEXT("%s reflected copy %s"), OwnerName, LexToString(State)));

		TArray<uint8> Bytes;
		OwnerType MutableSource = Source;
		{
			FMemoryWriter Writer(Bytes, true);
			OwnerType::StaticStruct()->SerializeBin(Writer, &MutableSource);
		}

		OwnerType SerializedCopy;
		Configure(SerializedCopy, EOptionalFixtureState::SetNonDefault);
		{
			FMemoryReader Reader(Bytes, true);
			OwnerType::StaticStruct()->SerializeBin(Reader, &SerializedCopy);
		}
		Verify(SerializedCopy, State, FString::Printf(TEXT("%s serialized copy %s"), OwnerName, LexToString(State)));
	}
}

void SetFloat(FDesignOptionalFloat& Optional, const EOptionalFixtureState State, const float NonDefaultValue)
{
	Optional = NonDefaultValue;
	if (State == EOptionalFixtureState::Unset)
	{
		Optional.Reset();
	}
	else if (State == EOptionalFixtureState::SetDefault)
	{
		Optional = 0.0f;
	}
}

void SetBool(FDesignOptionalBool& Optional, const EOptionalFixtureState State)
{
	Optional = true;
	if (State == EOptionalFixtureState::Unset)
	{
		Optional.Reset();
	}
	else if (State == EOptionalFixtureState::SetDefault)
	{
		Optional = false;
	}
}

void SetVector(
	FDesignOptionalVector2D& Optional,
	const EOptionalFixtureState State,
	const FVector2D& NonDefaultValue)
{
	Optional = NonDefaultValue;
	if (State == EOptionalFixtureState::Unset)
	{
		Optional.Reset();
	}
	else if (State == EOptionalFixtureState::SetDefault)
	{
		Optional = FVector2D::ZeroVector;
	}
}

void CheckFloat(
	FAutomationTestBase& Test,
	const FString& Context,
	const TCHAR* FieldName,
	const FDesignOptionalFloat& Optional,
	const EOptionalFixtureState State,
	const float NonDefaultValue)
{
	const bool bExpectedSet = State != EOptionalFixtureState::Unset;
	const float ExpectedValue = State == EOptionalFixtureState::SetNonDefault ? NonDefaultValue : 0.0f;
	Test.TestEqual(FString::Printf(TEXT("%s %s presence"), *Context, FieldName), Optional.IsSet(), bExpectedSet);
	Test.TestTrue(
		FString::Printf(TEXT("%s %s stored value"), *Context, FieldName),
		FMath::IsNearlyEqual(Optional.Value, ExpectedValue));
	if (bExpectedSet)
	{
		Test.TestTrue(
			FString::Printf(TEXT("%s %s exposed value"), *Context, FieldName),
			FMath::IsNearlyEqual(Optional.GetValue(), ExpectedValue));
	}
}

void CheckBool(
	FAutomationTestBase& Test,
	const FString& Context,
	const TCHAR* FieldName,
	const FDesignOptionalBool& Optional,
	const EOptionalFixtureState State)
{
	const bool bExpectedSet = State != EOptionalFixtureState::Unset;
	const bool bExpectedValue = State == EOptionalFixtureState::SetNonDefault;
	Test.TestEqual(FString::Printf(TEXT("%s %s presence"), *Context, FieldName), Optional.IsSet(), bExpectedSet);
	Test.TestEqual(FString::Printf(TEXT("%s %s stored value"), *Context, FieldName), Optional.Value, bExpectedValue);
	if (bExpectedSet)
	{
		Test.TestEqual(
			FString::Printf(TEXT("%s %s exposed value"), *Context, FieldName), Optional.GetValue(), bExpectedValue);
	}
}

void CheckVector(
	FAutomationTestBase& Test,
	const FString& Context,
	const TCHAR* FieldName,
	const FDesignOptionalVector2D& Optional,
	const EOptionalFixtureState State,
	const FVector2D& NonDefaultValue)
{
	const bool bExpectedSet = State != EOptionalFixtureState::Unset;
	const FVector2D ExpectedValue =
		State == EOptionalFixtureState::SetNonDefault ? NonDefaultValue : FVector2D::ZeroVector;
	Test.TestEqual(FString::Printf(TEXT("%s %s presence"), *Context, FieldName), Optional.IsSet(), bExpectedSet);
	Test.TestTrue(
		FString::Printf(TEXT("%s %s stored value"), *Context, FieldName), Optional.Value.Equals(ExpectedValue));
	if (bExpectedSet)
	{
		Test.TestTrue(
			FString::Printf(TEXT("%s %s exposed value"), *Context, FieldName),
			Optional.GetValue().Equals(ExpectedValue));
	}
}

FDesignFill MakeNestedGradientFill()
{
	FDesignFill Fill;
	Fill.Type = EDesignFillType::Gradient;
	Fill.Opacity = 0.625f;
	Fill.BlendMode = TEXT("MULTIPLY");
	Fill.Gradient.GradientType = EDesignGradientType::Radial;
	Fill.Gradient.Center = FVector2D(0.25f, 0.75f);
	Fill.Gradient.Size = FVector2D(0.8f, 0.6f);
	FDesignGradientStop& FirstStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
	FirstStop.Color = TEXT("#11223344");
	FirstStop.Position = 0.2f;
	FDesignGradientStop& SecondStop = Fill.Gradient.Colors.AddDefaulted_GetRef();
	SecondStop.Color = TEXT("#AABBCCDD");
	SecondStop.Position = 0.9f;
	return Fill;
}

void SetFill(FDesignOptionalFill& Optional, const EOptionalFixtureState State)
{
	Optional = MakeNestedGradientFill();
	if (State == EOptionalFixtureState::Unset)
	{
		Optional.Reset();
	}
	else if (State == EOptionalFixtureState::SetDefault)
	{
		Optional = FDesignFill();
	}
}

void CheckFill(
	FAutomationTestBase& Test,
	const FString& Context,
	const FDesignOptionalFill& Optional,
	const EOptionalFixtureState State)
{
	const bool bExpectedSet = State != EOptionalFixtureState::Unset;
	Test.TestEqual(FString::Printf(TEXT("%s Fill presence"), *Context), Optional.IsSet(), bExpectedSet);
	if (State != EOptionalFixtureState::SetNonDefault)
	{
		Test.TestEqual(
			FString::Printf(TEXT("%s Fill stored default type"), *Context),
			Optional.Value.Type,
			EDesignFillType::None);
		return;
	}

	const FDesignFill& Fill = Optional.GetValue();
	Test.TestEqual(FString::Printf(TEXT("%s Fill type"), *Context), Fill.Type, EDesignFillType::Gradient);
	Test.TestTrue(FString::Printf(TEXT("%s Fill opacity"), *Context), FMath::IsNearlyEqual(Fill.Opacity, 0.625f));
	Test.TestEqual(FString::Printf(TEXT("%s Fill blend mode"), *Context), Fill.BlendMode, FString(TEXT("MULTIPLY")));
	Test.TestEqual(FString::Printf(TEXT("%s Fill stop count"), *Context), Fill.Gradient.Colors.Num(), 2);
	CheckVector(
		Test,
		Context + TEXT(" Fill gradient"),
		TEXT("Center"),
		Fill.Gradient.Center,
		EOptionalFixtureState::SetNonDefault,
		FVector2D(0.25f, 0.75f));
	CheckVector(
		Test,
		Context + TEXT(" Fill gradient"),
		TEXT("Size"),
		Fill.Gradient.Size,
		EOptionalFixtureState::SetNonDefault,
		FVector2D(0.8f, 0.6f));
	if (Fill.Gradient.Colors.Num() == 2)
	{
		Test.TestEqual(
			FString::Printf(TEXT("%s Fill first stop color"), *Context),
			Fill.Gradient.Colors[0].Color,
			FString(TEXT("#11223344")));
		Test.TestTrue(
			FString::Printf(TEXT("%s Fill second stop position"), *Context),
			FMath::IsNearlyEqual(Fill.Gradient.Colors[1].Position, 0.9f));
	}
}
}

static_assert(std::is_same_v<decltype(FDesignGradient::StartPoint), FDesignOptionalVector2D>);
static_assert(std::is_same_v<decltype(FDesignImageFill::ScalingFactor), FDesignOptionalFloat>);
static_assert(std::is_same_v<decltype(FDesignStroke::Fill), FDesignOptionalFill>);
static_assert(std::is_same_v<decltype(FDesignTextRun::bUnderline), FDesignOptionalBool>);
static_assert(std::is_same_v<decltype(FUMGPrototypeTransitionData::HasMatchLayers), FDesignOptionalBool>);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignGradientOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.DesignGradient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignGradientOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FDesignGradient& Value, const EOptionalFixtureState State)
	{
		SetVector(Value.StartPoint, State, FVector2D(0.1f, 0.2f));
		SetVector(Value.EndPoint, State, FVector2D(0.8f, 0.9f));
		SetVector(Value.Center, State, FVector2D(0.25f, 0.75f));
		SetVector(Value.Size, State, FVector2D(0.6f, 0.4f));
	};
	auto Verify = [this](const FDesignGradient& Value, const EOptionalFixtureState State, const FString& Context)
	{
		CheckVector(*this, Context, TEXT("StartPoint"), Value.StartPoint, State, FVector2D(0.1f, 0.2f));
		CheckVector(*this, Context, TEXT("EndPoint"), Value.EndPoint, State, FVector2D(0.8f, 0.9f));
		CheckVector(*this, Context, TEXT("Center"), Value.Center, State, FVector2D(0.25f, 0.75f));
		CheckVector(*this, Context, TEXT("Size"), Value.Size, State, FVector2D(0.6f, 0.4f));
	};
	VerifyCopyAndSerializationPaths<FDesignGradient>(*this, TEXT("FDesignGradient"), Configure, Verify);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignImageFillOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.DesignImageFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignImageFillOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FDesignImageFill& Value, const EOptionalFixtureState State)
	{
		SetFloat(Value.ScalingFactor, State, 1.75f);
	};
	auto Verify = [this](const FDesignImageFill& Value, const EOptionalFixtureState State, const FString& Context)
	{
		CheckFloat(*this, Context, TEXT("ScalingFactor"), Value.ScalingFactor, State, 1.75f);
	};
	VerifyCopyAndSerializationPaths<FDesignImageFill>(*this, TEXT("FDesignImageFill"), Configure, Verify);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignStrokeOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.DesignStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignStrokeOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FDesignStroke& Value, const EOptionalFixtureState State)
	{
		SetFill(Value.Fill, State);
	};
	auto Verify = [this](const FDesignStroke& Value, const EOptionalFixtureState State, const FString& Context)
	{
		CheckFill(*this, Context, Value.Fill, State);
	};
	VerifyCopyAndSerializationPaths<FDesignStroke>(*this, TEXT("FDesignStroke"), Configure, Verify);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesignTextRunOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.DesignTextRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignTextRunOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FDesignTextRun& Value, const EOptionalFixtureState State)
	{
		SetFloat(Value.FontSize, State, 28.0f);
		SetFloat(Value.LineHeight, State, 1.4f);
		SetFloat(Value.LetterSpacing, State, -0.75f);
		SetBool(Value.bUnderline, State);
		SetBool(Value.bStrikethrough, State);
	};
	auto Verify = [this](const FDesignTextRun& Value, const EOptionalFixtureState State, const FString& Context)
	{
		CheckFloat(*this, Context, TEXT("FontSize"), Value.FontSize, State, 28.0f);
		CheckFloat(*this, Context, TEXT("LineHeight"), Value.LineHeight, State, 1.4f);
		CheckFloat(*this, Context, TEXT("LetterSpacing"), Value.LetterSpacing, State, -0.75f);
		CheckBool(*this, Context, TEXT("bUnderline"), Value.bUnderline, State);
		CheckBool(*this, Context, TEXT("bStrikethrough"), Value.bStrikethrough, State);
	};
	VerifyCopyAndSerializationPaths<FDesignTextRun>(*this, TEXT("FDesignTextRun"), Configure, Verify);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGPrototypeTransitionOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.UMGPrototypeTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGPrototypeTransitionOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FUMGPrototypeTransitionData& Value, const EOptionalFixtureState State)
	{
		SetBool(Value.HasMatchLayers, State);
	};
	auto Verify = [this](
		const FUMGPrototypeTransitionData& Value,
		const EOptionalFixtureState State,
		const FString& Context)
	{
		CheckBool(*this, Context, TEXT("HasMatchLayers"), Value.HasMatchLayers, State);
	};
	VerifyCopyAndSerializationPaths<FUMGPrototypeTransitionData>(
		*this, TEXT("FUMGPrototypeTransitionData"), Configure, Verify);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUMGSourceNodeOptionalReflectionTest,
	"Design2UMG.Reflection.OptionalPersistence.UMGSourceNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGSourceNodeOptionalReflectionTest::RunTest(const FString& Parameters)
{
	auto Configure = [](FUMGSourceNodeData& Value, const EOptionalFixtureState State)
	{
		SetBool(Value.HasIsExposedInstance, State);
	};
	auto Verify = [this](const FUMGSourceNodeData& Value, const EOptionalFixtureState State, const FString& Context)
	{
		CheckBool(*this, Context, TEXT("HasIsExposedInstance"), Value.HasIsExposedInstance, State);
	};
	VerifyCopyAndSerializationPaths<FUMGSourceNodeData>(*this, TEXT("FUMGSourceNodeData"), Configure, Verify);
	return true;
}

#endif
