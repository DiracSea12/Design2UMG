// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenToUMGSettings.h"

UPenToUMGSettings* UPenToUMGSettings::Get()
{
	return GetMutableDefault<UPenToUMGSettings>();
}
