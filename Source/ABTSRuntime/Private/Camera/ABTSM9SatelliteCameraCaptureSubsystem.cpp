// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM9SatelliteCameraCaptureSubsystem.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationRig.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM9SatelliteCameraCapturePrivate
{
	template <typename T>
	T* FindFirst(UWorld& World)
	{
		for (TActorIterator<T> It(&World); It; ++It)
		{
			if (IsValid(*It)) return *It;
		}
		return nullptr;
	}

	bool ProjectPoint(
		const FMinimalViewInfo& View,
		const FIntPoint Size,
		const FVector& World,
		FVector2D& OutScreen)
	{
		const FRotationMatrix Basis(View.Rotation);
		const FVector Relative = World - View.Location;
		const double X = FVector::DotProduct(Relative, Basis.GetUnitAxis(EAxis::X));
		if (X <= 1.0 || View.FOV <= 0.0f || View.FOV >= 179.0f) return false;
		const double Y = FVector::DotProduct(Relative, Basis.GetUnitAxis(EAxis::Y));
		const double Z = FVector::DotProduct(Relative, Basis.GetUnitAxis(EAxis::Z));
		const double TanH = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
		const double TanV = TanH / (static_cast<double>(Size.X) / Size.Y);
		OutScreen.X = (Y / (X * TanH) * 0.5 + 0.5) * Size.X;
		OutScreen.Y = (0.5 - Z / (X * TanV) * 0.5) * Size.Y;
		return true;
	}
}

bool UABTSM9SatelliteCameraCaptureSubsystem::ShouldCreateSubsystem(
	UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	(void)Outer;
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("ABTSM9CameraCapture"))
		&& Super::ShouldCreateSubsystem(Outer);
#endif
}

bool UABTSM9SatelliteCameraCaptureSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game;
}

void UABTSM9SatelliteCameraCaptureSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FParse::Value(FCommandLine::Get(), TEXT("MovieFolder="), OutputDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("MovieName="), MovieName);
	FParse::Value(FCommandLine::Get(), TEXT("MovieFrameRate="), FrameRate);
	FParse::Value(FCommandLine::Get(), TEXT("MovieQuality="), JpegQuality);
	FParse::Value(FCommandLine::Get(), TEXT("ABTSM9CaptureWarmupFrames="), WarmupFrames);
	FParse::Value(FCommandLine::Get(), TEXT("ABTSM9CaptureDurationSeconds="), RecordingDurationSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("ABTSM9CaptureTimeoutSeconds="), TimeoutSeconds);
	RemainingWarmupFrames = FMath::Clamp(WarmupFrames, 1, 300);
	FrameRate = FMath::Clamp(FrameRate, 1, 120);
	JpegQuality = FMath::Clamp(JpegQuality, 1, 100);
	RecordingDurationSeconds = FMath::Clamp(RecordingDurationSeconds, 2.0, 60.0);
	TimeoutSeconds = FMath::Max(RecordingDurationSeconds + 10.0, TimeoutSeconds);
	if (OutputDirectory.IsEmpty() || MovieName.IsEmpty())
	{
		ConfigFailure = TEXT("MovieFolderAndMovieNameRequired");
	}
}

void UABTSM9SatelliteCameraCaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartRealSeconds = FPlatformTime::Seconds();
	if (!ConfigFailure.IsEmpty())
	{
		Finish(false, ConfigFailure);
		return;
	}
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.CreateDirectoryTree(*OutputDirectory))
	{
		Finish(false, TEXT("OutputDirectoryCreateFailed"));
		return;
	}
	FABTSStylizedRenderingControl::SetEnabled(true);
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / FrameRate);
}

