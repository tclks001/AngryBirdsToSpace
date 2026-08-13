// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "EngineGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9Satellite.h"

bool AABTSM6SlingshotSystem::ConfigureLaunchProfiles(
	const FABTSM6LaunchProfileCatalog& InCatalog)
{
	bLaunchProfileCatalogEnabled = false;
	CalibrationLaunchProfileCatalog = FABTSM6LaunchProfileCatalog();
	CalibrationLaunchProfileHash = 0;
	FABTSM6LaunchProfileCatalog ResolvedCatalog;
	FString FailureReason;
	if (!FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
		InCatalog, ResolvedCatalog, &FailureReason))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][ProfileCatalog] Rejected Reason=%s"),
			*FailureReason);
		return false;
	}
	CalibrationLaunchProfileCatalog = MoveTemp(ResolvedCatalog);
	CalibrationLaunchProfileHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			CalibrationLaunchProfileCatalog);
	if (CalibrationLaunchProfileHash == 0)
	{
		CalibrationLaunchProfileCatalog = FABTSM6LaunchProfileCatalog();
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][ProfileCatalog] Rejected Reason=Resolved catalog hash is zero."));
		return false;
	}
	bLaunchProfileCatalogEnabled = true;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M6][ProfileCatalog] Ready Version=%d Profiles=%d CameraDistance=%.1f CameraPitch=%.2f TargetForward=%.1f TargetHeight=%.1f LaunchProfileHash=%llu"),
		CalibrationLaunchProfileCatalog.Version,
		CalibrationLaunchProfileCatalog.Profiles.Num(),
		CalibrationLaunchProfileCatalog.AimCameraDistanceCM,
		CalibrationLaunchProfileCatalog.AimCameraPitchDegrees,
		CalibrationLaunchProfileCatalog.AimTargetForwardDistanceCM,
		CalibrationLaunchProfileCatalog.AimTargetHeightCM,
		CalibrationLaunchProfileHash);
	return true;
}

bool AABTSM6SlingshotSystem::ConfigureCalibrationLaunchProfiles(
	const FABTSM6LaunchProfileCatalog& InCatalog)
{
	if (SlingshotCamera == nullptr)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][ProfileCatalog] Rejected Reason=Slingshot camera is unavailable."));
		return false;
	}
	FABTSM6LaunchProfileCatalog CatalogWithCameraSnapshot = InCatalog;
	if (!SlingshotCamera->CopyAimFraming(
		CatalogWithCameraSnapshot.AimCameraDistanceCM,
		CatalogWithCameraSnapshot.AimCameraPitchDegrees,
		CatalogWithCameraSnapshot.AimTargetForwardDistanceCM,
		CatalogWithCameraSnapshot.AimTargetHeightCM))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][ProfileCatalog] Rejected Reason=Authored Slingshot camera framing is invalid. CameraClass=%s"),
			*GetNameSafe(SlingshotCamera->GetClass()));
		return false;
	}
	if (!ConfigureLaunchProfiles(CatalogWithCameraSnapshot)) return false;
	bCalibrationModeEnabled = true;
	// The isolated calibration GameMode never creates required buildings. Avoid
	// promoting unrelated old-map HISM instances or waiting on their warmup.
	bEnableStartupPhysicsWarmup = false;
	bStartupPhysicsWarmupComplete = true;
	bStartupPhysicsWarmupFailed = false;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Calibration][ProfileCatalog] Ready Version=%d Profiles=%d CameraClass=%s CameraDistance=%.1f CameraPitch=%.2f TargetForward=%.1f TargetHeight=%.1f LaunchProfileHash=%llu"),
		CalibrationLaunchProfileCatalog.Version,
		CalibrationLaunchProfileCatalog.Profiles.Num(),
		*GetNameSafe(SlingshotCamera->GetClass()),
		CalibrationLaunchProfileCatalog.AimCameraDistanceCM,
		CalibrationLaunchProfileCatalog.AimCameraPitchDegrees,
		CalibrationLaunchProfileCatalog.AimTargetForwardDistanceCM,
		CalibrationLaunchProfileCatalog.AimTargetHeightCM,
		CalibrationLaunchProfileHash);
	return true;
}

bool AABTSM6SlingshotSystem::CopyLaunchProfileCatalog(
	FABTSM6LaunchProfileCatalog& OutCatalog,
	uint64& OutLaunchProfileHash) const
{
	OutCatalog = FABTSM6LaunchProfileCatalog();
	OutLaunchProfileHash = 0;
	if (!bLaunchProfileCatalogEnabled || CalibrationLaunchProfileHash == 0)
	{
		return false;
	}
	OutCatalog = CalibrationLaunchProfileCatalog;
	OutLaunchProfileHash = CalibrationLaunchProfileHash;
	return true;
}

bool AABTSM6SlingshotSystem::CopyCalibrationCatalog(
	FABTSM6LaunchProfileCatalog& OutCatalog,
	uint64& OutLaunchProfileHash) const
{
	if (!bCalibrationModeEnabled)
	{
		OutCatalog = FABTSM6LaunchProfileCatalog();
		OutLaunchProfileHash = 0;
		return false;
	}
	return CopyLaunchProfileCatalog(OutCatalog, OutLaunchProfileHash);
}

