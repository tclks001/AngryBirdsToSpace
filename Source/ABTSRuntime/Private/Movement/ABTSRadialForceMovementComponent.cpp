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
#include "Terrain/ABTSM3Planet.h"
#include "Player/ABTSM25BirdCharacter.h"
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
		TEXT("[ABTS][ForceSuspension] Movement ready. Mass=%.1f Gravity=%.1f MoveAccel=%.1f GroundDrag=%.2f IdleBrake=%.2f AirDrag=%.2f TerminalSpeed=%.1f Step=%.5f"),
		VirtualMassKG,
		GravityAccelerationCMPerSec2,
		GroundMoveAccelerationCMPerSec2,
		GroundDragPerSecond,
		GroundIdleBrakePerSecond,
		AirDragPerSecond,
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

void UABTSRadialForceMovementComponent::ConfigureCollisionGroundingExperiment(
	const bool bEnabled,
	const float MaxGroundAngleDegrees)
{
	bUseCollisionNormalGroundingExperiment = bEnabled;
	CollisionGroundMaxAngleDegrees = FMath::Clamp(MaxGroundAngleDegrees, 0.0f, 89.0f);
	bCollisionGrounded = false;
}

void UABTSRadialForceMovementComponent::ResetMotionState()
{
	Velocity = FVector::ZeroVector;
	PendingMoveVector = FVector::ZeroVector;
	JumpBufferRemainingSeconds = 0.0f;
	ControlHandoffJumpGraceRemainingSeconds = 0.0f;
	bCollisionGrounded = false;
	if (UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension())
	{
		ResolvedSuspension->ResetSuspensionState();
	}
}

void UABTSRadialForceMovementComponent::ClearControlHandoffState()
{
	ClearControlHandoffVelocity();
	JumpBufferRemainingSeconds = 0.0f;
	bBallisticFlight = false;
}

void UABTSRadialForceMovementComponent::ClearControlHandoffInput()
{
	PendingMoveVector = FVector::ZeroVector;
	JumpBufferRemainingSeconds = 0.0f;
	ControlHandoffJumpGraceRemainingSeconds = 0.0f;
	bBallisticFlight = false;
}

void UABTSRadialForceMovementComponent::ClearControlHandoffVelocity()
{
	Velocity = FVector::ZeroVector;
	PendingMoveVector = FVector::ZeroVector;
}

bool UABTSRadialForceMovementComponent::StabilizeForGroundedControlHandoff()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension();
	if (Character == nullptr || ResolvedPlanet == nullptr || ResolvedSuspension == nullptr) return false;
	if (bUseCollisionNormalGroundingExperiment)
	{
		ClearControlHandoffState();
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][GroundCollisionExperiment] Handoff preserves collision ground only. Owner=%s Grounded=%d"),
			*GetNameSafe(GetOwner()), bCollisionGrounded ? 1 : 0);
		return bCollisionGrounded;
	}

	const FVector OriginalVelocity = Velocity;
	const FABTSRadialSuspensionSample Before = ResolvedSuspension->Evaluate(
		*ResolvedPlanet,
		*Character,
		Velocity,
		GravityAccelerationCMPerSec2,
		0.0f);
	const bool bNeedsSurfaceAlignment = !Before.bGrounded;
	ClearControlHandoffState();
	if (bNeedsSurfaceAlignment)
	{
		const FVector Center = ResolvedPlanet->GetPlanetCenterWorld();
		Character->SetActorLocation(
			Center + Before.RadialUp * Before.DesiredCenterRadiusCM,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}
	ResolvedSuspension->ResetSuspensionState();
	const FABTSRadialSuspensionSample After = ResolvedSuspension->Evaluate(
		*ResolvedPlanet,
		*Character,
		Velocity,
		GravityAccelerationCMPerSec2,
		0.0f);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M4][HandoffGround] Snapped=%d BeforeGround=%d BeforeHeight=%.2f BeforeSpeed=%.2f AfterGround=%d AfterHeight=%.2f"),
		bNeedsSurfaceAlignment ? 1 : 0,
		Before.bGrounded ? 1 : 0,
		Before.HeightAboveTargetCM,
		FVector::DotProduct(OriginalVelocity, Before.RadialUp),
		After.bGrounded ? 1 : 0,
		After.HeightAboveTargetCM);
	return After.bGrounded;
}

