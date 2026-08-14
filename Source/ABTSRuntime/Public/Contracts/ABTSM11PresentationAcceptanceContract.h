// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Presentation-safe route selected for one canonical M11 launch input. */
enum class EABTSM11PresentationRoute : uint8
{
	Invalid = 0,
	StrictF4Success,
	EarlyPhysicalContactSuccess,
	DirectedFailureRecovery,
	OrdinaryFlightFallbackRecovery
};

/** Success authority exposed to the terminal presentation. */
enum class EABTSM11PresentationEndpointAuthority : uint8
{
	None = 0,
	CandidateQualified,
	PhysicalContact
};

/** Strict M11-B certification remains independent from presentation acceptance. */
enum class EABTSM11StrictCertificationStatus : uint8
{
	Unknown = 0,
	StrictUncertified,
	StrictCertified
};

/** Frozen identity copied from one Editor-only M11 candidate catalog entry. */
struct ABTSRUNTIME_API FABTSM11PresentationCandidateIdentity
{
	int32 CandidateRank = 0;
	uint64 GlobalWorkIndex = 0;
	uint64 CandidateSourceHash = 0;
	uint64 NominalRequestHash = 0;
	uint64 NominalResultHash = 0;
	uint64 ScoreHash = 0;

	bool IsValid(FString* OutFailure = nullptr) const;
	uint64 ComputeIdentityHash() const;
};

/**
 * Stable v1 semantics for the emergency presentation-safety acceptance gate.
 *
 * This policy deliberately does not certify F4 topology, play width, trust
 * regions, ablations, or assist quality. It only accepts routes that the
 * frozen camera/playback/failure state machine can complete deterministically.
 */
struct ABTSRUNTIME_API FABTSM11PresentationAcceptancePolicy
{
	int32 ContractVersion = 1;
	int32 RouteSchemaVersion = 1;
	int32 EvidenceHashSchemaVersion = 1;
	int32 ManifestHashSchemaVersion = 1;
	int32 RequiredAssistCount = 3;
	bool bAllowEarlyPhysicalContactSuccess = true;
	bool bAllowOrdinaryFlightFallbackForFailure = true;

	bool IsValid(FString* OutFailure = nullptr) const;
	uint64 ComputePolicyHash() const;

	static FABTSM11PresentationAcceptancePolicy MakeV1();
};

/** Determinism identity produced by replaying the same route set at 30/60/120 Hz. */
struct ABTSRUNTIME_API FABTSM11PresentationReplayIdentity
{
	int32 ReplayVersion = 1;
	uint64 ResultHash30Hz = 0;
	uint64 ResultHash60Hz = 0;
	uint64 ResultHash120Hz = 0;

	bool IsDeterministic(FString* OutFailure = nullptr) const;
	uint64 ComputeIdentityHash() const;
};

/** Machine-verifiable evidence for one canonically ordered launch input. */
struct ABTSRUNTIME_API FABTSM11PresentationRouteEvidence
{
	int32 EvidenceVersion = 1;
	int32 InputOrdinal = INDEX_NONE;
	uint64 PolicyHash = 0;
	uint64 CandidateIdentityHash = 0;
	uint64 LaunchInputHash = 0;
	uint64 TrajectoryHash = 0;
	uint64 PlaybackPlanHash = 0;
	uint64 ShotPlanHash = 0;
	uint64 TerminalPresentationHash = 0;
	uint64 OutcomeHash = 0;
	EABTSM11PresentationRoute Route = EABTSM11PresentationRoute::Invalid;
	EABTSM11PresentationEndpointAuthority EndpointAuthority =
		EABTSM11PresentationEndpointAuthority::None;
	int32 CompletedAssistCount = 0;
	bool bStrictF4 = false;
	bool bAuthorityDataFiniteAndOrdered = false;
	bool bPlaybackPlanBuilt = false;
	bool bPlaybackPlanMatchesTrajectory = false;
	bool bShotPlanBuilt = false;
	bool bShotPlanMatchesTrajectory = false;
	bool bTerminalPresentationBuilt = false;
	bool bTerminalTransferBuilt = false;
	bool bTargetHit = false;
	bool bCameraDirectorUnavailable = false;
	bool bOrdinaryFlightFallbackUsed = false;
	bool bFailedStateObserved = false;
	bool bRecoveringStateObserved = false;
	bool bReadyStateObserved = false;
	uint64 EvidenceHash = 0;

