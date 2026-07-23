// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ABTSM25RadialMovementComponent.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Planet/ABTSM2Planet.h"

UABTSM25RadialMovementComponent::UABTSM25RadialMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UABTSM25RadialMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	AddTickPrerequisiteActor(GetOwner());
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5][Jump] Movement ready. Gravity=%.1f JumpSpeed=%.1f Buffer=%.3f Snap=%.1f Detach=%.1f SnapSpeed=%.1f Unground=%.1f"),
		GravityAccelerationCMPerSec2,
		JumpSpeedCMPerSec,
		JumpBufferSeconds,
		GroundSnapToleranceCM,
		GroundDetachToleranceCM,
		GroundSnapSpeedCMPerSec,
		UngroundSpeedCMPerSec);
}

void UABTSM25RadialMovementComponent::SetMoveInput(const FVector& Direction, const float Scale)
{
	PendingMoveVector += Direction.GetSafeNormal() * FMath::Clamp(Scale, -1.0f, 1.0f);
}

void UABTSM25RadialMovementComponent::QueueJump()
{
	// Even when designers set buffering to zero, the press must survive until
	// this component's next tick so a currently grounded jump still works.
	JumpBufferRemainingSeconds = FMath::Max(JumpBufferSeconds, KINDA_SMALL_NUMBER);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5][Jump] Input queued. Grounded=%d Buffer=%.3f"),
		bGrounded ? 1 : 0,
		JumpBufferRemainingSeconds);
}

void UABTSM25RadialMovementComponent::ResetMotionState()
{
	Velocity = FVector::ZeroVector;
	PendingMoveVector = FVector::ZeroVector;
	JumpBufferRemainingSeconds = 0.0f;
	ControlHandoffJumpGraceRemainingSeconds = 0.0f;
	bGrounded = false;
}

void UABTSM25RadialMovementComponent::ClearControlHandoffState()
{
	ClearControlHandoffVelocity();
	JumpBufferRemainingSeconds = 0.0f;
}

void UABTSM25RadialMovementComponent::ClearControlHandoffVelocity()
{
	Velocity = FVector::ZeroVector;
	PendingMoveVector = FVector::ZeroVector;
}

void UABTSM25RadialMovementComponent::GrantControlHandoffJumpGrace(const float Seconds)
{
	ControlHandoffJumpGraceRemainingSeconds = FMath::Max(ControlHandoffJumpGraceRemainingSeconds, FMath::Max(0.0f, Seconds));
}

void UABTSM25RadialMovementComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (FindPlanet() == nullptr || DeltaTime <= SMALL_NUMBER)
	{
		if (DeltaTime > SMALL_NUMBER && !bLoggedNoReadyPlanet)
		{
			UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M2.5][Jump] Movement tick skipped: no ready planet."));
			bLoggedNoReadyPlanet = true;
		}
		return;
	}
	bLoggedNoReadyPlanet = false;

	IntegrateMotion(DeltaTime);
	PendingMoveVector = FVector::ZeroVector;
}

