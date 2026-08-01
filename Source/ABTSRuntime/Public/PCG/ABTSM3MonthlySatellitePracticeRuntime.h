// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "ABTSM3MonthlySatellitePracticeRuntime.generated.h"

class AABTSCalibrationTargetProxy;
class AABTSM3Planet;
class AABTSM6SlingshotSystem;
class AABTSM9Satellite;

/**
 * Session-persistent copy of the exact R-5.1 candidate used to create the
 * playable satellite/E5 preview. Layout identity never changes when the
 * developer gravity override is toggled.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3MonthlySatelliteRuntimeSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	bool bValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 SourcePreviewResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 SourceCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 LaunchProfileHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 SatellitePracticePresetVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 SatellitePracticePresetHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FTransform SatelliteWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm"))
	float SatelliteRadiusCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm/s^2"))
	float SatelliteSurfaceGravityCMPerSec2 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FTransform E5WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm"))
	FVector E5HalfExtentCM = FVector::ZeroVector;

	/** Hash of the frozen primary/satellite/drag inputs with satellite gravity enabled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 BaselineGravitySnapshotHash = 0;

	/** Candidate, preview result and baseline gravity joined into one session layout identity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 RuntimeLayoutSnapshotHash = 0;
};

/**
 * Explicit R-5 preview-only bridge to the real M9 gravity/collision actor and
 * the same E5 proxy/M6 target path used by SlingshotSatelliteCalibration.
 */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSM3MonthlySatellitePracticeRuntime : public AActor
{
	GENERATED_BODY()

public:
	AABTSM3MonthlySatellitePracticeRuntime();

	bool Configure(
		AABTSM3Planet& InPrimaryPlanet,
		const FABTSM3MonthlySatellitePreviewCandidate& InCandidate,
		int64 InPreviewResultHash);

	/** Idempotent so automation fixtures can activate without a begun-play World. */
	bool ActivateSnapshot();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	const FABTSM3MonthlySatelliteRuntimeSnapshot& GetRuntimeSnapshot() const
	{
		return RuntimeSnapshot;
	}

	bool IsRuntimeReady() const { return bRuntimeReady; }
	bool IsSatelliteCollisionEnabled() const { return bSatelliteCollisionEnabled; }
	bool IsE5CollisionEnabled() const { return bE5CollisionEnabled; }
	bool IsM6TargetBound() const { return bM6TargetBound; }
	bool IsSatelliteGravityEnabled() const;
	int32 GetSatelliteGravityOverride() const { return LastGravityOverride; }
	AABTSM9Satellite* GetRuntimeSatellite() const { return RuntimeSatellite.Get(); }
	AABTSCalibrationTargetProxy* GetRuntimeE5Target() const { return RuntimeE5Target.Get(); }

private:
	bool SpawnSnapshotActors();
	bool BindM6Target();
	void ApplyGravityOverride(bool bForceLog);
	void ClearOwnedRuntime();
	void RefreshReadyState();

	UPROPERTY(Transient)
	TObjectPtr<AABTSM3Planet> PrimaryPlanet;

	UPROPERTY(Transient)
	FABTSM3MonthlySatellitePreviewCandidate CandidateSnapshot;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FABTSM3MonthlySatelliteRuntimeSnapshot RuntimeSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM9Satellite> RuntimeSatellite;

	UPROPERTY(Transient)
	TObjectPtr<AABTSCalibrationTargetProxy> RuntimeE5Target;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM6SlingshotSystem> BoundSlingshotSystem;

	int64 SourcePreviewResultHash = 0;
	int32 LastGravityOverride = MIN_int32;
	bool bConfigured = false;
	bool bRuntimeActorsSpawned = false;
	bool bRuntimeReady = false;
	bool bSatelliteCollisionEnabled = false;
	bool bE5CollisionEnabled = false;
	bool bM6TargetBound = false;
};
