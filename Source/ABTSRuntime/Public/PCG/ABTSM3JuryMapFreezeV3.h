// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"

struct FABTSM2Cell;

enum class EABTSM3JuryMapFreezeV3RejectReason : uint8
{
	None = 0,
	NotEvaluated,
	InvalidInput,
	SourceIdentityMismatch,
	FrozenCatalogMismatch,
	PlacementReservationFailed,
	PlacementSeparationFailed,
	PlacementFrameInvalid,
	AttackCorridorOrientationInvalid,
	StructuralContractInvalid,
	HashMismatch
};

/** M3-only diagnostics around one immutable V3 contract site. */
struct ABTSRUNTIME_API FABTSM3JuryMapFreezeV3Placement
{
	FABTSJuryDemoFixedSixBuildingSite Site;
	int32 TargetAnchorCellId = INDEX_NONE;
	int32 PadCenterCellId = INDEX_NONE;
	TArray<int32> ReservedPadCellIds;
	TArray<int32> ReservedEffectCellIds;
	FVector AttackCorridorWorldDirection = FVector::ZeroVector;
	FVector HorizontalLongAxisWorld = FVector::ZeroVector;
	double AttackCorridorLongAxisAbsDot = 1.0;
};

/**
 * Additive MapFreezeV3 handoff. It is deliberately not the production V2
 * export; Integration owns the later contract-version activation.
 */
struct ABTSRUNTIME_API FABTSM3JuryMapFreezeV3Result
{
	int32 SchemaVersion = 3;
	int32 WorldSeed = 0;
	int32 SourceCandidateId = INDEX_NONE;
	uint64 SourceSpatialResultHash = 0;
	uint64 SourceSpatialCandidateHash = 0;
	uint64 SourceSatellitePreviewResultHash = 0;
	uint64 SourceSatellitePreviewCandidateHash = 0;
	TArray<FABTSM3JuryMapFreezeV3Placement> Placements;
	FABTSJuryDemoFixedSixContract HandoffContract;
	EABTSM3JuryMapFreezeV3RejectReason RejectReason =
		EABTSM3JuryMapFreezeV3RejectReason::NotEvaluated;
	uint64 LayoutHash = 0;
	bool bMapFreezeReady = false;
};

class ABTSRUNTIME_API FABTSM3JuryMapFreezeV3Builder
{
public:
	static constexpr int32 SchemaVersion = 3;
	static constexpr int32 FrozenWorldSeed = 312503;
	static constexpr int32 FrozenSourceCandidateId = 4;
	static constexpr int32 ExpectedSiteCount = 6;
	static constexpr int32 ExpectedPrimarySiteCount = 5;
	static constexpr int32 SatelliteSiteIndex = 4;
	static constexpr uint64 FrozenSourceSpatialResultHash =
		0x16A44AF72C58261Eull;
	static constexpr uint64 FrozenSourceSpatialCandidateHash =
		0x645E131BE34A5B3Eull;

	static bool Build(
		const TArray<FABTSM2Cell>& Cells,
		const FVector& PrimaryCenterWorldCM,
		double PrimaryRadiusCM,
		double PrimarySurfaceGravityCMPerSec2,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySatellitePreviewResult& SatellitePreviewResult,
		FABTSM3JuryMapFreezeV3Result& OutResult,
		FString& OutFailure);

	static bool Validate(
		const TArray<FABTSM2Cell>& Cells,
		const FVector& PrimaryCenterWorldCM,
		double PrimaryRadiusCM,
		double PrimarySurfaceGravityCMPerSec2,
		const FABTSM3MonthlySpatialResult& SpatialResult,
		const FABTSM3MonthlySatellitePreviewResult& SatellitePreviewResult,
		const FABTSM3JuryMapFreezeV3Result& Result,
		EABTSM3JuryMapFreezeV3RejectReason& OutReason,
		FString& OutFailure);

	static uint64 ComputePlacementHash(
		const FABTSM3JuryMapFreezeV3Placement& Placement);
	static uint64 ComputeLayoutHash(
		const FABTSM3JuryMapFreezeV3Result& Result);
	static const TCHAR* GetRejectReasonName(
		EABTSM3JuryMapFreezeV3RejectReason Reason);
};
