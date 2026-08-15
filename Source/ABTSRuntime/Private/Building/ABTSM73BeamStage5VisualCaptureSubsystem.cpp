// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamStage5VisualCaptureSubsystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamD1PreviewActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace ABTSM73BeamStage5VisualCapture
{
	constexpr int32 ShotCount = 4;
	constexpr int32 WarmupFrames = 18;
	constexpr double TimeoutSeconds = 90.0;
	constexpr double ScreenshotGraceSeconds = 8.0;

	const TCHAR* ShotName(const int32 Index)
	{
		switch (Index)
		{
		case 0: return TEXT("Stage14OverviewIso");
		case 1: return TEXT("Stage5ProductionIso");
		case 2: return TEXT("Stage5AdditionsIso");
		case 3: return TEXT("Stage5AdditionsSide");
		default: return TEXT("Invalid");
		}
	}

	EABTSM73BeamC3Stage4DiagnosticLayer LayerForShot(const int32 Index)
	{
		return Index == 0
			? EABTSM73BeamC3Stage4DiagnosticLayer::Stage14Overview
			: EABTSM73BeamC3Stage4DiagnosticLayer::FacadeToTopConnections;
	}

	FVector DirectionForShot(const int32 Index)
	{
		switch (Index)
		{
		case 2: return FVector(-1.0, -0.82, 0.28).GetSafeNormal();
		case 3: return FVector(-1.0, 0.0, 0.08).GetSafeNormal();
		default: return FVector(-1.0, -0.82, 0.46).GetSafeNormal();
		}
	}
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::ShouldCreateSubsystem(
	UObject* Outer) const
{
	return FParse::Param(FCommandLine::Get(), TEXT("ABTSM73Stage5Capture"))
		&& Super::ShouldCreateSubsystem(Outer);
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

void UABTSM73BeamStage5VisualCaptureSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartRealSeconds = FPlatformTime::Seconds();
	FString DemoValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ABTSM73CaptureDemo="), DemoValue))
	{
		if (DemoValue.Equals(TEXT("E1"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E1ColumnBreak;
		else if (DemoValue.Equals(TEXT("E2"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E2DropTrigger;
		else if (DemoValue.Equals(TEXT("E3"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E3SlideRelease;
		else if (DemoValue.Equals(TEXT("E4"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E4TipOver;
		else if (DemoValue.Equals(TEXT("E5"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E5SeamRelease;
		else if (DemoValue.Equals(TEXT("E6"), ESearchCase::IgnoreCase))
			DemoBuilding = EABTSM73BeamDemoBuilding::E6TipOver;
	}
	FParse::Value(FCommandLine::Get(), TEXT("ABTSM73CaptureOutput="), OutputDirectory);
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("ABTSVisualCaptures"),
			TEXT("M73Stage5"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	}
	OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);

	FString Error;
	if (!PrepareCapture(Error))
	{
		Finish(false, Error);
		return;
	}
	if (!PrepareShot(Error))
	{
		Finish(false, Error);
	}
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::PrepareCapture(FString& OutError)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutError = TEXT("WorldUnavailable");
		return false;
	}
	APlayerController* Controller = World->GetFirstPlayerController();
	if (!IsValid(Controller))
	{
		OutError = TEXT("PlayerControllerUnavailable");
		return false;
	}
	CaptureController = Controller;
	SavedViewTarget = Controller->GetViewTarget();
	bSavedScreenMessagesEnabled = GAreScreenMessagesEnabled;
	GAreScreenMessagesEnabled = false;

	for (TActorIterator<AABTSM73BeamD1PreviewActor> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->SetActorHiddenInGame(true);
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	PreviewActor = World->SpawnActor<AABTSM73BeamD1PreviewActor>(
		AABTSM73BeamD1PreviewActor::StaticClass(),
		FTransform(FVector(100000.0, 100000.0, 0.0)), SpawnParameters);
	if (!IsValid(PreviewActor))
	{
		OutError = TEXT("PreviewActorSpawnFailed");
		return false;
	}
	PreviewActor->SetActorHiddenInGame(false);

	CaptureCamera = World->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!IsValid(CaptureCamera) || CaptureCamera->GetCameraComponent() == nullptr)
	{
		OutError = TEXT("CaptureCameraSpawnFailed");
		return false;
	}
	CaptureCamera->GetCameraComponent()->SetProjectionMode(
		ECameraProjectionMode::Perspective);
	CaptureCamera->GetCameraComponent()->SetFieldOfView(48.0f);
	FPostProcessSettings& PostProcess =
		CaptureCamera->GetCameraComponent()->PostProcessSettings;
	PostProcess.bOverride_MotionBlurAmount = true;
	PostProcess.MotionBlurAmount = 0.0f;
	PostProcess.bOverride_MotionBlurMax = true;
	PostProcess.MotionBlurMax = 0.0f;
	CaptureCamera->GetCameraComponent()->SetPostProcessBlendWeight(1.0f);
	Controller->SetViewTarget(CaptureCamera);
	ScreenshotProcessedHandle =
		FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
			this,
			&UABTSM73BeamStage5VisualCaptureSubsystem::HandleScreenshotProcessed);
	return true;
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::PrepareShot(FString& OutError)
{
	using namespace ABTSM73BeamStage5VisualCapture;
	if (!IsValid(PreviewActor) || !IsValid(CaptureCamera)
		|| CurrentShot < 0 || CurrentShot >= ShotCount)
	{
		OutError = TEXT("CaptureStateInvalid");
		return false;
	}
	const bool bConfigured = CurrentShot == 0
		? PreviewActor->ConfigureForAutomatedCapture(
			DemoBuilding, LayerForShot(CurrentShot), OutError)
		: PreviewActor->ConfigureStage5ProductionForAutomatedCapture(
			DemoBuilding, CurrentShot >= 2, OutError);
	if (!bConfigured)
	{
		return false;
	}
	const FBox Bounds = PreviewActor->GetAutomatedCaptureBounds();
	if (!Bounds.IsValid)
	{
		OutError = TEXT("VisiblePreviewBoundsInvalid");
		return false;
	}
	const FVector Target = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();
	const double Radius = FMath::Max(Extent.Size(), 100.0);
	const FVector Direction = DirectionForShot(CurrentShot);
	const FVector CameraLocation = Target + Direction * Radius * 2.65;
	CaptureCamera->SetActorLocationAndRotation(
		CameraLocation, (Target - CameraLocation).Rotation(), false, nullptr,
		ETeleportType::TeleportPhysics);
	if (APlayerController* Controller = CaptureController.Get())
	{
		Controller->SetViewTarget(CaptureCamera);
		if (Controller->PlayerCameraManager != nullptr)
		{
			Controller->PlayerCameraManager->UpdateCamera(0.0f);
			Controller->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
	RemainingWarmupFrames = WarmupFrames;
	Phase = EPhase::Warming;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-D1][Stage5CaptureShotPrepared]")
		TEXT(" Demo=%d Shot=%s Layer=%d BoundsMin=%s BoundsMax=%s"),
		static_cast<int32>(DemoBuilding), ShotName(CurrentShot),
		static_cast<int32>(LayerForShot(CurrentShot)),
		*Bounds.Min.ToCompactString(), *Bounds.Max.ToCompactString());
	return true;
}

void UABTSM73BeamStage5VisualCaptureSubsystem::Tick(float DeltaTime)
{
	(void)DeltaTime;
	using namespace ABTSM73BeamStage5VisualCapture;
	if (Phase == EPhase::Inactive || Phase == EPhase::Terminal)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartRealSeconds > TimeoutSeconds)
	{
		Finish(false, TEXT("CaptureTimeout"));
		return;
	}
	if (Phase == EPhase::Warming)
	{
		// GameMode possession can replace the view target while the map finishes
		// BeginPlay. Reassert the transient camera through the whole warmup.
		if (APlayerController* Controller = CaptureController.Get())
		{
			Controller->SetViewTarget(CaptureCamera);
			if (Controller->PlayerCameraManager != nullptr)
			{
				Controller->PlayerCameraManager->UpdateCamera(0.0f);
				Controller->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
		}
		if (RemainingWarmupFrames-- <= 0)
		{
			RequestShot();
		}
	}
	else if (Phase == EPhase::WaitingForScreenshot && bScreenshotProcessed)
	{
		if (IFileManager::Get().FileExists(*ActiveScreenshotPath))
		{
			CompleteShot();
		}
		else if (Now - ScreenshotProcessedRealSeconds > ScreenshotGraceSeconds)
		{
			Finish(false, TEXT("ScreenshotWritebackMissing"));
		}
	}
}

void UABTSM73BeamStage5VisualCaptureSubsystem::RequestShot()
{
	using namespace ABTSM73BeamStage5VisualCapture;
	if (APlayerController* Controller = CaptureController.Get())
	{
		Controller->SetViewTarget(CaptureCamera);
		if (Controller->PlayerCameraManager != nullptr)
		{
			Controller->PlayerCameraManager->UpdateCamera(0.0f);
			Controller->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		Finish(false, TEXT("ScreenshotRequestAlreadyActive"));
		return;
	}
	ActiveScreenshotPath = FPaths::Combine(
		OutputDirectory,
		FString::Printf(TEXT("%02d_%s.png"), CurrentShot + 1,
			ShotName(CurrentShot)));
	bScreenshotProcessed = false;
	ScreenshotProcessedRealSeconds = 0.0;
	FScreenshotRequest::RequestScreenshot(
		ActiveScreenshotPath, true, false, false, FIntRect(), true);
	if (!FScreenshotRequest::IsScreenshotRequested())
	{
		Finish(false, TEXT("ScreenshotRequestRejected"));
		return;
	}
	Phase = EPhase::WaitingForScreenshot;
}

void UABTSM73BeamStage5VisualCaptureSubsystem::HandleScreenshotProcessed()
{
	if (Phase == EPhase::WaitingForScreenshot)
	{
		bScreenshotProcessed = true;
		ScreenshotProcessedRealSeconds = FPlatformTime::Seconds();
	}
}

void UABTSM73BeamStage5VisualCaptureSubsystem::CompleteShot()
{
	CapturedArtifacts.Add(ActiveScreenshotPath);
	++CurrentShot;
	if (CurrentShot >= ABTSM73BeamStage5VisualCapture::ShotCount)
	{
		Finish(true, FString());
		return;
	}
	FString Error;
	if (!PrepareShot(Error))
	{
		Finish(false, Error);
	}
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::WriteManifest(
	const TCHAR* Status, const FString& Reason) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), Status);
	Root->SetStringField(TEXT("reason"), Reason);
	Root->SetNumberField(TEXT("demoBuilding"), static_cast<int32>(DemoBuilding));
	Root->SetNumberField(TEXT("capturedArtifactCount"), CapturedArtifacts.Num());
	Root->SetStringField(TEXT("evidenceLayer"), TEXT("OffscreenVisualDiagnostic"));
	Root->SetBoolField(TEXT("mapAssetModified"), false);
	TArray<TSharedPtr<FJsonValue>> Artifacts;
	for (const FString& Artifact : CapturedArtifacts)
	{
		Artifacts.Add(MakeShared<FJsonValueString>(Artifact));
	}
	Root->SetArrayField(TEXT("artifacts"), Artifacts);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(
		Json, *FPaths::Combine(OutputDirectory, TEXT("manifest.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UABTSM73BeamStage5VisualCaptureSubsystem::Finish(
	const bool bSuccess, const FString& Reason)
{
	if (Phase == EPhase::Terminal)
	{
		return;
	}
	const bool bManifestWritten = WriteManifest(
		bSuccess ? TEXT("Succeeded") : TEXT("Failed"), Reason);
	RestoreRuntimeState();
	Phase = EPhase::Terminal;
	const bool bEffectiveSuccess = bSuccess && bManifestWritten;
	if (bEffectiveSuccess)
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-D1][Stage5CaptureTerminal]")
			TEXT(" Success=1 Demo=%d Records=%d Output=%s Reason=None"),
			static_cast<int32>(DemoBuilding), CapturedArtifacts.Num(),
			*OutputDirectory);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-Beam-D1][Stage5CaptureTerminal]")
			TEXT(" Success=0 Demo=%d Records=%d Output=%s Reason=%s"),
			static_cast<int32>(DemoBuilding), CapturedArtifacts.Num(),
			*OutputDirectory,
			Reason.IsEmpty() ? TEXT("ManifestWriteFailed") : *Reason);
	}
	FPlatformMisc::RequestExitWithStatus(
		false, bEffectiveSuccess ? 0 : 2, TEXT("M73Stage5CaptureComplete"));
}

void UABTSM73BeamStage5VisualCaptureSubsystem::RestoreRuntimeState()
{
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(
			ScreenshotProcessedHandle);
		ScreenshotProcessedHandle.Reset();
	}
	if (FScreenshotRequest::IsScreenshotRequested())
	{
		FScreenshotRequest::Reset();
	}
	GAreScreenMessagesEnabled = bSavedScreenMessagesEnabled;
	if (APlayerController* Controller = CaptureController.Get())
	{
		if (AActor* ViewTarget = SavedViewTarget.Get())
		{
			Controller->SetViewTarget(ViewTarget);
		}
	}
}

void UABTSM73BeamStage5VisualCaptureSubsystem::Deinitialize()
{
	RestoreRuntimeState();
	Super::Deinitialize();
}

TStatId UABTSM73BeamStage5VisualCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSM73BeamStage5VisualCaptureSubsystem, STATGROUP_Tickables);
}

bool UABTSM73BeamStage5VisualCaptureSubsystem::IsTickable() const
{
	return Phase != EPhase::Inactive && Phase != EPhase::Terminal;
}
