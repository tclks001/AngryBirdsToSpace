// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM6SlingshotCamera.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM9Satellite.h"

AABTSM6SlingshotCamera::AABTSM6SlingshotCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	GetCameraComponent()->SetFieldOfView(50.0f);
}

void AABTSM6SlingshotCamera::SetAimFrame(const FVector& InCenter, const FVector& InForward, const FVector& InUp)
{
	AimCenter = InCenter;
	AimUp = InUp.GetSafeNormal();
	AimForward = FVector::VectorPlaneProject(InForward, AimUp).GetSafeNormal();
	if (AimForward.IsNearlyZero()) AimForward = FVector::ForwardVector;
	bFollowBird = false;
	UpdateAim(0.0f);
}

bool AABTSM6SlingshotCamera::CopyAimFraming(
	float& OutDistanceCM,
	float& OutPitchDegrees,
	float& OutTargetForwardDistanceCM,
	float& OutTargetHeightCM) const
{
	OutDistanceCM = AimDistanceCM;
	OutPitchDegrees = AimPitchDegrees;
	OutTargetForwardDistanceCM = AimTargetForwardDistanceCM;
	OutTargetHeightCM = AimTargetHeightCM;
	return FMath::IsFinite(OutDistanceCM)
		&& FMath::IsFinite(OutPitchDegrees)
		&& FMath::IsFinite(OutTargetForwardDistanceCM)
		&& FMath::IsFinite(OutTargetHeightCM)
		&& OutDistanceCM >= 100.0f
		&& OutPitchDegrees >= -10.0f
		&& OutPitchDegrees <= 75.0f
		&& OutTargetForwardDistanceCM >= 0.0f;
}

bool AABTSM6SlingshotCamera::BuildAimView(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutLocation,
	FVector& OutLook,
	FVector& OutScreenUp) const
{
	const FVector SafeUp = InUp.GetSafeNormal();
	const FVector SafeForward =
		FVector::VectorPlaneProject(
			InForward,
			SafeUp).GetSafeNormal();
	if (SafeUp.IsNearlyZero() || SafeForward.IsNearlyZero())
	{
		return false;
	}
	const float PitchRadians = FMath::DegreesToRadians(AimPitchDegrees);
	const FVector BackAndUp =
		(-SafeForward * FMath::Cos(PitchRadians)
			+ SafeUp * FMath::Sin(PitchRadians)).GetSafeNormal();
	OutLocation = InCenter + BackAndUp * AimDistanceCM;
	const FVector Target =
		InCenter
		+ SafeForward * AimTargetForwardDistanceCM
		+ SafeUp * AimTargetHeightCM;
	OutLook = (Target - OutLocation).GetSafeNormal();
	OutScreenUp =
		FVector::VectorPlaneProject(SafeUp, OutLook).GetSafeNormal();
	return !OutLook.IsNearlyZero() && !OutScreenUp.IsNearlyZero();
}

bool AABTSM6SlingshotCamera::BuildAimInputPlaneBasis(
	const FVector& InCenter,
	const FVector& InForward,
	const FVector& InUp,
	FVector& OutPlaneNormal,
	FVector& OutInPlaneAxis,
	FVector& OutOutOfPlaneAxis) const
{
	OutPlaneNormal = FVector::ZeroVector;
	OutInPlaneAxis = FVector::ZeroVector;
	OutOutOfPlaneAxis = FVector::ZeroVector;
	FVector CameraLocation;
	if (!BuildAimView(
		InCenter,
		InForward,
		InUp,
		CameraLocation,
		OutPlaneNormal,
		OutInPlaneAxis))
	{
		return false;
	}
	OutOutOfPlaneAxis =
		FVector::CrossProduct(
			OutInPlaneAxis,
			OutPlaneNormal).GetSafeNormal();
	const FVector PreferredRight =
		FVector::CrossProduct(
			InUp.GetSafeNormal(),
			InForward.GetSafeNormal()).GetSafeNormal();
	if (FVector::DotProduct(
		OutOutOfPlaneAxis,
		PreferredRight) < 0.0f)
	{
		OutOutOfPlaneAxis *= -1.0f;
	}
	return !OutOutOfPlaneAxis.IsNearlyZero();
}

