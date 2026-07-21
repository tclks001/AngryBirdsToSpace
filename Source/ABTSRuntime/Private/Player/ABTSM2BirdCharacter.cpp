// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM2BirdCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Planet/ABTSM2SphericalSurfaceComponent.h"

AABTSM2BirdCharacter::AABTSM2BirdCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	GetCharacterMovement()->GravityScale = 0.0f;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	SphericalSurface = CreateDefaultSubobject<UABTSM2SphericalSurfaceComponent>(TEXT("SphericalSurface"));
}

void AABTSM2BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	SphericalSurface->SetSurfaceOffsetCM(GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

void AABTSM2BirdCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SphericalSurface->UpdateSurfaceFrame();
}

void AABTSM2BirdCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveForward"), this, &AABTSM2BirdCharacter::MoveOnSphereForward);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveRight"), this, &AABTSM2BirdCharacter::MoveOnSphereRight);
	PlayerInputComponent->BindAxis(TEXT("ABTS_Turn"), this, &AABTSM2BirdCharacter::TurnOnSphere);
	PlayerInputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &AABTSM2BirdCharacter::LookOnSphere);
}

void AABTSM2BirdCharacter::MoveOnSphereForward(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && SphericalSurface->IsSurfaceFrameReady())
	{
		const FVector Direction = SphericalSurface->GetTangentForward();
		SphericalSurface->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		AddMovementInput(Direction, Value);
	}
}

void AABTSM2BirdCharacter::MoveOnSphereRight(const float Value)
{
	if (!FMath::IsNearlyZero(Value) && SphericalSurface->IsSurfaceFrameReady())
	{
		const FVector Direction = SphericalSurface->GetTangentRight();
		SphericalSurface->SetMovementFacing(Value >= 0.0f ? Direction : -Direction);
		AddMovementInput(Direction, Value);
	}
}

void AABTSM2BirdCharacter::TurnOnSphere(const float Value)
{
	SphericalSurface->AddCameraYaw(Value);
}

void AABTSM2BirdCharacter::LookOnSphere(const float Value)
{
	SphericalSurface->AddCameraPitch(Value);
}