void UABTSRadialForceMovementComponent::GrantControlHandoffJumpGrace(const float Seconds)
{
	ControlHandoffJumpGraceRemainingSeconds = FMath::Max(ControlHandoffJumpGraceRemainingSeconds, FMath::Max(0.0f, Seconds));
}

void UABTSRadialForceMovementComponent::BeginBallisticFlight(
	const FVector& InitialVelocity,
	const float InFlightAirDragPerSecond)
{
	bBallisticFlight = true;
	bCollisionGrounded = false;
	BallisticFlightAirDragPerSecond = FMath::Max(0.0f, InFlightAirDragPerSecond);
	Velocity = InitialVelocity;
	PendingMoveVector = FVector::ZeroVector;
	JumpBufferRemainingSeconds = 0.0f;
	if (UABTSRadialSurfaceSuspensionComponent* ResolvedSuspension = FindSuspension()) ResolvedSuspension->ResetSuspensionState();
}

void UABTSRadialForceMovementComponent::EndBallisticFlight(const bool bResetVelocity)
{
	bBallisticFlight = false;
	if (bResetVelocity) ResetMotionState();
}

bool UABTSRadialForceMovementComponent::IsGrounded() const
{
	if (bUseCollisionNormalGroundingExperiment) return bCollisionGrounded;
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

	const float CurrentRadiusCM = FVector::Distance(Character.GetActorLocation(), ResolvedPlanet.GetPlanetCenterWorld());
	const float ReferenceRadiusCM = FMath::Max(ResolvedPlanet.GetPlanetRadiusCM(), 1.0f);
	const float LocalGravityAcceleration = bBallisticFlight
		? GravityAccelerationCMPerSec2 * FMath::Square(ReferenceRadiusCM / FMath::Max(CurrentRadiusCM, 1.0f))
		: GravityAccelerationCMPerSec2;
	FABTSRadialSuspensionSample Surface = ResolvedSuspension->Evaluate(
		ResolvedPlanet,
		Character,
		Velocity,
		LocalGravityAcceleration,
		DeltaTime);
	if (bUseCollisionNormalGroundingExperiment) Surface.bGrounded = bCollisionGrounded;
	if (EnsureGroundClearance(Character, ResolvedPlanet, Surface))
	{
		// Refresh immediately after a spawn/penetration correction; stale support
		// data would otherwise apply one substep of force from the old radius.
		Surface = ResolvedSuspension->Evaluate(
			ResolvedPlanet,
			Character,
			Velocity,
			LocalGravityAcceleration,
			DeltaTime);
		if (bUseCollisionNormalGroundingExperiment) Surface.bGrounded = bCollisionGrounded;
	}

	bool bJumpAccepted = false;
	const bool bMayJump = bUseCollisionNormalGroundingExperiment
		? Surface.bGrounded
		: Surface.bGrounded || ControlHandoffJumpGraceRemainingSeconds > 0.0f;
	if (JumpBufferRemainingSeconds > 0.0f && bMayJump)
	{
		Velocity = FVector::VectorPlaneProject(Velocity, Surface.RadialUp) + Surface.RadialUp * JumpSpeedCMPerSec;
		ResolvedSuspension->NotifyJump();
		JumpBufferRemainingSeconds = 0.0f;
		Surface.bGrounded = false;
		Surface.bSupportActive = false;
		Surface.OutwardSupportAccelerationCMPerSec2 = 0.0f;
		bCollisionGrounded = false;
		bJumpAccepted = true;
		if (bUseCollisionNormalGroundingExperiment)
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][GroundCollisionExperiment] AirborneByJump Owner=%s; waiting for next qualifying blocking hit."),
				*GetNameSafe(GetOwner()));
		}
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
	ControlHandoffJumpGraceRemainingSeconds = FMath::Max(0.0f, ControlHandoffJumpGraceRemainingSeconds - DeltaTime);

	const FVector Input = bBallisticFlight
		? FVector::ZeroVector
		: FVector::VectorPlaneProject(PendingMoveVector, Surface.RadialUp).GetClampedToMaxSize(1.0f);
	// Support may begin slightly above the ground to damp a landing. It is not a
	// collision contact, so it must not grant ground control or ground friction.
	const FVector MovementPlaneNormal = Surface.bGrounded ? Surface.SurfaceNormal : Surface.RadialUp;
	const FVector MoveDirection = FVector::VectorPlaneProject(Input, MovementPlaneNormal).GetSafeNormal();
	const float InputMagnitude = Input.Size();
	const FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, MovementPlaneNormal);
	const float ControlScale = Surface.bGrounded ? 1.0f : FMath::Clamp(AirControlScale, 0.0f, 1.0f);
	float DragPerSecond = Surface.bGrounded
		? FMath::Max(0.0f, GroundDragPerSecond)
		: FMath::Max(0.0f, AirTangentDragPerSecond);
	if (Surface.bGrounded)
	{
		if (const AABTSM3Planet* M3Planet = Cast<AABTSM3Planet>(&ResolvedPlanet))
		{
			FABTSM3SurfacePhysicsSample PhysicsSample;
			const FVector Direction = (Character.GetActorLocation() - ResolvedPlanet.GetPlanetCenterWorld()).GetSafeNormal();
			if (M3Planet->QuerySurfacePhysics(Direction, PhysicsSample)) DragPerSecond = PhysicsSample.GroundDragPerSecond;
		}
		if (InputMagnitude <= 0.01f) DragPerSecond = FMath::Max(DragPerSecond, GroundIdleBrakePerSecond);
	}
	const float MassKG = FMath::Max(0.1f, VirtualMassKG);

	const FVector GravityForce = -Surface.RadialUp * (MassKG * LocalGravityAcceleration);
	const FVector SupportForce = Surface.RadialUp * (MassKG * Surface.OutwardSupportAccelerationCMPerSec2);
	const FVector MoveForce = MoveDirection * (MassKG * GroundMoveAccelerationCMPerSec2 * ControlScale * InputMagnitude);
	FVector DragForce = -TangentVelocity * (MassKG * DragPerSecond);
	const float TangentSpeed = TangentVelocity.Size();
	if (Surface.bGrounded && TangentSpeed > DesignMaxGroundSpeedCMPerSec)
	{
		DragForce -= TangentVelocity.GetSafeNormal()
			* (MassKG * OverspeedDragPerSecond * (TangentSpeed - DesignMaxGroundSpeedCMPerSec));
	}

	const float ResolvedAirDrag = bBallisticFlight ? BallisticFlightAirDragPerSecond : AirDragPerSecond;
	const FVector AirDragForce = -Velocity * (MassKG * FMath::Max(0.0f, ResolvedAirDrag));
	const FVector NetForce = GravityForce + SupportForce + MoveForce + DragForce + AirDragForce;
	Velocity += (NetForce / MassKG) * DeltaTime;
	if (Surface.bGrounded)
	{
		// BuildGroundFollowingDelta owns radial ground tracking. Keeping a second
		// radial velocity state lets spring overshoot alternate Grounded/Airborne
		// and generates false M4 jump events. Ground velocity is therefore purely
		// tangential; slopes are followed by the CellTopo-derived target radius.
		Velocity = FVector::VectorPlaneProject(Velocity, Surface.RadialUp);
	}
	const FVector RequestedDelta = BuildGroundFollowingDelta(Character, ResolvedPlanet, Surface, Velocity * DeltaTime);
	MoveWithCollision(Character, ResolvedPlanet, RequestedDelta);
}