bool AABTSM6SlingshotSystem::CopyCalibrationBirdCollisionRadius(
	float& OutRadiusCM) const
{
	OutRadiusCM = 0.0f;
	if (!bCalibrationModeEnabled || !Party.IsValid()) return false;
	for (AABTSM25BirdCharacter* Bird : Party->GetPartyMembers())
	{
		if (Bird == nullptr) continue;
		OutRadiusCM = FMath::Max(
			OutRadiusCM,
			Bird->GetSlingshotTrajectoryCollisionRadiusCM());
	}
	return FMath::IsFinite(OutRadiusCM) && OutRadiusCM > 0.0f;
}

void AABTSM6SlingshotSystem::BuildCalibrationReachEnvelopes(
	TArray<FABTSM6ReachEnvelope>& OutEnvelopes) const
{
	OutEnvelopes.Reset();
	if (!bCalibrationModeEnabled || !Planet.IsValid()) return;
	float BirdCollisionRadiusCM = 0.0f;
	if (!CopyCalibrationBirdCollisionRadius(BirdCollisionRadiusCM)) return;
	for (const FABTSM6LaunchProfile& Profile : CalibrationLaunchProfileCatalog.Profiles)
	{
		OutEnvelopes.Add(
			FABTSSlingshotSatelliteCalibrationModel::EstimateReachEnvelope(
				Profile,
				Planet->GetPlanetRadiusCM(),
				980.0f,
				CalibrationLaunchProfileCatalog.FlightAirDragPerSecond,
				BirdCollisionRadiusCM));
	}
}

void AABTSM6SlingshotSystem::ConfigureSatellitePracticeTarget(
	AABTSM9Satellite& InSatellite,
	AActor& InTargetActor,
	const FVector& InTargetHalfExtentCM,
	const float InPredictionStepSeconds,
	const float InPredictionMaximumFlightSeconds)
{
	SatellitePracticeBody = &InSatellite;
	SatellitePracticeTarget = &InTargetActor;
	SatellitePracticeTargetHalfExtentCM =
		InTargetHalfExtentCM.GetAbs().ComponentMax(FVector(1.0f));
	SatellitePracticePredictionStepSeconds =
		FMath::Clamp(InPredictionStepSeconds, 0.01f, 0.2f);
	SatellitePracticePredictionMaximumFlightSeconds =
		FMath::Clamp(InPredictionMaximumFlightSeconds, 2.0f, 60.0f);
	ClearCurrentTrajectoryPreview();
	if (SlingshotCamera)
	{
		SlingshotCamera->ConfigureSatelliteFlightPresentation(
			&InSatellite,
			&InTargetActor);
	}
}

void AABTSM6SlingshotSystem::ClearSatellitePracticeTarget(
	const AActor* ExpectedTargetActor)
{
	if (ExpectedTargetActor != nullptr
		&& SatellitePracticeTarget.Get() != ExpectedTargetActor)
	{
		return;
	}
	SatellitePracticeBody.Reset();
	SatellitePracticeTarget.Reset();
	SatellitePracticeTargetHalfExtentCM = FVector::ZeroVector;
	SatellitePracticePredictionStepSeconds = 0.0f;
	SatellitePracticePredictionMaximumFlightSeconds = 0.0f;
	ClearCurrentTrajectoryPreview();
	if (SlingshotCamera)
	{
		SlingshotCamera->ClearSatelliteFlightPresentation();
	}
}

bool AABTSM6SlingshotSystem::CopySatellitePracticeTarget(
	AABTSM9Satellite*& OutSatellite,
	AActor*& OutTargetActor,
	FVector& OutTargetHalfExtentCM) const
{
	OutSatellite = SatellitePracticeBody.Get();
	OutTargetActor = SatellitePracticeTarget.Get();
	OutTargetHalfExtentCM = SatellitePracticeTargetHalfExtentCM;
	return OutSatellite != nullptr
		&& OutTargetActor != nullptr
		&& OutTargetHalfExtentCM.GetMin() > 0.0f;
}

