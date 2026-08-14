// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSM11ConnectivityClosureContract.h"

namespace ABTSM11ConnectivityClosurePrivate
{
	constexpr uint64 FnvOffset = 1469598103934665603ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	bool Reject(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	struct FHash
	{
		uint64 Value = FnvOffset;

		template <typename T>
		void AddPod(const T& Pod)
		{
			const uint8* Bytes = reinterpret_cast<const uint8*>(&Pod);
			for (int32 Index = 0; Index < sizeof(T); ++Index)
			{
				Value ^= Bytes[Index];
				Value *= FnvPrime;
			}
		}

		void AddBool(const bool bValue)
		{
			const uint8 Value8 = bValue ? 1u : 0u;
			AddPod(Value8);
		}

		void AddString(const FString& String)
		{
			FTCHARToUTF8 Utf8(*String);
			const int32 Length = Utf8.Length();
			AddPod(Length);
			for (int32 Index = 0; Index < Length; ++Index)
			{
				Value ^= static_cast<uint8>(Utf8.Get()[Index]);
				Value *= FnvPrime;
			}
		}
	};

	int32 Flatten(
		const int32 Yaw,
		const int32 Pitch,
		const int32 Power,
		const FABTSM11ConnectivityGridShape& Shape)
	{
		return Yaw + Shape.YawCount * (Pitch + Shape.PitchCount * Power);
	}

	void Unflatten(
		const int32 Index,
		const FABTSM11ConnectivityGridShape& Shape,
		int32& OutYaw,
		int32& OutPitch,
		int32& OutPower)
	{
		OutYaw = Index % Shape.YawCount;
		const int32 Remainder = Index / Shape.YawCount;
		OutPitch = Remainder % Shape.PitchCount;
		OutPower = Remainder / Shape.PitchCount;
	}

	uint64 ComputeEdgeHash(
		const int32 SampleA,
		const int32 SampleB,
		const uint8 ChangedAxisMask)
	{
		FHash Hash;
		Hash.AddPod(static_cast<uint32>(0x11b31801u));
		Hash.AddPod(SampleA);
		Hash.AddPod(SampleB);
		Hash.AddPod(ChangedAxisMask);
		return Hash.Value;
	}

	struct FDisjointSet
	{
		TArray<int32> Parent;

		explicit FDisjointSet(const int32 Count)
		{
			Parent.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(const int32 Value)
		{
			int32 Root = Value;
			while (Parent[Root] != Root)
			{
				Root = Parent[Root];
			}
			int32 Current = Value;
			while (Parent[Current] != Current)
			{
				const int32 Next = Parent[Current];
				Parent[Current] = Root;
				Current = Next;
			}
			return Root;
		}

		bool Union(const int32 A, const int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA == RootB)
			{
				return false;
			}
			const int32 Minimum = FMath::Min(RootA, RootB);
			const int32 Maximum = FMath::Max(RootA, RootB);
			Parent[Maximum] = Minimum;
			return true;
		}
	};

	int32 CountRoots(FDisjointSet& Sets, const int32 Count)
	{
		TSet<int32> Roots;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Roots.Add(Sets.Find(Index));
		}
		return Roots.Num();
	}
}

bool FABTSM11ConnectivityGridShape::IsValid() const
{
	return YawCount > 0
		&& PitchCount > 0
		&& PowerCount > 0
		&& GetSampleCount() > 0;
}

int32 FABTSM11ConnectivityGridShape::GetSampleCount() const
{
	const int64 Count = static_cast<int64>(YawCount)
		* static_cast<int64>(PitchCount)
		* static_cast<int64>(PowerCount);
	return Count > 0 && Count <= MAX_int32 ? static_cast<int32>(Count) : 0;
}

bool FABTSM11BridgeClosurePolicy::IsDisabled() const
{
	return PolicyVersion == 0
		&& RegionConstructionVersion == 0
		&& RecursiveSubdivisionVersion == 0
		&& VisitOrderVersion == 0
		&& EvidenceHashSchemaVersion == 0
		&& RegionHaloFinalCells == 0
		&& MaximumRecursionDepth == 0
		&& MaximumSampleCountPerBridge == 0
		&& FinalYawPrecisionDegrees == 0.0
		&& FinalPitchPrecisionDegrees == 0.0
		&& FinalPowerPrecision == 0.0;
}

