// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BuildingTypes.h"
#include "CoreMinimal.h"
#include "Game/ABTSM6GameMode.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "TimerManager.h"
#include "ABTSM7GameMode.generated.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM73StableBuildingActor;
class AABTSM3Planet;
class FABTSM7SatellitePracticeE1CrystalBindingLifecycleTest;
class UProceduralMeshComponent;
class UWorld;
struct FABTSBuildingGenerationContract;

enum class EABTSM7SatellitePracticeE1CrystalBindingState : uint8
{
	Inactive,
	Waiting,
	Binding,
	Bound,
	Rejected,
	Cancelled
};

enum class EABTSM7SatellitePracticeE1CrystalBindingAction : uint8
{
	None,
	Wait,
	Bind,
	Reject
};

struct FABTSM7SatellitePracticeE1CrystalBindingObservation
{
	int32 AcceptedStaticBuildingCount = 0;
	/** Number of complete pre-promotion 54-Brick ordered unions, not caps. */
	int32 E1OrderedUnionCount = 0;
	int32 SatelliteRuntimeCount = 0;
	bool bSatelliteRuntimeReady = false;
};

/** Bounded, idempotent lifecycle used by production and its focused automation. */
struct ABTSRUNTIME_API FABTSM7SatellitePracticeE1CrystalBindingLifecycle
{
	static constexpr int32 ExpectedStaticBuildingCount = 6;
	static constexpr double TimeoutSeconds = 10.0;

	bool Start(double NowSeconds);
	EABTSM7SatellitePracticeE1CrystalBindingAction Advance(
		double NowSeconds,
		const FABTSM7SatellitePracticeE1CrystalBindingObservation& Observation,
		FString& OutReason);
	void MarkBound();
	void MarkBindingRejected(const FString& Reason);
	void Cancel();

	EABTSM7SatellitePracticeE1CrystalBindingState GetState() const
	{
		return State;
	}
	int32 GetAttemptCount() const { return AttemptCount; }
	double GetStartSeconds() const { return StartSeconds; }
	const FString& GetTerminalReason() const { return TerminalReason; }

private:
	EABTSM7SatellitePracticeE1CrystalBindingState State =
		EABTSM7SatellitePracticeE1CrystalBindingState::Inactive;
	double StartSeconds = 0.0;
	int32 AttemptCount = 0;
	FString TerminalReason;
};

/** Runtime-only binding from a TaskGraph spawn site to its generated building. */
struct FABTSM7TaskGraphBuildingDebugEntry
{
	TWeakObjectPtr<AABTSM73StableBuildingActor> Building;
	int32 TaskId = INDEX_NONE;
	EABTSM3TaskType TaskType = EABTSM3TaskType::Unassigned;
	int32 CellId = INDEX_NONE;
};

/** One difficulty/authorship profile selected by the logical TaskGraph task type. */
USTRUCT(BlueprintType)
struct FABTSM7TaskGraphBuildingProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	EABTSM3TaskType TaskType = EABTSM3TaskType::TargetBuilding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	bool bSpawnBuilding = true;

	/** The concrete material and scale required by this task/difficulty tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation")
	FABTSM73GenerationSettings GenerationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAGGenerationSettings DAGGenerationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAGLayoutSettings DAGLayoutSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAGFailureFrontierSettings DAGFailureFrontierSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAGFailurePatternSettings DAGFailurePatternSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAGFailurePlayabilitySettings DAGFailurePlayabilitySettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DAG")
	FABTSM73DAG4ValidationSettings DAG4ValidationSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	FABTSM73DifficultySettings DifficultySettings;
};

/**
 * Pure profile router for the production TaskGraph -> M7.3-DAG2.3 path.
 *
 * Existing GameMode Blueprint assets may still serialize the retired Legacy
 * profile array. ResolveRuntimeProfile upgrades only those legacy entries to
 * the bounded DAG2.3 launch profiles while keeping an explicitly authored DAG
 * profile editable.
 */
