// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionSystem.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM11FinaleFlightCamera.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "UI/ABTSM11FinalePresentation.h"
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

	FQuat MakeM11PouchRotation(
		const FVector& LaunchDirection,
		const FVector& PreferredRight)
	{
		const FVector PouchForwardZ = LaunchDirection.GetSafeNormal();
		FVector PouchSideY = FVector::VectorPlaneProject(
			PreferredRight,
			PouchForwardZ).GetSafeNormal();
		if (PouchSideY.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(PouchForwardZ.Z) < 0.9f
				? FVector::UpVector
				: FVector::ForwardVector;
			PouchSideY = FVector::CrossProduct(
				PouchForwardZ,
				FallbackAxis).GetSafeNormal();
		}
		return FRotationMatrix::MakeFromYZ(
			PouchSideY,
			PouchForwardZ).ToQuat();
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
	TargetPreviewCapture->FOVAngle = static_cast<float>(
		ABTSM11FinaleTargetPreviewFOVDegrees);
	TargetPreviewCapture->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	TargetPreviewCapture->CaptureSource =
		ESceneCaptureSource::SCS_FinalColorLDR;
}

const AActor*
AABTSM11FinaleInteractionSystem::GetFinaleRemotePreviewCaptureOwner() const
{
	return TargetPreviewCapture != nullptr
		? TargetPreviewCapture->GetOwner()
		: nullptr;
}

const USceneCaptureComponent2D*
AABTSM11FinaleInteractionSystem::GetFinaleRemotePreviewCaptureComponent() const
{
	return TargetPreviewCapture;
}

EABTSStylizedViewClass
AABTSM11FinaleInteractionSystem::GetFinaleRemotePreviewStylizedViewClass() const
{
	return EABTSStylizedViewClass::FinaleRemotePreview;
}

bool AABTSM11FinaleInteractionSystem::Initialize(
	AABTSM11FinaleSystem& InFinaleSystem,
	AABTSBirdParty& InParty,
	TSubclassOf<AABTSM6SlingshotCamera> InAimCameraClass)
{
	FString Failure;
	if (!ValidateInteractionContract(InFinaleSystem, &Failure)
		|| !InParty.IsPartyReady()
		|| !InAimCameraClass)
	{
		if (Failure.IsEmpty())
		{
			Failure = !InParty.IsPartyReady()
				? TEXT("PartyNotReady")
				: TEXT("AimCameraClassMissing");
		}
		FailInteraction(Failure);
		return false;
	}
	FinaleSystem = &InFinaleSystem;
	Party = &InParty;
	AimCameraClass = InAimCameraClass;
	if (!EnsureAimCamera())
	{
		FailInteraction(TEXT("AimCameraSpawnFailed"));
		return false;
	}
	if (!EnsureFlightCamera())
	{
		FailInteraction(TEXT("FlightCameraSpawnFailed"));
		return false;
	}
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
	InitialAimInput = InitialInput;
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
		|| !EnsureAimCamera()
		|| !EnsureFlightCamera()
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
	if (!BuildAimFrame(Cord, *Bird))
	{
		FailInteraction(TEXT("InvalidFinaleAimFrame"));
		return false;
	}
	AimCamera->SetAimFrame(
		AimSlingCenter,
		AimSlingForward,
		AimSlingUp);
	FlightCamera->ResetAuthorityFollow();
	ActiveFinaleController = &Controller;
	Controller.SetViewTarget(AimCamera);

	AttemptBird = Bird;
	ActiveCord = &Cord;
	AttemptBirdOriginalTransform = Bird->GetActorTransform();
	Party->SetSlingshotMode(true);
	const FABTSM11FinaleLayoutPreset& Preset =
		FinaleSystem->GetLayoutPreset();
	Stabilizer.Reset(InitialAimInput);
	const FVector LocalDirection = FVector(
		Preset.LaunchModel.MapDirection(
			Stabilizer.GetControlledInput()));
	const FVector WorldDirection =
		FinaleSystem->GetFinaleFrame().WorldTransform
			.TransformVectorNoScale(LocalDirection);
	const FQuat PouchRotation = MakeM11PouchRotation(
		WorldDirection,
		AimSlingRight);
	Bird->EnterSlingshotPouch(
		AimRestPouchLocation
			+ PouchRotation.RotateVector(
				FVector(
					0.0,
					0.0,
					FABTSM11M6InputParityProfile::
						BirdInPouchOffsetCM)),
		PouchRotation);
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
	FailureTimeline.Reset();
	PlaybackElapsedSeconds = 0.0;
	PlaybackPresentationEndTimeSeconds = 0.0;
	++AimRevision;
	bPreviewDirty = true;
	TargetSelector.Reset();
	LatestSolvedRevision = INDEX_NONE;
	LatestQualifiedResult.Reset();
	LatestSameInputPhysicalResult.Reset();
	bLatestPhysicalResultAvailable = false;
	CurrentClassification = FABTSM11PrefixClassification();
	PreviewSelection = FABTSM11PreviewSelection();
	PreviewPlaybackPlan.Reset();
	DiagramSnapshot = FABTSM11OrbitalDiagramSnapshot();
	HudControlPanel = FABTSM11FinaleControlPanelState();
	HudOverviewView = FABTSM11OverviewViewState();
	InitialHudOverviewView = FABTSM11OverviewViewState();
	HudOrbitalScene = FABTSM11OrbitalSceneSnapshot();
	HudOverviewProjection = FABTSM11OverviewProjection();
	HudTrajectoryProbe = FABTSM11TrajectoryProbe();
	HudProbeProjection = FABTSM11ProbeProjection();
	HudProbeReferenceScene = FABTSM11OrbitalSceneSnapshot();
	HudOverviewRevision = 0;
	HudProbeRevision = 0;
	bTargetCaptureDirty = false;
	bTargetCaptureInitialized = false;
	UpdatePouchPresentation();
	FVector2D PouchScreen;
	if (Controller.ProjectWorldLocationToScreen(
			AimPouchLocation,
			PouchScreen))
	{
		Controller.SetMouseLocation(
			FMath::RoundToInt(PouchScreen.X),
			FMath::RoundToInt(PouchScreen.Y));
	}
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
	DrainCompletedSolves();
	switch (InteractionState)
	{
	case EABTSM11FinaleInteractionState::Aiming:
	case EABTSM11FinaleInteractionState::ReleasePending:
		UpdateAiming(DeltaSeconds);
		break;
	case EABTSM11FinaleInteractionState::Launched:
		UpdatePlayback(DeltaSeconds);
		break;
	case EABTSM11FinaleInteractionState::Failed:
	case EABTSM11FinaleInteractionState::Recovering:
		UpdateFailurePresentation(DeltaSeconds);
		break;
	default:
		break;
	}
	FlushTargetCapture();
}