bool AABTSM6SlingshotSystem::StartSatelliteCameraCaptureLaunch(
	AABTSM51SlingshotCord& Cord,
	const float PullAlphaOverride,
	const float AimInPlaneCM,
	const float AimOutOfPlaneCM)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ABTSM9CameraCapture"))
		|| !bCalibrationModeEnabled
		|| !SatellitePracticeBody.IsValid()
		|| !SatellitePracticeTarget.IsValid()
		|| !TryEnterLaunchMode(Cord)
		|| Cord.GetSlingshotTier() != EABTSSlingshotTier::Reinforced)
	{
		return false;
	}
	// The recorder can be attached to the current M3 satellite-practice cord,
	// which is deliberately separate from the legacy calibration triplet. Build
	// the signed input plane from this cord and the current satellite instead of
	// restoring a stale world-space calibration frame.
	const FVector PreferredForward = FVector::VectorPlaneProject(
		SatellitePracticeBody->GetPlanetCenterWorld() - SlingCenter,
		SlingUp).GetSafeNormal();
	if (PreferredForward.IsNearlyZero()) return false;
	if (FVector::DotProduct(SlingForward, PreferredForward) < 0.0f)
	{
		SlingForward *= -1.0f;
		SlingRight *= -1.0f;
	}
	FABTSM6CalibrationLaunchFrame CaptureLaunchFrame;
	CaptureLaunchFrame.SlingCenterWorld = SlingCenter;
	CaptureLaunchFrame.SlingUpWorld = SlingUp;
	CaptureLaunchFrame.SlingForwardWorld = SlingForward;
	CaptureLaunchFrame.SlingRightWorld = SlingRight;
	CaptureLaunchFrame.RestPouchWorldLocation = RestPouchLocation;
	CaptureLaunchFrame.BirdInPouchOffsetCM = BirdInPouchOffsetCM;
	if (SlingshotCamera == nullptr
		|| !SlingshotCamera->BuildAimInputPlaneBasis(
			SlingCenter,
			SlingForward,
			SlingUp,
			CaptureLaunchFrame.AimPlaneNormalWorld,
			CaptureLaunchFrame.AimInPlaneAxisWorld,
			CaptureLaunchFrame.AimOutOfPlaneAxisWorld))
	{
		return false;
	}
	LaunchState = EABTSM6LaunchState::Pulling;
	if (SlingshotCamera)
	{
		SlingshotCamera->SetAimFrame(SlingCenter, SlingForward, SlingUp);
	}
	float SelectedPull = 0.0f;
	float SelectedAimInPlane = 0.0f;
	float SelectedAimOutOfPlane = 0.0f;
	float BestEncounterClearance = BIG_NUMBER;
	float BestEncounterPull = 0.0f;
	float BestEncounterAimInPlane = 0.0f;
	float BestEncounterAimOutOfPlane = 0.0f;
	float BestSatelliteClearance = BIG_NUMBER;
	float BestSatellitePull = 0.0f;
	float BestSatelliteAimInPlane = 0.0f;
	float BestSatelliteAimOutOfPlane = 0.0f;
	bool bFoundE5 = false;
	const FVector AimInPlaneAxis =
		CaptureLaunchFrame.AimInPlaneAxisWorld.GetSafeNormal();
	const FVector AimOutOfPlaneAxis =
		CaptureLaunchFrame.AimOutOfPlaneAxisWorld.GetSafeNormal();
	const auto EvaluateCandidate = [&](const float CandidatePull,
		const float CandidateAimInPlane,
		const float CandidateAimOutOfPlane)
	{
		PullAlpha = FMath::Clamp(CandidatePull, 0.0f, 1.0f);
		AimPlaneOffset = (
			AimInPlaneAxis * CandidateAimInPlane
			+ AimOutOfPlaneAxis * CandidateAimOutOfPlane)
			.GetClampedToMaxSize(GetResolvedMaximumAimPlaneOffsetCM());
		UpdatePouchAndPreview();
		RebuildCurrentTrajectoryPreview();
		if (bCurrentTrajectoryPreviewValid
			&& CurrentTrajectoryPreview.EncounterSatelliteRadiusCM > 0.0f
			&& CurrentTrajectoryPreview.ClosestSatelliteClearanceCM
				< BestSatelliteClearance)
		{
			BestSatelliteClearance =
				CurrentTrajectoryPreview.ClosestSatelliteClearanceCM;
			BestSatellitePull = PullAlpha;
			BestSatelliteAimInPlane = CandidateAimInPlane;
			BestSatelliteAimOutOfPlane = CandidateAimOutOfPlane;
		}
		if (bCurrentTrajectoryPreviewValid
			&& CurrentTrajectoryPreview.bHasSatelliteEncounter
			&& CurrentTrajectoryPreview.ClosestSatelliteClearanceCM
				< BestEncounterClearance)
		{
			BestEncounterClearance =
				CurrentTrajectoryPreview.ClosestSatelliteClearanceCM;
			BestEncounterPull = PullAlpha;
			BestEncounterAimInPlane = CandidateAimInPlane;
			BestEncounterAimOutOfPlane = CandidateAimOutOfPlane;
		}
		if (bCurrentTrajectoryPreviewValid
			&& CurrentTrajectoryPreview.TerminalType
				== EABTSM6TrajectoryTerminalType::SatelliteE5)
		{
			SelectedPull = PullAlpha;
			SelectedAimInPlane = CandidateAimInPlane;
			SelectedAimOutOfPlane = CandidateAimOutOfPlane;
			return true;
		}
		return false;
	};
	// First try the offline sweep witness verbatim. If the current SDF surface
	// intercepts it, search only the frozen success-island neighbourhood using
	// the production M6 predictor. This is Preview/Test authority and never
	// publishes a replacement calibration result.
	bFoundE5 = EvaluateCandidate(
		PullAlphaOverride, AimInPlaneCM, AimOutOfPlaneCM);
	for (int32 PullIndex = 0; !bFoundE5 && PullIndex <= 25; ++PullIndex)
	{
		const float CandidatePull = 0.75f + PullIndex * 0.01f;
		for (int32 OutIndex = 0; !bFoundE5 && OutIndex < 5; ++OutIndex)
		{
			const float CandidateOut = -80.0f + OutIndex * 40.0f;
			for (int32 InIndex = 0; !bFoundE5 && InIndex <= 40; ++InIndex)
			{
				const float CandidateIn = -260.0f + InIndex * 13.0f;
				bFoundE5 = EvaluateCandidate(
					CandidatePull, CandidateIn, CandidateOut);
			}
		}
	}
	bool bCaptureIntentOverride = false;
	if (!bFoundE5 && BestEncounterClearance < BIG_NUMBER)
	{
		bFoundE5 = EvaluateCandidate(
			BestEncounterPull,
			BestEncounterAimInPlane,
			BestEncounterAimOutOfPlane);
		if (!bFoundE5 && bCurrentTrajectoryPreviewValid
			&& CurrentTrajectoryPreview.bHasSatelliteEncounter)
		{
			SelectedPull = BestEncounterPull;
			SelectedAimInPlane = BestEncounterAimInPlane;
			SelectedAimOutOfPlane = BestEncounterAimOutOfPlane;
			CurrentTrajectoryPreview.TerminalType =
				EABTSM6TrajectoryTerminalType::SatelliteE5;
			bFoundE5 = true;
			bCaptureIntentOverride = true;
		}
	}
	if (!bFoundE5 && BestSatelliteClearance < BIG_NUMBER)
	{
		EvaluateCandidate(
			BestSatellitePull,
			BestSatelliteAimInPlane,
			BestSatelliteAimOutOfPlane);
		if (bCurrentTrajectoryPreviewValid)
		{
			SelectedPull = BestSatellitePull;
			SelectedAimInPlane = BestSatelliteAimInPlane;
			SelectedAimOutOfPlane = BestSatelliteAimOutOfPlane;
			CurrentTrajectoryPreview.bHasSatelliteEncounter = true;
			CurrentTrajectoryPreview.TerminalType =
				EABTSM6TrajectoryTerminalType::SatelliteE5;
			bFoundE5 = true;
			bCaptureIntentOverride = true;
			BestEncounterClearance = BestSatelliteClearance;
		}
	}
	if (!bFoundE5)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M9][CameraCapture] NoProductionE5PredictionInCalibrationDomain"));
		return false;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][CameraCapture] LaunchPrepared Pull=%.3f AimInPlane=%.1f AimOutOfPlane=%.1f Terminal=%s Encounter=%d"),
		SelectedPull,
		SelectedAimInPlane,
		SelectedAimOutOfPlane,
		*UEnum::GetValueAsString(CurrentTrajectoryPreview.TerminalType),
		CurrentTrajectoryPreview.bHasSatelliteEncounter ? 1 : 0);
	if (bCaptureIntentOverride)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M9][CameraCapture] PreviewTestIntentOverride=1 Clearance=%.1f ProductionGameplayUnchanged=1"),
			BestEncounterClearance);
	}
	ReleaseLaunch();
	return LaunchState == EABTSM6LaunchState::Flying;
}

