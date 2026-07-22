// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ABTSRadialForceMovementComponent.h"

#include "ABTSRuntime.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Movement/ABTSRadialSurfaceSuspensionComponent.h"
#include "Planet/ABTSM2Planet.h"
#include "ProceduralMeshComponent.h"

UABTSRadialForceMovementComponent::UABTSRadialForceMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UABTSRadialForceMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteActor(GetOwner());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][ForceSuspension] Movement ready. Mass=%.1f Gravity=%.1f MoveAccel=%.1f GroundDrag=%.2f TerminalSpeed=%.1f Step=%.5f"),
		VirtualMassKG,
		GravityAccelerationCMPerSec2,
		GroundMoveAccelerationCMPerSec2,
		GroundDragPerSecond,
		GroundDragPerSecond > SMALL_NUMBER ? GroundMoveAccelerationCMPerSec2 / GroundDragPerSecond : 0.0f,
		MaxSimulationStepSeconds);
}

void UABTSRadialForceMovementComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension();
	if (Character == nullptr || ResolvedPlanet == nullptr || ResolvedSuspension == nullptr || DeltaTime <= SMALL_NUMBER)
	{
		if (!bLoggedNoDependencies)
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][ForceSuspension] Tick skipped. Character=%d Planet=%d Suspension=%d"),
				Character ? 1 : 0,
				ResolvedPlanet ? 1 : 0,
				ResolvedSuspension ? 1 : 0);
			bLoggedNoDependencies = true;
		}
		return;
	}
	bLoggedNoDependencies = false;

	float RemainingTime = DeltaTime;
	const float MaxStep = FMath::Max(0.001f, MaxSimulationStepSeconds);
	const int32 StepLimit = FMath::Clamp(MaxSimulationSubsteps, 1, 16);
	for (int32 StepIndex = 0; StepIndex < StepLimit && RemainingTime > SMALL_NUMBER; ++StepIndex)
	{
		const int32 StepsLeft = StepLimit - StepIndex;
		const float StepSeconds = RemainingTime > MaxStep * StepsLeft
			? RemainingTime / static_cast<float>(StepsLeft)
			: FMath::Min(MaxStep, RemainingTime);
		SimulateSubstep(*Character, *ResolvedPlanet, StepSeconds);
		RemainingTime -= StepSeconds;
	}
	PendingMoveVector = FVector::ZeroVector;
}

void UABTSRadialForceMovementComponent::SetMoveInput(const FVector& Direction, const float Scale)
{
	PendingMoveVector += Direction.GetSafeNormal() * FMath::Clamp(Scale, -1.0f, 1.0f);
}

void UABTSRadialForceMovementComponent::QueueJump()
{
	JumpBufferRemainingSeconds = FMath::Max(JumpBufferSeconds, KINDA_SMALL_NUMBER);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ForceSuspension][Jump] Input queued. Grounded=%d Buffer=%.3f"),
		IsGrounded() ? 1 : 0,
		JumpBufferRemainingSeconds);
}

void UABTSRadialForceMovementComponent::ResetMotionState()
{
	Velocity = FVector::ZeroVector;
	PendingMoveVector = FVector::ZeroVector;
	JumpBufferRemainingSeconds = 0.0f;
	if (UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension())
	{
		ResolvedSuspension->ResetSuspensionState();
	}
}

bool UABTSRadialForceMovementComponent::IsGrounded() const
{
	return Suspension.IsValid() && Suspension->IsGrounded();
}