void AABTSM6SlingshotCamera::FollowBird(AABTSM25BirdCharacter* InBird, AABTSM2Planet* InPlanet)
{
	Bird = InBird;
	Planet = InPlanet;
	bPlanarFollow = false;
	bFollowBird = true;
	bSatelliteE5Hit = false;
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::FollowBirdPlanar(AABTSM25BirdCharacter* InBird, const FVector& InPlanarUp)
{
	Bird = InBird;
	Planet.Reset();
	PlanarFollowUp = InPlanarUp.GetSafeNormal();
	if (PlanarFollowUp.IsNearlyZero()) PlanarFollowUp = FVector::UpVector;
	bPlanarFollow = true;
	bFollowBird = true;
}

void AABTSM6SlingshotCamera::ConfigureSatelliteFlightPresentation(
	AABTSM9Satellite* InSatellite,
	AActor* InE5Target)
{
	Satellite = InSatellite;
	E5Target = InE5Target;
	bSatelliteE5Hit = false;
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::ClearSatelliteFlightPresentation()
{
	Satellite.Reset();
	E5Target.Reset();
	bSatelliteE5Hit = false;
	bForcePrimaryFrameUntilNextFollow = false;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::NotifySatelliteE5Hit()
{
	if (!Satellite.IsValid() || !E5Target.IsValid()) return;
	bSatelliteE5Hit = true;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::E5Impact);
}

void AABTSM6SlingshotCamera::BeginReturnToPrimaryFrame()
{
	bSatelliteE5Hit = false;
	bForcePrimaryFrameUntilNextFollow = true;
	SatelliteOrbitViewNormal = FVector::ZeroVector;
	SetSatelliteFlightPhase(
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
}

void AABTSM6SlingshotCamera::SetSatelliteFlightPhase(
	const EABTSM9SatelliteFlightCameraPhase NewPhase)
{
	if (SatelliteFlightPhase == NewPhase) return;
	const EABTSM9SatelliteFlightCameraPhase Previous =
		SatelliteFlightPhase;
	SatelliteFlightPhase = NewPhase;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][FlightCamera] Phase=%s Previous=%s"),
		*UEnum::GetValueAsString(NewPhase),
		*UEnum::GetValueAsString(Previous));
}

void AABTSM6SlingshotCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFollowBird) UpdateFollow(DeltaSeconds); else UpdateAim(DeltaSeconds);
}

void AABTSM6SlingshotCamera::UpdateAim(const float DeltaSeconds)
{
	// Use only the cord frame captured on launch-mode entry. Pulling the pouch
	// must not rotate or translate the camera around the slingshot.
	FVector DesiredLocation;
	FVector Look;
	FVector ScreenUp;
	if (!BuildAimView(
		AimCenter,
		AimForward,
		AimUp,
		DesiredLocation,
		Look,
		ScreenUp))
	{
		return;
	}
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat();
	SetActorLocationAndRotation(DeltaSeconds > 0.0f ? FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, AimCameraBlendSpeed) : DesiredLocation, Rotation);
}