bool AABTSM6SlingshotSystem::CopySatelliteCameraCaptureState(
	AABTSM25BirdCharacter*& OutBird,
	AABTSM9Satellite*& OutSatellite,
	AActor*& OutE5Target,
	EABTSM9SatelliteFlightCameraIntent& OutIntent,
	EABTSM9SatelliteFlightCameraPhase& OutPhase,
	float* OutSurfaceFrameAlpha,
	bool* OutSurfaceFrameCommitted) const
{
	OutBird = LaunchedBird.Get();
	OutSatellite = SatellitePracticeBody.Get();
	OutE5Target = SatellitePracticeTarget.Get();
	OutIntent = SlingshotCamera
		? SlingshotCamera->GetSatelliteFlightIntent()
		: EABTSM9SatelliteFlightCameraIntent::None;
	OutPhase = SlingshotCamera
		? SlingshotCamera->GetSatelliteFlightPhase()
		: EABTSM9SatelliteFlightCameraPhase::PrimaryFollow;
	if (OutSurfaceFrameAlpha)
	{
		*OutSurfaceFrameAlpha = SlingshotCamera
			? SlingshotCamera->GetSatelliteSurfaceFrameAlpha()
			: 0.0f;
	}
	if (OutSurfaceFrameCommitted)
	{
		*OutSurfaceFrameCommitted = SlingshotCamera
			? SlingshotCamera->IsSatelliteSurfaceFrameCommitted()
			: false;
	}
	return OutBird != nullptr
		&& OutSatellite != nullptr
		&& OutE5Target != nullptr
		&& SlingshotCamera != nullptr;
}

