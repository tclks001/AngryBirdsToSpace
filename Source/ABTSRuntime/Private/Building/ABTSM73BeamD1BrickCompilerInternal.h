// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ABTSM73BeamBGenerator.h"
#include "ABTSM73BeamC3V2CoupledExteriorFrameTypes.h"
#include "ABTSM73BeamD0ProfileCatalog.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"

namespace ABTSM73BeamD1
{
	/** Internal production seam exposed only to focused module automation. */
	bool DeriveCoupledExteriorFrameCells(
		const FABTSM73BeamC3V2CoupledExteriorFrameSettings& Settings,
		const FABTSM73BeamAPreviewSettings& BeamASettings,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const FABTSM73BeamBGenerationResult& BeamB,
		int32 RequiredSupportedSpans,
		bool bRequireSharedCoursePair,
		TArray<ABTSM73BeamC3V2::FCoupledExteriorFrameCellRequest>& OutCells,
		FString& OutError);
}