bool FABTSM11BridgeClosurePolicy::IsValid(FString* OutFailure) const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	if (PolicyVersion != 1
		|| RegionConstructionVersion != 1
		|| RecursiveSubdivisionVersion != 1
		|| VisitOrderVersion != 1
		|| EvidenceHashSchemaVersion != 1)
	{
		return Reject(OutFailure, TEXT("UnsupportedBridgeClosurePolicy"));
	}
	if (RegionHaloFinalCells < 0
		|| RegionHaloFinalCells > 8
		|| MaximumRecursionDepth < 1
		|| MaximumRecursionDepth > 12
		|| MaximumSampleCountPerBridge < 1
		|| !FMath::IsFinite(FinalYawPrecisionDegrees)
		|| !FMath::IsFinite(FinalPitchPrecisionDegrees)
		|| !FMath::IsFinite(FinalPowerPrecision)
		|| FinalYawPrecisionDegrees <= 0.0
		|| FinalPitchPrecisionDegrees <= 0.0
		|| FinalPowerPrecision <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidBridgeClosureBudget"));
	}
	return true;
}

uint64 FABTSM11BridgeClosurePolicy::ComputePolicyHash() const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11b3c001u));
	Hash.AddPod(PolicyVersion);
	Hash.AddPod(RegionConstructionVersion);
	Hash.AddPod(RecursiveSubdivisionVersion);
	Hash.AddPod(VisitOrderVersion);
	Hash.AddPod(EvidenceHashSchemaVersion);
	Hash.AddPod(RegionHaloFinalCells);
	Hash.AddPod(MaximumRecursionDepth);
	Hash.AddPod(MaximumSampleCountPerBridge);
	Hash.AddPod(FinalYawPrecisionDegrees);
	Hash.AddPod(FinalPitchPrecisionDegrees);
	Hash.AddPod(FinalPowerPrecision);
	return Hash.Value;
}

FABTSM11BridgeClosurePolicy FABTSM11BridgeClosurePolicy::MakeV1(
	const double InFinalYawPrecisionDegrees,
	const double InFinalPitchPrecisionDegrees,
	const double InFinalPowerPrecision)
{
	FABTSM11BridgeClosurePolicy Policy;
	Policy.PolicyVersion = 1;
	Policy.RegionConstructionVersion = 1;
	Policy.RecursiveSubdivisionVersion = 1;
	Policy.VisitOrderVersion = 1;
	Policy.EvidenceHashSchemaVersion = 1;
	Policy.RegionHaloFinalCells = 1;
	Policy.MaximumRecursionDepth = 3;
	Policy.MaximumSampleCountPerBridge = 32768;
	Policy.FinalYawPrecisionDegrees = InFinalYawPrecisionDegrees;
	Policy.FinalPitchPrecisionDegrees = InFinalPitchPrecisionDegrees;
	Policy.FinalPowerPrecision = InFinalPowerPrecision;
	return Policy;
}

bool FABTSM11PendingBridgeEdge::IsValid(
	const FABTSM11ConnectivityGridShape& Shape,
	FString* OutFailure) const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	if (!Shape.IsValid()
		|| SampleA < 0
		|| SampleB <= SampleA
		|| SampleB >= Shape.GetSampleCount()
		|| FaceComponentA < 0
		|| FaceComponentB < 0
		|| FaceComponentA == FaceComponentB
		|| (ChangedAxisMask != 0x3u
			&& ChangedAxisMask != 0x5u
			&& ChangedAxisMask != 0x6u))
	{
		return Reject(OutFailure, TEXT("InvalidPendingBridgeEdge"));
	}
	if (EdgeHash != ComputeEdgeHash(SampleA, SampleB, ChangedAxisMask))
	{
		return Reject(OutFailure, TEXT("PendingBridgeEdgeHashMismatch"));
	}
	return true;
}

