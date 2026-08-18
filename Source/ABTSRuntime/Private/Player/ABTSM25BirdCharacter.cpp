// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM25BirdCharacter.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Movement/ABTSMovementModeSelector.h"
#include "Movement/ABTSChaosBirdMovementComponent.h"
#include "Movement/ABTSM25RadialMovementComponent.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSRadialSurfaceSuspensionComponent.h"
#include "Planet/ABTSM2SphericalSurfaceComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Presentation/ABTSBirdAnimationPresentationComponent.h"
#include "Player/ABTSM4PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"

namespace
{
	constexpr float M25DefaultGroundMovementSpeedMultiplier = 2.0f;
}

AABTSM25BirdCharacter::AABTSM25BirdCharacter()
{
	RadialMovement = CreateDefaultSubobject<UABTSM25RadialMovementComponent>(TEXT("RadialMovement"));
	ForceMovement = CreateDefaultSubobject<UABTSRadialForceMovementComponent>(TEXT("ForceMovement"));
	SurfaceSuspension = CreateDefaultSubobject<UABTSRadialSurfaceSuspensionComponent>(TEXT("SurfaceSuspension"));
	ChaosMovement = CreateDefaultSubobject<UABTSChaosBirdMovementComponent>(TEXT("ChaosMovement"));
	ChaosPhysicsSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChaosPhysicsSphere"));
	ChaosPhysicsSphere->SetupAttachment(GetCapsuleComponent());
	ChaosPhysicsSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChaosPhysicsSphere->SetVisibility(false);
	ChaosPhysicsSphere->SetHiddenInGame(true);
	ChaosPhysicsSphere->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChaosSphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (ChaosSphereMesh.Succeeded()) ChaosPhysicsSphere->SetStaticMesh(ChaosSphereMesh.Object);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_3.M_CuteBird_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlueFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_3.M_Dino_face_3"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_10.M_CuteBird_10"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> YellowFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_6.M_Dino_face_6"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackColor(TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_16.M_CuteBird_16"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BlackFace(TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_17.M_Dino_face_17"));
	CuteBirdColorMaterials[0] = RedColor.Object;
	CuteBirdFaceMaterials[0] = RedFace.Object;
	CuteBirdColorMaterials[1] = BlueColor.Object;
	CuteBirdFaceMaterials[1] = BlueFace.Object;
	CuteBirdColorMaterials[2] = YellowColor.Object;
	CuteBirdFaceMaterials[2] = YellowFace.Object;
	CuteBirdColorMaterials[3] = BlackColor.Object;
	CuteBirdFaceMaterials[3] = BlackFace.Object;
}

void AABTSM25BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->DisableMovement();
	GetSphericalSurface()->SetProjectToBaseSurface(false);
	ConfigureMovementMode();
	BirdAnimationPresentation = NewObject<UABTSBirdAnimationPresentationComponent>(this, TEXT("BirdAnimationPresentation"));
	if (BirdAnimationPresentation)
	{
		BirdAnimationPresentation->RegisterComponent();
		BirdAnimationPresentation->InitializePresentation(GetBirdVisual(), IsRadiallyGrounded());
	}
	ApplyCuteBirdMaterials();
	UpdateBirdAnimationPresentation(0.0f);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SlingshotImpactFacingLockRemainingSeconds = FMath::Max(
		0.0f,
		SlingshotImpactFacingLockRemainingSeconds - DeltaSeconds);
	if (bSlingshotPresentationUpActive && IsSlingshotFlightActive())
	{
		UpdateSlingshotPresentationFrame(DeltaSeconds);
	}
	else if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		UpdateChaosVisualFrame(DeltaSeconds);
	}
	UpdateBirdAnimationPresentation(DeltaSeconds);
	UpdateBirdLocomotionAudio(DeltaSeconds);
	if (ControlDiagnosticRemainingSeconds <= 0.0f) return;
	ControlDiagnosticRemainingSeconds = FMath::Max(0.0f, ControlDiagnosticRemainingSeconds - DeltaSeconds);
	ControlDiagnosticLogAccumulator += DeltaSeconds;
	if (ControlDiagnosticLogAccumulator >= 0.1f || ControlDiagnosticRemainingSeconds <= 0.0f)
	{
		ControlDiagnosticLogAccumulator = 0.0f;
		LogControlDiagnosticSnapshot();
	}
}

void AABTSM25BirdCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Possessed] Bird=%d Controller=%s Pawn=%s Flag=%d"),
		ABTSBirdIdToIndex(BirdId), *GetNameSafe(NewController), *GetNameSafe(NewController ? NewController->GetPawn() : nullptr), bPartyControlled ? 1 : 0);
}

void AABTSM25BirdCharacter::UnPossessed()
{
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][UnPossessed] Bird=%d Controller=%s Flag=%d"),
		ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0);
	Super::UnPossessed();
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::ConfigureMovementMode()
{
	RadialMovement->SetGameplayWalkingSpeedMultiplier(
		M25DefaultGroundMovementSpeedMultiplier);
	ForceMovement->SetGameplayWalkingSpeedMultiplier(
		M25DefaultGroundMovementSpeedMultiplier);
	ChaosMovement->SetGameplayWalkingSpeedMultiplier(
		M25DefaultGroundMovementSpeedMultiplier);
	bool bUseCollisionGroundingExperiment = false;
	float CollisionGroundMaxAngleDegrees = 55.0f;
	for (TActorIterator<AABTSMovementModeSelector> It(GetWorld()); It; ++It)
	{
		MovementMode = It->MovementMode;
		bUseCollisionGroundingExperiment = It->bUseCollisionNormalGroundingExperiment;
		CollisionGroundMaxAngleDegrees = It->CollisionGroundMaxAngleDegrees;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Level selector found: %s"), *GetNameSafe(*It));
		break;
	}
	RadialMovement->ConfigureCollisionGroundingExperiment(
		bUseCollisionGroundingExperiment,
		CollisionGroundMaxAngleDegrees);
	ForceMovement->ConfigureCollisionGroundingExperiment(
		bUseCollisionGroundingExperiment,
		CollisionGroundMaxAngleDegrees);
	const bool bUseForceSuspension = MovementMode == EABTSBirdMovementMode::ForceSuspension;
	const bool bUseLegacySweep = MovementMode == EABTSBirdMovementMode::LegacySweep;
	const bool bUseChaos = MovementMode == EABTSBirdMovementMode::ChaosRigidBody;
	RadialMovement->SetComponentTickEnabled(bUseLegacySweep);
	ForceMovement->SetComponentTickEnabled(bUseForceSuspension);
	ChaosMovement->SetComponentTickEnabled(bUseChaos);
	ChaosMovement->ConfigureCollisionGrounding(CollisionGroundMaxAngleDegrees);
	ConfigureChaosPhysicsBody(bUseChaos);
	GetSphericalSurface()->SetApplyActorFrame(!bUseChaos);
	ChaosMovement->SetChaosEnabled(bUseChaos);
	ResetRadialMovementState();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Active=%s LegacyTick=%d ForceTick=%d ChaosTick=%d CollisionGroundExperiment=%d MaxGroundAngle=%.1f"),
		bUseForceSuspension ? TEXT("ForceSuspension") : bUseLegacySweep ? TEXT("LegacySweep") : TEXT("ChaosRigidBody"),
		RadialMovement->IsComponentTickEnabled() ? 1 : 0,
		ForceMovement->IsComponentTickEnabled() ? 1 : 0,
		ChaosMovement->IsComponentTickEnabled() ? 1 : 0,
		bUseCollisionGroundingExperiment ? 1 : 0,
		FMath::Clamp(CollisionGroundMaxAngleDegrees, 0.0f, 89.0f));
}