void UABTSM9SatelliteCameraCaptureSubsystem::Tick(const float DeltaTime)
{
	if (Phase == EABTSM9SatelliteCameraCapturePhase::Terminal) return;
	if (FPlatformTime::Seconds() - StartRealSeconds > TimeoutSeconds)
	{
		Finish(false, TEXT("CaptureTimeout"));
		return;
	}
	if (Phase == EABTSM9SatelliteCameraCapturePhase::WaitingForRig)
	{
		FString Failure;
		if (!ResolveAndLaunch(Failure))
		{
			if (!Failure.IsEmpty()) Finish(false, Failure);
			return;
		}
		Phase = EABTSM9SatelliteCameraCapturePhase::Warmup;
		return;
	}
	if (Phase == EABTSM9SatelliteCameraCapturePhase::Warmup)
	{
		if (--RemainingWarmupFrames <= 0)
		{
			RecordingStartSeconds = GetWorld()->GetTimeSeconds();
			Phase = EABTSM9SatelliteCameraCapturePhase::Recording;
		}
		return;
	}
	FString Failure;
	if (!CaptureFrame(Failure))
	{
		Finish(false, Failure);
		return;
	}
	if (GetWorld()->GetTimeSeconds() - RecordingStartSeconds
		>= RecordingDurationSeconds)
	{
		Finish(
			LockedIntent == EABTSM9SatelliteFlightCameraIntent::CinematicE5
				&& IntentVisibleFrames > 0
				&& BirdVisibleFrames > 0
				&& SatelliteMissingIntentFrames == 0
				&& SurfaceFrameBlendFrames > 0
				&& MaximumSurfaceFrameAlpha >= 0.9f
				&& MaximumHandoffCameraRelativeBirdDeltaDegrees <= 8.0f
				&& SuddenBirdHalfTurnFrames == 0,
			TEXT("RecordingDurationComplete"));
	}
}

