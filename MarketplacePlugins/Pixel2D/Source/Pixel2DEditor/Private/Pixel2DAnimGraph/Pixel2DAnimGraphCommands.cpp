// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pixel2DAnimGraphCommands.h"

#define LOCTEXT_NAMESPACE "Pixel2DAnimGraphCommands"

void FPixel2DAnimGraphCommands::RegisterCommands()
{
	UI_COMMAND(OpenRelatedAsset, "Open Asset", "Opens the asset related to this node", EUserInterfaceActionType::Button, FInputChord())
}

#undef LOCTEXT_NAMESPACE
