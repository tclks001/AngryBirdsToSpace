// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM4PartyCamera.h"

#include "ABTSRuntime.h"
#include "Camera/CameraComponent.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"

namespace
{
	// Signed elevation: +85 is near top-down, 0 is tangent/horizontal and
	// -85 is near bottom-up. Avoid +/-90 because radial screen-up degenerates
	// when the look direction becomes exactly parallel to radial Up.
	constexpr float OrbitPitchLimitDegrees = 85.0f;

	struct FABTSM4CameraSweepCandidate
	{
		FVector Direction = FVector::BackwardVector;
		FHitResult Hit;
		float SafeDistanceCM = 0.0f;
		float YawOffsetDegrees = 0.0f;
		float VerticalOffsetDegrees = 0.0f;
		float Score = 0.0f;
		int32 Index = 0;
		bool bBlocked = false;
	};

	FVector BuildOffsetArmDirection(
		const FVector& DesiredArmDirection,
		const FVector& CameraUp,
		const float YawOffsetDegrees,
		const float VerticalOffsetDegrees)
	{
		FVector Direction = DesiredArmDirection.RotateAngleAxis(YawOffsetDegrees, CameraUp).GetSafeNormal();
		FVector Horizontal = FVector::VectorPlaneProject(Direction, CameraUp).GetSafeNormal();
		if (Horizontal.IsNearlyZero()) return Direction;
		const float CurrentElevation = FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction, CameraUp), -1.0f, 1.0f));
		const float TargetElevation = FMath::Clamp(
			CurrentElevation + FMath::DegreesToRadians(VerticalOffsetDegrees),
			FMath::DegreesToRadians(-OrbitPitchLimitDegrees),
			FMath::DegreesToRadians(OrbitPitchLimitDegrees));
		return (Horizontal * FMath::Cos(TargetElevation) + CameraUp * FMath::Sin(TargetElevation)).GetSafeNormal();
	}
}

AABTSM4PartyCamera::AABTSM4PartyCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	GetCameraComponent()->SetFieldOfView(52.0f);
}

void AABTSM4PartyCamera::BeginPlay()
{
	Super::BeginPlay();
	UpdateCamera(0.0f, true);
}

void AABTSM4PartyCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCamera(DeltaSeconds, false);
}

void AABTSM4PartyCamera::InitializeOrbit(AABTSM25BirdCharacter& TargetBird, const FVector& Up)
{
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	OrbitForwardTangent = FVector::VectorPlaneProject(TargetBird.GetActorForwardVector(), Up).GetSafeNormal();
	if (OrbitForwardTangent.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(Up.Z) < 0.95f ? FVector::UpVector : FVector::ForwardVector;
		OrbitForwardTangent = FVector::CrossProduct(Reference, Up).GetSafeNormal();
	}
	ElevationDegrees = Settings ? Settings->DefaultElevationDegrees : 60.0f;
	OrbitDistanceCM = Settings ? Settings->OrbitDistanceCM : 850.0f;
	EffectiveDistanceCM = OrbitDistanceCM;
	ObstructionFilter.Reset(OrbitDistanceCM);
	ObstructionYawOffsetDegrees = 0.0f;
	ObstructionVerticalOffsetDegrees = 0.0f;
	SelectedObstructionCandidate = 0;
	PreviousUp = Up;
}

void AABTSM4PartyCamera::TransportOrbitForward(const FVector& NewUp)
{
	if (NewUp.IsNearlyZero()) return;
	if (!PreviousUp.IsNearlyZero())
	{
		const FQuat Transport = FQuat::FindBetweenNormals(PreviousUp.GetSafeNormal(), NewUp.GetSafeNormal());
		OrbitForwardTangent = Transport.RotateVector(OrbitForwardTangent);
	}
	OrbitForwardTangent = FVector::VectorPlaneProject(OrbitForwardTangent, NewUp).GetSafeNormal();
	if (OrbitForwardTangent.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(NewUp.Z) < 0.95f ? FVector::UpVector : FVector::ForwardVector;
		OrbitForwardTangent = FVector::CrossProduct(Reference, NewUp).GetSafeNormal();
	}
	PreviousUp = NewUp;
}

