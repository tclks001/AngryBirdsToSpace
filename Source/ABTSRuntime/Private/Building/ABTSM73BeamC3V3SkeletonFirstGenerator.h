// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ABTSM73BeamC3V3SkeletonFirstTypes.h"
#include "ABTSM73BeamD0ProfileCatalog.h"
#include "ABTSM73DAG5BShapeGrammarV2.h"

/** Pure-data, deterministic skeleton-first compiler for Beam-C3 V3 Stage-1. */
class FABTSM73BeamC3V3SkeletonFirstGenerator final
{
public:
	/** Hashes only the accepted semantic envelope; it emits no member data. */
	int64 ComputeEnvelopeHashForDiagnostics(
		const FABTSM73DAG5BV2GenerationResult& Silhouette) const;

	bool BuildPlan(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::FPlan& OutPlan,
		FString& OutError) const;

	bool Generate(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::FGenerationResult& OutResult,
		FString& OutError) const;

	/** Emits only grounded CoreCourse, replaced SharedCourse and bridge diaphragms. */
	bool GenerateStage1(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::FGenerationResult& OutResult,
		FString& OutError) const;

	/** First pass for the Stage-0 feedback loop. It emits no new semantic volume
	 * and no final result: only deterministic square main reservations derived
	 * from the accepted local podium-height plan. */
	bool BuildRaisedMainReservations(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		TArray<FABTSM73DAG5BV2RaisedMainReservation>& OutReservations,
		FString& OutError) const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Exercises the production 10-second Stage-1 fail-closed budget without waiting. */
	bool ValidateStage1TimingBudgetForTesting(
		double ElapsedMilliseconds,
		ABTSM73BeamC3V3::FPlan& InOutPlan,
		FString& OutError) const;

	/** Stops at WFC semantics and enumerates minimum grounded bridge-endpoint
	 * cells. It deliberately does not build cores, members, Beam-A IR or DAG. */
	bool EvaluateSharedEndpointReachabilityForTesting(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		TArray<ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic>&
			OutDiagnostics,
		FString& OutError) const;

	/** Re-runs the production full-solid envelope, void and penetration gates
	 * against a deliberately mutated plan. Never used by production routing. */
	bool ValidateGeometryForTesting(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::FPlan& InOutPlan,
		FString& OutError) const;

	/** Exercises the production axis-aligned union containment primitive. */
	bool ValidateSolidCoverageForTesting(
		const FBox& Solid,
		const TArray<FBox>& AllowedBoxes,
		FVector& OutUncoveredPoint) const;

	/** Compares rebuilt Bearing rows with the complete canonical contact plan. */
	bool ValidateBearingContactsForTesting(
		const FABTSM73BeamAPreviewSettings& Settings,
		const ABTSM73BeamC3V3::FPlan& Plan,
		const TArray<FABTSM73BeamABearingContact>& ActualContacts,
		FString& OutError) const;

	/** Re-runs the production distinct grounded exterior Z-post station gate. */
	bool ValidateExteriorPostStationsForTesting(
		ABTSM73BeamC3V3::FPlan& InOutPlan,
		FString& OutError) const;

	/** Re-runs the production grounded-core/core-derived-shell topology contract. */
	bool ValidateSkeletonTopologyForTesting(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::FPlan& InOutPlan,
		FString& OutError) const;
#endif

private:
	bool BuildPlanForStage(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::EGenerationStage Stage,
		ABTSM73BeamC3V3::FPlan& OutPlan,
		FString& OutError) const;

	bool GenerateForStage(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		ABTSM73BeamC3V3::EGenerationStage Stage,
		ABTSM73BeamC3V3::FGenerationResult& OutResult,
		FString& OutError) const;
};
