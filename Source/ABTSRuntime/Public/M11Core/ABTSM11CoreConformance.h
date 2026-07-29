// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "M11Core/ABTSM11CoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ABTS::M11Core::Testing
{
	inline constexpr std::int32_t PortableContractVersion = 1;
	inline constexpr std::int32_t PortableCorpusCaseCount = 11;
	inline constexpr std::uint64_t V1PortableGoldenHash =
		0xd78e8f7153cca7f1ull;

	/** Frozen after the first successful standalone/UE exact parity pass. */
	inline constexpr std::uint64_t V2PortableGoldenHash =
		0xa12de4bf0ac1c0d7ull;

	/**
	 * Stable identifiers for the cross-front-end corpus.
	 *
	 * Values are explicit so JSON artifacts and UE automation output remain
	 * comparable even if the declaration order changes.
	 */
	enum class CorpusCaseId : std::uint32_t
	{
		V1GoldenNaturalFlyby = 0x11a21001u,
		V2StrongAssist = 0x11a21002u,
		ThreeAssistNominal = 0x11a21003u,
		TargetNearBoundary = 0x11a21004u,
		BodyCollision = 0x11a21005u,
		WrongOrder = 0x11a21006u,
		AblateAssist1 = 0x11a21007u,
		AblateAssist2 = 0x11a21008u,
		AblateAssist3 = 0x11a21009u,
		LateTimeout = 0x11a2100au,
		MacroStepFallback = 0x11a2100bu,
		Count = 0x11a2100cu
	};

	/**
	 * One immutable corpus input plus its frozen, portable expectations.
	 *
	 * RequestIdentity identifies the input independently from solver output.
	 * ResultHash and the topology counts identify the authoritative result.
	 */
	struct CorpusCaseDefinition
	{
		CorpusCaseId Id = CorpusCaseId::V1GoldenNaturalFlyby;
		std::string Name;
		TrajectoryRequest Request;
		std::uint64_t ExpectedRequestIdentity = 0;
		std::uint64_t ExpectedResultHash = 0;
		TrajectoryTermination ExpectedTermination =
			TrajectoryTermination::None;
		std::int32_t ExpectedPointCount = -1;
		std::int32_t ExpectedEventCount = -1;
		std::int32_t ExpectedCompletedAssistCount = -1;
	};

	/** Machine-readable evidence for one corpus case. */
	struct CorpusCaseReport
	{
		CorpusCaseId Id = CorpusCaseId::V1GoldenNaturalFlyby;
		std::string Name;
		std::uint64_t RequestIdentity = 0;
		std::uint64_t ResultHash = 0;
		TrajectoryTermination Termination =
			TrajectoryTermination::None;
		std::int32_t PointCount = 0;
		std::int32_t EventCount = 0;
		std::int32_t CompletedAssistCount = 0;
		bool ExpectedOutcomeMatch = false;
		bool RepeatedResultMatch = false;
		bool ParallelResultMatch = false;
		bool Passed = false;
		std::string Diagnostic;
	};

	struct ConformanceReport
	{
		bool Passed = false;
		std::int32_t ContractVersion = PortableContractVersion;
		std::uint64_t V1ValidationHash = 0;
		std::int32_t V1PointCount = 0;
		std::int32_t V1EventCount = 0;
		TrajectoryTermination V1Termination =
			TrajectoryTermination::None;
		std::uint64_t V2ValidationHash = 0;
		std::int32_t V2PointCount = 0;
		std::int32_t V2EventCount = 0;
		TrajectoryTermination V2Termination =
			TrajectoryTermination::None;
		std::uint64_t CorpusAggregateHash = 0;
		bool RepeatedResultsMatch = false;
		bool ParallelResultsMatch = false;
		bool AllCaseExpectationsMatch = false;
		bool InvalidInputFailsClosed = false;
		std::vector<CorpusCaseReport> Cases;
		std::string Diagnostic;
	};

	[[nodiscard]] TrajectoryRequest MakeV1GoldenRequest();
	[[nodiscard]] TrajectoryRequest MakeV2StrongAssistRequest();

	/**
	 * Deliberately non-default sentinels for proving that the Unreal adapter
	 * preserves every request/result/derived-diagnostics field in both
	 * directions. These fixtures are conversion contracts, not solver inputs.
	 */
	[[nodiscard]] TrajectoryRequest MakeAllFieldsRequestSentinel();
	[[nodiscard]] TrajectoryResult MakeAllFieldsResultSentinel();
	[[nodiscard]] TrajectoryPacingDiagnostics
		MakeAllFieldsPacingDiagnosticsSentinel();

	/**
	 * Computes a stable identity over every field in a request. Unlike a
	 * result hash this value is available before Solve and includes
	 * presentation-only request fields to guard adapter round trips.
	 */
	[[nodiscard]] std::uint64_t ComputeRequestIdentity(
		const TrajectoryRequest& Request);

	/**
	 * Constructs the complete shared corpus in stable CorpusCaseId order.
	 * Both standalone and UE tests enumerate this exact API.
	 */
	[[nodiscard]] std::vector<CorpusCaseDefinition>
		MakePortableConformanceCorpus();

	/** Exact field comparison used by standalone and UE adapter parity tests. */
	[[nodiscard]] bool RequestsExactlyEqual(
		const TrajectoryRequest& A,
		const TrajectoryRequest& B);
	[[nodiscard]] bool ResultsExactlyEqual(
		const TrajectoryResult& A,
		const TrajectoryResult& B);
	[[nodiscard]] bool PacingDiagnosticsExactlyEqual(
		const TrajectoryPacingDiagnostics& A,
		const TrajectoryPacingDiagnostics& B);

	/**
	 * Runs the shared, engine-free conformance corpus. The same function is
	 * compiled into the standalone executable and the Unreal runtime module.
	 */
	[[nodiscard]] bool RunPortableConformance(
		ConformanceReport& OutReport,
		std::string* OutFailure = nullptr);
}
