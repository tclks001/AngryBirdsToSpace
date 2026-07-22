// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM25BirdCharacter.h"

#include "ABTSRuntime.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/ABTSMovementModeSelector.h"
#include "Movement/ABTSM25RadialMovementComponent.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSRadialSurfaceSuspensionComponent.h"
#include "Planet/ABTSM2SphericalSurfaceComponent.h"

AABTSM25BirdCharacter::AABTSM25BirdCharacter()
{
	RadialMovement = CreateDefaultSubobject<UABTSM25RadialMovementComponent>(TEXT("RadialMovement"));
	ForceMovement = CreateDefaultSubobject<UABTSRadialForceMovementComponent>(TEXT("ForceMovement"));
	SurfaceSuspension = CreateDefaultSubobject<UABTSRadialSurfaceSuspensionComponent>(TEXT("SurfaceSuspension"));
}

void AABTSM25BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->DisableMovement();
	GetSphericalSurface()->SetProjectToBaseSurface(false);
	ConfigureMovementMode();
}

void AABTSM25BirdCharacter::ConfigureMovementMode()
{
	for (TActorIterator<AABTSMovementModeSelector> It(GetWorld()); It; ++It)
	{
		MovementMode = It->MovementMode;
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Level selector found: %s"), *GetNameSafe(*It));
		break;
	}
	const bool bUseForceSuspension = MovementMode == EABTSBirdMovementMode::ForceSuspension;
	RadialMovement->SetComponentTickEnabled(!bUseForceSuspension);
	ForceMovement->SetComponentTickEnabled(bUseForceSuspension);
	ResetRadialMovementState();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][MovementMode] Active=%s LegacyTick=%d ForceTick=%d"),
		bUseForceSuspension ? TEXT("ForceSuspension") : TEXT("LegacySweep"),
		RadialMovement->IsComponentTickEnabled() ? 1 : 0,
		ForceMovement->IsComponentTickEnabled() ? 1 : 0);
}

void AABTSM25BirdCharacter::ResetRadialMovementState()
{
	RadialMovement->ResetMotionState();
	ForceMovement->ResetMotionState();
}

void AABTSM25BirdCharacter::SetBirdIdentity(
	const EABTSBirdId InBirdId,
	const EABTSBirdSlingshotCapability InCapability,
	const bool bInPlayerControlled)
{
	BirdId = InBirdId;
	SlingshotCapability = InCapability;
	SetPartyControlled(bInPlayerControlled);
}

void AABTSM25BirdCharacter::SetPartyControlled(const bool bInPlayerControlled)
{
	bPartyControlled = bInPlayerControlled;
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
}

bool AABTSM25BirdCharacter::CanUseSlingshotCapability(const EABTSBirdSlingshotCapability RequiredCapability) const
{
	return SlingshotCapability == RequiredCapability;
}

void AABTSM25BirdCharacter::ApplyMoveInput(const FVector& Direction, const float Scale)
{
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->SetMoveInput(Direction, Scale);
	}
	else
	{
		RadialMovement->SetMoveInput(Direction, Scale);
	}
}

void AABTSM25BirdCharacter::ApplyPartyMoveInput(const FVector& Direction, const float Scale)
{
	if (bPartyControlled || Direction.IsNearlyZero() || FMath::IsNearlyZero(Scale)) return;
	GetSphericalSurface()->SetMovementFacing(Scale >= 0.0f ? Direction : -Direction);
	ApplyMoveInput(Direction, Scale);
}

void AABTSM25BirdCharacter::ApplyPartyJump()
{
	if (bPartyControlled) return;
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension) ForceMovement->QueueJump();
	else RadialMovement->QueueJump();
}

bool AABTSM25BirdCharacter::IsRadiallyGrounded() const
{
	return MovementMode == EABTSBirdMovementMode::ForceSuspension
		? ForceMovement->IsGrounded()
		: RadialMovement->IsGrounded();
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
	if (!FMath::IsNearlyZero(Value) && GetSphericalSurface()->IsSurfaceFrameReady())
	{
		const FVector Direction = GetSphericalSurface()->GetTangentForward();
		GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		if (bPartyControlled) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsRight(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && GetSphericalSurface()->IsSurfaceFrameReady())
	{
		const FVector Direction = GetSphericalSurface()->GetTangentRight();
		GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		if (bPartyControlled) ApplyMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::TurnWithRadialPhysics(const float Value)
{
	TurnOnSphere(Value);
}

void AABTSM25BirdCharacter::LookWithRadialPhysics(const float Value)
{
	LookOnSphere(Value);
}

void AABTSM25BirdCharacter::BeginRadialJump()
{
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Jump] Space input reached AABTSM25BirdCharacter. Mode=%s"),
		MovementMode == EABTSBirdMovementMode::ForceSuspension ? TEXT("ForceSuspension") : TEXT("LegacySweep"));
	if (!bPartyControlled) return;
	if (MovementMode == EABTSBirdMovementMode::ForceSuspension)
	{
		ForceMovement->QueueJump();
	}
	else
	{
		RadialMovement->QueueJump();
	}
}
