// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73BeamStage45PlacementFreeze.h"

namespace ABTSM73BeamStage5Tests
{
	constexpr double StaticSealToleranceCM = 1.0e-3;

	bool ContainsBox(
		const FBox& Outer,
		const FBox& Inner,
		const double Tolerance = StaticSealToleranceCM)
	{
		return Outer.IsValid && Inner.IsValid
			&& Inner.Min.X >= Outer.Min.X - Tolerance
			&& Inner.Min.Y >= Outer.Min.Y - Tolerance
			&& Inner.Min.Z >= Outer.Min.Z - Tolerance
			&& Inner.Max.X <= Outer.Max.X + Tolerance
			&& Inner.Max.Y <= Outer.Max.Y + Tolerance
			&& Inner.Max.Z <= Outer.Max.Z + Tolerance;
	}

	void AppendBox(FBox& Aggregate, const FBox& Box)
	{
		if (Box.IsValid)
		{
			Aggregate += Box.Min;
			Aggregate += Box.Max;
		}
	}

	FVector2D RequiredHalfExtentXY(const FBox& Bounds)
	{
		return Bounds.IsValid
			? FVector2D(
				FMath::Max(FMath::Abs(Bounds.Min.X), FMath::Abs(Bounds.Max.X)),
				FMath::Max(FMath::Abs(Bounds.Min.Y), FMath::Abs(Bounds.Max.Y)))
			: FVector2D::ZeroVector;
	}

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
		Test.TestEqual(*(Prefix + TEXT(" frozen Stage4 prefix count")),
			Result.Stage4ActiveMemberCount, Frozen.ActiveMemberCount);
		const int32 ExpectedProductionCount = Result.Stage4ActiveMemberCount
			+ Result.ReachabilitySupportPostCount
			+ Result.StructuralClosureMemberCount;
		Test.TestEqual(*(Prefix + TEXT(" explicit production count")),
			Result.CompactAssembly.Members.Num(), ExpectedProductionCount);
		Test.TestEqual(*(Prefix + TEXT(" brick count")),
			Result.Bricks.Num(), ExpectedProductionCount);
		Test.TestEqual(*(Prefix + TEXT(" load node count")),
			Result.LoadDAG.Nodes.Num(), ExpectedProductionCount);
		Test.TestTrue(*(Prefix + TEXT(" production remains in profile budget")),
			Result.Bricks.Num() <= Result.Summary.TargetMaximumBrickCount);
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
			TEXT(":Stage4Active=%d:Reachability=%d:StructuralClosure=%d")
			TEXT(":Production=%d:Contacts=%d:Ground=%d:Geometry=%llu:Bearing=%llu:Load=%lld:ProductionHash=%llu:Chaos=NotEvaluated"),
			*Entry.StableId.ToString(),
			*Entry.Settings.GameplayProfileId.ToString(),
			Entry.Settings.DifficultyTier, Entry.Settings.BuildingSeed,
			Result.Stage4.Plan.Members.Num(),
			Result.SuppressedStage4MemberCount,
			Result.Stage4ActiveMemberCount,
			Result.ReachabilitySupportPostCount,
			Result.StructuralClosureMemberCount,
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
	FABTSM73BeamJ4StaticSealBoundsAndPadTest,
	"ABTS.M73DAG.BeamC3V3.Demo.J4StaticSeal.BoundsAndPad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamJ4StaticSealBoundsAndPadTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73BeamStage5Tests;
	bool bAllPassed = true;
	int32 DynamicEnvelopeRequiredCount = 0;
	for (const FABTSM73BeamDemoManifestEntry& Entry
		: FABTSM73BeamDemoManifest::GetEntries())
	{
		FABTSM73BeamStage45PlacementDescriptor Frozen;
		FABTSM73BeamD1Stage55Result Result;
		FString Error;
		if (!FABTSM73BeamStage45PlacementFreeze::ResolveFrozen(
				Entry.Id, Frozen, Error))
		{
			AddError(FString::Printf(TEXT("%s frozen descriptor: %s"),
				*Entry.StableId.ToString(), *Error));
			bAllPassed = false;
			continue;
		}
		if (!FABTSM73BeamD1BrickCompiler().GenerateStage55DeviceAssembly(
				Entry.Settings, Result, Error))
		{
			AddError(FString::Printf(TEXT("%s Stage 5.5: %s"),
				*Entry.StableId.ToString(), *Error));
			bAllPassed = false;
			continue;
		}

		FBox PhysicalBounds(EForceInit::ForceInit);
		for (const FABTSM73BeamD1BrickBinding& Brick : Result.Stage5.Bricks)
		{
			AppendBox(PhysicalBounds, Brick.LocalBounds);
		}
		FBox EffectBounds(EForceInit::ForceInit);
		for (const FABTSM73BeamD1DeviceBinding& Device : Result.Devices)
		{
			AppendBox(PhysicalBounds, Device.LocalBounds);
			AppendBox(EffectBounds, Device.EffectCorridorLocalBounds);
			bAllPassed = TestTrue(
				*FString::Printf(TEXT("%s effect corridor contains its device"),
					*Entry.StableId.ToString()),
				ContainsBox(Device.EffectCorridorLocalBounds, Device.LocalBounds))
				&& bAllPassed;
		}

		const FVector2D PhysicalHalfExtent = RequiredHalfExtentXY(PhysicalBounds);
		const FVector2D EffectHalfExtent = RequiredHalfExtentXY(EffectBounds);
		const FVector2D PadMargin = Frozen.RequiredPadHalfExtentCM
			- PhysicalHalfExtent;
		const FVector2D EffectPadMargin = Frozen.RequiredPadHalfExtentCM
			- EffectHalfExtent;
		const bool bPhysicalInsideFrozenBounds =
			ContainsBox(Frozen.LocalBounds, PhysicalBounds);
		const bool bRetainsStaticSafetyMargin =
			PadMargin.X + StaticSealToleranceCM
				>= FABTSM73BeamStage45PlacementFreeze::PadSafetyMarginCM
			&& PadMargin.Y + StaticSealToleranceCM
				>= FABTSM73BeamStage45PlacementFreeze::PadSafetyMarginCM;
		const bool bEffectInsidePad = EffectBounds.IsValid
			&& EffectPadMargin.X >= -StaticSealToleranceCM
			&& EffectPadMargin.Y >= -StaticSealToleranceCM;

		bAllPassed = TestTrue(
			*FString::Printf(TEXT("%s physical Stage 5/5.5 bounds stay inside frozen LocalBounds"),
				*Entry.StableId.ToString()),
			bPhysicalInsideFrozenBounds) && bAllPassed;
		bAllPassed = TestTrue(
			*FString::Printf(TEXT("%s physical Stage 5/5.5 bounds retain the 36 cm Pad margin"),
				*Entry.StableId.ToString()),
			bRetainsStaticSafetyMargin) && bAllPassed;
		DynamicEnvelopeRequiredCount += bEffectInsidePad ? 0 : 1;

		AddInfo(FString::Printf(
			TEXT("J4StaticSeal Entry=%s Tier=%d Seed=%d Catalog=%llu Descriptor=%llu")
			TEXT(" Production=%llu Device=%llu PhysicalMin=%s PhysicalMax=%s")
			TEXT(" Pad=%s PadMargin=%s EffectMin=%s EffectMax=%s EffectPadMargin=%s")
			TEXT(" StaticAccepted=%d DynamicEnvelopeRequired=%d Chaos=NotEvaluated"),
			*Entry.StableId.ToString(), Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash,
			Frozen.DescriptorHash, Result.Stage5.ProductionIdentityHash,
			Result.DeviceAssemblyHash, *PhysicalBounds.Min.ToString(),
			*PhysicalBounds.Max.ToString(),
			*Frozen.RequiredPadHalfExtentCM.ToString(), *PadMargin.ToString(),
			*EffectBounds.Min.ToString(), *EffectBounds.Max.ToString(),
			*EffectPadMargin.ToString(),
			bPhysicalInsideFrozenBounds && bRetainsStaticSafetyMargin ? 1 : 0,
			bEffectInsidePad ? 0 : 1));
	}
	AddInfo(FString::Printf(
		TEXT("J4StaticSealSummary Buildings=%d DynamicEnvelopeRequired=%d")
		TEXT(" Catalog=%llu Authority=StaticOnly Chaos=NotEvaluated"),
		FABTSM73BeamDemoManifest::GetEntries().Num(),
		DynamicEnvelopeRequiredCount,
		FABTSM73BeamStage45PlacementFreeze::FrozenCatalogHash));
	return bAllPassed;
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
