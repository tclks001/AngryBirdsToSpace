// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG4ResponseEvaluator.h"

namespace
{
	constexpr float ScoreDisplacementSaturationMultiplier = 2.0f;
	constexpr float ScoreRotationSaturationMultiplier = 2.0f;
	constexpr float ScorePropagationDepthSaturation = 4.0f;
	constexpr float ScoreSecondaryContactSaturation = 4.0f;

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteNonNegative(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f;
	}

	bool IsUnitRatio(const float Value)
	{
		return FMath::IsFinite(Value)
			&& Value >= 0.0f
			&& Value <= 1.0f;
	}

	bool IsTrialKindValid(const EABTSM73DAG4TrialKind Kind)
	{
		return Kind == EABTSM73DAG4TrialKind::WeakPoint
			|| Kind == EABTSM73DAG4TrialKind::Ordinary;
	}

	bool IsFailureMotionValid(const EABTSM73DAGFailureMotion Motion)
	{
		return Motion == EABTSM73DAGFailureMotion::Drop
			|| Motion == EABTSM73DAGFailureMotion::Tip
			|| Motion == EABTSM73DAGFailureMotion::SlideThenTip;
	}

	void SortUniqueIds(TArray<int32>& NodeIds)
	{
		NodeIds.Sort();
		for (int32 Index = NodeIds.Num() - 1; Index > 0; --Index)
		{
			if (NodeIds[Index] == NodeIds[Index - 1])
			{
				NodeIds.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	bool ContainsDuplicateIds(TArray<int32> NodeIds)
	{
		const int32 OriginalCount = NodeIds.Num();
		SortUniqueIds(NodeIds);
		return NodeIds.Num() != OriginalCount;
	}

	uint64 MakeUndirectedPairKey(const int32 NodeA, const int32 NodeB)
	{
		const uint32 Lower = static_cast<uint32>(
			FMath::Min(NodeA, NodeB));
		const uint32 Upper = static_cast<uint32>(
			FMath::Max(NodeA, NodeB));
		return (static_cast<uint64>(Lower) << 32)
			| static_cast<uint64>(Upper);
	}

	bool ValidateResponseSettings(
		const FABTSM73DAG4ValidationSettings& Settings,
		FString& OutError)
	{
		const int64 RequiredTrialCount =
			static_cast<int64>(Settings.NonWeakProbeCount) + 1;
		const double MinimumRolloutSeconds =
			static_cast<double>(RequiredTrialCount)
			* static_cast<double>(Settings.TrialDurationSeconds);
		const bool bValid =
			Settings.NonWeakProbeCount >= 3
			&& Settings.MaxSettledBodyCount >= 1
			&& Settings.MaxContactPairQueryCount >= 1
			&& Settings.MaxTrialCount >= RequiredTrialCount
			&& Settings.MaxTrialTickCount >= 1
			&& Settings.MaxContactEventCount >= 1
			&& FMath::IsFinite(Settings.TrialDurationSeconds)
			&& Settings.TrialDurationSeconds > 0.0f
			&& FMath::IsFinite(Settings.TrialWarmupSeconds)
			&& Settings.TrialWarmupSeconds >= 0.0f
			&& Settings.TrialWarmupSeconds
				< Settings.TrialDurationSeconds
			&& FMath::IsFinite(Settings.MaxTotalValidationSeconds)
			&& Settings.MaxTotalValidationSeconds > 0.0f
			&& MinimumRolloutSeconds
				<= static_cast<double>(
					Settings.MaxTotalValidationSeconds)
					+ UE_DOUBLE_SMALL_NUMBER
			&& FMath::IsFinite(Settings.SignificantDisplacementCM)
			&& Settings.SignificantDisplacementCM > 0.0f
			&& FMath::IsFinite(Settings.SignificantRotationDegrees)
			&& Settings.SignificantRotationDegrees > 0.0f
			&& IsUnitRatio(
				Settings.MaxOrdinaryPredictedAffectedMassRatio)
			&& IsUnitRatio(Settings.MinWeakAffectedMassRatio)
			&& IsUnitRatio(Settings.MaxWeakAffectedMassRatio)
			&& Settings.MinWeakAffectedMassRatio
				<= Settings.MaxWeakAffectedMassRatio
			&& IsUnitRatio(
				Settings.MinPredictedAffectedRealizationRatio)
			&& IsUnitRatio(Settings.MaxOrdinaryAffectedMassRatio)
			&& FMath::IsFinite(Settings.MinWeakResponseAdvantage)
			&& Settings.MinWeakResponseAdvantage >= 1.0f
			&& IsUnitRatio(
				Settings.MinWeakAbsoluteAffectedMassAdvantage)
			&& FMath::IsFinite(
				Settings.MinFailureDirectionAlignment)
			&& Settings.MinFailureDirectionAlignment >= -1.0f
			&& Settings.MinFailureDirectionAlignment <= 1.0f
			&& FMath::IsFinite(Settings.MinWeakResponseScore)
			&& Settings.MinWeakResponseScore >= 0.0f
			&& IsFiniteNonNegative(
				Settings.MinSecondaryContactSpeedCMPerSec)
			&& IsFiniteNonNegative(
				Settings.SecondaryContactDebounceSeconds);
		if (!bValid)
		{
			OutError = TEXT("DAG4ResponseSettingsInvalid");
		}
		return bValid;
	}

	void ResetTrialResultFields(
		FABTSM73DAG4ValidationResult& Result)
	{
		Result.bChaosComparisonAccepted = false;
		Result.bAccepted = false;
		Result.Trials.Reset();
		Result.WeakTrialIndex = INDEX_NONE;
		Result.WeakResponseScore = 0.0f;
		Result.MaxOrdinaryResponseScore = 0.0f;
		Result.MaxOrdinaryAffectedMassRatio = 0.0f;
		Result.WeakResponseAdvantage = 0.0f;
		Result.TotalValidationSeconds = 0.0f;
		Result.ValidationHash = 0;
		Result.RejectReason.Reset();
	}

	int32 QuantizeForHash(
		const float Value,
		const float Scale = 1000.0f)
	{
		const double Clamped = FMath::Clamp(
			static_cast<double>(Value),
			-1000000.0,
			1000000.0);
		return static_cast<int32>(FMath::RoundToInt64(
			Clamped * static_cast<double>(Scale)));
	}

	void AddHashValue(uint32& Hash, const uint32 Value)
	{
		Hash = HashCombineFast(Hash, Value);
	}

	uint32 EnsureNonZeroHash(const uint32 Hash)
	{
		return Hash != 0 ? Hash : 1u;
	}

	uint32 BuildValidationHash(
		const FABTSM73DAG4ValidationSettings& Settings,
		const EABTSM73DAGFailureMotion ExpectedMotion,
		const FABTSM73DAG4ValidationResult& Result)
	{
		uint32 Hash = 0xD474CE47u;
		AddHashValue(
			Hash,
			GetTypeHash(
				static_cast<uint32>(Result.BaselineContactHash)));
		AddHashValue(
			Hash,
			GetTypeHash(
				static_cast<uint32>(
					static_cast<uint64>(
						Result.BaselineContactHash) >> 32)));
		AddHashValue(
			Hash,
			GetTypeHash(
				static_cast<uint32>(Result.SettledContactHash)));
		AddHashValue(
			Hash,
			GetTypeHash(
				static_cast<uint32>(
					static_cast<uint64>(
						Result.SettledContactHash) >> 32)));
		AddHashValue(Hash, static_cast<uint32>(ExpectedMotion));
		AddHashValue(
			Hash,
			GetTypeHash(Settings.NonWeakProbeCount));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.SignificantDisplacementCM)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.SignificantRotationDegrees)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinWeakAffectedMassRatio)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MaxWeakAffectedMassRatio)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinWeakResponseAdvantage)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MaxOrdinaryPredictedAffectedMassRatio)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinPredictedAffectedRealizationRatio)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MaxOrdinaryAffectedMassRatio)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinWeakAbsoluteAffectedMassAdvantage)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinFailureDirectionAlignment)));
		AddHashValue(
			Hash,
			GetTypeHash(QuantizeForHash(
				Settings.MinWeakResponseScore)));
		AddHashValue(
			Hash,
			GetTypeHash(Result.Trials.Num()));
		for (const FABTSM73DAG4TrialMetrics& Trial : Result.Trials)
		{
			AddHashValue(Hash, 0x74726961u);
			AddHashValue(Hash, static_cast<uint32>(Trial.Kind));
			AddHashValue(Hash, GetTypeHash(Trial.ProbeIndex));
			AddHashValue(
				Hash,
				GetTypeHash(Trial.RemovedNodeIds.Num()));
			for (const int32 RemovedNodeId : Trial.RemovedNodeIds)
			{
				AddHashValue(Hash, GetTypeHash(RemovedNodeId));
			}
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.PredictedAffectedMainBodyMassRatio)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.AffectedMainBodyMassRatio)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.PredictedAffectedRealizationRatio)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.MaxDisplacementCM)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.MaxRotationDegrees)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.MaxDropDistanceCM)));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.MaxExpectedDirectionSlideCM)));
			AddHashValue(
				Hash,
				GetTypeHash(Trial.PropagationDepth));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.DirectionAlignment)));
			AddHashValue(
				Hash,
				GetTypeHash(Trial.SecondaryContactCount));
			AddHashValue(
				Hash,
				GetTypeHash(QuantizeForHash(
					Trial.ResponseScore)));
		}
		return EnsureNonZeroHash(Hash);
	}

	bool ValidateCompletedTrialMetrics(
		const FABTSM73DAG4ValidationSettings& Settings,
		const FABTSM73DAG4TrialMetrics& Trial,
		const int32 TrialIndex,
		FString& OutError)
	{
		if (!IsTrialKindValid(Trial.Kind)
			|| !Trial.bCompleted
			|| !Trial.RejectReason.IsEmpty()
			|| Trial.ProbeIndex < 0
			|| Trial.RemovedNodeIds.Num() != 1
			|| ContainsDuplicateIds(Trial.RemovedNodeIds)
			|| !FMath::IsFinite(Trial.DurationSeconds)
			|| Trial.DurationSeconds
				+ KINDA_SMALL_NUMBER
					< Settings.TrialDurationSeconds
			|| Trial.DurationSeconds
				> Settings.MaxTotalValidationSeconds
			|| Trial.TickCount <= 0
			|| Trial.TickCount > Settings.MaxTrialTickCount
			|| !IsUnitRatio(
				Trial.PredictedAffectedMainBodyMassRatio)
			|| !IsUnitRatio(Trial.AffectedMainBodyMassRatio)
			|| !IsUnitRatio(
				Trial.PredictedAffectedRealizationRatio)
			|| !IsFiniteNonNegative(Trial.MaxDisplacementCM)
			|| !IsFiniteNonNegative(Trial.MaxRotationDegrees)
			|| !IsFiniteNonNegative(Trial.MaxDropDistanceCM)
			|| !IsFiniteNonNegative(
				Trial.MaxExpectedDirectionSlideCM)
			|| Trial.PropagationDepth < 0
			|| !FMath::IsFinite(Trial.DirectionAlignment)
			|| Trial.DirectionAlignment < -1.0f
			|| Trial.DirectionAlignment > 1.0f
			|| Trial.SecondaryContactCount < 0
			|| Trial.SecondaryContactCount
				> Settings.MaxContactEventCount
			|| !FMath::IsFinite(Trial.ResponseScore)
			|| Trial.ResponseScore < 0.0f
			|| Trial.ResponseScore > 1.0f)
		{
			OutError = FString::Printf(
				TEXT("DAG4ComparisonTrialInvalid:%d"),
				TrialIndex);
			return false;
		}
		return true;
	}
}

