// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionSystem.h"

#include "ABTSRuntime.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
	bool SameInteractionInput(
		const FABTSM11FinaleLaunchInput& A,
		const FABTSM11FinaleLaunchInput& B)
	{
		return A.YawDegrees == B.YawDegrees
			&& A.PitchDegrees == B.PitchDegrees
			&& A.Power == B.Power;
	}

	bool SameTrustIdentity(
		const FABTSM11PrefixTrustRegion& A,
		const FABTSM11PrefixTrustRegion& B)
	{
		return A.PrefixLevel == B.PrefixLevel
			&& A.RegionHash == B.RegionHash
			&& A.Minimum.YawDegrees == B.Minimum.YawDegrees
			&& A.Minimum.PitchDegrees == B.Minimum.PitchDegrees
			&& A.Minimum.Power == B.Minimum.Power
			&& A.Maximum.YawDegrees == B.Maximum.YawDegrees
			&& A.Maximum.PitchDegrees == B.Maximum.PitchDegrees
			&& A.Maximum.Power == B.Maximum.Power
			&& A.CaptureMarginCells == B.CaptureMarginCells
			&& A.ReleaseMarginCells == B.ReleaseMarginCells;
	}

	FRotator MakeFlightRotation(
		const FVector& Forward,
		const FVector& PreferredUp)
	{
		FVector SafeForward = Forward.GetSafeNormal();
		if (SafeForward.IsNearlyZero())
		{
			SafeForward = FVector::ForwardVector;
		}
		FVector SafeUp = PreferredUp.GetSafeNormal();
		if (FMath::Abs(FVector::DotProduct(SafeForward, SafeUp)) > 0.98f)
		{
			SafeUp = FVector::RightVector;
		}
		return FRotationMatrix::MakeFromXZ(SafeForward, SafeUp).Rotator();
	}
}

AABTSM11FinaleInteractionSystem::AABTSM11FinaleInteractionSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	TargetPreviewCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(
		TEXT("TargetPreviewCapture"));
	TargetPreviewCapture->SetupAttachment(SceneRoot);
	TargetPreviewCapture->bCaptureEveryFrame = false;
	TargetPreviewCapture->bCaptureOnMovement = false;
	TargetPreviewCapture->FOVAngle = 42.0f;
	TargetPreviewCapture->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	TargetPreviewCapture->CaptureSource =
		ESceneCaptureSource::SCS_FinalColorLDR;
}

bool AABTSM11FinaleInteractionSystem::Initialize(
	AABTSM11FinaleSystem& InFinaleSystem,
	AABTSBirdParty& InParty)
{
	FString Failure;
	if (!ValidateInteractionContract(InFinaleSystem, &Failure)
		|| !InParty.IsPartyReady())
	{
		FailInteraction(Failure.IsEmpty()
			? TEXT("PartyNotReady")
			: Failure);
		return false;
	}
	FinaleSystem = &InFinaleSystem;
	Party = &InParty;
	TargetPreviewRenderTarget = NewObject<UTextureRenderTarget2D>(
		this,
		TEXT("M11TargetPreviewRT"));
	if (TargetPreviewRenderTarget == nullptr)
	{
		FailInteraction(TEXT("TargetPreviewRenderTargetAllocationFailed"));
		return false;
	}
	TargetPreviewRenderTarget->ClearColor =
		FLinearColor(0.005f, 0.012f, 0.025f, 1.0f);
	TargetPreviewRenderTarget->RenderTargetFormat =
		ETextureRenderTargetFormat::RTF_RGBA8;
	TargetPreviewRenderTarget->InitAutoFormat(
		FMath::Max(64, TargetPreviewWidth),
		FMath::Max(64, TargetPreviewHeight));
	TargetPreviewRenderTarget->UpdateResourceImmediate(true);
	TargetPreviewCapture->TextureTarget = TargetPreviewRenderTarget;

	const FABTSM11FinaleLaunchModel& LaunchModel =
		FinaleSystem->GetLayoutPreset().LaunchModel;
	FABTSM11FinaleLaunchInput InitialInput;
	InitialInput.YawDegrees =
		(LaunchModel.MinimumYawDegrees
			+ LaunchModel.MaximumYawDegrees) * 0.5;
	InitialInput.PitchDegrees = FMath::Clamp(
		InitialPitchDegrees,
		LaunchModel.MinimumPitchDegrees,
		LaunchModel.MaximumPitchDegrees);
	InitialInput.Power = FMath::Clamp(
		InitialPower,
		LaunchModel.MinimumPower,
		LaunchModel.MaximumPower);
	if (!Stabilizer.Initialize(
		FinaleSystem->GetLayoutPreset(),
		InitialInput))
	{
		FailInteraction(TEXT("StabilizerInitializationFailed"));
		return false;
	}

	RuntimeFailure.Reset();
	InteractionState = EABTSM11FinaleInteractionState::Ready;
	QueueNominalPhysicalSolve();
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Interaction] Ready Bundle=0x%016llx SlotPair=%d"),
		FinaleSystem->GetLayoutPreset().CertifiedBundleHash,
		FinaleSystem->GetFinaleFrame().SlotPairId);
	return true;
}

