// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG4TrialPlanner.h"

namespace
{
	constexpr double SegmentEntryTolerance = 1.0e-6;

	struct FSettledOrientedBox
	{
		FVector Center = FVector::ZeroVector;
		FVector Axes[3] =
		{
			FVector::ForwardVector,
			FVector::RightVector,
			FVector::UpVector
		};
		FVector HalfExtent = FVector::ZeroVector;
	};

	struct FOrdinaryCandidate
	{
		int32 NodeId = INDEX_NONE;
		bool bRequiredPivot = false;
		TArray<int32> PredictedAffectedMainBodyNodeIds;
		double PredictedAffectedMainBodyMassRatio = 0.0;
		double IncomingFaceDepth = 0.0;
		double WeakHeightDifference = 0.0;
	};

	uint64 MakeEdgeKey(const int32 LowerNodeId, const int32 UpperNodeId)
	{
		return
			(static_cast<uint64>(static_cast<uint32>(LowerNodeId)) << 32)
			| static_cast<uint32>(UpperNodeId);
	}

	bool IsFinitePlannerVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFinitePlannerQuat(const FQuat& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z)
			&& FMath::IsFinite(Value.W);
	}

	bool AddUniqueIds(
		const TConstArrayView<int32> NodeIds,
		const TMap<int32, const FABTSM73DAG4SettledNode*>& NodesById,
		TSet<int32>& OutNodeIds)
	{
		OutNodeIds.Reset();
		for (const int32 NodeId : NodeIds)
		{
			if (!NodesById.Contains(NodeId)
				|| OutNodeIds.Contains(NodeId))
			{
				return false;
			}
			OutNodeIds.Add(NodeId);
		}
		return true;
	}

	FSettledOrientedBox MakeOrientedBox(
		const FABTSM73DAG4SettledNode& Node)
	{
		FSettledOrientedBox Box;
		Box.Center = Node.LocalTransform.GetLocation();
		const FQuat Rotation = Node.LocalTransform.GetRotation();
		Box.Axes[0] = Rotation.GetAxisX();
		Box.Axes[1] = Rotation.GetAxisY();
		Box.Axes[2] = Rotation.GetAxisZ();
		Box.HalfExtent = Node.DimensionsCM * 0.5f;
		return Box;
	}

	bool SegmentIntersectsInflatedBox(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FSettledOrientedBox& Box,
		const double InflationCM,
		double& OutEntry)
	{
		const FVector Delta = SegmentEnd - SegmentStart;
		const FVector RelativeStart = SegmentStart - Box.Center;
		double Entry = 0.0;
		double Exit = 1.0;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const double Origin = FVector::DotProduct(
				RelativeStart,
				Box.Axes[AxisIndex]);
			const double Direction = FVector::DotProduct(
				Delta,
				Box.Axes[AxisIndex]);
			const double Extent =
				Box.HalfExtent[AxisIndex] + InflationCM;
			if (FMath::Abs(Direction) <= SMALL_NUMBER)
			{
				if (Origin < -Extent || Origin > Extent)
				{
					return false;
				}
				continue;
			}

			double Near = (-Extent - Origin) / Direction;
			double Far = (Extent - Origin) / Direction;
			if (Near > Far)
			{
				Swap(Near, Far);
			}
			Entry = FMath::Max(Entry, Near);
			Exit = FMath::Min(Exit, Far);
			if (Entry > Exit)
			{
				return false;
			}
		}
		if (Exit < 0.0 || Entry > 1.0)
		{
			return false;
		}
		OutEntry = FMath::Clamp(Entry, 0.0, 1.0);
		return true;
	}

	double RayEntryDistanceFromBoxCenter(
		const FSettledOrientedBox& Box,
		const FVector& Direction,
		const double InflationCM)
	{
		double EntryDistance = TNumericLimits<double>::Max();
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const double DirectionComponent = FMath::Abs(
				FVector::DotProduct(Direction, Box.Axes[AxisIndex]));
			if (DirectionComponent <= SMALL_NUMBER)
			{
				continue;
			}
			EntryDistance = FMath::Min(
				EntryDistance,
				(Box.HalfExtent[AxisIndex] + InflationCM)
					/ DirectionComponent);
		}
		return EntryDistance;
	}

	bool IsReachableFromAttackDirection(
		const int32 TargetNodeId,
		const FVector& AttackDirection,
		const double ProjectileRadiusCM,
		const double AttackApproachDistanceCM,
		const TArray<int32>& SortedNodeIds,
		const TMap<int32, FSettledOrientedBox>& BoxesById,
		double& OutIncomingFaceDepth,
		int32& OutBlockingNodeId)
	{
		OutIncomingFaceDepth = 0.0;
		OutBlockingNodeId = INDEX_NONE;
		const FSettledOrientedBox* TargetBox =
			BoxesById.Find(TargetNodeId);
		if (TargetBox == nullptr)
		{
			return false;
		}

		// The path is for the projectile centre. Inflating the target gives
		// the first legal centre position at contact, while inflating every
		// blocker turns the swept sphere into a finite segment/OBB query.
		const double TargetEntryDistance = RayEntryDistanceFromBoxCenter(
			*TargetBox,
			AttackDirection,
			ProjectileRadiusCM);
		if (!FMath::IsFinite(TargetEntryDistance)
			|| TargetEntryDistance <= 0.0)
		{
			return false;
		}
		const FVector SegmentEnd =
			TargetBox->Center - AttackDirection * TargetEntryDistance;
		const FVector SegmentStart =
			SegmentEnd - AttackDirection * AttackApproachDistanceCM;
		if (!IsFinitePlannerVector(SegmentStart)
			|| !IsFinitePlannerVector(SegmentEnd))
		{
			return false;
		}

		double EarliestEntry = TNumericLimits<double>::Max();
		for (const int32 BlockerNodeId : SortedNodeIds)
		{
			if (BlockerNodeId == TargetNodeId)
			{
				continue;
			}
			const FSettledOrientedBox* BlockerBox =
				BoxesById.Find(BlockerNodeId);
			if (BlockerBox == nullptr)
			{
				return false;
			}
			double Entry = 0.0;
			if (SegmentIntersectsInflatedBox(
				SegmentStart,
				SegmentEnd,
				*BlockerBox,
				ProjectileRadiusCM,
				Entry)
				&& Entry < 1.0 - SegmentEntryTolerance
				&& (Entry < EarliestEntry
					|| (FMath::IsNearlyEqual(Entry, EarliestEntry)
						&& BlockerNodeId < OutBlockingNodeId)))
			{
				EarliestEntry = Entry;
				OutBlockingNodeId = BlockerNodeId;
			}
		}
		OutIncomingFaceDepth =
			FVector::DotProduct(SegmentEnd, AttackDirection);
		return OutBlockingNodeId == INDEX_NONE;
	}

	void GatherGroundReachable(
		const TMap<int32, TArray<int32>>& ChildrenByNodeId,
		const TArray<int32>& SortedGroundNodeIds,
		const TSet<int32>& RemovedNodeIds,
		TSet<int32>& OutReachable)
	{
		OutReachable.Reset();
		TArray<int32> Queue;
		for (const int32 GroundNodeId : SortedGroundNodeIds)
		{
			if (!RemovedNodeIds.Contains(GroundNodeId))
			{
				OutReachable.Add(GroundNodeId);
				Queue.Add(GroundNodeId);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const TArray<int32>* Children =
				ChildrenByNodeId.Find(Queue[Head]);
			if (Children == nullptr)
			{
				continue;
			}
			for (const int32 ChildNodeId : *Children)
			{
				if (RemovedNodeIds.Contains(ChildNodeId)
					|| OutReachable.Contains(ChildNodeId))
				{
					continue;
				}
				OutReachable.Add(ChildNodeId);
				Queue.Add(ChildNodeId);
			}
		}
	}

	void ComputePredictedAffectedMainBody(
		const int32 RemovedNodeId,
		const TArray<int32>& SortedNodeIds,
		const TMap<int32, const FABTSM73DAG4SettledNode*>& NodesById,
		const TMap<int32, TArray<int32>>& ChildrenByNodeId,
		const TArray<int32>& SortedGroundNodeIds,
		const double TotalMainBodyMass,
		TArray<int32>& OutAffectedNodeIds,
		double& OutAffectedMassRatio)
	{
		TSet<int32> RemovedNodeIds;
		RemovedNodeIds.Add(RemovedNodeId);
		TSet<int32> Reachable;
		GatherGroundReachable(
			ChildrenByNodeId,
			SortedGroundNodeIds,
			RemovedNodeIds,
			Reachable);

		OutAffectedNodeIds.Reset();
		double AffectedMass = 0.0;
		for (const int32 NodeId : SortedNodeIds)
		{
			const FABTSM73DAG4SettledNode* const* Found =
				NodesById.Find(NodeId);
			if (Found == nullptr
				|| *Found == nullptr
				|| !(*Found)->bMainBody
				|| NodeId == RemovedNodeId
				|| Reachable.Contains(NodeId))
			{
				continue;
			}
			OutAffectedNodeIds.Add(NodeId);
			AffectedMass += (*Found)->Mass;
		}
		OutAffectedMassRatio = TotalMainBodyMass > 0.0
			? AffectedMass / TotalMainBodyMass
			: 0.0;
	}
}

