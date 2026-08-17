// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class AngryBirdsToSpaceTarget : TargetRules
{
	public AngryBirdsToSpaceTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			// Keep release-blocking startup/authority diagnostics available in the
			// packaged Shipping build. The installed engine shares UnrealGame build
			// products, so this target must explicitly accept the logging override.
			// Console and developer gameplay features remain disabled.
			bUseLoggingInShipping = true;
			bOverrideBuildEnvironment = true;
		}
		ExtraModuleNames.Add("AngryBirdsToSpace");
	}
}
