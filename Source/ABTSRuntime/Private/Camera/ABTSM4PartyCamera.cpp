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
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	AABTSM25BirdCharacter* TargetBird = ResolvedParty ? ResolvedParty->GetControlledBird() : nullptr;
	if (ResolvedParty == nullptr || ResolvedPlanet == nullptr || TargetBird == nullptr) return;

	const AABTSBirdPartySettings* Settings = ResolvedParty->GetResolvedSettings();
	const float LookAtHeightCM = Settings ? Settings->CameraLookAtHeightCM : 35.0f;
	const float PivotFollowSpeed = Settings ? Settings->OrbitPivotFollowSpeed : 7.5f;
	const float RotationFollowSpeed = Settings ? Settings->OrbitRotationFollowSpeed : 12.0f;
	const float DeadZoneCM = Settings ? Settings->CameraPivotDeadZoneCM : 22.0f;
	const float SwitchBlendSeconds = Settings ? Settings->CameraSwitchBlendSeconds : 0.48f;
	const float MinDistance = Settings ? Settings->MinOrbitDistanceCM : 550.0f;
	const float MaxDistance = Settings ? Settings->MaxOrbitDistanceCM : 1300.0f;
	GetCameraComponent()->SetFieldOfView(Settings ? Settings->CameraFieldOfViewDegrees : 52.0f);
	ElevationDegrees = FMath::Clamp(ElevationDegrees, -OrbitPitchLimitDegrees, OrbitPitchLimitDegrees);
	OrbitDistanceCM = FMath::Clamp(OrbitDistanceCM, FMath::Min(MinDistance, MaxDistance), FMath::Max(MinDistance, MaxDistance));

	const FVector PlanetCenter = ResolvedPlanet->GetPlanetCenterWorld();
	const FVector TargetLocation = TargetBird->GetActorLocation();
	const FVector TargetUp = ResolvedPlanet->GetRadialUpAtWorldLocation(TargetLocation);
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
		SmoothedPivot = BlendPivotOnSphere(SwitchStartPivot, RawPivot, EasedAlpha, PlanetCenter);
		bSwitchBlendActive = LinearAlpha < 1.0f;
	}
	else if (bForceInstant || !bInitializedView)
	{
		SmoothedPivot = RawPivot;
	}
	else if (FVector::Distance(SmoothedPivot, RawPivot) > DeadZoneCM)
	{
		const FVector Interpolated = FMath::VInterpTo(SmoothedPivot, RawPivot, DeltaSeconds, PivotFollowSpeed);
		const float TargetRadius = FVector::Distance(RawPivot, PlanetCenter);
		SmoothedPivot = PlanetCenter + (Interpolated - PlanetCenter).GetSafeNormal() * TargetRadius;
	}

	const FVector CameraUp = (SmoothedPivot - PlanetCenter).GetSafeNormal();
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
	EffectiveDistanceCM = ResolveObstructedDistance(SmoothedPivot, UnblockedLocation, OrbitDistanceCM, DeltaSeconds);
	const FVector DesiredLocation = SmoothedPivot + UnblockedOffsetDirection * EffectiveDistanceCM;
	const FVector DesiredLookDirection = (SmoothedPivot - DesiredLocation).GetSafeNormal();
	// Forward alone does not define camera roll. Constrain the screen-up axis to
	// the focus point's radial Up projected onto the image plane, so the visible
	// world cannot roll sideways or invert while orbiting.
	FVector DesiredScreenUp = FVector::VectorPlaneProject(CameraUp, DesiredLookDirection).GetSafeNormal();
	if (DesiredScreenUp.IsNearlyZero()) DesiredScreenUp = OrbitForwardTangent;
	const FQuat DesiredRotation = FRotationMatrix::MakeFromXZ(DesiredLookDirection, DesiredScreenUp).ToQuat();
	const bool bInstant = bForceInstant || !bInitializedView;
	const FVector NewLocation = bInstant
		? DesiredLocation
		: FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, PivotFollowSpeed);
	const FQuat InterpolatedRotation = bInstant
		? DesiredRotation
		: FMath::QInterpTo(GetActorQuat(), DesiredRotation, DeltaSeconds, RotationFollowSpeed).GetNormalized();
	// A shortest-arc quaternion interpolation is continuous, but its intermediate
	// orientation can still contain roll. Rebuild the final basis around the
	// interpolated look direction and current radial Up every frame.
	const FVector InterpolatedLookDirection = InterpolatedRotation.GetForwardVector().GetSafeNormal();
	FVector ConstrainedScreenUp = FVector::VectorPlaneProject(CameraUp, InterpolatedLookDirection).GetSafeNormal();
	if (ConstrainedScreenUp.IsNearlyZero()) ConstrainedScreenUp = DesiredScreenUp;
	const FQuat RollLockedRotation = FRotationMatrix::MakeFromXZ(
		InterpolatedLookDirection,
		ConstrainedScreenUp).ToQuat();
	SetActorLocationAndRotation(NewLocation, RollLockedRotation, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector ActualScreenUp = RollLockedRotation.RotateVector(FVector::UpVector).GetSafeNormal();
	const float RollErrorDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(ActualScreenUp, ConstrainedScreenUp), -1.0f, 1.0f)));

	if (bTargetChanged)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M4][OrbitCamera] Target=%d Distance=%.1f Elevation=%.1f PitchRange=[%.1f,%.1f] SwitchBlend=%.2f PivotLag=%.2f RollError=%.3f"),
			ABTSBirdIdToIndex(TargetBird->GetBirdId()),
			OrbitDistanceCM,
			ElevationDegrees,
			-OrbitPitchLimitDegrees,
			OrbitPitchLimitDegrees,
			SwitchBlendSeconds,
			PivotFollowSpeed,
			RollErrorDegrees);
	}
	LastTargetBird = TargetBird;
	bInitializedView = true;
}