void AABTSM25BirdCharacter::ResetRadialMovementState()
{
	RadialMovement->ResetMotionState();
	ForceMovement->ResetMotionState();
	ChaosMovement->ResetMotionState();
}

void AABTSM25BirdCharacter::EnablePlanarChaosMovement(const FVector& PlaneOrigin, const FVector& PlaneUp)
{
	bPlanarChaosMode = true;
	MovementMode = EABTSBirdMovementMode::ChaosRigidBody;
	RadialMovement->SetComponentTickEnabled(false);
	ForceMovement->SetComponentTickEnabled(false);
	ChaosMovement->ConfigurePlanarTestMode(true, PlaneOrigin, PlaneUp);
	ChaosMovement->SetComponentTickEnabled(true);
	ConfigureChaosPhysicsBody(true);
	GetSphericalSurface()->SetApplyActorFrame(false);
	ChaosMovement->SetChaosEnabled(true);
	ResetRadialMovementState();
}

void AABTSM25BirdCharacter::AddPartyTickPrerequisite(AActor* PartyActor)
{
	if (PartyActor == nullptr) return;
	RadialMovement->AddTickPrerequisiteActor(PartyActor);
	ForceMovement->AddTickPrerequisiteActor(PartyActor);
	ChaosMovement->AddTickPrerequisiteActor(PartyActor);
}

void AABTSM25BirdCharacter::ClearControlHandoffState()
{
	RadialMovement->ClearControlHandoffState();
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->ClearControlHandoffState();
		return;
	}
	// A newly possessed follower may currently be airborne because it was
	// executing a queued follow jump. Clearing velocity alone leaves it suspended
	// above the surface, so gravity looks like an autonomous post-switch command.
	// Establish a deterministic grounded handoff for the new controlled bird.
	if (Controller != nullptr && bPartyControlled)
	{
		ForceMovement->StabilizeForGroundedControlHandoff();
	}
	else
	{
		// A departing airborne leader keeps its physical trajectory; only steering
		// authority is removed. A grounded leader may stop immediately.
		if (ForceMovement->IsGrounded()) ForceMovement->ClearControlHandoffState();
		else ForceMovement->ClearControlHandoffInput();
	}
	RadialMovement->GrantControlHandoffJumpGrace(0.28f);
	ForceMovement->GrantControlHandoffJumpGrace(0.28f);
}

bool AABTSM25BirdCharacter::ResetForControlHandoffCacheExperiment()
{
	// This deliberately resets both implementations, even though only one ticks.
	// A later editor mode change must not revive a velocity, buffered jump or
	// contact cache captured while this bird was still a follower.
	RadialMovement->ResetMotionState();
	ForceMovement->EndBallisticFlight(true);
	ForceMovement->ResetMotionState();
	ChaosMovement->EndBallisticFlight(true);
	ChaosMovement->ResetMotionState();

	bool bGroundContactRebuilt = false;
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		bGroundContactRebuilt = ForceMovement->StabilizeForGroundedControlHandoff();
	}

	UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][HandoffExperiment] Bird=%d MotionCachesCleared=1 Mode=%d GroundRebuilt=%d"),
		ABTSBirdIdToIndex(BirdId),
		static_cast<int32>(MovementMode),
		bGroundContactRebuilt ? 1 : 0);
	return bGroundContactRebuilt;
}

void AABTSM25BirdCharacter::BeginControlHandoffDiagnostics(const float Seconds)
{
	ControlDiagnosticRemainingSeconds = FMath::Max(0.0f, Seconds);
	ControlDiagnosticLogAccumulator = 1.0f;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Begin] Bird=%d Seconds=%.2f Controller=%s Flag=%d Mode=%d"),
		ABTSBirdIdToIndex(BirdId), ControlDiagnosticRemainingSeconds, *GetNameSafe(Controller), bPartyControlled ? 1 : 0, static_cast<int32>(MovementMode));
}

void AABTSM25BirdCharacter::SetBirdIdentity(
	const EABTSBirdId InBirdId,
	const EABTSBirdSlingshotCapability InCapability,
	const bool bInPlayerControlled)
{
	BirdId = InBirdId;
	SlingshotCapability = InCapability;
	SetPartyControlled(bInPlayerControlled);
	ApplyCuteBirdMaterials();
}

void AABTSM25BirdCharacter::SetPartyControlled(const bool bInPlayerControlled)
{
	bPartyControlled = bInPlayerControlled;
	UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][Flag] Bird=%d NewFlag=%d Controller=%s"),
		ABTSBirdIdToIndex(BirdId), bPartyControlled ? 1 : 0, *GetNameSafe(Controller));
}

void AABTSM25BirdCharacter::SetPartyCollisionIsolation(const bool bIsolateFromParty)
{
	if (GetCapsuleComponent() == nullptr) return;
	// Party birds are not gameplay obstacles for one another. WorldStatic
	// buildings and terrain keep their normal responses.
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ECC_Pawn,
		bIsolateFromParty ? ECR_Ignore : ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ECC_Camera,
		bIsolateFromParty ? ECR_Ignore : ECR_Block);
	if (ChaosPhysicsSphere)
	{
		ChaosPhysicsSphere->SetCollisionResponseToChannel(ECC_Pawn, bIsolateFromParty ? ECR_Ignore : ECR_Block);
		ChaosPhysicsSphere->SetCollisionResponseToChannel(ECC_Camera, bIsolateFromParty ? ECR_Ignore : ECR_Block);
	}
}

void AABTSM25BirdCharacter::SetDeveloperWalkEnabled(const bool bEnabled, const float SpeedMultiplier)
{
#if UE_BUILD_SHIPPING
	bDeveloperWalkEnabled = false;
	const float ResolvedMultiplier = 1.0f;
	(void)bEnabled;
	(void)SpeedMultiplier;
#else
	bDeveloperWalkEnabled = bEnabled;
	const float ResolvedMultiplier = bDeveloperWalkEnabled
		? FMath::Clamp(SpeedMultiplier, 1.0f, 10.0f)
		: 1.0f;
#endif
	ApplyDeveloperObstacleCollisionResponse();
	RadialMovement->SetDeveloperWalkingSpeedMultiplier(ResolvedMultiplier);
	ForceMovement->SetDeveloperWalkingSpeedMultiplier(ResolvedMultiplier);
	ChaosMovement->SetDeveloperWalkingSpeedMultiplier(ResolvedMultiplier);
}

ECollisionResponse AABTSM25BirdCharacter::
ResolveDeveloperObstacleCollisionResponse(
	const bool bDeveloperWalk,
	const bool bSlingshotFlight)
{
	return bDeveloperWalk && !bSlingshotFlight ? ECR_Ignore : ECR_Block;
}