bool AABTSM11FinaleInteractionSystem::BeginAimFromCursor(
	APlayerController& Controller)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !bAimFrameValid)
	{
		return false;
	}
	FVector2D PouchScreen;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!Controller.ProjectWorldLocationToScreen(
			AimPouchLocation,
			PouchScreen)
		|| !Controller.GetMousePosition(MouseX, MouseY)
		|| FVector2D::Distance(
			PouchScreen,
			FVector2D(MouseX, MouseY))
			> FABTSM11M6InputParityProfile::PouchPickRadiusPixels)
	{
		return false;
	}
	return ApplyAbsoluteCursorAim(Controller);
}

bool AABTSM11FinaleInteractionSystem::UpdateAimFromCursor(
	APlayerController& Controller)
{
	return InteractionState == EABTSM11FinaleInteractionState::Aiming
		&& ApplyAbsoluteCursorAim(Controller);
}

void AABTSM11FinaleInteractionSystem::AdjustAimPower(
	const double WheelSteps)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !FMath::IsFinite(WheelSteps))
	{
		return;
	}
	const FABTSM11FinaleLaunchInput Before =
		Stabilizer.GetControlledInput();
	FABTSM11FinaleLaunchInput Desired =
		Stabilizer.GetDesiredInput();
	Desired.Power += WheelSteps
		* FABTSM11M6InputParityProfile::PowerWheelStep;
	Stabilizer.SetDesiredPower(Desired.Power);
	if (!SameInteractionInput(
			Before,
			Stabilizer.GetControlledInput()))
	{
		++AimRevision;
		bPreviewDirty = true;
		UpdatePouchPresentation();
	}
}

bool AABTSM11FinaleInteractionSystem::ApplyHudControlDrag(
	const EABTSM11FinaleControlAxis Axis,
	const double PixelDelta,
	const EABTSM11ControlSpeedGear Gear)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !IsValid(FinaleSystem)
		|| !HudControlPanel.Initialize(
			FinaleSystem->GetLayoutPreset().LaunchModel,
			Stabilizer.GetDesiredInput()))
	{
		return false;
	}
	HudControlPanel.SetSpeedGear(Gear);
	return HudControlPanel.ApplyDragPixels(Axis, PixelDelta)
		&& ApplyHudTargetInput(HudControlPanel.GetInput());
}

bool AABTSM11FinaleInteractionSystem::ApplyHudControlWheel(
	const EABTSM11FinaleControlAxis Axis,
	const double WheelSteps,
	const EABTSM11ControlSpeedGear Gear)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !IsValid(FinaleSystem)
		|| !HudControlPanel.Initialize(
			FinaleSystem->GetLayoutPreset().LaunchModel,
			Stabilizer.GetDesiredInput()))
	{
		return false;
	}
	HudControlPanel.SetSpeedGear(Gear);
	return HudControlPanel.ApplyWheelSteps(Axis, WheelSteps)
		&& ApplyHudTargetInput(HudControlPanel.GetInput());
}

bool AABTSM11FinaleInteractionSystem::ResetHudControlAxis(
	const EABTSM11FinaleControlAxis Axis)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming)
	{
		return false;
	}
	FABTSM11FinaleLaunchInput Target = Stabilizer.GetDesiredInput();
	switch (Axis)
	{
	case EABTSM11FinaleControlAxis::Yaw:
		Target.YawDegrees = InitialAimInput.YawDegrees;
		break;
	case EABTSM11FinaleControlAxis::Pitch:
		Target.PitchDegrees = InitialAimInput.PitchDegrees;
		break;
	case EABTSM11FinaleControlAxis::Power:
		Target.Power = InitialAimInput.Power;
		break;
	default:
		return false;
	}
	// A reset is an explicit absolute command, not a precision-scaled trim.
	Stabilizer.CancelProtection();
	return ApplyHudTargetInput(Target);
}

bool AABTSM11FinaleInteractionSystem::ApplyHudTargetInput(
	const FABTSM11FinaleLaunchInput& TargetDesiredInput)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !TargetDesiredInput.IsFinite())
	{
		return false;
	}
	const FABTSM11FinaleLaunchInput Before =
		Stabilizer.GetControlledInput();
	const FABTSM11FinaleLaunchInput Desired =
		Stabilizer.GetDesiredInput();
	Stabilizer.ApplyInputDelta(
		TargetDesiredInput.YawDegrees - Desired.YawDegrees,
		TargetDesiredInput.PitchDegrees - Desired.PitchDegrees,
		TargetDesiredInput.Power - Desired.Power);
	if (SameInteractionInput(Before, Stabilizer.GetControlledInput()))
	{
		return false;
	}
	++AimRevision;
	bPreviewDirty = true;
	UpdatePouchPresentation();
	return true;
}