bool AABTSM11FinaleInteractionSystem::TryEnterFinale(
	AABTSM51SlingshotCord& Cord,
	APlayerController& Controller)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Ready
		|| !IsValid(FinaleSystem)
		|| !IsValid(Party)
		|| !Cord.IsFinaleSpaceSlingshot()
		|| Cord.GetFinaleSlotPairId()
			!= FinaleSystem->GetFinaleFrame().SlotPairId)
	{
		return false;
	}
	AABTSM25BirdCharacter* Bird = Party->GetControlledBird();
	if (!IsValid(Bird))
	{
		FailInteraction(TEXT("ControlledBirdMissing"));
		return false;
	}

	AttemptBird = Bird;
	ActiveCord = &Cord;
	AttemptBirdOriginalTransform = Bird->GetActorTransform();
	Party->SetSlingshotMode(true);
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	const FABTSM11FinaleLayoutPreset& Preset =
		FinaleSystem->GetLayoutPreset();
	const FVector PouchWorld = Frame.TransformLocalPosition(
		FVector(Preset.LaunchModel.PouchLocalPositionCM));
	const FVector LocalDirection = FVector(
		Preset.LaunchModel.MapDirection(
			Stabilizer.GetControlledInput()));
	const FVector WorldDirection =
		Frame.WorldTransform.TransformVectorNoScale(LocalDirection);
	const FQuat PouchRotation = MakeFlightRotation(
		WorldDirection,
		Frame.GetUp()).Quaternion();
	Bird->EnterSlingshotPouch(PouchWorld, PouchRotation);
	bAttemptBirdInPouch = true;

	TInlineComponentArray<UActorComponent*> BirdComponents;
	Bird->GetComponents(BirdComponents);
	for (UActorComponent* Component : BirdComponents)
	{
		if (Component != nullptr
			&& Component->PrimaryComponentTick.bCanEverTick)
		{
			PrimaryActorTick.AddPrerequisite(
				Component,
				Component->PrimaryComponentTick);
		}
	}

	InteractionState = EABTSM11FinaleInteractionState::Aiming;
	PlaybackElapsedSeconds = 0.0;
	++AimRevision;
	bPreviewDirty = true;
	PreviewSubmitAccumulatorSeconds = PreviewSubmitIntervalSeconds;
	TargetSelector.Reset();
	UpdatePouchPresentation();
	QueuePreviewSolveIfNeeded();
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Aim] Entered Pair=%d Bird=%s"),
		Cord.GetFinaleSlotPairId(),
		*GetNameSafe(Bird));
	return true;
}

void AABTSM11FinaleInteractionSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PreviewSubmitAccumulatorSeconds += FMath::Max(0.0f, DeltaSeconds);
	switch (InteractionState)
	{
	case EABTSM11FinaleInteractionState::Aiming:
	case EABTSM11FinaleInteractionState::ReleasePending:
		UpdateAiming(DeltaSeconds);
		break;
	case EABTSM11FinaleInteractionState::Launched:
		UpdatePlayback(DeltaSeconds);
		break;
	default:
		break;
	}
}