bool AABTSM6SlingshotSystem::StageSatelliteCameraCaptureNearPass()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ABTSM9CameraCapture"))
		|| LaunchState != EABTSM6LaunchState::Flying
		|| !LaunchedBird.IsValid()
		|| !SatellitePracticeBody.IsValid()
		|| !SatellitePracticeTarget.IsValid()
		|| SlingshotCamera == nullptr)
	{
		return false;
	}

	AABTSM9Satellite& TargetSatellite = *SatellitePracticeBody.Get();
	const FVector SatelliteCenter = TargetSatellite.GetPlanetCenterWorld();
	const float SatelliteRadiusCM =
		FMath::Max(1.0f, TargetSatellite.GetPlanetRadiusCM());
	const FVector E5Radial =
		(SatellitePracticeTarget->GetActorLocation() - SatelliteCenter)
		.GetSafeNormal();
	if (E5Radial.IsNearlyZero()) return false;

	FVector Reference = Planet.IsValid()
		? (Planet->GetPlanetCenterWorld() - SatelliteCenter).GetSafeNormal()
		: FVector::UpVector;
	FVector OrbitNormal = FVector::CrossProduct(E5Radial, Reference).GetSafeNormal();
	if (OrbitNormal.IsNearlyZero())
	{
		OrbitNormal = FVector::CrossProduct(
			E5Radial,
			FMath::Abs(E5Radial.Z) < 0.9f
				? FVector::UpVector
				: FVector::ForwardVector).GetSafeNormal();
	}
	if (OrbitNormal.IsNearlyZero()) return false;

	constexpr float EntryAngleDegrees = -112.0f;
	constexpr float EntryRadiusMultiplier = 3.35f;
	const FVector EntryRadial = FQuat(
		OrbitNormal,
		FMath::DegreesToRadians(EntryAngleDegrees))
		.RotateVector(E5Radial).GetSafeNormal();
	FVector Tangent = FVector::CrossProduct(OrbitNormal, EntryRadial).GetSafeNormal();
	if (FVector::DotProduct(Tangent, E5Radial) < 0.0f) Tangent *= -1.0f;
	const FVector EntryLocation = SatelliteCenter
		+ EntryRadial * SatelliteRadiusCM * EntryRadiusMultiplier;
	const float SurfaceGravity = FMath::Max(
		1.0f,
		TargetSatellite.GetSurfaceGravityAccelerationCMPerSec2());
	const float CircularSpeed = FMath::Sqrt(
		SurfaceGravity * SatelliteRadiusCM / EntryRadiusMultiplier);
	// A tangential 0.78 circular-speed entry yields an eccentric, readable
	// fly-by with periapsis above the body. Do not inject an artificial inward
	// component: that made the recorder fixture graze the satellite and freeze
	// the second half of the shot on its limb.
	const FVector EntryVelocity = Tangent * CircularSpeed * 0.78f;

	FABTSM6TrajectoryPreview CameraWitness;
	CameraWitness.bHasSatelliteEncounter = true;
	CameraWitness.TerminalType = EABTSM6TrajectoryTerminalType::SatelliteE5;
	CameraWitness.EncounterSatelliteCenterWorld = SatelliteCenter;
	CameraWitness.EncounterSatelliteRadiusCM = SatelliteRadiusCM;
	constexpr int32 WitnessPointCount = 25;
	CameraWitness.WorldPoints.Reserve(WitnessPointCount);
	for (int32 Index = 0; Index < WitnessPointCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index)
			/ static_cast<float>(WitnessPointCount - 1);
		const float Angle = FMath::Lerp(
			EntryAngleDegrees,
			-8.0f,
			Alpha);
		const float RadiusMultiplier = FMath::Lerp(
			EntryRadiusMultiplier,
			1.12f,
			FMath::Square(Alpha));
		const FVector Radial = FQuat(
			OrbitNormal,
			FMath::DegreesToRadians(Angle))
			.RotateVector(E5Radial).GetSafeNormal();
		CameraWitness.WorldPoints.Add(
			SatelliteCenter + Radial * SatelliteRadiusCM * RadiusMultiplier);
	}
	CameraWitness.ClosestSatelliteClearanceCM =
		SatelliteRadiusCM * 0.12f;

	LaunchedBird->SetActorLocationAndRotation(
		EntryLocation,
		FRotationMatrix::MakeFromXZ(EntryVelocity.GetSafeNormal(), EntryRadial).ToQuat(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	LaunchedBird->SetSlingshotVelocity(EntryVelocity);
	SlingshotCamera->LockSatelliteFlightIntent(CameraWitness);
	SlingshotCamera->FollowBird(LaunchedBird.Get(), Planet.Get());
	if (!SlingshotCamera->SnapToPrimaryFollowForSatelliteCapture())
	{
		return false;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][CameraCapture] NearPassStaged=1 Authority=PreviewTest EntryRadius=%.2fR Speed=%.1f ProductionGameplayUnchanged=1"),
		EntryRadiusMultiplier,
		EntryVelocity.Size());
	return true;
}

bool AABTSM6SlingshotSystem::CopyActiveCalibrationLaunchSample(
	FABTSM6LaunchCalibrationTelemetry& OutTelemetry,
	FVector& OutBirdWorldLocation,
	bool* OutHasCurrentSatelliteBodyEvidence,
	bool* OutHasCurrentSatelliteE5Evidence) const
{
	OutTelemetry = FABTSM6LaunchCalibrationTelemetry();
	OutBirdWorldLocation = FVector::ZeroVector;
	if (OutHasCurrentSatelliteBodyEvidence)
	{
		*OutHasCurrentSatelliteBodyEvidence = false;
	}
	if (OutHasCurrentSatelliteE5Evidence)
	{
		*OutHasCurrentSatelliteE5Evidence = false;
	}
	if (!bActiveLaunchCalibrationTelemetry
		|| !LaunchedBird.IsValid()
		|| (LaunchState != EABTSM6LaunchState::Flying
			&& LaunchState != EABTSM6LaunchState::Settling
			&& !(LaunchState == EABTSM6LaunchState::Returning
				&& bHasPendingLaunchCompletion
				&& ReturnElapsedSeconds <= KINDA_SMALL_NUMBER)))
	{
		return false;
	}
	OutTelemetry = ActiveLaunchCalibrationTelemetry;
	if (OutHasCurrentSatelliteBodyEvidence)
	{
		*OutHasCurrentSatelliteBodyEvidence =
			CalibrationSatelliteBodyHitFrame == GFrameCounter;
	}
	if (OutHasCurrentSatelliteE5Evidence)
	{
		*OutHasCurrentSatelliteE5Evidence =
			CalibrationSatelliteE5HitFrame == GFrameCounter;
	}
	OutBirdWorldLocation =
		LaunchState == EABTSM6LaunchState::Returning
			? PendingCompletedLandingLocation
			: LaunchedBird->GetActorLocation();
	return true;
}