bool AABTSM11FinaleInteractionSystem::RotateHudOverview(
	const double YawDegrees,
	const double PitchDegrees)
{
	if (!IsAiming()
		|| !HudOverviewView.ApplyOrbitRotation(
			YawDegrees,
			PitchDegrees)
		|| !FABTSM11OverviewProjector::Build(
			HudOrbitalScene,
			HudOverviewView,
			HudOverviewProjection))
	{
		return false;
	}
	++HudOverviewRevision;
	return true;
}

bool AABTSM11FinaleInteractionSystem::PanHudOverview(
	const FVector2d& NormalizedScreenDelta)
{
	if (!IsAiming()
		|| !HudOverviewView.ApplyPanNormalized(NormalizedScreenDelta)
		|| !FABTSM11OverviewProjector::Build(
			HudOrbitalScene,
			HudOverviewView,
			HudOverviewProjection))
	{
		return false;
	}
	++HudOverviewRevision;
	return true;
}

bool AABTSM11FinaleInteractionSystem::ZoomHudOverview(
	const double ZoomMultiplier)
{
	if (!IsAiming()
		|| !HudOverviewView.ApplyZoom(ZoomMultiplier)
		|| !FABTSM11OverviewProjector::Build(
			HudOrbitalScene,
			HudOverviewView,
			HudOverviewProjection))
	{
		return false;
	}
	++HudOverviewRevision;
	return true;
}

bool AABTSM11FinaleInteractionSystem::ResetHudOverview()
{
	if (!IsAiming() || !InitialHudOverviewView.bValid)
	{
		return false;
	}
	HudOverviewView = InitialHudOverviewView;
	if (!FABTSM11OverviewProjector::Build(
		HudOrbitalScene,
		HudOverviewView,
		HudOverviewProjection))
	{
		return false;
	}
	++HudOverviewRevision;
	return true;
}

bool AABTSM11FinaleInteractionSystem::SelectHudTrajectoryProbe(
	const FABTSM11TrajectoryHit& Hit)
{
	if (!IsAiming()
		|| !HudOrbitalScene.bValid
		|| !HudOverviewView.bValid
		|| !FABTSM11TrajectoryProbeBuilder::Create(
			HudOrbitalScene,
			Hit,
			FVector3d::UpVector,
			HudOverviewView.ViewForward,
			HudTrajectoryProbe))
	{
		return false;
	}
	HudProbeReferenceScene = HudOrbitalScene;
	if (!FABTSM11TrajectoryProbeResolver::Resolve(
		HudOrbitalScene,
		HudTrajectoryProbe,
		HudProbeProjection))
	{
		HudTrajectoryProbe = FABTSM11TrajectoryProbe();
		HudProbeReferenceScene = FABTSM11OrbitalSceneSnapshot();
		return false;
	}
	++HudProbeRevision;
	MarkTargetCaptureDirty();
	return true;
}

bool AABTSM11FinaleInteractionSystem::RebaseHudTrajectoryProbe()
{
	if (!IsAiming() || !HudTrajectoryProbe.bValid)
	{
		return false;
	}
	FABTSM11TrajectoryProbe Rebased;
	if (!FABTSM11TrajectoryProbeBuilder::Rebase(
		HudOrbitalScene,
		HudTrajectoryProbe,
		FVector3d::UpVector,
		Rebased))
	{
		return false;
	}
	HudTrajectoryProbe = Rebased;
	HudProbeReferenceScene = HudOrbitalScene;
	if (!FABTSM11TrajectoryProbeResolver::Resolve(
		HudOrbitalScene,
		HudTrajectoryProbe,
		HudProbeProjection))
	{
		return false;
	}
	++HudProbeRevision;
	MarkTargetCaptureDirty();
	return true;
}

void AABTSM11FinaleInteractionSystem::FollowAutomaticPreviewTarget()
{
	if (!IsAiming())
	{
		return;
	}
	HudTrajectoryProbe = FABTSM11TrajectoryProbe();
	HudProbeProjection = FABTSM11ProbeProjection();
	HudProbeReferenceScene = FABTSM11OrbitalSceneSnapshot();
	++HudProbeRevision;
	MarkTargetCaptureDirty();
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
		QueuePreviewSolveIfNeeded();
	}
}

void AABTSM11FinaleInteractionSystem::CancelStabilizerOrResetAttempt()
{
	if (ABTSM11IsResettableFinaleState(InteractionState))
	{
		RestoreAttemptToWorld(false);
		FailureTimeline.Reset();
		RuntimeFailure.Reset();
		InteractionState = EABTSM11FinaleInteractionState::Ready;
	}
}

void AABTSM11FinaleInteractionSystem::ExitFinale()
{
	if (!IsFinaleActive())
	{
		return;
	}
	RestoreAttemptToWorld(false);
	FailureTimeline.Reset();
	RuntimeFailure.Reset();
	InteractionState = EABTSM11FinaleInteractionState::Ready;
}