float AABTSM4PartyCamera::ResolveObstructedDistance(
	const FVector& Pivot,
	const FVector& DesiredLocation,
	const float DesiredDistance,
	const float DeltaSeconds)
{
	AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	UWorld* World = GetWorld();
	if (World == nullptr) return DesiredDistance;
	const float ProbeRadius = Settings ? Settings->CameraProbeRadiusCM : 24.0f;
	const float PullInSpeed = Settings ? Settings->CameraObstructionPullInSpeed : 22.0f;
	const float RestoreSpeed = Settings ? Settings->CameraObstructionRestoreSpeed : 5.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ABTSM4OrbitCamera), false, this);
	for (TActorIterator<AABTSM25BirdCharacter> It(World); It; ++It) QueryParams.AddIgnoredActor(*It);
	if (ResolvedParty) QueryParams.AddIgnoredActor(ResolvedParty);
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(
		Hit,
		Pivot,
		DesiredLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(FMath::Max(1.0f, ProbeRadius)),
		QueryParams);
	// Collision safety must override any comfort minimum. This is especially
	// important for negative elevation: the orbit ray approaches the ground and
	// must be allowed to contract close to the pivot instead of forcing the
	// camera through the terrain to preserve an arbitrary minimum arm length.
	const float TargetDistance = bHit && Hit.bBlockingHit
		? FMath::Clamp(DesiredDistance * Hit.Time - ProbeRadius, 1.0f, DesiredDistance)
		: DesiredDistance;
	const float InterpSpeed = TargetDistance < EffectiveDistanceCM ? PullInSpeed : RestoreSpeed;
	return DeltaSeconds <= 0.0f
		? TargetDistance
		: FMath::FInterpTo(EffectiveDistanceCM, TargetDistance, DeltaSeconds, InterpSpeed);
}

void AABTSM4PartyCamera::AddOrbitYawInput(const float Value)
{
	if (!bInitializedView || FMath::IsNearlyZero(Value)) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float DegreesPerInput = Settings ? Settings->OrbitYawDegreesPerInput : 1.0f;
	OrbitForwardTangent = OrbitForwardTangent.RotateAngleAxis(Value * DegreesPerInput, PreviousUp).GetSafeNormal();
	bRecenterRequested = false;
}

void AABTSM4PartyCamera::AddOrbitPitchInput(const float Value)
{
	if (!bInitializedView || FMath::IsNearlyZero(Value)) return;
	const AABTSBirdParty* ResolvedParty = FindParty();
	const AABTSBirdPartySettings* Settings = ResolvedParty ? ResolvedParty->GetResolvedSettings() : nullptr;
	const float DegreesPerInput = Settings ? Settings->OrbitPitchDegreesPerInput : 0.7f;
	ElevationDegrees = FMath::Clamp(
		ElevationDegrees + Value * DegreesPerInput,
		-OrbitPitchLimitDegrees,
		OrbitPitchLimitDegrees);
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
	if (!bInitializedView || ResolvedPlanet == nullptr) return false;
	const FVector Up = ResolvedPlanet->GetRadialUpAtWorldLocation(WorldLocation);
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