ECollisionResponse AABTSM25BirdCharacter::
ResolveWalkBarrierCollisionResponse(
	const bool bDeveloperWalk,
	const bool bSlingshotFlight)
{
	return bDeveloperWalk || bSlingshotFlight ? ECR_Ignore : ECR_Block;
}

void AABTSM25BirdCharacter::ApplyDeveloperObstacleCollisionResponse()
{
	const ECollisionResponse Response =
		ResolveDeveloperObstacleCollisionResponse(
			bDeveloperWalkEnabled,
			bSlingshotObstacleCollisionOverride);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ABTSDeveloperObstacleChannel, Response);
	const ECollisionResponse WalkBarrierResponse =
		ResolveWalkBarrierCollisionResponse(
			bDeveloperWalkEnabled,
			bSlingshotObstacleCollisionOverride);
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ABTSWalkBarrierChannel,
		WalkBarrierResponse);
	if (ChaosPhysicsSphere)
	{
		ChaosPhysicsSphere->SetCollisionResponseToChannel(ABTSDeveloperObstacleChannel, Response);
		ChaosPhysicsSphere->SetCollisionResponseToChannel(
			ABTSWalkBarrierChannel,
			WalkBarrierResponse);
	}
}

bool AABTSM25BirdCharacter::CanUseSlingshotCapability(const EABTSBirdSlingshotCapability RequiredCapability) const
{
	return SlingshotCapability == RequiredCapability;
}

void AABTSM25BirdCharacter::EnterSlingshotPouch(const FVector& WorldLocation, const FQuat& WorldRotation)
{
	StableChaosPresentationForward = FVector::ZeroVector;
	bChaosVisualRotationInitialized = false;
	SlingshotImpactFacingLockRemainingSeconds = 0.0f;
	ClearSlingshotPresentationUp();
	SavedCapsuleCollision = GetCapsuleComponent()->GetCollisionEnabled();
	SavedChaosBodyCollision = ChaosPhysicsSphere ? ChaosPhysicsSphere->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
	bSlingshotObstacleCollisionOverride = true;
	ApplyDeveloperObstacleCollisionResponse();
	SetLocomotionCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->EndBallisticFlight(true);
		ChaosMovement->SetChaosEnabled(false);
	}
	ForceMovement->EndBallisticFlight(true);
	ForceMovement->SetComponentTickEnabled(false);
	SetActorLocationAndRotation(WorldLocation, WorldRotation, false, nullptr, ETeleportType::TeleportPhysics);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::LaunchFromSlingshot(const FVector& InitialVelocity, const float FlightAirDragPerSecond)
{
	ClearSlingshotPresentationUp();
	bSlingshotObstacleCollisionOverride = true;
	ApplyDeveloperObstacleCollisionResponse();
	SetLocomotionCollisionEnabled(MovementMode == EABTSBirdMovementMode::ChaosRigidBody ? SavedChaosBodyCollision : SavedCapsuleCollision);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M6][LaunchCollision] Bird=%d DeveloperWalk=%d DeveloperObstacle=Block WalkBarrier=Ignore DeferredBuildingFirstHitEnabled=1"),
		ABTSBirdIdToIndex(BirdId), bDeveloperWalkEnabled ? 1 : 0);
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->SetChaosEnabled(true);
		ChaosMovement->SetComponentTickEnabled(true);
		ChaosMovement->BeginBallisticFlight(InitialVelocity, FlightAirDragPerSecond);
		ResetBirdLocomotionAudio(IsRadiallyGrounded());
		return;
	}
	ForceMovement->SetComponentTickEnabled(true);
	ForceMovement->BeginBallisticFlight(InitialVelocity, FlightAirDragPerSecond);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::BeginSlingshotReturn()
{
	ClearSlingshotPresentationUp();
	SetLocomotionCollisionEnabled(ECollisionEnabled::NoCollision);
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->EndBallisticFlight(true);
		ChaosMovement->SetChaosEnabled(false);
	}
	ForceMovement->EndBallisticFlight(true);
	ForceMovement->SetComponentTickEnabled(false);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::FinishSlingshotReturn()
{
	ClearSlingshotPresentationUp();
	bSlingshotObstacleCollisionOverride = false;
	ApplyDeveloperObstacleCollisionResponse();
	SetLocomotionCollisionEnabled(MovementMode == EABTSBirdMovementMode::ChaosRigidBody ? SavedChaosBodyCollision : SavedCapsuleCollision);
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->SetChaosEnabled(true);
		ChaosMovement->SetComponentTickEnabled(true);
		ChaosMovement->EndBallisticFlight(true);
		ResetBirdLocomotionAudio(IsRadiallyGrounded());
		return;
	}
	ForceMovement->SetComponentTickEnabled(true);
	ForceMovement->EndBallisticFlight(true);
	ResetBirdLocomotionAudio(IsRadiallyGrounded());
}

void AABTSM25BirdCharacter::SetSlingshotVelocity(const FVector& InVelocity)
{
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		ChaosMovement->SetVelocity(InVelocity);
		return;
	}
	ForceMovement->SetVelocity(InVelocity);
}

FVector AABTSM25BirdCharacter::GetSlingshotVelocity() const
{
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody) return ChaosMovement->GetVelocity();
	return ForceMovement->GetVelocity();
}

float AABTSM25BirdCharacter::GetSlingshotTrajectoryCollisionRadiusCM() const
{
	const bool bMovementModeAlreadyUsesChaos =
		MovementMode == EABTSBirdMovementMode::ChaosRigidBody;
	bool bUsesChaosCollision = bMovementModeAlreadyUsesChaos;
	// The calibration Rig can be spawned from the initial player's BeginPlay
	// callback before that player's ConfigureMovementMode returns. Resolve the
	// already-authored level selector in that narrow startup window instead of
	// briefly reporting the inactive capsule half-height.
	if (!bUsesChaosCollision && GetWorld() != nullptr)
	{
		TActorIterator<AABTSMovementModeSelector> It(GetWorld());
		if (It)
		{
			bUsesChaosCollision =
				It->MovementMode
					== EABTSBirdMovementMode::ChaosRigidBody;
		}
	}
	if (bUsesChaosCollision && ChaosPhysicsSphere != nullptr)
	{
		if (!bMovementModeAlreadyUsesChaos
			&& GetCapsuleComponent() != nullptr)
		{
			// Super::BeginPlay may assemble the calibration Rig before this
			// actor's ConfigureMovementMode has captured SavedChaosCapsuleRadius.
			// In that window the still-authoritative capsule has the authored
			// world-space radius; do not leak the native 42 cm initializer.
			return FMath::Max(
				1.0f,
				GetCapsuleComponent()->GetScaledCapsuleRadius());
		}
		// ConfigureChaosPhysicsBody scales the Engine sphere from this captured
		// capsule radius. Component Bounds may include attached visual/capsule
		// children and therefore is not the Chaos collision shape authority.
		return FMath::Max(1.0f, SavedChaosCapsuleRadius);
	}
	// Preserve the established ForceSuspension preview contract: radial terrain
	// clearance uses the capsule's scaled radius, not its half height.
	return GetCapsuleComponent()
		? FMath::Max(1.0f, GetCapsuleComponent()->GetScaledCapsuleRadius())
		: 1.0f;
}