bool AABTSM11FinaleInteractionSystem::IsFinaleActive() const
{
	return InteractionState == EABTSM11FinaleInteractionState::Aiming
		|| InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending
		|| InteractionState == EABTSM11FinaleInteractionState::Launched
		|| InteractionState == EABTSM11FinaleInteractionState::TargetHit
		|| ((InteractionState == EABTSM11FinaleInteractionState::Failed
				|| InteractionState
					== EABTSM11FinaleInteractionState::Recovering)
			&& (FailureTimeline.IsActive()
				|| bAttemptBirdInPouch));
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

const FABTSM11TrajectoryResult*
AABTSM11FinaleInteractionSystem::GetTargetPreviewPrediction() const
{
	if (PreviewSelection.Target == EABTSM11PreviewTarget::UFO
		&& bLatestPhysicalResultAvailable
		&& LatestSameInputPhysicalResult.ValidationHash != 0)
	{
		return &LatestSameInputPhysicalResult;
	}
	return GetCurrentPrediction();
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
	if (!Candidate.IsValid())
	{
		return Reject(TEXT("InvalidFinalePreset"));
	}
	if (InFinaleSystem.IsEditorCandidateMode())
	{
		const FABTSM11CandidateExperienceIdentity& Identity =
			InFinaleSystem.GetEditorCandidateIdentity();
		if (!Identity.IsValid())
		{
			return Reject(TEXT("InvalidEditorCandidateIdentity"));
		}
		if (Candidate.PresetVersion != 1
			|| Candidate.CompatibleGeneratorVersion != 3
			|| Candidate.CompatibleFrameLayoutVersion != 1
			|| (Candidate.SearchAlgorithmVersion != 1
				&& Candidate.SearchAlgorithmVersion != 2
				&& Candidate.SearchAlgorithmVersion != 3)
			|| Candidate.LaunchModel.LaunchModelVersion != 1
			|| Candidate.SolverConfig.SolverVersion != 2
			|| Candidate.SolverConfig.HashSchemaVersion != 2
			|| Candidate.LaunchModel.MaximumSimulationTimeSeconds
				!= 60.0
			|| Candidate.SolverConfig.MaximumSimulationTimeSeconds
				!= 60.0)
		{
			return Reject(TEXT("UnsupportedEditorCandidateContract"));
		}
		if (Candidate.PresetSourceHash != 0
			|| Candidate.PresetHash != 0
			|| Candidate.ScanContractHash != 0
			|| Candidate.CertificationHash != 0
			|| Candidate.NominalTrajectoryHash != 0
			|| Candidate.PhysicalPlaybackTrajectoryHash != 0
			|| Candidate.CertifiedBundleHash != 0)
		{
			return Reject(TEXT("EditorCandidateClaimsFormalCertification"));
		}
		for (int32 Index = 0;
			Index < Candidate.PrefixTrustRegions.Num();
			++Index)
		{
			const FABTSM11PrefixTrustRegion& Region =
				Candidate.PrefixTrustRegions[Index];
			if (Region.PrefixLevel != Index + 1
				|| Region.RegionHash != 0
				|| !Region.IsValid(Candidate.LaunchModel))
			{
				return Reject(TEXT("InvalidTemporaryCandidateTrustRegion"));
			}
		}
		return true;
	}
	const FABTSM11FinaleLayoutPreset Frozen =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	if (Candidate.PresetVersion != Frozen.PresetVersion
		|| Candidate.CompatibleGeneratorVersion
			!= Frozen.CompatibleGeneratorVersion
		|| Candidate.CompatibleFrameLayoutVersion
			!= Frozen.CompatibleFrameLayoutVersion
		|| Candidate.SearchAlgorithmVersion
			!= Frozen.SearchAlgorithmVersion
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
		if (ABTSM11ShouldRefreshFinaleHudTargetCapture(
			HudTrajectoryProbe.bValid,
			bTargetCaptureInitialized,
			PreviewSelection.Target != PreviousTarget,
			false))
		{
			MarkTargetCaptureDirty();
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
		|| !EnsureFlightCamera()
		|| ReleasedPlaybackPlan.Points.IsEmpty())
	{
		FailInteraction(TEXT("PlaybackDependencyLost"));
		return;
	}
	const double PresentationEndTime = FMath::Clamp(
		PlaybackPresentationEndTimeSeconds,
		ReleasedPlaybackPlan.Points[0].TimeSeconds,
		ReleasedPlaybackPlan.DurationSeconds);
	const double PresentationTimeScale =
		FinaleSystem->IsEditorCandidateMode()
			? 1.0
			: FMath::Max(0.1, PlaybackTimeScale);
	PlaybackElapsedSeconds = FMath::Min(
		PresentationEndTime,
		PlaybackElapsedSeconds
			+ FMath::Max(0.0f, DeltaSeconds)
				* PresentationTimeScale);
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
	FVector CameraTangent = WorldVelocity;
	if (CameraTangent.IsNearlyZero()
		&& FlightCamera->IsAuthorityFollowActive())
	{
		CameraTangent = FlightCamera->GetLastAuthorityForward();
	}
	if (!FlightCamera->IsAuthorityFollowActive())
	{
		const FTransform InitialViewTransform =
			IsValid(AimCamera)
			? AimCamera->GetActorTransform()
			: FTransform(
				MakeFlightRotation(
					CameraTangent,
					Frame.GetUp()),
				WorldPosition);
		if (IsValid(AimCamera)
			&& IsValid(AimCamera->GetCameraComponent())
			&& IsValid(FlightCamera->GetCameraComponent()))
		{
			FlightCamera->GetCameraComponent()->SetFieldOfView(
				AimCamera->GetCameraComponent()->FieldOfView);
		}
		if (!FlightCamera->BeginAuthorityFollow(
			WorldPosition,
			CameraTangent,
			Frame.GetUp(),
			InitialViewTransform))
		{
			FailInteraction(TEXT("FlightCameraFollowInitializationFailed"));
			return;
		}
		if (APlayerController* Controller =
			ActiveFinaleController.Get())
		{
			Controller->SetViewTarget(FlightCamera);
		}
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][FlightCamera] FollowStarted Camera=%s Bird=%s"),
			*GetNameSafe(FlightCamera),
			*GetNameSafe(AttemptBird));
	}
	AttemptBird->SetActorLocationAndRotation(
		WorldPosition,
		MakeFlightRotation(WorldVelocity, Frame.GetUp()),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (!FlightCamera->UpdateAuthoritySample(
		WorldPosition,
		CameraTangent,
		Frame.GetUp(),
		static_cast<float>(
			FMath::Max(0.0, static_cast<double>(DeltaSeconds))
				* PresentationTimeScale)))
	{
		FailInteraction(TEXT("FlightCameraAuthoritySampleRejected"));
		return;
	}
	if (PlaybackElapsedSeconds >= PresentationEndTime - 1.0e-9)
	{
		if (ReleasedPlaybackPlan.bPhysicalTargetHit
			|| ReleasedPlaybackPlan.bCandidateQualifiedIntercept)
		{
			InteractionState =
				EABTSM11FinaleInteractionState::TargetHit;
			if (ReleasedPlaybackPlan.bCandidateQualifiedIntercept)
			{
				UE_LOG(
					LogABTSRuntime,
					Log,
					TEXT("[ABTS][M11-C-v2.1][Playback] CandidateQualified-UNCERTIFIED Plan=0x%016llx Rank=%d Work=%llu"),
					ReleasedPlaybackPlan.PlanHash,
					FinaleSystem->GetEditorCandidateIdentity().Rank,
					static_cast<unsigned long long>(
						FinaleSystem->GetEditorCandidateIdentity()
							.GlobalWorkIndex));
			}
			else
			{
				UE_LOG(
					LogABTSRuntime,
					Log,
					TEXT("[ABTS][M11-C][Playback] PhysicalTargetHit Plan=0x%016llx Transfer=%d"),
					ReleasedPlaybackPlan.PlanHash,
					ReleasedPlaybackPlan
						.bUsesVisibleTerminalTransfer ? 1 : 0);
			}
		}
		else
		{
			BeginAttemptFailure(ABTSM11FailureReasonLabel(
				ABTSM11ClassifyFailure(
					LatestQualifiedResult,
					CurrentClassification)));
		}
	}
}

void AABTSM11FinaleInteractionSystem::UpdateFailurePresentation(
	const float DeltaSeconds)
{
	bool bShouldRestoreWorld = false;
	FailureTimeline.Advance(
		FMath::Max(0.0f, DeltaSeconds),
		bShouldRestoreWorld);
	if (bShouldRestoreWorld)
	{
		RestoreAttemptToWorld(false);
		InteractionState =
			EABTSM11FinaleInteractionState::Recovering;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][Failure] RestoredAtBlack Reason=%s"),
			*RuntimeFailure);
	}
	if (FailureTimeline.IsComplete())
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][Failure] RecoveryComplete Reason=%s"),
			*RuntimeFailure);
		FailureTimeline.Reset();
		RuntimeFailure.Reset();
		InteractionState = EABTSM11FinaleInteractionState::Ready;
	}
}

