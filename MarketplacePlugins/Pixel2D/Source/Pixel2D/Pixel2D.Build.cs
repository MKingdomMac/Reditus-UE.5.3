// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Pixel2D : ModuleRules
{
	public Pixel2D(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateIncludePaths.Add("Pixel2D/Private");
        PrivateIncludePaths.Add("Pixel2D/Classes");

        bLegacyParentIncludePaths = true;

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "RHI",
                "SlateCore",
                "Slate",
                "Paper2D"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Renderer",
                "Paper2D"
            }
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "Paper2D"
        });

        PublicIncludePathModuleNames.AddRange(
            new string[] {
                "Paper2D"
         });
    }
}
