// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Physics/ABTSSweptCollision.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Slingshot/ABTSM6Types.h"

namespace ABTSSlingshotCalibrationTests
{
	class FScopedLaunchProfileWorld
	{
	public:
		FScopedLaunchProfileWorld()
		{
			const UWorld::InitializationValues Values =
				UWorld::InitializationValues()
					.InitializeScenes(false)
					.AllowAudioPlayback(false)
					.RequiresHitProxies(false)
					.CreatePhysicsScene(false)
					.CreateNavigation(false)
					.CreateAISystem(false)
					.ShouldSimulatePhysics(false)
					.EnableTraceCollision(false)
					.SetTransactional(false)
					.CreateFXSystem(false);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM6LaunchProfileAutomationWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FScopedLaunchProfileWorld()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};

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
		OutScenario.TargetHalfExtentCM =
			FVector(Preset.TargetProxyRadiusCM);
		if (!FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldTransform(
				OutScenario.LaunchFrame.RestPouchWorldLocation,
				OutScenario.Gravity,
				Preset,
				OutScenario.TargetWorldTransform))
		{
			return false;
		}
		OutScenario.TargetWorldLocation =
			OutScenario.TargetWorldTransform.GetLocation();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM6ProductionLaunchProfileConsumptionTest,
	"ABTS.M6.LaunchProfiles.ProductionCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM6ProductionLaunchProfileConsumptionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	ABTSSlingshotCalibrationTests::FScopedLaunchProfileWorld TestWorld;
	if (!TestNotNull(TEXT("Production profile test world exists"), TestWorld.Get()))
	{
		return false;
	}
	AABTSM6SlingshotSystem* System =
		TestWorld.Get()->SpawnActor<AABTSM6SlingshotSystem>();
	if (!TestNotNull(TEXT("M6 system spawns"), System))
	{
		return false;
	}

	FABTSM6LaunchProfileCatalog CopiedCatalog;
	uint64 CopiedHash = 0;
	TestFalse(
		TEXT("Unconfigured production system has no catalog"),
		System->CopyLaunchProfileCatalog(CopiedCatalog, CopiedHash));
	const FABTSM6LaunchProfileCatalog FrozenCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	float NativeCameraDistanceCM = 0.0f;
	float NativeCameraPitchDegrees = 0.0f;
	float NativeTargetForwardDistanceCM = 0.0f;
	float NativeTargetHeightCM = 0.0f;
	const AABTSM6SlingshotCamera* NativeCameraDefaults =
		GetDefault<AABTSM6SlingshotCamera>();
	TestTrue(
		TEXT("Native camera defaults expose valid frozen framing"),
		NativeCameraDefaults != nullptr
			&& NativeCameraDefaults->CopyAimFraming(
				NativeCameraDistanceCM,
				NativeCameraPitchDegrees,
				NativeTargetForwardDistanceCM,
				NativeTargetHeightCM));
	TestEqual(
		TEXT("Native camera distance matches frozen production catalog"),
		NativeCameraDistanceCM,
		FrozenCatalog.AimCameraDistanceCM);
	TestEqual(
		TEXT("Native camera pitch matches frozen production catalog"),
		NativeCameraPitchDegrees,
		FrozenCatalog.AimCameraPitchDegrees);
	TestEqual(
		TEXT("Native camera target forward matches frozen production catalog"),
		NativeTargetForwardDistanceCM,
		FrozenCatalog.AimTargetForwardDistanceCM);
	TestEqual(
		TEXT("Native camera target height matches frozen production catalog"),
		NativeTargetHeightCM,
		FrozenCatalog.AimTargetHeightCM);
	TestTrue(
		TEXT("Frozen catalog configures production M6"),
		System->ConfigureLaunchProfiles(FrozenCatalog));
	TestTrue(
		TEXT("Production catalog is exposed read-only"),
		System->CopyLaunchProfileCatalog(CopiedCatalog, CopiedHash));
	TestEqual(
		TEXT("Production catalog keeps the frozen identity"),
		CopiedHash,
		static_cast<uint64>(14031317829084174406ull));
	TestFalse(
		TEXT("Production configuration does not enable calibration mode"),
		System->CopyCalibrationCatalog(CopiedCatalog, CopiedHash));

