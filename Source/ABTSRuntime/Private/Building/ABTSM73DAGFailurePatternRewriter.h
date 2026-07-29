// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EABTSM73DAGFailurePattern : uint8;
struct FABTSM7MaterialProfile;
struct FABTSM73DAGFailureFrontierCandidate;
struct FABTSM73DAGFailurePatternSettings;
struct FABTSM73DAGFailurePatternResult;
struct FABTSM73DAGFailureRewriteIntent;
struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSpatialLayout;
struct FABTSM73DifficultySettings;
struct FABTSM73GenerationSettings;
struct FABTSM73StructureData;

/**
 * DAG3-B: transactionally rewrites one accepted physical support interface
 * into a distinct internal failure pattern. No material or WeakPoint record is
 * changed here; those remain DAG3-C responsibilities.
 */
class FABTSM73DAGFailurePatternRewriter
{
public:
	/** Resolves one baseline physical frontier to a stable macro-interface intent. */
	bool MakeIntent(
		const FABTSM73DAGFailurePatternSettings& PatternSettings,
		const FABTSM73DifficultySettings& DifficultySettings,
		const FABTSM73DAGFailureFrontierCandidate& SourceFrontier,
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGSpatialLayout& BaselineLayout,
		const FABTSM73StructureData& BaselineData,
		EABTSM73DAGFailurePattern Pattern,
		FABTSM73DAGFailureRewriteIntent& OutIntent,
		FString& OutError) const;

	/** Certifies the recompiled physical graph and writes its independent pattern identity. */
	bool ValidateRealizedPattern(
		const FABTSM73DAGFailureRewriteIntent& Intent,
		const FABTSM73DAGFailurePatternSettings& PatternSettings,
		const FABTSM73GenerationSettings& GenerationSettings,
		const FABTSM73DifficultySettings& DifficultySettings,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
		const FABTSM73StructureData& Data,
		FABTSM73DAGFailurePatternResult& OutResult,
		FString& OutError) const;
};
