// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigationInput.h"

#include "Design2UMGPrototypeNavigationExtensions.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"

namespace
{
	TMap<TWeakObjectPtr<UDesign2UMGPrototypeNavigationUserWidgetExtension>, TArray<int32>>
		GPrototypeNavigationHistoryByExtension;
	TArray<TWeakObjectPtr<UDesign2UMGPrototypeNavigationUserWidgetExtension>>
		GPrototypeKeyboardExtensions;

	class FDesign2UMGPrototypeKeyboardInputProcessor final : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override
		{
		}

		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InKeyEvent) override
		{
			return RouteKeyEvent(InKeyEvent.GetKey(), true);
		}

		virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& InKeyEvent) override
		{
			return RouteKeyEvent(InKeyEvent.GetKey(), false);
		}

		virtual const TCHAR* GetDebugName() const override
		{
			return TEXT("Design2UMGPrototypeKeyboardInputProcessor");
		}

	private:
		static bool RouteKeyEvent(const FKey& Key, const bool bIsKeyDown)
		{
			bool bHandled = false;
			for (int32 Index = GPrototypeKeyboardExtensions.Num() - 1; Index >= 0; --Index)
			{
				TWeakObjectPtr<UDesign2UMGPrototypeNavigationUserWidgetExtension>& ExtensionPtr =
					GPrototypeKeyboardExtensions[Index];
				UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension = ExtensionPtr.Get();
				if (!Extension)
				{
					GPrototypeKeyboardExtensions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
					continue;
				}

				if (Extension->ShouldCaptureKeyboardInput())
				{
					bHandled = Extension->HandleKeyboardInput(Key, bIsKeyDown) || bHandled;
				}
			}

			return bHandled;
		}
	};

	TSharedPtr<FDesign2UMGPrototypeKeyboardInputProcessor> GPrototypeKeyboardInputProcessor;

	void CompactPrototypeNavigationHistory()
	{
		for (auto It = GPrototypeNavigationHistoryByExtension.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	TArray<int32>& GetPrototypeNavigationHistory(UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension)
	{
		CompactPrototypeNavigationHistory();
		return GPrototypeNavigationHistoryByExtension.FindOrAdd(Extension);
	}

	FString NormalizePrototypeKeyToken(const FString& RawToken)
	{
		FString Normalized = RawToken.ToUpper();
		Normalized.ReplaceInline(TEXT("_"), TEXT(""));
		Normalized.ReplaceInline(TEXT("-"), TEXT(""));
		Normalized.ReplaceInline(TEXT(" "), TEXT(""));
		return Normalized;
	}

	void EnsureKeyboardInputProcessorRegistered()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		if (!GPrototypeKeyboardInputProcessor.IsValid())
		{
			GPrototypeKeyboardInputProcessor = MakeShared<FDesign2UMGPrototypeKeyboardInputProcessor>();
			FSlateApplication::Get().RegisterInputPreProcessor(GPrototypeKeyboardInputProcessor);
		}
	}
}

void Design2UMGPrototypeNavigationInput::ResetHistory(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension)
{
	GetPrototypeNavigationHistory(Extension).Reset();
}

void Design2UMGPrototypeNavigationInput::PushHistory(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension,
	const int32 PageIndex)
{
	if (PageIndex < 0)
	{
		return;
	}

	TArray<int32>& History = GetPrototypeNavigationHistory(Extension);
	if (History.Num() == 0 || History.Last() != PageIndex)
	{
		History.Add(PageIndex);
	}
}

bool Design2UMGPrototypeNavigationInput::TryPopHistory(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension,
	const int32 CurrentPageIndex,
	int32& OutPageIndex)
{
	TArray<int32>& History = GetPrototypeNavigationHistory(Extension);
	while (History.Num() > 0)
	{
		const int32 Candidate = History.Pop(EAllowShrinking::No);
		if (Candidate >= 0 && Candidate != CurrentPageIndex)
		{
			OutPageIndex = Candidate;
			return true;
		}
	}

	return false;
}