	TestTrue(
		TEXT("Production catalog remains available after calibration-only copy is rejected"),
		System->CopyLaunchProfileCatalog(CopiedCatalog, CopiedHash));
	const FABTSM6LaunchProfile* Reinforced =
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			CopiedCatalog,
			EABTSSlingshotTier::Reinforced);
	if (TestNotNull(TEXT("Reinforced production profile exists"), Reinforced))
	{
		TestEqual(
			TEXT("Reinforced production maximum is frozen at 3300 cm/s"),
			Reinforced->MaximumSpeedCMPerSec,
			3300.0f);
		TestEqual(
			TEXT("Reinforced production wheel step is frozen at 0.01"),
			Reinforced->PullPowerWheelStep,
			0.01f);
	}
	TestNull(
		TEXT("Space remains outside the normal production catalog"),
		FABTSSlingshotSatelliteCalibrationModel::FindProfile(
			CopiedCatalog,
			EABTSSlingshotTier::Space));

	return true;
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
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	const FABTSM6LaunchProfile DefaultSimpleProfile;
	TestEqual(
		TEXT("New launch profiles default to the Simple wheel step"),
		DefaultSimpleProfile.PullPowerWheelStep,
		0.02f);
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
	const float ExpectedMinimumSpeeds[] = { 700.0f, 900.0f, 1050.0f };
	const float ExpectedMaximumSpeeds[] = { 1700.0f, 2300.0f, 3300.0f };
	const float ExpectedPowerExponents[] = { 1.15f, 1.08f, 1.0f };
	const float ExpectedWheelSteps[] = { 0.04f, 0.02f, 0.01f };
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
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen minimum speed"), Index),
			Profile.MinimumSpeedCMPerSec,
			ExpectedMinimumSpeeds[Index]);
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen maximum speed"), Index),
			Profile.MaximumSpeedCMPerSec,
			ExpectedMaximumSpeeds[Index]);
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen power exponent"), Index),
			Profile.PowerExponent,
			ExpectedPowerExponents[Index]);
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen minimum pull distance"), Index),
			Profile.MinimumPullDistanceCM,
			120.0f);
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen maximum pull distance"), Index),
			Profile.MaximumPullDistanceCM,
			430.0f);
		TestEqual(
			FString::Printf(TEXT("Tier %d frozen wheel step"), Index),
			Profile.PullPowerWheelStep,
			ExpectedWheelSteps[Index]);
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
				Resolved.FlightAirDragPerSecond,
				42.0f);
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
	TestEqual(
		TEXT("Frozen launch catalog has the accepted portable identity"),
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			Candidate),
		14031317829084174406ull);

	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenSatellitePracticePresetV0();
	TestEqual(
		TEXT("Frozen satellite clearance ratio"),
		FrozenPreset.SatelliteCenterClearancePrimaryRatio,
		0.55f);
	TestEqual(
		TEXT("Frozen satellite gravity ratio"),
		FrozenPreset.SatelliteSurfaceGravityPrimaryRatio,
		2.0f);
	TestEqual(
		TEXT("Frozen satellite target azimuth"),
		FrozenPreset.TargetLocalAzimuthDeg,
		20.0f);
	TestEqual(
		TEXT("Frozen satellite certified pull maximum"),
		FrozenPreset.PullMaximum,
		1.0f);
	TestEqual(
		TEXT("Frozen satellite preset has the accepted portable identity"),
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(FrozenPreset),
		11534008174155323086ull);
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
	Preset.BirdCollisionRadiusCM += 1.0f;
	TestNotEqual(
		TEXT("Resolved bird collision geometry changes the preset hash"),
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
		TEXT("E5 cube centre rests one half extent above the satellite"),
		FMath::IsNearlyEqual(
			FVector::Distance(
				Scenario.TargetWorldLocation,
				Scenario.Gravity.SatelliteCenterWorld),
			Scenario.Gravity.SatelliteRadiusCM
				+ Preset.TargetProxyRadiusCM
				+ Preset.TargetSatelliteClearanceCM,
			0.01f));
	TestTrue(
		TEXT("E5 local up follows satellite outward"),
		Scenario.TargetWorldTransform.GetUnitAxis(EAxis::Z).Equals(
			TargetDirection,
			1.0e-4f));
	TestTrue(
		TEXT("E5 lower face rests at the configured surface gap"),
		FMath::IsNearlyEqual(
			FVector::Distance(
				Scenario.TargetWorldLocation,
				Scenario.Gravity.SatelliteCenterWorld)
				- Preset.TargetProxyRadiusCM,
			Scenario.Gravity.SatelliteRadiusCM
				+ Preset.TargetSatelliteClearanceCM,
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
	FTransform TranslatedTargetTransform;
	TestTrue(
		TEXT("Translated target builds"),
		FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldTransform(
				Scenario.LaunchWorldLocation + Translation,
				Translated,
				Preset,
				TranslatedTargetTransform));
	TestTrue(
		TEXT("Target construction is translation invariant"),
		TranslatedTargetTransform.GetLocation().Equals(
			Scenario.TargetWorldLocation + Translation,
			0.01f));
	TestTrue(
		TEXT("Target frame rotation is translation invariant"),
		TranslatedTargetTransform.GetRotation().Equals(
			Scenario.TargetWorldTransform.GetRotation(),
			1.0e-5f));

	FABTSSatellitePracticePreset Invalid = Preset;
	Invalid.TargetProxyRadiusCM = 0.0f;
	FVector RejectedTarget;
	TestFalse(
		TEXT("A non-positive E5 half extent fails closed"),
		FABTSSlingshotSatelliteCalibrationModel::
			BuildSatelliteTargetWorldLocation(
				Scenario.LaunchWorldLocation,
				Scenario.Gravity,
				Invalid,
				RejectedTarget));
	Invalid = Preset;
	Invalid.TargetSatelliteClearanceCM = -1.0f;
	TestFalse(
		TEXT("A negative E5 surface gap fails closed"),
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
	FABTSSlingshotCalibrationSweptCollisionTest,
	"ABTS.Calibration.SweptCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationSweptCollisionTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FVector Start(300.0f, 0.0f, 0.0f);
	const FVector End(-300.0f, 0.0f, 0.0f);
	float SatelliteAlpha = BIG_NUMBER;
	TestTrue(
		TEXT("Bird-centre segment hits the bird-expanded satellite"),
		ABTSSweptCollision::SegmentSphereFirstAlpha(
			Start,
			End,
			FVector::ZeroVector,
			100.0f + 42.0f,
			SatelliteAlpha));
	TestTrue(
		TEXT("Expanded satellite first-contact alpha is deterministic"),
		FMath::IsNearlyEqual(
			SatelliteAlpha,
			158.0f / 600.0f,
			1.0e-5f));

	const FTransform TargetTransform(
		FQuat::Identity,
		FVector(180.0f, 0.0f, 0.0f));
	float TargetAlpha = BIG_NUMBER;
	TestTrue(
		TEXT("Bird-centre segment hits the expanded E5 OBB"),
		ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
			Start,
			End,
			TargetTransform,
			FVector(20.0f),
			42.0f,
			TargetAlpha));
	TestTrue(
		TEXT("Protruding E5 is encountered before the satellite body"),
		TargetAlpha < SatelliteAlpha);
	TestTrue(
		TEXT("Expanded E5 clearance is positive before contact"),
		ABTSSweptCollision::PointExpandedOrientedBoxClearance(
			Start,
			TargetTransform,
			FVector(20.0f),
			42.0f) > 0.0f);
	TestTrue(
		TEXT("Expanded E5 clearance is zero at first contact"),
		FMath::IsNearlyZero(
			ABTSSweptCollision::PointExpandedOrientedBoxClearance(
				FMath::Lerp(Start, End, TargetAlpha),
				TargetTransform,
				FVector(20.0f),
				42.0f),
			1.0e-4f));
	float CornerAlpha = BIG_NUMBER;
	TestFalse(
		TEXT("A rounded-corner miss is not promoted to an E5 hit"),
		ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
			FVector(50.0f, 50.0f, 100.0f),
			FVector(50.0f, 50.0f, -100.0f),
			FTransform::Identity,
			FVector(20.0f),
			42.0f,
			CornerAlpha));
	TestTrue(
		TEXT("Rounded-corner miss retains positive exact clearance"),
		ABTSSweptCollision::PointExpandedOrientedBoxClearance(
			FVector(50.0f, 50.0f, 0.0f),
			FTransform::Identity,
			FVector(20.0f),
			42.0f) > 0.0f);
	TestTrue(
		TEXT("A sphere centre inside E5 has negative clearance"),
		ABTSSweptCollision::PointExpandedOrientedBoxClearance(
			FVector::ZeroVector,
			FTransform::Identity,
			FVector(20.0f),
			42.0f) < 0.0f);
	float ClosestAlpha = BIG_NUMBER;
	const float ParallelFaceClearance =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				FVector(-100.0f, 70.0f, 0.0f),
				FVector(100.0f, 70.0f, 0.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f,
				&ClosestAlpha);
	TestTrue(
		TEXT("Exact segment clearance resolves a face-parallel miss"),
		FMath::IsNearlyEqual(
			ParallelFaceClearance,
			8.0f,
			1.0e-4f));
	TestTrue(
		TEXT("Exact segment clearance reports the earliest equal minimum"),
		FMath::IsNearlyEqual(
			ClosestAlpha,
			0.4f,
			1.0e-4f));
	const float CornerClearance =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				FVector(50.0f, 50.0f, 100.0f),
				FVector(50.0f, 50.0f, -100.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f);
	TestTrue(
		TEXT("Exact segment clearance preserves rounded OBB corners"),
		FMath::IsNearlyEqual(
			CornerClearance,
			FMath::Sqrt(1800.0f) - 42.0f,
			1.0e-4f));
	const float ZeroLengthClearance =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				FVector(50.0f, 50.0f, 0.0f),
				FVector(50.0f, 50.0f, 0.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f);
	TestTrue(
		TEXT("Zero-length segment clearance equals point clearance"),
		FMath::IsNearlyEqual(
			ZeroLengthClearance,
			ABTSSweptCollision::PointExpandedOrientedBoxClearance(
				FVector(50.0f, 50.0f, 0.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f),
			1.0e-4f));
	const FTransform TransformedBox(
		FQuat(FVector::UpVector, FMath::DegreesToRadians(37.0f)),
		FVector(320.0f, -170.0f, 85.0f));
	float TransformedClosestAlpha = BIG_NUMBER;
	const float TransformedClearance =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				TransformedBox.TransformPosition(
					FVector(-100.0f, 70.0f, 0.0f)),
				TransformedBox.TransformPosition(
					FVector(100.0f, 70.0f, 0.0f)),
				TransformedBox,
				FVector(20.0f),
				42.0f,
				&TransformedClosestAlpha);
	TestTrue(
		TEXT("Segment clearance is rotation and translation invariant"),
		FMath::IsNearlyEqual(
			TransformedClearance,
			ParallelFaceClearance,
			1.0e-4f)
		&& FMath::IsNearlyEqual(
			TransformedClosestAlpha,
			ClosestAlpha,
			1.0e-4f));
	const float BelowMissGate =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				FVector(-100.0f, 121.9f, 0.0f),
				FVector(100.0f, 121.9f, 0.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f);
	const float AboveMissGate =
		ABTSSweptCollision::
			SegmentExpandedOrientedBoxMinimumClearance(
				FVector(-100.0f, 122.1f, 0.0f),
				FVector(100.0f, 122.1f, 0.0f),
				FTransform::Identity,
				FVector(20.0f),
				42.0f);
	TestTrue(
		TEXT("Exact clearance preserves both sides of the 60 cm miss gate"),
		BelowMissGate < 60.0f
		&& AboveMissGate > 60.0f);

	float LargerBirdAlpha = BIG_NUMBER;
	TestTrue(
		TEXT("A different collision radius produces a valid contact"),
		ABTSSweptCollision::SegmentSphereFirstAlpha(
			Start,
			End,
			FVector::ZeroVector,
			100.0f + 55.0f,
			LargerBirdAlpha));
	TestTrue(
		TEXT("The real collision radius participates in contact time"),
		LargerBirdAlpha < SatelliteAlpha);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSSlingshotCalibrationSatellitePreviewGeometryTest,
	"ABTS.Calibration.SatellitePreviewGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSSlingshotCalibrationSatellitePreviewGeometryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSM6TrajectoryPreview Preview;
	TestFalse(
		TEXT("A non-terminal satellite encounter cannot open lunar preview"),
		AABTSM101LandingPreviewCamera::
			IsSatelliteLandingTerminal(Preview));
	Preview.TerminalType =
		EABTSM6TrajectoryTerminalType::PrimarySurface;
	TestFalse(
		TEXT("A primary-surface landing cannot open lunar preview"),
		AABTSM101LandingPreviewCamera::
			IsSatelliteLandingTerminal(Preview));

	Preview.TerminalType =
		EABTSM6TrajectoryTerminalType::SatelliteBody;
	Preview.TerminalWorldLocation =
		FVector(1000.0f, 0.0f, 0.0f);
	Preview.TerminalWorldVelocity =
		FVector(-100.0f, 400.0f, 0.0f);
	Preview.WorldPoints =
	{
		FVector(1300.0f, -800.0f, 0.0f),
		FVector(1120.0f, -400.0f, 0.0f),
		Preview.TerminalWorldLocation
	};
	TestTrue(
		TEXT("A satellite-body terminal opens lunar preview"),
		AABTSM101LandingPreviewCamera::
			IsSatelliteLandingTerminal(Preview));

	FVector LandingWorld;
	FVector CameraWorld;
	FVector LookDirection;
	FVector ScreenUp;
	TestTrue(
		TEXT("A fixed-pitch lunar landing frame can be built"),
		AABTSM101LandingPreviewCamera::
			BuildSatelliteLandingViewFrame(
				Preview,
				FVector::ZeroVector,
				1200.0f,
				45.0f,
				LandingWorld,
				CameraWorld,
				LookDirection,
				ScreenUp));
	TestTrue(
		TEXT("Lunar framing targets the authoritative terminal"),
		LandingWorld.Equals(
			Preview.TerminalWorldLocation,
			1.0e-4f));
	TestTrue(
		TEXT("Lunar and primary landing previews share camera distance"),
		FMath::IsNearlyEqual(
			FVector::Distance(CameraWorld, LandingWorld),
			1200.0f,
			1.0e-4f));
	const FVector RadialUp = LandingWorld.GetSafeNormal();
	TestTrue(
		TEXT("The default lunar view looks down by 45 degrees"),
		FMath::IsNearlyEqual(
			FVector::DotProduct(LookDirection, -RadialUp),
			FMath::Sin(FMath::DegreesToRadians(45.0f)),
			1.0e-4f));
	TestTrue(
		TEXT("Satellite radial Up constrains screen Up"),
		FMath::IsNearlyZero(
			FVector::DotProduct(LookDirection, ScreenUp),
			1.0e-4f)
		&& FVector::DotProduct(ScreenUp, RadialUp) > 0.0f);

	Preview.TerminalType =
		EABTSM6TrajectoryTerminalType::SatelliteE5;
	TestTrue(
		TEXT("A surface E5 terminal also counts as lunar landing"),
		AABTSM101LandingPreviewCamera::
			IsSatelliteLandingTerminal(Preview));
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