bool FABTSM11ConnectivityDiscoveryPlan::IsValid(FString* OutFailure) const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	if (PlanVersion != 1
		|| PrefixLevel < 1
		|| PrefixLevel > 4
		|| !Shape.IsValid()
		|| ActiveSampleCount <= 0
		|| FaceComponentCount <= 0
		|| DiscoveryComponentCount <= 0
		|| DiscoveryComponentCount > FaceComponentCount
		|| RequiredBridgeEdges.Num()
			!= FaceComponentCount - DiscoveryComponentCount
		|| ActiveMaskHash == 0)
	{
		return Reject(OutFailure, TEXT("InvalidConnectivityDiscoveryPlan"));
	}
	for (int32 Index = 0; Index < RequiredBridgeEdges.Num(); ++Index)
	{
		if (!RequiredBridgeEdges[Index].IsValid(Shape, OutFailure)
			|| (Index > 0
				&& (RequiredBridgeEdges[Index - 1].SampleA
						> RequiredBridgeEdges[Index].SampleA
					|| (RequiredBridgeEdges[Index - 1].SampleA
							== RequiredBridgeEdges[Index].SampleA
						&& RequiredBridgeEdges[Index - 1].SampleB
							>= RequiredBridgeEdges[Index].SampleB))))
		{
			return Reject(OutFailure, TEXT("NonCanonicalBridgeEdgeOrder"));
		}
	}
	if (PlanHash != FABTSM11ConnectivityClosure::ComputePlanHash(*this))
	{
		return Reject(OutFailure, TEXT("ConnectivityPlanHashMismatch"));
	}
	return true;
}

uint64 FABTSM11BridgeClosureEvidence::ComputeEvidenceHash() const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11b3e001u));
	Hash.AddPod(EvidenceVersion);
	Hash.AddPod(EdgeHash);
	Hash.AddPod(PolicyHash);
	Hash.AddPod(RecursionDepth);
	Hash.AddPod(SampleCount);
	Hash.AddPod(PathSampleCount);
	Hash.AddPod(ReachedYawPrecisionDegrees);
	Hash.AddPod(ReachedPitchPrecisionDegrees);
	Hash.AddPod(ReachedPowerPrecision);
	Hash.AddPod(VisitOrderHash);
	Hash.AddPod(ContinuousPathHash);
	Hash.AddBool(bReachedFinalPrecision);
	Hash.AddBool(bProvenContinuousF4Path);
	return Hash.Value;
}

bool FABTSM11BridgeClosureEvidence::IsValidFor(
	const FABTSM11PendingBridgeEdge& Edge,
	const FABTSM11BridgeClosurePolicy& Policy,
	FString* OutFailure) const
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	if (!Policy.IsValid(OutFailure)
		|| EvidenceVersion != 1
		|| EdgeHash != Edge.EdgeHash
		|| PolicyHash != Policy.ComputePolicyHash()
		|| RecursionDepth < 1
		|| RecursionDepth > Policy.MaximumRecursionDepth
		|| SampleCount < 1
		|| SampleCount > Policy.MaximumSampleCountPerBridge
		|| PathSampleCount < 2
		|| PathSampleCount > SampleCount
		|| !FMath::IsNearlyEqual(
			ReachedYawPrecisionDegrees,
			Policy.FinalYawPrecisionDegrees,
			1.0e-12)
		|| !FMath::IsNearlyEqual(
			ReachedPitchPrecisionDegrees,
			Policy.FinalPitchPrecisionDegrees,
			1.0e-12)
		|| !FMath::IsNearlyEqual(
			ReachedPowerPrecision,
			Policy.FinalPowerPrecision,
			1.0e-12)
		|| VisitOrderHash == 0
		|| ContinuousPathHash == 0
		|| !bReachedFinalPrecision
		|| !bProvenContinuousF4Path)
	{
		return Reject(OutFailure, TEXT("IncompleteBridgeClosureEvidence"));
	}
	if (EvidenceHash != ComputeEvidenceHash())
	{
		return Reject(OutFailure, TEXT("BridgeClosureEvidenceHashMismatch"));
	}
	return true;
}

