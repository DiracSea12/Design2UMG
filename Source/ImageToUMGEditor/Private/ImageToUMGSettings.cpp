// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGSettings.h"

UImageToUMGSettings* UImageToUMGSettings::Get()
{
	return GetMutableDefault<UImageToUMGSettings>();
}