bool AABTSM25BirdCharacter::IsSlingshotFlightActive() const
{
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody) return ChaosMovement->IsBallisticFlight();
	return ForceMovement->IsBallisticFlight();
}

void AABTSM25BirdCharacter::ApplyMoveInput(const FVector& Direction, const float Scale)
{
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->SetMoveInput(Direction, Scale);
	}
	else if (MovementMode == EABTSBirdMovementMode::LegacySweep)
	{
		RadialMovement->SetMoveInput(Direction, Scale);
	}
	else ChaosMovement->SetMoveInput(Direction, Scale);
}

void AABTSM25BirdCharacter::ApplyPartyMoveInput(const FVector& Direction, const float Scale)
{
	// Possession is the authoritative input owner.  A duplicated party flag can
	// be stale for part of a control-transfer frame; a possessed pawn must never
	// consume follower steering under any ordering.
	if (Controller != nullptr || bPartyControlled || Direction.IsNearlyZero() || FMath::IsNearlyZero(Scale))
	{
		if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M4][HandoffDiag][PartyMoveRejected] Bird=%d Controller=%s Flag=%d Dir=(%.2f,%.2f,%.2f) Scale=%.2f"),
			ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0, Direction.X, Direction.Y, Direction.Z, Scale);
		return;
	}
	if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][HandoffDiag][PartyMoveAccepted] Bird=%d Dir=(%.2f,%.2f,%.2f) Scale=%.2f"),
		ABTSBirdIdToIndex(BirdId), Direction.X, Direction.Y, Direction.Z, Scale);
	GetSphericalSurface()->SetMovementFacing(Scale >= 0.0f ? Direction : -Direction);
	ApplyMoveInput(Direction, Scale);
}

void AABTSM25BirdCharacter::ApplyPartyJump()
{
	if (Controller != nullptr || bPartyControlled)
	{
		if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][PartyJumpRejected] Bird=%d Controller=%s Flag=%d"), ABTSBirdIdToIndex(BirdId), *GetNameSafe(Controller), bPartyControlled ? 1 : 0);
		return;
	}
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension) ForceMovement->QueueJump();
	else if (MovementMode == EABTSBirdMovementMode::LegacySweep) RadialMovement->QueueJump();
	else ChaosMovement->QueueJump();
}

bool AABTSM25BirdCharacter::IsRadiallyGrounded() const
{
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension) return ForceMovement->IsGrounded();
	if (MovementMode == EABTSBirdMovementMode::LegacySweep) return RadialMovement->IsGrounded();
	return ChaosMovement->IsGrounded();
}

void AABTSM25BirdCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveForward"), this, &AABTSM25BirdCharacter::MoveWithRadialPhysicsForward);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveRight"), this, &AABTSM25BirdCharacter::MoveWithRadialPhysicsRight);
	PlayerInputComponent->BindAxis(TEXT("ABTS_Turn"), this, &AABTSM25BirdCharacter::TurnWithRadialPhysics);
	PlayerInputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &AABTSM25BirdCharacter::LookWithRadialPhysics);
	PlayerInputComponent->BindAction(TEXT("ABTS_Jump"), IE_Pressed, this, &AABTSM25BirdCharacter::BeginRadialJump);
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsForward(const float Value)
{
	if (IsControllerRoutedMovementInputExperimentEnabled()) return;
	ProcessMoveWithRadialPhysicsForward(Value, false);
}

void AABTSM25BirdCharacter::HandleControllerRoutedMoveForward(const float Value)
{
	ProcessMoveWithRadialPhysicsForward(Value, true);
}

void AABTSM25BirdCharacter::ProcessMoveWithRadialPhysicsForward(const float Value, const bool bControllerRouted)
{
	if (!FMath::IsNearlyZero(Value) && (bPlanarChaosMode || GetSphericalSurface()->IsSurfaceFrameReady()))
	{
		const FVector Up = bPlanarChaosMode
			? ChaosMovement->GetMovementUpAt(GetActorLocation())
			: GetSphericalSurface()->GetRadialUp();
		FVector Direction = bPlanarChaosMode
			? FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal()
			: GetSphericalSurface()->GetTangentForward();
		FVector CameraRight = FVector::ZeroVector;
		if (const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller))
		{
			M4Controller->GetCameraRelativeMovementBasis(GetActorLocation(), Direction, CameraRight);
		}
		if (!bPlanarChaosMode) GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		const bool bAccepted = Controller != nullptr && IsLocallyControlled();
		if (IsControlHandoffDiagnosticsActive())
		{
			const APlayerController* PlayerController = Cast<APlayerController>(Controller);
			UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M4][HandoffDiag][%sForward] Bird=%d Value=%.2f Accepted=%d Controller=%s W=%d S=%d"),
			bControllerRouted ? TEXT("ControllerRouted") : TEXT("Player"),
			ABTSBirdIdToIndex(BirdId), Value, bAccepted ? 1 : 0, *GetNameSafe(Controller),
				PlayerController && PlayerController->IsInputKeyDown(EKeys::W) ? 1 : 0,
				PlayerController && PlayerController->IsInputKeyDown(EKeys::S) ? 1 : 0);
		}
		if (bAccepted) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsRight(const float Value)
{
	if (IsControllerRoutedMovementInputExperimentEnabled()) return;
	ProcessMoveWithRadialPhysicsRight(Value, false);
}

void AABTSM25BirdCharacter::HandleControllerRoutedMoveRight(const float Value)
{
	ProcessMoveWithRadialPhysicsRight(Value, true);
}

void AABTSM25BirdCharacter::ProcessMoveWithRadialPhysicsRight(const float Value, const bool bControllerRouted)
{
	if (!FMath::IsNearlyZero(Value) && (bPlanarChaosMode || GetSphericalSurface()->IsSurfaceFrameReady()))
	{
		const FVector Up = bPlanarChaosMode
			? ChaosMovement->GetMovementUpAt(GetActorLocation())
			: GetSphericalSurface()->GetRadialUp();
		FVector CameraForward = FVector::ZeroVector;
		FVector Direction = bPlanarChaosMode
			? FVector::CrossProduct(Up, FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal()).GetSafeNormal()
			: GetSphericalSurface()->GetTangentRight();
		if (const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller))
		{
			M4Controller->GetCameraRelativeMovementBasis(GetActorLocation(), CameraForward, Direction);
		}
		if (!bPlanarChaosMode) GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		const bool bAccepted = Controller != nullptr && IsLocallyControlled();
		if (IsControlHandoffDiagnosticsActive())
		{
			const APlayerController* PlayerController = Cast<APlayerController>(Controller);
			UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M4][HandoffDiag][%sRight] Bird=%d Value=%.2f Accepted=%d Controller=%s D=%d A=%d"),
			bControllerRouted ? TEXT("ControllerRouted") : TEXT("Player"),
			ABTSBirdIdToIndex(BirdId), Value, bAccepted ? 1 : 0, *GetNameSafe(Controller),
				PlayerController && PlayerController->IsInputKeyDown(EKeys::D) ? 1 : 0,
				PlayerController && PlayerController->IsInputKeyDown(EKeys::A) ? 1 : 0);
		}
		if (bAccepted) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::TurnWithRadialPhysics(const float Value)
{
	if (Controller && Controller->IsA<AABTSM4PlayerController>()) return;
	TurnOnSphere(Value);
}