bool FABTSM73DAG4ResponseEvaluator::EvaluateTrial(
	const FABTSM73DAG4ValidationSettings& Settings,
	const FABTSM73DAG4TrialEvaluationInput& Input,
	FABTSM73DAG4TrialMetrics& OutMetrics,
	FString& OutError) const
{
	OutMetrics = FABTSM73DAG4TrialMetrics();
	OutError.Reset();
	if (!Settings.bEnableSettledChaosValidation)
	{
		return true;
	}

	auto Reject = [&Input, &OutMetrics, &OutError](
		const FString& Reason)
	{
		FABTSM73DAG4TrialMetrics Rejected;
		Rejected.Kind = Input.Plan.Kind;
		Rejected.ProbeIndex = Input.Plan.ProbeIndex;
		Rejected.RemovedNodeIds = Input.Plan.RemovedNodeIds;
		SortUniqueIds(Rejected.RemovedNodeIds);
		Rejected.RejectReason = Reason;
		OutMetrics = MoveTemp(Rejected);
		OutError = Reason;
		return false;
	};

	FString ValidationError;
	if (!ValidateResponseSettings(Settings, ValidationError))
	{
		return Reject(ValidationError);
	}
	if (!IsTrialKindValid(Input.Plan.Kind)
		|| !IsFailureMotionValid(Input.ExpectedMotion)
		|| !IsFiniteVector(Input.ExpectedFailureDirectionLocal)
		|| ((Input.ExpectedMotion == EABTSM73DAGFailureMotion::Tip
				|| Input.ExpectedMotion
					== EABTSM73DAGFailureMotion::SlideThenTip)
			&& FVector(
				Input.ExpectedFailureDirectionLocal.X,
				Input.ExpectedFailureDirectionLocal.Y,
				0.0f).IsNearlyZero()))
	{
		return Reject(TEXT("DAG4ResponseMotionInputInvalid"));
	}
	if (Input.Nodes.IsEmpty()
		|| Input.Nodes.Num() > Settings.MaxSettledBodyCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4ResponseBodyBudgetInvalid:%d:%d"),
			Input.Nodes.Num(),
			Settings.MaxSettledBodyCount));
	}
	if (Input.SettledContacts.Num()
		> Settings.MaxContactPairQueryCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4ResponseContactBudgetExceeded:%d:%d"),
			Input.SettledContacts.Num(),
			Settings.MaxContactPairQueryCount));
	}
	if (Input.SecondaryContactNodePairs.Num() % 2 != 0)
	{
		return Reject(TEXT("DAG4ResponseSecondaryPairArrayInvalid"));
	}
	const int32 SuppliedSecondaryPairCount =
		Input.SecondaryContactNodePairs.Num() / 2;
	if (SuppliedSecondaryPairCount
		> Settings.MaxContactEventCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4ResponseContactEventBudgetExceeded:%d:%d"),
			SuppliedSecondaryPairCount,
			Settings.MaxContactEventCount));
	}
	if (!FMath::IsFinite(Input.DurationSeconds)
		|| Input.DurationSeconds
			+ KINDA_SMALL_NUMBER
				< Settings.TrialDurationSeconds
		|| Input.DurationSeconds
			> Settings.MaxTotalValidationSeconds)
	{
		return Reject(TEXT("DAG4ResponseDurationBudgetInvalid"));
	}
	if (Input.TickCount <= 0
		|| Input.TickCount > Settings.MaxTrialTickCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4ResponseTickBudgetInvalid:%d:%d"),
			Input.TickCount,
			Settings.MaxTrialTickCount));
	}
	if (Input.Plan.ProbeIndex < 0
		|| Input.Plan.RemovedNodeIds.Num() != 1
		|| ContainsDuplicateIds(Input.Plan.RemovedNodeIds)
		|| ContainsDuplicateIds(
			Input.Plan.PredictedAffectedMainBodyNodeIds)
		|| !IsUnitRatio(
			Input.Plan.PredictedAffectedMainBodyMassRatio))
	{
		return Reject(TEXT("DAG4ResponsePlanInvalid"));
	}

	TMap<int32, const FABTSM73DAG4SettledNode*> NodesById;
	double TotalMainBodyMass = 0.0;
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		if (Node.NodeId == INDEX_NONE
			|| NodesById.Contains(Node.NodeId)
			|| !FMath::IsFinite(Node.Mass)
			|| Node.Mass <= 0.0)
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseNodeInvalid:%d"),
				Node.NodeId));
		}
		NodesById.Add(Node.NodeId, &Node);
		if (Node.bMainBody)
		{
			TotalMainBodyMass += Node.Mass;
		}
	}
	if (!FMath::IsFinite(TotalMainBodyMass)
		|| TotalMainBodyMass <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Reject(TEXT("DAG4ResponseMainBodyMassInvalid"));
	}

	TSet<int32> RemovedNodeIds;
	for (const int32 NodeId : Input.Plan.RemovedNodeIds)
	{
		if (!NodesById.Contains(NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseRemovedNodeMissing:%d"),
				NodeId));
		}
		RemovedNodeIds.Add(NodeId);
	}

	TSet<int32> PredictedAffectedNodeIds;
	double PredictedAffectedMass = 0.0;
	for (const int32 NodeId
		: Input.Plan.PredictedAffectedMainBodyNodeIds)
	{
		const FABTSM73DAG4SettledNode* const* FoundNode =
			NodesById.Find(NodeId);
		if (FoundNode == nullptr
			|| *FoundNode == nullptr
			|| !(*FoundNode)->bMainBody
			|| RemovedNodeIds.Contains(NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponsePredictedNodeInvalid:%d"),
				NodeId));
		}
		PredictedAffectedNodeIds.Add(NodeId);
		PredictedAffectedMass += (*FoundNode)->Mass;
	}
	if (Input.Plan.Kind == EABTSM73DAG4TrialKind::WeakPoint
		&& PredictedAffectedNodeIds.IsEmpty())
	{
		return Reject(TEXT("DAG4ResponseWeakPredictionEmpty"));
	}

	TMap<int32, const FABTSM73DAG4NodeOutcome*> OutcomesById;
	for (const FABTSM73DAG4NodeOutcome& Outcome : Input.Outcomes)
	{
		if (!NodesById.Contains(Outcome.NodeId)
			|| RemovedNodeIds.Contains(Outcome.NodeId)
			|| OutcomesById.Contains(Outcome.NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseOutcomeInvalid:%d:Identity"),
				Outcome.NodeId));
		}
		if (!IsFiniteVector(Outcome.FinalDisplacementLocal)
			|| !IsFiniteNonNegative(
				Outcome.FinalRotationDegrees)
			|| !IsFiniteNonNegative(Outcome.MaxDisplacementCM)
			|| !IsFiniteNonNegative(Outcome.MaxRotationDegrees)
			|| !IsFiniteNonNegative(Outcome.MaxDropDistanceCM)
			|| !IsFiniteNonNegative(
				Outcome.MaxExpectedDirectionSlideCM))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseOutcomeInvalid:%d:NonFinite:Final=(%.6f,%.6f,%.6f):FinalRot=%.6f:Max=%.6f:MaxRot=%.6f:Drop=%.6f:Slide=%.6f"),
				Outcome.NodeId,
				Outcome.FinalDisplacementLocal.X,
				Outcome.FinalDisplacementLocal.Y,
				Outcome.FinalDisplacementLocal.Z,
				Outcome.FinalRotationDegrees,
				Outcome.MaxDisplacementCM,
				Outcome.MaxRotationDegrees,
				Outcome.MaxDropDistanceCM,
				Outcome.MaxExpectedDirectionSlideCM));
		}
		constexpr float FinalMetricTolerance = 0.05f;
		if (Outcome.FinalDisplacementLocal.Size()
				> Outcome.MaxDisplacementCM + FinalMetricTolerance
			|| Outcome.FinalRotationDegrees
				> Outcome.MaxRotationDegrees + FinalMetricTolerance)
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseOutcomeInvalid:%d:FinalExceedsMax:Final=%.6f:Max=%.6f:FinalRot=%.6f:MaxRot=%.6f"),
				Outcome.NodeId,
				Outcome.FinalDisplacementLocal.Size(),
				Outcome.MaxDisplacementCM,
				Outcome.FinalRotationDegrees,
				Outcome.MaxRotationDegrees));
		}
		OutcomesById.Add(Outcome.NodeId, &Outcome);
	}
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		if (!RemovedNodeIds.Contains(Node.NodeId)
			&& !OutcomesById.Contains(Node.NodeId))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseOutcomeMissing:%d"),
				Node.NodeId));
		}
	}

	TMap<int32, TArray<int32>> AdjacencyByNodeId;
	TSet<uint64> SettledContactKeys;
	for (const FABTSM73DAG4SettledContact& Contact
		: Input.SettledContacts)
	{
		if (Contact.LowerNodeId == Contact.UpperNodeId
			|| !NodesById.Contains(Contact.LowerNodeId)
			|| !NodesById.Contains(Contact.UpperNodeId)
			|| !FMath::IsFinite(Contact.ContactAreaCM2)
			|| Contact.ContactAreaCM2 <= 0.0f
			|| !FMath::IsFinite(Contact.SignedGapCM))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseSettledContactInvalid:%d:%d"),
				Contact.LowerNodeId,
				Contact.UpperNodeId));
		}
		const uint64 ContactKey = MakeUndirectedPairKey(
			Contact.LowerNodeId,
			Contact.UpperNodeId);
		if (SettledContactKeys.Contains(ContactKey))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseSettledContactDuplicate:%d:%d"),
				Contact.LowerNodeId,
				Contact.UpperNodeId));
		}
		SettledContactKeys.Add(ContactKey);
		AdjacencyByNodeId.FindOrAdd(Contact.LowerNodeId).Add(
			Contact.UpperNodeId);
		AdjacencyByNodeId.FindOrAdd(Contact.UpperNodeId).Add(
			Contact.LowerNodeId);
	}

	FABTSM73DAG4TrialMetrics Working;
	Working.Kind = Input.Plan.Kind;
	Working.ProbeIndex = Input.Plan.ProbeIndex;
	Working.RemovedNodeIds = Input.Plan.RemovedNodeIds;
	SortUniqueIds(Working.RemovedNodeIds);
	Working.DurationSeconds = Input.DurationSeconds;
	Working.TickCount = Input.TickCount;
	Working.PredictedAffectedMainBodyMassRatio =
		FMath::Clamp(
			static_cast<float>(
				PredictedAffectedMass / TotalMainBodyMass),
			0.0f,
			1.0f);
	if (!FMath::IsNearlyEqual(
		Working.PredictedAffectedMainBodyMassRatio,
		Input.Plan.PredictedAffectedMainBodyMassRatio,
		1.0e-4f))
	{
		return Reject(FString::Printf(
			TEXT("DAG4ResponsePlanPredictionMismatch:%.6f:%.6f"),
			Input.Plan.PredictedAffectedMainBodyMassRatio,
			Working.PredictedAffectedMainBodyMassRatio));
	}

	TSet<int32> SignificantMovedNodeIds;
	TSet<int32> SignificantMovedMainBodyNodeIds;
	double AffectedMainBodyMass = 0.0;
	double RealizedPredictedMass = 0.0;
	double DirectionMass = 0.0;
	FVector MassWeightedFinalDisplacement = FVector::ZeroVector;
	for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
	{
		if (RemovedNodeIds.Contains(Node.NodeId))
		{
			continue;
		}
		const FABTSM73DAG4NodeOutcome* const* FoundOutcome =
			OutcomesById.Find(Node.NodeId);
		if (FoundOutcome == nullptr || *FoundOutcome == nullptr)
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseOutcomeMissing:%d"),
				Node.NodeId));
		}
		const FABTSM73DAG4NodeOutcome& Outcome = **FoundOutcome;
		Working.MaxDisplacementCM = FMath::Max(
			Working.MaxDisplacementCM,
			Outcome.MaxDisplacementCM);
		Working.MaxRotationDegrees = FMath::Max(
			Working.MaxRotationDegrees,
			Outcome.MaxRotationDegrees);
		Working.MaxDropDistanceCM = FMath::Max(
			Working.MaxDropDistanceCM,
			Outcome.MaxDropDistanceCM);
		Working.MaxExpectedDirectionSlideCM = FMath::Max(
			Working.MaxExpectedDirectionSlideCM,
			Outcome.MaxExpectedDirectionSlideCM);

		const bool bSignificant =
			Outcome.MaxDisplacementCM
				>= Settings.SignificantDisplacementCM
			|| Outcome.MaxRotationDegrees
				>= Settings.SignificantRotationDegrees;
		if (!bSignificant)
		{
			continue;
		}
		SignificantMovedNodeIds.Add(Node.NodeId);
		if (!Node.bMainBody)
		{
			continue;
		}
		SignificantMovedMainBodyNodeIds.Add(Node.NodeId);
		AffectedMainBodyMass += Node.Mass;
		DirectionMass += Node.Mass;
		MassWeightedFinalDisplacement +=
			Outcome.FinalDisplacementLocal * Node.Mass;
		if (PredictedAffectedNodeIds.Contains(Node.NodeId))
		{
			RealizedPredictedMass += Node.Mass;
		}
	}
	Working.AffectedMainBodyMassRatio =
		FMath::Clamp(
			static_cast<float>(
				AffectedMainBodyMass / TotalMainBodyMass),
			0.0f,
			1.0f);
	Working.PredictedAffectedRealizationRatio =
		PredictedAffectedMass > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(
				static_cast<float>(
					RealizedPredictedMass
						/ PredictedAffectedMass),
				0.0f,
				1.0f)
			: 0.0f;

	if (DirectionMass > UE_DOUBLE_SMALL_NUMBER)
	{
		const FVector WeightedDisplacement =
			MassWeightedFinalDisplacement / DirectionMass;
		if (Input.ExpectedMotion == EABTSM73DAGFailureMotion::Drop)
		{
			Working.DirectionAlignment =
				WeightedDisplacement.IsNearlyZero()
					? 0.0f
					: FVector::DotProduct(
						WeightedDisplacement.GetSafeNormal(),
						-FVector::UpVector);
		}
		else
		{
			const FVector PlanarDisplacement(
				WeightedDisplacement.X,
				WeightedDisplacement.Y,
				0.0f);
			const FVector ExpectedPlanarDirection(
				Input.ExpectedFailureDirectionLocal.X,
				Input.ExpectedFailureDirectionLocal.Y,
				0.0f);
			Working.DirectionAlignment =
				PlanarDisplacement.IsNearlyZero()
					? 0.0f
					: FVector::DotProduct(
						PlanarDisplacement.GetSafeNormal(),
						ExpectedPlanarDirection.GetSafeNormal());
		}
	}
	Working.DirectionAlignment = FMath::Clamp(
		Working.DirectionAlignment,
		-1.0f,
		1.0f);

	TMap<int32, int32> DistanceByNodeId;
	TArray<int32> TraversalQueue;
	for (const int32 RemovedNodeId : RemovedNodeIds)
	{
		DistanceByNodeId.Add(RemovedNodeId, 0);
		TraversalQueue.Add(RemovedNodeId);
	}
	for (int32 QueueIndex = 0;
		QueueIndex < TraversalQueue.Num();
		++QueueIndex)
	{
		const int32 CurrentNodeId = TraversalQueue[QueueIndex];
		const int32 CurrentDistance =
			DistanceByNodeId.FindChecked(CurrentNodeId);
		const TArray<int32>* Neighbors =
			AdjacencyByNodeId.Find(CurrentNodeId);
		if (Neighbors == nullptr)
		{
			continue;
		}
		for (const int32 NeighborNodeId : *Neighbors)
		{
			if (DistanceByNodeId.Contains(NeighborNodeId))
			{
				continue;
			}
			DistanceByNodeId.Add(
				NeighborNodeId,
				CurrentDistance + 1);
			TraversalQueue.Add(NeighborNodeId);
		}
	}
	for (const int32 SignificantNodeId : SignificantMovedNodeIds)
	{
		if (const int32* Distance =
			DistanceByNodeId.Find(SignificantNodeId))
		{
			Working.PropagationDepth = FMath::Max(
				Working.PropagationDepth,
				*Distance);
		}
	}

	TSet<uint64> SecondaryContactKeys;
	for (int32 PairIndex = 0;
		PairIndex < Input.SecondaryContactNodePairs.Num();
		PairIndex += 2)
	{
		const int32 NodeA =
			Input.SecondaryContactNodePairs[PairIndex];
		const int32 NodeB =
			Input.SecondaryContactNodePairs[PairIndex + 1];
		const bool bNodeAKnown = NodesById.Contains(NodeA);
		const bool bNodeBKnown = NodesById.Contains(NodeB);
		const bool bExternalPair =
			(NodeA == INDEX_NONE && bNodeBKnown)
			|| (NodeB == INDEX_NONE && bNodeAKnown);
		if (NodeA == NodeB
			|| (!bExternalPair
				&& (!bNodeAKnown || !bNodeBKnown))
			|| (bNodeAKnown && RemovedNodeIds.Contains(NodeA))
			|| (bNodeBKnown && RemovedNodeIds.Contains(NodeB)))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseSecondaryPairInvalid:%d:%d"),
				NodeA,
				NodeB));
		}
		const uint64 SecondaryKey =
			MakeUndirectedPairKey(NodeA, NodeB);
		if (!bExternalPair
			&& SettledContactKeys.Contains(SecondaryKey))
		{
			return Reject(FString::Printf(
				TEXT("DAG4ResponseSecondaryPairWasSettled:%d:%d"),
				NodeA,
				NodeB));
		}
		if (SignificantMovedMainBodyNodeIds.Contains(NodeA)
			|| SignificantMovedMainBodyNodeIds.Contains(NodeB))
		{
			SecondaryContactKeys.Add(SecondaryKey);
		}
	}
	Working.SecondaryContactCount = SecondaryContactKeys.Num();

	// DAG-4 ResponseScore v1 is deliberately frozen and bounded:
	// 50% affected main-body mass, 15% peak displacement, 15% peak
	// rotation, 10% contact-graph propagation and 10% secondary contacts.
	// Direction remains an independent semantic certification gate so a
	// wrong-way collapse cannot improve the scalar response score.
	const float MassTerm = FMath::Clamp(
		Working.AffectedMainBodyMassRatio,
		0.0f,
		1.0f);
	const float DisplacementTerm = FMath::Clamp(
		Working.MaxDisplacementCM
			/ (Settings.SignificantDisplacementCM
				* ScoreDisplacementSaturationMultiplier),
		0.0f,
		1.0f);
	const float RotationTerm = FMath::Clamp(
		Working.MaxRotationDegrees
			/ (Settings.SignificantRotationDegrees
				* ScoreRotationSaturationMultiplier),
		0.0f,
		1.0f);
	const float PropagationTerm = FMath::Clamp(
		static_cast<float>(Working.PropagationDepth)
			/ ScorePropagationDepthSaturation,
		0.0f,
		1.0f);
	const float SecondaryContactTerm = FMath::Clamp(
		static_cast<float>(Working.SecondaryContactCount)
			/ ScoreSecondaryContactSaturation,
		0.0f,
		1.0f);
	Working.ResponseScore = FMath::Clamp(
		0.50f * MassTerm
			+ 0.15f * DisplacementTerm
			+ 0.15f * RotationTerm
			+ 0.10f * PropagationTerm
			+ 0.10f * SecondaryContactTerm,
		0.0f,
		1.0f);
	Working.bCompleted = true;
	OutMetrics = MoveTemp(Working);
	return true;
}

