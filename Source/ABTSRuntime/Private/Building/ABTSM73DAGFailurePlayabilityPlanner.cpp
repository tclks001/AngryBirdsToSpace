// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGFailurePlayabilityPlanner.h"

#include "Building/ABTSM7BuildingTypes.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM73WeakPointAnalysis.h"

namespace ABTSM73DAG3CPlayabilityPrivate
{
	constexpr float AttackAlignmentThreshold = 0.25f;
	constexpr float SegmentEntryTolerance = 1.0e-4f;
	constexpr float AttackFaceInsetRatio = 0.80f;
	constexpr int32 ClearanceBinarySearchIterations = 12;
	constexpr float MaximumReportedAttackClearanceCM = 500.0f;

	struct FAttackCandidateResult
	{
		bool bReachable = false;
		int32 VisibleSampleCount = 0;
		int32 TotalSampleCount = 0;
		int32 BlockingNodeId = INDEX_NONE;
		FVector BestImpactPoint = FVector::ZeroVector;
		float BestClearanceCM = 0.0f;
	};

	const FABTSM73BrickNode* FindNode(
		const FABTSM73StructureData& Data,
		const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	}

	FABTSM73BrickNode* FindMutableNode(
		FABTSM73StructureData& Data,
		const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	}

	FBox MakeNodeBox(const FABTSM73BrickNode& Node)
	{
		const FVector Half = Node.DimensionsCM * 0.5f;
		return FBox(Node.LocalCenter - Half, Node.LocalCenter + Half);
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
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

	bool SegmentBoxEntry(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FBox& Box,
		float& OutEntry)
	{
		const FVector Delta = SegmentEnd - SegmentStart;
		float Entry = 0.0f;
		float Exit = 1.0f;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Origin = SegmentStart[Axis];
			const float Direction = Delta[Axis];
			if (FMath::Abs(Direction) <= SMALL_NUMBER)
			{
				if (Origin < Box.Min[Axis] || Origin > Box.Max[Axis])
				{
					return false;
				}
				continue;
			}
			float Near = (Box.Min[Axis] - Origin) / Direction;
			float Far = (Box.Max[Axis] - Origin) / Direction;
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
		OutEntry = Entry;
		return Exit >= 0.0f && Entry <= 1.0f;
	}

	FBox ExpandBox(const FBox& Box, const float AmountCM)
	{
		const FVector Expansion(FMath::Max(0.0f, AmountCM));
		return FBox(Box.Min - Expansion, Box.Max + Expansion);
	}

	float EstimateCorridorClearance(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FBox& BlockerBox,
		const float ProjectileRadiusCM)
	{
		float Entry = 0.0f;
		if (!SegmentBoxEntry(
			SegmentStart,
			SegmentEnd,
			ExpandBox(BlockerBox, ProjectileRadiusCM + MaximumReportedAttackClearanceCM),
			Entry))
		{
			return MaximumReportedAttackClearanceCM;
		}

		float Low = 0.0f;
		float High = MaximumReportedAttackClearanceCM;
		for (int32 Iteration = 0; Iteration < ClearanceBinarySearchIterations; ++Iteration)
		{
			const float Mid = 0.5f * (Low + High);
			if (SegmentBoxEntry(
				SegmentStart,
				SegmentEnd,
				ExpandBox(BlockerBox, ProjectileRadiusCM + Mid),
				Entry))
			{
				High = Mid;
			}
			else
			{
				Low = Mid;
			}
		}
		return Low;
	}

	void GetAttackFaceAxes(
		const FVector& Direction,
		int32& OutFaceAxis,
		int32& OutAxisU,
		int32& OutAxisV)
	{
		OutFaceAxis = 0;
		if (FMath::Abs(Direction.Y) > FMath::Abs(Direction[OutFaceAxis]))
		{
			OutFaceAxis = 1;
		}
		if (FMath::Abs(Direction.Z) > FMath::Abs(Direction[OutFaceAxis]))
		{
			OutFaceAxis = 2;
		}
		OutAxisU = (OutFaceAxis + 1) % 3;
		OutAxisV = (OutFaceAxis + 2) % 3;
	}

	FAttackCandidateResult EvaluateAttackCandidate(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const FVector& AttackDirection,
		const FABTSM73BrickNode& Target,
		const FABTSM73StructureData& Data)
	{
		FAttackCandidateResult Result;
		const FBox TargetBox = MakeNodeBox(Target);
		int32 FaceAxis = 0;
		int32 AxisU = 1;
		int32 AxisV = 2;
		GetAttackFaceAxes(AttackDirection, FaceAxis, AxisU, AxisV);
		const int32 Resolution = FMath::Clamp(Settings.AttackFaceGridResolution, 1, 5);
		const float FaceCoordinate = AttackDirection[FaceAxis] >= 0.0f
			? TargetBox.Min[FaceAxis]
			: TargetBox.Max[FaceAxis];
		const FVector Half = Target.DimensionsCM * 0.5f;
		float EarliestBlockingEntry = TNumericLimits<float>::Max();

		for (int32 UIndex = 0; UIndex < Resolution; ++UIndex)
		{
			const float UAlpha = Resolution == 1
				? 0.0f
				: (static_cast<float>(UIndex) / (Resolution - 1)) * 2.0f - 1.0f;
			for (int32 VIndex = 0; VIndex < Resolution; ++VIndex)
			{
				const float VAlpha = Resolution == 1
					? 0.0f
					: (static_cast<float>(VIndex) / (Resolution - 1)) * 2.0f - 1.0f;
				FVector Impact = Target.LocalCenter;
				Impact[FaceAxis] = FaceCoordinate;
				Impact[AxisU] += UAlpha * Half[AxisU] * AttackFaceInsetRatio;
				Impact[AxisV] += VAlpha * Half[AxisV] * AttackFaceInsetRatio;
				const FVector Start =
					Impact - AttackDirection * Settings.AttackApproachDistanceCM;
				++Result.TotalSampleCount;

				bool bBlocked = false;
				int32 SampleBlockerId = INDEX_NONE;
				float SampleBlockingEntry = TNumericLimits<float>::Max();
				float SampleClearanceCM = MaximumReportedAttackClearanceCM;
				for (const FABTSM73BrickNode& Blocker : Data.Bricks)
				{
					if (Blocker.NodeId == Target.NodeId)
					{
						continue;
					}
					const FBox BlockerBox = MakeNodeBox(Blocker);
					float Entry = 0.0f;
					if (SegmentBoxEntry(
						Start,
						Impact,
						ExpandBox(BlockerBox, Settings.ProjectileRadiusCM),
						Entry)
						&& Entry < 1.0f - SegmentEntryTolerance)
					{
						bBlocked = true;
						if (Entry < SampleBlockingEntry)
						{
							SampleBlockingEntry = Entry;
							SampleBlockerId = Blocker.NodeId;
						}
						continue;
					}
					SampleClearanceCM = FMath::Min(
						SampleClearanceCM,
						EstimateCorridorClearance(
							Start,
							Impact,
							BlockerBox,
							Settings.ProjectileRadiusCM));
				}

				if (bBlocked)
				{
					if (SampleBlockingEntry < EarliestBlockingEntry)
					{
						EarliestBlockingEntry = SampleBlockingEntry;
						Result.BlockingNodeId = SampleBlockerId;
					}
					continue;
				}
				++Result.VisibleSampleCount;
				if (!Result.bReachable
					|| SampleClearanceCM > Result.BestClearanceCM + KINDA_SMALL_NUMBER)
				{
					Result.bReachable = true;
					Result.BestImpactPoint = Impact;
					Result.BestClearanceCM = SampleClearanceCM;
				}
			}
		}
		return Result;
	}

	bool BoxesPenetrate(
		const FBox& A,
		const FBox& B,
		const float ToleranceCM)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Overlap =
				FMath::Min(A.Max[Axis], B.Max[Axis])
				- FMath::Max(A.Min[Axis], B.Min[Axis]);
			if (Overlap <= ToleranceCM)
			{
				return false;
			}
		}
		return true;
	}

	FBox MakeTransformedNodeBox(
		const FABTSM73BrickNode& Node,
		const FVector& Translation,
		const FVector& Pivot,
		const FVector& RotationAxis,
		const float RotationDegrees)
	{
		const FVector Half = Node.DimensionsCM * 0.5f;
		const FQuat Rotation = FMath::Abs(RotationDegrees) > SMALL_NUMBER
			? FQuat(RotationAxis, FMath::DegreesToRadians(RotationDegrees))
			: FQuat::Identity;
		FBox Result(EForceInit::ForceInit);
		for (int32 X = -1; X <= 1; X += 2)
		{
			for (int32 Y = -1; Y <= 1; Y += 2)
			{
				for (int32 Z = -1; Z <= 1; Z += 2)
				{
					const FVector Corner = Node.LocalCenter
						+ FVector(Half.X * X, Half.Y * Y, Half.Z * Z)
						+ Translation;
					Result += Pivot + Rotation.RotateVector(Corner - Pivot);
				}
			}
		}
		return Result;
	}

	FBox MakeSweptTransformedNodeBox(
		const FABTSM73BrickNode& Node,
		const FVector& PreviousTranslation,
		const FVector& CurrentTranslation,
		const FVector& Pivot,
		const FVector& RotationAxis,
		const float PreviousRotationDegrees,
		const float CurrentRotationDegrees)
	{
		FBox Result = MakeTransformedNodeBox(
			Node,
			PreviousTranslation,
			Pivot,
			RotationAxis,
			PreviousRotationDegrees);
		Result += MakeTransformedNodeBox(
			Node,
			PreviousTranslation,
			Pivot,
			RotationAxis,
			CurrentRotationDegrees);

		const FVector SafeAxis = RotationAxis.GetSafeNormal();
		const double MinAngle = FMath::DegreesToRadians(FMath::Min(
			PreviousRotationDegrees,
			CurrentRotationDegrees));
		const double MaxAngle = FMath::DegreesToRadians(FMath::Max(
			PreviousRotationDegrees,
			CurrentRotationDegrees));
		if (!SafeAxis.IsNearlyZero()
			&& MaxAngle - MinAngle > UE_DOUBLE_SMALL_NUMBER)
		{
			const FVector Half = Node.DimensionsCM * 0.5f;
			for (int32 X = -1; X <= 1; X += 2)
			{
				for (int32 Y = -1; Y <= 1; Y += 2)
				{
					for (int32 Z = -1; Z <= 1; Z += 2)
					{
						const FVector Corner =
							Node.LocalCenter
							+ FVector(
								Half.X * X,
								Half.Y * Y,
								Half.Z * Z)
							+ PreviousTranslation;
						const FVector Relative = Corner - Pivot;
						const FVector Parallel =
							SafeAxis
							* FVector::DotProduct(SafeAxis, Relative);
						const FVector Perpendicular =
							Relative - Parallel;
						const FVector Cross =
							FVector::CrossProduct(
								SafeAxis,
								Relative);
						for (int32 Component = 0; Component < 3;
							++Component)
						{
							const double Phase = FMath::Atan2(
								Cross[Component],
								Perpendicular[Component]);
							const int32 FirstK = FMath::CeilToInt(
								(MinAngle - Phase) / UE_DOUBLE_PI);
							const int32 LastK = FMath::FloorToInt(
								(MaxAngle - Phase) / UE_DOUBLE_PI);
							for (int32 K = FirstK; K <= LastK; ++K)
							{
								const double CriticalAngle =
									Phase + K * UE_DOUBLE_PI;
								const FQuat Rotation(
									SafeAxis,
									CriticalAngle);
								Result += Pivot
									+ Rotation.RotateVector(Relative);
							}
						}
					}
				}
			}
		}

		const FVector TranslationDelta =
			CurrentTranslation - PreviousTranslation;
		if (!TranslationDelta.IsNearlyZero())
		{
			Result += FBox(
				Result.Min + TranslationDelta,
				Result.Max + TranslationDelta);
		}
		return Result;
	}

	bool CheckMotionConfiguration(
		const FABTSM73StructureData& Data,
		const TArray<int32>& AffectedNodeIds,
		const TSet<int32>& ExcludedStaticNodeIds,
		const FVector& PreviousTranslation,
		const FVector& CurrentTranslation,
		const FVector& Pivot,
		const FVector& RotationAxis,
		const float PreviousRotationDegrees,
		const float CurrentRotationDegrees,
		const float CollisionToleranceCM,
		int32& OutBlockingNodeId)
	{
		OutBlockingNodeId = INDEX_NONE;
		for (const int32 AffectedNodeId : AffectedNodeIds)
		{
			const FABTSM73BrickNode* Affected = FindNode(Data, AffectedNodeId);
			if (Affected == nullptr)
			{
				OutBlockingNodeId = AffectedNodeId;
				return false;
			}
			const FBox SweptBox = MakeSweptTransformedNodeBox(
				*Affected,
				PreviousTranslation,
				CurrentTranslation,
				Pivot,
				RotationAxis,
				PreviousRotationDegrees,
				CurrentRotationDegrees);
			for (const FABTSM73BrickNode& Blocker : Data.Bricks)
			{
				if (ExcludedStaticNodeIds.Contains(Blocker.NodeId)
					|| AffectedNodeIds.Contains(Blocker.NodeId))
				{
					continue;
				}
				if (BoxesPenetrate(
					SweptBox,
					MakeNodeBox(Blocker),
					CollisionToleranceCM))
				{
					OutBlockingNodeId = Blocker.NodeId;
					return false;
				}
			}
		}
		return true;
	}

	bool ConsumeMotionSample(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		FABTSM73DAGFailurePlayabilityResult& Result,
		FString& OutError)
	{
		if (Result.MotionSweepSampleCount
			< Settings.MaxMotionSweepSampleCount)
		{
			++Result.MotionSweepSampleCount;
			return true;
		}
		OutError = FString::Printf(
			TEXT("DAG3CMotionSweepBudgetExceeded:%d:%d"),
			Result.MotionSweepSampleCount,
			Settings.MaxMotionSweepSampleCount);
		Result.RejectReason = OutError;
		return false;
	}

	bool SweepTranslation(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const FABTSM73StructureData& Data,
		const TArray<int32>& AffectedNodeIds,
		const TSet<int32>& ExcludedStaticNodeIds,
		const FVector& UnitDirection,
		const float RequiredDistanceCM,
		FABTSM73DAGFailurePlayabilityResult& Result,
		float& OutFreeDistanceCM,
		int32& OutBlockingNodeId,
		FString& OutError)
	{
		OutFreeDistanceCM = 0.0f;
		OutBlockingNodeId = INDEX_NONE;
		const float StepCM = Settings.TranslationSweepStepCM;
		const int32 StepCount = FMath::CeilToInt(RequiredDistanceCM / StepCM);
		for (int32 Step = 1; Step <= StepCount; ++Step)
		{
			if (!ConsumeMotionSample(Settings, Result, OutError))
			{
				return false;
			}
			const float PreviousDistanceCM = FMath::Min(
				RequiredDistanceCM,
				(Step - 1) * StepCM);
			const float CurrentDistanceCM = FMath::Min(
				RequiredDistanceCM,
				Step * StepCM);
			if (!CheckMotionConfiguration(
				Data,
				AffectedNodeIds,
				ExcludedStaticNodeIds,
				UnitDirection * PreviousDistanceCM,
				UnitDirection * CurrentDistanceCM,
				FVector::ZeroVector,
				FVector::UpVector,
				0.0f,
				0.0f,
				Settings.CollisionToleranceCM,
				OutBlockingNodeId))
			{
				OutFreeDistanceCM = PreviousDistanceCM;
				return false;
			}
			OutFreeDistanceCM = CurrentDistanceCM;
		}
		return true;
	}

	bool SweepTip(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const FABTSM73StructureData& Data,
		const TArray<int32>& AffectedNodeIds,
		const TSet<int32>& ExcludedStaticNodeIds,
		const FVector& BaseTranslation,
		const FVector& Pivot,
		const FVector& RotationAxis,
		const float RequiredAngleDegrees,
		FABTSM73DAGFailurePlayabilityResult& Result,
		float& OutFreeAngleDegrees,
		int32& OutBlockingNodeId,
		FString& OutError)
	{
		OutFreeAngleDegrees = 0.0f;
		OutBlockingNodeId = INDEX_NONE;
		const float StepDegrees = Settings.TipSweepStepDegrees;
		const int32 StepCount =
			FMath::CeilToInt(RequiredAngleDegrees / StepDegrees);
		for (int32 Step = 1; Step <= StepCount; ++Step)
		{
			if (!ConsumeMotionSample(Settings, Result, OutError))
			{
				return false;
			}
			const float PreviousAngle = FMath::Min(
				RequiredAngleDegrees,
				(Step - 1) * StepDegrees);
			const float CurrentAngle = FMath::Min(
				RequiredAngleDegrees,
				Step * StepDegrees);
			if (!CheckMotionConfiguration(
				Data,
				AffectedNodeIds,
				ExcludedStaticNodeIds,
				BaseTranslation,
				BaseTranslation,
				Pivot,
				RotationAxis,
				PreviousAngle,
				CurrentAngle,
				Settings.CollisionToleranceCM,
				OutBlockingNodeId))
			{
				OutFreeAngleDegrees = PreviousAngle;
				return false;
			}
			OutFreeAngleDegrees = CurrentAngle;
		}
		return true;
	}

	bool GatherAffectedClosure(
		const FABTSM73StructureData& Data,
		const int32 RootNodeId,
		TArray<int32>& OutAffectedNodeIds,
		FString& OutError)
	{
		if (FindNode(Data, RootNodeId) == nullptr)
		{
			OutError = FString::Printf(
				TEXT("DAG3CAffectedRootMissing:%d"),
				RootNodeId);
			return false;
		}
		TMap<int32, TArray<int32>> Children;
		for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
		{
			if (FindNode(Data, Edge.LowerNodeId) == nullptr
				|| FindNode(Data, Edge.UpperNodeId) == nullptr)
			{
				OutError = FString::Printf(
					TEXT("DAG3CInvalidSupportEdge:%d:%d"),
					Edge.LowerNodeId,
					Edge.UpperNodeId);
				return false;
			}
			Children.FindOrAdd(Edge.LowerNodeId).AddUnique(Edge.UpperNodeId);
		}

		TSet<int32> Visited;
		TArray<int32> Queue;
		Visited.Add(RootNodeId);
		Queue.Add(RootNodeId);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			if (const TArray<int32>* Next = Children.Find(Queue[Head]))
			{
				for (const int32 NodeId : *Next)
				{
					if (!Visited.Contains(NodeId))
					{
						Visited.Add(NodeId);
						Queue.Add(NodeId);
					}
				}
			}
		}
		OutAffectedNodeIds = Visited.Array();
		SortUniqueIds(OutAffectedNodeIds);
		return true;
	}

	bool ValidateSettings(
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		FString& OutError)
	{
		const bool bValid =
			FMath::IsFinite(Settings.ProjectileRadiusCM)
			&& Settings.ProjectileRadiusCM >= 1.0f
			&& Settings.ProjectileRadiusCM <= 200.0f
			&& FMath::IsFinite(Settings.AttackApproachDistanceCM)
			&& Settings.AttackApproachDistanceCM >= 100.0f
			&& Settings.AttackApproachDistanceCM <= 5000.0f
			&& Settings.AttackApproachDistanceCM
				> Settings.ProjectileRadiusCM
			&& Settings.AttackFaceGridResolution >= 1
			&& Settings.AttackFaceGridResolution <= 5
			&& FMath::IsFinite(Settings.MinAttackExposure)
			&& Settings.MinAttackExposure >= 0.0f
			&& Settings.MinAttackExposure <= 1.0f
			&& FMath::IsFinite(Settings.MinFreeDropDistanceCM)
			&& Settings.MinFreeDropDistanceCM >= 0.0f
			&& Settings.MinFreeDropDistanceCM <= 300.0f
			&& FMath::IsFinite(Settings.MinFreeTipAngleDegrees)
			&& Settings.MinFreeTipAngleDegrees >= 0.0f
			&& Settings.MinFreeTipAngleDegrees <= 45.0f
			&& FMath::IsFinite(Settings.MinFreeSlideDistanceCM)
			&& Settings.MinFreeSlideDistanceCM >= 0.0f
			&& Settings.MinFreeSlideDistanceCM <= 300.0f
			&& FMath::IsFinite(Settings.TranslationSweepStepCM)
			&& Settings.TranslationSweepStepCM >= 0.5f
			&& Settings.TranslationSweepStepCM <= 50.0f
			&& FMath::IsFinite(Settings.TipSweepStepDegrees)
			&& Settings.TipSweepStepDegrees >= 0.25f
			&& Settings.TipSweepStepDegrees <= 10.0f
			&& FMath::IsFinite(Settings.CollisionToleranceCM)
			&& Settings.CollisionToleranceCM >= 0.0f
			&& Settings.CollisionToleranceCM <= 10.0f
			&& Settings.MaxMotionSweepSampleCount >= 8
			&& Settings.MaxMotionSweepSampleCount <= 1024;
		if (!bValid)
		{
			OutError = TEXT("DAG3CSettingsInvalid");
		}
		return bValid;
	}

	bool ValidateDifficultySettings(
		const FABTSM73DifficultySettings& Settings,
		FString& OutError)
	{
		const bool bValid =
			FMath::IsFinite(Settings.MinWeakCollapseRatio)
			&& FMath::IsFinite(Settings.TargetWeakCollapseRatio)
			&& FMath::IsFinite(Settings.MaxSingleWeakCollapseRatio)
			&& Settings.MinWeakCollapseRatio >= 0.0f
			&& Settings.MaxSingleWeakCollapseRatio <= 1.0f
			&& Settings.MinWeakCollapseRatio
				<= Settings.TargetWeakCollapseRatio
			&& Settings.TargetWeakCollapseRatio
				<= Settings.MaxSingleWeakCollapseRatio;
		if (!bValid)
		{
			OutError = TEXT("DAG3CDifficultySettingsInvalid");
		}
		return bValid;
	}

	bool ValidateMaterialProfile(
		const FABTSM7MaterialProfile& Profile)
	{
		return FMath::IsFinite(Profile.KnockSpeedCMPerSec)
			&& Profile.KnockSpeedCMPerSec >= 0.0f
			&& FMath::IsFinite(Profile.BreakSpeedCMPerSec)
			&& Profile.BreakSpeedCMPerSec > 0.0f
			&& FMath::IsFinite(Profile.DynamicFriction)
			&& Profile.DynamicFriction >= 0.0f
			&& FMath::IsFinite(Profile.StaticFriction)
			&& Profile.StaticFriction >= 0.0f
			&& FMath::IsFinite(Profile.Restitution)
			&& Profile.Restitution >= 0.0f
			&& Profile.Restitution <= 1.0f
			&& FMath::IsFinite(Profile.DensityGPerCubicCM)
			&& Profile.DensityGPerCubicCM > 0.0f
			&& FMath::IsFinite(Profile.DamageAtBreakSpeed)
			&& Profile.DamageAtBreakSpeed > 0.0f
			&& FMath::IsFinite(Profile.BreakDamage)
			&& Profile.BreakDamage > 0.0f
			&& FMath::IsFinite(Profile.PushVelocityTransfer)
			&& Profile.PushVelocityTransfer >= 0.0f
			&& Profile.PushVelocityTransfer <= 2.0f;
	}

	double NodeMass(
		const FABTSM73BrickNode& Node,
		const FABTSM7MaterialProfile& Profile)
	{
		const FVector Dimensions = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		return static_cast<double>(Dimensions.X)
			* Dimensions.Y
			* Dimensions.Z
			* static_cast<double>(Profile.DensityGPerCubicCM);
	}

	int32 QuantizeForHash(const float Value, const float Scale = 1000.0f)
	{
		return FMath::RoundToInt(Value * Scale);
	}

	uint32 BuildPlayabilityHash(
		const FABTSM73DAGFailurePatternResult& Pattern,
		const FABTSM73DAGFailurePlayabilitySettings& Settings,
		const FABTSM73DifficultySettings& DifficultySettings,
		const float AffectedMassRatio,
		const FABTSM73DAGFailurePlayabilityResult& Result)
	{
		uint32 Hash = 0x73C2A11Du;
		auto Add = [&Hash](const uint32 Value)
		{
			Hash = HashCombineFast(Hash, Value);
		};
		Add(Pattern.RealizedPatternHash);
		Add(static_cast<uint32>(Result.Pattern));
		Add(static_cast<uint32>(Result.ExpectedMotion));
		Add(static_cast<uint32>(Result.Material));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialKnockSpeedCMPerSec)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialBreakSpeedCMPerSec)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialDynamicFriction)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialStaticFriction)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialRestitution)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialDensityGPerCubicCM)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialDamageAtBreakSpeed)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialBreakDamage)));
		Add(GetTypeHash(QuantizeForHash(
			Result.MaterialPushVelocityTransfer)));
		Add(GetTypeHash(QuantizeForHash(Settings.ProjectileRadiusCM)));
		Add(GetTypeHash(QuantizeForHash(Settings.AttackApproachDistanceCM)));
		Add(GetTypeHash(Settings.AttackFaceGridResolution));
		Add(GetTypeHash(QuantizeForHash(Settings.MinAttackExposure)));
		Add(GetTypeHash(QuantizeForHash(Settings.MinFreeDropDistanceCM)));
		Add(GetTypeHash(QuantizeForHash(Settings.MinFreeTipAngleDegrees)));
		Add(GetTypeHash(QuantizeForHash(Settings.MinFreeSlideDistanceCM)));
		Add(GetTypeHash(QuantizeForHash(Settings.TranslationSweepStepCM)));
		Add(GetTypeHash(QuantizeForHash(Settings.TipSweepStepDegrees)));
		Add(GetTypeHash(QuantizeForHash(Settings.CollisionToleranceCM)));
		Add(GetTypeHash(Settings.MaxMotionSweepSampleCount));
		Add(Settings.bRequireAllWeakNodesReachable ? 1u : 0u);
		Add(DifficultySettings.bRejectOutsideDifficultyWindow ? 1u : 0u);
		Add(GetTypeHash(QuantizeForHash(
			DifficultySettings.MinWeakCollapseRatio)));
		Add(GetTypeHash(QuantizeForHash(
			DifficultySettings.TargetWeakCollapseRatio)));
		Add(GetTypeHash(QuantizeForHash(
			DifficultySettings.MaxSingleWeakCollapseRatio)));
		Add(GetTypeHash(QuantizeForHash(AffectedMassRatio)));
		for (const int32 NodeId : Result.WeakNodeIds)
		{
			Add(GetTypeHash(NodeId));
		}
		for (const int32 NodeId : Result.AffectedNodeIds)
		{
			Add(GetTypeHash(NodeId));
		}
		Add(GetTypeHash(QuantizeForHash(Result.AcceptedAttackDirectionLocal.X)));
		Add(GetTypeHash(QuantizeForHash(Result.AcceptedAttackDirectionLocal.Y)));
		Add(GetTypeHash(QuantizeForHash(Result.AcceptedAttackDirectionLocal.Z)));
		Add(GetTypeHash(QuantizeForHash(Result.AttackImpactPointLocal.X)));
		Add(GetTypeHash(QuantizeForHash(Result.AttackImpactPointLocal.Y)));
		Add(GetTypeHash(QuantizeForHash(Result.AttackImpactPointLocal.Z)));
		Add(GetTypeHash(QuantizeForHash(Result.AttackExposure)));
		Add(GetTypeHash(QuantizeForHash(Result.MinAttackClearanceCM)));
		Add(GetTypeHash(QuantizeForHash(Result.FreeDropDistanceCM)));
		Add(GetTypeHash(QuantizeForHash(Result.FreeTipAngleDegrees)));
		Add(GetTypeHash(QuantizeForHash(Result.FreeSlideDistanceCM)));
		Add(GetTypeHash(QuantizeForHash(Result.LocalBreakEffort)));
		Add(GetTypeHash(Result.EstimatedHits));
		Add(GetTypeHash(Result.AttackSampleCount));
		Add(GetTypeHash(Result.MotionSweepSampleCount));
		return Hash != 0 ? Hash : 1u;
	}

	EABTSM73PredictedCollapseMode MapLegacyCollapseMode(
		const EABTSM73DAGFailureMotion Motion)
	{
		switch (Motion)
		{
		case EABTSM73DAGFailureMotion::Tip:
			return EABTSM73PredictedCollapseMode::Tip;
		case EABTSM73DAGFailureMotion::SlideThenTip:
			return EABTSM73PredictedCollapseMode::SlideAndTip;
		default:
			return EABTSM73PredictedCollapseMode::None;
		}
	}
}

