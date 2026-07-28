// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM3TaskGraphTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM3TaskType : uint8
{
	// These values are serialized by existing M7 Blueprint profiles. Preserve
	// value, order and meaning; future entries may only append at the tail.
	Unassigned = 0,
	Start = 1,
	Workshop = 2,
	SlingshotRange = 3,
	TargetBuilding = 4,
	BridgeGate = 5,
	FurnaceRuins = 6,
	LaunchSite = 7,
	Scout = 8,
	SatelliteWindow = 9
};

static_assert(
	static_cast<uint8>(EABTSM3TaskType::Unassigned) == 0
	&& static_cast<uint8>(EABTSM3TaskType::Workshop) == 2
	&& static_cast<uint8>(EABTSM3TaskType::TargetBuilding) == 4
	&& static_cast<uint8>(EABTSM3TaskType::FurnaceRuins) == 6
	&& static_cast<uint8>(EABTSM3TaskType::LaunchSite) == 7
	&& static_cast<uint8>(EABTSM3TaskType::SatelliteWindow) == 9,
	"EABTSM3TaskType values are a serialized M3/M7 compatibility contract.");

UENUM(BlueprintType)
enum class EABTSM3TerrainType : uint8
{
	Plain,
	Forest,
	Highland,
	Mountain,
	Water
};

UENUM(BlueprintType)
enum class EABTSM3TaskLinkRole : uint8
{
	MainPath,
	Branch,
	LockedGate,
	LateShortcut
};

UENUM(BlueprintType)
enum class EABTSM3ProgressKey : uint8
{
	None,
	BuildWorkbench,
	SimpleSlingshotReady,
	TargetDestroyed,
	HaveWood,
	BridgeBuilt,
	ReinforcedSlingshotReady,
	SatelliteShotSolved,
	HaveCrystalCore
};

UENUM(BlueprintType)
enum class EABTSM3WaterEdgeType : uint8
{
	None,
	Stream,
	ShallowRiver,
	DeepRiver,
	LakeShore
};

UENUM(BlueprintType)
enum class EABTSM3CrossingType : uint8
{
	None,
	Ford,
	FallenLog,
	BridgeSite,
	Bridge
};

UENUM(BlueprintType)
enum class EABTSM3TransportType : uint8
{
	None,
	Trail,
	MainRoad
};

USTRUCT(BlueprintType)
struct FABTSM3CellEdgeKey
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 CellA = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 CellB = INDEX_NONE;

	FABTSM3CellEdgeKey() = default;
	FABTSM3CellEdgeKey(const int32 InA, const int32 InB)
		: CellA(FMath::Min(InA, InB)), CellB(FMath::Max(InA, InB)) {}

	bool operator==(const FABTSM3CellEdgeKey& Other) const
	{
		return CellA == Other.CellA && CellB == Other.CellB;
	}
};

FORCEINLINE uint32 GetTypeHash(const FABTSM3CellEdgeKey& Key)
{
	return HashCombineFast(GetTypeHash(Key.CellA), GetTypeHash(Key.CellB));
}

USTRUCT(BlueprintType)
struct FABTSM3TaskLink
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 LinkId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 TaskA = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 TaskB = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	EABTSM3TaskLinkRole Role = EABTSM3TaskLinkRole::MainPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	EABTSM3ProgressKey RequiredKey = EABTSM3ProgressKey::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	TArray<int32> CorridorCells;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	TArray<FABTSM3CellEdgeKey> CorridorEdges;
};

USTRUCT(BlueprintType)
struct FABTSM3CellEdgeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3CellEdgeKey Key;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	EABTSM3TransportType Transport = EABTSM3TransportType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	EABTSM3WaterEdgeType Water = EABTSM3WaterEdgeType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	EABTSM3CrossingType Crossing = EABTSM3CrossingType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	EABTSM3ProgressKey RequiredKey = EABTSM3ProgressKey::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 DownstreamCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	float FlowAccumulation = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	bool bBlocksOnFoot = false;
};

USTRUCT(BlueprintType)
struct FABTSM3TaskNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 TaskId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	EABTSM3TaskType Type = EABTSM3TaskType::Unassigned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 SeedCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	TArray<int32> CellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	TArray<int32> LinkedTaskIds;
};