AABTSM2Planet* UABTSM25RadialMovementComponent::FindPlanet()
{
	if (Planet.IsValid())
	{
		return Planet.Get();
	}

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

void UABTSM25RadialMovementComponent::IntegrateMotion(const float DeltaTime)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AABTSM2Planet* ResolvedPlanet = Planet.Get();
	if (Character == nullptr || ResolvedPlanet == nullptr)
	{
		return;
	}

	const FVector Up = ResolvedPlanet->GetRadialUpAtWorldLocation(Character->GetActorLocation());

	// Refresh contact before consuming input. Reading only last frame's cached
	// state makes a jump press dependent on tick order and transient contact loss.
	ResolveBaseSphereContact(DeltaTime * 0.5f);
	if (JumpBufferRemainingSeconds > 0.0f && (bGrounded || ControlHandoffJumpGraceRemainingSeconds > 0.0f))
	{
		Velocity = FVector::VectorPlaneProject(Velocity, Up) + Up * JumpSpeedCMPerSec;
		bGrounded = false;
		JumpBufferRemainingSeconds = 0.0f;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5][Jump] Accepted. Speed=%.1f Up=(%.3f,%.3f,%.3f)"),
			JumpSpeedCMPerSec,
			Up.X,
			Up.Y,
			Up.Z);
	}
	else
	{
		const bool bBufferWillExpire = JumpBufferRemainingSeconds > 0.0f && JumpBufferRemainingSeconds <= DeltaTime;
		JumpBufferRemainingSeconds = FMath::Max(0.0f, JumpBufferRemainingSeconds - DeltaTime);
		if (bBufferWillExpire)
		{
			UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M2.5][Jump] Rejected: buffer expired while not grounded."));
		}
	}
	ControlHandoffJumpGraceRemainingSeconds = FMath::Max(0.0f, ControlHandoffJumpGraceRemainingSeconds - DeltaTime);

	const FVector TangentInput = FVector::VectorPlaneProject(PendingMoveVector, Up).GetClampedToMaxSize(1.0f);
	if (bGrounded)
	{
		// Radial tangent input is horizontal only on the base sphere. On M3 terrain it
		// must be lifted into the smooth surface tangent plane; otherwise the capsule
		// repeatedly drives into an uphill triangle and loses travel to the sweep.
		FVector SurfaceNormal = ResolvedPlanet->GetSurfaceNormalAtDirection(Up).GetSafeNormal();
		if (SurfaceNormal.IsNearlyZero() || FVector::DotProduct(SurfaceNormal, Up) < 0.0f)
		{
			SurfaceNormal = Up;
		}
		const float InputMagnitude = TangentInput.Size();
		const FVector SurfaceMoveDirection = FVector::VectorPlaneProject(TangentInput, SurfaceNormal).GetSafeNormal();
		const FVector TargetGroundVelocity = SurfaceMoveDirection * (InputMagnitude * MaxGroundSpeedCMPerSec);
		const float ChangeRate = TangentInput.IsNearlyZero()
			? GroundBrakingCMPerSec2
			: GroundAccelerationCMPerSec2;
		Velocity = FMath::VInterpConstantTo(
			FVector::VectorPlaneProject(Velocity, SurfaceNormal),
			TargetGroundVelocity,
			DeltaTime,
			ChangeRate);
		if (Velocity.SizeSquared() > FMath::Square(MaxGroundSpeedCMPerSec))
		{
			Velocity = Velocity.GetSafeNormal() * MaxGroundSpeedCMPerSec;
		}
	}
	else
	{
		Velocity += TangentInput * GroundAccelerationCMPerSec2 * AirControlScale * DeltaTime;
		const FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, Up);
		if (TangentVelocity.SizeSquared() > FMath::Square(MaxGroundSpeedCMPerSec))
		{
			Velocity += TangentVelocity.GetSafeNormal() * (MaxGroundSpeedCMPerSec - TangentVelocity.Size());
		}
		Velocity -= Up * GravityAccelerationCMPerSec2 * DeltaTime;
	}

	const FVector RequestedDelta = Velocity * DeltaTime;
	FHitResult Hit;
	Character->AddActorWorldOffset(RequestedDelta, true, &Hit, ETeleportType::None);
	if (Hit.bBlockingHit)
	{
		ResolveBlockingHit(Hit);

		// A sweep stops at first contact. Preserve the unconsumed tangential travel
		// by sliding it over the blocking surface instead of losing it every frame.
		const FVector RemainingDelta = RequestedDelta * (1.0f - Hit.Time);
		const FVector SlideDelta = FVector::VectorPlaneProject(RemainingDelta, Hit.ImpactNormal);
		if (!SlideDelta.IsNearlyZero())
		{
			FHitResult SlideHit;
			Character->AddActorWorldOffset(SlideDelta, true, &SlideHit, ETeleportType::None);
			if (SlideHit.bBlockingHit)
			{
				ResolveBlockingHit(SlideHit);
			}
		}
	}
	ResolveBaseSphereContact(DeltaTime * 0.5f);
}