bool UABTSM9SatelliteCameraCaptureSubsystem::ResolveAndLaunch(
	FString& OutFailure)
{
	UWorld* World = GetWorld();
	if (World == nullptr) return false;
	AABTSSlingshotSatelliteCalibrationRig* Rig =
		ABTSM9SatelliteCameraCapturePrivate::FindFirst<
			AABTSSlingshotSatelliteCalibrationRig>(*World);
	SlingshotSystem = ABTSM9SatelliteCameraCapturePrivate::FindFirst<
		AABTSM6SlingshotSystem>(*World);
	if (Rig == nullptr || !Rig->IsReady() || SlingshotSystem == nullptr)
	{
		return false;
	}
	AABTSM51SlingshotCord* Reinforced = nullptr;
	float CapturePull = Rig->GetSweepSummary().BestGravityOnPullAlpha;
	float CaptureAimInPlane = Rig->GetSweepSummary().BestGravityOnAimInPlaneCM;
	float CaptureAimOutOfPlane = Rig->GetSweepSummary().BestGravityOnAimOutOfPlaneCM;
	for (TActorIterator<AABTSM3MonthlySatellitePracticeRuntime> It(World); It; ++It)
	{
		if (!It->IsRuntimeReady() || !It->IsTrajectoryCertified()) continue;
		Reinforced = It->GetRuntimePracticeCord();
		const FABTSM3MonthlySatelliteRuntimeSnapshot& Snapshot =
			It->GetRuntimeSnapshot();
		CapturePull = Snapshot.BestPullAlpha;
		CaptureAimInPlane = Snapshot.BestAimInPlaneCM;
		CaptureAimOutOfPlane = Snapshot.BestAimOutOfPlaneCM;
		break;
	}
	AABTSM9Satellite* PracticeSatellite = nullptr;
	AActor* PracticeTarget = nullptr;
	FVector PracticeTargetHalfExtent = FVector::ZeroVector;
	if (!SlingshotSystem->CopySatellitePracticeTarget(
		PracticeSatellite,
		PracticeTarget,
		PracticeTargetHalfExtent))
	{
		OutFailure = TEXT("SatellitePracticeTargetUnavailable");
		return false;
	}
	(void)PracticeTarget;
	(void)PracticeTargetHalfExtent;
	float BestCordDistanceSquared = Reinforced
		? FVector::DistSquared(
			Reinforced->GetRestPouchTransform().GetLocation(),
			PracticeSatellite->GetPlanetCenterWorld())
		: BIG_NUMBER;
	for (TActorIterator<AABTSM51SlingshotCord> It(World); Reinforced == nullptr && It; ++It)
	{
		AABTSM51SlingshotCord* Cord = *It;
		if (Cord && Cord->GetSlingshotTier() == EABTSSlingshotTier::Reinforced)
		{
			const float DistanceSquared = FVector::DistSquared(
				Cord->GetRestPouchTransform().GetLocation(),
				PracticeSatellite->GetPlanetCenterWorld());
			if (DistanceSquared < BestCordDistanceSquared)
			{
				BestCordDistanceSquared = DistanceSquared;
				Reinforced = Cord;
			}
		}
	}
	if (Reinforced == nullptr)
	{
		OutFailure = TEXT("ReinforcedCalibrationCordUnavailable");
		return false;
	}
	const FABTSCalibrationSweepSummary& Sweep = Rig->GetSweepSummary();
	if (!Sweep.bPassed || !SlingshotSystem->StartSatelliteCameraCaptureLaunch(
		*Reinforced,
		CapturePull,
		CaptureAimInPlane,
		CaptureAimOutOfPlane))
	{
		OutFailure = TEXT("CertifiedCalibrationLaunchRejected");
		return false;
	}
	EABTSM9SatelliteFlightCameraPhase CameraPhase;
	AABTSM25BirdCharacter* ResolvedBird = nullptr;
	AABTSM9Satellite* ResolvedSatellite = nullptr;
	AActor* ResolvedE5Target = nullptr;
	if (!SlingshotSystem->CopySatelliteCameraCaptureState(
		ResolvedBird,
		ResolvedSatellite,
		ResolvedE5Target,
		LockedIntent,
		CameraPhase)
		|| LockedIntent != EABTSM9SatelliteFlightCameraIntent::CinematicE5)
	{
		OutFailure = TEXT("CinematicE5IntentNotLocked");
		return false;
	}
	const bool bUseNearPassFixture =
#if UE_BUILD_SHIPPING
		false;
#else
		FParse::Param(
		FCommandLine::Get(),
		TEXT("ABTSM9CameraCaptureNearPass"));
#endif
	if (bUseNearPassFixture
		&& !SlingshotSystem->StageSatelliteCameraCaptureNearPass())
	{
		OutFailure = TEXT("PreviewTestNearPassFixtureRejected");
		return false;
	}
	if (!SlingshotSystem->CopySatelliteCameraCaptureState(
		ResolvedBird,
		ResolvedSatellite,
		ResolvedE5Target,
		LockedIntent,
		CameraPhase)
		|| LockedIntent != EABTSM9SatelliteFlightCameraIntent::CinematicE5)
	{
		OutFailure = TEXT("FlightCinematicIntentNotLocked");
		return false;
	}
	Bird = ResolvedBird;
	Satellite = ResolvedSatellite;
	E5Target = ResolvedE5Target;
	RecordingRenderTarget = NewObject<UTextureRenderTarget2D>(this);
	RecordingRenderTarget->ClearColor = FLinearColor::Black;
	RecordingRenderTarget->InitCustomFormat(
		CaptureWidth, CaptureHeight, PF_B8G8R8A8, false);
	RecordingRenderTarget->UpdateResourceImmediate(true);
	RecordingCapture = NewObject<USceneCaptureComponent2D>(this);
	RecordingCapture->RegisterComponentWithWorld(World);
	RecordingCapture->TextureTarget = RecordingRenderTarget;
	RecordingCapture->bCaptureEveryFrame = false;
	RecordingCapture->bCaptureOnMovement = false;
	RecordingCapture->bAlwaysPersistRenderingState = true;
	RecordingCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	bStylizedViewRegistered = FABTSStylizedSceneCaptureRegistry::Register(
		*RecordingCapture,
		EABTSStylizedViewClass::FinaleGameplayMirrorCapture);
	if (!bStylizedViewRegistered)
	{
		OutFailure = TEXT("StylizedSceneCaptureRegistrationFailed");
		return false;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M9][CameraCapture] Started Intent=%s FromGroundRelease=%d NearPassFixture=%d Output=%s"),
		*UEnum::GetValueAsString(LockedIntent),
		bUseNearPassFixture ? 0 : 1,
		bUseNearPassFixture ? 1 : 0,
		*OutputDirectory);
	return true;
}

