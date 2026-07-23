// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ABTSChaosBirdMovementComponent.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Planet/ABTSM2Planet.h"
#include "Player/ABTSM25BirdCharacter.h"

UABTSChaosBirdMovementComponent::UABTSChaosBirdMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UABTSChaosBirdMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteActor(GetOwner());
	if (UPrimitiveComponent* Body = ResolveBody())
	{
		Body->OnComponentHit.AddDynamic(this, &UABTSChaosBirdMovementComponent::HandlePhysicsHit);
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ChaosMovement] Ready. Gravity=%.1f MaxSpeed=%.1f Jump=%.1f CCD=DeferredUntilMode"),
		GravityAccelerationCMPerSec2, MaxGroundSpeedCMPerSec, JumpSpeedCMPerSec);
}

void UABTSChaosBirdMovementComponent::SetChaosEnabled(const bool bEnabled)
{
	bChaosEnabled = bEnabled;
	if (UPrimitiveComponent* Body = ResolveBody())
	{
		Body->SetSimulatePhysics(bEnabled);
		Body->SetEnableGravity(false);
		Body->SetUseCCD(bEnabled);
		Body->SetNotifyRigidBodyCollision(bEnabled);
		Body->SetLinearDamping(0.0f);
		Body->SetAngularDamping(20.0f);
		if (!bEnabled) Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}
	if (!bEnabled) ResetMotionState();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ChaosMovement] Enabled=%d Body=%s"), bEnabled ? 1 : 0, *GetNameSafe(ResolveBody()));
}

void UABTSChaosBirdMovementComponent::ConfigurePlanarTestMode(
	const bool bEnabled,
	const FVector& InPlaneOrigin,
	const FVector& InPlaneUp)
{
	bPlanarTestMode = bEnabled;
	PlanarOrigin = InPlaneOrigin;
	PlanarUp = InPlaneUp.GetSafeNormal();
	if (PlanarUp.IsNearlyZero()) PlanarUp = FVector::UpVector;
	if (bEnabled) Planet.Reset();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M7.1][ChaosMovement] Planar=%d Up=(%.3f,%.3f,%.3f)"),
		bEnabled ? 1 : 0, PlanarUp.X, PlanarUp.Y, PlanarUp.Z);
}

FVector UABTSChaosBirdMovementComponent::GetMovementUpAt(const FVector& WorldLocation) const
{
	if (bPlanarTestMode) return PlanarUp;
	if (Planet.IsValid()) return Planet->GetRadialUpAtWorldLocation(WorldLocation);
	return FVector::UpVector;
}

void UABTSChaosBirdMovementComponent::ConfigureCollisionGrounding(const float MaxGroundAngleDegrees)
{
	CollisionGroundMaxAngleDegrees = FMath::Clamp(MaxGroundAngleDegrees, 0.0f, 89.0f);
}

void UABTSChaosBirdMovementComponent::SetMoveInput(const FVector& Direction, const float Scale)
{
	PendingMoveVector += Direction.GetSafeNormal() * FMath::Clamp(Scale, -1.0f, 1.0f);
}

void UABTSChaosBirdMovementComponent::QueueJump()
{
	bJumpQueued = true;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ChaosMovement][Jump] Input queued. Grounded=%d"), bGrounded ? 1 : 0);
}

void UABTSChaosBirdMovementComponent::ResetMotionState()
{
	PendingMoveVector = FVector::ZeroVector;
	bJumpQueued = false;
	bGrounded = false;
	LastGroundContactAgeSeconds = BIG_NUMBER;
	if (UPrimitiveComponent* Body = ResolveBody())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void UABTSChaosBirdMovementComponent::ClearControlHandoffState()
{
	ClearControlHandoffVelocity();
	bJumpQueued = false;
}

void UABTSChaosBirdMovementComponent::ClearControlHandoffVelocity()
{
	PendingMoveVector = FVector::ZeroVector;
	if (UPrimitiveComponent* Body = ResolveBody())
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}

void UABTSChaosBirdMovementComponent::BeginBallisticFlight(const FVector& InitialVelocity, const float InAirDragPerSecond)
{
	bBallisticFlight = true;
	bGrounded = false;
	BallisticAirDragPerSecond = FMath::Max(0.0f, InAirDragPerSecond);
	PendingMoveVector = FVector::ZeroVector;
	SetVelocity(InitialVelocity);
}

void UABTSChaosBirdMovementComponent::EndBallisticFlight(const bool bResetVelocity)
{
	bBallisticFlight = false;
	if (bResetVelocity) ResetMotionState();
}

void UABTSChaosBirdMovementComponent::SetVelocity(const FVector& InVelocity)
{
	if (UPrimitiveComponent* Body = ResolveBody()) Body->SetPhysicsLinearVelocity(InVelocity);
}

FVector UABTSChaosBirdMovementComponent::GetVelocity() const
{
	if (const UPrimitiveComponent* Body = ResolveBody()) return Body->GetPhysicsLinearVelocity();
	return FVector::ZeroVector;
}

void UABTSChaosBirdMovementComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bChaosEnabled || DeltaTime <= SMALL_NUMBER) return;
	LastGroundContactAgeSeconds += DeltaTime;
	PreviousPhysicsVelocity = GetVelocity();
	ApplyRadialForces(DeltaTime);
	PendingMoveVector = FVector::ZeroVector;
}

