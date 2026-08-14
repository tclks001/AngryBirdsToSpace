// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73BeamStage45PlacementFreeze.h"

namespace ABTSM73BeamStage5Tests
{
	bool ValidateEntry(
		FAutomationTestBase& Test,
		const FABTSM73BeamDemoManifestEntry& Entry)
	{
		FABTSM73BeamStage45PlacementDescriptor Frozen;
		FString Error;
		if (!FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
			Entry.Id, Frozen, Error))
		{
			Test.AddError(FString::Printf(TEXT("%s frozen descriptor: %s"),
				*Entry.StableId.ToString(), *Error));
			return false;
		}
		FABTSM73BeamD1Stage5Result Result;
		const bool bGenerated = FABTSM73BeamD1BrickCompiler().GenerateStage5(
			Entry.Settings, Result, Error);
		Test.TestTrue(*FString::Printf(TEXT("%s Stage5 accepts: %s"),
			*Entry.StableId.ToString(), *Error), bGenerated);
		if (!bGenerated)
		{
			return false;
		}
		const FString Prefix = Entry.StableId.ToString();
		Test.TestTrue(*(Prefix + TEXT(" summary accepted")),
			Result.Summary.bAccepted);
		Test.TestTrue(*(Prefix + TEXT(" static DAG evaluated")),
			Result.Summary.bStageStaticDAGEvaluated);
		Test.TestFalse(*(Prefix + TEXT(" Chaos remains unevaluated")),
			Result.bPhysicalStabilityEvaluated);
		Test.TestEqual(*(Prefix + TEXT(" active geometry hash")),
			Result.ActiveGeometryHash, Frozen.StaticGeometryHash);
		Test.TestEqual(*(Prefix + TEXT(" compact member count")),
			Result.CompactAssembly.Members.Num(), Frozen.ActiveMemberCount);
		Test.TestEqual(*(Prefix + TEXT(" brick count")),
			Result.Bricks.Num(), Frozen.ActiveMemberCount);
		Test.TestEqual(*(Prefix + TEXT(" load node count")),
			Result.LoadDAG.Nodes.Num(), Frozen.ActiveMemberCount);
		Test.TestEqual(*(Prefix + TEXT(" bearing/load edge parity")),
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Edges.Num());
		Test.TestEqual(*(Prefix + TEXT(" no unreachable load nodes")),
			Result.LoadDAG.Summary.GroundUnreachableNodeCount, 0);
		Test.TestEqual(*(Prefix + TEXT(" no DAG cycles")),
			Result.LoadDAG.Summary.CycleNodeCount, 0);
		Test.TestEqual(*(Prefix + TEXT(" no weakness candidates")),
			Result.Summary.WeaknessCandidateCount, 0);
		Test.TestEqual(*(Prefix + TEXT(" no device roles")),
			Result.Summary.DeviceRoleCount, 0);
		int32 ActiveMappingCount = 0;
		int32 SuppressedMappingCount = 0;
		for (int32 Stage4Index = 0;
			Stage4Index < Result.Stage4ToCompactMember.Num(); ++Stage4Index)
		{
			const bool bSuppressed = Result.Stage4.Plan.Members[Stage4Index]
				.bSuppressedByStage4FacadeToTop;
			const int32 CompactId = Result.Stage4ToCompactMember[Stage4Index];
			if (bSuppressed)
			{
				++SuppressedMappingCount;
				Test.TestEqual(*(Prefix + TEXT(" suppressed maps to none")),
					CompactId, INDEX_NONE);
			}
			else
			{
				Test.TestEqual(*(Prefix + TEXT(" active mapping is dense")),
					CompactId, ActiveMappingCount++);
			}
		}
		Test.TestEqual(*(Prefix + TEXT(" mapping active count")),
			ActiveMappingCount, Frozen.ActiveMemberCount);
		Test.TestEqual(*(Prefix + TEXT(" mapping suppressed count")),
			SuppressedMappingCount, Result.SuppressedStage4MemberCount);
		Test.AddInfo(FString::Printf(
			TEXT("Stage5:%s:Profile=%s:Tier=%d:Seed=%d:Stage4=%d:Suppressed=%d")
			TEXT(":Active=%d:Contacts=%d:Ground=%d:Geometry=%llu:Bearing=%llu:Load=%lld:Production=%llu:Chaos=NotEvaluated"),
			*Entry.StableId.ToString(),
			*Entry.Settings.GameplayProfileId.ToString(),
			Entry.Settings.DifficultyTier, Entry.Settings.BuildingSeed,
			Result.Stage4.Plan.Members.Num(),
			Result.SuppressedStage4MemberCount,
			Result.CompactAssembly.Members.Num(),
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.ActiveGeometryHash, Result.BearingDAGHash,
			Result.LoadDAG.Summary.LoadDAGHash,
			Result.ProductionIdentityHash));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5E1Test,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage5Production.E1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5E1Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5Tests::ValidateEntry(
		*this, FABTSM73BeamDemoManifest::GetEntries()[0]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5SixBuildingsTest,
	"ABTS.M73DAG.BeamC3V3.Demo.Stage5Production.SixBuildings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5SixBuildingsTest::RunTest(const FString& Parameters)
{
	bool bAllPassed = true;
	for (const FABTSM73BeamDemoManifestEntry& Entry
		: FABTSM73BeamDemoManifest::GetEntries())
	{
		bAllPassed = ABTSM73BeamStage5Tests::ValidateEntry(*this, Entry)
			&& bAllPassed;
	}
	return bAllPassed;
}

#endif