AABTSM2Planet* UABTSRadialForceMovementComponent::FindPlanet()
{
	if (Planet.IsValid() && Planet->IsPlanetReady()) return Planet.Get();
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

UABTSRadialSurfaceSuspensionComponent* UABTSRadialForceMovementComponent::FindSuspension()
{
	if (Suspension.IsValid()) return Suspension.Get();
	Suspension = GetOwner() ? GetOwner()->FindComponentByClass<UABTSRadialSurfaceSuspensionComponent>() : nullptr;
	return Suspension.Get();
}

void UABTSRadialForceMovementComponent::SimulateSubstep(
	ACharacter& Character,
	AABTSM2Planet& ResolvedPlanet,
	const float DeltaTime)
{
	UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension();
	if (ResolvedSuspension == nullptr) return;

	FABTSRadialSuspensionSample Surface = ResolvedSuspension->Evaluate(
		ResolvedPlanet,
		Character,
		Velocity,
		GravityAccelerationCMPerSec2,
		DeltaTime);

	bool bJumpAccepted = false;
	if (JumpBufferRemainingSeconds > 0.0f && Surface.bGrounded)
	{
		Velocity = FVector::VectorPlaneProject(Velocity, Surface.RadialUp) + Surface.RadialUp * JumpSpeedCMPerSec;
		ResolvedSuspension->NotifyJump();
		JumpBufferRemainingSeconds = 0.0f;
		Surface.bGrounded = false;
		Surface.bSupportActive = false;
		Surface.OutwardSupportAccelerationCMPerSec2 = 0.0f;
		bJumpAccepted = true;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][ForceSuspension][Jump] Accepted. Speed=%.1f Up=(%.3f,%.3f,%.3f)"),
			JumpSpeedCMPerSec,
			Surface.RadialUp.X,
			Surface.RadialUp.Y,
			Surface.RadialUp.Z);
	}
	if (!bJumpAccepted && JumpBufferRemainingSeconds > 0.0f)
	{
		const bool bWillExpire = JumpBufferRemainingSeconds <= DeltaTime;
		JumpBufferRemainingSeconds = FMath::Max(0.0f, JumpBufferRemainingSeconds - DeltaTime);
		if (bWillExpire)
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][ForceSuspension][Jump] Rejected: buffer expired while not grounded."));
		}
	}

	const FVector Input = FVector::VectorPlaneProject(PendingMoveVector, Surface.RadialUp).GetClampedToMaxSize(1.0f);
	const FVector MovementPlaneNormal = Surface.bSupportActive ? Surface.SurfaceNormal : Surface.RadialUp;
	const FVector MoveDirection = FVector::VectorPlaneProject(Input, MovementPlaneNormal).GetSafeNormal();
	const float InputMagnitude = Input.Size();
	const FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, MovementPlaneNormal);
	const float ControlScale = Surface.bSupportActive ? 1.0f : FMath::Clamp(AirControlScale, 0.0f, 1.0f);
	const float DragPerSecond = Surface.bSupportActive
		? FMath::Max(0.0f, GroundDragPerSecond)
		: FMath::Max(0.0f, AirTangentDragPerSecond);
	const float MassKG = FMath::Max(0.1f, VirtualMassKG);

	const FVector GravityForce = -Surface.RadialUp * (MassKG * GravityAccelerationCMPerSec2);
	const FVector SupportForce = Surface.RadialUp * (MassKG * Surface.OutwardSupportAccelerationCMPerSec2);
	const FVector MoveForce = MoveDirection * (MassKG * GroundMoveAccelerationCMPerSec2 * ControlScale * InputMagnitude);
	FVector DragForce = -TangentVelocity * (MassKG * DragPerSecond);
	const float TangentSpeed = TangentVelocity.Size();
	if (Surface.bSupportActive && TangentSpeed > DesignMaxGroundSpeedCMPerSec)
	{
		DragForce -= TangentVelocity.GetSafeNormal()
			* (MassKG * OverspeedDragPerSecond * (TangentSpeed - DesignMaxGroundSpeedCMPerSec));
	}

	const FVector NetForce = GravityForce + SupportForce + MoveForce + DragForce;
	Velocity += (NetForce / MassKG) * DeltaTime;
	MoveIgnoringTerrain(Character, ResolvedPlanet, Velocity * DeltaTime);
}

void UABTSRadialForceMovementComponent::MoveIgnoringTerrain(
	ACharacter& Character,
	const AABTSM2Planet& ResolvedPlanet,
	const FVector& RequestedDelta)
{
	if (RequestedDelta.IsNearlyZero()) return;
	UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	UWorld* World = GetWorld();
	if (Capsule == nullptr || World == nullptr) return;

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(),
		Capsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ABTSForceSuspensionMove), false, &Character);
	// Ignore only the continuous terrain mesh. Other Planet-owned components may
	// later become gameplay obstacles and must remain eligible for the sweep.
	QueryParams.AddIgnoredComponent(ResolvedPlanet.ContinuousSurface.Get());
	const auto SweepDelta = [&](const FVector& Start, const FVector& Delta, FHitResult& OutHit)
	{
		return World->SweepSingleByProfile(
			OutHit,
			Start,
			Start + Delta,
			Character.GetActorQuat(),
			Capsule->GetCollisionProfileName(),
			Shape,
			QueryParams);
	};

	const FVector Start = Character.GetActorLocation();
	FHitResult Hit;
	if (!SweepDelta(Start, RequestedDelta, Hit) || !Hit.bBlockingHit)
	{
		Character.SetActorLocation(Start + RequestedDelta, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	Character.SetActorLocation(Hit.Location, false, nullptr, ETeleportType::TeleportPhysics);
	ResolveBlockingHit(Hit);
	const FVector RemainingDelta = RequestedDelta * (1.0f - Hit.Time);
	const FVector SlideDelta = FVector::VectorPlaneProject(RemainingDelta, Hit.ImpactNormal);
	if (SlideDelta.IsNearlyZero()) return;

	FHitResult SlideHit;
	const FVector SlideStart = Character.GetActorLocation();
	if (!SweepDelta(SlideStart, SlideDelta, SlideHit) || !SlideHit.bBlockingHit)
	{
		Character.SetActorLocation(SlideStart + SlideDelta, false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		Character.SetActorLocation(SlideHit.Location, false, nullptr, ETeleportType::TeleportPhysics);
		ResolveBlockingHit(SlideHit);
	}
}

void UABTSRadialForceMovementComponent::ResolveBlockingHit(const FHitResult& Hit)
{
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, Normal);
	if (IntoSurfaceSpeed < 0.0f)
	{
		Velocity -= Normal * IntoSurfaceSpeed;
	}
}
