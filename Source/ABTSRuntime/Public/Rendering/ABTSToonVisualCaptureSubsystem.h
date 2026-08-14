// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSToonEnvironmentTypes.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSToonVisualCaptureSubsystem.generated.h"

class AActor;
class ACameraActor;
class APlayerController;

struct FABTSToonSavedActorTransform
{
	TWeakObjectPtr<AActor> Actor;
	FTransform Transform = FTransform::Identity;
};

enum class EABTSToonVisualCapturePhase : uint8
{
	Inactive = 0,
	WaitingForWorld,
	WarmingCamera,
	WaitingForScreenshot,
	CoolingGPUProfile,
	Terminal
};

struct FABTSToonVisualCaptureManifestRecord
{
	FName PointId = NAME_None;
	EABTSToonVisualCaptureAnchor Anchor =
		EABTSToonVisualCaptureAnchor::GroundStart;
	EABTSStylizedRenderProfile Profile =
		EABTSStylizedRenderProfile::GroundDay;
	FName VariantId = NAME_None;
	bool bStyleEnabled = false;
	EABTSStylizedDiagnosticPassMask PassMask =
		EABTSStylizedDiagnosticPassMask::ToneAndOutline;
	bool bShadowsEnabled = true;
	int32 StyleImplementationVersion = 0;
	FTransform CameraWorldTransform = FTransform::Identity;
	FVector LookAtWorld = FVector::ZeroVector;
	float FieldOfViewDegrees = 60.0f;
	uint64 SemanticIdentityHash = 0;
	uint64 CameraPoseHash = 0;
	uint64 EffectiveCameraPoseHash = 0;
	uint64 EnvironmentSnapshotHash = 0;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	FString ArtifactPath;
	FString ArtifactMD5;
	bool bGPUProfileCommandAccepted = false;
	int32 GPUProfileSampleCount = 0;
};

/**
 * Explicit T0 baseline runner. It exists only when -ABTSToonT0Capture (or the
 * named ToonT0 suite) is present, and never mutates generated-world authority.
 */
UCLASS()
class ABTSRUNTIME_API UABTSToonVisualCaptureSubsystem final
	: public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(
		const EWorldType::Type WorldType) const override;

