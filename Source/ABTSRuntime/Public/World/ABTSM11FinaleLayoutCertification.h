// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "World/ABTSM11FinaleLayoutTypes.h"

struct ABTSRUNTIME_API FABTSM11CertificationSuiteReport
{
	int32 SuiteVersion = 1;
	FABTSM11LayoutCertificationReport Baseline;
	FABTSM11LayoutCertificationReport HalfCellBaseline;
	FABTSM11LayoutCertificationReport RefinedBaseline;
	TStaticArray<FABTSM11LayoutCertificationReport, 4> Ablations;
	TStaticArray<FABTSM11LayoutCertificationReport, 4> HalfCellAblations;
	TStaticArray<FABTSM11LayoutCertificationReport, 4> RefinedAblations;
	TStaticArray<uint8, 4> AblationMasks = {0x6u, 0x5u, 0x3u, 0x0u};
	int32 DiscoverySampleCount = 0;
	int32 RefinementSampleCount = 0;
	int32 TotalSolverInvocationCount = 0;
	int32 RefinementIterationCount = 0;
	bool bDiscoveryCoverageComplete = false;
	bool bClosureConverged = false;
	uint64 SuiteHash = 0;
	bool bPassed = false;
	FString Failure;
};

/** Strict event/side/quality classifier shared by search, certification and M11-C. */
class ABTSRUNTIME_API FABTSM11PrefixClassifier final
{
public:
	static FABTSM11PrefixClassification Classify(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		uint8 EnabledAssistMask);
};

/**
 * Deterministic full-domain certification. Solver calls may run in parallel,
 * but aggregation, connectivity and hashing always use canonical grid order.
 */
class ABTSRUNTIME_API FABTSM11FinaleLayoutCertification final
{
public:
	static bool ScanGrid(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11InputGrid& Grid,
		uint8 EnabledAssistMask,
		FABTSM11LayoutCertificationReport& OutReport,
		FString* OutFailure = nullptr);

	static bool ScanRegularGrid(
		const FABTSM11FinaleLayoutPreset& Preset,
		uint8 EnabledAssistMask,
		FABTSM11LayoutCertificationReport& OutReport,
		FString* OutFailure = nullptr);

	static bool Certify(
		const FABTSM11FinaleLayoutPreset& Preset,
		FABTSM11CertificationSuiteReport& OutSuite,
		FString* OutFailure = nullptr);

	/** Public for deterministic connectivity fixtures. */
	static int32 CountComponents6(
		TConstArrayView<FABTSM11CertificationSample> Samples,
		int32 YawCount,
		int32 PitchCount,
		int32 PowerCount,
		int32 PrefixLevel,
		TArray<int32>* OutLabels = nullptr);
};
