// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73DAG5Types.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGGenerationSettings;
struct FABTSM73DAGLayoutSettings;
struct FABTSM73DAGSpatialLayout;
struct FABTSM73StructureData;

/**
 * DAG5-B pure-data front end.
 *
 * Shape Grammar selects a coarse authored family, bounded local WFC resolves
 * its semantic cells, and the accepted envelope emits a support DAG plus the
 * initial macro layout consumed by DAG2.3. It never spawns Actors or touches a
 * World.
 */
class FABTSM73DAG5BSemanticEnvelopeBuilder
{
public:
	bool Build(
		const FABTSM73DAG5BSettings& Settings,
		const FABTSM73DAGGenerationSettings& DAGSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		FABTSM73DAGGenerationResult& OutGraph,
		FABTSM73DAGSpatialLayout& OutInitialLayout,
		FABTSM73SemanticEnvelope& OutEnvelope,
		FABTSM73DAG5BResult& OutResult,
		FString& OutError) const;

	/** Converts semantic anchors into exact post-layout physical audit boxes. */
	bool BindPhysicalContract(
		const FABTSM73DAGSpatialLayout& Layout,
		FABTSM73SemanticEnvelope& InOutEnvelope,
		FString& OutError) const;

	/** Recomputes the sealed canonical identity from the stored artifact. */
	bool ValidateEnvelopeIdentity(
		const FABTSM73SemanticEnvelope& Envelope,
		FString& OutError) const;

	/** Recomputes a support port from its raw WFC source cells. */
	bool ValidateSupportPortProvenance(
		const FABTSM73SemanticEnvelope& Envelope,
		const FABTSM73DAG5BSupportPortConstraint& Port,
		FString& OutError) const;
};

/** Independent final-Brick audit of an accepted SemanticEnvelope. */
class FABTSM73DAG5BEnvelopeAuditor
{
public:
	bool Audit(
		const FABTSM73SemanticEnvelope& Envelope,
		const FABTSM73StructureData& Data,
		FABTSM73DAG5BAuditResult& OutResult,
		FString& OutError) const;
};
