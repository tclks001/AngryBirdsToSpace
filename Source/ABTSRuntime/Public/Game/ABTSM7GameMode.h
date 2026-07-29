// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73BuildingTypes.h"
#include "CoreMinimal.h"
#include "Game/ABTSM6GameMode.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "ABTSM7GameMode.generated.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM73StableBuildingActor;
class AABTSM3Planet;
struct FABTSBuildingGenerationContract;

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

protected:
	virtual void OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, int32 SpawnCellId) override;

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
	void DrawTaskGraphPositionDebug();

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
};
