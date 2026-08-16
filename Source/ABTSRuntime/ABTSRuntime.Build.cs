// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ABTSRuntime : ModuleRules
{
	public ABTSRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// M3's frozen trajectory and layout identities are shared by Editor and
		// packaged Game targets. Keep their floating-point evaluation identical.
		FPSemantics = FPSemanticsMode.Precise;

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