void AABTSM6SlingshotSystem::NotifyCalibrationTargetEvent(
	const FName TargetId,
	const bool bSatelliteBodyFirst,
	const bool bFinalizeSameFrameSatelliteEvidence)
{
	if (!bActiveLaunchCalibrationTelemetry)
	{
		return;
	}
	const bool bNewSatelliteBody =
		bSatelliteBodyFirst && TargetId == TEXT("Satellite.Body");
	const bool bNewE5Authority =
		!bSatelliteBodyFirst
		&& TargetId == TEXT("Satellite.Backside");
	const bool bNewSatelliteAuthority =
		bNewSatelliteBody || bNewE5Authority;
	if (bNewSatelliteAuthority
		&& CalibrationSatelliteDecisionFrame != MAX_uint64)
	{
		return;
	}
	if (bNewSatelliteBody)
	{
		CalibrationSatelliteBodyHitFrame = GFrameCounter;
		if (SlingshotCamera)
		{
			SlingshotCamera->NotifySatelliteSurfaceContact();
		}
	}
	else if (bNewE5Authority)
	{
		CalibrationSatelliteE5HitFrame = GFrameCounter;
		if (SlingshotCamera)
		{
			SlingshotCamera->NotifySatelliteSurfaceContact();
		}
	}
	const bool bExistingE5Authority =
		ActiveLaunchCalibrationTelemetry.HitTargetId
		== TEXT("Satellite.Backside");
	const bool bExistingSatelliteBodyAuthority =
		ActiveLaunchCalibrationTelemetry.bHitSatelliteBodyFirst;
	const bool bExistingSatelliteAuthority =
		bExistingE5Authority || bExistingSatelliteBodyAuthority;
	const bool bOpposingEvidenceIsCurrent =
		(bNewE5Authority
			&& bExistingSatelliteBodyAuthority
			&& CalibrationSatelliteBodyHitFrame == GFrameCounter)
		|| (bNewSatelliteBody
			&& bExistingE5Authority
			&& CalibrationSatelliteE5HitFrame == GFrameCounter);
	if (bNewSatelliteAuthority)
	{
		if (!bExistingSatelliteAuthority
			&& ActiveLaunchCalibrationTelemetry.HitTargetId != NAME_None)
		{
			return;
		}
		if (bExistingSatelliteAuthority
			&& !bFinalizeSameFrameSatelliteEvidence)
		{
			// Chaos contact order is not authority. Keep the first contact only as
			// provisional telemetry; the Rig will resolve both shapes by sweep.
			return;
		}
		if (bExistingSatelliteAuthority
			&& bFinalizeSameFrameSatelliteEvidence
			&& bExistingE5Authority != bNewE5Authority
			&& !bOpposingEvidenceIsCurrent)
		{
			return;
		}
	}
	else if (ActiveLaunchCalibrationTelemetry.HitTargetId != NAME_None)
	{
		return;
	}
	if (bNewSatelliteAuthority && bFinalizeSameFrameSatelliteEvidence)
	{
		CalibrationSatelliteDecisionFrame = GFrameCounter;
	}
	ActiveLaunchCalibrationTelemetry.HitTargetId = TargetId;
	ActiveLaunchCalibrationTelemetry.bHitSatelliteBodyFirst = bSatelliteBodyFirst;
	ActiveLaunchCalibrationTelemetry.bHitTarget = !bSatelliteBodyFirst
		&& TargetId != TEXT("Timeout");
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Calibration][TargetEvent] Seq=%d Target=%s Hit=%d SatelliteBodyFirst=%d Finalized=%d"),
		ActiveLaunchCalibrationTelemetry.Sequence,
		*TargetId.ToString(),
		ActiveLaunchCalibrationTelemetry.bHitTarget ? 1 : 0,
		bSatelliteBodyFirst ? 1 : 0,
		bFinalizeSameFrameSatelliteEvidence ? 1 : 0);
	if (!bSatelliteBodyFirst
		&& TargetId == TEXT("Satellite.Backside")
		&& bFinalizeSameFrameSatelliteEvidence)
	{
		if (SlingshotCamera)
		{
			SlingshotCamera->NotifySatelliteE5Hit();
		}
		CalibrationSuccessReturnRemainingSeconds =
			FMath::Max(
				0.1f,
				CalibrationE5ImpactHoldSeconds);
	}
}

AABTSM71PlaceableSlingshotActor*
AABTSM6SlingshotSystem::SpawnCalibrationSlingshot(
	const FVector& CenterDirection,
	const FVector& LaunchDirection,
	const EABTSSlingshotTier Tier)
{
	if (!Planet.IsValid() || GetWorld() == nullptr) return nullptr;
	TSubclassOf<AABTSM71PlaceableSlingshotActor> SlingshotClass;
	switch (Tier)
	{
	case EABTSSlingshotTier::Twig:
		SlingshotClass = DebugTwigSlingshotClass;
		break;
	case EABTSSlingshotTier::Simple:
		SlingshotClass = DebugSimpleSlingshotClass;
		break;
	case EABTSSlingshotTier::Reinforced:
		SlingshotClass = DebugReinforcedSlingshotClass;
		break;
	default:
		return nullptr;
	}
	if (!SlingshotClass) return nullptr;
	FTransform SpawnTransform;
	if (!QueryDebugSurfaceTransform(
		CenterDirection, LaunchDirection, 0.0f, SpawnTransform))
	{
		return nullptr;
	}
	SpawnTransform.SetScale3D(
		DebugSlingshotActorScale.ComponentMax(FVector(0.01f)));
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<AABTSM71PlaceableSlingshotActor>(
		SlingshotClass, SpawnTransform, Params);
}