void AABTSM25BirdCharacter::LookWithRadialPhysics(const float Value)
{
	if (Controller && Controller->IsA<AABTSM4PlayerController>()) return;
	LookOnSphere(Value);
}

void AABTSM25BirdCharacter::BeginRadialJump()
{
	if (IsControllerRoutedMovementInputExperimentEnabled()) return;
	ProcessRadialJump(false);
}

void AABTSM25BirdCharacter::HandleControllerRoutedJump()
{
	ProcessRadialJump(true);
}

void AABTSM25BirdCharacter::ProcessRadialJump(const bool bControllerRouted)
{
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Jump] Space input reached AABTSM25BirdCharacter. Mode=%s"),
		MovementMode == EABTSBirdMovementMode::ForceSuspension ? TEXT("ForceSuspension") : MovementMode == EABTSBirdMovementMode::LegacySweep ? TEXT("LegacySweep") : TEXT("ChaosRigidBody"));
	const bool bAccepted = Controller != nullptr && IsLocallyControlled();
	if (IsControlHandoffDiagnosticsActive()) UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M4][HandoffDiag][%sJump] Bird=%d Accepted=%d Controller=%s Grounded=%d"), bControllerRouted ? TEXT("ControllerRouted") : TEXT("Player"), ABTSBirdIdToIndex(BirdId), bAccepted ? 1 : 0, *GetNameSafe(Controller), IsRadiallyGrounded() ? 1 : 0);
	if (!bAccepted) return;
	const bool bAudioEligibleJump = IsRadiallyGrounded();
	if (IsClearMotionBeforePlayerJumpExperimentEnabled())
	{
		ApplyClearMotionBeforeJumpExperiment();
	}
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->QueueJump();
	}
	else if (MovementMode == EABTSBirdMovementMode::LegacySweep)
	{
		RadialMovement->QueueJump();
	}
	else ChaosMovement->QueueJump();
	if (bAudioEligibleJump)
	{
		PendingPlayerJumpAudioSeconds = 0.35f;
	}
}

FVector AABTSM25BirdCharacter::GetPresentationVelocity() const
{
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension) return ForceMovement->GetVelocity();
	if (MovementMode == EABTSBirdMovementMode::LegacySweep) return RadialMovement->GetVelocity();
	return ChaosMovement->GetVelocity();
}

void AABTSM25BirdCharacter::SetSlingshotPresentationUp(
	const FVector& WorldUp,
	const float DeltaSeconds,
	const bool bLockFacingReversal,
	const FVector& ViewStableWorldForward)
{
	(void)DeltaSeconds;
	const FVector SafeUp = WorldUp.GetSafeNormal();
	if (SafeUp.IsNearlyZero()) return;
	bSlingshotPresentationUpActive = true;
	bSlingshotPresentationLockFacingReversal = bLockFacingReversal;
	SlingshotPresentationUp = SafeUp;
	SlingshotPresentationViewForward = ViewStableWorldForward.GetSafeNormal();
}

FVector AABTSM25BirdCharacter::ComputeRotationMinimizedSlingshotForward(
	const FVector& PreviousForward,
	const FVector& PreviousUp,
	const FVector& NewUp,
	const FVector& Velocity,
	const FVector& ViewStableWorldForward,
	const float MaximumVelocityCorrectionDegrees,
	const bool bLockFacingReversal)
{
	const FVector FromUp = PreviousUp.GetSafeNormal();
	const FVector ToUp = NewUp.GetSafeNormal();
	if (ToUp.IsNearlyZero()) return FVector::ZeroVector;

	FVector SourceForward = FVector::VectorPlaneProject(
		PreviousForward,
		FromUp.IsNearlyZero() ? ToUp : FromUp).GetSafeNormal();
	if (SourceForward.IsNearlyZero())
	{
		const FVector Reference = FMath::Abs(ToUp.Z) < 0.9f
			? FVector::UpVector
			: FVector::ForwardVector;
		SourceForward = FVector::CrossProduct(Reference, ToUp).GetSafeNormal();
	}

	FVector TransportedForward = SourceForward;
	if (!FromUp.IsNearlyZero())
	{
		const float UpDot = FMath::Clamp(
			FVector::DotProduct(FromUp, ToUp), -1.0f, 1.0f);
		FVector RotationAxis = FVector::CrossProduct(FromUp, ToUp).GetSafeNormal();
		if (RotationAxis.IsNearlyZero() && UpDot < 0.0f)
		{
			RotationAxis = FVector::VectorPlaneProject(
				SourceForward, FromUp).GetSafeNormal();
			if (RotationAxis.IsNearlyZero())
			{
				const FVector Reference = FMath::Abs(FromUp.Z) < 0.9f
					? FVector::UpVector
					: FVector::ForwardVector;
				RotationAxis = FVector::CrossProduct(
					FromUp, Reference).GetSafeNormal();
			}
		}
		if (!RotationAxis.IsNearlyZero())
		{
			TransportedForward = FQuat(
				RotationAxis,
				FMath::Acos(UpDot)).RotateVector(SourceForward);
		}
	}
	TransportedForward = FVector::VectorPlaneProject(
		TransportedForward, ToUp).GetSafeNormal();
	if (TransportedForward.IsNearlyZero()) return SourceForward;

	const FVector ViewStableForward = FVector::VectorPlaneProject(
		ViewStableWorldForward, ToUp).GetSafeNormal();
	if (!ViewStableForward.IsNearlyZero())
	{
		// The follow camera and the presentation Up are driven by the same
		// continuously blended primary-to-satellite frame.  Consume that view
		// anchor directly: applying the physical velocity correction budget here
		// makes the mesh lag behind the camera basis and reads as an extra roll.
		// This changes only the visual mesh frame; actor/Chaos authority remains
		// free to rotate in world space.
		if (bLockFacingReversal
			&& FVector::DotProduct(TransportedForward, ViewStableForward) < 0.0f)
		{
			return TransportedForward;
		}
		return ViewStableForward;
	}

	const FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, ToUp);
	const float VelocitySize = Velocity.Size();
	const bool bVelocityDirectionReliable = TangentVelocity.Size() >= 120.0f
		&& (VelocitySize <= KINDA_SMALL_NUMBER
			|| TangentVelocity.Size() / VelocitySize >= 0.25f);
	if (!bVelocityDirectionReliable) return TransportedForward;
	const FVector VelocityForward = TangentVelocity.GetSafeNormal();
	const float ForwardDot = FMath::Clamp(
		FVector::DotProduct(TransportedForward, VelocityForward), -1.0f, 1.0f);
	if (bLockFacingReversal && ForwardDot < 0.0f) return TransportedForward;

	const float SignedCorrectionRadians = FMath::Atan2(
		FVector::DotProduct(
			FVector::CrossProduct(TransportedForward, VelocityForward),
			ToUp),
		ForwardDot);
	const float LimitedCorrectionRadians = FMath::Clamp(
		SignedCorrectionRadians,
		-FMath::DegreesToRadians(FMath::Max(0.0f, MaximumVelocityCorrectionDegrees)),
		FMath::DegreesToRadians(FMath::Max(0.0f, MaximumVelocityCorrectionDegrees)));
	return FQuat(ToUp, LimitedCorrectionRadians)
		.RotateVector(TransportedForward).GetSafeNormal();
}

