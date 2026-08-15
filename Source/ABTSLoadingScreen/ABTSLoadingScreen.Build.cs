// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ABTSLoadingScreen : ModuleRules
{
	public ABTSLoadingScreen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"MoviePlayer",
			"Slate",
			"SlateCore",
		});
	}
}