void UABTSChaosBirdMovementComponent::ApplyRadialForces(const float DeltaTime)
{
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	UPrimitiveComponent* Body = ResolveBody();
	if ((!bPlanarTestMode && ResolvedPlanet == nullptr) || Body == nullptr || !Body->IsSimulatingPhysics()) return;
	const FVector RadialUp = bPlanarTestMode ? PlanarUp : ResolvedPlanet->GetRadialUpAtWorldLocation(Body->GetComponentLocation());
	const float Mass = FMath::Max(0.1f, Body->GetMass());
	Body->AddForce(-RadialUp * (Mass * GravityAccelerationCMPerSec2), NAME_None, false);

	if (bJumpQueued && bGrounded)
	{
		const FVector CurrentVelocity = Body->GetPhysicsLinearVelocity();
		const FVector TangentVelocity = FVector::VectorPlaneProject(CurrentVelocity, RadialUp);
		Body->SetPhysicsLinearVelocity(TangentVelocity + RadialUp * JumpSpeedCMPerSec, false);
		bGrounded = false;
		LastGroundContactAgeSeconds = BIG_NUMBER;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ChaosMovement][Jump] Accepted. Up=(%.3f,%.3f,%.3f) Speed=%.1f"), RadialUp.X, RadialUp.Y, RadialUp.Z, JumpSpeedCMPerSec);
	}
	bJumpQueued = false;
	if (bBallisticFlight)
	{
		Body->AddForce(-Body->GetPhysicsLinearVelocity() * (Mass * BallisticAirDragPerSecond), NAME_None, false);
		return;
	}

	const FVector Input = PendingMoveVector.GetClampedToMaxSize(1.0f);
	const FVector DesiredTangentVelocity = FVector::VectorPlaneProject(Input, RadialUp).GetClampedToMaxSize(1.0f)
		* MaxGroundSpeedCMPerSec;
	const FVector CurrentTangentVelocity = FVector::VectorPlaneProject(Body->GetPhysicsLinearVelocity(), RadialUp);
	const float Acceleration = Input.IsNearlyZero() ? GroundBrakingCMPerSec2 : GroundAccelerationCMPerSec2;
	const FVector DeltaVelocity = (DesiredTangentVelocity - CurrentTangentVelocity)
		.GetClampedToMaxSize(FMath::Max(0.0f, Acceleration) * DeltaTime);
	Body->AddImpulse(DeltaVelocity * Mass, NAME_None, false);
	const float AirDrag = bGrounded ? 0.0f : AirDragPerSecond;
	if (AirDrag > 0.0f) Body->AddForce(-Body->GetPhysicsLinearVelocity() * (Mass * AirDrag), NAME_None, false);
}

void UABTSChaosBirdMovementComponent::HandlePhysicsHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	TryGroundFromHit(Hit);
	if (bBallisticFlight)
	{
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		const float NormalSpeed = FMath::Max(0.0f, -FVector::DotProduct(PreviousPhysicsVelocity, Normal));
		BlockingImpact.Broadcast(Hit, NormalSpeed, PreviousPhysicsVelocity);
	}
}

void UABTSChaosBirdMovementComponent::TryGroundFromHit(const FHitResult& Hit)
{
	if (!bChaosEnabled || !Hit.bBlockingHit) return;
	AABTSM2Planet* ResolvedPlanet = FindPlanet();
	if (!bPlanarTestMode && ResolvedPlanet == nullptr) return;
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	FVector SampleLocation = GetOwner()->GetActorLocation();
	if (!Hit.ImpactPoint.IsNearlyZero()) SampleLocation = FVector(Hit.ImpactPoint);
	const FVector RadialUp = bPlanarTestMode ? PlanarUp : ResolvedPlanet->GetRadialUpAtWorldLocation(SampleLocation);
	const float Dot = FVector::DotProduct(Normal, RadialUp);
	const float MinDot = FMath::Cos(FMath::DegreesToRadians(CollisionGroundMaxAngleDegrees));
	if (Normal.IsNearlyZero() || Dot < MinDot) return;
	const bool bWasGrounded = bGrounded;
	bGrounded = true;
	LastGroundContactAgeSeconds = 0.0f;
	if (!bWasGrounded)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][ChaosMovement][Ground] Owner=%s NormalDot=%.3f Angle=%.2f Hit=%s Component=%s"),
			*GetNameSafe(GetOwner()), Dot,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f))),
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
	}
}

AABTSM2Planet* UABTSChaosBirdMovementComponent::FindPlanet()
{
	if (bPlanarTestMode) return nullptr;
	if (Planet.IsValid() && Planet->IsPlanetReady()) return Planet.Get();
	for (TActorIterator<AABTSM2Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady()) { Planet = *It; return Planet.Get(); }
	}
	return nullptr;
}

UPrimitiveComponent* UABTSChaosBirdMovementComponent::ResolveBody() const
{
	const AABTSM25BirdCharacter* Bird = Cast<AABTSM25BirdCharacter>(GetOwner());
	return Bird ? Bird->GetChaosPhysicsBody() : nullptr;
}