private:
	enum class EWorldResolveResult : uint8
	{
		Waiting = 0,
		Ready,
		Failed
	};

	void BeginCapture(UWorld& World);
	EWorldResolveResult TryResolveWorldAndCapturePoints(FString& OutReason);
	bool PrepareCaptureCamera(FString& OutFailure);
	void BeginCurrentVariant();
	bool ValidateEffectiveCamera(FString& OutFailure);
	void RequestCurrentScreenshot();
	void HandleScreenshotProcessed();
	void CompleteCurrentScreenshot();
	void DispatchCurrentGPUProfile();
	void CompleteCurrentGPUProfile();
	void AdvanceVariantOrFinish();
	void FinishCapture(bool bSuccess, const FString& Reason);
	void RestoreRuntimeState();
	bool CaptureBirdPartyTransforms(FString& OutFailure);
	bool ApplyCurrentDiagnosticBirdPartyPlacement(FString& OutFailure);
	void RestoreBirdPartyTransforms();
	bool WriteManifest(const TCHAR* Status, const FString& FailureReason);
	FIntPoint GetActualViewportResolution() const;
	FString MakeCurrentArtifactPath() const;

	FABTSToonVisualCaptureRunConfig RunConfig;
	EABTSToonVisualCapturePhase Phase =
		EABTSToonVisualCapturePhase::Inactive;
	TArray<FABTSToonResolvedCapturePoint> ResolvedPoints;
	TArray<FABTSToonDiagnosticVariantDefinition> VariantDefinitions;
	TArray<FABTSToonVisualCaptureManifestRecord> ManifestRecords;
	int32 CurrentPointIndex = 0;
	int32 CurrentVariantIndex = 0;
	int32 RemainingWarmupFrames = 0;
	int32 RemainingGPUCooldownFrames = 0;
	int32 CurrentGPUProfileSampleIndex = 0;
	double CaptureStartRealSeconds = 0.0;
	double ScreenshotProcessedRealSeconds = 0.0;
	double GPUProfileFalseObservedRealSeconds = 0.0;
	bool bScreenshotProcessed = false;
	bool bRuntimeStateCaptured = false;
	bool bWorldWasPaused = false;
	bool bSavedScreenMessagesEnabled = true;
	bool bSavedProfileGPUShowUI = true;
	uint32 SavedProfileGPUShowUISetBy = 0;
	bool bProfileGPUShowUIStateCaptured = false;
	float SavedScreenPercentage = 0.0f;
	uint32 SavedScreenPercentageSetBy = 0;
	bool bScreenPercentageStateCaptured = false;
	bool bSavedStyleEnabled = false;
	EABTSStylizedDiagnosticPassMask SavedDiagnosticPassMask =
		EABTSStylizedDiagnosticPassMask::ToneAndOutline;
	int32 SavedShadowQuality = 0;
	uint32 SavedShadowQualitySetBy = 0;
	bool bShadowQualityStateCaptured = false;
	EABTSStylizedRenderProfile SavedStyleProfile =
		EABTSStylizedRenderProfile::GroundDay;
	FString OutputDirectory;
	FString RunId;
	FString ActiveScreenshotPath;
	FDelegateHandle ScreenshotProcessedHandle;
	TWeakObjectPtr<APlayerController> CaptureController;
	TWeakObjectPtr<AActor> SavedViewTarget;
	TArray<FABTSToonSavedActorTransform> SavedBirdPartyTransforms;
	FVector SavedBirdPartyCenterWorld = FVector::ZeroVector;
	FVector SavedBirdPartyUp = FVector::UpVector;
	bool bBirdPartyTransformsCaptured = false;

	int32 ActualWorldSeed = 0;
	int32 ActualGeneratorVersion = 0;
	int32 ActualGenerationAttempt = INDEX_NONE;
	bool bActualSourceWorldAccepted = false;
	uint64 CaptureCatalogueHash = 0;
	uint64 VariantCatalogueHash = 0;
	FABTSToonEnvironmentSnapshot EnvironmentSnapshot;
	uint64 CurrentEffectiveCameraPoseHash = 0;
	int32 MonthlyPresentationCandidateId = INDEX_NONE;
	int64 MonthlyPresentationCandidateHash = 0;
	int32 OrdinarySlotAuthority = 0;
	int32 OrdinaryMaxCordLengthCM = 0;
	uint64 OrdinarySlotEvidenceHash = 0;
	int32 FinaleFrameAuthority = 0;
	int32 FinaleFrameSourceCandidateId = INDEX_NONE;
	int64 FinaleFrameSpatialCandidateHash = 0;
	int64 FinaleFramePlanResultHash = 0;
	int64 FinaleFramePreviewHash = 0;
	int64 FinaleFrameContextHash = 0;
	bool bFinaleFrameMonthlyWorldAccepted = false;
	int64 SatelliteRuntimeLayoutHash = 0;
	int32 SatelliteSourceCandidateId = INDEX_NONE;
	int64 SatelliteSourcePreviewResultHash = 0;
	int64 SatelliteSourceCandidateHash = 0;
	int64 SatelliteLaunchProfileHash = 0;
	int64 SatelliteProductionLaunchProfileHash = 0;
	int64 SatellitePresetHash = 0;
	int64 SatelliteTrajectoryCertificationHash = 0;
	uint64 FinalePresetHash = 0;
	uint64 FinaleCertifiedBundleHash = 0;
	bool bFinaleEditorCandidateMode = false;
	int32 FinaleEditorCandidateRank = 0;
	uint64 FinaleEditorCandidateSourceHash = 0;
	uint64 FinaleEditorCandidateResultHash = 0;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CaptureCamera;
};