void AABTSM6SlingshotCamera::UpdateFollow(const float DeltaSeconds)
{
	AABTSM25BirdCharacter* TargetBird = Bird.Get();
	AABTSM2Planet* TargetPlanet = Planet.Get();
	if (TargetBird == nullptr || (!bPlanarFollow && TargetPlanet == nullptr)) return;
	if (!bPlanarFollow
		&& UpdateSatelliteFollow(*TargetBird, DeltaSeconds))
	{
		return;
	}
	const FVector Up = bPlanarFollow ? PlanarFollowUp : TargetPlanet->GetRadialUpAtWorldLocation(TargetBird->GetActorLocation());
	FVector Forward = FVector::VectorPlaneProject(TargetBird->GetSlingshotVelocity(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(TargetBird->GetActorForwardVector(), Up).GetSafeNormal();
	const FVector DesiredLocation = TargetBird->GetActorLocation() - Forward * FlightDistanceCM + Up * FlightHeightCM;
	const FVector Look = (TargetBird->GetActorLocation() + Up * 80.0f - DesiredLocation).GetSafeNormal();
	FVector ScreenUp = FVector::VectorPlaneProject(Up, Look).GetSafeNormal();
	if (ScreenUp.IsNearlyZero()) ScreenUp = Up;
	const FVector Location = FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, FollowSpeed);
	const FQuat Rotation = FMath::QInterpTo(GetActorQuat(), FRotationMatrix::MakeFromXZ(Look, ScreenUp).ToQuat(), DeltaSeconds, FollowSpeed);
	SetActorLocationAndRotation(Location, Rotation);
}

bool AABTSM6SlingshotCamera::UpdateSatelliteFollow(
	AABTSM25BirdCharacter& TargetBird,
	const float DeltaSeconds)
{
	AABTSM9Satellite* TargetSatellite = Satellite.Get();
	AActor* TargetE5 = E5Target.Get();
	if (bForcePrimaryFrameUntilNextFollow)
	{
		return false;
	}
	if (TargetSatellite == nullptr || TargetE5 == nullptr)
	{
		if (SatelliteFlightPhase
			!= EABTSM9SatelliteFlightCameraPhase::PrimaryFollow)
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
		}
		return false;
	}
	const FVector SatelliteCenter =
		TargetSatellite->GetPlanetCenterWorld();
	const float SatelliteRadiusCM =
		FMath::Max(1.0f, TargetSatellite->GetPlanetRadiusCM());
	const FVector BirdLocation = TargetBird.GetActorLocation();
	const FVector SatelliteToBird = BirdLocation - SatelliteCenter;
	const float SatelliteDistanceCM = SatelliteToBird.Size();
	const FVector BirdRadialUp = SatelliteToBird.GetSafeNormal();
	if (BirdRadialUp.IsNearlyZero()) return false;

	const float EnterDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			2.0f,
			SatelliteApproachEnterRadiusMultiplier);
	const float ExitDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			SatelliteApproachEnterRadiusMultiplier + 0.1f,
			SatelliteApproachExitRadiusMultiplier);
	const float OrbitDistanceCM =
		SatelliteRadiusCM
		* FMath::Clamp(
			SatelliteOrbitEnterRadiusMultiplier,
			1.1f,
			SatelliteApproachEnterRadiusMultiplier);
	const float OrbitExitDistanceCM =
		SatelliteRadiusCM
		* FMath::Max(
			SatelliteOrbitEnterRadiusMultiplier + 0.1f,
			SatelliteOrbitExitRadiusMultiplier);

	if (SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
		&& SatelliteDistanceCM > EnterDistanceCM)
	{
		return false;
	}
	if (!bSatelliteE5Hit
		&& SatelliteFlightPhase
			!= EABTSM9SatelliteFlightCameraPhase::PrimaryFollow
		&& SatelliteDistanceCM >= ExitDistanceCM)
	{
		SatelliteOrbitViewNormal = FVector::ZeroVector;
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::PrimaryFollow);
		return false;
	}

	const FVector Velocity = TargetBird.GetSlingshotVelocity();
	if (SatelliteFlightPhase
		== EABTSM9SatelliteFlightCameraPhase::PrimaryFollow)
	{
		FVector CandidateNormal =
			FVector::CrossProduct(
				BirdRadialUp,
				Velocity.GetSafeNormal()).GetSafeNormal();
		if (CandidateNormal.IsNearlyZero())
		{
			CandidateNormal = FVector::VectorPlaneProject(
				GetActorLocation() - SatelliteCenter,
				BirdRadialUp).GetSafeNormal();
		}
		if (CandidateNormal.IsNearlyZero())
		{
			CandidateNormal = FVector::CrossProduct(
				BirdRadialUp,
				FMath::Abs(BirdRadialUp.Z) < 0.9f
					? FVector::UpVector
					: FVector::ForwardVector).GetSafeNormal();
		}
		if (FVector::DotProduct(
			CandidateNormal,
			GetActorLocation() - SatelliteCenter) < 0.0f)
		{
			CandidateNormal *= -1.0f;
		}
		SatelliteOrbitViewNormal = CandidateNormal;
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::SatelliteApproach);
	}
	if (bSatelliteE5Hit)
	{
		SetSatelliteFlightPhase(
			EABTSM9SatelliteFlightCameraPhase::E5Impact);
	}
	else
	{
		const FVector E5Location = TargetE5->GetActorLocation();
		const FVector E5Up =
			(E5Location - SatelliteCenter).GetSafeNormal();
		const bool bWasE5Approach =
			SatelliteFlightPhase
				== EABTSM9SatelliteFlightCameraPhase::E5Approach;
		const float E5DistanceThresholdCM = bWasE5Approach
			? FMath::Max(
				SatelliteE5ApproachDistanceCM + 100.0f,
				SatelliteE5ApproachExitDistanceCM)
			: FMath::Max(
				100.0f,
				SatelliteE5ApproachDistanceCM);
		const bool bNearE5 =
			FVector::Distance(BirdLocation, E5Location)
				<= E5DistanceThresholdCM
			&& FVector::DotProduct(BirdRadialUp, E5Up)
				> (bWasE5Approach ? -0.08f : 0.0f);
		const bool bWasOrbitFraming =
			SatelliteFlightPhase
				== EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit
			|| bWasE5Approach;
		if (bNearE5)
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::E5Approach);
		}
		else if (SatelliteDistanceCM
			<= (bWasOrbitFraming
				? OrbitExitDistanceCM
				: OrbitDistanceCM))
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit);
		}
		else
		{
			SetSatelliteFlightPhase(
				EABTSM9SatelliteFlightCameraPhase::SatelliteApproach);
		}
	}

	if (SatelliteOrbitViewNormal.IsNearlyZero()) return false;
	const FVector E5Location = TargetE5->GetActorLocation();
	const bool bE5Phase =
		SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::E5Approach
		|| SatelliteFlightPhase
			== EABTSM9SatelliteFlightCameraPhase::E5Impact;
	const float FocusBias =
		FMath::Clamp(SatelliteFocusBias, 0.0f, 1.0f);
	const FVector Focus = bE5Phase
		? FMath::Lerp(BirdLocation, E5Location, 0.68f)
		: FMath::Lerp(
			BirdLocation,
			SatelliteCenter,
			SatelliteFlightPhase
				== EABTSM9SatelliteFlightCameraPhase::SatelliteOrbit
					? 0.50f
					: FocusBias);
	const FVector FrameUp = bE5Phase
		? (E5Location - SatelliteCenter).GetSafeNormal()
		: BirdRadialUp;
	const float SideDistanceCM = FMath::Max(
		FMath::Max(100.0f, SatelliteSideViewDistanceCM),
		SatelliteRadiusCM * 2.1f);
	const FVector DesiredLocation =
		Focus
		+ SatelliteOrbitViewNormal * SideDistanceCM
		+ FrameUp * FMath::Max(0.0f, SatelliteSideViewHeightCM);
	const FVector Look = (Focus - DesiredLocation).GetSafeNormal();
	FVector ScreenUp =
		FVector::VectorPlaneProject(FrameUp, Look).GetSafeNormal();
	if (Look.IsNearlyZero()) return false;
	if (ScreenUp.IsNearlyZero())
	{
		ScreenUp = FVector::VectorPlaneProject(
			BirdRadialUp,
			Look).GetSafeNormal();
	}
	if (ScreenUp.IsNearlyZero()) return false;
	const float BlendSpeed =
		FMath::Max(0.1f, SatelliteFollowBlendSpeed);
	const FVector Location = FMath::VInterpTo(
		GetActorLocation(),
		DesiredLocation,
		DeltaSeconds,
		BlendSpeed);
	const FQuat Rotation = FMath::QInterpTo(
		GetActorQuat(),
		FRotationMatrix::MakeFromXZ(
			Look,
			ScreenUp).ToQuat(),
		DeltaSeconds,
		BlendSpeed);
	SetActorLocationAndRotation(Location, Rotation);
	return true;
}

