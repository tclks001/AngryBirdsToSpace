// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ABTSRuntime : ModuleRules
{
	public ABTSRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"ABTSRender",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"InputCore",
			"PhysicsCore",
			"ProceduralMeshComponent",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Chaos",
			"Json",
			"RHI",
		});
	}
}