void AABTSM25BirdCharacter::ClearSlingshotPresentationUp()
{
	if (bSlingshotPresentationUpActive)
	{
		if (const USkeletalMeshComponent* Visual = GetBirdVisual())
		{
			ChaosVisualRotation = Visual->GetComponentQuat().GetNormalized();
			bChaosVisualRotationInitialized = true;
		}
	}
	bSlingshotPresentationUpActive = false;
	bSlingshotPresentationFrameInitialized = false;
	bSlingshotPresentationLockFacingReversal = false;
	SlingshotPresentationUp = FVector::UpVector;
	SlingshotPresentationViewForward = FVector::ZeroVector;
	SlingshotPresentationFrame = FQuat::Identity;
}

void AABTSM25BirdCharacter::NotifySlingshotPresentationImpact()
{
	SlingshotImpactFacingLockRemainingSeconds = FMath::Max(
		SlingshotImpactFacingLockRemainingSeconds,
		0.55f);
}

void AABTSM25BirdCharacter::ApplyCuteBirdMaterials()
{
	USkeletalMeshComponent* Visual = GetBirdVisual();
	const int32 MaterialIndex = ABTSBirdIdToIndex(BirdId);
	if (Visual == nullptr || MaterialIndex < 0 || MaterialIndex >= UE_ARRAY_COUNT(CuteBirdColorMaterials)) return;
	if (CuteBirdColorMaterials[MaterialIndex] != nullptr) Visual->SetMaterial(0, CuteBirdColorMaterials[MaterialIndex]);
	if (CuteBirdFaceMaterials[MaterialIndex] != nullptr) Visual->SetMaterial(1, CuteBirdFaceMaterials[MaterialIndex]);
}

void AABTSM25BirdCharacter::UpdateBirdAnimationPresentation(const float DeltaSeconds)
{
	if (!BirdAnimationPresentation) return;
	const FVector Velocity = GetPresentationVelocity();
	const FVector Up = bPlanarChaosMode
		? ChaosMovement->GetMovementUpAt(GetActorLocation())
		: GetSphericalSurface()->GetRadialUp();
	FABTSBirdAnimationSnapshot Snapshot;
	Snapshot.bGrounded = IsRadiallyGrounded();
	Snapshot.bForceFlight = IsSlingshotFlightActive();
	Snapshot.TangentialSpeedCMPerSecond = FVector::VectorPlaneProject(Velocity, Up).Size();
	BirdAnimationPresentation->UpdatePresentation(Snapshot, DeltaSeconds);
}

void AABTSM25BirdCharacter::ResetBirdLocomotionAudio(const bool bGrounded)
{
	bLocomotionAudioInitialized = true;
	bLocomotionAudioWasGrounded = bGrounded;
	PreviousLocomotionAudioLocation = GetActorLocation();
	AccumulatedFootstepDistanceCM = 0.0f;
	PeakAirborneDownwardSpeedCMPerSec = 0.0f;
	PendingPlayerJumpAudioSeconds = 0.0f;
}

EABTSFootstepSurface AABTSM25BirdCharacter::ResolveFootstepSurface(const FVector& WorldUp) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr) return EABTSFootstepSurface::Grass;
	const FVector SafeUp = WorldUp.IsNearlyZero() ? GetActorUpVector() : WorldUp.GetSafeNormal();
	const float TraceDepthCM = (GetCapsuleComponent()
		? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 60.0f) + 140.0f;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ABTSBirdFootstepSurface), false, this);
	QueryParams.bReturnPhysicalMaterial = true;
	if (!World->LineTraceSingleByChannel(
		Hit,
		GetActorLocation() + SafeUp * 20.0f,
		GetActorLocation() - SafeUp * TraceDepthCM,
		ECC_Visibility,
		QueryParams))
	{
		return EABTSFootstepSurface::Grass;
	}

	FString SemanticName;
	if (const UPhysicalMaterial* PhysicalMaterial = Hit.PhysMaterial.Get())
	{
		SemanticName += PhysicalMaterial->GetName();
	}
	if (const UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		SemanticName += TEXT(" ") + HitComponent->GetName();
		SemanticName += TEXT(" ") + HitComponent->GetClass()->GetName();
		for (int32 MaterialIndex = 0; MaterialIndex < HitComponent->GetNumMaterials(); ++MaterialIndex)
		{
			if (const UMaterialInterface* Material = HitComponent->GetMaterial(MaterialIndex))
			{
				SemanticName += TEXT(" ") + Material->GetName();
			}
		}
	}
	if (const AActor* HitActor = Hit.GetActor())
	{
		SemanticName += TEXT(" ") + HitActor->GetName();
		SemanticName += TEXT(" ") + HitActor->GetClass()->GetName();
	}
	return UABTSAudioWorldSubsystem::ResolveFootstepSurfaceFromSemanticName(SemanticName);
}

void AABTSM25BirdCharacter::UpdateBirdLocomotionAudio(const float DeltaSeconds)
{
	const bool bGrounded = IsRadiallyGrounded();
	const FVector CurrentLocation = GetActorLocation();
	const bool bEligible = Controller != nullptr
		&& IsLocallyControlled()
		&& !IsSlingshotFlightActive();
	if (!bLocomotionAudioInitialized || !bEligible)
	{
		ResetBirdLocomotionAudio(bGrounded);
		return;
	}

	PendingPlayerJumpAudioSeconds = FMath::Max(
		0.0f,
		PendingPlayerJumpAudioSeconds - FMath::Max(0.0f, DeltaSeconds));
	const FVector WorldUp = bPlanarChaosMode
		? ChaosMovement->GetMovementUpAt(CurrentLocation)
		: GetSphericalSurface()->GetRadialUp();
	const FVector Velocity = GetPresentationVelocity();
	const float TangentialSpeedCMPerSec = FVector::VectorPlaneProject(Velocity, WorldUp).Size();
	const float DownwardSpeedCMPerSec = FMath::Max(0.0f, -FVector::DotProduct(Velocity, WorldUp));
	UABTSAudioWorldSubsystem* Audio = GetWorld()
		? GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>()
		: nullptr;

	if (!bGrounded)
	{
		PeakAirborneDownwardSpeedCMPerSec = FMath::Max(
			PeakAirborneDownwardSpeedCMPerSec,
			DownwardSpeedCMPerSec);
	}
	if (bLocomotionAudioWasGrounded && !bGrounded)
	{
		AccumulatedFootstepDistanceCM = 0.0f;
		if (Audio && PendingPlayerJumpAudioSeconds > 0.0f)
		{
			Audio->PlayBirdChirp(CurrentLocation, BirdId, 0.62f);
		}
		PendingPlayerJumpAudioSeconds = 0.0f;
	}
	else if (!bLocomotionAudioWasGrounded && bGrounded)
	{
		if (Audio)
		{
			Audio->PlayLanding(
				CurrentLocation,
				ResolveFootstepSurface(WorldUp),
				PeakAirborneDownwardSpeedCMPerSec);
		}
		AccumulatedFootstepDistanceCM = 0.0f;
		PeakAirborneDownwardSpeedCMPerSec = 0.0f;
	}
	else if (bGrounded)
	{
		const float TangentialTravelCM = FVector::VectorPlaneProject(
			CurrentLocation - PreviousLocomotionAudioLocation,
			WorldUp).Size();
		if (TangentialSpeedCMPerSec >= 120.0f && TangentialTravelCM <= 440.0f)
		{
			AccumulatedFootstepDistanceCM += TangentialTravelCM;
			const float StepSpacingCM = UABTSAudioWorldSubsystem::ComputeFootstepSpacingCM(
				TangentialSpeedCMPerSec);
			if (Audio && AccumulatedFootstepDistanceCM >= StepSpacingCM)
			{
				Audio->PlayFootstep(
					CurrentLocation,
					ResolveFootstepSurface(WorldUp),
					TangentialSpeedCMPerSec);
				AccumulatedFootstepDistanceCM = FMath::Fmod(
					AccumulatedFootstepDistanceCM,
					StepSpacingCM);
			}
		}
		else if (TangentialSpeedCMPerSec < 120.0f || TangentialTravelCM > 440.0f)
		{
			AccumulatedFootstepDistanceCM = 0.0f;
		}
	}

	bLocomotionAudioWasGrounded = bGrounded;
	PreviousLocomotionAudioLocation = CurrentLocation;
}

