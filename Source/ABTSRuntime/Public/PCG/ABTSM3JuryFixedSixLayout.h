// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "ABTSM3JuryFixedSixLayout.generated.h"

struct FABTSM2Cell;

UENUM(BlueprintType)
enum class EABTSM3JuryFixedSixRejectReason : uint8
{
	None = 0,
	NotEvaluated = 1,
	InvalidInput = 2,
	SourceIdentityMismatch = 3,
	FrozenCatalogMismatch = 4,
	EncounterIdentityMismatch = 5,
	PlacementFrameInvalid = 6,
	PadReservationFailed = 7,
	PadSeparationFailed = 8,
	HashMismatch = 9
};

/**
 * M3-owned copy of the immutable placement facts published by M7 Stage 4.5.
 *
 * This is a DDL-scoped fixture used to lay out the single jury candidate. It
 * is not a runtime M7-to-M3 channel and does not replace the integration-owned
 * world-generation contract. Source hashes make any later M7 geometry change
 * an explicit versioned migration instead of a silent footprint change.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3JuryBuildingPlacementFixture
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FName ManifestEntryId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FName StableId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 DifficultyTier = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 BuildingSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six",
		meta = (Units = "cm"))
	FVector2D RequiredPadHalfExtentCM = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 StaticGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 SourceDescriptorHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3JuryBuildingPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 EncounterIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FName ManifestEntryId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FName StableId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 DifficultyTier = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 BuildingSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 TargetAnchorCellId = INDEX_NONE;

	/** Resolved center cell; may move within the encounter's frozen target region. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 PadCenterCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 SlingshotAnchorCellId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six",
		meta = (Units = "cm"))
	FVector WorldLocationCM = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FVector WorldForwardAxis = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FVector WorldRightAxis = FVector::RightVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	FVector WorldUpAxis = FVector::UpVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six",
		meta = (Units = "cm"))
	FVector2D RequiredPadHalfExtentCM = FVector2D::ZeroVector;

	/** Sorted candidate cells reserved by the rotated 3 x 3 pad samples. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	TArray<int32> ReservedPadCellIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 SourceDescriptorHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 PlacementHash = 0;
};

USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM3JuryFixedSixLayoutResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 WorldSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 SourceCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 SourceSpatialResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 M7PlacementSchemaVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int32 M7SourceManifestVersion = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 M7SourceManifestHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 M7PlacementCatalogHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	TArray<FABTSM3JuryBuildingPlacement> Placements;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	EABTSM3JuryFixedSixRejectReason RejectReason =
		EABTSM3JuryFixedSixRejectReason::NotEvaluated;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	int64 LayoutHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Jury Fixed Six")
	bool bPlacementReady = false;
};

class ABTSRUNTIME_API FABTSM3JuryFixedSixLayoutBuilder
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 ExpectedEncounterCount = 6;
	static constexpr int32 FrozenWorldSeed = 312503;
	static constexpr int32 FrozenSourceCandidateId = 4;
	static constexpr int32 M7PlacementSchemaVersion = 1;
	static constexpr int32 M7SourceManifestVersion = 1;
	static constexpr int64 M7SourceManifestHash = 2324068295ll;
	static constexpr uint64 M7PlacementCatalogHash =
		13889440156022460967ull;
	static constexpr uint64 FrozenSourceSpatialResultHash =
		0x16A44AF72C58261Eull;
	static constexpr uint64 FrozenSourceSpatialCandidateHash =
		0x645E131BE34A5B3Eull;

	static TConstArrayView<FABTSM3JuryBuildingPlacementFixture>
		GetFrozenPlacementFixtures();

	static bool Build(
		const TArray<FABTSM2Cell>& Cells,
		float PlanetRadiusCM,
		const FABTSM3MonthlySpatialResult& SourceSpatialResult,
		FABTSM3JuryFixedSixLayoutResult& OutResult,
		FString& OutFailure);

	static uint64 ComputeFixtureCatalogHash();
	static uint64 ComputePlacementHash(
		const FABTSM3JuryBuildingPlacement& Placement);
	static uint64 ComputeLayoutHash(
		const FABTSM3JuryFixedSixLayoutResult& Result);
	static const TCHAR* GetRejectReasonName(
		EABTSM3JuryFixedSixRejectReason Reason);
};
