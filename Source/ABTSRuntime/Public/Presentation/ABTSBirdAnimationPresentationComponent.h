// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "ABTSBirdAnimationPresentationComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/** A short, presentation-only overlay. It never changes Gameplay or physics state. */
UENUM(BlueprintType)
enum class EABTSBirdPresentationAction : uint8
{
	Impact UMETA(DisplayName = "Impact / Attack"),
	Damage UMETA(DisplayName = "Damage Reaction")
};

/** Read-only facts copied from Gameplay/physics for one presentation update. */
struct FABTSBirdAnimationSnapshot
{
	bool bGrounded = true;
	bool bForceFlight = false;
	float TangentialSpeedCMPerSecond = 0.0f;
};

/**
 * Code-driven CuteBird animation player. Gameplay supplies snapshots and optional
 * presentation requests; this component only changes the skeletal mesh pose.
 */
UCLASS(ClassGroup = (ABTS), NotBlueprintable, Transient)
class ABTSRUNTIME_API UABTSBirdAnimationPresentationComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UABTSBirdAnimationPresentationComponent();

	void InitializePresentation(USkeletalMeshComponent* InVisual, bool bInitiallyGrounded);
	void UpdatePresentation(const FABTSBirdAnimationSnapshot& Snapshot, float DeltaSeconds);
	void RequestAction(EABTSBirdPresentationAction Action);

private:
	enum class ELocomotionState : uint8
	{
		Idle,
		Move,
		Jump,
		Fly
	};

	void PlayAnimation(UAnimSequence* Animation, bool bLooping, float PlayRate = 1.0f);
	UAnimSequence* ResolveActionAnimation(EABTSBirdPresentationAction Action) const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BirdVisual;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> MoveAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> JumpAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FlyAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ImpactAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> DamageAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveAnimation;

	ELocomotionState LocomotionState = ELocomotionState::Idle;
	bool bWasGrounded = true;
	float StateElapsedSeconds = 0.0f;
	float ActionElapsedSeconds = 0.0f;
	TOptional<EABTSBirdPresentationAction> PendingAction;
};