void AABTSM25BirdCharacter::RequestBirdPresentationAction(const EABTSBirdPresentationAction Action)
{
	if (BirdAnimationPresentation)
	{
		BirdAnimationPresentation->RequestAction(Action);
	}
}

void AABTSM25BirdCharacter::UpdateChaosVisualFrame(const float DeltaSeconds)
{
	USkeletalMeshComponent* Visual = GetBirdVisual();
	if (Visual == nullptr) return;
	if (!bPlanarChaosMode && !GetSphericalSurface()->IsSurfaceFrameReady()) return;
	const FVector Up = bPlanarChaosMode
		? ChaosMovement->GetMovementUpAt(GetActorLocation())
		: GetSphericalSurface()->GetRadialUp();
	FVector Forward = bPlanarChaosMode
		? FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal()
		: GetSphericalSurface()->GetActorForwardTangent();
	const FVector Velocity = ChaosMovement->GetVelocity();
	const FVector TangentVelocity = FVector::VectorPlaneProject(Velocity, Up);
	const FVector VelocityForward = TangentVelocity.Size() >= 120.0f
		? TangentVelocity.GetSafeNormal()
		: FVector::ZeroVector;
	FVector PreviousForward = FVector::VectorPlaneProject(
		StableChaosPresentationForward,
		Up).GetSafeNormal();
	const bool bImpactReverse = SlingshotImpactFacingLockRemainingSeconds > 0.0f
		&& !PreviousForward.IsNearlyZero()
		&& !VelocityForward.IsNearlyZero()
		&& FVector::DotProduct(VelocityForward, PreviousForward) < 0.0f;
	if (!VelocityForward.IsNearlyZero() && !bImpactReverse)
	{
		Forward = VelocityForward;
	}
	else if (!PreviousForward.IsNearlyZero())
	{
		Forward = PreviousForward;
	}
	if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
	StableChaosPresentationForward = Forward;
	const FQuat PhysicsFacing = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
	const FQuat DesiredVisualRotation =
		(PhysicsFacing * GetBirdVisualAxisCorrection()).GetNormalized();
	if (!bChaosVisualRotationInitialized)
	{
		ChaosVisualRotation = Visual->GetComponentQuat().GetNormalized();
		bChaosVisualRotationInitialized = true;
	}
	ChaosVisualRotation = DeltaSeconds > 0.0f
		? FMath::QInterpTo(
			ChaosVisualRotation,
			DesiredVisualRotation,
			DeltaSeconds,
			8.0f).GetNormalized()
		: DesiredVisualRotation;
	const FVector CollisionCenter = ChaosPhysicsSphere ? ChaosPhysicsSphere->GetComponentLocation() : GetActorLocation();
	const FVector SupportPoint = CollisionCenter - Up * SavedChaosCapsuleRadius;
	const FVector VisualLocation = SupportPoint + PhysicsFacing.RotateVector(GetBirdVisualRelativeLocation());
	Visual->SetWorldLocationAndRotation(VisualLocation, ChaosVisualRotation);
}

void AABTSM25BirdCharacter::UpdateSlingshotPresentationFrame(
	const float DeltaSeconds)
{
	USkeletalMeshComponent* Visual = GetBirdVisual();
	if (Visual == nullptr || !bSlingshotPresentationUpActive) return;
	const FVector Up = SlingshotPresentationUp.GetSafeNormal();
	if (Up.IsNearlyZero()) return;
	if (!bSlingshotPresentationFrameInitialized)
	{
		SlingshotPresentationFrame = (
			Visual->GetComponentQuat()
			* GetBirdVisualAxisCorrection().Inverse()).GetNormalized();
		bSlingshotPresentationFrameInitialized = true;
	}
	const FVector Forward = ComputeRotationMinimizedSlingshotForward(
		SlingshotPresentationFrame.GetAxisX(),
		SlingshotPresentationFrame.GetAxisZ(),
		Up,
		GetSlingshotVelocity(),
		SlingshotPresentationViewForward,
		FMath::Max(0.0f, SlingshotPresentationForwardCorrectionDegreesPerSecond)
			* FMath::Max(0.0f, DeltaSeconds),
		bSlingshotPresentationLockFacingReversal);
	if (Forward.IsNearlyZero()) return;
	// The camera already rate-limits Up. Applying the rotation-minimizing frame
	// directly avoids a second quaternion filter that can turn radial hand-off
	// into visible roll in camera space.
	SlingshotPresentationFrame = FRotationMatrix::MakeFromXZ(
		Forward, Up).ToQuat().GetNormalized();
	FVector FrameOrigin = GetActorLocation();
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		const FVector CollisionCenter = ChaosPhysicsSphere
			? ChaosPhysicsSphere->GetComponentLocation()
			: GetActorLocation();
		FrameOrigin = CollisionCenter
			- SlingshotPresentationFrame.GetAxisZ() * SavedChaosCapsuleRadius;
	}
	const FVector VisualLocation = FrameOrigin
		+ SlingshotPresentationFrame.RotateVector(GetBirdVisualRelativeLocation());
	Visual->SetWorldLocationAndRotation(
		VisualLocation,
		SlingshotPresentationFrame * GetBirdVisualAxisCorrection());
}

UPrimitiveComponent* AABTSM25BirdCharacter::GetChaosPhysicsBody() const
{
	return ChaosPhysicsSphere;
}

