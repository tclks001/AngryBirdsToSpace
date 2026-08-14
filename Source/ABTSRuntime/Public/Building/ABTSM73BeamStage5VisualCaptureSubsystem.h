// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamD1Types.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSM73BeamStage5VisualCaptureSubsystem.generated.h"

class AABTSM73BeamD1PreviewActor;
class AActor;
class ACameraActor;
class APlayerController;

/**
 * Explicit, transient Stage-5 visual evidence runner. It is created only for
 * -ABTSM73Stage5Capture and never edits the source map or production defaults.
 */
UCLASS()
class ABTSRUNTIME_API UABTSM73BeamStage5VisualCaptureSubsystem final
	: public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8
	{
		Inactive = 0,
		Warming,
		WaitingForScreenshot,
		Terminal
	};

	bool PrepareCapture(FString& OutError);
	bool PrepareShot(FString& OutError);
	void RequestShot();
	void HandleScreenshotProcessed();
	void CompleteShot();
	void Finish(bool bSuccess, const FString& Reason);
	bool WriteManifest(const TCHAR* Status, const FString& Reason) const;
	void RestoreRuntimeState();

	EPhase Phase = EPhase::Inactive;
	EABTSM73BeamDemoBuilding DemoBuilding =
		EABTSM73BeamDemoBuilding::E2DropTrigger;
	int32 CurrentShot = 0;
	int32 RemainingWarmupFrames = 0;
	double StartRealSeconds = 0.0;
	double ScreenshotProcessedRealSeconds = 0.0;
	bool bScreenshotProcessed = false;
	bool bSavedScreenMessagesEnabled = true;
	FString OutputDirectory;
	FString ActiveScreenshotPath;
	TArray<FString> CapturedArtifacts;
	FDelegateHandle ScreenshotProcessedHandle;
	TWeakObjectPtr<APlayerController> CaptureController;
	TWeakObjectPtr<AActor> SavedViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM73BeamD1PreviewActor> PreviewActor;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CaptureCamera;
};
