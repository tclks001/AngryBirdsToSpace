// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM25BirdCharacter.h"

#include "ABTSRuntime.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/ABTSM25RadialMovementComponent.h"
#include "Planet/ABTSM2SphericalSurfaceComponent.h"

AABTSM25BirdCharacter::AABTSM25BirdCharacter()
{
	RadialMovement = CreateDefaultSubobject<UABTSM25RadialMovementComponent>(TEXT("RadialMovement"));
}

void AABTSM25BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->DisableMovement();
	GetSphericalSurface()->SetProjectToBaseSurface(false);
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
		RadialMovement->SetMoveInput(Direction, Value);
	}
}

void AABTSM25BirdCharacter::MoveWithRadialPhysicsRight(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && GetSphericalSurface()->IsSurfaceFrameReady())
	{
		const FVector Direction = GetSphericalSurface()->GetTangentRight();
		GetSphericalSurface()->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		RadialMovement->SetMoveInput(Direction, Value);
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
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M2.5][Jump] Space input reached AABTSM25BirdCharacter."));
	RadialMovement->QueueJump();
}
