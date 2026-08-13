// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ABTS::M11V3
{
	struct GridShape
	{
		std::int32_t YawCount = 0;
		std::int32_t PitchCount = 0;
		std::int32_t PowerCount = 0;

		[[nodiscard]] bool IsValid() const;
		[[nodiscard]] std::int32_t GetSampleCount() const;
	};

	struct BridgeClosurePolicy
	{
		std::int32_t PolicyVersion = 1;
		std::int32_t RegionConstructionVersion = 1;
		std::int32_t RecursiveSubdivisionVersion = 1;
		std::int32_t VisitOrderVersion = 1;
		std::int32_t EvidenceHashSchemaVersion = 1;
		std::int32_t RegionHaloFinalCells = 1;
		std::int32_t MaximumRecursionDepth = 3;
		std::int32_t MaximumSampleCountPerBridge = 32768;
		double FinalYawPrecisionDegrees = 0.1875;
		double FinalPitchPrecisionDegrees = 0.25;
		double FinalPowerPrecision = 0.003125;

		[[nodiscard]] bool IsValid(std::string* OutFailure = nullptr) const;
		[[nodiscard]] std::uint64_t ComputePolicyHash() const;
	};

	struct PendingBridgeEdge
	{
		std::int32_t SampleA = -1;
		std::int32_t SampleB = -1;
		std::int32_t FaceComponentA = -1;
		std::int32_t FaceComponentB = -1;
		std::uint8_t ChangedAxisMask = 0;
		std::uint64_t EdgeHash = 0;
	};

	struct DiscoveryPlan
	{
		std::int32_t PlanVersion = 1;
		std::int32_t PrefixLevel = 0;
		GridShape Shape;
		std::int32_t ActiveSampleCount = 0;
		std::int32_t FaceComponentCount = 0;
		std::int32_t DiscoveryComponentCount = 0;
		std::vector<PendingBridgeEdge> RequiredBridgeEdges;
		std::uint64_t ActiveMaskHash = 0;
		std::uint64_t PlanHash = 0;
	};

	struct BridgeClosureEvidence
	{
		std::int32_t EvidenceVersion = 1;
		std::uint64_t EdgeHash = 0;
		std::uint64_t PolicyHash = 0;
		std::int32_t RecursionDepth = 0;
		std::int32_t SampleCount = 0;
		std::int32_t PathSampleCount = 0;
		double ReachedYawPrecisionDegrees = 0.0;
		double ReachedPitchPrecisionDegrees = 0.0;
		double ReachedPowerPrecision = 0.0;
		std::uint64_t VisitOrderHash = 0;
		std::uint64_t ContinuousPathHash = 0;
		bool ReachedFinalPrecision = false;
		bool ProvenContinuousF4Path = false;
		std::uint64_t EvidenceHash = 0;

		[[nodiscard]] std::uint64_t ComputeEvidenceHash() const;
	};

	struct ClosureResult
	{
		std::int32_t ResultVersion = 1;
		std::uint64_t PlanHash = 0;
		std::uint64_t PolicyHash = 0;
		std::int32_t RequiredBridgeCount = 0;
		std::int32_t ProvenBridgeCount = 0;
		std::int32_t FinalComponentCount = 0;
		std::uint64_t BridgeEvidenceAggregateHash = 0;
		std::uint64_t ResultHash = 0;
		bool EvidenceComplete = false;
		bool Passed = false;
		std::string Failure;
	};

	[[nodiscard]] std::int32_t Flatten(
		std::int32_t Yaw,
		std::int32_t Pitch,
		std::int32_t Power,
		const GridShape& Shape);
	void Unflatten(
		std::int32_t Index,
		const GridShape& Shape,
		std::int32_t& OutYaw,
		std::int32_t& OutPitch,
		std::int32_t& OutPower);

	[[nodiscard]] bool BuildDiscoveryPlan18(
		const std::vector<std::uint8_t>& ActiveMask,
		const GridShape& Shape,
		std::int32_t PrefixLevel,
		DiscoveryPlan& OutPlan,
		std::string* OutFailure = nullptr);
	[[nodiscard]] std::uint64_t ComputePlanHash(const DiscoveryPlan& Plan);
	[[nodiscard]] bool CloseWithEvidence(
		const DiscoveryPlan& Plan,
		const BridgeClosurePolicy& Policy,
		const std::vector<BridgeClosureEvidence>& Evidence,
		ClosureResult& OutResult,
		std::string* OutFailure = nullptr);
	[[nodiscard]] std::uint64_t ComputeResultHash(const ClosureResult& Result);

	/** Exact byte-level parity fixture for the integration-owned UE contract. */
	[[nodiscard]] bool RunContractSelfTest(std::string* OutFailure = nullptr);
}