bool FABTSM73DAGFailurePlayabilityPlanner::Plan(
	const FABTSM73DAGFailurePlayabilitySettings& Settings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const EABTSM7BuildingMaterial ExpectedBuildingMaterial,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FVector& LocalAttackDirection,
	FABTSM73StructureData& InOutData,
	FABTSM73DAGFailurePlayabilityResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73DAGFailurePlayabilityResult();
	OutError.Reset();
	if (!Settings.bEnablePlayabilityRouting)
	{
		return true;
	}

	OutResult.bEnabled = true;
	auto Reject = [&OutResult, &OutError](const FString& Reason)
	{
		FABTSM73DAGFailurePlayabilityResult RejectedResult;
		RejectedResult.bEnabled = true;
		RejectedResult.bPlayable = false;
		RejectedResult.RejectReason = Reason;
		OutResult = MoveTemp(RejectedResult);
		OutError = Reason;
		return false;
	};
	if (!ABTSM73DAG3CPlayabilityPrivate::ValidateSettings(
		Settings,
		OutError))
	{
		return Reject(OutError);
	}
	if (!ABTSM73DAG3CPlayabilityPrivate::ValidateDifficultySettings(
		DifficultySettings,
		OutError))
	{
		return Reject(OutError);
	}
	if (!InOutData.DAGFailureFrontierAnalysis.bEnabled
		|| !InOutData.DAGFailureFrontierAnalysis.bAccepted
		|| !InOutData.DAGFailurePatternResult.bEnabled
		|| !InOutData.DAGFailurePatternResult.bApplied)
	{
		return Reject(TEXT("DAG3CPrerequisiteMissing"));
	}
	const FABTSM73DAGFailurePatternResult& Pattern =
		InOutData.DAGFailurePatternResult;
	OutResult.Pattern = Pattern.Pattern;
	OutResult.ExpectedMotion = Pattern.ExpectedMotion;
	if (Pattern.WeakNodeIds.Num() != 1
		|| Pattern.LoadPlateNodeId == INDEX_NONE
		|| Pattern.ExpectedMotion == EABTSM73DAGFailureMotion::None)
	{
		return Reject(TEXT("DAG3CPatternContractInvalid"));
	}
	if (!InOutData.WeakPoints.IsEmpty())
	{
		return Reject(TEXT("DAG3CWeakPointStateDirty"));
	}

	TSet<int32> UniqueNodeIds;
	EABTSM7BuildingMaterial BuildingMaterial =
		EABTSM7BuildingMaterial::Wood;
	bool bFirstNode = true;
	for (const FABTSM73BrickNode& Node : InOutData.Bricks)
	{
		if (Node.NodeId == INDEX_NONE
			|| UniqueNodeIds.Contains(Node.NodeId)
			|| !ABTSM73DAG3CPlayabilityPrivate::IsFiniteVector(
				Node.LocalCenter)
			|| !ABTSM73DAG3CPlayabilityPrivate::IsFiniteVector(
				Node.DimensionsCM)
			|| Node.DimensionsCM.GetMin() <= 0.0f)
		{
			return Reject(TEXT("DAG3CBrickIdentityOrGeometryInvalid"));
		}
		UniqueNodeIds.Add(Node.NodeId);
		if (Node.Material != Node.OriginalMaterial
			|| Node.bWeakPoint
			|| Node.bReinforcedCriticalNode)
		{
			return Reject(TEXT("DAG3CMaterialOrWeakStateChanged"));
		}
		if (bFirstNode)
		{
			BuildingMaterial = Node.Material;
			bFirstNode = false;
		}
		else if (Node.Material != BuildingMaterial)
		{
			return Reject(TEXT("DAG3CMixedMaterialUnsupported"));
		}
	}
	if (bFirstNode)
	{
		return Reject(TEXT("DAG3CNoBricks"));
	}
	if (BuildingMaterial != ExpectedBuildingMaterial)
	{
		return Reject(TEXT("DAG3CPrimaryMaterialMismatch"));
	}
	OutResult.Material = BuildingMaterial;

	const FABTSM7MaterialProfile* ActualProfile = nullptr;
	int32 MatchingProfileCount = 0;
	for (const FABTSM7MaterialProfile& Profile : MaterialProfiles)
	{
		if (Profile.Material == BuildingMaterial)
		{
			ActualProfile = &Profile;
			++MatchingProfileCount;
		}
	}
	if (MatchingProfileCount != 1
		|| ActualProfile == nullptr
		|| !ABTSM73DAG3CPlayabilityPrivate::ValidateMaterialProfile(
			*ActualProfile))
	{
		return Reject(TEXT("DAG3CMaterialProfileMissingOrInvalid"));
	}
	OutResult.bMaterialProfileValidated = true;
	OutResult.MaterialKnockSpeedCMPerSec =
		ActualProfile->KnockSpeedCMPerSec;
	OutResult.MaterialBreakSpeedCMPerSec =
		ActualProfile->BreakSpeedCMPerSec;
	OutResult.MaterialDynamicFriction =
		ActualProfile->DynamicFriction;
	OutResult.MaterialStaticFriction =
		ActualProfile->StaticFriction;
	OutResult.MaterialRestitution =
		ActualProfile->Restitution;
	OutResult.MaterialDensityGPerCubicCM =
		ActualProfile->DensityGPerCubicCM;
	OutResult.MaterialDamageAtBreakSpeed =
		ActualProfile->DamageAtBreakSpeed;
	OutResult.MaterialBreakDamage =
		ActualProfile->BreakDamage;
	OutResult.MaterialPushVelocityTransfer =
		ActualProfile->PushVelocityTransfer;
	OutResult.LocalBreakEffort =
		ActualProfile->BreakDamage / ActualProfile->DamageAtBreakSpeed;
	if (!FMath::IsFinite(OutResult.LocalBreakEffort)
		|| OutResult.LocalBreakEffort <= 0.0f)
	{
		return Reject(TEXT("DAG3CBreakEffortInvalid"));
	}
	OutResult.EstimatedHits =
		FMath::Max(1, FMath::CeilToInt(OutResult.LocalBreakEffort));

	FVector AttackDirection = LocalAttackDirection.GetSafeNormal();
	if (AttackDirection.IsNearlyZero()
		|| !ABTSM73DAG3CPlayabilityPrivate::IsFiniteVector(
			AttackDirection))
	{
		return Reject(TEXT("DAG3CAttackDirectionInvalid"));
	}
	const FVector HorizontalAttack =
		FVector(AttackDirection.X, AttackDirection.Y, 0.0f).GetSafeNormal();
	const FVector FailureDirection =
		FVector(
			Pattern.ExpectedFailureDirectionLocal.X,
			Pattern.ExpectedFailureDirectionLocal.Y,
			0.0f).GetSafeNormal();
	// AttackDirection is the bird velocity into the building. The authored
	// failure direction points back toward the incoming/weak side.
	if (Pattern.ExpectedMotion != EABTSM73DAGFailureMotion::Drop
		&& (HorizontalAttack.IsNearlyZero()
			|| FailureDirection.IsNearlyZero()
			|| FVector::DotProduct(-HorizontalAttack, FailureDirection)
				< ABTSM73DAG3CPlayabilityPrivate::
					AttackAlignmentThreshold))
	{
		return Reject(TEXT("DAG3CAttackDirectionOpposesFailure"));
	}

	OutResult.WeakNodeIds = Pattern.WeakNodeIds;
	ABTSM73DAG3CPlayabilityPrivate::SortUniqueIds(
		OutResult.WeakNodeIds);
	if (OutResult.WeakNodeIds.Num() != Pattern.WeakNodeIds.Num())
	{
		return Reject(TEXT("DAG3CWeakNodeIdentityInvalid"));
	}
	if (!ABTSM73DAG3CPlayabilityPrivate::GatherAffectedClosure(
		InOutData,
		Pattern.LoadPlateNodeId,
		OutResult.AffectedNodeIds,
		OutError))
	{
		return Reject(OutError);
	}
	for (const int32 NodeId : Pattern.AffectedMainBodyNodeIds)
	{
		if (!OutResult.AffectedNodeIds.Contains(NodeId))
		{
			return Reject(TEXT("DAG3CAffectedClosureMismatch"));
		}
	}

	int32 TotalVisibleAttackSamples = 0;
	float MinimumAcceptedClearanceCM =
		ABTSM73DAG3CPlayabilityPrivate::
			MaximumReportedAttackClearanceCM;
	TMap<int32, float> ExposureByWeakNode;
	bool bHaveImpactPoint = false;
	for (const int32 WeakNodeId : OutResult.WeakNodeIds)
	{
		const FABTSM73BrickNode* WeakNode =
			ABTSM73DAG3CPlayabilityPrivate::FindNode(
				InOutData,
				WeakNodeId);
		if (WeakNode == nullptr
			|| OutResult.AffectedNodeIds.Contains(WeakNodeId))
		{
			return Reject(TEXT("DAG3CWeakNodeContractInvalid"));
		}
		const ABTSM73DAG3CPlayabilityPrivate::FAttackCandidateResult Attack =
			ABTSM73DAG3CPlayabilityPrivate::EvaluateAttackCandidate(
			Settings,
			AttackDirection,
			*WeakNode,
			InOutData);
		OutResult.AttackSampleCount += Attack.TotalSampleCount;
		TotalVisibleAttackSamples += Attack.VisibleSampleCount;
		const float Exposure = Attack.TotalSampleCount > 0
			? static_cast<float>(Attack.VisibleSampleCount)
				/ Attack.TotalSampleCount
			: 0.0f;
		ExposureByWeakNode.Add(WeakNodeId, Exposure);
		if (!Attack.bReachable)
		{
			OutResult.BlockingNodeId = Attack.BlockingNodeId;
			if (Settings.bRequireAllWeakNodesReachable)
			{
				return Reject(FString::Printf(
					TEXT("DAG3CAttackCorridorBlocked:%d:%d"),
					WeakNodeId,
					Attack.BlockingNodeId));
			}
			continue;
		}
		MinimumAcceptedClearanceCM = FMath::Min(
			MinimumAcceptedClearanceCM,
			Attack.BestClearanceCM);
		if (!bHaveImpactPoint)
		{
			OutResult.AttackImpactPointLocal = Attack.BestImpactPoint;
			bHaveImpactPoint = true;
		}
	}
	OutResult.AttackExposure = OutResult.AttackSampleCount > 0
		? static_cast<float>(TotalVisibleAttackSamples)
			/ OutResult.AttackSampleCount
		: 0.0f;
	OutResult.MinAttackClearanceCM = bHaveImpactPoint
		? MinimumAcceptedClearanceCM
		: 0.0f;
	if (!bHaveImpactPoint
		|| OutResult.AttackExposure + KINDA_SMALL_NUMBER
			< Settings.MinAttackExposure)
	{
		return Reject(FString::Printf(
			TEXT("DAG3CAttackExposureTooSmall:%.4f:%.4f"),
			OutResult.AttackExposure,
			Settings.MinAttackExposure));
	}
	OutResult.AcceptedAttackDirectionLocal = AttackDirection;
	OutResult.BlockingNodeId = INDEX_NONE;

	TSet<int32> ExcludedStaticNodeIds;
	for (const int32 WeakNodeId : OutResult.WeakNodeIds)
	{
		ExcludedStaticNodeIds.Add(WeakNodeId);
	}
	for (const int32 PivotNodeId : Pattern.RemainingSupportNodeIds)
	{
		if (ABTSM73DAG3CPlayabilityPrivate::FindNode(
			InOutData,
			PivotNodeId) == nullptr)
		{
			return Reject(TEXT("DAG3CPivotNodeMissing"));
		}
		ExcludedStaticNodeIds.Add(PivotNodeId);
	}

	int32 MotionBlockingNodeId = INDEX_NONE;
	switch (Pattern.ExpectedMotion)
	{
	case EABTSM73DAGFailureMotion::Drop:
		if (!ABTSM73DAG3CPlayabilityPrivate::SweepTranslation(
			Settings,
			InOutData,
			OutResult.AffectedNodeIds,
			ExcludedStaticNodeIds,
			-FVector::UpVector,
			Settings.MinFreeDropDistanceCM,
			OutResult,
			OutResult.FreeDropDistanceCM,
			MotionBlockingNodeId,
			OutError))
		{
			OutResult.BlockingNodeId = MotionBlockingNodeId;
			const FString Reason = OutResult.RejectReason.IsEmpty()
				? FString::Printf(
					TEXT("DAG3CMotionBlocked:Drop:%d:%.3f"),
					MotionBlockingNodeId,
					OutResult.FreeDropDistanceCM)
				: OutResult.RejectReason;
			return Reject(Reason);
		}
		break;
	case EABTSM73DAGFailureMotion::Tip:
	case EABTSM73DAGFailureMotion::SlideThenTip:
		{
			if (Pattern.RemainingSupportNodeIds.Num() != 1)
			{
				return Reject(TEXT("DAG3CPivotCardinalityInvalid"));
			}
			const FABTSM73BrickNode* PivotNode =
				ABTSM73DAG3CPlayabilityPrivate::FindNode(
					InOutData,
					Pattern.RemainingSupportNodeIds[0]);
			if (PivotNode == nullptr || FailureDirection.IsNearlyZero())
			{
				return Reject(TEXT("DAG3CPivotGeometryInvalid"));
			}
			const FVector Pivot = PivotNode->LocalCenter
				+ FVector::UpVector * (PivotNode->DimensionsCM.Z * 0.5f);
			const FVector RotationAxis =
				FVector::CrossProduct(FVector::UpVector, FailureDirection)
					.GetSafeNormal();
			FVector BaseTranslation = FVector::ZeroVector;
			if (Pattern.ExpectedMotion
				== EABTSM73DAGFailureMotion::SlideThenTip)
			{
				if (!ABTSM73DAG3CPlayabilityPrivate::SweepTranslation(
					Settings,
					InOutData,
					OutResult.AffectedNodeIds,
					ExcludedStaticNodeIds,
					FailureDirection,
					Settings.MinFreeSlideDistanceCM,
					OutResult,
					OutResult.FreeSlideDistanceCM,
					MotionBlockingNodeId,
					OutError))
				{
					OutResult.BlockingNodeId = MotionBlockingNodeId;
					const FString Reason = OutResult.RejectReason.IsEmpty()
						? FString::Printf(
							TEXT("DAG3CMotionBlocked:Slide:%d:%.3f"),
							MotionBlockingNodeId,
							OutResult.FreeSlideDistanceCM)
						: OutResult.RejectReason;
					return Reject(Reason);
				}
				BaseTranslation =
					FailureDirection * OutResult.FreeSlideDistanceCM;
			}
			if (!ABTSM73DAG3CPlayabilityPrivate::SweepTip(
				Settings,
				InOutData,
				OutResult.AffectedNodeIds,
				ExcludedStaticNodeIds,
				BaseTranslation,
				Pivot,
				RotationAxis,
				Settings.MinFreeTipAngleDegrees,
				OutResult,
				OutResult.FreeTipAngleDegrees,
				MotionBlockingNodeId,
				OutError))
			{
				OutResult.BlockingNodeId = MotionBlockingNodeId;
				const FString Reason = OutResult.RejectReason.IsEmpty()
					? FString::Printf(
						TEXT("DAG3CMotionBlocked:Tip:%d:%.3f"),
						MotionBlockingNodeId,
						OutResult.FreeTipAngleDegrees)
					: OutResult.RejectReason;
				return Reject(Reason);
			}
		}
		break;
	default:
		return Reject(TEXT("DAG3CUnsupportedMotion"));
	}

	double TotalMainBodyMass = 0.0;
	double AffectedMainBodyMass = 0.0;
	TSet<int32> AffectedSet;
	for (const int32 NodeId : Pattern.AffectedMainBodyNodeIds)
	{
		AffectedSet.Add(NodeId);
	}
	for (const FABTSM73BrickNode& Node : InOutData.Bricks)
	{
		if (!Node.bFailureFrontierMainBody)
		{
			continue;
		}
		const double Mass =
			ABTSM73DAG3CPlayabilityPrivate::NodeMass(
				Node,
				*ActualProfile);
		TotalMainBodyMass += Mass;
		if (AffectedSet.Contains(Node.NodeId))
		{
			AffectedMainBodyMass += Mass;
		}
	}
	const float AffectedMassRatio = TotalMainBodyMass > SMALL_NUMBER
		? static_cast<float>(AffectedMainBodyMass / TotalMainBodyMass)
		: 0.0f;
	if (!FMath::IsFinite(AffectedMassRatio)
		|| AffectedMassRatio <= 0.0f)
	{
		return Reject(TEXT("DAG3CAffectedMassInvalid"));
	}
	if (DifficultySettings.bRejectOutsideDifficultyWindow
		&& (AffectedMassRatio + KINDA_SMALL_NUMBER
				< DifficultySettings.MinWeakCollapseRatio
			|| AffectedMassRatio - KINDA_SMALL_NUMBER
				> DifficultySettings.MaxSingleWeakCollapseRatio))
	{
		return Reject(FString::Printf(
			TEXT("DAG3CAffectedMassOutsideDifficulty:%.4f:%.4f:%.4f"),
			AffectedMassRatio,
			DifficultySettings.MinWeakCollapseRatio,
			DifficultySettings.MaxSingleWeakCollapseRatio));
	}

	FABTSM73StructureData Working = InOutData;
	Working.WeakPoints.Reset();
	Working.ReinforcedNodeIds.Reset();
	Working.BestWeakPointScore = 0.0f;
	Working.PredictedWeakCollapseRatio = AffectedMassRatio;
	Working.PredictedNonWeakEffect = 0.0f;
	Working.EstimatedWeakPointHits = OutResult.EstimatedHits;
	float ScoreSum = 0.0f;
	for (const int32 WeakNodeId : OutResult.WeakNodeIds)
	{
		FABTSM73BrickNode* WeakNode =
			ABTSM73DAG3CPlayabilityPrivate::FindMutableNode(
				Working,
				WeakNodeId);
		if (WeakNode == nullptr)
		{
			return Reject(TEXT("DAG3CWeakNodeLostDuringBinding"));
		}
		const float Exposure = ExposureByWeakNode.FindRef(WeakNodeId);
		const EABTSM73WeakPointRole Role =
			ABTSM73WeakPointAnalysis::ClassifyRole(*WeakNode, Working);
		const float Readability =
			ABTSM73WeakPointAnalysis::RoleReadability(Role);
		const float Score =
			ABTSM73WeakPointAnalysis::ComputeCandidateScore(
				AffectedMassRatio,
				Exposure,
				Readability,
				OutResult.EstimatedHits,
				DifficultySettings);

		WeakNode->WeakPointRole = Role;
		WeakNode->WeakPointScore = Score;
		WeakNode->UnsupportedMassRatio = AffectedMassRatio;
		WeakNode->AttackExposure = Exposure;
		WeakNode->EstimatedHits = OutResult.EstimatedHits;
		WeakNode->bWeakPoint = true;

		FABTSM73WeakPointRecord& Record =
			Working.WeakPoints.AddDefaulted_GetRef();
		Record.NodeId = WeakNodeId;
		Record.Role = Role;
		Record.UnsupportedMassRatio = AffectedMassRatio;
		Record.Exposure = Exposure;
		Record.Readability = Readability;
		Record.LocalBreakEffort = OutResult.LocalBreakEffort;
		Record.Score = Score;
		Record.EstimatedHits = OutResult.EstimatedHits;
		Record.UnsupportedNodeIds = OutResult.AffectedNodeIds;
		Record.AffectedNodeIds = OutResult.AffectedNodeIds;
		Record.CollapseMode =
			ABTSM73DAG3CPlayabilityPrivate::MapLegacyCollapseMode(
				Pattern.ExpectedMotion);
		Record.InitialSupportMarginCM =
			Pattern.InitialSupportMarginCM;
		Record.TipMarginCM =
			Pattern.PostFailureTipMarginCM;
		Record.ReseatRisk = Pattern.ReseatRisk;
		Record.DAGFailurePattern = Pattern.Pattern;
		Record.DAGFailureMotion = Pattern.ExpectedMotion;
		Record.AcceptedAttackDirectionLocal = AttackDirection;
		Working.BestWeakPointScore =
			FMath::Max(Working.BestWeakPointScore, Score);
		ScoreSum += Score;
	}
	Working.WeakPoints.Sort(
		[](const FABTSM73WeakPointRecord& A,
			const FABTSM73WeakPointRecord& B)
		{
			return A.NodeId < B.NodeId;
		});
	Working.DifficultyScore = Working.WeakPoints.IsEmpty()
		? 0.0f
		: ScoreSum / Working.WeakPoints.Num();

	OutResult.bPlayable = true;
	OutResult.RejectReason.Reset();
	OutResult.PlayabilityHash =
		ABTSM73DAG3CPlayabilityPrivate::BuildPlayabilityHash(
			Pattern,
			Settings,
			DifficultySettings,
			AffectedMassRatio,
			OutResult);
	Working.DAGFailurePlayabilityResult = OutResult;
	InOutData = MoveTemp(Working);
	return true;
}