bool FABTSM73DAG4TrialPlanner::BuildPlans(
	const FABTSM73DAG4ValidationSettings& Settings,
	const FABTSM73DAG4TrialPlanningInput& Input,
	TArray<FABTSM73DAG4TrialPlan>& OutPlans,
	FString& OutError) const
{
	OutPlans.Reset();
	OutError.Reset();
	if (!Settings.bEnableSettledChaosValidation)
	{
		return true;
	}

	auto Reject = [&OutPlans, &OutError](const FString& Reason)
	{
		OutPlans.Reset();
		OutError = Reason;
		return false;
	};

	if (Settings.NonWeakProbeCount < 3
		|| Settings.MaxTrialCount < 1
		|| Settings.MaxSettledBodyCount < 1
		|| !FMath::IsFinite(
			Settings.MaxOrdinaryPredictedAffectedMassRatio)
		|| Settings.MaxOrdinaryPredictedAffectedMassRatio < 0.0f
		|| Settings.MaxOrdinaryPredictedAffectedMassRatio > 1.0f)
	{
		return Reject(TEXT("DAG4PlannerSettingsInvalid"));
	}
	const int64 MinimumRequiredTrialCount =
		1ll + Settings.NonWeakProbeCount;
	if (MinimumRequiredTrialCount > Settings.MaxTrialCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerTrialBudgetExceeded:%lld:%d"),
			MinimumRequiredTrialCount,
			Settings.MaxTrialCount));
	}
	if (Input.Nodes.IsEmpty()
		|| Input.Nodes.Num() > Settings.MaxSettledBodyCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerBodyBudgetInvalid:%d:%d"),
			Input.Nodes.Num(),
			Settings.MaxSettledBodyCount));
	}
	if (!FMath::IsFinite(Input.ProjectileRadiusCM)
		|| Input.ProjectileRadiusCM <= 0.0f
		|| !FMath::IsFinite(Input.AttackApproachDistanceCM)
		|| Input.AttackApproachDistanceCM <= 0.0f
		|| !IsFinitePlannerVector(Input.AttackDirectionLocal))
	{
		return Reject(TEXT("DAG4PlannerAttackInputInvalid"));
	}
	const FVector AttackDirection =
		Input.AttackDirectionLocal.GetSafeNormal();
	if (AttackDirection.IsNearlyZero()
		|| !IsFinitePlannerVector(AttackDirection))
	{
		return Reject(TEXT("DAG4PlannerAttackDirectionInvalid"));
	}

	TMap<int32, const FABTSM73DAG4SettledNode*> NodesById;
	TMap<int32, FSettledOrientedBox> BoxesById;
	TArray<int32> SortedNodeIds;
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		const FQuat Rotation = Node.LocalTransform.GetRotation();
		const FVector Scale = Node.LocalTransform.GetScale3D();
		if (Node.NodeId == INDEX_NONE
			|| NodesById.Contains(Node.NodeId)
			|| static_cast<uint8>(Node.Material)
				> static_cast<uint8>(
					EABTSM7BuildingMaterial::Glass)
			|| !IsFinitePlannerVector(Node.DimensionsCM)
			|| Node.DimensionsCM.GetMin() <= 0.0f
			|| Node.LocalTransform.ContainsNaN()
			|| !IsFinitePlannerVector(
				Node.LocalTransform.GetLocation())
			|| !IsFinitePlannerQuat(Rotation)
			|| !FMath::IsNearlyEqual(
				Rotation.SizeSquared(),
				1.0f,
				1.0e-3f)
			|| !IsFinitePlannerVector(Scale)
			|| !Scale.Equals(FVector::OneVector, 1.0e-3f)
			|| !FMath::IsFinite(Node.Mass)
			|| Node.Mass <= 0.0)
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerNodeInvalid:%d"),
				Node.NodeId));
		}
		NodesById.Add(Node.NodeId, &Node);
		BoxesById.Add(Node.NodeId, MakeOrientedBox(Node));
		SortedNodeIds.Add(Node.NodeId);
	}
	SortedNodeIds.Sort();

	TSet<int32> GroundNodeIds;
	if (Input.GroundNodeIds.IsEmpty()
		|| !AddUniqueIds(
			Input.GroundNodeIds,
			NodesById,
			GroundNodeIds))
	{
		return Reject(TEXT("DAG4PlannerGroundIdentityInvalid"));
	}
	TArray<int32> SortedGroundNodeIds = Input.GroundNodeIds;
	SortedGroundNodeIds.Sort();

	if (Input.WeakNodeIds.Num() != 1)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerWeakPointCountInvalid:%d"),
			Input.WeakNodeIds.Num()));
	}
	const int32 WeakNodeId = Input.WeakNodeIds[0];
	if (!NodesById.Contains(WeakNodeId)
		|| GroundNodeIds.Contains(WeakNodeId))
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerWeakIdentityInvalid:%d"),
			WeakNodeId));
	}

	TSet<int32> PivotNodeIds;
	if (!AddUniqueIds(
		Input.RemainingSupportNodeIds,
		NodesById,
		PivotNodeIds))
	{
		return Reject(TEXT("DAG4PlannerPivotIdentityInvalid"));
	}
	for (const int32 PivotNodeId : Input.RemainingSupportNodeIds)
	{
		if (PivotNodeId == WeakNodeId
			|| GroundNodeIds.Contains(PivotNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerPivotIdentityInvalid:%d"),
				PivotNodeId));
		}
	}
	if (PivotNodeIds.Num() > Settings.NonWeakProbeCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerPivotProbeBudgetExceeded:%d:%d"),
			PivotNodeIds.Num(),
			Settings.NonWeakProbeCount));
	}

	TSet<int32> ExpectedAffectedNodeIds;
	if (Input.ExpectedAffectedMainBodyNodeIds.IsEmpty()
		|| !AddUniqueIds(
			Input.ExpectedAffectedMainBodyNodeIds,
			NodesById,
			ExpectedAffectedNodeIds))
	{
		return Reject(
			TEXT("DAG4PlannerExpectedAffectedIdentityInvalid"));
	}
	for (const int32 AffectedNodeId
		: Input.ExpectedAffectedMainBodyNodeIds)
	{
		const FABTSM73DAG4SettledNode* const* Found =
			NodesById.Find(AffectedNodeId);
		if (Found == nullptr
			|| *Found == nullptr
			|| !(*Found)->bMainBody
			|| AffectedNodeId == WeakNodeId
			|| PivotNodeIds.Contains(AffectedNodeId)
			|| GroundNodeIds.Contains(AffectedNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerExpectedAffectedIdentityInvalid:%d"),
				AffectedNodeId));
		}
	}

	TMap<int32, TArray<int32>> ChildrenByNodeId;
	for (const int32 NodeId : SortedNodeIds)
	{
		ChildrenByNodeId.Add(NodeId);
	}
	TSet<uint64> ContactKeys;
	for (const FABTSM73DAG4SettledContact& Contact : Input.Contacts)
	{
		if (Contact.LowerNodeId == Contact.UpperNodeId
			|| !NodesById.Contains(Contact.LowerNodeId)
			|| !NodesById.Contains(Contact.UpperNodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerContactIdentityInvalid:%d:%d"),
				Contact.LowerNodeId,
				Contact.UpperNodeId));
		}
		const uint64 Key = MakeEdgeKey(
			Contact.LowerNodeId,
			Contact.UpperNodeId);
		if (ContactKeys.Contains(Key))
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerContactDuplicate:%d:%d"),
				Contact.LowerNodeId,
				Contact.UpperNodeId));
		}
		ContactKeys.Add(Key);
		ChildrenByNodeId.FindChecked(
			Contact.LowerNodeId).Add(Contact.UpperNodeId);
	}
	for (const int32 NodeId : SortedNodeIds)
	{
		ChildrenByNodeId.FindChecked(NodeId).Sort();
	}

	double TotalMainBodyMass = 0.0;
	for (const int32 NodeId : SortedNodeIds)
	{
		const FABTSM73DAG4SettledNode* const* Found =
			NodesById.Find(NodeId);
		if (Found != nullptr && *Found != nullptr
			&& (*Found)->bMainBody)
		{
			TotalMainBodyMass += (*Found)->Mass;
		}
	}
	if (!FMath::IsFinite(TotalMainBodyMass)
		|| TotalMainBodyMass <= 0.0)
	{
		return Reject(TEXT("DAG4PlannerMainBodyMassInvalid"));
	}

	TSet<int32> NoRemovedNodeIds;
	TSet<int32> IntactReachableNodeIds;
	GatherGroundReachable(
		ChildrenByNodeId,
		SortedGroundNodeIds,
		NoRemovedNodeIds,
		IntactReachableNodeIds);
	for (const int32 NodeId : SortedNodeIds)
	{
		const FABTSM73DAG4SettledNode* const* Found =
			NodesById.Find(NodeId);
		if (Found != nullptr
			&& *Found != nullptr
			&& (*Found)->bMainBody
			&& !IntactReachableNodeIds.Contains(NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerIntactMainBodyDisconnected:%d"),
				NodeId));
		}
	}

	TArray<int32> SortedExpectedAffectedNodeIds =
		Input.ExpectedAffectedMainBodyNodeIds;
	SortedExpectedAffectedNodeIds.Sort();
	double WeakPredictedAffectedMass = 0.0;
	for (const int32 NodeId : SortedExpectedAffectedNodeIds)
	{
		WeakPredictedAffectedMass +=
			NodesById.FindChecked(NodeId)->Mass;
	}
	const double WeakPredictedAffectedMassRatio =
		WeakPredictedAffectedMass / TotalMainBodyMass;
	if (!FMath::IsFinite(WeakPredictedAffectedMassRatio)
		|| WeakPredictedAffectedMassRatio < 0.0
		|| WeakPredictedAffectedMassRatio > 1.0
			+ KINDA_SMALL_NUMBER)
	{
		return Reject(TEXT("DAG4PlannerWeakPredictionInvalid"));
	}

	TArray<FABTSM73DAG4TrialPlan> WorkingPlans;
	WorkingPlans.Reserve(Settings.MaxTrialCount);
	FABTSM73DAG4TrialPlan WeakPlan;
	WeakPlan.Kind = EABTSM73DAG4TrialKind::WeakPoint;
	WeakPlan.ProbeIndex = 0;
	WeakPlan.RemovedNodeIds.Add(WeakNodeId);
	WeakPlan.PredictedAffectedMainBodyNodeIds =
		SortedExpectedAffectedNodeIds;
	WeakPlan.PredictedAffectedMainBodyMassRatio =
		static_cast<float>(WeakPredictedAffectedMassRatio);
	WorkingPlans.Add(MoveTemp(WeakPlan));

	const double WeakHeight =
		NodesById.FindChecked(WeakNodeId)
			->LocalTransform.GetLocation().Z;
	TArray<FOrdinaryCandidate> OrdinaryCandidates;
	for (const int32 CandidateNodeId : SortedNodeIds)
	{
		if (CandidateNodeId == WeakNodeId
			|| GroundNodeIds.Contains(CandidateNodeId))
		{
			continue;
		}

		FOrdinaryCandidate Candidate;
		Candidate.NodeId = CandidateNodeId;
		Candidate.bRequiredPivot =
			PivotNodeIds.Contains(CandidateNodeId);
		ComputePredictedAffectedMainBody(
			CandidateNodeId,
			SortedNodeIds,
			NodesById,
			ChildrenByNodeId,
			SortedGroundNodeIds,
			TotalMainBodyMass,
			Candidate.PredictedAffectedMainBodyNodeIds,
			Candidate.PredictedAffectedMainBodyMassRatio);
		if (!FMath::IsFinite(
			Candidate.PredictedAffectedMainBodyMassRatio)
			|| Candidate.PredictedAffectedMainBodyMassRatio < 0.0
			|| Candidate.PredictedAffectedMainBodyMassRatio > 1.0
				+ KINDA_SMALL_NUMBER)
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerOrdinaryPredictionInvalid:%d"),
				CandidateNodeId));
		}
		if (Candidate.PredictedAffectedMainBodyMassRatio
			> Settings.MaxOrdinaryPredictedAffectedMassRatio
				+ KINDA_SMALL_NUMBER)
		{
			if (Candidate.bRequiredPivot)
			{
				return Reject(FString::Printf(
					TEXT("DAG4PlannerPivotPredictedEffectExceeded:%d:%.6f:%.6f"),
					CandidateNodeId,
					Candidate.PredictedAffectedMainBodyMassRatio,
					Settings.MaxOrdinaryPredictedAffectedMassRatio));
			}
			continue;
		}

		int32 BlockingNodeId = INDEX_NONE;
		if (!IsReachableFromAttackDirection(
			CandidateNodeId,
			AttackDirection,
			Input.ProjectileRadiusCM,
			Input.AttackApproachDistanceCM,
			SortedNodeIds,
			BoxesById,
			Candidate.IncomingFaceDepth,
			BlockingNodeId))
		{
			// P is a mandatory structural control, not a promise that the
			// player can shoot through W to reach it. Keeping it in the
			// comparison prevents the planner from manufacturing weak-point
			// advantage by silently discarding the strongest non-weak support.
			if (!Candidate.bRequiredPivot)
			{
				continue;
			}
		}
		Candidate.WeakHeightDifference = FMath::Abs(
			NodesById.FindChecked(CandidateNodeId)
				->LocalTransform.GetLocation().Z - WeakHeight);
		OrdinaryCandidates.Add(MoveTemp(Candidate));
	}

	OrdinaryCandidates.Sort([](
		const FOrdinaryCandidate& A,
		const FOrdinaryCandidate& B)
	{
		if (A.bRequiredPivot != B.bRequiredPivot)
		{
			return A.bRequiredPivot;
		}
		if (A.PredictedAffectedMainBodyMassRatio
			!= B.PredictedAffectedMainBodyMassRatio)
		{
			// Use the strongest admissible ordinary contrasts first; this
			// prevents candidate selection from manufacturing weak advantage.
			return A.PredictedAffectedMainBodyMassRatio
				> B.PredictedAffectedMainBodyMassRatio;
		}
		if (A.IncomingFaceDepth != B.IncomingFaceDepth)
		{
			return A.IncomingFaceDepth < B.IncomingFaceDepth;
		}
		if (A.WeakHeightDifference != B.WeakHeightDifference)
		{
			return A.WeakHeightDifference < B.WeakHeightDifference;
		}
		return A.NodeId < B.NodeId;
	});

	const int32 RequiredOrdinaryCount = Settings.NonWeakProbeCount;
	const int64 RequiredTotalTrialCount =
		1ll + RequiredOrdinaryCount;
	if (RequiredTotalTrialCount > Settings.MaxTrialCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerTrialBudgetExceeded:%lld:%d"),
			RequiredTotalTrialCount,
			Settings.MaxTrialCount));
	}
	if (OrdinaryCandidates.Num() < RequiredOrdinaryCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4PlannerOrdinaryCandidateShortage:%d:%d"),
			OrdinaryCandidates.Num(),
			RequiredOrdinaryCount));
	}

	for (int32 ProbeIndex = 0;
		ProbeIndex < RequiredOrdinaryCount;
		++ProbeIndex)
	{
		if (WorkingPlans.Num() >= Settings.MaxTrialCount)
		{
			return Reject(FString::Printf(
				TEXT("DAG4PlannerTrialBudgetExceeded:%d:%d"),
				WorkingPlans.Num() + 1,
				Settings.MaxTrialCount));
		}
		const FOrdinaryCandidate& Candidate =
			OrdinaryCandidates[ProbeIndex];
		FABTSM73DAG4TrialPlan Plan;
		Plan.Kind = EABTSM73DAG4TrialKind::Ordinary;
		Plan.ProbeIndex = ProbeIndex;
		Plan.RemovedNodeIds.Add(Candidate.NodeId);
		Plan.PredictedAffectedMainBodyNodeIds =
			Candidate.PredictedAffectedMainBodyNodeIds;
		Plan.PredictedAffectedMainBodyMassRatio =
			static_cast<float>(
				Candidate.PredictedAffectedMainBodyMassRatio);
		WorkingPlans.Add(MoveTemp(Plan));
	}

	OutPlans = MoveTemp(WorkingPlans);
	return true;
}
