// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ABTSM73BeamAGenerator.h"
#include "ABTSM73BeamC3V2CoupledExteriorFrameTypes.h"
#include "ABTSM73BeamCGenerator.h"
#include "ABTSM73BeamD0ProfileCatalog.h"

/**
 * Bounded Stage-1 generator for the 36 cm four-face grounded coupled frame.
 * It has no candidate search: a fixed Settings + ordered cell set has exactly
 * one plan, which is either committed in full or rejected without mutation.
 */
class FABTSM73BeamC3V2CoupledExteriorFrameGenerator final
{
public:
	/** Explicit one-cell fixture entry. The accepted result contains only V2 members. */
	bool BuildSingleCell(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FBox& CellBounds,
		FABTSM73BeamAGenerationResult& OutAssembly,
		ABTSM73BeamC3V2::FCoupledExteriorFrameResult& OutResult,
		FString& OutError,
		int32 BayId = 0,
		int32 SourceVolumeId = 0) const;

	/**
	 * Replace positive-volume conflicts owned by ordinary body-frame roles and
	 * append one CribCore assembly per cell. Protected members and reserved
	 * support voids reject the entire transaction, except that the atomic E6
	 * route may transactionally replace a BridgeRail fully covered by one of its
	 * registered shared-course members.
	 */
	bool Generate(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const TArray<ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest>& Cells,
		FABTSM73BeamAGenerationResult& InOutAssembly,
		ABTSM73BeamC3V2::FCoupledExteriorFrameResult& OutResult,
		FString& OutError) const;

	/** Reconstruct the private geometry contract and require accepted DAG reachability. */
	bool CertifyFinalAssembly(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73BeamAGenerationResult& Assembly,
		const FABTSM73BeamCGenerationResult& FinalBeamC,
		ABTSM73BeamC3V2::FCoupledExteriorFrameResult& InOutResult,
		FString& OutError) const;
};