bool AABTSM6SlingshotSystem::CaptureCalibrationLaunchFrame(
	const AABTSM71PlaceableSlingshotActor& Slingshot,
	const FVector& PreferredForward,
	FABTSM6CalibrationLaunchFrame& OutLaunchFrame) const
{
	OutLaunchFrame = FABTSM6CalibrationLaunchFrame();
	if (!Planet.IsValid()) return false;
	const AABTSM51SlingshotCord* Cord = Slingshot.GetRuntimeCord();
	if (Cord == nullptr) return false;
	const FVector CapturedSlingCenter =
		(Cord->GetEndpointA() + Cord->GetEndpointB()) * 0.5f;
	const FVector CapturedSlingUp =
		Planet->GetRadialUpAtWorldLocation(
			CapturedSlingCenter).GetSafeNormal();
	FVector CapturedSlingRight = FVector::VectorPlaneProject(
		Cord->GetEndpointB() - Cord->GetEndpointA(),
		CapturedSlingUp).GetSafeNormal();
	FVector CapturedSlingForward =
		FVector::CrossProduct(
			CapturedSlingRight,
			CapturedSlingUp).GetSafeNormal();
	const FVector PreferredTangentForward =
		FVector::VectorPlaneProject(
			PreferredForward,
			CapturedSlingUp).GetSafeNormal();
	if (CapturedSlingRight.IsNearlyZero()
		|| CapturedSlingForward.IsNearlyZero()
		|| PreferredTangentForward.IsNearlyZero())
	{
		return false;
	}
	if (FVector::DotProduct(
		CapturedSlingForward,
		PreferredTangentForward) < 0.0f)
	{
		CapturedSlingForward *= -1.0f;
		CapturedSlingRight *= -1.0f;
	}
	OutLaunchFrame.SlingCenterWorld = CapturedSlingCenter;
	OutLaunchFrame.SlingUpWorld = CapturedSlingUp;
	OutLaunchFrame.SlingForwardWorld = CapturedSlingForward;
	OutLaunchFrame.SlingRightWorld = CapturedSlingRight;
	if (SlingshotCamera == nullptr
		|| !SlingshotCamera->BuildAimInputPlaneBasis(
			CapturedSlingCenter,
			CapturedSlingForward,
			CapturedSlingUp,
			OutLaunchFrame.AimPlaneNormalWorld,
			OutLaunchFrame.AimInPlaneAxisWorld,
			OutLaunchFrame.AimOutOfPlaneAxisWorld))
	{
		return false;
	}
	OutLaunchFrame.RestPouchWorldLocation =
		Cord->GetRestPouchTransform().GetLocation();
	OutLaunchFrame.BirdInPouchOffsetCM = BirdInPouchOffsetCM;
	return true;
}

int32 AABTSM6SlingshotSystem::SpawnCalibrationSlingshots(
	const int32 InStartCellId,
	const FVector& TowardWorldLocation)
{
	if (!bCalibrationModeEnabled
		|| bCalibrationSlingshotsSpawned
		|| !ResolveDependencies()
		|| !Planet->LogicalCells.IsValidIndex(InStartCellId))
	{
		return 0;
	}
	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	const float PlanetRadiusCM = Planet->GetPlanetRadiusCM();
	const FVector StartDirection =
		Planet->LogicalCells[InStartCellId].UnitCenter.GetSafeNormal();
	FVector LaunchDirection = FVector::VectorPlaneProject(
		TowardWorldLocation - (PlanetCenter + StartDirection * PlanetRadiusCM),
		StartDirection).GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(StartDirection.Z) < 0.9f
			? FVector::UpVector
			: FVector::ForwardVector;
		LaunchDirection =
			FVector::CrossProduct(Reference, StartDirection).GetSafeNormal();
	}
	const FVector Right =
		FVector::CrossProduct(StartDirection, LaunchDirection).GetSafeNormal();
	const EABTSSlingshotTier Tiers[] =
	{
		EABTSSlingshotTier::Twig,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced
	};
	int32 Spawned = 0;
	bHasReinforcedCalibrationLaunchFrame = false;
	ReinforcedCalibrationLaunchFrame = FABTSM6CalibrationLaunchFrame();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tiers); ++Index)
	{
		const float SideOffsetCM = static_cast<float>(Index - 1) * 620.0f;
		const FVector SiteDirection =
			(StartDirection * PlanetRadiusCM
				+ Right * SideOffsetCM
				+ LaunchDirection * 420.0f).GetSafeNormal();
		AABTSM71PlaceableSlingshotActor* SpawnedSlingshot =
			SpawnCalibrationSlingshot(
				SiteDirection, LaunchDirection, Tiers[Index]);
		if (SpawnedSlingshot == nullptr) continue;
		++Spawned;
		if (Tiers[Index] == EABTSSlingshotTier::Reinforced)
		{
			bHasReinforcedCalibrationLaunchFrame =
				CaptureCalibrationLaunchFrame(
					*SpawnedSlingshot,
					LaunchDirection,
					ReinforcedCalibrationLaunchFrame);
		}
	}
	bCalibrationSlingshotsSpawned =
		Spawned == UE_ARRAY_COUNT(Tiers)
		&& bHasReinforcedCalibrationLaunchFrame;
	if (bCalibrationSlingshotsSpawned)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][Calibration][Slingshots] StartCell=%d Spawned=%d Twig=1 Simple=1 Reinforced=1 Frame=1 RestPouch=%s"),
			InStartCellId,
			Spawned,
			*ReinforcedCalibrationLaunchFrame.RestPouchWorldLocation.ToCompactString());
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][Calibration][Slingshots] StartCell=%d Spawned=%d Expected=3 Frame=%d"),
			InStartCellId,
			Spawned,
			bHasReinforcedCalibrationLaunchFrame ? 1 : 0);
	}
	return Spawned;
}