void AABTSM11FinaleInteractionSystem::ApplyAimAxis(
	const double YawAxisDelta,
	const double PitchAxisDelta,
	const double PowerAxisDelta)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming)
	{
		return;
	}
	const FABTSM11FinaleLaunchInput Before =
		Stabilizer.GetControlledInput();
	Stabilizer.ApplyInputDelta(
		YawAxisDelta * YawDegreesPerAxisUnit,
		PitchAxisDelta * PitchDegreesPerAxisUnit,
		PowerAxisDelta * PowerPerWheelUnit);
	if (!SameInteractionInput(
			Before,
			Stabilizer.GetControlledInput()))
	{
		++AimRevision;
		bPreviewDirty = true;
		UpdatePouchPresentation();
	}
}

void AABTSM11FinaleInteractionSystem::RequestRelease()
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming)
	{
		return;
	}
	FrozenReleaseInput = Stabilizer.GetControlledInput();
	InteractionState = EABTSM11FinaleInteractionState::ReleasePending;
	if (!FinalizePendingRelease())
	{
		bPreviewDirty = true;
		PreviewSubmitAccumulatorSeconds = PreviewSubmitIntervalSeconds;
		QueuePreviewSolveIfNeeded();
	}
}

void AABTSM11FinaleInteractionSystem::CancelStabilizerOrResetAttempt()
{
	if (InteractionState == EABTSM11FinaleInteractionState::Aiming)
	{
		Stabilizer.CancelProtection();
		return;
	}
	if (InteractionState == EABTSM11FinaleInteractionState::ReleasePending)
	{
		InteractionState = EABTSM11FinaleInteractionState::Aiming;
		return;
	}
	if (InteractionState == EABTSM11FinaleInteractionState::Failed)
	{
		RestoreAttemptToWorld(true);
	}
}

void AABTSM11FinaleInteractionSystem::ExitFinale()
{
	if (!IsFinaleActive())
	{
		return;
	}
	RestoreAttemptToWorld(false);
	InteractionState = EABTSM11FinaleInteractionState::Ready;
}

bool AABTSM11FinaleInteractionSystem::IsFinaleActive() const
{
	return InteractionState == EABTSM11FinaleInteractionState::Aiming
		|| InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending
		|| InteractionState == EABTSM11FinaleInteractionState::Launched
		|| InteractionState == EABTSM11FinaleInteractionState::TargetHit
		|| InteractionState == EABTSM11FinaleInteractionState::Failed;
}

bool AABTSM11FinaleInteractionSystem::IsAiming() const
{
	return InteractionState == EABTSM11FinaleInteractionState::Aiming;
}

bool AABTSM11FinaleInteractionSystem::IsReleasePending() const
{
	return InteractionState
		== EABTSM11FinaleInteractionState::ReleasePending;
}

const FABTSM11TrajectoryResult*
AABTSM11FinaleInteractionSystem::GetCurrentPrediction() const
{
	return LatestQualifiedResult.ValidationHash != 0
		? &LatestQualifiedResult
		: nullptr;
}