bool FABTSM73DAG4ResponseEvaluator::CertifyComparison(
	const FABTSM73DAG4ValidationSettings& Settings,
	const EABTSM73DAGFailureMotion ExpectedMotion,
	const TConstArrayView<FABTSM73DAG4TrialMetrics> Trials,
	FABTSM73DAG4ValidationResult& InOutResult,
	FString& OutError) const
{
	// The input view is allowed to alias InOutResult.Trials. Snapshot it
	// before clearing stale rollout fields from a previous certification.
	TArray<FABTSM73DAG4TrialMetrics> TrialSnapshot;
	TrialSnapshot.Append(Trials.GetData(), Trials.Num());
	ResetTrialResultFields(InOutResult);
	InOutResult.bEnabled =
		Settings.bEnableSettledChaosValidation;
	OutError.Reset();
	if (!Settings.bEnableSettledChaosValidation)
	{
		return true;
	}

	auto RejectInput = [&InOutResult, &OutError](
		const FString& Reason)
	{
		ResetTrialResultFields(InOutResult);
		InOutResult.RejectReason = Reason;
		OutError = Reason;
		return false;
	};

	FString ValidationError;
	if (!ValidateResponseSettings(Settings, ValidationError))
	{
		return RejectInput(ValidationError);
	}
	if (!InOutResult.bSettledContactAccepted)
	{
		return RejectInput(
			TEXT("DAG4ComparisonSettledPrerequisiteMissing"));
	}
	if (!IsFailureMotionValid(ExpectedMotion))
	{
		return RejectInput(
			TEXT("DAG4ComparisonExpectedMotionInvalid"));
	}
	const int32 RequiredTrialCount =
		Settings.NonWeakProbeCount + 1;
	if (TrialSnapshot.Num() != RequiredTrialCount
		|| TrialSnapshot.Num() > Settings.MaxTrialCount)
	{
		return RejectInput(FString::Printf(
			TEXT("DAG4ComparisonTrialCountMismatch:%d:%d"),
			TrialSnapshot.Num(),
			RequiredTrialCount));
	}

	int32 WeakTrialIndex = INDEX_NONE;
	int32 WeakTrialCount = 0;
	int32 OrdinaryTrialCount = 0;
	float TotalTrialSeconds = 0.0f;
	float MaxOrdinaryResponseScore = 0.0f;
	float MaxOrdinaryAffectedMassRatio = 0.0f;
	TSet<int32> RemovedNodeIds;
	TSet<int32> OrdinaryProbeIndices;
	for (int32 TrialIndex = 0;
		TrialIndex < TrialSnapshot.Num();
		++TrialIndex)
	{
		const FABTSM73DAG4TrialMetrics& Trial =
			TrialSnapshot[TrialIndex];
		if (!ValidateCompletedTrialMetrics(
			Settings,
			Trial,
			TrialIndex,
			ValidationError))
		{
			return RejectInput(ValidationError);
		}
		const int32 RemovedNodeId = Trial.RemovedNodeIds[0];
		if (RemovedNodeIds.Contains(RemovedNodeId))
		{
			return RejectInput(FString::Printf(
				TEXT("DAG4ComparisonRemovedNodeRepeated:%d"),
				RemovedNodeId));
		}
		RemovedNodeIds.Add(RemovedNodeId);
		TotalTrialSeconds += Trial.DurationSeconds;
		if (!FMath::IsFinite(TotalTrialSeconds)
			|| TotalTrialSeconds
				> Settings.MaxTotalValidationSeconds
					+ KINDA_SMALL_NUMBER)
		{
			return RejectInput(
				TEXT("DAG4ComparisonTotalTimeBudgetExceeded"));
		}
		if (Trial.Kind == EABTSM73DAG4TrialKind::WeakPoint)
		{
			if (TrialIndex != 0 || Trial.ProbeIndex != 0)
			{
				return RejectInput(
					TEXT("DAG4ComparisonWeakTrialOrderInvalid"));
			}
			WeakTrialIndex = TrialIndex;
			++WeakTrialCount;
		}
		else
		{
			if (Trial.ProbeIndex >= Settings.NonWeakProbeCount
				|| OrdinaryProbeIndices.Contains(Trial.ProbeIndex))
			{
				return RejectInput(FString::Printf(
					TEXT("DAG4ComparisonOrdinaryProbeInvalid:%d"),
					Trial.ProbeIndex));
			}
			OrdinaryProbeIndices.Add(Trial.ProbeIndex);
			++OrdinaryTrialCount;
			MaxOrdinaryResponseScore = FMath::Max(
				MaxOrdinaryResponseScore,
				Trial.ResponseScore);
			MaxOrdinaryAffectedMassRatio = FMath::Max(
				MaxOrdinaryAffectedMassRatio,
				Trial.AffectedMainBodyMassRatio);
		}
	}
	if (WeakTrialCount != 1)
	{
		return RejectInput(FString::Printf(
			TEXT("DAG4ComparisonWeakTrialCountMismatch:%d:1"),
			WeakTrialCount));
	}
	if (OrdinaryTrialCount != Settings.NonWeakProbeCount)
	{
		return RejectInput(FString::Printf(
			TEXT("DAG4ComparisonOrdinaryTrialCountMismatch:%d:%d"),
			OrdinaryTrialCount,
			Settings.NonWeakProbeCount));
	}

	FABTSM73DAG4ValidationResult Candidate = InOutResult;
	Candidate.Trials = TrialSnapshot;
	Candidate.WeakTrialIndex = WeakTrialIndex;
	const FABTSM73DAG4TrialMetrics& WeakTrial =
		Candidate.Trials[WeakTrialIndex];
	Candidate.WeakResponseScore = WeakTrial.ResponseScore;
	Candidate.MaxOrdinaryResponseScore =
		MaxOrdinaryResponseScore;
	Candidate.MaxOrdinaryAffectedMassRatio =
		MaxOrdinaryAffectedMassRatio;
	Candidate.WeakResponseAdvantage =
		MaxOrdinaryResponseScore > SMALL_NUMBER
			? WeakTrial.ResponseScore
				/ MaxOrdinaryResponseScore
			: (WeakTrial.ResponseScore > SMALL_NUMBER
				? 1000000.0f
				: 1.0f);
	Candidate.TotalValidationSeconds = TotalTrialSeconds;

	auto RejectCertification =
		[&Candidate, &InOutResult, &OutError](
			const FString& Reason)
	{
		Candidate.bChaosComparisonAccepted = false;
		Candidate.bAccepted = false;
		Candidate.ValidationHash = 0;
		Candidate.RejectReason = Reason;
		InOutResult = MoveTemp(Candidate);
		OutError = Reason;
		return false;
	};

	for (const FABTSM73DAG4TrialMetrics& Trial
		: Candidate.Trials)
	{
		if (Trial.Kind != EABTSM73DAG4TrialKind::Ordinary)
		{
			continue;
		}
		if (Trial.PredictedAffectedMainBodyMassRatio
			> Settings.MaxOrdinaryPredictedAffectedMassRatio
				+ KINDA_SMALL_NUMBER)
		{
			return RejectCertification(FString::Printf(
				TEXT("DAG4OrdinaryPredictedMassExceeded:%d:%.3f:%.3f"),
				Trial.ProbeIndex,
				Trial.PredictedAffectedMainBodyMassRatio,
				Settings.MaxOrdinaryPredictedAffectedMassRatio));
		}
		if (Trial.AffectedMainBodyMassRatio
			> Settings.MaxOrdinaryAffectedMassRatio
				+ KINDA_SMALL_NUMBER)
		{
			return RejectCertification(FString::Printf(
				TEXT("DAG4OrdinaryAffectedMassExceeded:%d:%.3f:%.3f"),
				Trial.ProbeIndex,
				Trial.AffectedMainBodyMassRatio,
				Settings.MaxOrdinaryAffectedMassRatio));
		}
	}
	if (WeakTrial.AffectedMainBodyMassRatio
		+ KINDA_SMALL_NUMBER
			< Settings.MinWeakAffectedMassRatio
		|| WeakTrial.AffectedMainBodyMassRatio
			> Settings.MaxWeakAffectedMassRatio
				+ KINDA_SMALL_NUMBER)
	{
		return RejectCertification(FString::Printf(
			TEXT("DAG4WeakAffectedMassOutsideRange:%.3f:%.3f:%.3f"),
			WeakTrial.AffectedMainBodyMassRatio,
			Settings.MinWeakAffectedMassRatio,
			Settings.MaxWeakAffectedMassRatio));
	}
	if (WeakTrial.PredictedAffectedRealizationRatio
		+ KINDA_SMALL_NUMBER
			< Settings.MinPredictedAffectedRealizationRatio)
	{
		return RejectCertification(FString::Printf(
			TEXT("DAG4WeakPredictedRealizationTooLow:%.3f:%.3f"),
			WeakTrial.PredictedAffectedRealizationRatio,
			Settings.MinPredictedAffectedRealizationRatio));
	}

	const bool bDirectionAccepted =
		WeakTrial.DirectionAlignment + KINDA_SMALL_NUMBER
			>= Settings.MinFailureDirectionAlignment;
	switch (ExpectedMotion)
	{
	case EABTSM73DAGFailureMotion::Drop:
		if (WeakTrial.MaxDropDistanceCM + KINDA_SMALL_NUMBER
				< Settings.SignificantDisplacementCM
			|| !bDirectionAccepted)
		{
			return RejectCertification(FString::Printf(
				TEXT("DAG4WeakDropMotionRejected:%.2f:%.3f"),
				WeakTrial.MaxDropDistanceCM,
				WeakTrial.DirectionAlignment));
		}
		break;
	case EABTSM73DAGFailureMotion::Tip:
		if (WeakTrial.MaxRotationDegrees + KINDA_SMALL_NUMBER
				< Settings.SignificantRotationDegrees
			|| !bDirectionAccepted)
		{
			return RejectCertification(FString::Printf(
				TEXT("DAG4WeakTipMotionRejected:%.2f:%.3f"),
				WeakTrial.MaxRotationDegrees,
				WeakTrial.DirectionAlignment));
		}
		break;
	case EABTSM73DAGFailureMotion::SlideThenTip:
		if (WeakTrial.MaxRotationDegrees + KINDA_SMALL_NUMBER
				< Settings.SignificantRotationDegrees
			|| WeakTrial.MaxExpectedDirectionSlideCM
				+ KINDA_SMALL_NUMBER
					< Settings.SignificantDisplacementCM
			|| !bDirectionAccepted)
		{
			return RejectCertification(FString::Printf(
				TEXT("DAG4WeakSeamMotionRejected:%.2f:%.2f:%.3f"),
				WeakTrial.MaxRotationDegrees,
				WeakTrial.MaxExpectedDirectionSlideCM,
				WeakTrial.DirectionAlignment));
		}
		break;
	default:
		return RejectCertification(
			TEXT("DAG4ComparisonExpectedMotionInvalid"));
	}
	if (WeakTrial.ResponseScore + KINDA_SMALL_NUMBER
		< Settings.MinWeakResponseScore)
	{
		return RejectCertification(FString::Printf(
			TEXT("DAG4WeakResponseScoreTooLow:%.3f:%.3f"),
			WeakTrial.ResponseScore,
			Settings.MinWeakResponseScore));
	}
	if (WeakTrial.ResponseScore + KINDA_SMALL_NUMBER
		< MaxOrdinaryResponseScore
			* Settings.MinWeakResponseAdvantage)
	{
		return RejectCertification(FString::Printf(
			TEXT("DAG4WeakResponseAdvantageTooLow:%.3f:%.3f:%.3f"),
			WeakTrial.ResponseScore,
			MaxOrdinaryResponseScore,
			Settings.MinWeakResponseAdvantage));
	}
	if (WeakTrial.AffectedMainBodyMassRatio
			- MaxOrdinaryAffectedMassRatio
			+ KINDA_SMALL_NUMBER
		< Settings.MinWeakAbsoluteAffectedMassAdvantage)
	{
		return RejectCertification(FString::Printf(
			TEXT("DAG4WeakAffectedMassAdvantageTooLow:%.3f:%.3f:%.3f"),
			WeakTrial.AffectedMainBodyMassRatio,
			MaxOrdinaryAffectedMassRatio,
			Settings.MinWeakAbsoluteAffectedMassAdvantage));
	}

	Candidate.bChaosComparisonAccepted = true;
	Candidate.bAccepted = true;
	Candidate.RejectReason.Reset();
	Candidate.ValidationHash = static_cast<int64>(
		BuildValidationHash(
			Settings,
			ExpectedMotion,
			Candidate));
	InOutResult = MoveTemp(Candidate);
	return true;
}