bool UABTSM9SatelliteCameraCaptureSubsystem::CaptureFrame(FString& OutFailure)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager : nullptr;
	if (!IsValid(CameraManager) || !IsValid(RecordingCapture)
		|| !IsValid(RecordingRenderTarget) || !IsValid(Bird))
	{
		OutFailure = TEXT("CaptureDependenciesUnavailable");
		return false;
	}
	const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
	FMinimalViewInfo CaptureView = View;
	if (AABTSM6SlingshotCamera* FlightCamera =
		Cast<AABTSM6SlingshotCamera>(PC->GetViewTarget()))
	{
		FlightCamera->CalcCamera(0.0f, CaptureView);
	}
	const FQuat CurrentCameraRotation = CaptureView.Rotation.Quaternion().GetNormalized();
	float CameraFrameDeltaDegrees = 0.0f;
	if (bHasPreviousCameraRotation)
	{
		CameraFrameDeltaDegrees = FMath::RadiansToDegrees(
			PreviousCameraRotation.AngularDistance(CurrentCameraRotation));
		MaximumCameraRotationFrameDeltaDegrees = FMath::Max(
			MaximumCameraRotationFrameDeltaDegrees,
			CameraFrameDeltaDegrees);
	}
	PreviousCameraRotation = CurrentCameraRotation;
	bHasPreviousCameraRotation = true;
	RecordingCapture->SetWorldLocationAndRotation(CaptureView.Location, CaptureView.Rotation);
	RecordingCapture->FOVAngle = CaptureView.FOV;
	RecordingCapture->PostProcessSettings = CaptureView.PostProcessSettings;
	RecordingCapture->PostProcessBlendWeight = CaptureView.PostProcessBlendWeight;
	RecordingCapture->CaptureScene();
	FTextureRenderTargetResource* Resource =
		RecordingRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	if (Resource == nullptr || !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != CaptureWidth * CaptureHeight)
	{
		OutFailure = TEXT("RenderTargetReadFailed");
		return false;
	}
	const FString FramePath = FPaths::Combine(
		OutputDirectory,
		FString::Printf(TEXT("%s.%06d.jpg"), *MovieName, CapturedFrameCount));
	const FImageView Image(Pixels.GetData(), CaptureWidth, CaptureHeight, EGammaSpace::sRGB);
	if (!FImageUtils::SaveImageByExtension(*FramePath, Image, JpegQuality))
	{
		OutFailure = TEXT("JpegFrameWriteFailed");
		return false;
	}
	FVector2D BirdScreen;
	const bool bBirdOnScreen = ABTSM9SatelliteCameraCapturePrivate::ProjectPoint(
		CaptureView, FIntPoint(CaptureWidth, CaptureHeight), Bird->GetActorLocation(), BirdScreen)
		&& BirdScreen.X >= 0.0 && BirdScreen.X <= CaptureWidth
		&& BirdScreen.Y >= 0.0 && BirdScreen.Y <= CaptureHeight;
	if (bBirdOnScreen) ++BirdVisibleFrames;
	float BirdVisualFrameDeltaDegrees = 0.0f;
	float CameraRelativeBirdFrameDeltaDegrees = 0.0f;
	if (const USkeletalMeshComponent* BirdVisual = Bird->GetBirdVisual())
	{
		const FQuat CurrentVisualRotation = BirdVisual->GetComponentQuat().GetNormalized();
		if (bHasPreviousBirdVisualRotation)
		{
			BirdVisualFrameDeltaDegrees = FMath::RadiansToDegrees(
				PreviousBirdVisualRotation.AngularDistance(CurrentVisualRotation));
			if (BirdVisualFrameDeltaDegrees > MaximumBirdVisualFrameDeltaDegrees)
			{
				MaximumBirdVisualFrameDeltaDegrees = BirdVisualFrameDeltaDegrees;
				MaximumBirdVisualFrameDeltaFrame = CapturedFrameCount;
			}
			if (BirdVisualFrameDeltaDegrees >= 90.0f)
			{
				++SuddenBirdHalfTurnFrames;
				UE_LOG(LogABTSRuntime, Error,
					TEXT("[ABTS][M9][CameraCapture] SuddenBirdHalfTurn Frame=%d DeltaDegrees=%.2f"),
					CapturedFrameCount,
					BirdVisualFrameDeltaDegrees);
			}
		}
		PreviousBirdVisualRotation = CurrentVisualRotation;
		bHasPreviousBirdVisualRotation = true;

		const FQuat CameraRelativeBirdRotation = (
			CurrentCameraRotation.Inverse() * CurrentVisualRotation).GetNormalized();
		if (bHasPreviousCameraRelativeBirdRotation)
		{
			CameraRelativeBirdFrameDeltaDegrees = FMath::RadiansToDegrees(
				PreviousCameraRelativeBirdRotation.AngularDistance(
					CameraRelativeBirdRotation));
		}
		PreviousCameraRelativeBirdRotation = CameraRelativeBirdRotation;
		bHasPreviousCameraRelativeBirdRotation = true;
	}
	EABTSM9SatelliteFlightCameraPhase CameraPhase;
	EABTSM9SatelliteFlightCameraIntent CurrentIntent;
	float SurfaceFrameAlpha = 0.0f;
	bool bSurfaceFrameCommitted = false;
	AABTSM25BirdCharacter* CurrentBird = nullptr;
	AABTSM9Satellite* CurrentSatellite = nullptr;
	AActor* CurrentE5 = nullptr;
	if (SlingshotSystem->CopySatelliteCameraCaptureState(
		CurrentBird, CurrentSatellite, CurrentE5, CurrentIntent, CameraPhase,
		&SurfaceFrameAlpha, &bSurfaceFrameCommitted)
		&& CameraPhase != EABTSM9SatelliteFlightCameraPhase::PrimaryFollow)
	{
		++IntentVisibleFrames;
		const float CameraBirdDistanceCM = FVector::Distance(
			CaptureView.Location,
			CurrentBird->GetActorLocation());
		MinimumIntentCameraBirdDistanceCM = FMath::Min(
			MinimumIntentCameraBirdDistanceCM,
			CameraBirdDistanceCM);
		MaximumIntentCameraBirdDistanceCM = FMath::Max(
			MaximumIntentCameraBirdDistanceCM,
			CameraBirdDistanceCM);
		MinimumIntentFieldOfViewDegrees = FMath::Min(
			MinimumIntentFieldOfViewDegrees,
			CaptureView.FOV);
		MaximumIntentFieldOfViewDegrees = FMath::Max(
			MaximumIntentFieldOfViewDegrees,
			CaptureView.FOV);
		const FVector CameraToSatellite =
			CurrentSatellite->GetPlanetCenterWorld() - CaptureView.Location;
		const FVector LocalSatellite =
			CaptureView.Rotation.Quaternion().UnrotateVector(CameraToSatellite);
		const float DistanceCM = CameraToSatellite.Size();
		const float AngularRadius = DistanceCM > KINDA_SMALL_NUMBER
			? FMath::Asin(FMath::Clamp(
				CurrentSatellite->GetPlanetRadiusCM() / DistanceCM,
				0.0f,
				0.999f))
			: HALF_PI;
		const float HorizontalHalfFov =
			FMath::DegreesToRadians(CaptureView.FOV * 0.5f);
		const float VerticalHalfFov = FMath::Atan(
			FMath::Tan(HorizontalHalfFov)
				/ FMath::Max(0.1f, static_cast<float>(CaptureWidth) / CaptureHeight));
		const bool bSatelliteIntersectsView = LocalSatellite.X > 0.0f
			&& FMath::Abs(FMath::Atan2(LocalSatellite.Y, LocalSatellite.X))
				<= HorizontalHalfFov + AngularRadius
			&& FMath::Abs(FMath::Atan2(LocalSatellite.Z, LocalSatellite.X))
				<= VerticalHalfFov + AngularRadius;
		if (bSatelliteIntersectsView)
		{
			++SatelliteVisibleIntentFrames;
		}
		else
		{
			++SatelliteMissingIntentFrames;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M9][CameraCapture] SatelliteMissing Frame=%d Phase=%s"),
				CapturedFrameCount,
				*UEnum::GetValueAsString(CameraPhase));
		}
		if (bHasPreviousCameraRotation
			&& bHasPreviousCameraPhase
			&& CameraPhase != PreviousCameraPhase)
		{
			MaximumCameraPhaseTransitionDeltaDegrees = FMath::Max(
				MaximumCameraPhaseTransitionDeltaDegrees,
				CameraFrameDeltaDegrees);
			if (CameraFrameDeltaDegrees >= 8.0f)
			{
				++SuddenCameraPhaseCutFrames;
				UE_LOG(LogABTSRuntime, Error,
					TEXT("[ABTS][M9][CameraCapture] SuddenCameraPhaseCut Frame=%d Phase=%s DeltaDegrees=%.2f"),
					CapturedFrameCount,
					*UEnum::GetValueAsString(CameraPhase),
					CameraFrameDeltaDegrees);
			}
		}
	}
	PreviousCameraPhase = CameraPhase;
	bHasPreviousCameraPhase = true;
	MaximumSurfaceFrameAlpha = FMath::Max(
		MaximumSurfaceFrameAlpha,
		SurfaceFrameAlpha);
	if (SurfaceFrameAlpha > 0.01f)
	{
		if (BirdVisualFrameDeltaDegrees > MaximumSurfaceFrameBirdVisualDeltaDegrees)
		{
			MaximumSurfaceFrameBirdVisualDeltaDegrees = BirdVisualFrameDeltaDegrees;
			MaximumSurfaceFrameBirdVisualDeltaFrame = CapturedFrameCount;
		}
		if (CameraRelativeBirdFrameDeltaDegrees
			> MaximumSurfaceFrameCameraRelativeBirdDeltaDegrees)
		{
			MaximumSurfaceFrameCameraRelativeBirdDeltaDegrees =
				CameraRelativeBirdFrameDeltaDegrees;
			MaximumSurfaceFrameCameraRelativeBirdDeltaFrame = CapturedFrameCount;
		}
		if (!bSurfaceFrameCommitted
			&& CameraRelativeBirdFrameDeltaDegrees
				> MaximumHandoffCameraRelativeBirdDeltaDegrees)
		{
			MaximumHandoffCameraRelativeBirdDeltaDegrees =
				CameraRelativeBirdFrameDeltaDegrees;
			MaximumHandoffCameraRelativeBirdDeltaFrame = CapturedFrameCount;
		}
	}
	if (SurfaceFrameAlpha > 0.01f
		&& !bSurfaceFrameCommitted
		&& bBirdOnScreen)
	{
		if (bHasPreviousHandoffBirdScreen)
		{
			const FVector2D ScreenVelocity =
				BirdScreen - PreviousHandoffBirdScreen;
			const float Motion = static_cast<float>(ScreenVelocity.Size());
			if (Motion > MaximumHandoffBirdScreenMotionPixelsPerFrame)
			{
				MaximumHandoffBirdScreenMotionPixelsPerFrame = Motion;
				MaximumHandoffBirdScreenMotionFrame = CapturedFrameCount;
			}
			if (bHasPreviousHandoffBirdScreenVelocity)
			{
				const float Acceleration = static_cast<float>(
					(ScreenVelocity - PreviousHandoffBirdScreenVelocity).Size());
				if (Acceleration
					> MaximumHandoffBirdScreenAccelerationPixelsPerFrameSquared)
				{
					MaximumHandoffBirdScreenAccelerationPixelsPerFrameSquared =
						Acceleration;
					MaximumHandoffBirdScreenAccelerationFrame = CapturedFrameCount;
				}
			}
			PreviousHandoffBirdScreenVelocity = ScreenVelocity;
			bHasPreviousHandoffBirdScreenVelocity = true;
		}
		PreviousHandoffBirdScreen = BirdScreen;
		bHasPreviousHandoffBirdScreen = true;
	}
	else
	{
		bHasPreviousHandoffBirdScreen = false;
		bHasPreviousHandoffBirdScreenVelocity = false;
	}
	if (SurfaceFrameAlpha > 0.01f) ++SurfaceFrameBlendFrames;
	if (bSurfaceFrameCommitted)
	{
		if (FirstSurfaceFrameCommittedFrame == INDEX_NONE)
		{
			FirstSurfaceFrameCommittedFrame = CapturedFrameCount;
		}
		++SurfaceFrameCommittedFrames;
	}
	++CapturedFrameCount;
	return true;
}