bool UABTSRadialForceMovementComponent::EnsureGroundClearance(
	ACharacter& Character,
	const AABTSM2Planet& ResolvedPlanet,
	const FABTSRadialSuspensionSample& Surface)
{
	if (!Surface.bSupportActive || Surface.HeightAboveTargetCM >= -KINDA_SMALL_NUMBER) return false;
	const FVector Center = ResolvedPlanet.GetPlanetCenterWorld();
	const FVector Up = ResolvedPlanet.GetRadialUpAtWorldLocation(Character.GetActorLocation());
	const float InwardRadialSpeed = FVector::DotProduct(Velocity, Up);
	if (InwardRadialSpeed < 0.0f)
	{
		// Position depenetration and velocity resolution are one contact operation.
		// Keeping an inward velocity after teleporting to the support radius made
		// the next substep penetrate again and drove the spring to +/-MaxSupport.
		Velocity -= Up * InwardRadialSpeed;
	}
	Character.SetActorLocation(Center + Up * Surface.DesiredCenterRadiusCM, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogABTSRuntime, VeryVerbose,
		TEXT("[ABTS][M5.2][GroundContact] Depenetrated=%.2f RemovedInwardSpeed=%.2f"),
		-Surface.HeightAboveTargetCM,
		FMath::Max(0.0f, -InwardRadialSpeed));
	return true;
}

