// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Misc/AutomationTest.h"

namespace ABTSSlingshotCalibrationTests
{
	/**
	 * Deterministic POD fixture captured from the old-map calibration carrier.
	 * These two terrain deltas belong to test evidence, not to the portable
	 * SatellitePracticePresetHash or production placement.
	 */
	bool MakeReferenceScenario(
		const FABTSSatellitePracticePreset& Preset,
		const FABTSM6LaunchProfileCatalog& Catalog,
		FABTSCalibrationScenario& OutScenario)
	{
		OutScenario = FABTSCalibrationScenario();
		OutScenario.Gravity.PrimaryCenterWorld = FVector::ZeroVector;
		OutScenario.Gravity.PrimaryRadiusCM = 10000.0f;
		OutScenario.Gravity.PrimarySurfaceGravityCMPerSec2 = 980.0f;
		const FVector StartDirection = FVector::ForwardVector;
		const FVector CalibrationForward = FVector::RightVector;
		const FVector CalibrationRight =
			FVector::CrossProduct(
				StartDirection,
				CalibrationForward).GetSafeNormal();
		const FVector ReinforcedSiteDirection =
			(StartDirection * OutScenario.Gravity.PrimaryRadiusCM
				+ CalibrationRight * 620.0f
				+ CalibrationForward * 420.0f).GetSafeNormal();
		const FVector ReinforcedForward =
			FVector::VectorPlaneProject(
				CalibrationForward,
				ReinforcedSiteDirection).GetSafeNormal();
		const FVector ReinforcedRight =
			FVector::CrossProduct(
				ReinforcedSiteDirection,
				ReinforcedForward).GetSafeNormal();
		constexpr float ReferenceLaunchTerrainDeltaCM = 120.3f;
		constexpr float ReferenceSatelliteTerrainDeltaCM = 230.2f;
		const FVector ReinforcedSurface =
			ReinforcedSiteDirection
			* (OutScenario.Gravity.PrimaryRadiusCM
				+ ReferenceLaunchTerrainDeltaCM);
		OutScenario.LaunchFrame.SlingUpWorld = ReinforcedSiteDirection;
		OutScenario.LaunchFrame.SlingForwardWorld = ReinforcedForward;
		OutScenario.LaunchFrame.SlingRightWorld = ReinforcedRight;
		OutScenario.LaunchFrame.SlingCenterWorld =
			ReinforcedSurface + ReinforcedSiteDirection * 220.0f;
		OutScenario.LaunchFrame.RestPouchWorldLocation =
			ReinforcedSurface + ReinforcedSiteDirection * 190.0f;
		OutScenario.LaunchFrame.BirdInPouchOffsetCM = 20.0f;
		const float CameraPitchRadians =
			FMath::DegreesToRadians(Catalog.AimCameraPitchDegrees);
		const FVector CameraBackAndUp =
			(-ReinforcedForward * FMath::Cos(CameraPitchRadians)
				+ ReinforcedSiteDirection
					* FMath::Sin(CameraPitchRadians)).GetSafeNormal();
		const FVector CameraLocation =
			OutScenario.LaunchFrame.SlingCenterWorld
			+ CameraBackAndUp * Catalog.AimCameraDistanceCM;
		const FVector CameraTarget =
			OutScenario.LaunchFrame.SlingCenterWorld
			+ ReinforcedForward
				* Catalog.AimTargetForwardDistanceCM
			+ ReinforcedSiteDirection
				* Catalog.AimTargetHeightCM;
		const FVector CameraLook =
			(CameraTarget - CameraLocation).GetSafeNormal();
		const FVector CameraScreenUp =
			FVector::VectorPlaneProject(
				ReinforcedSiteDirection,
				CameraLook).GetSafeNormal();
		FVector CameraScreenRight =
			FVector::CrossProduct(
				CameraScreenUp,
				CameraLook).GetSafeNormal();
		if (FVector::DotProduct(
			CameraScreenRight,
			ReinforcedRight) < 0.0f)
		{
			CameraScreenRight *= -1.0f;
		}
		OutScenario.LaunchFrame.AimPlaneNormalWorld = CameraLook;
		OutScenario.LaunchFrame.AimInPlaneAxisWorld = CameraScreenUp;
		OutScenario.LaunchFrame.AimOutOfPlaneAxisWorld =
			CameraScreenRight;
		OutScenario.LaunchWorldLocation =
			OutScenario.LaunchFrame.RestPouchWorldLocation;
		const float ArcRadians =
			FMath::DegreesToRadians(Preset.SatelliteAnchorArcDegrees);
		const FVector SatelliteDirection(
			FMath::Cos(ArcRadians),
			FMath::Sin(ArcRadians),
			0.0f);
		OutScenario.Gravity.SatelliteCenterWorld =
			SatelliteDirection
			* (OutScenario.Gravity.PrimaryRadiusCM
				+ ReferenceSatelliteTerrainDeltaCM
				+ OutScenario.Gravity.PrimaryRadiusCM
					* Preset.SatelliteCenterClearancePrimaryRatio);
		OutScenario.Gravity.SatelliteRadiusCM =
			OutScenario.Gravity.PrimaryRadiusCM
			* Preset.SatelliteRadiusPrimaryRatio;
		OutScenario.Gravity.SatelliteSurfaceGravityCMPerSec2 =
			OutScenario.Gravity.PrimarySurfaceGravityCMPerSec2
			* Preset.SatelliteSurfaceGravityPrimaryRatio;
		OutScenario.Gravity.FlightAirDragPerSecond =
			Catalog.FlightAirDragPerSecond;
		OutScenario.Gravity.bSatelliteGravityEnabled = true;
		OutScenario.TargetProxyRadiusCM = Preset.TargetProxyRadiusCM;
		return FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldLocation(
				OutScenario.LaunchFrame.RestPouchWorldLocation,
				OutScenario.Gravity,
				Preset,
				OutScenario.TargetWorldLocation);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSSlingshotCalibrationProfileCatalogTest,
	"ABTS.Calibration.ProfileCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationProfileCatalogTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM6LaunchProfileCatalog Candidate =
		FABTSSlingshotSatelliteCalibrationModel::MakeCandidateCatalogV0();
	FABTSM6LaunchProfileCatalog Resolved;
	FString FailureReason;
	TestTrue(
		TEXT("Candidate catalog resolves"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			Candidate, Resolved, &FailureReason));
	TestEqual(TEXT("Exactly three normal tiers are present"), Resolved.Profiles.Num(), 3);
	const EABTSSlingshotTier Expected[] =
	{
		EABTSSlingshotTier::Twig,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced
	};
	float PreviousComfortableReach = 0.0f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
	{
		if (!Resolved.Profiles.IsValidIndex(Index)) continue;
		const FABTSM6LaunchProfile& Profile = Resolved.Profiles[Index];
		TestEqual(
			FString::Printf(TEXT("Tier %d is ordered"), Index),
			static_cast<uint8>(Profile.Tier),
			static_cast<uint8>(Expected[Index]));
		TestEqual(
			FString::Printf(TEXT("Tier %d pull zero maps to minimum"), Index),
			FABTSSlingshotSatelliteCalibrationModel::EvaluateLaunchSpeed(
				Profile, 0.0f),
			Profile.MinimumSpeedCMPerSec);
		TestEqual(
			FString::Printf(TEXT("Tier %d pull one maps to maximum"), Index),
			FABTSSlingshotSatelliteCalibrationModel::EvaluateLaunchSpeed(
				Profile, 1.0f),
			Profile.MaximumSpeedCMPerSec);
		TestTrue(
			FString::Printf(
				TEXT("Tier %d initial pull is player-enterable"),
				Index),
			Profile.InitialPullAlpha >= 0.0f
				&& Profile.InitialPullAlpha <= 1.0f
				&& Profile.PullPowerWheelStep > 0.0f);
		float PreviousSpeed = 0.0f;
		for (int32 SampleIndex = 0; SampleIndex <= 20; ++SampleIndex)
		{
			const float Speed =
				FABTSSlingshotSatelliteCalibrationModel::EvaluateLaunchSpeed(
					Profile,
					static_cast<float>(SampleIndex) / 20.0f);
			TestTrue(
				FString::Printf(
					TEXT("Tier %d speed curve is monotonic at %d"),
					Index,
					SampleIndex),
				Speed + KINDA_SMALL_NUMBER >= PreviousSpeed);
			PreviousSpeed = Speed;
		}
		const FABTSM6ReachEnvelope Envelope =
			FABTSSlingshotSatelliteCalibrationModel::EstimateReachEnvelope(
				Profile,
				10000.0f,
				980.0f,
				Resolved.FlightAirDragPerSecond);
		TestTrue(
			FString::Printf(TEXT("Tier %d comfortable reach is positive"), Index),
			Envelope.ComfortableReachCM > 0.0f);
		TestTrue(
			FString::Printf(TEXT("Tier %d maximum contains comfortable reach"), Index),
			Envelope.MaximumReachCM + KINDA_SMALL_NUMBER
				>= Envelope.ComfortableReachCM);
		if (Index > 0)
		{
			TestTrue(
				FString::Printf(TEXT("Tier %d preserves at least 25 percent separation"), Index),
				Envelope.ComfortableReachCM
					>= PreviousComfortableReach * 1.25f);
		}
		PreviousComfortableReach = Envelope.ComfortableReachCM;
	}

	FABTSM6LaunchProfileCatalog Invalid = Candidate;
	if (Invalid.Profiles.Num() == 3)
	{
		Invalid.Profiles[1].Tier = Invalid.Profiles[0].Tier;
	}
	TestFalse(
		TEXT("A duplicate tier fails closed"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			Invalid, Resolved, &FailureReason));
	Invalid = Candidate;
	if (!Invalid.Profiles.IsEmpty())
	{
		Invalid.Profiles[0].PullPowerWheelStep = 0.005f;
	}
	TestFalse(
		TEXT("A sub-contract wheel step fails closed"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			Invalid, Resolved, &FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSSlingshotCalibrationStableHashesTest,
	"ABTS.Calibration.StableHashes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationStableHashesTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM6LaunchProfileCatalog Catalog;
	TestTrue(
		TEXT("Candidate resolves for hashing"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			FABTSSlingshotSatelliteCalibrationModel::MakeCandidateCatalogV0(),
			Catalog));
	const uint64 CatalogHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(Catalog);
	TestTrue(TEXT("Catalog hash is nonzero"), CatalogHash != 0);
	TestEqual(
		TEXT("Catalog hash repeats"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(Catalog),
		CatalogHash);
	FABTSM6LaunchProfileCatalog Reordered = Catalog;
	Algo::Reverse(Reordered.Profiles);
	FABTSM6LaunchProfileCatalog ReResolved;
	TestTrue(
		TEXT("Reordered catalog resolves"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			Reordered, ReResolved));
	TestEqual(
		TEXT("Resolved order owns the catalog identity"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			ReResolved),
		CatalogHash);
	ReResolved.Profiles.Last().MaximumSpeedCMPerSec += 1.0f;
	TestNotEqual(
		TEXT("A launch field mutation changes the hash"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			ReResolved),
		CatalogHash);
	ReResolved = Catalog;
	ReResolved.AimCameraPitchDegrees += 1.0f;
	TestNotEqual(
		TEXT("Aim-camera input framing changes the launch hash"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			ReResolved),
		CatalogHash);

	FABTSSatellitePracticePreset Preset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeCandidatePracticePresetV0();
	const uint64 PresetHash =
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(Preset);
	TestTrue(TEXT("Preset hash is nonzero"), PresetHash != 0);
	TestEqual(
		TEXT("Preset hash repeats"),
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(Preset),
		PresetHash);
	Preset.TargetBody = TEXT("PracticeSatelliteChanged");
	TestNotEqual(
		TEXT("Target-body content changes the preset hash"),
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(Preset),
		PresetHash);
	Preset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeCandidatePracticePresetV0();
	Preset.RangeTargetProxyRadiusCM += 1.0f;
	TestNotEqual(
		TEXT("Range-target geometry changes the preset hash"),
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(Preset),
		PresetHash);

	FABTSCalibrationScenario Scenario;
	const FABTSSatellitePracticePreset DefaultPreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeCandidatePracticePresetV0();
	TestTrue(
		TEXT("Reference-carrier gravity fixture builds"),
		ABTSSlingshotCalibrationTests::MakeReferenceScenario(
			DefaultPreset, Catalog, Scenario));
	const uint64 GravityHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			Scenario.Gravity);
	TestTrue(TEXT("Gravity hash is nonzero"), GravityHash != 0);
	FABTSCalibrationGravitySnapshot Translated = Scenario.Gravity;
	const FVector Translation(1234.0f, -987.0f, 432.0f);
	Translated.PrimaryCenterWorld += Translation;
	Translated.SatelliteCenterWorld += Translation;
	TestEqual(
		TEXT("Gravity identity is independent of absolute world origin"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			Translated),
		GravityHash);
	Translated.bSatelliteGravityEnabled = false;
	TestNotEqual(
		TEXT("Gravity enable state changes the snapshot hash"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			Translated),
		GravityHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSSlingshotCalibrationTargetGeometryTest,
	"ABTS.Calibration.TargetGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationTargetGeometryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM6LaunchProfileCatalog Catalog;
	TestTrue(
		TEXT("Candidate resolves for geometry"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			FABTSSlingshotSatelliteCalibrationModel::MakeCandidateCatalogV0(),
			Catalog));
	FABTSSatellitePracticePreset Preset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeCandidatePracticePresetV0();
	FABTSCalibrationScenario Scenario;
	TestTrue(
		TEXT("Backside target builds"),
		ABTSSlingshotCalibrationTests::MakeReferenceScenario(
			Preset, Catalog, Scenario));
	const FVector FacingLaunch =
		(Scenario.LaunchFrame.RestPouchWorldLocation
			- Scenario.Gravity.SatelliteCenterWorld).GetSafeNormal();
	const FVector TargetDirection =
		(Scenario.TargetWorldLocation
			- Scenario.Gravity.SatelliteCenterWorld).GetSafeNormal();
	TestTrue(
		TEXT("Target is at the configured satellite-local radius"),
		FMath::IsNearlyEqual(
			FVector::Distance(
				Scenario.TargetWorldLocation,
				Scenario.Gravity.SatelliteCenterWorld),
			Scenario.Gravity.SatelliteRadiusCM
				+ Preset.TargetAltitudeAboveSurfaceCM,
			0.01f));
	TestTrue(
		TEXT("Target backside angle is satellite-local"),
		FMath::IsNearlyEqual(
			FVector::DotProduct(FacingLaunch, TargetDirection),
			FMath::Cos(FMath::DegreesToRadians(Preset.BacksideAngleDeg)),
			1.0e-4f));
	TestTrue(
		TEXT("Backside target is actually behind the satellite"),
		FVector::DotProduct(FacingLaunch, TargetDirection) < 0.0f);
	TestTrue(
		TEXT("Expanded target remains clear of the expanded satellite"),
		Preset.TargetAltitudeAboveSurfaceCM
			- Preset.TargetProxyRadiusCM
			- Preset.BirdCollisionRadiusCM * 2.0f
			>= Preset.TargetSatelliteClearanceCM);

	const FABTSM6LaunchProfile* Reinforced =
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			Catalog,
			EABTSSlingshotTier::Reinforced);
	FVector SampleBirdWorld;
	FVector SampleVelocity;
	TestTrue(
		TEXT("Actual M6 pouch state converts to a launch sample"),
		Reinforced != nullptr
			&& FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
				Scenario.LaunchFrame,
				*Reinforced,
				0.0f,
				0.0f,
				Reinforced->InitialPullAlpha,
				SampleBirdWorld,
				SampleVelocity));
	if (Reinforced)
	{
		const float ExpectedPullDistance = FMath::Lerp(
			Reinforced->MinimumPullDistanceCM,
			Reinforced->MaximumPullDistanceCM,
			Reinforced->InitialPullAlpha);
		const FVector ExpectedPouch =
			Scenario.LaunchFrame.RestPouchWorldLocation
			- Scenario.LaunchFrame.SlingForwardWorld.GetSafeNormal()
				* ExpectedPullDistance;
		const FVector ExpectedDirection =
			(Scenario.LaunchFrame.SlingCenterWorld
				+ Scenario.LaunchFrame.SlingUpWorld.GetSafeNormal() * 65.0f
				- ExpectedPouch).GetSafeNormal();
		TestTrue(
			TEXT("Launch sample uses the real M6 center/pouch direction"),
			SampleVelocity.GetSafeNormal().Equals(
				ExpectedDirection,
				1.0e-4f));
		TestTrue(
			TEXT("Bird centre preserves the M6 pouch visual offset"),
			FMath::IsNearlyEqual(
				FVector::Distance(SampleBirdWorld, ExpectedPouch),
				Scenario.LaunchFrame.BirdInPouchOffsetCM,
				1.0e-3f));
	}

	const FVector Translation(-700.0f, 400.0f, 1200.0f);
	FABTSCalibrationGravitySnapshot Translated = Scenario.Gravity;
	Translated.PrimaryCenterWorld += Translation;
	Translated.SatelliteCenterWorld += Translation;
	FVector TranslatedTarget;
	TestTrue(
		TEXT("Translated target builds"),
		FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldLocation(
				Scenario.LaunchWorldLocation + Translation,
				Translated,
				Preset,
				TranslatedTarget));
	TestTrue(
		TEXT("Target construction is translation invariant"),
		TranslatedTarget.Equals(
			Scenario.TargetWorldLocation + Translation,
			0.01f));

	FABTSSatellitePracticePreset Invalid = Preset;
	Invalid.TargetAltitudeAboveSurfaceCM =
		Invalid.TargetProxyRadiusCM
		+ Invalid.BirdCollisionRadiusCM * 2.0f
		+ Invalid.TargetSatelliteClearanceCM
		- 1.0f;
	FVector RejectedTarget;
	TestFalse(
		TEXT("A target overlapping the satellite fails closed"),
		FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldLocation(
				Scenario.LaunchWorldLocation,
				Scenario.Gravity,
				Invalid,
				RejectedTarget));
	Invalid = Preset;
	Invalid.TargetBody = TEXT("MisspelledSatellite");
	TestFalse(
		TEXT("An unresolved target body fails closed"),
		FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldLocation(
				Scenario.LaunchWorldLocation,
				Scenario.Gravity,
				Invalid,
				RejectedTarget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSSlingshotCalibrationSuccessIslandTest,
	"ABTS.Calibration.SuccessIsland",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationSuccessIslandTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM6LaunchProfileCatalog Catalog;
	TestTrue(
		TEXT("Candidate resolves for sweep"),
		FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
			FABTSSlingshotSatelliteCalibrationModel::MakeCandidateCatalogV0(),
			Catalog));
	const FABTSSatellitePracticePreset Preset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeCandidatePracticePresetV0();
	FABTSCalibrationScenario Scenario;
	TestTrue(
		TEXT("Reference-carrier sweep scenario builds"),
		ABTSSlingshotCalibrationTests::MakeReferenceScenario(
			Preset, Catalog, Scenario));
	const FABTSCalibrationSweepSummary First =
		FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
			Scenario, Catalog, Preset);
	const FABTSCalibrationSweepSummary Second =
		FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
			Scenario, Catalog, Preset);
	TestEqual(
		TEXT("The player-reachable pull lattice is stable"),
		First.ReinforcedReachablePullSamples,
		Second.ReinforcedReachablePullSamples);
	TestTrue(
		TEXT("Multiple player-enterable pull notches are certified"),
		First.ReinforcedCertifiedPullSamples >= 2);
	TestTrue(
		TEXT("The actual M6 pouch input domain is sampled"),
		First.ReinforcedSampleCount > 0);
	TestTrue(TEXT("The certified sweep passes"), First.bPassed);
	TestTrue(TEXT("Gravity-dependent samples exist"), First.GravityDependentHits > 0);
	TestTrue(
		TEXT("The largest success island meets its size gate"),
		First.LargestSuccessIslandSamples
			>= Preset.MinimumSuccessIslandSamples);
	TestTrue(TEXT("The island spans aim neighbours"), First.bIslandSpansAimNeighbors);
	TestTrue(TEXT("The island spans pull neighbours"), First.bIslandSpansPullNeighbors);
	TestEqual(TEXT("Simple full power cannot solve the target"), First.SimpleFullPowerHits, 0);
	TestEqual(
		TEXT("Reinforced cannot solve outside the certified pull band"),
		First.ReinforcedOutsideCertifiedPullHits,
		0);
	TestTrue(
		TEXT("The successful pull range stays inside 75 to 95 percent"),
		First.SuccessPullMinimum + KINDA_SMALL_NUMBER >= Preset.PullMinimum
			&& First.SuccessPullMaximum
				<= Preset.PullMaximum + KINDA_SMALL_NUMBER);
	TestTrue(
		TEXT("Gravity-off counterparts clearly miss"),
		First.MinimumGravityOffMissCM + KINDA_SMALL_NUMBER
			>= Preset.GravityOffMinimumMissCM);
	TestEqual(TEXT("Sweep result hash repeats"), Second.ResultHash, First.ResultHash);
	TestEqual(
		TEXT("Sweep hit count repeats"),
		Second.GravityDependentHits,
		First.GravityDependentHits);
	return true;
}

#endif
