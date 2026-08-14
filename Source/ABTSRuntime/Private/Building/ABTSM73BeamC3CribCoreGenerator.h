// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ABTSM73BeamC3CribCoreTypes.h"
#include "ABTSM73BeamAGenerator.h"

/**
 * Beam-C3 pure-data assembly rewrite. It concentrates a small number of
 * members into alternating X/Y crib courses before Beam-C2 builds the load DAG.
 */
class FABTSM73BeamC3CribCoreGenerator
{
public:
	bool Generate(
		const FABTSM73BeamC3CribCoreSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		FABTSM73BeamAGenerationResult& InOutAssembly,
		FABTSM73BeamC3CribCoreResult& OutResult,
		FString& OutError,
		const FABTSM73BeamC3CribCoreResult* ExistingCertifiedPlan = nullptr) const;

	/**
	 * Re-audit the authoritative post Beam-C2 assembly. Certification covers
	 * the closed four-corner crib topology and every final Z-post station.
	 */
	bool CertifyFinalAssembly(
		const FABTSM73BeamC3CribCoreSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73BeamAGenerationResult& Assembly,
		FABTSM73BeamC3CribCoreResult& InOutResult,
		FString& OutError) const;
};