FVector UABTSRadialForceMovementComponent::BuildGroundFollowingDelta(
	const ACharacter& Character,
	const AABTSM2Planet& ResolvedPlanet,
	const FABTSRadialSuspensionSample& Surface,
	const FVector& RequestedDelta) const
{
	if (!Surface.bGrounded
		|| Surface.RadialSpeedCMPerSec < -FMath::Max(0.0f, MaxGroundFollowDescentSpeedCMPerSec)
		|| RequestedDelta.IsNearlyZero()) return RequestedDelta;
	const FVector Center = ResolvedPlanet.GetPlanetCenterWorld();
	const FVector Start = Character.GetActorLocation();
	const FVector PredictedUp = ResolvedPlanet.GetRadialUpAtWorldLocation(Start + RequestedDelta);
	const FVector PredictedNormal = ResolvedPlanet.GetSurfaceNormalAtDirection(PredictedUp).GetSafeNormal();
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	if (Capsule == nullptr || PredictedNormal.IsNearlyZero()) return RequestedDelta;
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CylinderHalfHeight = FMath::Max(0.0f, Capsule->GetScaledCapsuleHalfHeight() - CapsuleRadius);
	const float NormalUpDot = FMath::Max(FVector::DotProduct(PredictedNormal, PredictedUp), Surface.MinimumGroundNormalUpDot);
	const float SupportOffset = CylinderHalfHeight + CapsuleRadius / NormalUpDot;
	const float TargetRadius = ResolvedPlanet.GetSurfaceRadiusAtDirection(PredictedUp) + SupportOffset + Surface.GroundClearanceCM;
	return Center + PredictedUp * TargetRadius - Start;
}