bool Design2UMGPrototypeNavigationInput::MatchesKeyCode(
	const FKey& Key,
	const FString& PrototypeKeyCode)
{
	const FString Desired = NormalizePrototypeKeyToken(PrototypeKeyCode);
	const FString KeyName = NormalizePrototypeKeyToken(Key.GetFName().ToString());
	const FString DisplayName = NormalizePrototypeKeyToken(Key.GetDisplayName().ToString());
	if (Desired == KeyName || Desired == DisplayName)
	{
		return true;
	}

	if (Desired == TEXT("SPACE"))
	{
		return Key == EKeys::SpaceBar;
	}
	if (Desired == TEXT("ENTER") || Desired == TEXT("RETURN"))
	{
		return Key == EKeys::Enter;
	}
	if (Desired == TEXT("ESC") || Desired == TEXT("ESCAPE"))
	{
		return Key == EKeys::Escape;
	}
	if (Desired == TEXT("ARROWLEFT"))
	{
		return Key == EKeys::Left;
	}
	if (Desired == TEXT("ARROWRIGHT"))
	{
		return Key == EKeys::Right;
	}
	if (Desired == TEXT("ARROWUP"))
	{
		return Key == EKeys::Up;
	}
	if (Desired == TEXT("ARROWDOWN"))
	{
		return Key == EKeys::Down;
	}
	if (Desired == TEXT("CTRL") || Desired == TEXT("CONTROL"))
	{
		return Key == EKeys::LeftControl || Key == EKeys::RightControl;
	}
	if (Desired == TEXT("SHIFT"))
	{
		return Key == EKeys::LeftShift || Key == EKeys::RightShift;
	}
	if (Desired == TEXT("ALT") || Desired == TEXT("OPTION"))
	{
		return Key == EKeys::LeftAlt || Key == EKeys::RightAlt;
	}
	if (Desired == TEXT("CMD") || Desired == TEXT("COMMAND") || Desired == TEXT("META"))
	{
		return Key == EKeys::LeftCommand || Key == EKeys::RightCommand;
	}
	if (Desired == TEXT("BACKSPACE"))
	{
		return Key == EKeys::BackSpace;
	}
	if (Desired == TEXT("DELETE"))
	{
		return Key == EKeys::Delete;
	}
	if (Desired == TEXT("TAB"))
	{
		return Key == EKeys::Tab;
	}

	if (Desired.StartsWith(TEXT("DIGIT")) && Desired.Len() == 6)
	{
		static const FKey DigitKeys[] = {
			EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
			EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		const TCHAR DigitChar = Desired[5];
		if (DigitChar >= TEXT('0') && DigitChar <= TEXT('9'))
		{
			return Key == DigitKeys[DigitChar - TEXT('0')];
		}
	}

	return false;
}

void Design2UMGPrototypeNavigationInput::RegisterKeyboardExtension(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension)
{
	if (!Extension)
	{
		return;
	}

	EnsureKeyboardInputProcessorRegistered();
	GPrototypeKeyboardExtensions.RemoveAllSwap(
		[](const TWeakObjectPtr<UDesign2UMGPrototypeNavigationUserWidgetExtension>& ExtensionPtr)
		{
			return !ExtensionPtr.IsValid();
		},
		EAllowShrinking::No);
	GPrototypeKeyboardExtensions.AddUnique(Extension);
}

void Design2UMGPrototypeNavigationInput::UnregisterKeyboardExtension(
	UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension)
{
	GPrototypeKeyboardExtensions.RemoveAllSwap(
		[Extension](const TWeakObjectPtr<UDesign2UMGPrototypeNavigationUserWidgetExtension>& ExtensionPtr)
		{
			return !ExtensionPtr.IsValid() || ExtensionPtr.Get() == Extension;
		},
		EAllowShrinking::No);

	if (GPrototypeKeyboardExtensions.Num() == 0
		&& GPrototypeKeyboardInputProcessor.IsValid()
		&& FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(GPrototypeKeyboardInputProcessor);
		GPrototypeKeyboardInputProcessor.Reset();
	}
}