struct ABTSRUNTIME_API FABTSM7TaskGraphDAG23ProfileResolver
{
	static constexpr float FurnaceMinSupportContactAreaRatio = 0.06f;

	static bool IsSupportedBuildingTask(EABTSM3TaskType TaskType);
	static EABTSM73DAGPreset GetDefaultPreset(EABTSM3TaskType TaskType);
	static FABTSM7TaskGraphBuildingProfile MakeDefaultProfile(
		EABTSM3TaskType TaskType,
		EABTSM7BuildingMaterial Material);
	static bool ResolveRuntimeProfile(
		EABTSM3TaskType TaskType,
		const FABTSM7TaskGraphBuildingProfile& SourceProfile,
		FABTSM7TaskGraphBuildingProfile& OutProfile,
		bool& bOutMigratedLegacy);
};

/** M7 entry owns the material runtime and an optional M7.3-A first-anchor building test. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM7GameMode : public AABTSM6GameMode
{
	GENERATED_BODY()

public:
	AABTSM7GameMode();
	virtual void Tick(float DeltaSeconds) override;
	/** Restores terrain Block before a deferred first-hit promotion. */
	bool RestoreJuryDemoFixedSixTerrainCollisionForDeferredFirstHit(
		FString& OutError);

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	const FABTSM7TaskGraphBuildingProfile* FindTaskGraphBuildingProfile(EABTSM3TaskType TaskType) const;
	int32 CountRequiredTaskGraphBuildings(
		const FABTSBuildingGenerationContract& Contract) const;
	int32 SpawnTaskGraphBuildings(
		AABTSM3Planet& Planet,
		const FABTSBuildingGenerationContract& Contract,
		AABTSM7BuildingMaterialSystem& MaterialSystem,
		AABTSM6SlingshotSystem* SlingshotSystem,
		bool& bOutSetupFailed);
	int32 SpawnJuryDemoFixedSixStaticBuildings(
		const FABTSBuildingGenerationContract& Contract,
		AABTSM7BuildingMaterialSystem& MaterialSystem,
		AABTSM6SlingshotSystem* SlingshotSystem,
		bool& bOutSetupFailed);
	void ScheduleSatellitePracticeE1CrystalTargetBinding();
	void TryBindSatellitePracticeE1CrystalTarget();
	void ClearSatellitePracticeE1CrystalTargetBindingTimer();
	void DrawTaskGraphPositionDebug();
	bool BeginJuryDemoFixedSixProductionChaosBatch();
	void UpdateJuryDemoFixedSixProductionChaosBatch();
	bool EnterJuryDemoFixedSixProductionChaosFixedStep();
	void RestoreJuryDemoFixedSixProductionChaosFixedStep();
	bool ApplyJuryDemoFixedSixTerrainBuildingCollisionOverride(
		FString& OutError);
	void RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
		const TCHAR* Reason);
	void UpdateProductionFlowTiming(float DeltaSeconds);
	void LogProductionFlowSegment(const TCHAR* Segment);
	void FinishProductionFlow(bool bReady, const FString& Reason);

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7")
	TSubclassOf<AABTSM7BuildingMaterialSystem> BuildingMaterialSystemClass;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M7.3-A")
	TSubclassOf<AABTSM73StableBuildingActor> StableBuildingClass;

	/** Test bridge to M3: spawn one M7.3-A structure at the first TaskGraph building anchor. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A")
	bool bSpawnStableBuildingAtFirstAnchor = false;

	/**
	 * M7 closure path: every CellTopo building anchor resolves its Task type to one
	 * profile. The profile is also the editor-facing current-difficulty material API.
	 */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|TaskGraph Buildings")
	bool bSpawnTaskGraphBuildings = true;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|TaskGraph Buildings", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MaxTaskGraphBuildings = 8;

	/**
	 * The continuous terrain pad makes the old 5-degree FoundationCap guard too
	 * conservative for Gatehouse/TwinTower footprints. This remains a hard cap.
	 */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|TaskGraph Buildings", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float MaxTaskGraphBuildingAngularSpanDegrees = 7.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|TaskGraph Buildings")
	TArray<FABTSM7TaskGraphBuildingProfile> TaskGraphBuildingProfiles;

	/** Test-only material gallery near the runtime TaskGraph spawn. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Testing")
	bool bSpawnBuildingMaterialTestSet = false;

	/** Real-time player/building latitude-longitude list, useful for navigating to TaskGraph buildings in PIE. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Debug")
	bool bShowTaskGraphPositionDebug = true;

	/** Draw a matching latitude-longitude label above each generated TaskGraph building. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Debug")
	bool bDrawTaskGraphBuildingWorldLabels = true;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Debug", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float TaskGraphPositionDebugTextScale = 1.2f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7|Debug", meta = (ClampMin = "0.0", ClampMax = "2000.0", Units = "cm"))
	float TaskGraphBuildingWorldLabelHeightCM = 350.0f;

	TWeakObjectPtr<AABTSM3Planet> TaskGraphDebugPlanet;
	TWeakObjectPtr<ACharacter> TaskGraphDebugPlayer;
	TArray<FABTSM7TaskGraphBuildingDebugEntry> TaskGraphBuildingDebugEntries;

	static constexpr float SatellitePracticeE1CrystalBindingRetrySeconds = 0.1f;
	FABTSM7SatellitePracticeE1CrystalBindingLifecycle
		SatellitePracticeE1CrystalBindingLifecycle;
	FTimerHandle SatellitePracticeE1CrystalBindingTimerHandle;
	TWeakObjectPtr<UWorld> SatellitePracticeE1CrystalBindingWorld;
	FString LastSatellitePracticeE1CrystalBindingWaitReason;
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>
		JuryDemoFixedSixChaosBuildings;
	TWeakObjectPtr<AABTSM6SlingshotSystem> ProductionFlowSlingshotSystem;
	bool bJuryDemoFixedSixChaosBatchActive = false;
	bool bJuryDemoFixedSixChaosBatchTerminal = false;
	int32 JuryDemoFixedSixChaosActiveIndex = INDEX_NONE;
	bool bJuryDemoFixedSixChaosOwnsFixedStep = false;
	bool bJuryDemoFixedSixChaosOwnsSolverDeterminism = false;
	bool bJuryDemoFixedSixPreviousUseFixedTimeStep = false;
	bool bJuryDemoFixedSixPreviousSolverDeterminism = false;
	double JuryDemoFixedSixPreviousFixedDeltaSeconds = 0.0;
	TArray<TWeakObjectPtr<UProceduralMeshComponent>>
		JuryDemoFixedSixTerrainSurfaces;
	TArray<TEnumAsByte<ECollisionResponse>>
		JuryDemoFixedSixTerrainPreviousBuildingResponses;
	uint64 JuryDemoFixedSixProductionGenerationToken = 0;
	uint64 JuryDemoFixedSixTerrainOverrideGenerationToken = 0;
	bool bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride = false;
	bool bProductionFlowTimingActive = false;
	bool bProductionFlowTerminal = false;
	double ProductionFlowStartWallSeconds = 0.0;
	double ProductionFlowLastSegmentWallSeconds = 0.0;
	double ProductionFlowEstimatedCPUSeconds = 0.0;
	double ProductionFlowLastSegmentCPUSeconds = 0.0;
	double ProductionFlowAccumulatedTickWallSeconds = 0.0;
	double ProductionFlowLastSegmentTickWallSeconds = 0.0;
	double ProductionFlowLastCPUSampleWallSeconds = 0.0;

	friend class FABTSM7SatellitePracticeE1CrystalBindingLifecycleTest;
};