float UABTSM25RadialMovementComponent::GetGroundCenterOffsetCM(
	const ACharacter& Character,
	const FVector& RadialUp,
	const FVector& SurfaceNormal) const
{
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CylinderHalfHeight = FMath::Max(0.0f, CapsuleHalfHeight - CapsuleRadius);
	const float NormalUpDot = FMath::Max(FVector::DotProduct(SurfaceNormal, RadialUp), MinimumGroundNormalUpDot);

	// A capsule on a tilted plane needs more radial clearance than HalfHeight.
	// Support along N is CylinderHalfHeight * dot(N, Up) + Radius; converting it
	// back to a radial center offset gives CylinderHalfHeight + Radius / dot.
	return CylinderHalfHeight + CapsuleRadius / NormalUpDot;
}

void UABTSM25RadialMovementComponent::ResolveBaseSphereContact(const float DeltaTime)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	AABTSM2Planet* ResolvedPlanet = Planet.Get();
	if (Character == nullptr || ResolvedPlanet == nullptr)
	{
		return;
	}

	const FVector Center = ResolvedPlanet->GetPlanetCenterWorld();
	const FVector Location = Character->GetActorLocation();
	const FVector Up = ResolvedPlanet->GetRadialUpAtWorldLocation(Location);
	FVector SurfaceNormal = ResolvedPlanet->GetSurfaceNormalAtDirection(Up).GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero() || FVector::DotProduct(SurfaceNormal, Up) < 0.0f)
	{
		SurfaceNormal = Up;
	}
	const float SurfaceNormalUpDot = FVector::DotProduct(SurfaceNormal, Up);
	const float DesiredRadius = ResolvedPlanet->GetSurfaceRadiusAtDirection(Up)
		+ GetGroundCenterOffsetCM(*Character, Up, SurfaceNormal);
	const float CurrentRadius = FVector::Distance(Location, Center);
	const float RadialSpeed = FVector::DotProduct(Velocity, Up);

	const bool bWasGrounded = bGrounded;
	const float ContactToleranceCM = bWasGrounded ? GroundDetachToleranceCM : GroundSnapToleranceCM;
	const bool bAtBaseSphere = CurrentRadius <= DesiredRadius + ContactToleranceCM;
	// Ordinary uphill velocity also has an outward radial component. It must not
	// detach an existing ground contact; only an already-airborne launch uses this
	// speed gate (the jump path explicitly clears bGrounded first).
	const bool bExplicitlyLaunching = !bWasGrounded && RadialSpeed > UngroundSpeedCMPerSec;
	bGrounded = bAtBaseSphere && !bExplicitlyLaunching;
	if (bWasGrounded != bGrounded)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5][Ground] %s. Radius=%.2f Desired=%.2f Delta=%.2f RadialSpeed=%.2f NormalUp=%.3f Tolerance=%.1f AtSphere=%d Launching=%d"),
			bGrounded ? TEXT("Grounded") : TEXT("Airborne"),
			CurrentRadius,
			DesiredRadius,
			CurrentRadius - DesiredRadius,
			RadialSpeed,
			SurfaceNormalUpDot,
			ContactToleranceCM,
			bAtBaseSphere ? 1 : 0,
			bExplicitlyLaunching ? 1 : 0);
	}
	if (bGrounded)
	{
		const float CorrectedRadius = FMath::FInterpConstantTo(
			CurrentRadius,
			DesiredRadius,
			FMath::Max(DeltaTime, 0.0f),
			GroundSnapSpeedCMPerSec);
		Character->SetActorLocation(Center + Up * CorrectedRadius, false, nullptr, ETeleportType::TeleportPhysics);
		// Keep the velocity along the actual smooth terrain. Projecting it onto the
		// radial plane would erase the climb component and recreate the impact/snap
		// cycle on every uphill frame.
		Velocity = FVector::VectorPlaneProject(Velocity, SurfaceNormal);
	}
}

void UABTSM25RadialMovementComponent::ResolveBlockingHit(const FHitResult& Hit)
{
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, Normal);
	if (IntoSurfaceSpeed < 0.0f)
	{
		Velocity -= Normal * IntoSurfaceSpeed;
	}
}
