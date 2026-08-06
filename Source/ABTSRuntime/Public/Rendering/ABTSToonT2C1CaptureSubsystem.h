// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderProfile.h"
#include "Rendering/ABTSToonT2C1CaptureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSToonT2C1CaptureSubsystem.generated.h"

class AABTSM101LandingPreviewCamera;
class AABTSM11FinaleInteractionSystem;
class AABTSM3MonthlySatellitePracticeRuntime;
class AABTSM3Planet;
class UTextureRenderTarget2D;

enum class EABTSToonT2C1CapturePhase : uint8
{
	Inactive = 0,
	WaitingForWorld,
	WarmingCapture,
	Terminal
};

struct FABTSToonT2C1CaptureRecord
{
	FString Subject;
	FString ViewClass;
	FString Authority = TEXT("PreviewTest");
	FString ArtifactPath;
	FString ArtifactMD5;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	uint64 FixtureHash = 0;
	uint64 RuntimeCaptureRevision = 0;
};

/**
 * T2-C1 no-M7 pixel harness. It drives existing preview cameras or observes
 * M11's normal remote-preview capture; it never publishes gameplay results.
 */
UCLASS()
class ABTSRUNTIME_API UABTSToonT2C1CaptureSubsystem final
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
	enum class EResolveResult : uint8
	{
		Waiting = 0,
		Ready,
		Failed
	};

	EResolveResult ResolveLandingPreviews(FString& OutReason);
	EResolveResult ResolveFinaleRemotePreview(FString& OutReason);
	bool BeginLandingSubject(FString& OutFailure);
	bool CaptureCurrentArtifact(FString& OutFailure);
	bool SaveRenderTarget(
		UTextureRenderTarget2D& RenderTarget,
		const FString& Subject,
		const FString& ViewClass,
		uint64 FixtureHash,
		uint64 RuntimeCaptureRevision,
		FString& OutFailure);
	void Finish(bool bSuccess, const FString& Reason);
	void RestoreRuntimeState();
	bool WriteManifest(const TCHAR* Status, const FString& Reason) const;

	FABTSToonT2C1CaptureConfig Config;
	EABTSToonT2C1CapturePhase Phase =
		EABTSToonT2C1CapturePhase::Inactive;
	TArray<FABTSToonT2C1CaptureRecord> Records;
	FABTSM6TrajectoryPreview GroundPreview;
	FABTSM6TrajectoryPreview SatellitePreview;
	int32 CurrentLandingSubjectIndex = 0;
	int32 RemainingWarmupFrames = 0;
	double StartRealSeconds = 0.0;
	int32 ActualWorldSeed = 0;
	bool bM7AdapterReady = false;
	bool bSavedStyleEnabled = false;
	EABTSStylizedRenderProfile SavedStyleProfile =
		EABTSStylizedRenderProfile::GroundDay;
	bool bRuntimeStateCaptured = false;
	bool bSavedScreenPercentage = false;
	float SavedScreenPercentage = 100.0f;
	FString ConfigFailure;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM101LandingPreviewCamera> PreviewCamera;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM3Planet> Planet;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM3MonthlySatellitePracticeRuntime> SatelliteRuntime;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleInteractionSystem> FinaleInteraction;
};