bool FABTSM11ConnectivityClosure::BuildDiscoveryPlan18(
	const TConstArrayView<uint8> ActiveMask,
	const FABTSM11ConnectivityGridShape& Shape,
	const int32 PrefixLevel,
	FABTSM11ConnectivityDiscoveryPlan& OutPlan,
	FString* OutFailure)
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	OutPlan = FABTSM11ConnectivityDiscoveryPlan();
	OutPlan.PrefixLevel = PrefixLevel;
	OutPlan.Shape = Shape;
	if (!Shape.IsValid()
		|| ActiveMask.Num() != Shape.GetSampleCount()
		|| PrefixLevel < 1
		|| PrefixLevel > 4)
	{
		return Reject(OutFailure, TEXT("InvalidConnectivityDiscoveryInput"));
	}

	FHash MaskHash;
	MaskHash.AddPod(static_cast<uint32>(0x11b31802u));
	for (const uint8 Value : ActiveMask)
	{
		const uint8 Canonical = Value != 0 ? 1u : 0u;
		MaskHash.AddPod(Canonical);
		OutPlan.ActiveSampleCount += Canonical;
	}
	OutPlan.ActiveMaskHash = MaskHash.Value;
	if (OutPlan.ActiveSampleCount == 0)
	{
		return Reject(OutFailure, TEXT("EmptyConnectivityDiscoverySet"));
	}

	TArray<int32> Labels;
	Labels.Init(-1, ActiveMask.Num());
	TArray<int32> Queue;
	Queue.Reserve(ActiveMask.Num());
	constexpr int32 FaceDeltas[6][3] = {
		{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
		{0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
	for (int32 Seed = 0; Seed < ActiveMask.Num(); ++Seed)
	{
		if (ActiveMask[Seed] == 0 || Labels[Seed] >= 0)
		{
			continue;
		}
		const int32 Label = OutPlan.FaceComponentCount++;
		Labels[Seed] = Label;
		Queue.Reset();
		Queue.Add(Seed);
		for (int32 Read = 0; Read < Queue.Num(); ++Read)
		{
			int32 Yaw = 0;
			int32 Pitch = 0;
			int32 Power = 0;
			Unflatten(Queue[Read], Shape, Yaw, Pitch, Power);
			for (const int32* Delta : FaceDeltas)
			{
				const int32 NYaw = Yaw + Delta[0];
				const int32 NPitch = Pitch + Delta[1];
				const int32 NPower = Power + Delta[2];
				if (NYaw < 0 || NYaw >= Shape.YawCount
					|| NPitch < 0 || NPitch >= Shape.PitchCount
					|| NPower < 0 || NPower >= Shape.PowerCount)
				{
					continue;
				}
				const int32 Neighbor = Flatten(
					NYaw, NPitch, NPower, Shape);
				if (ActiveMask[Neighbor] != 0 && Labels[Neighbor] < 0)
				{
					Labels[Neighbor] = Label;
					Queue.Add(Neighbor);
				}
			}
		}
	}

	TArray<FABTSM11PendingBridgeEdge> CandidateEdges;
	for (int32 SampleA = 0; SampleA < ActiveMask.Num(); ++SampleA)
	{
		if (ActiveMask[SampleA] == 0)
		{
			continue;
		}
		int32 Yaw = 0;
		int32 Pitch = 0;
		int32 Power = 0;
		Unflatten(SampleA, Shape, Yaw, Pitch, Power);
		for (int32 DeltaYaw = -1; DeltaYaw <= 1; ++DeltaYaw)
		{
			for (int32 DeltaPitch = -1; DeltaPitch <= 1; ++DeltaPitch)
			{
				for (int32 DeltaPower = -1; DeltaPower <= 1; ++DeltaPower)
				{
					const int32 ChangedAxes =
						(DeltaYaw != 0 ? 1 : 0)
						+ (DeltaPitch != 0 ? 1 : 0)
						+ (DeltaPower != 0 ? 1 : 0);
					if (ChangedAxes != 2)
					{
						continue;
					}
					const int32 NYaw = Yaw + DeltaYaw;
					const int32 NPitch = Pitch + DeltaPitch;
					const int32 NPower = Power + DeltaPower;
					if (NYaw < 0 || NYaw >= Shape.YawCount
						|| NPitch < 0 || NPitch >= Shape.PitchCount
						|| NPower < 0 || NPower >= Shape.PowerCount)
					{
						continue;
					}
					const int32 SampleB = Flatten(
						NYaw, NPitch, NPower, Shape);
					if (SampleB <= SampleA
						|| ActiveMask[SampleB] == 0
						|| Labels[SampleA] == Labels[SampleB])
					{
						continue;
					}
					FABTSM11PendingBridgeEdge Edge;
					Edge.SampleA = SampleA;
					Edge.SampleB = SampleB;
					Edge.FaceComponentA = Labels[SampleA];
					Edge.FaceComponentB = Labels[SampleB];
					Edge.ChangedAxisMask =
						(DeltaYaw != 0 ? 0x1u : 0u)
						| (DeltaPitch != 0 ? 0x2u : 0u)
						| (DeltaPower != 0 ? 0x4u : 0u);
					Edge.EdgeHash = ComputeEdgeHash(
						Edge.SampleA,
						Edge.SampleB,
						Edge.ChangedAxisMask);
					CandidateEdges.Add(Edge);
				}
			}
		}
	}
	CandidateEdges.Sort([](
		const FABTSM11PendingBridgeEdge& A,
		const FABTSM11PendingBridgeEdge& B)
	{
		return A.SampleA != B.SampleA
			? A.SampleA < B.SampleA
			: A.SampleB < B.SampleB;
	});

	FDisjointSet Components(OutPlan.FaceComponentCount);
	for (const FABTSM11PendingBridgeEdge& Edge : CandidateEdges)
	{
		if (Components.Union(Edge.FaceComponentA, Edge.FaceComponentB))
		{
			OutPlan.RequiredBridgeEdges.Add(Edge);
		}
	}
	OutPlan.DiscoveryComponentCount =
		CountRoots(Components, OutPlan.FaceComponentCount);
	OutPlan.PlanHash = ComputePlanHash(OutPlan);
	if (!OutPlan.IsValid(OutFailure))
	{
		return false;
	}
	return true;
}

uint64 FABTSM11ConnectivityClosure::ComputePlanHash(
	const FABTSM11ConnectivityDiscoveryPlan& Plan)
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11b31803u));
	Hash.AddPod(Plan.PlanVersion);
	Hash.AddPod(Plan.PrefixLevel);
	Hash.AddPod(Plan.Shape.YawCount);
	Hash.AddPod(Plan.Shape.PitchCount);
	Hash.AddPod(Plan.Shape.PowerCount);
	Hash.AddPod(Plan.ActiveSampleCount);
	Hash.AddPod(Plan.FaceComponentCount);
	Hash.AddPod(Plan.DiscoveryComponentCount);
	Hash.AddPod(Plan.ActiveMaskHash);
	Hash.AddPod(Plan.RequiredBridgeEdges.Num());
	for (const FABTSM11PendingBridgeEdge& Edge : Plan.RequiredBridgeEdges)
	{
		Hash.AddPod(Edge.SampleA);
		Hash.AddPod(Edge.SampleB);
		Hash.AddPod(Edge.FaceComponentA);
		Hash.AddPod(Edge.FaceComponentB);
		Hash.AddPod(Edge.ChangedAxisMask);
		Hash.AddPod(Edge.EdgeHash);
	}
	return Hash.Value;
}