USTRUCT(BlueprintType)
struct FABTSM3CellState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 TaskId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	EABTSM3TerrainType TerrainType = EABTSM3TerrainType::Plain;

	/** Logical macro height normalized to [0,1]. It is generated by PCG, not sampled from the render mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	float LogicalHeight01 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	float Moisture01 = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	float LogicalSlopeDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 RoadDistance = MAX_int32;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 MainRoadDistance = MAX_int32;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	int32 ProgressDistance = MAX_int32;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	bool bRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	bool bWater = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	bool bBuildingAnchor = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|TaskGraph")
	bool bBuildable = false;
};

USTRUCT(BlueprintType)
struct FABTSM3PCGConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxAttempts = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG", meta = (ClampMin = "80", ClampMax = "800"))
	int32 TaskTargetCells = 280;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG", meta = (ClampMin = "0.25", ClampMax = "1.5"))
	float WaterBarrierHalfWidthCells = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG", meta = (ClampMin = "8", ClampMax = "256"))
	int32 StreamFlowThreshold = 72;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG", meta = (ClampMin = "1.0", ClampMax = "25.0"))
	float MaxBuildSlopeDegrees = 8.0f;

	/**
	 * Minimum CellTopo clearance around every task building anchor. The M7 sphere
	 * consumes this certified region for its footprint, not merely the seed Cell.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings", meta = (ClampMin = "1", ClampMax = "4"))
	int32 BuildingPadClearanceRingCells = 2;

	/**
	 * Keeps the reinforced-slingshot satellite lesson away from the terminal
	 * LaunchSite. The terminal slingshot still uses the SatelliteWindow direction
	 * as its deterministic lateral axis, but the satellite itself must not sit
	 * inside the finale launch neighbourhood.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure",
		meta = (ClampMin = "20.0", ClampMax = "120.0", UIMin = "35.0", UIMax = "90.0", Units = "deg"))
	float MinSatelliteLaunchAngularSeparationDegrees = 55.0f;
};

/**
 * Presentation/physics construction pad derived from a CellTopo building anchor.
 * It never changes the logical CellTopo height or topology; it only defines the
 * local tangent-plane surface sampled by the continuous terrain renderer/query.
 */
USTRUCT(BlueprintType)
struct FABTSM3BuildingPadSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings")
	bool bEnableTerrainFlattening = true;

	/** Half size of the level inner construction area. It must cover a building's footprint and foundation margin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings", meta = (ClampMin = "100.0", UIMax = "1600.0"))
	FVector2D HalfExtentCM = FVector2D(650.0f, 450.0f);

	/** Smooth tangent-plane to terrain transition outside the inner rectangular construction area. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings", meta = (ClampMin = "10.0", UIMax = "800.0"))
	float EdgeBlendWidthCM = 180.0f;
};

USTRUCT(BlueprintType)
struct FABTSM3PCGSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 GeneratorVersion = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 AttemptIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 AssignedTaskCells = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 RiverEdges = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 RoadEdges = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3CellEdgeKey BridgeEdge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3CellEdgeKey ShortcutEdge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	bool bBridgeLockedBeforeBuild = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	bool bMainPathReachableAfterBridge = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure")
	float SatelliteLaunchAngularSeparationDegrees = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	bool bAccepted = false;
};

USTRUCT(BlueprintType)
struct FABTSM3BuildingSpawnSite
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	int32 TaskId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	int32 CellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	EABTSM3TaskType TaskType = EABTSM3TaskType::Unassigned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	float MaxSlopeDegrees = 0.0f;

	/** CellTopo-derived radial normal used as the local vertical for the construction pad and building. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	FVector AnchorDirection = FVector::UpVector;

	/** Tangent-plane axes in planet-local/world orientation; no mesh vertex is a logical source. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	FVector TangentForward = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	FVector TangentRight = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	float PadEdgeBlendWidthCM = 0.0f;

	/** Surface radius at the anchor before pad flattening; defines the tangent construction plane. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	float PadTargetRadiusCM = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Building Pad")
	bool bTerrainPadApplied = false;
};