bool AABTSM11FinaleInteractionSystem::ValidateInteractionContract(
	const AABTSM11FinaleSystem& InFinaleSystem,
	FString* OutFailure)
{
	const auto Reject = [OutFailure](const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	};
	if (!InFinaleSystem.IsLayoutReady()
		|| !InFinaleSystem.GetFinaleFrame().IsUsable())
	{
		return Reject(TEXT("FinaleSystemNotReady"));
	}
	const FABTSM11FinaleLayoutPreset& Candidate =
		InFinaleSystem.GetLayoutPreset();
	const FABTSM11FinaleLayoutPreset Frozen =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	if (!Candidate.IsValid())
	{
		return Reject(TEXT("InvalidCertifiedPreset"));
	}
	if (Candidate.PresetVersion != Frozen.PresetVersion
		|| Candidate.CompatibleGeneratorVersion
			!= Frozen.CompatibleGeneratorVersion
		|| Candidate.CompatibleFrameLayoutVersion
			!= Frozen.CompatibleFrameLayoutVersion
		|| Candidate.LaunchModel.LaunchModelVersion
			!= Frozen.LaunchModel.LaunchModelVersion
		|| Candidate.SolverConfig.SolverVersion
			!= Frozen.SolverConfig.SolverVersion
		|| Candidate.SolverConfig.HashSchemaVersion
			!= Frozen.SolverConfig.HashSchemaVersion
		|| Candidate.ScanContract.ScanContractVersion
			!= Frozen.ScanContract.ScanContractVersion
		|| Candidate.PhysicalPlaybackContractVersion
			!= Frozen.PhysicalPlaybackContractVersion)
	{
		return Reject(TEXT("UnsupportedM11CContractVersion"));
	}
	if (Candidate.PresetSourceHash != Frozen.PresetSourceHash
		|| Candidate.PresetHash != Frozen.PresetHash
		|| Candidate.CanonicalScenario.ScenarioHash
			!= Frozen.CanonicalScenario.ScenarioHash
		|| Candidate.ScanContractHash != Frozen.ScanContractHash
		|| Candidate.CertificationHash != Frozen.CertificationHash
		|| Candidate.NominalTrajectoryHash
			!= Frozen.NominalTrajectoryHash
		|| Candidate.PhysicalPlaybackTrajectoryHash
			!= Frozen.PhysicalPlaybackTrajectoryHash
		|| Candidate.CertifiedBundleHash
			!= Frozen.CertifiedBundleHash)
	{
		return Reject(TEXT("M11CIdentityMismatch"));
	}
	for (int32 Index = 0; Index < Candidate.PrefixTrustRegions.Num(); ++Index)
	{
		if (!SameTrustIdentity(
			Candidate.PrefixTrustRegions[Index],
			Frozen.PrefixTrustRegions[Index]))
		{
			return Reject(TEXT("M11CTrustRegionIdentityMismatch"));
		}
	}
	return true;
}

void AABTSM11FinaleInteractionSystem::UpdateAiming(
	const float DeltaSeconds)
{
	if (DoesInputMatchLatestSolve())
	{
		const FABTSM11FinaleLaunchInput Before =
			Stabilizer.GetControlledInput();
		Stabilizer.Update(DeltaSeconds, CurrentClassification);
		if (!SameInteractionInput(
				Before,
				Stabilizer.GetControlledInput()))
		{
			++AimRevision;
			bPreviewDirty = true;
		}
		const EABTSM11PreviewTarget PreviousTarget =
			PreviewSelection.Target;
		const FABTSM11TrajectoryResult& TargetSelectionResult =
			bLatestPhysicalResultAvailable
				&& (CurrentClassification.ValidAssistMask & 0x7u)
					== 0x7u
			? LatestSameInputPhysicalResult
			: LatestQualifiedResult;
		PreviewSelection = TargetSelector.Update(
			DeltaSeconds,
			FinaleSystem->GetLayoutPreset(),
			TargetSelectionResult,
			CurrentClassification);
		if (PreviewSelection.Target != PreviousTarget)
		{
			UpdateTargetCapture();
		}
	}
	UpdatePouchPresentation();
	QueuePreviewSolveIfNeeded();
}