bool FABTSM11ConnectivityClosure::CloseWithEvidence(
	const FABTSM11ConnectivityDiscoveryPlan& Plan,
	const FABTSM11BridgeClosurePolicy& Policy,
	const TConstArrayView<FABTSM11BridgeClosureEvidence> Evidence,
	FABTSM11ConnectivityClosureResult& OutResult,
	FString* OutFailure)
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	OutResult = FABTSM11ConnectivityClosureResult();
	OutResult.PlanHash = Plan.PlanHash;
	OutResult.PolicyHash = Policy.ComputePolicyHash();
	OutResult.RequiredBridgeCount = Plan.RequiredBridgeEdges.Num();
	if (!Plan.IsValid(&OutResult.Failure)
		|| !Policy.IsValid(&OutResult.Failure))
	{
		OutResult.ResultHash = ComputeResultHash(OutResult);
		if (OutFailure != nullptr)
		{
			*OutFailure = OutResult.Failure;
		}
		return false;
	}

	TMap<uint64, const FABTSM11BridgeClosureEvidence*> ByEdge;
	for (const FABTSM11BridgeClosureEvidence& Item : Evidence)
	{
		if (ByEdge.Contains(Item.EdgeHash))
		{
			OutResult.Failure = TEXT("DuplicateBridgeClosureEvidence");
			break;
		}
		ByEdge.Add(Item.EdgeHash, &Item);
	}
	FDisjointSet Components(Plan.FaceComponentCount);
	FHash Aggregate;
	Aggregate.AddPod(static_cast<uint32>(0x11b3e002u));
	if (OutResult.Failure.IsEmpty())
	{
		for (const TPair<uint64,
			const FABTSM11BridgeClosureEvidence*>& Pair : ByEdge)
		{
			const bool bRequested = Plan.RequiredBridgeEdges.ContainsByPredicate(
				[&Pair](const FABTSM11PendingBridgeEdge& Edge)
				{
					return Edge.EdgeHash == Pair.Key;
				});
			if (!bRequested)
			{
				OutResult.Failure = TEXT("UnexpectedBridgeClosureEvidence");
				break;
			}
		}
	}
	if (OutResult.Failure.IsEmpty())
	{
		for (const FABTSM11PendingBridgeEdge& Edge : Plan.RequiredBridgeEdges)
		{
			const FABTSM11BridgeClosureEvidence* const* Found =
				ByEdge.Find(Edge.EdgeHash);
			if (Found == nullptr)
			{
				OutResult.Failure = TEXT("MissingBridgeClosureEvidence");
				break;
			}
			FString EvidenceFailure;
			if (!(*Found)->IsValidFor(Edge, Policy, &EvidenceFailure))
			{
				OutResult.Failure = EvidenceFailure;
				break;
			}
			Components.Union(Edge.FaceComponentA, Edge.FaceComponentB);
			Aggregate.AddPod((*Found)->EvidenceHash);
			++OutResult.ProvenBridgeCount;
		}
	}
	OutResult.FinalComponentCount =
		CountRoots(Components, Plan.FaceComponentCount);
	OutResult.BridgeEvidenceAggregateHash = Aggregate.Value;
	OutResult.bEvidenceComplete = OutResult.Failure.IsEmpty()
		&& OutResult.ProvenBridgeCount == OutResult.RequiredBridgeCount;
	if (OutResult.bEvidenceComplete && Plan.DiscoveryComponentCount != 1)
	{
		OutResult.Failure = TEXT("DiscoveryGraphNotSingleComponent");
	}
	else if (OutResult.bEvidenceComplete
		&& OutResult.FinalComponentCount != 1)
	{
		OutResult.Failure = TEXT("BridgeClosureNotSingleComponent");
	}
	OutResult.bPassed = OutResult.Failure.IsEmpty()
		&& OutResult.bEvidenceComplete
		&& OutResult.FinalComponentCount == 1;
	OutResult.ResultHash = ComputeResultHash(OutResult);
	if (!OutResult.bPassed && OutFailure != nullptr)
	{
		*OutFailure = OutResult.Failure;
	}
	return OutResult.bPassed;
}

uint64 FABTSM11ConnectivityClosure::ComputeResultHash(
	const FABTSM11ConnectivityClosureResult& Result)
{
	using namespace ABTSM11ConnectivityClosurePrivate;
	FHash Hash;
	Hash.AddPod(static_cast<uint32>(0x11b3c002u));
	Hash.AddPod(Result.ResultVersion);
	Hash.AddPod(Result.PlanHash);
	Hash.AddPod(Result.PolicyHash);
	Hash.AddPod(Result.RequiredBridgeCount);
	Hash.AddPod(Result.ProvenBridgeCount);
	Hash.AddPod(Result.FinalComponentCount);
	Hash.AddPod(Result.BridgeEvidenceAggregateHash);
	Hash.AddBool(Result.bEvidenceComplete);
	Hash.AddBool(Result.bPassed);
	Hash.AddString(Result.Failure);
	return Hash.Value;
}
