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
	/** Opposed endpoint supports for SupportedSpan; invalid for other roles. */
	int32 NegativeSupportVolumeId = INDEX_NONE;
	int32 PositiveSupportVolumeId = INDEX_NONE;
	/** 0 for X and 1 for Y; INDEX_NONE for non-span volumes. */
	int32 SpanAxisIndex = INDEX_NONE;
	/** Clear undercroft interval along SpanAxisIndex. */
	double SpanOpeningMinCM = 0.0;
	double SpanOpeningMaxCM = 0.0;
	FString DerivationPath;
};

/** One evaluated semantic podium split. Rejected candidates always carry the
 * first fail-closed reason; the accepted row is the selected highest split. */
struct FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic
{
	FString Scope;
	double LegacyTopZ = 0.0;
	double SemanticSeamZ = 0.0;
	double QuantizedTopZ = 0.0;
	double ProtectedSpanTopZ = 0.0;
	bool bCandidateStartsCrown = false;
	bool bAccepted = false;
	FString RejectReason;
};

/** Completeness record for one applied podium plan or one observation-only
 * building-wide common-seam probe. */
struct FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic
{
	FString Scope;
	int32 RootCount = 0;
	int32 CandidateCount = 0;
	double LegacyTopZ = 0.0;
	double SelectedSemanticSeamZ = 0.0;
	double SelectedTopZ = 0.0;
	bool bUsesSemanticSeam = false;
	bool bSelectedCandidateStartsCrown = false;
	bool bAppliedToEnvelope = false;
};

struct FABTSM73DAG5BV2GenerationResult
{
	FABTSM73DAG5BV2PreviewSummary Summary;
	TArray<FABTSM73DAG5BV2Volume> Volumes;
	TArray<FString> GrammarTrace;
	TArray<FABTSM73DAG5BV2SemanticPodiumCandidateDiagnostic>
		SemanticPodiumCandidateDiagnostics;
	TArray<FABTSM73DAG5BV2SemanticPodiumSelectionDiagnostic>
		SemanticPodiumSelectionDiagnostics;
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