bool AABTSM11FinaleInteractionSystem::EnsureAimCamera()
{
	if (IsValid(AimCamera))
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!AimCameraClass)
	{
		return false;
	}
	AimCamera = World->SpawnActor<AABTSM6SlingshotCamera>(
		AimCameraClass,
		FTransform::Identity,
		SpawnParameters);
	const bool bClassParity = IsValid(AimCamera)
		&& AimCamera->GetClass() == AimCameraClass.Get();
	if (bClassParity)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][CameraClassParity] SourceClass=%s SpawnedClass=%s Match=1"),
			*GetNameSafe(AimCameraClass.Get()),
			*GetNameSafe(AimCamera->GetClass()));
	}
	else
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-C][CameraClassParity] SourceClass=%s SpawnedClass=%s Match=0"),
			*GetNameSafe(AimCameraClass.Get()),
			*GetNameSafe(IsValid(AimCamera)
				? AimCamera->GetClass()
				: nullptr));
	}
	if (!bClassParity && IsValid(AimCamera))
	{
		AimCamera->Destroy();
		AimCamera = nullptr;
	}
	return bClassParity;
}

bool AABTSM11FinaleInteractionSystem::EnsureFlightCamera()
{
	if (IsValid(FlightCamera))
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FlightCamera = World->SpawnActor<AABTSM11FinaleFlightCamera>(
		AABTSM11FinaleFlightCamera::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	return IsValid(FlightCamera);
}

void AABTSM11FinaleInteractionSystem::RestoreAimCameraView()
{
	if (IsValid(FlightCamera))
	{
		FlightCamera->ResetAuthorityFollow();
	}
	APlayerController* Controller = ActiveFinaleController.Get();
	if (Controller != nullptr
		&& IsValid(AimCamera)
		&& (!IsValid(FlightCamera)
			|| Controller->GetViewTarget() == FlightCamera))
	{
		Controller->SetViewTarget(AimCamera);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][FlightCamera] RestoredAim Camera=%s"),
			*GetNameSafe(AimCamera));
	}
}

