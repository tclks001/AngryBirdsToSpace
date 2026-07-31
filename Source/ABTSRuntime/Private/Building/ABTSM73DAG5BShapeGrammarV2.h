// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73DAG5BShapePreviewTypes.h"

struct FABTSM73DAG5BV2Volume
{
	int32 VolumeId = INDEX_NONE;
	int32 GrammarDepth = 0;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	EABTSM73DAG5BV2VolumeRole Role =
		EABTSM73DAG5BV2VolumeRole::Body;
	EABTSM73DAG5BV2Primitive Primitive =
		EABTSM73DAG5BV2Primitive::Box;
	FString DerivationPath;
};

struct FABTSM73DAG5BV2GenerationResult
{
	FABTSM73DAG5BV2PreviewSummary Summary;
	TArray<FABTSM73DAG5BV2Volume> Volumes;
	TArray<FString> GrammarTrace;
};

/** Pure-data, deterministic Shape Grammar + graph-WFC silhouette generator. */
class FABTSM73DAG5BShapeGrammarV2
{
public:
	bool Generate(
		const FABTSM73DAG5BV2PreviewSettings& Settings,
		FABTSM73DAG5BV2GenerationResult& OutResult,
		FString& OutError) const;
};
