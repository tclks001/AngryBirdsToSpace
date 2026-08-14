// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11V3ConnectivityClosure.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <type_traits>

namespace ABTS::M11V3
{
	namespace
	{
		constexpr std::uint64_t FnvOffset = 1469598103934665603ull;
		constexpr std::uint64_t FnvPrime = 1099511628211ull;

		bool Reject(std::string* OutFailure, const char* Reason)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = Reason;
			}
			return false;
		}

		struct Hash
		{
			std::uint64_t Value = FnvOffset;

			template <typename T>
			void AddPod(const T& Pod)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				static_assert(std::endian::native == std::endian::little);
				const auto* Bytes = reinterpret_cast<const std::uint8_t*>(&Pod);
				for (std::size_t Index = 0; Index < sizeof(T); ++Index)
				{
					Value ^= Bytes[Index];
					Value *= FnvPrime;
				}
			}

			void AddBool(const bool ValueToAdd)
			{
				AddPod(static_cast<std::uint8_t>(ValueToAdd ? 1u : 0u));
			}

			void AddString(const std::string& Text)
			{
				const auto Length = static_cast<std::int32_t>(Text.size());
				AddPod(Length);
				for (const unsigned char Character : Text)
				{
					Value ^= Character;
					Value *= FnvPrime;
				}
			}
		};

		std::uint64_t ComputeEdgeHash(
			const std::int32_t SampleA,
			const std::int32_t SampleB,
			const std::uint8_t ChangedAxisMask)
		{
			Hash Value;
			Value.AddPod(static_cast<std::uint32_t>(0x11b31801u));
			Value.AddPod(SampleA);
			Value.AddPod(SampleB);
			Value.AddPod(ChangedAxisMask);
			return Value.Value;
		}

		struct DisjointSet
		{
			explicit DisjointSet(const std::int32_t Count) : Parent(Count)
			{
				for (std::int32_t Index = 0; Index < Count; ++Index)
				{
					Parent[Index] = Index;
				}
			}

			std::int32_t Find(const std::int32_t Value)
			{
				std::int32_t Root = Value;
				while (Parent[Root] != Root)
				{
					Root = Parent[Root];
				}
				std::int32_t Current = Value;
				while (Parent[Current] != Current)
				{
					const std::int32_t Next = Parent[Current];
					Parent[Current] = Root;
					Current = Next;
				}
				return Root;
			}

			bool Union(const std::int32_t A, const std::int32_t B)
			{
				const std::int32_t RootA = Find(A);
				const std::int32_t RootB = Find(B);
				if (RootA == RootB)
				{
					return false;
				}
				Parent[std::max(RootA, RootB)] = std::min(RootA, RootB);
				return true;
			}

			std::vector<std::int32_t> Parent;
		};

		std::int32_t CountRoots(DisjointSet& Sets, const std::int32_t Count)
		{
			std::set<std::int32_t> Roots;
			for (std::int32_t Index = 0; Index < Count; ++Index)
			{
				Roots.insert(Sets.Find(Index));
			}
			return static_cast<std::int32_t>(Roots.size());
		}

		bool IsEdgeValid(const PendingBridgeEdge& Edge, const GridShape& Shape)
		{
			return Shape.IsValid()
				&& Edge.SampleA >= 0
				&& Edge.SampleB > Edge.SampleA
				&& Edge.SampleB < Shape.GetSampleCount()
				&& Edge.FaceComponentA >= 0
				&& Edge.FaceComponentB >= 0
				&& Edge.FaceComponentA != Edge.FaceComponentB
				&& (Edge.ChangedAxisMask == 0x3u
					|| Edge.ChangedAxisMask == 0x5u
					|| Edge.ChangedAxisMask == 0x6u)
				&& Edge.EdgeHash == ComputeEdgeHash(
					Edge.SampleA, Edge.SampleB, Edge.ChangedAxisMask);
		}

		bool IsPlanValid(const DiscoveryPlan& Plan)
		{
			if (Plan.PlanVersion != 1 || Plan.PrefixLevel < 1
				|| Plan.PrefixLevel > 4 || !Plan.Shape.IsValid()
				|| Plan.ActiveSampleCount <= 0 || Plan.FaceComponentCount <= 0
				|| Plan.DiscoveryComponentCount <= 0
				|| Plan.DiscoveryComponentCount > Plan.FaceComponentCount
				|| static_cast<std::int32_t>(Plan.RequiredBridgeEdges.size())
					!= Plan.FaceComponentCount - Plan.DiscoveryComponentCount
				|| Plan.ActiveMaskHash == 0)
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Plan.RequiredBridgeEdges.size(); ++Index)
			{
				const PendingBridgeEdge& Edge = Plan.RequiredBridgeEdges[Index];
				if (!IsEdgeValid(Edge, Plan.Shape)
					|| (Index > 0
						&& (Plan.RequiredBridgeEdges[Index - 1].SampleA > Edge.SampleA
							|| (Plan.RequiredBridgeEdges[Index - 1].SampleA == Edge.SampleA
								&& Plan.RequiredBridgeEdges[Index - 1].SampleB >= Edge.SampleB))))
				{
					return false;
				}
			}
			return Plan.PlanHash == ComputePlanHash(Plan);
		}

		bool IsEvidenceValid(
			const BridgeClosureEvidence& Evidence,
			const PendingBridgeEdge& Edge,
			const BridgeClosurePolicy& Policy)
		{
			return Policy.IsValid()
				&& Evidence.EvidenceVersion == 1
				&& Evidence.EdgeHash == Edge.EdgeHash
				&& Evidence.PolicyHash == Policy.ComputePolicyHash()
				&& Evidence.RecursionDepth >= 1
				&& Evidence.RecursionDepth <= Policy.MaximumRecursionDepth
				&& Evidence.SampleCount >= 1
				&& Evidence.SampleCount <= Policy.MaximumSampleCountPerBridge
				&& Evidence.PathSampleCount >= 2
				&& Evidence.PathSampleCount <= Evidence.SampleCount
				&& std::abs(Evidence.ReachedYawPrecisionDegrees
					- Policy.FinalYawPrecisionDegrees) <= 1.0e-12
				&& std::abs(Evidence.ReachedPitchPrecisionDegrees
					- Policy.FinalPitchPrecisionDegrees) <= 1.0e-12
				&& std::abs(Evidence.ReachedPowerPrecision
					- Policy.FinalPowerPrecision) <= 1.0e-12
				&& Evidence.VisitOrderHash != 0
				&& Evidence.ContinuousPathHash != 0
				&& Evidence.ReachedFinalPrecision
				&& Evidence.ProvenContinuousF4Path
				&& Evidence.EvidenceHash == Evidence.ComputeEvidenceHash();
		}
	}

	bool GridShape::IsValid() const
	{
		return YawCount > 0 && PitchCount > 0 && PowerCount > 0
			&& GetSampleCount() > 0;
	}

	std::int32_t GridShape::GetSampleCount() const
	{
		const std::int64_t Count = static_cast<std::int64_t>(YawCount)
			* PitchCount * PowerCount;
		return Count > 0 && Count <= std::numeric_limits<std::int32_t>::max()
			? static_cast<std::int32_t>(Count) : 0;
	}

	bool BridgeClosurePolicy::IsValid(std::string* OutFailure) const
	{
		if (PolicyVersion != 1 || RegionConstructionVersion != 1
			|| RecursiveSubdivisionVersion != 1 || VisitOrderVersion != 1
			|| EvidenceHashSchemaVersion != 1)
		{
			return Reject(OutFailure, "UnsupportedBridgeClosurePolicy");
		}
		if (RegionHaloFinalCells < 0 || RegionHaloFinalCells > 8
			|| MaximumRecursionDepth < 1 || MaximumRecursionDepth > 12
			|| MaximumSampleCountPerBridge < 1
			|| !std::isfinite(FinalYawPrecisionDegrees)
			|| !std::isfinite(FinalPitchPrecisionDegrees)
			|| !std::isfinite(FinalPowerPrecision)
			|| FinalYawPrecisionDegrees <= 0.0
			|| FinalPitchPrecisionDegrees <= 0.0
			|| FinalPowerPrecision <= 0.0)
		{
			return Reject(OutFailure, "InvalidBridgeClosureBudget");
		}
		return true;
	}

	std::uint64_t BridgeClosurePolicy::ComputePolicyHash() const
	{
		Hash Value;
		Value.AddPod(static_cast<std::uint32_t>(0x11b3c001u));
		Value.AddPod(PolicyVersion);
		Value.AddPod(RegionConstructionVersion);
		Value.AddPod(RecursiveSubdivisionVersion);
		Value.AddPod(VisitOrderVersion);
		Value.AddPod(EvidenceHashSchemaVersion);
		Value.AddPod(RegionHaloFinalCells);
		Value.AddPod(MaximumRecursionDepth);
		Value.AddPod(MaximumSampleCountPerBridge);
		Value.AddPod(FinalYawPrecisionDegrees);
		Value.AddPod(FinalPitchPrecisionDegrees);
		Value.AddPod(FinalPowerPrecision);
		return Value.Value;
	}

	std::uint64_t BridgeClosureEvidence::ComputeEvidenceHash() const
	{
		Hash Value;
		Value.AddPod(static_cast<std::uint32_t>(0x11b3e001u));
		Value.AddPod(EvidenceVersion);
		Value.AddPod(EdgeHash);
		Value.AddPod(PolicyHash);
		Value.AddPod(RecursionDepth);
		Value.AddPod(SampleCount);
		Value.AddPod(PathSampleCount);
		Value.AddPod(ReachedYawPrecisionDegrees);
		Value.AddPod(ReachedPitchPrecisionDegrees);
		Value.AddPod(ReachedPowerPrecision);
		Value.AddPod(VisitOrderHash);
		Value.AddPod(ContinuousPathHash);
		Value.AddBool(ReachedFinalPrecision);
		Value.AddBool(ProvenContinuousF4Path);
		return Value.Value;
	}

	std::int32_t Flatten(
		const std::int32_t Yaw,
		const std::int32_t Pitch,
		const std::int32_t Power,
		const GridShape& Shape)
	{
		return Yaw + Shape.YawCount * (Pitch + Shape.PitchCount * Power);
	}

	void Unflatten(
		const std::int32_t Index,
		const GridShape& Shape,
		std::int32_t& OutYaw,
		std::int32_t& OutPitch,
		std::int32_t& OutPower)
	{
		OutYaw = Index % Shape.YawCount;
		const std::int32_t Remainder = Index / Shape.YawCount;
		OutPitch = Remainder % Shape.PitchCount;
		OutPower = Remainder / Shape.PitchCount;
	}

	bool BuildDiscoveryPlan18(
		const std::vector<std::uint8_t>& ActiveMask,
		const GridShape& Shape,
		const std::int32_t PrefixLevel,
		DiscoveryPlan& OutPlan,
		std::string* OutFailure)
	{
		OutPlan = DiscoveryPlan{};
		OutPlan.PrefixLevel = PrefixLevel;
		OutPlan.Shape = Shape;
		if (!Shape.IsValid()
			|| ActiveMask.size() != static_cast<std::size_t>(Shape.GetSampleCount())
			|| PrefixLevel < 1 || PrefixLevel > 4)
		{
			return Reject(OutFailure, "InvalidConnectivityDiscoveryInput");
		}

		Hash MaskHash;
		MaskHash.AddPod(static_cast<std::uint32_t>(0x11b31802u));
		for (const std::uint8_t Item : ActiveMask)
		{
			const std::uint8_t Canonical = Item != 0 ? 1u : 0u;
			MaskHash.AddPod(Canonical);
			OutPlan.ActiveSampleCount += Canonical;
		}
		OutPlan.ActiveMaskHash = MaskHash.Value;
		if (OutPlan.ActiveSampleCount == 0)
		{
			return Reject(OutFailure, "EmptyConnectivityDiscoverySet");
		}

		std::vector<std::int32_t> Labels(ActiveMask.size(), -1);
		std::vector<std::int32_t> Queue;
		Queue.reserve(ActiveMask.size());
		constexpr std::int32_t FaceDeltas[6][3] = {
			{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
			{0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
		for (std::int32_t Seed = 0; Seed < Shape.GetSampleCount(); ++Seed)
		{
			if (ActiveMask[Seed] == 0 || Labels[Seed] >= 0)
			{
				continue;
			}
			const std::int32_t Label = OutPlan.FaceComponentCount++;
			Labels[Seed] = Label;
			Queue.clear();
			Queue.push_back(Seed);
			for (std::size_t Read = 0; Read < Queue.size(); ++Read)
			{
				std::int32_t Yaw = 0, Pitch = 0, Power = 0;
				Unflatten(Queue[Read], Shape, Yaw, Pitch, Power);
				for (const auto& Delta : FaceDeltas)
				{
					const std::int32_t NYaw = Yaw + Delta[0];
					const std::int32_t NPitch = Pitch + Delta[1];
					const std::int32_t NPower = Power + Delta[2];
					if (NYaw < 0 || NYaw >= Shape.YawCount
						|| NPitch < 0 || NPitch >= Shape.PitchCount
						|| NPower < 0 || NPower >= Shape.PowerCount)
					{
						continue;
					}
					const std::int32_t Neighbor = Flatten(NYaw, NPitch, NPower, Shape);
					if (ActiveMask[Neighbor] != 0 && Labels[Neighbor] < 0)
					{
						Labels[Neighbor] = Label;
						Queue.push_back(Neighbor);
					}
				}
			}
		}

		std::vector<PendingBridgeEdge> CandidateEdges;
		for (std::int32_t SampleA = 0; SampleA < Shape.GetSampleCount(); ++SampleA)
		{
			if (ActiveMask[SampleA] == 0)
			{
				continue;
			}
			std::int32_t Yaw = 0, Pitch = 0, Power = 0;
			Unflatten(SampleA, Shape, Yaw, Pitch, Power);
			for (std::int32_t DeltaYaw = -1; DeltaYaw <= 1; ++DeltaYaw)
			for (std::int32_t DeltaPitch = -1; DeltaPitch <= 1; ++DeltaPitch)
			for (std::int32_t DeltaPower = -1; DeltaPower <= 1; ++DeltaPower)
			{
				const std::int32_t ChangedAxes = (DeltaYaw != 0 ? 1 : 0)
					+ (DeltaPitch != 0 ? 1 : 0) + (DeltaPower != 0 ? 1 : 0);
				if (ChangedAxes != 2)
				{
					continue;
				}
				const std::int32_t NYaw = Yaw + DeltaYaw;
				const std::int32_t NPitch = Pitch + DeltaPitch;
				const std::int32_t NPower = Power + DeltaPower;
				if (NYaw < 0 || NYaw >= Shape.YawCount
					|| NPitch < 0 || NPitch >= Shape.PitchCount
					|| NPower < 0 || NPower >= Shape.PowerCount)
				{
					continue;
				}
				const std::int32_t SampleB = Flatten(NYaw, NPitch, NPower, Shape);
				if (SampleB <= SampleA || ActiveMask[SampleB] == 0
					|| Labels[SampleA] == Labels[SampleB])
				{
					continue;
				}
				PendingBridgeEdge Edge;
				Edge.SampleA = SampleA;
				Edge.SampleB = SampleB;
				Edge.FaceComponentA = Labels[SampleA];
				Edge.FaceComponentB = Labels[SampleB];
				Edge.ChangedAxisMask = static_cast<std::uint8_t>(
					(DeltaYaw != 0 ? 0x1u : 0u)
					| (DeltaPitch != 0 ? 0x2u : 0u)
					| (DeltaPower != 0 ? 0x4u : 0u));
				Edge.EdgeHash = ComputeEdgeHash(
					Edge.SampleA, Edge.SampleB, Edge.ChangedAxisMask);
				CandidateEdges.push_back(Edge);
			}
		}
		std::sort(CandidateEdges.begin(), CandidateEdges.end(),
			[](const PendingBridgeEdge& A, const PendingBridgeEdge& B)
			{
				return A.SampleA != B.SampleA ? A.SampleA < B.SampleA
					: A.SampleB < B.SampleB;
			});

		DisjointSet Components(OutPlan.FaceComponentCount);
		for (const PendingBridgeEdge& Edge : CandidateEdges)
		{
			if (Components.Union(Edge.FaceComponentA, Edge.FaceComponentB))
			{
				OutPlan.RequiredBridgeEdges.push_back(Edge);
			}
		}
		OutPlan.DiscoveryComponentCount = CountRoots(
			Components, OutPlan.FaceComponentCount);
		OutPlan.PlanHash = ComputePlanHash(OutPlan);
		if (!IsPlanValid(OutPlan))
		{
			return Reject(OutFailure, "InvalidConnectivityDiscoveryPlan");
		}
		return true;
	}

	std::uint64_t ComputePlanHash(const DiscoveryPlan& Plan)
	{
		Hash Value;
		Value.AddPod(static_cast<std::uint32_t>(0x11b31803u));
		Value.AddPod(Plan.PlanVersion);
		Value.AddPod(Plan.PrefixLevel);
		Value.AddPod(Plan.Shape.YawCount);
		Value.AddPod(Plan.Shape.PitchCount);
		Value.AddPod(Plan.Shape.PowerCount);
		Value.AddPod(Plan.ActiveSampleCount);
		Value.AddPod(Plan.FaceComponentCount);
		Value.AddPod(Plan.DiscoveryComponentCount);
		Value.AddPod(Plan.ActiveMaskHash);
		Value.AddPod(static_cast<std::int32_t>(Plan.RequiredBridgeEdges.size()));
		for (const PendingBridgeEdge& Edge : Plan.RequiredBridgeEdges)
		{
			Value.AddPod(Edge.SampleA);
			Value.AddPod(Edge.SampleB);
			Value.AddPod(Edge.FaceComponentA);
			Value.AddPod(Edge.FaceComponentB);
			Value.AddPod(Edge.ChangedAxisMask);
			Value.AddPod(Edge.EdgeHash);
		}
		return Value.Value;
	}

	bool CloseWithEvidence(
		const DiscoveryPlan& Plan,
		const BridgeClosurePolicy& Policy,
		const std::vector<BridgeClosureEvidence>& Evidence,
		ClosureResult& OutResult,
		std::string* OutFailure)
	{
		OutResult = ClosureResult{};
		OutResult.PlanHash = Plan.PlanHash;
		OutResult.PolicyHash = Policy.ComputePolicyHash();
		OutResult.RequiredBridgeCount = static_cast<std::int32_t>(
			Plan.RequiredBridgeEdges.size());
		if (!IsPlanValid(Plan))
		{
			OutResult.Failure = "InvalidConnectivityDiscoveryPlan";
		}
		else if (!Policy.IsValid(&OutResult.Failure))
		{
		}
		if (!OutResult.Failure.empty())
		{
			OutResult.ResultHash = ComputeResultHash(OutResult);
			return Reject(OutFailure, OutResult.Failure.c_str());
		}

		std::map<std::uint64_t, const BridgeClosureEvidence*> ByEdge;
		for (const BridgeClosureEvidence& Item : Evidence)
		{
			if (ByEdge.contains(Item.EdgeHash))
			{
				OutResult.Failure = "DuplicateBridgeClosureEvidence";
				break;
			}
			ByEdge.emplace(Item.EdgeHash, &Item);
		}
		DisjointSet Components(Plan.FaceComponentCount);
		Hash Aggregate;
		Aggregate.AddPod(static_cast<std::uint32_t>(0x11b3e002u));
		if (OutResult.Failure.empty())
		{
			for (const auto& [EdgeHash, Item] : ByEdge)
			{
				(void)Item;
				const bool Requested = std::any_of(
					Plan.RequiredBridgeEdges.begin(), Plan.RequiredBridgeEdges.end(),
					[EdgeHash](const PendingBridgeEdge& Edge)
					{
						return Edge.EdgeHash == EdgeHash;
					});
				if (!Requested)
				{
					OutResult.Failure = "UnexpectedBridgeClosureEvidence";
					break;
				}
			}
		}
		if (OutResult.Failure.empty())
		{
			for (const PendingBridgeEdge& Edge : Plan.RequiredBridgeEdges)
			{
				const auto Found = ByEdge.find(Edge.EdgeHash);
				if (Found == ByEdge.end())
				{
					OutResult.Failure = "MissingBridgeClosureEvidence";
					break;
				}
				if (!IsEvidenceValid(*Found->second, Edge, Policy))
				{
					OutResult.Failure = "IncompleteBridgeClosureEvidence";
					break;
				}
				Components.Union(Edge.FaceComponentA, Edge.FaceComponentB);
				Aggregate.AddPod(Found->second->EvidenceHash);
				++OutResult.ProvenBridgeCount;
			}
		}
		OutResult.FinalComponentCount = CountRoots(
			Components, Plan.FaceComponentCount);
		OutResult.BridgeEvidenceAggregateHash = Aggregate.Value;
		OutResult.EvidenceComplete = OutResult.Failure.empty()
			&& OutResult.ProvenBridgeCount == OutResult.RequiredBridgeCount;
		if (OutResult.EvidenceComplete && Plan.DiscoveryComponentCount != 1)
		{
			OutResult.Failure = "DiscoveryGraphNotSingleComponent";
		}
		else if (OutResult.EvidenceComplete && OutResult.FinalComponentCount != 1)
		{
			OutResult.Failure = "BridgeClosureNotSingleComponent";
		}
		OutResult.Passed = OutResult.Failure.empty() && OutResult.EvidenceComplete
			&& OutResult.FinalComponentCount == 1;
		OutResult.ResultHash = ComputeResultHash(OutResult);
		if (!OutResult.Passed)
		{
			return Reject(OutFailure, OutResult.Failure.c_str());
		}
		return true;
	}

	std::uint64_t ComputeResultHash(const ClosureResult& Result)
	{
		Hash Value;
		Value.AddPod(static_cast<std::uint32_t>(0x11b3c002u));
		Value.AddPod(Result.ResultVersion);
		Value.AddPod(Result.PlanHash);
		Value.AddPod(Result.PolicyHash);
		Value.AddPod(Result.RequiredBridgeCount);
		Value.AddPod(Result.ProvenBridgeCount);
		Value.AddPod(Result.FinalComponentCount);
		Value.AddPod(Result.BridgeEvidenceAggregateHash);
		Value.AddBool(Result.EvidenceComplete);
		Value.AddBool(Result.Passed);
		Value.AddString(Result.Failure);
		return Value.Value;
	}

	bool RunContractSelfTest(std::string* OutFailure)
	{
		const GridShape Shape{3, 3, 1};
		std::vector<std::uint8_t> Mask(9, 0);
		Mask[0] = Mask[4] = Mask[8] = 1;
		DiscoveryPlan Plan;
		if (!BuildDiscoveryPlan18(Mask, Shape, 4, Plan, OutFailure)
			|| Plan.FaceComponentCount != 3
			|| Plan.DiscoveryComponentCount != 1
			|| Plan.RequiredBridgeEdges.size() != 2
			|| Plan.PlanHash != 0xa84e0b6460214b23ull)
		{
			return Reject(OutFailure, "DiscoveryFixtureMismatch");
		}
		BridgeClosurePolicy Policy;
		if (Policy.ComputePolicyHash() != 0x40ccc25283f67c8cull)
		{
			return Reject(OutFailure, "PolicyFixtureMismatch");
		}
		std::vector<BridgeClosureEvidence> Evidence;
		for (std::int32_t Index = 0; Index < 2; ++Index)
		{
			BridgeClosureEvidence Item;
			Item.EdgeHash = Plan.RequiredBridgeEdges[Index].EdgeHash;
			Item.PolicyHash = Policy.ComputePolicyHash();
			Item.RecursionDepth = Policy.MaximumRecursionDepth;
			Item.SampleCount = 16 + Index;
			Item.PathSampleCount = 4 + Index;
			Item.ReachedYawPrecisionDegrees = Policy.FinalYawPrecisionDegrees;
			Item.ReachedPitchPrecisionDegrees = Policy.FinalPitchPrecisionDegrees;
			Item.ReachedPowerPrecision = Policy.FinalPowerPrecision;
			Item.VisitOrderHash = 0x100ull + Index;
			Item.ContinuousPathHash = 0x200ull + Index;
			Item.ReachedFinalPrecision = true;
			Item.ProvenContinuousF4Path = true;
			Item.EvidenceHash = Item.ComputeEvidenceHash();
			Evidence.push_back(Item);
		}
		ClosureResult Result;
		if (!CloseWithEvidence(Plan, Policy, Evidence, Result, OutFailure)
			|| !Result.Passed || Result.FinalComponentCount != 1)
		{
			return Reject(OutFailure, "ClosureFixtureMismatch");
		}
		return true;
	}
}