FVector AABTSM4PartyCamera::BlendPivotOnSphere(
	const FVector& Start,
	const FVector& End,
	const float Alpha,
	const FVector& PlanetCenter) const
{
	const FVector StartOffset = Start - PlanetCenter;
	const FVector EndOffset = End - PlanetCenter;
	const float StartRadius = StartOffset.Size();
	const float EndRadius = EndOffset.Size();
	const FVector StartDirection = StartOffset.GetSafeNormal();
	const FVector EndDirection = EndOffset.GetSafeNormal();
	if (StartDirection.IsNearlyZero() || EndDirection.IsNearlyZero()) return FMath::Lerp(Start, End, Alpha);
	const FQuat ArcRotation = FQuat::FindBetweenNormals(StartDirection, EndDirection);
	const FVector BlendedDirection = FQuat::Slerp(FQuat::Identity, ArcRotation, Alpha).RotateVector(StartDirection).GetSafeNormal();
	return PlanetCenter + BlendedDirection * FMath::Lerp(StartRadius, EndRadius, Alpha);
}

void AABTSM4PartyCamera::UpdateCamera(const float DeltaSeconds, const bool bForceInstant)
{
	AABTSBirdParty* ResolvedParty = FindParty();
	const bool bPlanar = ResolvedParty && ResolvedParty->IsPlanarParty();
	AABTSM2Planet* ResolvedPlanet = bPlanar ? nullptr : FindPlanet();
	AABTSM25BirdCharacter* TargetBird = ResolvedParty ? ResolvedParty->GetControlledBird() : nullptr;
	if (ResolvedParty == nullptr || (!bPlanar && ResolvedPlanet == nullptr) || TargetBird == nullptr) return;

	const AABTSBirdPartySettings* Settings = ResolvedParty->GetResolvedSettings();
	const float LookAtHeightCM = Settings ? Settings->CameraLookAtHeightCM : 35.0f;
	const float PivotFollowSpeed = Settings
		? (bDirectManipulation ? Settings->PivotFollowWhileOrbitingSpeed : Settings->OrbitPivotFollowSpeed)
		: (bDirectManipulation ? 18.0f : 7.5f);
	const float MaxPivotLagCM = Settings ? Settings->CameraMaxPivotLagCM : 180.0f;
	const float DeadZoneCM = Settings ? Settings->CameraPivotDeadZoneCM : 22.0f;
	const float SwitchBlendSeconds = Settings ? Settings->CameraSwitchBlendSeconds : 0.48f;
	const float MinDistance = Settings ? Settings->MinOrbitDistanceCM : 550.0f;
	const float MaxDistance = Settings ? Settings->MaxOrbitDistanceCM : 1300.0f;
	GetCameraComponent()->SetFieldOfView(Settings ? Settings->CameraFieldOfViewDegrees : 52.0f);
	ElevationDegrees = FMath::Clamp(ElevationDegrees, -OrbitPitchLimitDegrees, OrbitPitchLimitDegrees);
	OrbitDistanceCM = FMath::Clamp(OrbitDistanceCM, FMath::Min(MinDistance, MaxDistance), FMath::Max(MinDistance, MaxDistance));

	const FVector PlanetCenter = bPlanar ? ResolvedParty->GetPlanarOrigin() : ResolvedPlanet->GetPlanetCenterWorld();
	const FVector TargetLocation = TargetBird->GetActorLocation();
	const FVector TargetUp = ResolvedParty->GetSurfaceUpAt(TargetLocation);
	const FVector RawPivot = TargetLocation + TargetUp * LookAtHeightCM;
	const bool bTargetChanged = LastTargetBird.Get() != TargetBird;
	if (!bInitializedView)
	{
		InitializeOrbit(*TargetBird, TargetUp);
		SmoothedPivot = RawPivot;
		SwitchStartPivot = RawPivot;
	}
	else if (bTargetChanged)
	{
		SwitchStartPivot = SmoothedPivot;
		SwitchElapsedSeconds = 0.0f;
		bSwitchBlendActive = SwitchBlendSeconds > SMALL_NUMBER;
	}

	if (bSwitchBlendActive)
	{
		SwitchElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
		const float LinearAlpha = FMath::Clamp(SwitchElapsedSeconds / FMath::Max(SwitchBlendSeconds, SMALL_NUMBER), 0.0f, 1.0f);
		const float EasedAlpha = FMath::SmoothStep(0.0f, 1.0f, LinearAlpha);
		SmoothedPivot = bPlanar
			? FMath::Lerp(SwitchStartPivot, RawPivot, EasedAlpha)
			: BlendPivotOnSphere(SwitchStartPivot, RawPivot, EasedAlpha, PlanetCenter);
		bSwitchBlendActive = LinearAlpha < 1.0f;
	}
	else if (bForceInstant || !bInitializedView)
	{
		SmoothedPivot = RawPivot;
	}
	else if (bPlanar && FVector::Distance(SmoothedPivot, RawPivot) > DeadZoneCM)
	{
		FVector Interpolated = FMath::VInterpTo(SmoothedPivot, RawPivot, DeltaSeconds, PivotFollowSpeed);
		const FVector RemainingLag = RawPivot - Interpolated;
		if (MaxPivotLagCM > 0.0f && RemainingLag.SizeSquared() > FMath::Square(MaxPivotLagCM))
		{
			Interpolated = RawPivot - RemainingLag.GetSafeNormal() * MaxPivotLagCM;
		}
		SmoothedPivot = Interpolated;
	}
	else if (!bPlanar)
	{
		SmoothedPivot = ABTSM4CameraRigModel::UpdateSphericalPivot(
			SmoothedPivot,
			RawPivot,
			PlanetCenter,
			DeltaSeconds,
			PivotFollowSpeed,
			MaxPivotLagCM,
			DeadZoneCM,
			TargetBird->IsRadiallyGrounded());
	}

	const FVector CameraUp = bPlanar ? TargetUp : (SmoothedPivot - PlanetCenter).GetSafeNormal();
	TransportOrbitForward(CameraUp);
	if (bRecenterRequested)
	{
		const FVector TargetForward = FVector::VectorPlaneProject(TargetBird->GetActorForwardVector(), CameraUp).GetSafeNormal();
		if (!TargetForward.IsNearlyZero())
		{
			OrbitForwardTangent = FMath::VInterpTo(OrbitForwardTangent, TargetForward, DeltaSeconds, 5.0f).GetSafeNormal();
			OrbitForwardTangent = FVector::VectorPlaneProject(OrbitForwardTangent, CameraUp).GetSafeNormal();
			bRecenterRequested = FVector::DotProduct(OrbitForwardTangent, TargetForward) < 0.9995f;
		}
		else
		{
			bRecenterRequested = false;
		}
	}

	const float ElevationRadians = FMath::DegreesToRadians(ElevationDegrees);
	const FVector UnblockedOffsetDirection = (
		CameraUp * FMath::Sin(ElevationRadians)
		- OrbitForwardTangent * FMath::Cos(ElevationRadians)).GetSafeNormal();
	const FVector UnblockedLocation = SmoothedPivot + UnblockedOffsetDirection * OrbitDistanceCM;
	float HardSafeDistanceCM = OrbitDistanceCM;
	FVector RenderedLocation = UnblockedLocation;
	if (Settings && Settings->bEnableCameraObstructionAvoidance)
	{
		RenderedLocation = ResolveObstructedLocation(
			SmoothedPivot,
			CameraUp,
			UnblockedOffsetDirection,
			OrbitDistanceCM,
			DeltaSeconds,
			HardSafeDistanceCM);
	}
	else
	{
		// Intentional no-op obstruction policy: keep the requested camera pose and
		// let world geometry remain visibly between the camera and controlled bird.
		EffectiveDistanceCM = OrbitDistanceCM;
		ObstructionFilter.Reset(OrbitDistanceCM);
		ObstructionYawOffsetDegrees = 0.0f;
		ObstructionVerticalOffsetDegrees = 0.0f;
		SelectedObstructionCandidate = 0;
		LastLoggedObstructionCandidate = 0;
		LastLoggedObstructionPhase = EABTSM4CameraObstructionPhase::Clear;
		PoseSnapshot.SafeLocation = UnblockedLocation;
		PoseSnapshot.BlockingActor.Reset();
		PoseSnapshot.BlockingComponent.Reset();
	}
	const FVector DesiredLookDirection = (SmoothedPivot - RenderedLocation).GetSafeNormal();
	// Forward alone does not define camera roll. Constrain the screen-up axis to
	// the focus point's radial Up projected onto the image plane, so the visible
	// world cannot roll sideways or invert while orbiting.
	FVector DesiredScreenUp = FVector::VectorPlaneProject(CameraUp, DesiredLookDirection).GetSafeNormal();
	if (DesiredScreenUp.IsNearlyZero()) DesiredScreenUp = OrbitForwardTangent;
	const FQuat DesiredRotation = FRotationMatrix::MakeFromXZ(DesiredLookDirection, DesiredScreenUp).ToQuat();
	// User orbit is a direct target, not a physical body. Follow lag remains on
	// the pivot only; adding another location/rotation filter here creates the
	// residual motion that used to continue after mouse release.
	const FVector RenderedLookDirection = DesiredRotation.GetForwardVector().GetSafeNormal();
	FVector ConstrainedScreenUp = FVector::VectorPlaneProject(CameraUp, RenderedLookDirection).GetSafeNormal();
	if (ConstrainedScreenUp.IsNearlyZero()) ConstrainedScreenUp = DesiredScreenUp;
	const FQuat RollLockedRotation = FRotationMatrix::MakeFromXZ(
		RenderedLookDirection,
		ConstrainedScreenUp).ToQuat();
	SetActorLocationAndRotation(RenderedLocation, RollLockedRotation, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector ActualScreenUp = RollLockedRotation.RotateVector(FVector::UpVector).GetSafeNormal();
	const float RollErrorDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(ActualScreenUp, ConstrainedScreenUp), -1.0f, 1.0f)));

	if (bTargetChanged)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][OrbitCamera] Target=%d Distance=%.1f Elevation=%.1f PitchRange=[%.1f,%.1f] SwitchBlend=%.2f PivotLag=%.2f Direct=%d RollError=%.3f"),
			ABTSBirdIdToIndex(TargetBird->GetBirdId()),
			OrbitDistanceCM,
			ElevationDegrees,
			-OrbitPitchLimitDegrees,
			OrbitPitchLimitDegrees,
			SwitchBlendSeconds,
			PivotFollowSpeed,
			bDirectManipulation ? 1 : 0,
			RollErrorDegrees);
	}
	PoseSnapshot.Pivot = SmoothedPivot;
	PoseSnapshot.DesiredLocation = UnblockedLocation;
	PoseSnapshot.RenderedLocation = RenderedLocation;
	PoseSnapshot.DesiredDistanceCM = OrbitDistanceCM;
	PoseSnapshot.SafeDistanceCM = HardSafeDistanceCM;
	PoseSnapshot.RenderedDistanceCM = EffectiveDistanceCM;
	PoseSnapshot.UserElevationDegrees = ElevationDegrees;
	PoseSnapshot.ObstructionCandidateIndex = SelectedObstructionCandidate;
	PoseSnapshot.ObstructionPhase = ObstructionFilter.GetPhase();
	PoseSnapshot.bDirectManipulation = bDirectManipulation;
	LastTargetBird = TargetBird;
	bInitializedView = true;
}

