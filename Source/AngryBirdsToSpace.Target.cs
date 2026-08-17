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
		// Keep release-blocking startup/authority diagnostics available in the
		// packaged Shipping build. Console and developer features remain disabled.
		bUseLoggingInShipping = true;
		ExtraModuleNames.Add("AngryBirdsToSpace");
	}
}