bool AABTSM11FinaleInteractionSystem::BuildAimFrame(
	const AABTSM51SlingshotCord& Cord,
	const AABTSM25BirdCharacter& Bird)
{
	if (!IsValid(FinaleSystem))
	{
		return false;
	}
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	AimSlingCenter =
		(Cord.GetEndpointA() + Cord.GetEndpointB()) * 0.5;
	AimSlingUp = Frame.GetUp().GetSafeNormal();
	AimSlingRight = FVector::VectorPlaneProject(
		Cord.GetEndpointB() - Cord.GetEndpointA(),
		AimSlingUp).GetSafeNormal();
	if (AimSlingUp.IsNearlyZero() || AimSlingRight.IsNearlyZero())
	{
		bAimFrameValid = false;
		return false;
	}

	// Match M6's one-time cord frame. The certified M11 local +X remains the
	// final sign authority so cursor yaw maps to the same immutable solver
	// domain even if the controlled bird approached from an unusual side.
	AimSlingForward = FVector::CrossProduct(
		AimSlingRight,
		AimSlingUp).GetSafeNormal();
	const FVector BirdSide = FVector::VectorPlaneProject(
		AimSlingCenter - Bird.GetActorLocation(),
		AimSlingUp).GetSafeNormal();
	if (!BirdSide.IsNearlyZero()
		&& FVector::DotProduct(AimSlingForward, BirdSide) < 0.0)
	{
		AimSlingForward *= -1.0;
	}
	const FVector CertifiedForward =
		Frame.GetForward().GetSafeNormal();
	if (!CertifiedForward.IsNearlyZero()
		&& FVector::DotProduct(
			AimSlingForward,
			CertifiedForward) < 0.0)
	{
		AimSlingForward *= -1.0;
	}
	if (FVector::DotProduct(
			FVector::CrossProduct(AimSlingUp, AimSlingForward),
			AimSlingRight) < 0.0)
	{
		AimSlingRight *= -1.0;
	}

	AimRestPouchLocation =
		Cord.GetRestPouchTransform().GetLocation();
	AimPouchLocation = AimRestPouchLocation;
	bAimFrameValid =
		!AimSlingForward.IsNearlyZero()
		&& FMath::IsFinite(AimRestPouchLocation.X)
		&& FMath::IsFinite(AimRestPouchLocation.Y)
		&& FMath::IsFinite(AimRestPouchLocation.Z);
	return bAimFrameValid;
}

bool AABTSM11FinaleInteractionSystem::ApplyAbsoluteCursorAim(
	APlayerController& Controller)
{
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		|| !bAimFrameValid
		|| !IsValid(AimCamera)
		|| !IsValid(FinaleSystem))
	{
		return false;
	}
	FVector RayOrigin;
	FVector RayDirection;
	if (!Controller.DeprojectMousePositionToWorld(
			RayOrigin,
			RayDirection))
	{
		return false;
	}
	const FVector PlaneNormal =
		AimCamera->GetActorForwardVector().GetSafeNormal();
	const double Denominator =
		FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::Abs(Denominator) <= SMALL_NUMBER)
	{
		return false;
	}
	const FABTSM11FinaleLaunchModel& LaunchModel =
		FinaleSystem->GetLayoutPreset().LaunchModel;
	const FABTSM11FinaleLaunchInput& Controlled =
		Stabilizer.GetControlledInput();
	const double PowerAlpha = FMath::Clamp(
		(Controlled.Power - LaunchModel.MinimumPower)
			/ (LaunchModel.MaximumPower
				- LaunchModel.MinimumPower),
		0.0,
		1.0);
	const double PullDistance = FMath::Lerp(
		FABTSM11M6InputParityProfile::MinimumPullDistanceCM,
		FABTSM11M6InputParityProfile::MaximumPullDistanceCM,
		PowerAlpha);
	const FVector PulledPlaneCenter =
		AimRestPouchLocation - AimSlingForward * PullDistance;
	const double RayDistance = FVector::DotProduct(
		PulledPlaneCenter - RayOrigin,
		PlaneNormal) / Denominator;
	if (RayDistance <= 0.0)
	{
		return false;
	}
	const FVector RawPouch =
		RayOrigin + RayDirection * RayDistance;
	const FVector AimPlaneOffset =
		(RawPouch - PulledPlaneCenter).GetClampedToMaxSize(
			FABTSM11M6InputParityProfile::MaximumAimPlaneOffsetCM);
	const FVector CursorPouch =
		PulledPlaneCenter + AimPlaneOffset;
	const FVector WorldDirection =
		(AimSlingCenter
			+ AimSlingUp
				* FABTSM11M6InputParityProfile::LaunchTargetLiftCM
			- CursorPouch).GetSafeNormal();
	if (WorldDirection.IsNearlyZero())
	{
		return false;
	}
	const FVector LocalDirection =
		FinaleSystem->GetFinaleFrame().WorldTransform
			.InverseTransformVectorNoScale(
				WorldDirection).GetSafeNormal();
	if (LocalDirection.IsNearlyZero())
	{
		return false;
	}

	FABTSM11FinaleLaunchInput Desired;
	if (!ABTSM11MapLocalLaunchDirectionToInput(
		LaunchModel,
		FVector3d(LocalDirection),
		Stabilizer.GetDesiredInput().Power,
		Desired))
	{
		return false;
	}
	const FABTSM11FinaleLaunchInput Before =
		Stabilizer.GetControlledInput();
	Stabilizer.SetAbsoluteDirectionInput(Desired);
	if (!SameInteractionInput(
			Before,
			Stabilizer.GetControlledInput()))
	{
		++AimRevision;
		bPreviewDirty = true;
		UpdatePouchPresentation();
	}
	return true;
}