FVector AABTSM4PartyCamera::ResolveObstructedLocation(
	const FVector& Pivot,
	const FVector& CameraUp,
	const FVector& DesiredArmDirection,
	const float DesiredDistance,
	const float DeltaSeconds,
	float& OutSafeDistance)
{
	AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutSafeDistance = DesiredDistance;
		EffectiveDistanceCM = DesiredDistance;
		return Pivot + DesiredArmDirection * DesiredDistance;
	}
	const float ProbeRadius = Settings ? Settings->CameraProbeRadiusCM : 24.0f;
	const float SafetyMargin = Settings ? Settings->CameraCollisionSafetyMarginCM : 4.0f;
	const float LateralEscapeDegrees = Settings ? Settings->CameraObstructionLateralEscapeDegrees : 8.0f;
	const float VerticalEscapeDegrees = Settings ? Settings->CameraObstructionVerticalEscapeDegrees : 7.0f;
	const float CandidateMinBenefitCM = Settings ? Settings->CameraObstructionCandidateMinBenefitCM : 80.0f;
	const float OffsetBlendSpeed = Settings ? Settings->CameraObstructionOffsetBlendDegreesPerSecond : 100.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ABTSM4OrbitCamera), false, this);
	if (ResolvedParty)
	{
		QueryParams.AddIgnoredActor(ResolvedParty);
		for (const AABTSM25BirdCharacter* Bird : ResolvedParty->GetPartyMembers())
		{
			if (Bird) QueryParams.AddIgnoredActor(Bird);
		}
	}

	auto SweepCandidate = [&](const int32 Index, const float YawOffset, const float VerticalOffset)
	{
		FABTSM4CameraSweepCandidate Candidate;
		Candidate.Index = Index;
		Candidate.YawOffsetDegrees = YawOffset;
		Candidate.VerticalOffsetDegrees = VerticalOffset;
		Candidate.Direction = BuildOffsetArmDirection(DesiredArmDirection, CameraUp, YawOffset, VerticalOffset);
		Candidate.bBlocked = World->SweepSingleByChannel(
			Candidate.Hit,
			Pivot,
			Pivot + Candidate.Direction * DesiredDistance,
			FQuat::Identity,
			ECC_Camera,
			FCollisionShape::MakeSphere(FMath::Max(1.0f, ProbeRadius)),
			QueryParams) && Candidate.Hit.bBlockingHit;
		Candidate.SafeDistanceCM = ABTSM4CameraRigModel::ComputeSafeSweepDistance(
			DesiredDistance,
			Candidate.bBlocked,
			Candidate.Hit.bStartPenetrating,
			Candidate.Hit.Distance,
			SafetyMargin);
		const float AngularCost = (FMath::Abs(YawOffset) + FMath::Abs(VerticalOffset)) * 5.0f;
		const float StickyBonus = Index == SelectedObstructionCandidate ? 35.0f : 0.0f;
		Candidate.Score = Candidate.SafeDistanceCM - AngularCost + StickyBonus;
		return Candidate;
	};

	TStaticArray<FABTSM4CameraSweepCandidate, 4> Candidates;
	Candidates[0] = SweepCandidate(0, 0.0f, 0.0f);
	Candidates[1] = SweepCandidate(1, 0.0f, VerticalEscapeDegrees);
	Candidates[2] = SweepCandidate(2, -LateralEscapeDegrees, 0.0f);
	Candidates[3] = SweepCandidate(3, LateralEscapeDegrees, 0.0f);

	int32 TargetCandidate = 0;
	if (Candidates[0].bBlocked)
	{
		for (int32 Index = 1; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index].SafeDistanceCM >= Candidates[0].SafeDistanceCM + CandidateMinBenefitCM
				&& Candidates[Index].Score > Candidates[TargetCandidate].Score)
			{
				TargetCandidate = Index;
			}
		}
	}
	else if (ObstructionFilter.GetPhase() != EABTSM4CameraObstructionPhase::Clear
		&& SelectedObstructionCandidate > 0
		&& SelectedObstructionCandidate < Candidates.Num())
	{
		// Hold the previous framing candidate through the clear-side hysteresis.
		TargetCandidate = SelectedObstructionCandidate;
	}
	SelectedObstructionCandidate = TargetCandidate;

	const FABTSM4CameraSweepCandidate& Target = Candidates[TargetCandidate];
	ObstructionYawOffsetDegrees = FMath::FInterpConstantTo(
		ObstructionYawOffsetDegrees,
		Target.YawOffsetDegrees,
		FMath::Max(0.0f, DeltaSeconds),
		FMath::Max(1.0f, OffsetBlendSpeed));
	ObstructionVerticalOffsetDegrees = FMath::FInterpConstantTo(
		ObstructionVerticalOffsetDegrees,
		Target.VerticalOffsetDegrees,
		FMath::Max(0.0f, DeltaSeconds),
		FMath::Max(1.0f, OffsetBlendSpeed));

	// Re-sweep the blended offset. This keeps the transition itself collision
	// safe instead of assuming that both safe endpoints imply a safe arc.
	const FABTSM4CameraSweepCandidate Blended = SweepCandidate(
		TargetCandidate,
		ObstructionYawOffsetDegrees,
		ObstructionVerticalOffsetDegrees);
	FABTSM4CameraObstructionFilterSettings FilterSettings;
	FilterSettings.EnterDelaySeconds = Settings ? Settings->CameraObstructionEnterDelaySeconds : 0.04f;
	FilterSettings.ExitDelaySeconds = Settings ? Settings->CameraObstructionExitDelaySeconds : 0.16f;
	EffectiveDistanceCM = ObstructionFilter.Update(
		Candidates[0].bBlocked,
		Blended.SafeDistanceCM,
		DesiredDistance,
		TargetCandidate != 0,
		DeltaSeconds,
		FilterSettings);
	OutSafeDistance = Blended.SafeDistanceCM;
	PoseSnapshot.SafeLocation = Pivot + Blended.Direction * Blended.SafeDistanceCM;
	PoseSnapshot.BlockingActor = Candidates[0].bBlocked ? Candidates[0].Hit.GetActor() : Blended.Hit.GetActor();
	PoseSnapshot.BlockingComponent = Candidates[0].bBlocked ? Candidates[0].Hit.GetComponent() : Blended.Hit.GetComponent();

	const EABTSM4CameraObstructionPhase Phase = ObstructionFilter.GetPhase();
	if (Phase != LastLoggedObstructionPhase || TargetCandidate != LastLoggedObstructionCandidate)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][CameraObstruction] Phase=%s Candidate=%d Desired=%.1f Safe=%.1f Rendered=%.1f Direct=%d Blocker=%s Component=%s"),
			ABTSM4CameraRigModel::LexToString(Phase),
			TargetCandidate,
			DesiredDistance,
			Blended.SafeDistanceCM,
			EffectiveDistanceCM,
			bDirectManipulation ? 1 : 0,
			*GetNameSafe(PoseSnapshot.BlockingActor.Get()),
			*GetNameSafe(PoseSnapshot.BlockingComponent.Get()));
		LastLoggedObstructionPhase = Phase;
		LastLoggedObstructionCandidate = TargetCandidate;
	}
	return Pivot + Blended.Direction * EffectiveDistanceCM;
}

