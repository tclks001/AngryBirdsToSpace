// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "ABTSM3MonthlySatellitePracticeRuntime.generated.h"

class AABTSM3Planet;
class AABTSM51SlingshotCord;
class AABTSM51SlingshotStake;
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

	/** Hash copied back from the live production M6 adapter after BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 ProductionLaunchProfileHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 SatellitePracticePresetVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 SatellitePracticePresetHash = 0;

	/** Actual terrain cells occupied by the two reinforced practice stakes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 PracticeStakeACellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 PracticeStakeBCellId = INDEX_NONE;

	/** Physical rest pouch frame rebuilt from the two independently grounded stakes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FTransform PracticeLaunchWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FVector PracticeStakeASurfaceWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FVector PracticeStakeBSurfaceWorld = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int32 SatelliteAnchorCellId = INDEX_NONE;

	/** Tangent-plane angle between the physical pouch forward and satellite sightline. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "deg"))
	float SatelliteFacingErrorDegrees = 180.0f;

	/** Deterministic correction around the frozen satellite arc ring. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "deg"))
	float SatelliteFacingCorrectionAzimuthDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm"))
	float SatellitePreviewRuntimeDeltaCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FTransform SatelliteWorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm"))
	float SatelliteRadiusCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm/s^2"))
	float SatelliteSurfaceGravityCMPerSec2 = 0.0f;

	/** Frozen-preview gravity retained for source-identity inspection; the production satellite consumes this exact value for gameplay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm/s^2"))
	float CalibrationSatelliteSurfaceGravityCMPerSec2 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FTransform E5WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime", meta = (Units = "cm"))
	FVector E5HalfExtentCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	EABTSM3MonthlySatelliteTargetAuthority TargetAuthority =
		EABTSM3MonthlySatelliteTargetAuthority::LegacyCalibrationProxy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 ProductionTargetDescriptorHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Practice")
	int32 ProductionTargetModuleId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 ProductionTargetIdentityHash = 0;

	/** Hash of the frozen primary/satellite/drag inputs with satellite gravity enabled. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	int64 BaselineGravitySnapshotHash = 0;

	/** Compatibility field.  Runtime policy is BuildingLevelAttackabilityV1, not the frozen 2g sweep. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	bool bTrajectoryCertified = false;

	/** Reachability evidence inherited from the historically hand-validated proxy volume overlapping the exact frozen E1 Brick OBB union; no numerical trajectory or exact-crystal requirement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	bool bBuildingLevelAttackabilityCertified = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 AttackabilityWitnessSampleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 AttackabilityWitnessBrickId = INDEX_NONE;

	/** Geometry-inheritance evidence from the historical reachable magenta proxy; no runtime trajectory samples are run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 ProxyOverlapBrickId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 ProxyOverlapBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 GravityOnHits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 GravityDependentHits = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int32 LargestSuccessIslandSamples = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	float BestAimInPlaneCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	float BestAimOutOfPlaneCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	float BestPullAlpha = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory", meta = (Units = "cm"))
	float MinimumGravityOffMissCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Runtime|Trajectory")
	int64 TrajectoryCertificationHash = 0;

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
	/** Integration V3 replaces the exact OBB stand-in union with the real frozen E1 building. */
	bool BindProductionE1BuildingModuleTarget(
		AActor& InTargetActor,
		const FVector& InTargetHalfExtentCM);
	/** Compatibility entry point for the current M7 cap adapter; exact union identity still gates it. */
	bool BindProductionE1CrystalTarget(
		AActor& InTargetActor,
		const FVector& InTargetHalfExtentCM)
	{
		return BindProductionE1BuildingModuleTarget(
			InTargetActor, InTargetHalfExtentCM);
	}

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
	bool IsTrajectoryCertified() const { return bTrajectoryCertified; }
	bool IsPracticeSlingshotReady() const { return bPracticeSlingshotReady; }
	bool IsPracticePouchInteractionReady() const
	{
		return bPracticePouchInteractionReady;
	}
	bool IsSatelliteGravityEnabled() const;
	int32 GetSatelliteGravityOverride() const { return LastGravityOverride; }
	AABTSM9Satellite* GetRuntimeSatellite() const { return RuntimeSatellite.Get(); }
	AActor* GetRuntimeE5Target() const { return RuntimeE5GameplayTarget.Get(); }
	AABTSM51SlingshotCord* GetRuntimePracticeCord() const;
	AABTSM51SlingshotStake* GetRuntimePracticeStakeA() const { return RuntimePracticeStakeA.Get(); }
	AABTSM51SlingshotStake* GetRuntimePracticeStakeB() const { return RuntimePracticeStakeB.Get(); }

private:
	bool SpawnSnapshotActors();
	bool SpawnPracticeSlingshot();
	bool BindM6Target();
	bool CertifyTrajectoryLayout();
	void ApplyGravityOverride(bool bForceLog);
	void LogGravityEvidence(float DeltaSeconds);
	void ClearOwnedRuntime();
	void RefreshReadyState();
	void RetireReleasePracticeSlingshotPresentation();

	UPROPERTY(Transient)
	TObjectPtr<AABTSM3Planet> PrimaryPlanet;

	UPROPERTY(Transient)
	FABTSM3MonthlySatellitePreviewCandidate CandidateSnapshot;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "ABTS|M3|Monthly Satellite Runtime")
	FABTSM3MonthlySatelliteRuntimeSnapshot RuntimeSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM9Satellite> RuntimeSatellite;

	UPROPERTY(Transient)
	TObjectPtr<AActor> RuntimeE5Target;

	/** Active gameplay authority; initially the exact 54-OBB union, then the real frozen building. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> RuntimeE5GameplayTarget;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotStake> RuntimePracticeStakeA;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotStake> RuntimePracticeStakeB;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotCord> RuntimePracticeCord;

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
	bool bProductionLaunchProfileBound = false;
	bool bTrajectoryCertified = false;
	bool bTrajectoryCertificationAttempted = false;
	bool bPracticeSlingshotReady = false;
	bool bPracticePouchInteractionReady = false;
	bool bReleasePracticeSlingshotPresentationRetired = false;
	float GravityEvidenceLogRemainingSeconds = 0.0f;
};
