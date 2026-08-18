// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/ABTSM1BirdCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float DefaultGroundMovementSpeedMultiplier = 2.0f;
}

AABTSM1BirdCharacter::AABTSM1BirdCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 60.0f);
	GetCharacterMovement()->MaxWalkSpeed = 620.0f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	BirdVisual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BirdVisual"));
	BirdVisual->SetupAttachment(GetCapsuleComponent());
	BirdVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BirdVisual->SetGenerateOverlapEvents(false);
	BirdVisual->SetSimulatePhysics(false);
	BirdVisual->SetCanEverAffectNavigation(false);
	BirdVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CuteBirdMesh(TEXT("/Game/CuteBird/Meshes/SM_Cute_Bird.SM_Cute_Bird"));
	if (CuteBirdMesh.Succeeded()) BirdVisual->SetSkeletalMesh(CuteBirdMesh.Object);
	ApplyBirdVisualTransform();

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 460.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 90.0f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void AABTSM1BirdCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed *=
		DefaultGroundMovementSpeedMultiplier;
	ApplyBirdVisualTransform();
}

void AABTSM1BirdCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyBirdVisualTransform();
}

void AABTSM1BirdCharacter::ApplyBirdVisualTransform()
{
	if (BirdVisual == nullptr) return;
	BirdVisual->SetRelativeLocation(BirdVisualRelativeLocation);
	BirdVisual->SetRelativeRotation(BirdVisualRelativeRotation);
	BirdVisual->SetRelativeScale3D(BirdVisualRelativeScale.ComponentMax(FVector(0.01f)));
}

void AABTSM1BirdCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveForward"), this, &AABTSM1BirdCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("ABTS_MoveRight"), this, &AABTSM1BirdCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("ABTS_Turn"), this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis(TEXT("ABTS_LookUp"), this, &APawn::AddControllerPitchInput);
}

void AABTSM1BirdCharacter::MoveForward(const float Value)
{
	if (Controller != nullptr && !FMath::IsNearlyZero(Value))
	{
		const FRotator ControlRotation = Controller->GetControlRotation();
		const FVector Forward = FRotationMatrix(FRotator(0.0f, ControlRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X);
		AddMovementInput(Forward, Value);
	}
}

void AABTSM1BirdCharacter::MoveRight(const float Value)
{
	if (Controller != nullptr && !FMath::IsNearlyZero(Value))
	{
		const FRotator ControlRotation = Controller->GetControlRotation();
		const FVector Right = FRotationMatrix(FRotator(0.0f, ControlRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
		AddMovementInput(Right, Value);
	}
}