void UABTSM9SatelliteCameraCaptureSubsystem::Finish(
	const bool bSuccess,
	const FString& Reason)
{
	if (Phase == EABTSM9SatelliteCameraCapturePhase::Terminal) return;
	Phase = EABTSM9SatelliteCameraCapturePhase::Terminal;
	WriteManifest(bSuccess, Reason);
	if (bSuccess)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M9][CameraCapture] Complete Success=1 Reason=%s Frames=%d IntentFrames=%d BirdFrames=%d"),
			*Reason,
			CapturedFrameCount,
			IntentVisibleFrames,
			BirdVisibleFrames);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M9][CameraCapture] Complete Success=0 Reason=%s Frames=%d IntentFrames=%d BirdFrames=%d"),
			*Reason,
			CapturedFrameCount,
			IntentVisibleFrames,
			BirdVisibleFrames);
	}
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 2, TEXT("M9CameraCaptureComplete"));
}

bool UABTSM9SatelliteCameraCaptureSubsystem::WriteManifest(
	const bool bSuccess,
	const FString& Reason) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), bSuccess ? TEXT("Complete") : TEXT("Failed"));
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetNumberField(TEXT("contractVersion"), 9);
	Root->SetStringField(TEXT("authority"), TEXT("PreviewTest"));
	Root->SetStringField(TEXT("cameraIntent"), UEnum::GetValueAsString(LockedIntent));
	Root->SetNumberField(TEXT("frameCount"), CapturedFrameCount);
	Root->SetNumberField(TEXT("intentVisibleFrames"), IntentVisibleFrames);
	Root->SetNumberField(TEXT("birdVisibleFrames"), BirdVisibleFrames);
	Root->SetNumberField(
		TEXT("satelliteVisibleIntentFrames"), SatelliteVisibleIntentFrames);
	Root->SetNumberField(
		TEXT("satelliteMissingIntentFrames"), SatelliteMissingIntentFrames);
	Root->SetNumberField(TEXT("surfaceFrameBlendFrames"), SurfaceFrameBlendFrames);
	Root->SetNumberField(
		TEXT("surfaceFrameCommittedFrames"),
		SurfaceFrameCommittedFrames);
	Root->SetNumberField(
		TEXT("firstSurfaceFrameCommittedFrame"),
		FirstSurfaceFrameCommittedFrame);
	Root->SetNumberField(TEXT("maximumSurfaceFrameAlpha"), MaximumSurfaceFrameAlpha);
	Root->SetNumberField(
		TEXT("maximumBirdVisualFrameDeltaDegrees"),
		MaximumBirdVisualFrameDeltaDegrees);
	Root->SetNumberField(
		TEXT("maximumBirdVisualFrameDeltaFrame"),
		MaximumBirdVisualFrameDeltaFrame);
	Root->SetNumberField(
		TEXT("maximumSurfaceFrameBirdVisualDeltaDegrees"),
		MaximumSurfaceFrameBirdVisualDeltaDegrees);
	Root->SetNumberField(
		TEXT("maximumSurfaceFrameBirdVisualDeltaFrame"),
		MaximumSurfaceFrameBirdVisualDeltaFrame);
	Root->SetNumberField(
		TEXT("maximumSurfaceFrameCameraRelativeBirdDeltaDegrees"),
		MaximumSurfaceFrameCameraRelativeBirdDeltaDegrees);
	Root->SetNumberField(
		TEXT("maximumSurfaceFrameCameraRelativeBirdDeltaFrame"),
		MaximumSurfaceFrameCameraRelativeBirdDeltaFrame);
	Root->SetNumberField(
		TEXT("maximumHandoffCameraRelativeBirdDeltaDegrees"),
		MaximumHandoffCameraRelativeBirdDeltaDegrees);
	Root->SetNumberField(
		TEXT("maximumHandoffCameraRelativeBirdDeltaFrame"),
		MaximumHandoffCameraRelativeBirdDeltaFrame);
	Root->SetNumberField(
		TEXT("maximumHandoffBirdScreenMotionPixelsPerFrame"),
		MaximumHandoffBirdScreenMotionPixelsPerFrame);
	Root->SetNumberField(
		TEXT("maximumHandoffBirdScreenMotionFrame"),
		MaximumHandoffBirdScreenMotionFrame);
	Root->SetNumberField(
		TEXT("maximumHandoffBirdScreenAccelerationPixelsPerFrameSquared"),
		MaximumHandoffBirdScreenAccelerationPixelsPerFrameSquared);
	Root->SetNumberField(
		TEXT("maximumHandoffBirdScreenAccelerationFrame"),
		MaximumHandoffBirdScreenAccelerationFrame);
	Root->SetBoolField(TEXT("sphericalApproachDirectionBlend"), true);
	Root->SetBoolField(TEXT("fixedRadiusPoseFinalizedInCalcCamera"), true);
	Root->SetBoolField(TEXT("rotationMinimizedBirdPresentation"), true);
	Root->SetNumberField(TEXT("suddenBirdHalfTurnFrames"), SuddenBirdHalfTurnFrames);
	Root->SetNumberField(
		TEXT("maximumCameraRotationFrameDeltaDegrees"),
		MaximumCameraRotationFrameDeltaDegrees);
	Root->SetNumberField(
		TEXT("maximumCameraPhaseTransitionDeltaDegrees"),
		MaximumCameraPhaseTransitionDeltaDegrees);
	Root->SetNumberField(
		TEXT("suddenCameraPhaseCutFrames"),
		SuddenCameraPhaseCutFrames);
	Root->SetBoolField(TEXT("constantBirdScaleExperiment"), true);
	Root->SetNumberField(
		TEXT("minimumIntentCameraBirdDistanceCM"),
		IntentVisibleFrames > 0 ? MinimumIntentCameraBirdDistanceCM : 0.0f);
	Root->SetNumberField(
		TEXT("maximumIntentCameraBirdDistanceCM"),
		MaximumIntentCameraBirdDistanceCM);
	Root->SetNumberField(
		TEXT("intentCameraBirdDistanceRangeCM"),
		IntentVisibleFrames > 0
			? MaximumIntentCameraBirdDistanceCM - MinimumIntentCameraBirdDistanceCM
			: 0.0f);
	Root->SetNumberField(
		TEXT("minimumIntentFieldOfViewDegrees"),
		IntentVisibleFrames > 0 ? MinimumIntentFieldOfViewDegrees : 0.0f);
	Root->SetNumberField(
		TEXT("maximumIntentFieldOfViewDegrees"),
		MaximumIntentFieldOfViewDegrees);
	Root->SetNumberField(
		TEXT("intentFieldOfViewRangeDegrees"),
		IntentVisibleFrames > 0
			? MaximumIntentFieldOfViewDegrees - MinimumIntentFieldOfViewDegrees
			: 0.0f);
	Root->SetStringField(TEXT("frameWildcard"), FPaths::Combine(
		OutputDirectory, MovieName + TEXT(".*.jpg")));
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return FFileHelper::SaveStringToFile(
		Json,
		*FPaths::Combine(OutputDirectory, MovieName + TEXT(".manifest.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UABTSM9SatelliteCameraCaptureSubsystem::Deinitialize()
{
	if (bStylizedViewRegistered && IsValid(RecordingCapture))
	{
		FABTSStylizedSceneCaptureRegistry::Unregister(*RecordingCapture);
	}
	FApp::SetUseFixedTimeStep(false);
	Super::Deinitialize();
}

TStatId UABTSM9SatelliteCameraCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSM9SatelliteCameraCaptureSubsystem,
		STATGROUP_Tickables);
}

bool UABTSM9SatelliteCameraCaptureSubsystem::IsTickable() const
{
	return Phase != EABTSM9SatelliteCameraCapturePhase::Terminal;
}