bool AABTSM6SlingshotSystem::CopyReinforcedCalibrationLaunchFrame(
	FABTSM6CalibrationLaunchFrame& OutLaunchFrame) const
{
	OutLaunchFrame = FABTSM6CalibrationLaunchFrame();
	if (!bCalibrationModeEnabled
		|| !bCalibrationSlingshotsSpawned
		|| !bHasReinforcedCalibrationLaunchFrame)
	{
		return false;
	}
	OutLaunchFrame = ReinforcedCalibrationLaunchFrame;
	return true;
}

const FABTSM6LaunchProfile* AABTSM6SlingshotSystem::FindLaunchProfile(
	const EABTSSlingshotTier Tier) const
{
	if (!bLaunchProfileCatalogEnabled || Tier == EABTSSlingshotTier::Space)
	{
		return nullptr;
	}
	return FABTSSlingshotSatelliteCalibrationModel::FindProfile(
		CalibrationLaunchProfileCatalog, Tier);
}

const FABTSM6LaunchProfile*
AABTSM6SlingshotSystem::GetActiveLaunchProfile() const
{
	return ActiveCord.IsValid()
		? FindLaunchProfile(ActiveCord->GetSlingshotTier())
		: nullptr;
}

float AABTSM6SlingshotSystem::GetResolvedFlightAirDragPerSecond() const
{
	return GetActiveLaunchProfile()
		? CalibrationLaunchProfileCatalog.FlightAirDragPerSecond
		: FlightAirDragPerSecond;
}

float AABTSM6SlingshotSystem::GetResolvedMinimumPullDistanceCM() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Profile->MinimumPullDistanceCM;
	}
	return MinPullDistanceCM;
}

float AABTSM6SlingshotSystem::GetResolvedMaximumPullDistanceCM() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Profile->MaximumPullDistanceCM;
	}
	return MaxPullDistanceCM;
}

float AABTSM6SlingshotSystem::GetResolvedInitialPullAlpha() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return FMath::Clamp(Profile->InitialPullAlpha, 0.0f, 1.0f);
	}
	return 0.55f;
}

float AABTSM6SlingshotSystem::GetResolvedPullPowerWheelStep() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Profile->PullPowerWheelStep;
	}
	return PullPowerWheelStep;
}

float AABTSM6SlingshotSystem::GetResolvedAimSensitivityScale() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Profile->AimSensitivityScale;
	}
	return 1.0f;
}

float AABTSM6SlingshotSystem::GetResolvedMaximumAimPlaneOffsetCM() const
{
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Profile->MaximumAimPlaneOffsetCM;
	}
	return MaxAimPlaneOffsetCM;
}

FVector AABTSM6SlingshotSystem::ComputeLaunchVelocity() const
{
	const FVector Direction =
		(SlingCenter + SlingUp * 65.0f - PouchLocation).GetSafeNormal();
	if (const FABTSM6LaunchProfile* Profile = GetActiveLaunchProfile())
	{
		return Direction
			* FABTSSlingshotSatelliteCalibrationModel::EvaluateLaunchSpeed(
				*Profile, PullAlpha);
	}
	return Direction
		* FMath::Lerp(MinLaunchSpeedCMPerSec, MaxLaunchSpeedCMPerSec, PullAlpha);
}

void AABTSM6SlingshotSystem::UpdateActiveLaunchTelemetry()
{
	if (!bActiveLaunchCalibrationTelemetry || !LaunchedBird.IsValid()) return;
	const FVector CurrentWorldLocation = LaunchedBird->GetActorLocation();
	ActiveLaunchCalibrationTelemetry.ActualPathLengthCM +=
		FVector::Distance(LastCalibrationTelemetrySampleWorld, CurrentWorldLocation);
	LastCalibrationTelemetrySampleWorld = CurrentWorldLocation;
	float AltitudeCM = 0.0f;
	if (bPlanarTestMode)
	{
		AltitudeCM = FVector::DotProduct(
			CurrentWorldLocation - PlanarOrigin, PlanarUp);
	}
	else if (Planet.IsValid())
	{
		AltitudeCM = FVector::Distance(
			CurrentWorldLocation, Planet->GetPlanetCenterWorld())
			- Planet->GetPlanetRadiusCM();
	}
	ActiveLaunchCalibrationTelemetry.ApexAltitudeAbovePrimaryCM =
		FMath::Max(
			ActiveLaunchCalibrationTelemetry.ApexAltitudeAbovePrimaryCM,
			AltitudeCM);
}

void AABTSM6SlingshotSystem::FinalizeActiveLaunchTelemetry(
	const FVector& LandingWorldLocation)
{
	if (!bActiveLaunchCalibrationTelemetry) return;
	UpdateActiveLaunchTelemetry();
	ActiveLaunchCalibrationTelemetry.LandingWorldLocation = LandingWorldLocation;
	if (ActiveLaunchCalibrationTelemetry.FlightTimeSeconds <= 0.0f)
	{
		ActiveLaunchCalibrationTelemetry.FlightTimeSeconds = FlightElapsedSeconds;
	}
	if (!bPlanarTestMode && Planet.IsValid())
	{
		const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
		const FVector StartDirection =
			(ActiveLaunchCalibrationTelemetry.InitialWorldLocation - PlanetCenter)
			.GetSafeNormal();
		const FVector LandingDirection =
			(LandingWorldLocation - PlanetCenter).GetSafeNormal();
		const float Dot = FMath::Clamp(
			FVector::DotProduct(StartDirection, LandingDirection),
			-1.0f,
			1.0f);
		ActiveLaunchCalibrationTelemetry.ActualLandingArcLengthCM =
			FMath::Acos(Dot) * Planet->GetPlanetRadiusCM();
	}
}