void AABTSM11FinaleInteractionSystem::UpdatePouchPresentation()
{
	if (!IsValid(FinaleSystem)
		|| !IsValid(ActiveCord)
		|| !IsValid(AttemptBird)
		|| !bAttemptBirdInPouch
		|| !bAimFrameValid
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
	const FABTSM11FinaleLaunchInput& Input =
		InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending
		? FrozenReleaseInput
		: Stabilizer.GetControlledInput();
	const double PullAlpha = FMath::Clamp(
		(Input.Power - Preset.LaunchModel.MinimumPower)
			/ (Preset.LaunchModel.MaximumPower
				- Preset.LaunchModel.MinimumPower),
		0.0,
		1.0);
	const double PullDistance = FMath::Lerp(
		FABTSM11M6InputParityProfile::MinimumPullDistanceCM,
		FABTSM11M6InputParityProfile::MaximumPullDistanceCM,
		PullAlpha);
	const FVector PulledPlaneCenter =
		AimRestPouchLocation - AimSlingForward * PullDistance;
	const FVector LocalDirection = FVector(
		Preset.LaunchModel.MapDirection(Input));
	const FVector WorldDirection =
		Frame.WorldTransform.TransformVectorNoScale(LocalDirection);
	const FVector Target =
		AimSlingCenter
			+ AimSlingUp
				* FABTSM11M6InputParityProfile::LaunchTargetLiftCM;
	FVector PresentationPosition = PulledPlaneCenter;
	const FVector PlaneNormal = IsValid(AimCamera)
		? AimCamera->GetActorForwardVector().GetSafeNormal()
		: AimSlingForward;
	const double DirectionDenominator =
		FVector::DotProduct(WorldDirection, PlaneNormal);
	if (FMath::Abs(DirectionDenominator) > SMALL_NUMBER)
	{
		const double Distance = FVector::DotProduct(
			Target - PulledPlaneCenter,
			PlaneNormal) / DirectionDenominator;
		if (Distance > 0.0)
		{
			PresentationPosition =
				Target - WorldDirection * Distance;
		}
	}
	PresentationPosition = PulledPlaneCenter
		+ (PresentationPosition - PulledPlaneCenter)
			.GetClampedToMaxSize(
				FABTSM11M6InputParityProfile::
					MaximumAimPlaneOffsetCM);
	AimPouchLocation = PresentationPosition;
	const FQuat PouchRotation = MakeM11PouchRotation(
		WorldDirection,
		AimSlingRight);
	ActiveCord->UpdatePulledPouchVisual(
		AimPouchLocation,
		PouchRotation);
	AttemptBird->SetActorLocationAndRotation(
		AimPouchLocation
			+ PouchRotation.RotateVector(
				FVector(
					0.0,
					0.0,
					FABTSM11M6InputParityProfile::
						BirdInPouchOffsetCM)),
		PouchRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AABTSM11FinaleInteractionSystem::MarkTargetCaptureDirty()
{
	bTargetCaptureDirty = true;
}

void AABTSM11FinaleInteractionSystem::FlushTargetCapture()
{
	check(IsInGameThread());
	if (!bTargetCaptureDirty)
	{
		return;
	}
	if (!IsValid(TargetPreviewCapture)
		|| !IsValid(TargetPreviewRenderTarget)
		|| !IsValid(FinaleSystem))
	{
		return;
	}
	AActor* TargetActor = HudTrajectoryProbe.bValid
		? ResolveHudProbeContextActor()
		: ResolvePreviewTargetActor(PreviewSelection.Target);
	if (!IsValid(TargetActor))
	{
		return;
	}
	const FABTSM110FinaleLocalFrame& Frame =
		FinaleSystem->GetFinaleFrame();
	FVector CameraWorld = FVector::ZeroVector;
	FVector ForwardWorld = FVector::ForwardVector;
	FVector UpWorld = FVector::UpVector;
	double HorizontalFovDegrees =
		ABTSM11FinaleTargetPreviewFOVDegrees;
	if (HudTrajectoryProbe.bValid)
	{
		const FABTSM11FrozenPipView& Frozen =
			HudTrajectoryProbe.FrozenPipView;
		if (!Frozen.bValid)
		{
			return;
		}
		const double CameraDistanceCM = Frozen.HalfExtentCM * 2.5;
		const FVector3d CameraLocal = Frozen.ViewCenterCM
			- Frozen.ViewForward * CameraDistanceCM;
		CameraWorld = Frame.TransformLocalPosition(FVector(CameraLocal));
		ForwardWorld = Frame.WorldTransform.TransformVectorNoScale(
			FVector(Frozen.ViewForward)).GetSafeNormal();
		UpWorld = Frame.WorldTransform.TransformVectorNoScale(
			FVector(Frozen.ViewUp)).GetSafeNormal();
		HorizontalFovDegrees = FMath::RadiansToDegrees(
			2.0 * FMath::Atan(Frozen.HalfExtentCM / CameraDistanceCM));
		TargetPreviewCapture->ProjectionType =
			ECameraProjectionMode::Orthographic;
		TargetPreviewCapture->OrthoWidth = static_cast<float>(
			Frozen.HalfExtentCM * 2.0);
	}
	else
	{
		FABTSM11TargetPipView PipView;
		if (!ABTSM11BuildTargetPipView(
			FinaleSystem->GetLayoutPreset(),
			PreviewSelection,
			TargetPreviewRenderTarget->SizeX,
			TargetPreviewRenderTarget->SizeY,
			PipView))
		{
			return;
		}
		CameraWorld = Frame.TransformLocalPosition(
			FVector(PipView.CameraLocationCM));
		ForwardWorld = Frame.WorldTransform.TransformVectorNoScale(
			FVector(PipView.Forward)).GetSafeNormal();
		UpWorld = Frame.WorldTransform.TransformVectorNoScale(
			FVector(PipView.Up)).GetSafeNormal();
		HorizontalFovDegrees = PipView.HorizontalFOVDegrees;
		TargetPreviewCapture->ProjectionType =
			ECameraProjectionMode::Perspective;
	}
	if (ForwardWorld.IsNearlyZero() || UpWorld.IsNearlyZero())
	{
		return;
	}
	TargetPreviewCapture->SetWorldLocationAndRotation(
		CameraWorld,
		FRotationMatrix::MakeFromXZ(
			ForwardWorld,
			UpWorld).ToQuat());
	TargetPreviewCapture->FOVAngle = static_cast<float>(
		HorizontalFovDegrees);
	TargetPreviewCapture->ClearShowOnlyComponents();
	TargetPreviewCapture->ShowOnlyActorComponents(TargetActor);
	TargetPreviewCapture->bCameraCutThisFrame = true;
	FABTSStylizedSceneCaptureRegistry::Register(
		*TargetPreviewCapture,
		EABTSStylizedViewClass::FinaleRemotePreview);
	bTargetCaptureDirty = false;
	bTargetCaptureInitialized = true;
	TargetPreviewCapture->CaptureScene();
	++TargetCaptureCount;
}

void AABTSM11FinaleInteractionSystem::RestoreAttemptToWorld(
	const bool bKeepFinaleMode)
{
	RestoreAimCameraView();
	++AimRevision;
	LatestSolvedRevision = INDEX_NONE;
	bPreviewDirty = false;
	bTargetCaptureDirty = false;
	bTargetCaptureInitialized = false;
	HudOverviewView = FABTSM11OverviewViewState();
	InitialHudOverviewView = FABTSM11OverviewViewState();
	HudOrbitalScene = FABTSM11OrbitalSceneSnapshot();
	HudOverviewProjection = FABTSM11OverviewProjection();
	HudTrajectoryProbe = FABTSM11TrajectoryProbe();
	HudProbeProjection = FABTSM11ProbeProjection();
	HudProbeReferenceScene = FABTSM11OrbitalSceneSnapshot();
	if (IsValid(AttemptBird) && bAttemptBirdInPouch)
	{
		// Collision must remain disabled until the bird has left any analytic
		// body endpoint and is back at its pre-finale world transform.
		AttemptBird->BeginSlingshotReturn();
		AttemptBird->SetActorTransform(
			AttemptBirdOriginalTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		AttemptBird->FinishSlingshotReturn();
	}
	bAttemptBirdInPouch = false;
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
		ActiveFinaleController.Reset();
		bAimFrameValid = false;
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
	PlaybackPresentationEndTimeSeconds = 0.0;
	RuntimeFailure.Reset();
	InteractionState = EABTSM11FinaleInteractionState::Aiming;
	UpdatePouchPresentation();
}

void AABTSM11FinaleInteractionSystem::BeginAttemptFailure(
	const FString& Reason)
{
	RuntimeFailure = Reason;
	FABTSM11FailurePresentationConfig Config;
	Config.ReadableHoldSeconds = FailureReadableHoldSeconds;
	Config.FadeToBlackSeconds = FailureFadeToBlackSeconds;
	Config.BlackHoldSeconds = FailureBlackHoldSeconds;
	Config.FadeFromBlackSeconds = FailureFadeFromBlackSeconds;
	if (!FailureTimeline.Begin(Config))
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-C][Failure] InvalidPresentationConfig Reason=%s"),
			*Reason);
		RestoreAttemptToWorld(false);
		InteractionState = EABTSM11FinaleInteractionState::Ready;
		return;
	}
	InteractionState = EABTSM11FinaleInteractionState::Failed;
	UE_LOG(
		LogABTSRuntime,
		Warning,
		TEXT("[ABTS][M11-C][Failure] Begin Reason=%s Hold=%.2f FadeIn=%.2f Black=%.2f FadeOut=%.2f"),
		*Reason,
		Config.ReadableHoldSeconds,
		Config.FadeToBlackSeconds,
		Config.BlackHoldSeconds,
		Config.FadeFromBlackSeconds);
}

void AABTSM11FinaleInteractionSystem::FailInteraction(
	const FString& Reason)
{
	if (bAttemptBirdInPouch)
	{
		BeginAttemptFailure(Reason);
		return;
	}
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

AActor* AABTSM11FinaleInteractionSystem::ResolveHudProbeContextActor() const
{
	if (!IsValid(FinaleSystem) || !HudTrajectoryProbe.bValid)
	{
		return nullptr;
	}
	if (HudTrajectoryProbe.bContextIsTarget)
	{
		return FinaleSystem->GetUFOActor();
	}
	if (HudTrajectoryProbe.ContextBodyIndex < 1
		|| HudTrajectoryProbe.ContextBodyIndex
			> FABTSM11GravityScenario::AssistCount)
	{
		return nullptr;
	}
	const int32 BodyId = FinaleSystem->GetLayoutPreset()
		.CanonicalScenario.Bodies[
			HudTrajectoryProbe.ContextBodyIndex].BodyId;
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
	if (TargetPreviewCapture)
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*TargetPreviewCapture);
	}
	if (IsFinaleActive())
	{
		RestoreAttemptToWorld(false);
	}
	RestoreAimCameraView();
	ActiveFinaleController.Reset();
	if (IsValid(FlightCamera))
	{
		FlightCamera->Destroy();
		FlightCamera = nullptr;
	}
	if (IsValid(AimCamera))
	{
		AimCamera->Destroy();
		AimCamera = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
