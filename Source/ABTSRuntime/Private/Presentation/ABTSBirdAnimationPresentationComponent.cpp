// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/ABTSBirdAnimationPresentationComponent.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

UABTSBirdAnimationPresentationComponent::UABTSBirdAnimationPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleAsset(TEXT("/Game/CuteBird/Animations/Cutebird_IdleA.Cutebird_IdleA"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> MoveAsset(TEXT("/Game/CuteBird/Animations/Cutebird_Move.Cutebird_Move"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> JumpAsset(TEXT("/Game/CuteBird/Animations/Cutebird_Jump.Cutebird_Jump"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> FlyAsset(TEXT("/Game/CuteBird/Animations/Cutebird_Fly.Cutebird_Fly"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> ImpactAsset(TEXT("/Game/CuteBird/Animations/Cutebird_Attack.Cutebird_Attack"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> DamageAsset(TEXT("/Game/CuteBird/Animations/Cutebird_Damage.Cutebird_Damage"));
	IdleAnimation = IdleAsset.Object;
	MoveAnimation = MoveAsset.Object;
	JumpAnimation = JumpAsset.Object;
	FlyAnimation = FlyAsset.Object;
	ImpactAnimation = ImpactAsset.Object;
	DamageAnimation = DamageAsset.Object;
}

void UABTSBirdAnimationPresentationComponent::InitializePresentation(
	USkeletalMeshComponent* InVisual,
	const bool bInitiallyGrounded)
{
	BirdVisual = InVisual;
	bWasGrounded = bInitiallyGrounded;
	LocomotionState = bInitiallyGrounded ? ELocomotionState::Idle : ELocomotionState::Fly;
	StateElapsedSeconds = 0.0f;
	ActionElapsedSeconds = 0.0f;
	PendingAction.Reset();
	ActiveAnimation = nullptr;
	if (BirdVisual)
	{
		BirdVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
}

void UABTSBirdAnimationPresentationComponent::UpdatePresentation(
	const FABTSBirdAnimationSnapshot& Snapshot,
	const float DeltaSeconds)
{
	if (!BirdVisual || !BirdVisual->GetSkeletalMeshAsset()) return;

	if (PendingAction.IsSet())
	{
		UAnimSequence* ActionAnimation = ResolveActionAnimation(PendingAction.GetValue());
		if (ActionAnimation)
		{
			ActionElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
			PlayAnimation(ActionAnimation, false);
			if (ActionElapsedSeconds < ActionAnimation->GetPlayLength())
			{
				bWasGrounded = Snapshot.bGrounded;
				return;
			}
		}
		PendingAction.Reset();
		ActionElapsedSeconds = 0.0f;
	}

	const bool bStartedJump = bWasGrounded && !Snapshot.bGrounded && !Snapshot.bForceFlight;
	if (bStartedJump)
	{
		LocomotionState = ELocomotionState::Jump;
		StateElapsedSeconds = 0.0f;
	}
	else if (Snapshot.bForceFlight)
	{
		LocomotionState = ELocomotionState::Fly;
	}
	else if (!Snapshot.bGrounded && LocomotionState == ELocomotionState::Jump)
	{
		StateElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
		const float JumpDuration = JumpAnimation ? JumpAnimation->GetPlayLength() : 0.0f;
		if (StateElapsedSeconds >= JumpDuration) LocomotionState = ELocomotionState::Fly;
	}
	else if (!Snapshot.bGrounded)
	{
		LocomotionState = ELocomotionState::Fly;
	}
	else
	{
		LocomotionState = Snapshot.TangentialSpeedCMPerSecond > 10.0f
			? ELocomotionState::Move
			: ELocomotionState::Idle;
	}

	switch (LocomotionState)
	{
	case ELocomotionState::Move:
		PlayAnimation(MoveAnimation, true,
			FMath::Clamp(Snapshot.TangentialSpeedCMPerSecond / 300.0f, 0.7f, 1.6f));
		break;
	case ELocomotionState::Jump:
		PlayAnimation(JumpAnimation, false);
		break;
	case ELocomotionState::Fly:
		PlayAnimation(FlyAnimation, true);
		break;
	default:
		PlayAnimation(IdleAnimation, true);
		break;
	}

	bWasGrounded = Snapshot.bGrounded;
}

void UABTSBirdAnimationPresentationComponent::RequestAction(const EABTSBirdPresentationAction Action)
{
	if (!ResolveActionAnimation(Action)) return;
	PendingAction = Action;
	ActionElapsedSeconds = 0.0f;
	ActiveAnimation = nullptr;
}

void UABTSBirdAnimationPresentationComponent::PlayAnimation(
	UAnimSequence* Animation,
	const bool bLooping,
	const float PlayRate)
{
	if (!BirdVisual || !Animation) return;
	if (ActiveAnimation != Animation)
	{
		BirdVisual->PlayAnimation(Animation, bLooping);
		ActiveAnimation = Animation;
	}
	if (UAnimSingleNodeInstance* SingleNode = BirdVisual->GetSingleNodeInstance())
	{
		SingleNode->SetPlayRate(PlayRate);
	}
}

UAnimSequence* UABTSBirdAnimationPresentationComponent::ResolveActionAnimation(
	const EABTSBirdPresentationAction Action) const
{
	return Action == EABTSBirdPresentationAction::Impact ? ImpactAnimation : DamageAnimation;
}