	uint64 ComputeEvidenceHash() const;
	bool IsAcceptedBy(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		FString* OutFailure = nullptr) const;
};

/** Deterministically sealed result of a complete presentation-domain replay. */
struct ABTSRUNTIME_API FABTSM11PresentationAcceptanceManifest
{
	int32 ManifestVersion = 1;
	uint64 PolicyHash = 0;
	uint64 CandidateIdentityHash = 0;
	uint64 InputDomainHash = 0;
	uint64 PresentationImplementationHash = 0;
	FABTSM11PresentationReplayIdentity ReplayIdentity;
	uint64 ReplayIdentityHash = 0;
	EABTSM11StrictCertificationStatus StrictCertificationStatus =
		EABTSM11StrictCertificationStatus::Unknown;
	int32 TotalInputCount = 0;
	int32 StrictF4SuccessCount = 0;
	int32 EarlyPhysicalContactSuccessCount = 0;
	int32 DirectedFailureRecoveryCount = 0;
	int32 OrdinaryFlightFallbackRecoveryCount = 0;
	int32 RejectedInputCount = 0;
	uint64 EvidenceAggregateHash = 0;
	uint64 ManifestHash = 0;
	bool bPresentationAccepted = false;
	FString Failure;

	bool IsValidFor(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		FString* OutFailure = nullptr) const;
};

/**
 * Integration-owned binding of one accepted manifest to candidate production.
 *
 * An unbound value keeps the existing StrictCertified v1 production default.
 * Presentation evidence alone never authorizes a candidate for production.
 */
struct ABTSRUNTIME_API FABTSM11PresentationProductionBinding
{
	int32 BindingVersion = 1;
	int32 CandidateRank = 0;
	uint64 PolicyHash = 0;
	uint64 CandidateIdentityHash = 0;
	uint64 AcceptanceManifestHash = 0;
	uint64 BindingHash = 0;

	bool IsUnbound() const;
	uint64 ComputeBindingHash() const;
	bool IsValidFor(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const FABTSM11PresentationAcceptanceManifest& Manifest,
		FString* OutFailure = nullptr) const;
};

/** Shared evaluator and Integration-owned production binding authority. */
class ABTSRUNTIME_API FABTSM11PresentationAcceptanceContract final
{
public:
	static bool BuildManifest(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		EABTSM11StrictCertificationStatus StrictCertificationStatus,
		uint64 InputDomainHash,
		uint64 PresentationImplementationHash,
		const FABTSM11PresentationReplayIdentity& ReplayIdentity,
		TConstArrayView<FABTSM11PresentationRouteEvidence> Evidence,
		FABTSM11PresentationAcceptanceManifest& OutManifest,
		FString* OutFailure = nullptr);

	static uint64 ComputeEvidenceAggregateHash(
		TConstArrayView<FABTSM11PresentationRouteEvidence> Evidence);
	static uint64 ComputeManifestHash(
		const FABTSM11PresentationAcceptanceManifest& Manifest);

	/** Current v1 value is intentionally unbound until M11 evidence and PIE land. */
	static FABTSM11PresentationProductionBinding
	GetFrozenProductionBindingV1();

	static bool IsProductionConsumptionAllowed(
		const FABTSM11PresentationAcceptancePolicy& Policy,
		const FABTSM11PresentationCandidateIdentity& Candidate,
		const FABTSM11PresentationAcceptanceManifest& Manifest,
		FString* OutFailure = nullptr);
};
