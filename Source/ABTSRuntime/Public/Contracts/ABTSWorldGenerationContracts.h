// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM110FinaleTypes.h"

/**
 * Stable, consumer-facing purpose of a generated construction site.
 *
 * This enum deliberately does not expose M3 TaskGraph task types. M3 may evolve
 * its mission grammar without forcing M7 to include or reinterpret the complete
 * TaskGraph schema.
 */
enum class EABTSGeneratedBuildingPurpose : uint8
{
	Unsupported = 0,
	Workshop,
	DestructibleTarget,
	FurnaceRuins,
	FinaleLaunchReserved,
	Count
};

/**
 * Versioned identity shared by all generated-world snapshots.
 *
 * Snapshots are immutable value copies. They are not UObject references, are
 * not serialized into maps, and never grant a consumer write access to M3.
 */
struct ABTSRUNTIME_API FABTSGeneratedWorldIdentity
{
	static constexpr int32 CurrentContractVersion = 1;

	int32 ContractVersion = CurrentContractVersion;
	int32 WorldSeed = 0;
	int32 GeneratorVersion = 0;
	int32 GenerationAttempt = INDEX_NONE;
	bool bSourceWorldAccepted = false;

	bool IsUsable() const;
};

/**
 * One stable M7 construction input exported from M3.
 *
 * The currently unused encounter fields are intentionally part of version 1 so
 * M3 can add route progression, difficulty and presentation metadata without
 * changing the binary contract used by the parallel M7 worktree.
 */
struct ABTSRUNTIME_API FABTSGeneratedBuildingSite
{
	/** Opaque stable identity. MAX_uint64 is reserved as the invalid sentinel. */
	uint64 SiteId = MAX_uint64;
	int32 TaskId = INDEX_NONE;
	int32 CellId = INDEX_NONE;
	int32 SourceTaskTypeValue = 0;
	EABTSGeneratedBuildingPurpose Purpose =
		EABTSGeneratedBuildingPurpose::Unsupported;

	int32 EncounterIndex = INDEX_NONE;
	int32 DifficultyTier = 0;
	float NormalizedRouteProgress = -1.0f;
	FName LayoutArchetypeId = NAME_None;
	FName VisualThemeId = NAME_None;

	/** Exact deterministic seed formerly reconstructed by M7 from M3 fields. */
	int32 DeterministicSeed = 0;

	FTransform WorldTransform = FTransform::Identity;
	float MaxSlopeDegrees = 0.0f;
	FVector AnchorDirection = FVector::UpVector;
	FVector TangentForward = FVector::ForwardVector;
	FVector TangentRight = FVector::RightVector;
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;
	float PadEdgeBlendWidthCM = 0.0f;
	float PadTargetRadiusCM = 0.0f;
	bool bTerrainPadApplied = false;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** Complete read-only input consumed by the M7 building-generation boundary. */
struct ABTSRUNTIME_API FABTSBuildingGenerationContract
{
	FABTSGeneratedWorldIdentity Identity;
	TArray<FABTSGeneratedBuildingSite> Sites;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/**
 * Complete read-only input consumed by the M11 finale boundary.
 *
 * M9's practice satellite and all M3 TaskGraph arrays are absent by design.
 */
struct ABTSRUNTIME_API FABTSFinaleWorldContract
{
	FABTSGeneratedWorldIdentity Identity;
	double PrimaryRadiusCM = 0.0;
	FABTSM110FinaleLocalFrame LaunchFrame;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};