void AABTSM4PartyCamera::AddOrbitYawInput(const float Value)
{
	AddMouseOrbitYawInput(Value);
}

void AABTSM4PartyCamera::AddOrbitPitchInput(const float Value)
{
	AddMouseOrbitPitchInput(Value);
}

void AABTSM4PartyCamera::BeginDirectManipulation()
{
	bDirectManipulation = true;
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::EndDirectManipulation()
{
	bDirectManipulation = false;
}

void AABTSM4PartyCamera::AddMouseOrbitYawInput(const float MouseDeltaPixels)
{
	if (!bInitializedView || FMath::IsNearlyZero(MouseDeltaPixels)) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float DegreesPerPixel = Settings ? Settings->MouseYawDegreesPerPixel : 0.14f;
	OrbitForwardTangent = OrbitForwardTangent.RotateAngleAxis(MouseDeltaPixels * DegreesPerPixel, PreviousUp).GetSafeNormal();
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::AddMouseOrbitPitchInput(const float MouseDeltaPixels)
{
	if (!bInitializedView || FMath::IsNearlyZero(MouseDeltaPixels)) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float DegreesPerPixel = Settings ? Settings->MousePitchDegreesPerPixel : 0.11f;
	ElevationDegrees = FMath::Clamp(
		ElevationDegrees + MouseDeltaPixels * DegreesPerPixel,
		-OrbitPitchLimitDegrees,
		OrbitPitchLimitDegrees);
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::AddGamepadOrbitYawInput(const float AxisValue, const float DeltaSeconds)
{
	if (!bInitializedView || DeltaSeconds <= 0.0f) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float Response = ABTSM4CameraRigModel::ApplyGamepadResponse(
		AxisValue,
		Settings ? Settings->GamepadLookDeadZone : 0.18f,
		Settings ? Settings->GamepadLookExponent : 1.35f);
	if (FMath::IsNearlyZero(Response)) return;
	const float Rate = Settings ? Settings->GamepadYawDegreesPerSecond : 120.0f;
	OrbitForwardTangent = OrbitForwardTangent.RotateAngleAxis(Response * Rate * DeltaSeconds, PreviousUp).GetSafeNormal();
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::AddGamepadOrbitPitchInput(const float AxisValue, const float DeltaSeconds)
{
	if (!bInitializedView || DeltaSeconds <= 0.0f) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float Response = ABTSM4CameraRigModel::ApplyGamepadResponse(
		AxisValue,
		Settings ? Settings->GamepadLookDeadZone : 0.18f,
		Settings ? Settings->GamepadLookExponent : 1.35f);
	if (FMath::IsNearlyZero(Response)) return;
	const float Rate = Settings ? Settings->GamepadPitchDegreesPerSecond : 90.0f;
	ElevationDegrees = FMath::Clamp(
		ElevationDegrees + Response * Rate * DeltaSeconds,
		-OrbitPitchLimitDegrees,
		OrbitPitchLimitDegrees);
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::AddZoomInput(const float Value)
{
	if (!bInitializedView || FMath::IsNearlyZero(Value)) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float ZoomStep = Settings ? Settings->OrbitZoomStepCM : 80.0f;
	const float MinDistance = Settings ? Settings->MinOrbitDistanceCM : 550.0f;
	const float MaxDistance = Settings ? Settings->MaxOrbitDistanceCM : 1300.0f;
	OrbitDistanceCM = FMath::Clamp(
		OrbitDistanceCM - Value * ZoomStep,
		FMath::Min(MinDistance, MaxDistance),
		FMath::Max(MinDistance, MaxDistance));
}

void AABTSM4PartyCamera::RequestRecenter()
{
	bRecenterRequested = true;
}

bool AABTSM4PartyCamera::GetMovementBasisAt(
	const FVector& WorldLocation,
	FVector& OutForward,
	FVector& OutRight) const
{
	const AABTSM2Planet* ResolvedPlanet = Planet.Get();
	const AABTSBirdParty* ResolvedParty = Party.Get();
	if (!bInitializedView || ResolvedParty == nullptr || (!ResolvedParty->IsPlanarParty() && ResolvedPlanet == nullptr)) return false;
	const FVector Up = ResolvedParty->GetSurfaceUpAt(WorldLocation);
	OutForward = FVector::VectorPlaneProject(OrbitForwardTangent, Up).GetSafeNormal();
	OutRight = FVector::CrossProduct(Up, OutForward).GetSafeNormal();
	return !OutForward.IsNearlyZero() && !OutRight.IsNearlyZero();
}

AABTSBirdParty* AABTSM4PartyCamera::FindParty()
{
	if (Party.IsValid()) return Party.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		Party = *It;
		return Party.Get();
	}
	return nullptr;
}

AABTSM2Planet* AABTSM4PartyCamera::FindPlanet()
{
	if (Planet.IsValid()) return Planet.Get();
	for (TActorIterator<AABTSM2Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			Planet = *It;
			return Planet.Get();
		}
	}
	return nullptr;
}