void AABTSM25BirdCharacter::ConfigureChaosPhysicsBody(const bool bEnable)
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (Capsule == nullptr || ChaosPhysicsSphere == nullptr) return;
	SavedChaosCapsuleRadius = Capsule->GetUnscaledCapsuleRadius();
	SavedChaosCapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	if (!bEnable)
	{
		ChaosPhysicsSphere->SetSimulatePhysics(false);
		ChaosPhysicsSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	const FTransform ActorTransform = GetActorTransform();
	ChaosPhysicsSphere->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(ChaosPhysicsSphere);
	ChaosPhysicsSphere->SetWorldTransform(ActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Capsule->AttachToComponent(ChaosPhysicsSphere, FAttachmentTransformRules::KeepWorldTransform);
	Capsule->SetRelativeTransform(FTransform::Identity);
	const float EngineSphereRadiusCM = 50.0f;
	ChaosPhysicsSphere->SetRelativeScale3D(FVector(SavedChaosCapsuleRadius / EngineSphereRadiusCM));
	ChaosPhysicsSphere->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	ChaosPhysicsSphere->SetCollisionObjectType(ECC_Pawn);
	ChaosPhysicsSphere->SetCollisionResponseToChannels(Capsule->GetCollisionResponseToChannels());
	ChaosPhysicsSphere->SetCollisionResponseToChannel(
		ABTSDeveloperObstacleChannel,
		ResolveDeveloperObstacleCollisionResponse(
			bDeveloperWalkEnabled,
			bSlingshotObstacleCollisionOverride));
	ChaosPhysicsSphere->SetCollisionResponseToChannel(
		ABTSWalkBarrierChannel,
		ResolveWalkBarrierCollisionResponse(
			bDeveloperWalkEnabled,
			bSlingshotObstacleCollisionOverride));
	ChaosPhysicsSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][ChaosMovement] DedicatedSphereBody=1 Mesh=%s Radius=%.1f Root=%s CapsuleCollision=%s SphereCollision=%s"),
		*GetNameSafe(ChaosPhysicsSphere->GetStaticMesh()), SavedChaosCapsuleRadius, *GetNameSafe(GetRootComponent()),
		*UEnum::GetValueAsString(Capsule->GetCollisionEnabled()),
		*UEnum::GetValueAsString(ChaosPhysicsSphere->GetCollisionEnabled()));
}

void AABTSM25BirdCharacter::SetLocomotionCollisionEnabled(const ECollisionEnabled::Type CollisionEnabled)
{
	if (MovementMode == EABTSBirdMovementMode::ChaosRigidBody)
	{
		if (ChaosPhysicsSphere) ChaosPhysicsSphere->SetCollisionEnabled(CollisionEnabled);
		if (GetCapsuleComponent()) GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else if (GetCapsuleComponent())
	{
		GetCapsuleComponent()->SetCollisionEnabled(CollisionEnabled);
	}
}

bool AABTSM25BirdCharacter::IsControllerRoutedMovementInputExperimentEnabled() const
{
	const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller);
	return M4Controller != nullptr && M4Controller->IsControllerRoutedMovementInputExperimentEnabled();
}

bool AABTSM25BirdCharacter::IsClearMotionBeforePlayerJumpExperimentEnabled() const
{
	const AABTSM4PlayerController* M4Controller = Cast<AABTSM4PlayerController>(Controller);
	return M4Controller != nullptr && M4Controller->IsClearMotionBeforePlayerJumpExperimentEnabled();
}

void AABTSM25BirdCharacter::ApplyClearMotionBeforeJumpExperiment()
{
	const FVector ForceVelocityBefore = ForceMovement->GetVelocity();
	const FVector ForcePendingBefore = ForceMovement->GetPendingMoveVector();
	const FVector LegacyVelocityBefore = RadialMovement->GetVelocity();
	const FVector LegacyPendingBefore = RadialMovement->GetPendingMoveVector();
	const bool bForceGroundedBefore = ForceMovement->IsGrounded();
	const bool bLegacyGroundedBefore = RadialMovement->IsGrounded();
	const bool bJumpDetachBefore = SurfaceSuspension->IsJumpDetachActive();

	// This intentionally preserves grounding, suspension state, jump buffers and actor transform.
	// It changes only the motion state that a blocking tree collision would remove.
	ForceMovement->ClearControlHandoffVelocity();
	RadialMovement->ClearControlHandoffVelocity();

	UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][DriftJumpExperiment] Bird=%d Mode=%s Grounded=%d ForceGround=%d LegacyGround=%d Detach=%d ForceSpeedBefore=%.2f LegacySpeedBefore=%.2f ForceVelBefore=(%.1f,%.1f,%.1f) LegacyVelBefore=(%.1f,%.1f,%.1f) ForcePendingBefore=(%.2f,%.2f,%.2f) LegacyPendingBefore=(%.2f,%.2f,%.2f) Cleared=1"),
		ABTSBirdIdToIndex(BirdId),
		MovementMode == EABTSBirdMovementMode::ForceSuspension ? TEXT("ForceSuspension") : TEXT("LegacySweep"),
		IsRadiallyGrounded() ? 1 : 0,
		bForceGroundedBefore ? 1 : 0,
		bLegacyGroundedBefore ? 1 : 0,
		bJumpDetachBefore ? 1 : 0,
		ForceVelocityBefore.Size(),
		LegacyVelocityBefore.Size(),
		ForceVelocityBefore.X, ForceVelocityBefore.Y, ForceVelocityBefore.Z,
		LegacyVelocityBefore.X, LegacyVelocityBefore.Y, LegacyVelocityBefore.Z,
		ForcePendingBefore.X, ForcePendingBefore.Y, ForcePendingBefore.Z,
		LegacyPendingBefore.X, LegacyPendingBefore.Y, LegacyPendingBefore.Z);
}

void AABTSM25BirdCharacter::LogControlDiagnosticSnapshot()
{
	const FVector ForceVelocity = ForceMovement->GetVelocity();
	const FVector ForcePending = ForceMovement->GetPendingMoveVector();
	const FVector LegacyVelocity = RadialMovement->GetVelocity();
	const FVector LegacyPending = RadialMovement->GetPendingMoveVector();
	UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M4][HandoffDiag][Snapshot] Bird=%d Mode=%d Flag=%d Controller=%s Local=%d ForceVel=(%.1f,%.1f,%.1f) ForcePending=(%.2f,%.2f,%.2f) ForceGround=%d ForceDetach=%d ForceJump=%.3f ForceGrace=%.3f LegacyVel=(%.1f,%.1f,%.1f) LegacyPending=(%.2f,%.2f,%.2f) LegacyGround=%d LegacyJump=%.3f LegacyGrace=%.3f"),
		ABTSBirdIdToIndex(BirdId), static_cast<int32>(MovementMode), bPartyControlled ? 1 : 0, *GetNameSafe(Controller), IsLocallyControlled() ? 1 : 0,
		ForceVelocity.X, ForceVelocity.Y, ForceVelocity.Z, ForcePending.X, ForcePending.Y, ForcePending.Z, ForceMovement->IsGrounded() ? 1 : 0, SurfaceSuspension->IsJumpDetachActive() ? 1 : 0, ForceMovement->GetJumpBufferRemainingSeconds(), ForceMovement->GetControlHandoffJumpGraceRemainingSeconds(),
		LegacyVelocity.X, LegacyVelocity.Y, LegacyVelocity.Z, LegacyPending.X, LegacyPending.Y, LegacyPending.Z, RadialMovement->IsGrounded() ? 1 : 0, RadialMovement->GetJumpBufferRemainingSeconds(), RadialMovement->GetControlHandoffJumpGraceRemainingSeconds());
}
