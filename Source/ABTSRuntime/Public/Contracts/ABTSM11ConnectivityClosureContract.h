// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Frozen grid-neighborhood semantics used by M11 certification discovery. */
enum class EABTSM11DiscoveryAdjacency : uint8
{
	Face6 = 6,
	FaceAndEdge18 = 18
};

/** Dimensions of the canonical yaw-major, then pitch, then power grid. */
struct ABTSRUNTIME_API FABTSM11ConnectivityGridShape
{
	int32 YawCount = 0;
	int32 PitchCount = 0;
	int32 PowerCount = 0;

	bool IsValid() const;
	int32 GetSampleCount() const;
};

/**
 * Stable v1 policy for proving a diagonal discovery edge at final precision.
 *
 * Discovery may propose an 18-neighbor edge, but only a recursive bridge
 * proof conforming to this policy may retain it in the certified graph.
 */
struct ABTSRUNTIME_API FABTSM11BridgeClosurePolicy
{
	int32 PolicyVersion = 0;
	int32 RegionConstructionVersion = 0;
	int32 RecursiveSubdivisionVersion = 0;
	int32 VisitOrderVersion = 0;
	int32 EvidenceHashSchemaVersion = 0;
	int32 RegionHaloFinalCells = 0;
	int32 MaximumRecursionDepth = 0;
	int32 MaximumSampleCountPerBridge = 0;
	double FinalYawPrecisionDegrees = 0.0;
	double FinalPitchPrecisionDegrees = 0.0;
	double FinalPowerPrecision = 0.0;

	bool IsDisabled() const;
	bool IsValid(FString* OutFailure = nullptr) const;
	uint64 ComputePolicyHash() const;

	static FABTSM11BridgeClosurePolicy MakeV1(
		double InFinalYawPrecisionDegrees,
		double InFinalPitchPrecisionDegrees,
		double InFinalPowerPrecision);
};

/** Canonically ordered diagonal edge between two active discovery samples. */
struct ABTSRUNTIME_API FABTSM11PendingBridgeEdge
{
	int32 SampleA = INDEX_NONE;
	int32 SampleB = INDEX_NONE;
	int32 FaceComponentA = INDEX_NONE;
	int32 FaceComponentB = INDEX_NONE;
	uint8 ChangedAxisMask = 0;
	uint64 EdgeHash = 0;

	bool IsValid(
		const FABTSM11ConnectivityGridShape& Shape,
		FString* OutFailure = nullptr) const;
};

/**
 * Output of the 18-neighbor discovery precheck.
 *
 * RequiredBridgeEdges is a deterministic spanning forest over the quotient
 * graph of six-neighbor components. It is not certification evidence.
 */
struct ABTSRUNTIME_API FABTSM11ConnectivityDiscoveryPlan
{
	int32 PlanVersion = 1;
	int32 PrefixLevel = 0;
	FABTSM11ConnectivityGridShape Shape;
	int32 ActiveSampleCount = 0;
	int32 FaceComponentCount = 0;
	int32 DiscoveryComponentCount = 0;
	TArray<FABTSM11PendingBridgeEdge> RequiredBridgeEdges;
	uint64 ActiveMaskHash = 0;
	uint64 PlanHash = 0;

	bool IsValid(FString* OutFailure = nullptr) const;
};

/** Machine-verifiable proof supplied by the later M11 portable scanner. */
struct ABTSRUNTIME_API FABTSM11BridgeClosureEvidence
{
	int32 EvidenceVersion = 1;
	uint64 EdgeHash = 0;
	uint64 PolicyHash = 0;
	int32 RecursionDepth = 0;
	int32 SampleCount = 0;
	int32 PathSampleCount = 0;
	double ReachedYawPrecisionDegrees = 0.0;
	double ReachedPitchPrecisionDegrees = 0.0;
	double ReachedPowerPrecision = 0.0;
	uint64 VisitOrderHash = 0;
	uint64 ContinuousPathHash = 0;
	bool bReachedFinalPrecision = false;
	bool bProvenContinuousF4Path = false;
	uint64 EvidenceHash = 0;

	bool IsValidFor(
		const FABTSM11PendingBridgeEdge& Edge,
		const FABTSM11BridgeClosurePolicy& Policy,
		FString* OutFailure = nullptr) const;
	uint64 ComputeEvidenceHash() const;
};

/** Deterministic fail-closed result of applying bridge evidence to a plan. */
struct ABTSRUNTIME_API FABTSM11ConnectivityClosureResult
{
	int32 ResultVersion = 1;
	uint64 PlanHash = 0;
	uint64 PolicyHash = 0;
	int32 RequiredBridgeCount = 0;
	int32 ProvenBridgeCount = 0;
	int32 FinalComponentCount = 0;
	uint64 BridgeEvidenceAggregateHash = 0;
	uint64 ResultHash = 0;
	bool bEvidenceComplete = false;
	bool bPassed = false;
	FString Failure;
};

/** Shared deterministic graph/evidence contract; it never invokes the solver. */
class ABTSRUNTIME_API FABTSM11ConnectivityClosure final
{
public:
	static bool BuildDiscoveryPlan18(
		TConstArrayView<uint8> ActiveMask,
		const FABTSM11ConnectivityGridShape& Shape,
		int32 PrefixLevel,
		FABTSM11ConnectivityDiscoveryPlan& OutPlan,
		FString* OutFailure = nullptr);

	static bool CloseWithEvidence(
		const FABTSM11ConnectivityDiscoveryPlan& Plan,
		const FABTSM11BridgeClosurePolicy& Policy,
		TConstArrayView<FABTSM11BridgeClosureEvidence> Evidence,
		FABTSM11ConnectivityClosureResult& OutResult,
		FString* OutFailure = nullptr);

	static uint64 ComputePlanHash(
		const FABTSM11ConnectivityDiscoveryPlan& Plan);
	static uint64 ComputeResultHash(
		const FABTSM11ConnectivityClosureResult& Result);
};