void UABTSRadialForceMovementComponent::MoveWithCollision(
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
	// The continuous surface is deliberately included.  Contact is resolved by
	// the same capsule sweep that handles HISM and buildings; radial suspension
	// is now only a near-ground stabilizer rather than a penetration recovery.
	// Party members are locomotion peers, never obstacles. Explicitly ignore all
	// bird actors as a second line of defence so auxiliary collision components
	// on a future bird model cannot reintroduce a deadlock.
	for (TActorIterator<AABTSM25BirdCharacter> It(World); It; ++It)
	{
		if (*It != &Character) QueryParams.AddIgnoredActor(*It);
	}
	const ECollisionChannel MovingObjectChannel = Capsule->GetCollisionObjectType();
	const FCollisionResponseParams MovingResponseParams(Capsule->GetCollisionResponseToChannels());
	const auto SweepDelta = [&](const FVector& Start, const FVector& Delta, FHitResult& OutHit)
	{
		// Do not query by profile name here. A profile lookup rebuilds the default
		// Pawn responses and discards per-instance overrides such as the M4 party's
		// Pawn=Ignore setting, causing touching birds to block each other anyway.
		return World->SweepSingleByChannel(
			OutHit,
			Start,
			Start + Delta,
			Character.GetActorQuat(),
			MovingObjectChannel,
			Shape,
			QueryParams,
			MovingResponseParams);
	};

	FVector Start = Character.GetActorLocation();
	FHitResult Hit;
	bool bBlockingHit = SweepDelta(Start, RequestedDelta, Hit) && Hit.bBlockingHit;
	if (bBlockingHit && Hit.bStartPenetrating)
	{
		// A complex procedural triangle can report a zero-time block after an
		// externally teleported spawn or a streaming rebuild. Never leave the
		// kinematic bird permanently at Time=0: move it out by the reported
		// penetration plus a small skin, then repeat the actual requested sweep.
		FVector DepenetrationNormal = Hit.Normal.GetSafeNormal();
		if (DepenetrationNormal.IsNearlyZero()) DepenetrationNormal = ResolvedPlanet.GetRadialUpAtWorldLocation(Start);
		const float DepenetrationCM = FMath::Clamp(Hit.PenetrationDepth + 2.0f, 2.0f, 120.0f);
		Start += DepenetrationNormal * DepenetrationCM;
		Character.SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		Hit.Reset();
		bBlockingHit = SweepDelta(Start, RequestedDelta, Hit) && Hit.bBlockingHit;
		UE_LOG(LogABTSRuntime, Verbose,
			TEXT("[ABTS][M5.2][GroundContact] Recovered zero-time sweep penetration by %.2fcm."),
			DepenetrationCM);
	}
	if (!bBlockingHit)
	{
		Character.SetActorLocation(Start + RequestedDelta, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	Character.SetActorLocation(Hit.Location, false, nullptr, ETeleportType::TeleportPhysics);
	ResolveBlockingHit(Hit, ResolvedPlanet);
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
		ResolveBlockingHit(SlideHit, ResolvedPlanet);
	}
}

void UABTSRadialForceMovementComponent::ResolveBlockingHit(const FHitResult& Hit, const AABTSM2Planet& ResolvedPlanet)
{
	TryEstablishCollisionGround(Hit, ResolvedPlanet);
	const FVector IncomingVelocity = Velocity;
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, Normal);
	if (IntoSurfaceSpeed < 0.0f)
	{
		Velocity -= Normal * IntoSurfaceSpeed;
		if (const AABTSM3Planet* M3Planet = Cast<AABTSM3Planet>(&ResolvedPlanet))
		{
			FABTSM3SurfacePhysicsSample PhysicsSample;
			const FVector Direction = (Hit.ImpactPoint - ResolvedPlanet.GetPlanetCenterWorld()).GetSafeNormal();
			if (M3Planet->QuerySurfacePhysics(Direction, PhysicsSample)
				&& -IntoSurfaceSpeed >= BounceSpeedThresholdCMPerSec)
			{
				Velocity += Normal * (-IntoSurfaceSpeed * PhysicsSample.Restitution);
			}
		}
	}
	if (bBallisticFlight)
	{
		BlockingImpact.Broadcast(Hit, FMath::Max(0.0f, -FVector::DotProduct(IncomingVelocity, Normal)), IncomingVelocity);
	}
}

void UABTSRadialForceMovementComponent::TryEstablishCollisionGround(
	const FHitResult& Hit,
	const AABTSM2Planet& ResolvedPlanet)
{
	if (!bUseCollisionNormalGroundingExperiment || !Hit.bBlockingHit) return;
	const FVector CollisionNormal = Hit.ImpactNormal.GetSafeNormal();
	FVector SampleLocation = GetOwner()->GetActorLocation();
	if (!Hit.ImpactPoint.IsNearlyZero()) SampleLocation = FVector(Hit.ImpactPoint);
	const FVector RadialUp = ResolvedPlanet.GetRadialUpAtWorldLocation(SampleLocation);
	const float NormalUpDot = FVector::DotProduct(CollisionNormal, RadialUp);
	const float MinimumGroundDot = FMath::Cos(FMath::DegreesToRadians(CollisionGroundMaxAngleDegrees));
	if (CollisionNormal.IsNearlyZero() || NormalUpDot < MinimumGroundDot) return;

	const bool bWasGrounded = bCollisionGrounded;
	bCollisionGrounded = true;
	if (!bWasGrounded)
	{
		const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(NormalUpDot, -1.0f, 1.0f)));
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][GroundCollisionExperiment] Grounded Owner=%s HitActor=%s HitComponent=%s Angle=%.2f MaxAngle=%.2f Normal=(%.3f,%.3f,%.3f) RadialUp=(%.3f,%.3f,%.3f)"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			AngleDegrees, CollisionGroundMaxAngleDegrees,
			CollisionNormal.X, CollisionNormal.Y, CollisionNormal.Z,
			RadialUp.X, RadialUp.Y, RadialUp.Z);
	}
}