void AABTSM11FinaleInteractionSystem::UpdatePlayback(
	const float DeltaSeconds)
{
	if (!IsValid(FinaleSystem)
		|| !IsValid(AttemptBird)
		|| ReleasedPlaybackPlan.Points.IsEmpty())
	{
		FailInteraction(TEXT("PlaybackDependencyLost"));
		return;
	}
	PlaybackElapsedSeconds = FMath::Min(
		ReleasedPlaybackPlan.DurationSeconds,
		PlaybackElapsedSeconds
			+ FMath::Max(0.0f, DeltaSeconds) * PlaybackTimeScale);
	FVector3d LocalPosition;
	FVector3d LocalVelocity;
	if (!ReleasedPlaybackPlan.Sample(
		PlaybackElapsedSeconds,
		LocalPosition,
		LocalVelocity))
	{
		FailInteraction(TEXT("PlaybackSamplingFailed"));
		return;
	}
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	const FVector WorldPosition = Frame.TransformLocalPosition(
		FVector(LocalPosition));
	const FVector WorldVelocity =
		Frame.WorldTransform.TransformVectorNoScale(
			FVector(LocalVelocity));
	AttemptBird->SetActorLocationAndRotation(
		WorldPosition,
		MakeFlightRotation(WorldVelocity, Frame.GetUp()),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (PlaybackElapsedSeconds
		>= ReleasedPlaybackPlan.DurationSeconds - 1.0e-9)
	{
		if (ReleasedPlaybackPlan.bPhysicalTargetHit)
		{
			InteractionState =
				EABTSM11FinaleInteractionState::TargetHit;
			UE_LOG(
				LogABTSRuntime,
				Log,
				TEXT("[ABTS][M11-C][Playback] PhysicalTargetHit Plan=0x%016llx Transfer=%d"),
				ReleasedPlaybackPlan.PlanHash,
				ReleasedPlaybackPlan.bUsesVisibleTerminalTransfer ? 1 : 0);
		}
		else
		{
			InteractionState =
				EABTSM11FinaleInteractionState::Failed;
			RuntimeFailure = ABTSM11FailureReasonLabel(
				ABTSM11ClassifyFailure(
					LatestQualifiedResult,
					CurrentClassification));
		}
	}
}

void AABTSM11FinaleInteractionSystem::UpdatePouchPresentation()
{
	if (!IsValid(FinaleSystem)
		|| !IsValid(ActiveCord)
		|| !IsValid(AttemptBird)
		|| !bAttemptBirdInPouch
		|| (InteractionState != EABTSM11FinaleInteractionState::Aiming
			&& InteractionState
				!= EABTSM11FinaleInteractionState::ReleasePending))
	{
		return;
	}
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	const FABTSM11FinaleLayoutPreset& Preset =
		FinaleSystem->GetLayoutPreset();
	const FVector WorldPosition = Frame.TransformLocalPosition(
		FVector(Preset.LaunchModel.PouchLocalPositionCM));
	const FVector LocalDirection = FVector(
		Preset.LaunchModel.MapDirection(
			Stabilizer.GetControlledInput()));
	const FVector WorldDirection =
		Frame.WorldTransform.TransformVectorNoScale(LocalDirection);
	const FQuat WorldRotation = MakeFlightRotation(
		WorldDirection,
		Frame.GetUp()).Quaternion();
	ActiveCord->UpdatePulledPouchVisual(
		WorldPosition,
		WorldRotation);
	AttemptBird->SetActorLocationAndRotation(
		WorldPosition,
		WorldRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AABTSM11FinaleInteractionSystem::UpdateTargetCapture()
{
	if (!IsValid(TargetPreviewCapture)
		|| !IsValid(TargetPreviewRenderTarget)
		|| !IsValid(FinaleSystem))
	{
		return;
	}
	AActor* TargetActor = ResolvePreviewTargetActor(
		PreviewSelection.Target);
	if (!IsValid(TargetActor))
	{
		return;
	}
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	const FVector TargetWorld = Frame.TransformLocalPosition(
		FVector(PreviewSelection.TargetCenterCM));
	FVector IncomingWorld =
		Frame.WorldTransform.TransformVectorNoScale(
			FVector(PreviewSelection.IncomingDirection)).GetSafeNormal();
	if (IncomingWorld.IsNearlyZero())
	{
		IncomingWorld = Frame.GetForward();
	}
	double VisualRadius = PreviewSelection.TargetRadiusCM;
	const int32 AssistIndex =
		static_cast<int32>(PreviewSelection.Target) + 1;
	if (AssistIndex >= 1
		&& AssistIndex <= FABTSM11GravityScenario::AssistCount)
	{
		VisualRadius = FinaleSystem->GetLayoutPreset()
			.CanonicalScenario.GetAssist(AssistIndex).VisualRadiusCM;
	}
	const double CameraDistance = FMath::Max(
		2500.0,
		VisualRadius * 5.0);
	const FVector CameraLocation =
		TargetWorld - IncomingWorld * CameraDistance
		+ Frame.GetUp() * (CameraDistance * 0.12);
	TargetPreviewCapture->SetWorldLocationAndRotation(
		CameraLocation,
		MakeFlightRotation(
			TargetWorld - CameraLocation,
			Frame.GetUp()));
	TargetPreviewCapture->ClearShowOnlyComponents();
	TargetPreviewCapture->ShowOnlyActorComponents(TargetActor);
	TargetPreviewCapture->CaptureScene();
}

void AABTSM11FinaleInteractionSystem::RestoreAttemptToWorld(
	const bool bKeepFinaleMode)
{
	if (IsValid(AttemptBird) && bAttemptBirdInPouch)
	{
		AttemptBird->FinishSlingshotReturn();
		AttemptBird->SetActorTransform(
			AttemptBirdOriginalTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		bAttemptBirdInPouch = false;
	}
	if (IsValid(ActiveCord))
	{
		ActiveCord->ResetPouchVisualToRest();
	}
	if (IsValid(Party))
	{
		Party->SetSlingshotMode(false);
	}
	if (!bKeepFinaleMode
		|| !IsValid(AttemptBird)
		|| !IsValid(ActiveCord)
		|| !IsValid(Party)
		|| !IsValid(FinaleSystem))
	{
		AttemptBird = nullptr;
		ActiveCord = nullptr;
		return;
	}

	Party->SetSlingshotMode(true);
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	const FABTSM11FinaleLayoutPreset& Preset =
		FinaleSystem->GetLayoutPreset();
	const FVector PouchWorld = Frame.TransformLocalPosition(
		FVector(Preset.LaunchModel.PouchLocalPositionCM));
	AttemptBird->EnterSlingshotPouch(
		PouchWorld,
		MakeFlightRotation(Frame.GetForward(), Frame.GetUp()).Quaternion());
	bAttemptBirdInPouch = true;
	Stabilizer.Reset(Stabilizer.GetControlledInput());
	ReleasedPlaybackPlan.Reset();
	PlaybackElapsedSeconds = 0.0;
	RuntimeFailure.Reset();
	InteractionState = EABTSM11FinaleInteractionState::Aiming;
	UpdatePouchPresentation();
}

void AABTSM11FinaleInteractionSystem::FailInteraction(
	const FString& Reason)
{
	RuntimeFailure = Reason;
	InteractionState = EABTSM11FinaleInteractionState::Failed;
	UE_LOG(
		LogABTSRuntime,
		Error,
		TEXT("[ABTS][M11-C][Interaction] Failed Reason=%s"),
		*Reason);
}

bool AABTSM11FinaleInteractionSystem::DoesInputMatchLatestSolve() const
{
	return LatestSolvedRevision == AimRevision
		&& SameInteractionInput(
			LatestSolvedInput,
			Stabilizer.GetControlledInput())
		&& LatestQualifiedResult.ValidationHash != 0;
}

AActor* AABTSM11FinaleInteractionSystem::ResolvePreviewTargetActor(
	const EABTSM11PreviewTarget Target) const
{
	if (!IsValid(FinaleSystem))
	{
		return nullptr;
	}
	if (Target == EABTSM11PreviewTarget::UFO)
	{
		return FinaleSystem->GetUFOActor();
	}
	const int32 BodyId = FinaleSystem->GetLayoutPreset()
		.CanonicalScenario.GetAssist(
			static_cast<int32>(Target) + 1).BodyId;
	for (AABTSM11GravityBodyActor* Actor
		: FinaleSystem->GetGravityBodyActors())
	{
		if (IsValid(Actor) && Actor->GetStableBodyId() == BodyId)
		{
			return Actor;
		}
	}
	return nullptr;
}

void AABTSM11FinaleInteractionSystem::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (IsFinaleActive())
	{
		RestoreAttemptToWorld(false);
	}
	Super::EndPlay(EndPlayReason);
}
