// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamC3V3SkeletonFirstGenerator.h"

#include "ABTSRuntime.h"
#include "Algo/Unique.h"
#include "HAL/PlatformTime.h"

namespace
{
	using namespace ABTSM73BeamC3V3;
	constexpr int32 BlockUnitsCM = 36;
	constexpr int32 MaximumHorizontalUnits = 18; // 648 cm, below the 720 cm hard gate.
	constexpr double GeometryToleranceCM = 0.5;
	constexpr double WitnessToleranceCM = 1.0;
	constexpr double CoverageMachineEpsilon = DBL_EPSILON;

	double ElapsedMilliseconds(const double StartSeconds)
	{
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	}

	struct FStage1PhaseTimer
	{
		explicit FStage1PhaseTimer(double& InDestinationMilliseconds)
			: DestinationMilliseconds(InDestinationMilliseconds)
			, StartSeconds(FPlatformTime::Seconds())
		{
		}

		~FStage1PhaseTimer()
		{
			Stop();
		}

		void Stop()
		{
			if (!bStopped)
			{
				Checkpoint();
				bStopped = true;
			}
		}

		void Checkpoint()
		{
			if (!bStopped)
			{
				DestinationMilliseconds += ElapsedMilliseconds(StartSeconds);
				StartSeconds = FPlatformTime::Seconds();
			}
		}

		double& DestinationMilliseconds;
		double StartSeconds = 0.0;
		bool bStopped = false;
	};

	struct FMainRailCoverageKey
	{
		int32 Course = 0;
		int32 AlongMinimum = 0;
		int32 AlongMaximum = 0;
		int32 CrossStation = 0;
		uint8 Axis = 0;
		bool bBodyAllowedBoxes = false;

		bool operator==(const FMainRailCoverageKey& Other) const
		{
			return Course == Other.Course
				&& AlongMinimum == Other.AlongMinimum
				&& AlongMaximum == Other.AlongMaximum
				&& CrossStation == Other.CrossStation
				&& Axis == Other.Axis
				&& bBodyAllowedBoxes == Other.bBodyAllowedBoxes;
		}

		friend uint32 GetTypeHash(const FMainRailCoverageKey& Key)
		{
			uint32 Hash = GetTypeHash(Key.Course);
			Hash = HashCombineFast(Hash, GetTypeHash(Key.AlongMinimum));
			Hash = HashCombineFast(Hash, GetTypeHash(Key.AlongMaximum));
			Hash = HashCombineFast(Hash, GetTypeHash(Key.CrossStation));
			Hash = HashCombineFast(Hash, GetTypeHash(Key.Axis));
			return HashCombineFast(Hash, GetTypeHash(Key.bBodyAllowedBoxes));
		}
	};

	struct FMainRailCoverageRowKey
	{
		int32 Course = 0;
		int32 CrossStation = 0;
		uint8 Axis = 0;
		bool bBodyAllowedBoxes = false;

		bool operator==(const FMainRailCoverageRowKey& Other) const
		{
			return Course == Other.Course
				&& CrossStation == Other.CrossStation
				&& Axis == Other.Axis
				&& bBodyAllowedBoxes == Other.bBodyAllowedBoxes;
		}

		friend uint32 GetTypeHash(const FMainRailCoverageRowKey& Key)
		{
			uint32 Hash = GetTypeHash(Key.Course);
			Hash = HashCombineFast(Hash, GetTypeHash(Key.CrossStation));
			Hash = HashCombineFast(Hash, GetTypeHash(Key.Axis));
			return HashCombineFast(Hash, GetTypeHash(Key.bBodyAllowedBoxes));
		}
	};

	struct FMainRailCoverageRow
	{
		int32 AlongMinimum = 0;
		int32 AlongMaximum = -1;
		TArray<int32> UncoveredPrefix;
	};

	struct FMainSourceProbeKey
	{
		// Twice-center grid coordinates preserve half-grid candidate centers exactly.
		int32 CenterXTwiceUnits = 0;
		int32 CenterYTwiceUnits = 0;
		int32 Course = 0;
		bool bAllowCrown = false;

		bool operator==(const FMainSourceProbeKey& Other) const
		{
			return CenterXTwiceUnits == Other.CenterXTwiceUnits
				&& CenterYTwiceUnits == Other.CenterYTwiceUnits
				&& Course == Other.Course
				&& bAllowCrown == Other.bAllowCrown;
		}

		friend uint32 GetTypeHash(const FMainSourceProbeKey& Key)
		{
			uint32 Hash = GetTypeHash(Key.CenterXTwiceUnits);
			Hash = HashCombineFast(Hash, GetTypeHash(Key.CenterYTwiceUnits));
			Hash = HashCombineFast(Hash, GetTypeHash(Key.Course));
			return HashCombineFast(Hash, GetTypeHash(Key.bAllowCrown));
		}
	};

	bool ValidateStage1ElapsedBudget(
		const EGenerationStage Stage,
		const double Elapsed,
		const TCHAR* Phase,
		FPlan& Plan,
		FString& OutError)
	{
		if (Stage != EGenerationStage::CoreAndShared)
		{
			return true;
		}
		Plan.Summary.bStage1TimingEvaluated = true;
		Plan.Summary.Stage1TotalMilliseconds = Elapsed;
		Plan.Summary.Stage1TimeBudgetMilliseconds =
			Stage1LeafTimeBudgetMilliseconds;
		if (Elapsed <= Stage1LeafTimeBudgetMilliseconds)
		{
			Plan.Summary.bStage1WithinTimeBudget = true;
			Plan.Summary.Stage1TimeoutPhase.Reset();
			return true;
		}
		Plan.Summary.bStage1WithinTimeBudget = false;
		Plan.Summary.Stage1TimeoutPhase = Phase;
		OutError = FString::Printf(
			TEXT("BeamC3V3Stage1Timeout:Phase=%s:ElapsedMs=%.3f:BudgetMs=%.3f:TimingMs=Demand:%.3f,Child:%.3f,Main:%.3f,Joint:%.3f,Emission:%.3f,DAG:%.3f"),
			Phase, Elapsed, Stage1LeafTimeBudgetMilliseconds,
			Plan.Summary.TerminalDemandMilliseconds,
			Plan.Summary.ChildCandidateMilliseconds,
			Plan.Summary.PodiumMainCandidateMilliseconds,
			Plan.Summary.JointSelectionMilliseconds,
			Plan.Summary.MemberEmissionMilliseconds,
			Plan.Summary.StaticDAGMilliseconds);
		Plan.Summary.RejectReason = OutError;
		return false;
	}

	bool CheckStage1TimeBudget(
		const EGenerationStage Stage,
		const double StageStartSeconds,
		const TCHAR* Phase,
		FPlan& Plan,
		FString& OutError)
	{
		return ValidateStage1ElapsedBudget(Stage,
			ElapsedMilliseconds(StageStartSeconds), Phase, Plan, OutError);
	}

	struct FRoot
	{
		FString Path;
		FBox Bounds = FBox(EForceInit::ForceInit);
		double GroundZCM = 0.0;
		double BodyTopCM = -DBL_MAX;
		double CrownTopCM = -DBL_MAX;
		TArray<const FABTSM73DAG5BV2Volume*> BodyVolumes;
		TArray<const FABTSM73DAG5BV2Volume*> CrownVolumes;
		TArray<int32> SourceVolumeIds;
		TArray<int32> CrownVolumeIds;
		TArray<int32> GroundSourceVolumeIds;
		TArray<int32> SourceGroundComponentIds;
		TMap<int32, int32> SourceVolumeOriginalComponentIds;
		TArray<FVerticalSupportWitness> Witnesses;
	};

	struct FDensityRecipe
	{
		int32 HorizontalUnits = 18;
		int32 VerticalUnits = 18;
		int32 RecipeId = 0;
	};

	struct FBandState
	{
		int32 BaseCourse = 0;
		TArray<bool> OccupiedCells;
		TArray<bool> ExteriorEmptyCells;
		TArray<int32> CellSourceVolumeIds;
		TArray<int32> XMemberIndices;
		TArray<int32> YMemberIndices;
		TArray<int32> PostMemberIndices;
	};

	bool SolidCoveredByBoxes(
		const FBox& Solid,
		const TArray<FBox>& AllowedBoxes,
		FVector& OutUncoveredPoint);

	bool IsSpanRole(const EABTSM73DAG5BV2VolumeRole Role)
	{
		return Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan
			|| Role == EABTSM73DAG5BV2VolumeRole::Bridge;
	}

	FString RootPath(const FString& Path)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), true);
		return Parts.Num() >= 2 ? Parts[0] + TEXT("/") + Parts[1] : Path;
	}

	FString BuildingPath(const FString& Path)
	{
		int32 Slash = INDEX_NONE;
		return Path.FindChar(TEXT('/'), Slash) ? Path.Left(Slash) : Path;
	}

	TArray<int32> MakeUniformStations(
		const int32 Minimum, const int32 Maximum, const int32 Count)
	{
		TArray<int32> Result;
		if (Count < 2 || Maximum - Minimum < Count - 1)
		{
			return Result;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Station = Minimum + FMath::RoundToInt(
				static_cast<double>(Index * (Maximum - Minimum))
				/ static_cast<double>(Count - 1));
			Result.AddUnique(Station);
		}
		return Result.Num() == Count ? Result : TArray<int32>();
	}

	int32 QMin(const double Value)
	{
		return FMath::CeilToInt(Value / static_cast<double>(BlockUnitsCM));
	}

	int32 QMax(const double Value)
	{
		return FMath::FloorToInt(Value / static_cast<double>(BlockUnitsCM));
	}

	int32 QRelativeCeil(const double Value, const double Origin)
	{
		return FMath::CeilToInt((Value - Origin) / static_cast<double>(BlockUnitsCM));
	}

	int32 QRelativeFloor(const double Value, const double Origin)
	{
		return FMath::FloorToInt((Value - Origin) / static_cast<double>(BlockUnitsCM));
	}

	FVector Position(const double X, const double Y, const double Z)
	{
		return FVector(X, Y, Z);
	}

	int64 HashText(const FString& Text)
	{
		const uint32 A = FCrc::StrCrc32(*Text);
		const uint32 B = FCrc::MemCrc32(&A, sizeof(A), 0xC3A30003u);
		return static_cast<int64>((static_cast<uint64>(A) << 32) | B);
	}

	int64 QHash(const double Value)
	{
		return FMath::RoundToInt64(Value * 1000.0);
	}

	int64 ComputeEnvelopeHash(const FABTSM73DAG5BV2GenerationResult& Silhouette)
	{
		TArray<FString> VolumeTokens;
		VolumeTokens.Reserve(Silhouette.Volumes.Num());
		for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
		{
			VolumeTokens.Add(FString::Printf(
				TEXT("V:%d:%d:%d:D=%d:P=%s:B=%lld,%lld,%lld,%lld,%lld,%lld:S=%d,%d,%d,%lld,%lld"),
				Volume.VolumeId, static_cast<int32>(Volume.Role),
				static_cast<int32>(Volume.Primitive), Volume.GrammarDepth,
				*Volume.DerivationPath,
				QHash(Volume.LocalBounds.Min.X), QHash(Volume.LocalBounds.Min.Y),
				QHash(Volume.LocalBounds.Min.Z), QHash(Volume.LocalBounds.Max.X),
				QHash(Volume.LocalBounds.Max.Y), QHash(Volume.LocalBounds.Max.Z),
				Volume.NegativeSupportVolumeId, Volume.PositiveSupportVolumeId,
				Volume.SpanAxisIndex, QHash(Volume.SpanOpeningMinCM),
				QHash(Volume.SpanOpeningMaxCM)));
		}
		// Sorting the complete tokens, rather than only VolumeId pointers,
		// keeps the identity invariant under input array reordering and stable
		// even if an invalid upstream fixture repeats an id.
		VolumeTokens.Sort();
		FString Envelope = FString::Printf(TEXT("Grammar=%lld:WFC=%lld:Result=%lld"),
			Silhouette.Summary.GrammarHash, Silhouette.Summary.WFCHash,
			Silhouette.Summary.ResultHash);
		for (const FString& Token : VolumeTokens)
		{
			Envelope += TEXT("|") + Token;
		}
		return HashText(Envelope);
	}

	double SkeletonV3OverlapLength(
		const double AMin,
		const double AMax,
		const double BMin,
		const double BMax)
	{
		return FMath::Max(0.0, FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin));
	}

	struct FRequiredHighProjectionDemand
	{
		int32 SemanticDemandId = INDEX_NONE;
		int32 TerminalSliceCourse = INDEX_NONE;
		int32 TerminalSliceComponentId = INDEX_NONE;
		int32 RequiredTopCourse = 0;
		FBox EntryBounds = FBox(EForceInit::ForceInit);
		FBox TerminalBounds = FBox(EForceInit::ForceInit);
		FBox BranchBounds = FBox(EForceInit::ForceInit);
		TArray<int32> SourceVolumeIds;
		/** Absolute course index -> permitted semantic sources on this branch. */
		TArray<TArray<int32>> CourseSourceVolumeIds;
	};

	/** Promotes one semantic terminal Body to one authoritative TowerChild
	 * demand.  The legacy course-slice terminal is retained only as diagnostic
	 * identity: two Body demands may therefore share its course/component after
	 * a Crown merge without sharing a child. */
	bool BuildSemanticChildProjectionDemands(
		const FRoot& Root,
		const int32 ComponentId,
		const int32 PodiumTopCourse,
		const FPlan& Plan,
		const TArray<FRequiredHighProjectionDemand>& LegacyDemands,
		TArray<FRequiredHighProjectionDemand>& OutDemands,
		FString& OutError)
	{
		OutDemands.Reset();
		const double PodiumTopZ = Root.GroundZCM
			+ PodiumTopCourse * static_cast<double>(BlockUnitsCM);
		for (const FSemanticTerminalDemandDiagnostic& Semantic
			: Plan.SemanticTerminalDemands)
		{
			if (Semantic.ComponentId != ComponentId)
			{
				continue;
			}
			if (!Semantic.bHasContinuousCoreFit
				|| !Semantic.ContinuousCoreFitBounds.IsValid
				|| Semantic.RequiredTopCourse < PodiumTopCourse + 2)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticChildDemandFitInvalid:Component=%d:Demand=%d:BodySource=%d:Fit=%d:Podium=%d:Top=%d"),
					ComponentId, Semantic.DemandId,
					Semantic.TerminalBodySourceVolumeId,
					Semantic.bHasContinuousCoreFit ? 1 : 0,
					PodiumTopCourse, Semantic.RequiredTopCourse);
				return false;
			}

			const FRequiredHighProjectionDemand* Legacy = nullptr;
			double BestLegacyOverlap = -1.0;
			for (const FRequiredHighProjectionDemand& Candidate : LegacyDemands)
			{
				const bool bContainsTerminalBody = Candidate.SourceVolumeIds.Contains(
					Semantic.TerminalBodySourceVolumeId);
				const double Overlap = SkeletonV3OverlapLength(
					Candidate.BranchBounds.Min.X, Candidate.BranchBounds.Max.X,
					Semantic.BodyBounds.Min.X, Semantic.BodyBounds.Max.X)
					* SkeletonV3OverlapLength(
						Candidate.BranchBounds.Min.Y, Candidate.BranchBounds.Max.Y,
						Semantic.BodyBounds.Min.Y, Semantic.BodyBounds.Max.Y);
				if (bContainsTerminalBody
					&& (Legacy == nullptr || Overlap > BestLegacyOverlap))
				{
					Legacy = &Candidate;
					BestLegacyOverlap = Overlap;
				}
			}
			if (Legacy == nullptr)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticChildLegacySliceMissing:Component=%d:Demand=%d:BodySource=%d:Legacy=%d"),
					ComponentId, Semantic.DemandId,
					Semantic.TerminalBodySourceVolumeId, LegacyDemands.Num());
				return false;
			}

			FRequiredHighProjectionDemand Demand;
			Demand.SemanticDemandId = Semantic.DemandId;
			Demand.TerminalSliceCourse = Legacy->TerminalSliceCourse;
			Demand.TerminalSliceComponentId = Legacy->TerminalSliceComponentId;
			Demand.RequiredTopCourse = Semantic.RequiredTopCourse;
			Demand.EntryBounds = Semantic.GroundProjectionBounds;
			Demand.EntryBounds.Min.Z = PodiumTopZ;
			Demand.EntryBounds.Max.Z = PodiumTopZ + BlockUnitsCM;
			Demand.TerminalBounds = Semantic.ContinuousCoreFitBounds;
			Demand.BranchBounds = Semantic.LoadBranchBounds;
			Demand.BranchBounds.Min.Z = PodiumTopZ;
			Demand.CourseSourceVolumeIds.SetNum(Demand.RequiredTopCourse);

			for (const int32 LineageNodeId : Semantic.LineageNodeIds)
			{
				if (!Plan.SemanticSupportVolumeNodes.IsValidIndex(LineageNodeId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SemanticChildLineageNodeInvalid:Demand=%d:Node=%d"),
						Semantic.DemandId, LineageNodeId);
					return false;
				}
				const FSemanticSupportVolumeNodeDiagnostic& Node =
					Plan.SemanticSupportVolumeNodes[LineageNodeId];
				if (Node.ComponentId != ComponentId || Node.bSyntheticCoupledGround
					|| Node.LocalBounds.Max.Z < PodiumTopZ + WitnessToleranceCM)
				{
					continue;
				}
				Demand.SourceVolumeIds.AddUnique(Node.SourceVolumeId);
				for (int32 Course = PodiumTopCourse;
					Course < Demand.RequiredTopCourse; ++Course)
				{
					const double SampleZ = Root.GroundZCM
						+ (Course + 0.5) * static_cast<double>(BlockUnitsCM);
					if (SampleZ >= Node.LocalBounds.Min.Z - WitnessToleranceCM
						&& SampleZ <= Node.LocalBounds.Max.Z + WitnessToleranceCM)
					{
						Demand.CourseSourceVolumeIds[Course].AddUnique(
							Node.SourceVolumeId);
					}
				}
			}
			Demand.SourceVolumeIds.Sort();
			for (int32 Course = PodiumTopCourse;
				Course < Demand.RequiredTopCourse; ++Course)
			{
				Demand.CourseSourceVolumeIds[Course].Sort();
				if (Demand.CourseSourceVolumeIds[Course].IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SemanticChildCourseLineageEmpty:Component=%d:Demand=%d:Course=%d:Top=%d"),
						ComponentId, Semantic.DemandId, Course,
						Demand.RequiredTopCourse);
					return false;
				}
			}
			if (Demand.SourceVolumeIds.IsEmpty()
				|| !Demand.EntryBounds.IsValid || !Demand.TerminalBounds.IsValid
				|| !Demand.BranchBounds.IsValid)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticChildDemandInvalid:Component=%d:Demand=%d:Sources=%d"),
					ComponentId, Semantic.DemandId, Demand.SourceVolumeIds.Num());
				return false;
			}
			OutDemands.Add(MoveTemp(Demand));
		}
		OutDemands.Sort([](const FRequiredHighProjectionDemand& A,
			const FRequiredHighProjectionDemand& B)
		{
			return A.SemanticDemandId < B.SemanticDemandId;
		});
		if (OutDemands.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SemanticChildDemandEmpty:Component=%d"), ComponentId);
			return false;
		}
		return true;
	}

	struct FProjectionSliceNode
	{
		int32 Course = INDEX_NONE;
		int32 SliceComponentId = INDEX_NONE;
		FBox Bounds = FBox(EForceInit::ForceInit);
		TArray<int32> SourceVolumeIds;
		TArray<int32> PreviousNodeIndices;
		TArray<int32> NextNodeIndices;
		bool bReachableFromPodium = false;
	};

	bool SkeletonV3XYConnected(const FBox& A, const FBox& B,
		const bool bAllowFullEdge)
	{
		const double XOverlap = SkeletonV3OverlapLength(
			A.Min.X, A.Max.X, B.Min.X, B.Max.X);
		const double YOverlap = SkeletonV3OverlapLength(
			A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
		if (XOverlap > WitnessToleranceCM && YOverlap > WitnessToleranceCM)
		{
			return true;
		}
		return bAllowFullEdge && (((FMath::Abs(A.Max.X - B.Min.X)
				<= WitnessToleranceCM
				|| FMath::Abs(B.Max.X - A.Min.X) <= WitnessToleranceCM)
				&& YOverlap > WitnessToleranceCM)
			|| ((FMath::Abs(A.Max.Y - B.Min.Y) <= WitnessToleranceCM
				|| FMath::Abs(B.Max.Y - A.Min.Y) <= WitnessToleranceCM)
				&& XOverlap > WitnessToleranceCM));
	}

	/** Builds the independent load-demand universe before any core occupies a lane.
	 * Horizontal full-edge contact joins one slice, but vertical succession requires
	 * positive XY area.  Consequently a broad podium trunk may split into several
	 * terminal demands even when it was one connected entry footprint. */
	bool BuildRequiredHighProjectionDemands(
		const FRoot& Root,
		const int32 PodiumTopCourse,
		TArray<FRequiredHighProjectionDemand>& OutDemands,
		FString& OutError)
	{
		OutDemands.Reset();
		TArray<const FABTSM73DAG5BV2Volume*> Volumes = Root.BodyVolumes;
		Volumes.Append(Root.CrownVolumes);
		Volumes.RemoveAll([](const FABTSM73DAG5BV2Volume* Volume)
		{
			return Volume == nullptr
				|| Volume->DerivationPath.StartsWith(TEXT("CoupledGround/"));
		});
		if (Volumes.IsEmpty())
		{
			OutError = TEXT("BeamC3V3TerminalDemandVolumeSetEmpty");
			return false;
		}
		Volumes.Sort([](const FABTSM73DAG5BV2Volume& A,
			const FABTSM73DAG5BV2Volume& B)
		{
			return A.VolumeId < B.VolumeId;
		});
		auto FindProjectionVolume = [&Volumes](const int32 VolumeId)
			-> const FABTSM73DAG5BV2Volume*
		{
			for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
			{
				if (Volume != nullptr && Volume->VolumeId == VolumeId)
				{
					return Volume;
				}
			}
			return nullptr;
		};
		const double TopZ = FMath::Max(Root.BodyTopCM, Root.CrownTopCM);
		const int32 TopCourse = FMath::CeilToInt(
			(TopZ - Root.GroundZCM) / BlockUnitsCM);
		if (TopCourse <= PodiumTopCourse)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3TerminalDemandHeightInvalid:Podium=%d:Top=%d"),
				PodiumTopCourse, TopCourse);
			return false;
		}

		TArray<FProjectionSliceNode> Nodes;
		TArray<TArray<int32>> NodeIndicesByCourse;
		NodeIndicesByCourse.SetNum(TopCourse - PodiumTopCourse);
		for (int32 Course = PodiumTopCourse; Course < TopCourse; ++Course)
		{
			const double SliceMinZ = Root.GroundZCM + Course * BlockUnitsCM;
			const double SliceMaxZ = SliceMinZ + BlockUnitsCM;
			const double SampleZ = (SliceMinZ + SliceMaxZ) * 0.5;
			TArray<const FABTSM73DAG5BV2Volume*> Active;
			for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
			{
				if (SampleZ >= Volume->LocalBounds.Min.Z - WitnessToleranceCM
					&& SampleZ <= Volume->LocalBounds.Max.Z + WitnessToleranceCM)
				{
					Active.Add(Volume);
				}
			}
			TArray<TArray<int32>> Adjacency;
			Adjacency.SetNum(Active.Num());
			for (int32 AIndex = 0; AIndex < Active.Num(); ++AIndex)
			{
				for (int32 BIndex = AIndex + 1; BIndex < Active.Num(); ++BIndex)
				{
					if (SkeletonV3XYConnected(Active[AIndex]->LocalBounds,
						Active[BIndex]->LocalBounds, true))
					{
						Adjacency[AIndex].Add(BIndex);
						Adjacency[BIndex].Add(AIndex);
					}
				}
			}
			TArray<bool> Visited;
			Visited.Init(false, Active.Num());
			int32 SliceComponentId = 0;
			for (int32 StartIndex = 0; StartIndex < Active.Num(); ++StartIndex)
			{
				if (Visited[StartIndex])
				{
					continue;
				}
				TArray<int32> Queue{StartIndex};
				Visited[StartIndex] = true;
				FProjectionSliceNode& Node = Nodes.AddDefaulted_GetRef();
				Node.Course = Course;
				Node.SliceComponentId = SliceComponentId++;
				for (int32 Head = 0; Head < Queue.Num(); ++Head)
				{
					const int32 ActiveIndex = Queue[Head];
					const FABTSM73DAG5BV2Volume* Volume = Active[ActiveIndex];
					Node.SourceVolumeIds.Add(Volume->VolumeId);
					FBox Clipped = Volume->LocalBounds;
					Clipped.Min.Z = FMath::Max(Clipped.Min.Z, SliceMinZ);
					Clipped.Max.Z = FMath::Min(Clipped.Max.Z, SliceMaxZ);
					Node.Bounds += Clipped;
					for (const int32 Adjacent : Adjacency[ActiveIndex])
					{
						if (!Visited[Adjacent])
						{
							Visited[Adjacent] = true;
							Queue.Add(Adjacent);
						}
					}
				}
				Node.SourceVolumeIds.Sort();
				NodeIndicesByCourse[Course - PodiumTopCourse].Add(Nodes.Num() - 1);
			}
		}

		for (int32 CourseOffset = 0;
			CourseOffset + 1 < NodeIndicesByCourse.Num(); ++CourseOffset)
		{
			for (const int32 LowerIndex : NodeIndicesByCourse[CourseOffset])
			{
				for (const int32 UpperIndex : NodeIndicesByCourse[CourseOffset + 1])
				{
					bool bHasPositiveAreaSuccessor = false;
					for (const int32 LowerSource : Nodes[LowerIndex].SourceVolumeIds)
					{
						const FABTSM73DAG5BV2Volume* LowerVolume =
							FindProjectionVolume(LowerSource);
						if (LowerVolume == nullptr)
						{
							continue;
						}
						for (const int32 UpperSource : Nodes[UpperIndex].SourceVolumeIds)
						{
							const FABTSM73DAG5BV2Volume* UpperVolume =
								FindProjectionVolume(UpperSource);
							bHasPositiveAreaSuccessor |= UpperVolume != nullptr
								&& SkeletonV3XYConnected(LowerVolume->LocalBounds,
									UpperVolume->LocalBounds, false);
							if (bHasPositiveAreaSuccessor)
							{
								break;
							}
						}
						if (bHasPositiveAreaSuccessor)
						{
							break;
						}
					}
					if (bHasPositiveAreaSuccessor)
					{
						Nodes[LowerIndex].NextNodeIndices.Add(UpperIndex);
						Nodes[UpperIndex].PreviousNodeIndices.Add(LowerIndex);
					}
				}
			}
		}
		if (NodeIndicesByCourse.IsEmpty()
			|| NodeIndicesByCourse[0].IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3TerminalDemandEntryEmpty:Podium=%d"),
				PodiumTopCourse);
			return false;
		}
		for (const int32 EntryIndex : NodeIndicesByCourse[0])
		{
			Nodes[EntryIndex].bReachableFromPodium = true;
		}
		for (int32 CourseOffset = 0;
			CourseOffset + 1 < NodeIndicesByCourse.Num(); ++CourseOffset)
		{
			for (const int32 NodeIndex : NodeIndicesByCourse[CourseOffset])
			{
				if (!Nodes[NodeIndex].bReachableFromPodium)
				{
					continue;
				}
				for (const int32 NextIndex : Nodes[NodeIndex].NextNodeIndices)
				{
					Nodes[NextIndex].bReachableFromPodium = true;
				}
			}
		}

		for (int32 TerminalIndex = 0; TerminalIndex < Nodes.Num(); ++TerminalIndex)
		{
			const FProjectionSliceNode& Terminal = Nodes[TerminalIndex];
			if (!Terminal.bReachableFromPodium
				|| !Terminal.NextNodeIndices.IsEmpty())
			{
				continue;
			}
			FRequiredHighProjectionDemand Demand;
			Demand.TerminalSliceCourse = Terminal.Course;
			Demand.TerminalSliceComponentId = Terminal.SliceComponentId;
			Demand.TerminalBounds = Terminal.Bounds;
			Demand.CourseSourceVolumeIds.SetNum(TopCourse);
			TArray<int32> Queue{TerminalIndex};
			TSet<int32> Ancestors;
			Ancestors.Add(TerminalIndex);
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const FProjectionSliceNode& Node = Nodes[Queue[Head]];
				Demand.SourceVolumeIds.Append(Node.SourceVolumeIds);
				Demand.CourseSourceVolumeIds[Node.Course].Append(Node.SourceVolumeIds);
				FBox Clipped = Node.Bounds;
				Clipped.Min.Z = FMath::Max(Clipped.Min.Z,
					Root.GroundZCM + PodiumTopCourse * BlockUnitsCM);
				Demand.BranchBounds += Clipped;
				if (Node.Course == PodiumTopCourse)
				{
					Demand.EntryBounds += Clipped;
				}
				for (const int32 PreviousIndex : Node.PreviousNodeIndices)
				{
					if (!Ancestors.Contains(PreviousIndex))
					{
						Ancestors.Add(PreviousIndex);
						Queue.Add(PreviousIndex);
					}
				}
			}
			Demand.SourceVolumeIds.Sort();
			Demand.SourceVolumeIds.SetNum(Algo::Unique(Demand.SourceVolumeIds));
			for (TArray<int32>& CourseSources : Demand.CourseSourceVolumeIds)
			{
				CourseSources.Sort();
				CourseSources.SetNum(Algo::Unique(CourseSources));
			}
			double BranchTopZ = -DBL_MAX;
			for (const int32 SourceId : Terminal.SourceVolumeIds)
			{
				if (const FABTSM73DAG5BV2Volume* Volume =
					FindProjectionVolume(SourceId))
				{
					BranchTopZ = FMath::Max(BranchTopZ, Volume->LocalBounds.Max.Z);
				}
			}
			Demand.RequiredTopCourse = FMath::FloorToInt(
				(BranchTopZ - Root.GroundZCM) / BlockUnitsCM);
			if (!Demand.EntryBounds.IsValid || !Demand.TerminalBounds.IsValid
				|| !Demand.BranchBounds.IsValid
				|| Demand.RequiredTopCourse < PodiumTopCourse + 2)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TerminalDemandInvalid:TerminalCourse=%d:RequiredTop=%d:Sources=%d"),
					Terminal.Course, Demand.RequiredTopCourse,
					Demand.SourceVolumeIds.Num());
				return false;
			}
			OutDemands.Add(MoveTemp(Demand));
		}
		OutDemands.Sort([](const FRequiredHighProjectionDemand& A,
			const FRequiredHighProjectionDemand& B)
		{
			if (!FMath::IsNearlyEqual(A.TerminalBounds.Min.Y, B.TerminalBounds.Min.Y))
			{
				return A.TerminalBounds.Min.Y < B.TerminalBounds.Min.Y;
			}
			if (!FMath::IsNearlyEqual(A.TerminalBounds.Min.X, B.TerminalBounds.Min.X))
			{
				return A.TerminalBounds.Min.X < B.TerminalBounds.Min.X;
			}
			if (A.RequiredTopCourse != B.RequiredTopCourse)
			{
				return A.RequiredTopCourse < B.RequiredTopCourse;
			}
			return A.SourceVolumeIds.IsEmpty() || B.SourceVolumeIds.IsEmpty()
				? A.SourceVolumeIds.Num() < B.SourceVolumeIds.Num()
				: A.SourceVolumeIds[0] < B.SourceVolumeIds[0];
		});
		if (OutDemands.IsEmpty() || OutDemands.Num() > 30)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3TerminalDemandCardinalityInvalid:Demands=%d"),
				OutDemands.Num());
			return false;
		}
		return true;
	}

	bool BuildSemanticSupportDemandDiagnostics(
		const FRoot& Root,
		const int32 ComponentId,
		FPlan& Plan,
		FString& OutError)
	{
		TArray<const FABTSM73DAG5BV2Volume*> Volumes = Root.BodyVolumes;
		Volumes.Append(Root.CrownVolumes);
		Volumes.RemoveAll([](const FABTSM73DAG5BV2Volume* Volume)
		{
			return Volume == nullptr;
		});
		Volumes.Sort([](const FABTSM73DAG5BV2Volume& A,
			const FABTSM73DAG5BV2Volume& B)
		{
			return A.VolumeId < B.VolumeId;
		});
		if (Volumes.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SemanticSupportVolumeSetEmpty:Component=%d"),
				ComponentId);
			return false;
		}

		const int32 FirstNodeIndex = Plan.SemanticSupportVolumeNodes.Num();
		TMap<int32, int32> NodeIdBySourceVolumeId;
		for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
		{
			FSemanticSupportVolumeNodeDiagnostic& Node =
				Plan.SemanticSupportVolumeNodes.AddDefaulted_GetRef();
			Node.NodeId = Plan.SemanticSupportVolumeNodes.Num() - 1;
			Node.ComponentId = ComponentId;
			Node.SourceVolumeId = Volume->VolumeId;
			Node.Role = Volume->Role;
			Node.Primitive = Volume->Primitive;
			Node.DerivationPath = Volume->DerivationPath;
			Node.LocalBounds = Volume->LocalBounds;
			Node.bGrounded = FMath::Abs(
				Volume->LocalBounds.Min.Z - Root.GroundZCM)
				<= WitnessToleranceCM;
			Node.bSyntheticCoupledGround =
				Volume->DerivationPath.StartsWith(TEXT("CoupledGround/"));
			Node.bSquareBody = Volume->Role != EABTSM73DAG5BV2VolumeRole::Crown
				&& Volume->Primitive == EABTSM73DAG5BV2Primitive::Box
				&& !Node.bSyntheticCoupledGround;
			NodeIdBySourceVolumeId.Add(Volume->VolumeId, Node.NodeId);
		}

		for (const FVerticalSupportWitness& Witness : Root.Witnesses)
		{
			const int32* LowerNodeId =
				NodeIdBySourceVolumeId.Find(Witness.LowerSourceVolumeId);
			const int32* UpperNodeId =
				NodeIdBySourceVolumeId.Find(Witness.UpperSourceVolumeId);
			if (LowerNodeId == nullptr || UpperNodeId == nullptr
				|| !Plan.SemanticSupportVolumeNodes.IsValidIndex(*LowerNodeId)
				|| !Plan.SemanticSupportVolumeNodes.IsValidIndex(*UpperNodeId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticSupportWitnessNodeMissing:Component=%d:Lower=%d:Upper=%d"),
					ComponentId, Witness.LowerSourceVolumeId,
					Witness.UpperSourceVolumeId);
				return false;
			}
			Plan.SemanticSupportVolumeNodes[*LowerNodeId]
				.ChildNodeIds.AddUnique(*UpperNodeId);
			Plan.SemanticSupportVolumeNodes[*UpperNodeId]
				.ParentNodeIds.AddUnique(*LowerNodeId);
		}
		int32 CoupledPodiumTopCourse = FMath::FloorToInt(
			(Root.BodyTopCM - Root.GroundZCM)
			/ static_cast<double>(BlockUnitsCM));
		bool bHasDerivedCoupledPodium = false;
		for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
		{
			if (Volume != nullptr
				&& Root.GroundSourceVolumeIds.Contains(Volume->VolumeId)
				&& Volume->DerivationPath.StartsWith(TEXT("CoupledGround/")))
			{
				bHasDerivedCoupledPodium = true;
				CoupledPodiumTopCourse = FMath::Min(CoupledPodiumTopCourse,
					FMath::FloorToInt((Volume->LocalBounds.Max.Z - Root.GroundZCM)
						/ static_cast<double>(BlockUnitsCM)));
			}
		}
		const int32 BodyTopCourse = FMath::FloorToInt(
			(Root.BodyTopCM - Root.GroundZCM)
			/ static_cast<double>(BlockUnitsCM));
		const bool bUsesTowerChildHierarchy = bHasDerivedCoupledPodium
			&& CoupledPodiumTopCourse >= 2
			&& CoupledPodiumTopCourse + 2 <= BodyTopCourse;

		for (int32 NodeIndex = FirstNodeIndex;
			NodeIndex < Plan.SemanticSupportVolumeNodes.Num(); ++NodeIndex)
		{
			FSemanticSupportVolumeNodeDiagnostic& Node =
				Plan.SemanticSupportVolumeNodes[NodeIndex];
			Node.ParentNodeIds.Sort();
			Node.ChildNodeIds.Sort();
			Node.bGraphTerminal = Node.ChildNodeIds.IsEmpty();
			const int32 NodeTopCourse = FMath::FloorToInt(
				(Node.LocalBounds.Max.Z - Root.GroundZCM)
				/ static_cast<double>(BlockUnitsCM));
			Node.bTowerChildLoadLeaf = Node.bGraphTerminal
				&& bUsesTowerChildHierarchy
				&& NodeTopCourse >= CoupledPodiumTopCourse + 2;
			const bool bBodyNode = Node.Role != EABTSM73DAG5BV2VolumeRole::Crown
				&& !Node.bSyntheticCoupledGround;
			Node.bTerminalBody = bBodyNode
				&& !Node.ChildNodeIds.ContainsByPredicate(
					[&Plan](const int32 ChildNodeId)
					{
						return Plan.SemanticSupportVolumeNodes.IsValidIndex(ChildNodeId)
							&& Plan.SemanticSupportVolumeNodes[ChildNodeId].Role
								!= EABTSM73DAG5BV2VolumeRole::Crown
							&& !Plan.SemanticSupportVolumeNodes[ChildNodeId]
								.bSyntheticCoupledGround;
					});
			if (Node.bSyntheticCoupledGround)
			{
				Node.DemandClassificationReason = TEXT("SyntheticCoupledGround");
			}
			else if (Node.Role == EABTSM73DAG5BV2VolumeRole::Crown)
			{
				Node.DemandClassificationReason = Node.bGraphTerminal
					? (Node.bTowerChildLoadLeaf
						? TEXT("TowerChildCrownLoadLeaf")
						: TEXT("MainCarriedCrownLoadLeaf"))
					: TEXT("InteriorCrown");
			}
			else if (!Node.bTerminalBody)
			{
				Node.DemandClassificationReason = TEXT("BodyContinuation");
			}
		}

		auto CollectTerminalLoadLeaves = [&Plan](const int32 StartNodeId)
		{
			TArray<int32> Leaves;
			TArray<int32> Queue{StartNodeId};
			TSet<int32> Visited;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 NodeId = Queue[Head];
				if (Visited.Contains(NodeId)
					|| !Plan.SemanticSupportVolumeNodes.IsValidIndex(NodeId))
				{
					continue;
				}
				Visited.Add(NodeId);
				const FSemanticSupportVolumeNodeDiagnostic& Node =
					Plan.SemanticSupportVolumeNodes[NodeId];
				if (Node.ChildNodeIds.IsEmpty())
				{
					Leaves.Add(NodeId);
					continue;
				}
				Queue.Append(Node.ChildNodeIds);
			}
			Leaves.Sort();
			return Leaves;
		};
		auto CollectTowerChildLoadLeaves = [&Plan, &CollectTerminalLoadLeaves](
			const int32 StartNodeId)
		{
			TArray<int32> Leaves = CollectTerminalLoadLeaves(StartNodeId);
			Leaves.RemoveAll([&Plan](const int32 NodeId)
			{
				return !Plan.SemanticSupportVolumeNodes.IsValidIndex(NodeId)
					|| !Plan.SemanticSupportVolumeNodes[NodeId]
						.bTowerChildLoadLeaf;
			});
			return Leaves;
		};
		for (int32 NodeIndex = FirstNodeIndex;
			NodeIndex < Plan.SemanticSupportVolumeNodes.Num(); ++NodeIndex)
		{
			FSemanticSupportVolumeNodeDiagnostic& Node =
				Plan.SemanticSupportVolumeNodes[NodeIndex];
			if (!Node.bTerminalBody)
			{
				continue;
			}
			Node.TerminalLoadBranchCount =
				CollectTowerChildLoadLeaves(Node.NodeId).Num();
			Node.DemandClassificationReason = Node.TerminalLoadBranchCount > 0
				? FString::Printf(TEXT("TerminalBodyTowerChildLoadBranches=%d"),
					Node.TerminalLoadBranchCount)
				: TEXT("TerminalBodyMainCarried");
		}

		TMap<int32, TArray<int32>> WitnessIndicesByContactCourse;
		for (int32 WitnessIndex = 0;
			WitnessIndex < Root.Witnesses.Num(); ++WitnessIndex)
		{
			const int32 ContactCourse = FMath::RoundToInt(
				(Root.Witnesses[WitnessIndex].ContactZCM - Root.GroundZCM)
				/ static_cast<double>(BlockUnitsCM));
			WitnessIndicesByContactCourse.FindOrAdd(ContactCourse)
				.Add(WitnessIndex);
		}
		TArray<int32> ContactCourses;
		WitnessIndicesByContactCourse.GetKeys(ContactCourses);
		ContactCourses.Sort();
		for (const int32 ContactCourse : ContactCourses)
		{
			const TArray<int32>& WitnessIndices =
				WitnessIndicesByContactCourse.FindChecked(ContactCourse);
			TArray<bool> Visited;
			Visited.Init(false, WitnessIndices.Num());
			for (int32 Start = 0; Start < WitnessIndices.Num(); ++Start)
			{
				if (Visited[Start])
				{
					continue;
				}
				TArray<int32> Queue{Start};
				Visited[Start] = true;
				TSet<int32> LowerSourceIds;
				TSet<int32> UpperSourceIds;
				for (int32 Head = 0; Head < Queue.Num(); ++Head)
				{
					const FVerticalSupportWitness& Current =
						Root.Witnesses[WitnessIndices[Queue[Head]]];
					LowerSourceIds.Add(Current.LowerSourceVolumeId);
					UpperSourceIds.Add(Current.UpperSourceVolumeId);
					for (int32 Candidate = 0;
						Candidate < WitnessIndices.Num(); ++Candidate)
					{
						if (Visited[Candidate])
						{
							continue;
						}
						const FVerticalSupportWitness& Other =
							Root.Witnesses[WitnessIndices[Candidate]];
						if (LowerSourceIds.Contains(Other.LowerSourceVolumeId)
							|| UpperSourceIds.Contains(Other.UpperSourceVolumeId)
							|| LowerSourceIds.Contains(Other.UpperSourceVolumeId)
							|| UpperSourceIds.Contains(Other.LowerSourceVolumeId))
						{
							Visited[Candidate] = true;
							Queue.Add(Candidate);
						}
					}
				}
				FSemanticSupportMergeLedgerDiagnostic& Ledger =
					Plan.SemanticSupportMergeLedger.AddDefaulted_GetRef();
				Ledger.LedgerId = Plan.SemanticSupportMergeLedger.Num() - 1;
				Ledger.ComponentId = ComponentId;
				Ledger.ContactCourse = ContactCourse;
				Ledger.ContactZCM = Root.GroundZCM
					+ ContactCourse * static_cast<double>(BlockUnitsCM);
				for (const int32 SourceId : LowerSourceIds)
				{
					Ledger.LowerNodeIds.Add(NodeIdBySourceVolumeId.FindChecked(SourceId));
				}
				for (const int32 SourceId : UpperSourceIds)
				{
					Ledger.UpperNodeIds.Add(NodeIdBySourceVolumeId.FindChecked(SourceId));
				}
				Ledger.LowerNodeIds.Sort();
				Ledger.UpperNodeIds.Sort();
				Ledger.bSplit = Ledger.UpperNodeIds.Num() > 1;
				Ledger.bMerge = Ledger.LowerNodeIds.Num() > 1;
			}
		}

		const int32 MinimumXUnit = QMin(Root.Bounds.Min.X + BlockUnitsCM * 0.5);
		const int32 MaximumXUnit = QMax(Root.Bounds.Max.X - BlockUnitsCM * 0.5);
		const int32 MinimumYUnit = QMin(Root.Bounds.Min.Y + BlockUnitsCM * 0.5);
		const int32 MaximumYUnit = QMax(Root.Bounds.Max.Y - BlockUnitsCM * 0.5);
		const int32 SizeX = MaximumXUnit - MinimumXUnit + 1;
		const int32 SizeY = MaximumYUnit - MinimumYUnit + 1;
		const int32 TopCourse = FMath::CeilToInt(
			(Root.Bounds.Max.Z - Root.GroundZCM)
			/ static_cast<double>(BlockUnitsCM));
		if (SizeX <= 0 || SizeY <= 0 || TopCourse <= 0)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SemanticSupportOccupancyGridInvalid:Component=%d:Size=%dx%d:Top=%d"),
				ComponentId, SizeX, SizeY, TopCourse);
			return false;
		}
		for (int32 Course = 0; Course < TopCourse; ++Course)
		{
			const double Z = Root.GroundZCM
				+ (Course + 0.5) * static_cast<double>(BlockUnitsCM);
			FSemanticSupportCourseOccupancyDiagnostic Occupancy;
			Occupancy.ComponentId = ComponentId;
			Occupancy.CourseIndex = Course;
			Occupancy.MinimumXUnit = MinimumXUnit;
			Occupancy.MinimumYUnit = MinimumYUnit;
			Occupancy.SizeX = SizeX;
			Occupancy.SizeY = SizeY;
			Occupancy.OccupiedWords.SetNumZeroed((SizeX * SizeY + 63) / 64);
			for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
			{
				if (Z < Volume->LocalBounds.Min.Z - WitnessToleranceCM
					|| Z > Volume->LocalBounds.Max.Z + WitnessToleranceCM)
				{
					continue;
				}
				bool bVolumeOccupied = false;
				for (int32 Y = 0; Y < SizeY; ++Y)
				{
					const double SampleY = (MinimumYUnit + Y)
						* static_cast<double>(BlockUnitsCM);
					for (int32 X = 0; X < SizeX; ++X)
					{
						const double SampleX = (MinimumXUnit + X)
							* static_cast<double>(BlockUnitsCM);
						if (SampleX < Volume->LocalBounds.Min.X - GeometryToleranceCM
							|| SampleX > Volume->LocalBounds.Max.X + GeometryToleranceCM
							|| SampleY < Volume->LocalBounds.Min.Y - GeometryToleranceCM
							|| SampleY > Volume->LocalBounds.Max.Y + GeometryToleranceCM)
						{
							continue;
						}
						const int32 BitIndex = Y * SizeX + X;
						const uint64 Mask = uint64(1) << (BitIndex & 63);
						uint64& Word = Occupancy.OccupiedWords[BitIndex >> 6];
						if ((Word & Mask) == 0)
						{
							Word |= Mask;
							++Occupancy.OccupiedCellCount;
							const FVector CellCenter(SampleX, SampleY, Z);
							Occupancy.OccupiedBounds += FBox(
								CellCenter - FVector(BlockUnitsCM * 0.5),
								CellCenter + FVector(BlockUnitsCM * 0.5));
						}
						bVolumeOccupied = true;
					}
				}
				if (bVolumeOccupied)
				{
					Occupancy.SourceVolumeIds.Add(Volume->VolumeId);
				}
			}
			if (Occupancy.OccupiedCellCount > 0)
			{
				Occupancy.SourceVolumeIds.Sort();
				Plan.SemanticSupportCourseOccupancies.Add(MoveTemp(Occupancy));
			}
		}

		const int32 FirstDemandIndex = Plan.SemanticTerminalDemands.Num();
		for (int32 NodeIndex = FirstNodeIndex;
			NodeIndex < Plan.SemanticSupportVolumeNodes.Num(); ++NodeIndex)
		{
			const FSemanticSupportVolumeNodeDiagnostic& TerminalBody =
				Plan.SemanticSupportVolumeNodes[NodeIndex];
			if (!TerminalBody.bTerminalBody)
			{
				continue;
			}
			const TArray<int32> TerminalLoadNodeIds =
				CollectTowerChildLoadLeaves(TerminalBody.NodeId);
			if (TerminalLoadNodeIds.IsEmpty())
			{
				continue;
			}
			for (const int32 TerminalLoadNodeId : TerminalLoadNodeIds)
			{
				if (!Plan.SemanticSupportVolumeNodes.IsValidIndex(TerminalLoadNodeId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SemanticTerminalLoadNodeInvalid:Component=%d:BodyNode=%d:LoadNode=%d"),
						ComponentId, TerminalBody.NodeId, TerminalLoadNodeId);
					return false;
				}
				const FSemanticSupportVolumeNodeDiagnostic& TerminalLoad =
					Plan.SemanticSupportVolumeNodes[TerminalLoadNodeId];
				FSemanticTerminalDemandDiagnostic& Demand =
					Plan.SemanticTerminalDemands.AddDefaulted_GetRef();
				Demand.DemandId = Plan.SemanticTerminalDemands.Num() - 1;
				Demand.ComponentId = ComponentId;
				Demand.TerminalBodyNodeId = TerminalBody.NodeId;
				Demand.TerminalBodySourceVolumeId = TerminalBody.SourceVolumeId;
				Demand.TerminalLoadNodeId = TerminalLoad.NodeId;
				Demand.TerminalLoadSourceVolumeId = TerminalLoad.SourceVolumeId;
				Demand.BodyBounds = TerminalBody.LocalBounds;
				Demand.TerminalLoadBounds = TerminalLoad.LocalBounds;
				Demand.GroundProjectionBounds = FBox(
					FVector(TerminalBody.LocalBounds.Min.X,
						TerminalBody.LocalBounds.Min.Y, Root.GroundZCM),
					FVector(TerminalBody.LocalBounds.Max.X,
						TerminalBody.LocalBounds.Max.Y,
						Root.GroundZCM + BlockUnitsCM));

				TSet<int32> LineageNodeIds;
				TArray<int32> AncestorQueue{TerminalBody.NodeId};
				for (int32 Head = 0; Head < AncestorQueue.Num(); ++Head)
				{
					const int32 CurrentId = AncestorQueue[Head];
					if (LineageNodeIds.Contains(CurrentId))
					{
						continue;
					}
					LineageNodeIds.Add(CurrentId);
					const FSemanticSupportVolumeNodeDiagnostic& Current =
						Plan.SemanticSupportVolumeNodes[CurrentId];
					AncestorQueue.Append(Current.ParentNodeIds);
					if (Current.bGrounded)
					{
						Demand.GroundSourceVolumeIds.AddUnique(Current.SourceVolumeId);
					}
				}

				TSet<int32> DescendantsFromBody;
				TArray<int32> DescendantQueue{TerminalBody.NodeId};
				for (int32 Head = 0; Head < DescendantQueue.Num(); ++Head)
				{
					const int32 CurrentId = DescendantQueue[Head];
					if (DescendantsFromBody.Contains(CurrentId))
					{
						continue;
					}
					DescendantsFromBody.Add(CurrentId);
					DescendantQueue.Append(
						Plan.SemanticSupportVolumeNodes[CurrentId].ChildNodeIds);
				}
				TSet<int32> AncestorsOfLoad;
				TArray<int32> LoadAncestorQueue{TerminalLoadNodeId};
				for (int32 Head = 0; Head < LoadAncestorQueue.Num(); ++Head)
				{
					const int32 CurrentId = LoadAncestorQueue[Head];
					if (AncestorsOfLoad.Contains(CurrentId))
					{
						continue;
					}
					AncestorsOfLoad.Add(CurrentId);
					LoadAncestorQueue.Append(
						Plan.SemanticSupportVolumeNodes[CurrentId].ParentNodeIds);
				}
				for (const int32 CurrentId : DescendantsFromBody)
				{
					if (!AncestorsOfLoad.Contains(CurrentId))
					{
						continue;
					}
					LineageNodeIds.Add(CurrentId);
					const FSemanticSupportVolumeNodeDiagnostic& Current =
						Plan.SemanticSupportVolumeNodes[CurrentId];
					Demand.LoadBranchBounds += Current.LocalBounds;
					if (Current.Role == EABTSM73DAG5BV2VolumeRole::Crown)
					{
						Demand.CrownSourceVolumeIds.AddUnique(Current.SourceVolumeId);
					}
				}
				for (const int32 LineageNodeId : LineageNodeIds)
				{
					Demand.LineageNodeIds.Add(LineageNodeId);
				}
				Demand.LineageNodeIds.Sort();
				Demand.CrownSourceVolumeIds.Sort();
				Demand.GroundSourceVolumeIds.Sort();
				Demand.RequiredTopCourse = FMath::FloorToInt(
					(Demand.LoadBranchBounds.Max.Z - Root.GroundZCM)
					/ static_cast<double>(BlockUnitsCM));

				const int32 CandidateMinimumX = QMin(
					TerminalBody.LocalBounds.Min.X + BlockUnitsCM * 0.5);
				const int32 CandidateMaximumX = QMax(
					TerminalBody.LocalBounds.Max.X - BlockUnitsCM * 0.5);
				const int32 CandidateMinimumY = QMin(
					TerminalBody.LocalBounds.Min.Y + BlockUnitsCM * 0.5);
				const int32 CandidateMaximumY = QMax(
					TerminalBody.LocalBounds.Max.Y - BlockUnitsCM * 0.5);
				FBox FitXY(EForceInit::ForceInit);
				for (int32 Y = CandidateMinimumY; Y <= CandidateMaximumY; ++Y)
				{
					for (int32 X = CandidateMinimumX; X <= CandidateMaximumX; ++X)
					{
						bool bContinuous = true;
						for (int32 Course = 0;
							Course < Demand.RequiredTopCourse; ++Course)
						{
							const FVector Sample(
								X * static_cast<double>(BlockUnitsCM),
								Y * static_cast<double>(BlockUnitsCM),
								Root.GroundZCM
									+ (Course + 0.5) * BlockUnitsCM);
							bContinuous = Demand.LineageNodeIds.ContainsByPredicate(
								[&Plan, &Sample](const int32 LineageNodeId)
								{
									return Plan.SemanticSupportVolumeNodes
										.IsValidIndex(LineageNodeId)
										&& Plan.SemanticSupportVolumeNodes[LineageNodeId]
											.LocalBounds.IsInsideOrOn(Sample);
								});
							if (!bContinuous)
							{
								break;
							}
						}
						if (bContinuous)
						{
							const FVector CellCenter(
								X * static_cast<double>(BlockUnitsCM),
								Y * static_cast<double>(BlockUnitsCM), 0.0);
							FitXY += FBox(
								CellCenter - FVector(BlockUnitsCM * 0.5,
									BlockUnitsCM * 0.5, 0.0),
								CellCenter + FVector(BlockUnitsCM * 0.5,
									BlockUnitsCM * 0.5, 0.0));
						}
					}
				}
				Demand.bHasContinuousCoreFit = FitXY.IsValid != 0;
				if (FitXY.IsValid)
				{
					Demand.ContinuousCoreFitBounds = FBox(
						FVector(FitXY.Min.X, FitXY.Min.Y, Root.GroundZCM),
						FVector(FitXY.Max.X, FitXY.Max.Y,
							Root.GroundZCM
								+ Demand.RequiredTopCourse * BlockUnitsCM));
				}
			}
		}

		for (int32 AIndex = FirstDemandIndex;
			AIndex < Plan.SemanticTerminalDemands.Num(); ++AIndex)
		{
			for (int32 BIndex = AIndex + 1;
				BIndex < Plan.SemanticTerminalDemands.Num(); ++BIndex)
			{
				FSemanticTerminalDemandDiagnostic& A =
					Plan.SemanticTerminalDemands[AIndex];
				FSemanticTerminalDemandDiagnostic& B =
					Plan.SemanticTerminalDemands[BIndex];
				if (B.ComponentId != ComponentId)
				{
					continue;
				}
				if (SkeletonV3XYConnected(A.GroundProjectionBounds,
					B.GroundProjectionBounds, true))
				{
					A.AdjacentDemandIds.Add(B.DemandId);
					B.AdjacentDemandIds.Add(A.DemandId);
				}
				const bool bSharesCrown =
					A.CrownSourceVolumeIds.ContainsByPredicate(
						[&B](const int32 SourceId)
						{
							return B.CrownSourceVolumeIds.Contains(SourceId);
						});
				A.bSharesMergedCrown |= bSharesCrown;
				B.bSharesMergedCrown |= bSharesCrown;
			}
			Plan.SemanticTerminalDemands[AIndex].AdjacentDemandIds.Sort();
		}
		if (Plan.SemanticTerminalDemands.Num() == FirstDemandIndex
			&& bUsesTowerChildHierarchy)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SemanticTerminalDemandEmpty:Component=%d"),
				ComponentId);
			return false;
		}
		return true;
	}

	bool FinalizeSemanticSupportDemandDiagnostics(
		FPlan& Plan,
		FString& OutError)
	{
		Plan.Summary.SemanticSupportNodeCount =
			Plan.SemanticSupportVolumeNodes.Num();
		Plan.Summary.SemanticSupportLedgerCount =
			Plan.SemanticSupportMergeLedger.Num();
		Plan.Summary.SemanticSupportSplitCount = 0;
		Plan.Summary.SemanticSupportMergeCount = 0;
		for (const FSemanticSupportMergeLedgerDiagnostic& Ledger
			: Plan.SemanticSupportMergeLedger)
		{
			Plan.Summary.SemanticSupportSplitCount += Ledger.bSplit ? 1 : 0;
			Plan.Summary.SemanticSupportMergeCount += Ledger.bMerge ? 1 : 0;
		}
		Plan.Summary.SemanticSupportCourseCount =
			Plan.SemanticSupportCourseOccupancies.Num();
		Plan.Summary.SemanticTerminalLoadBranchCount = 0;
		Plan.Summary.MultiBranchTerminalBodyCount = 0;
		Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount = 0;
		Plan.Summary.SemanticTerminalDemandCount =
			Plan.SemanticTerminalDemands.Num();
		Plan.Summary.SemanticTerminalDemandWithoutContinuousFitCount = 0;

		FString Canonical;
		for (const FSemanticSupportVolumeNodeDiagnostic& Node
			: Plan.SemanticSupportVolumeNodes)
		{
			if (Node.NodeId == INDEX_NONE || Node.ComponentId == INDEX_NONE
				|| !Node.LocalBounds.IsValid)
			{
				OutError = TEXT("BeamC3V3SemanticSupportNodeInvalid");
				return false;
			}
			if (Node.bTerminalBody)
			{
				Plan.Summary.SemanticTerminalLoadBranchCount +=
					Node.TerminalLoadBranchCount;
				Plan.Summary.MultiBranchTerminalBodyCount +=
					Node.TerminalLoadBranchCount > 1 ? 1 : 0;
			}
			Canonical += FString::Printf(
				TEXT("|N:%d:C=%d:V=%d:R=%d:P=%d:G=%d:L=%d:TL=%d:T=%d:LB=%d:B=%lld,%lld,%lld,%lld,%lld,%lld"),
				Node.NodeId, Node.ComponentId, Node.SourceVolumeId,
				static_cast<int32>(Node.Role), static_cast<int32>(Node.Primitive),
				Node.bGrounded ? 1 : 0, Node.bGraphTerminal ? 1 : 0,
				Node.bTowerChildLoadLeaf ? 1 : 0,
				Node.bTerminalBody ? 1 : 0, Node.TerminalLoadBranchCount,
				QHash(Node.LocalBounds.Min.X), QHash(Node.LocalBounds.Min.Y),
				QHash(Node.LocalBounds.Min.Z), QHash(Node.LocalBounds.Max.X),
				QHash(Node.LocalBounds.Max.Y), QHash(Node.LocalBounds.Max.Z));
			for (const int32 Child : Node.ChildNodeIds)
			{
				if (!Plan.SemanticSupportVolumeNodes.IsValidIndex(Child))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SemanticSupportChildInvalid:Node=%d:Child=%d"),
						Node.NodeId, Child);
					return false;
				}
				Canonical += FString::Printf(TEXT(":E%d"), Child);
			}
		}
		for (const FSemanticSupportMergeLedgerDiagnostic& Ledger
			: Plan.SemanticSupportMergeLedger)
		{
			Canonical += FString::Printf(TEXT("|L:%d:C=%d:Q=%d:S=%d:M=%d"),
				Ledger.LedgerId, Ledger.ComponentId, Ledger.ContactCourse,
				Ledger.bSplit ? 1 : 0, Ledger.bMerge ? 1 : 0);
			for (const int32 Lower : Ledger.LowerNodeIds)
			{
				Canonical += FString::Printf(TEXT(":D%d"), Lower);
			}
			for (const int32 Upper : Ledger.UpperNodeIds)
			{
				Canonical += FString::Printf(TEXT(":U%d"), Upper);
			}
		}
		for (const FSemanticSupportCourseOccupancyDiagnostic& Occupancy
			: Plan.SemanticSupportCourseOccupancies)
		{
			const int32 ExpectedWords =
				(Occupancy.SizeX * Occupancy.SizeY + 63) / 64;
			if (Occupancy.SizeX <= 0 || Occupancy.SizeY <= 0
				|| Occupancy.OccupiedCellCount <= 0
				|| Occupancy.OccupiedWords.Num() != ExpectedWords)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticSupportOccupancyInvalid:Component=%d:Course=%d:Size=%dx%d:Cells=%d:Words=%d/%d"),
					Occupancy.ComponentId, Occupancy.CourseIndex,
					Occupancy.SizeX, Occupancy.SizeY,
					Occupancy.OccupiedCellCount,
					Occupancy.OccupiedWords.Num(), ExpectedWords);
				return false;
			}
			Canonical += FString::Printf(TEXT("|O:C=%d:Q=%d:X=%d:Y=%d:N=%d"),
				Occupancy.ComponentId, Occupancy.CourseIndex,
				Occupancy.MinimumXUnit, Occupancy.MinimumYUnit,
				Occupancy.OccupiedCellCount);
			for (const uint64 Word : Occupancy.OccupiedWords)
			{
				Canonical += FString::Printf(TEXT(":%016llx"), Word);
			}
		}
		TSet<uint64> ExpectedTerminalLoadBranchKeys;
		for (const FSemanticSupportVolumeNodeDiagnostic& TerminalBody
			: Plan.SemanticSupportVolumeNodes)
		{
			if (!TerminalBody.bTerminalBody)
			{
				continue;
			}
			TArray<int32> Queue{TerminalBody.NodeId};
			TSet<int32> Visited;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 NodeId = Queue[Head];
				if (Visited.Contains(NodeId)
					|| !Plan.SemanticSupportVolumeNodes.IsValidIndex(NodeId))
				{
					continue;
				}
				Visited.Add(NodeId);
				const FSemanticSupportVolumeNodeDiagnostic& Node =
					Plan.SemanticSupportVolumeNodes[NodeId];
				if (Node.ChildNodeIds.IsEmpty())
				{
					if (Node.bTowerChildLoadLeaf)
					{
						ExpectedTerminalLoadBranchKeys.Add(
							(static_cast<uint64>(static_cast<uint32>(TerminalBody.NodeId))
								<< 32)
							| static_cast<uint32>(NodeId));
					}
				}
				else
				{
					Queue.Append(Node.ChildNodeIds);
				}
			}
		}
		TSet<uint64> RepresentedTerminalLoadBranchKeys;
		for (const FSemanticTerminalDemandDiagnostic& Demand
			: Plan.SemanticTerminalDemands)
		{
			if (!Plan.SemanticSupportVolumeNodes.IsValidIndex(
					Demand.TerminalBodyNodeId)
				|| !Plan.SemanticSupportVolumeNodes.IsValidIndex(
					Demand.TerminalLoadNodeId)
				|| Demand.RequiredTopCourse <= 0
				|| !Demand.BodyBounds.IsValid
				|| !Demand.TerminalLoadBounds.IsValid
				|| !Demand.GroundProjectionBounds.IsValid
				|| !Demand.LoadBranchBounds.IsValid)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticTerminalDemandInvalid:Demand=%d:BodyNode=%d:Top=%d"),
					Demand.DemandId, Demand.TerminalBodyNodeId,
					Demand.RequiredTopCourse);
				return false;
			}
			const uint64 BranchKey =
				(static_cast<uint64>(static_cast<uint32>(Demand.TerminalBodyNodeId))
					<< 32)
				| static_cast<uint32>(Demand.TerminalLoadNodeId);
			if (!ExpectedTerminalLoadBranchKeys.Contains(BranchKey)
				|| RepresentedTerminalLoadBranchKeys.Contains(BranchKey))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SemanticTerminalLoadBranchIdentityInvalid:Demand=%d:BodyNode=%d:LoadNode=%d:Expected=%d:Duplicate=%d"),
					Demand.DemandId, Demand.TerminalBodyNodeId,
					Demand.TerminalLoadNodeId,
					ExpectedTerminalLoadBranchKeys.Contains(BranchKey) ? 1 : 0,
					RepresentedTerminalLoadBranchKeys.Contains(BranchKey) ? 1 : 0);
				return false;
			}
			RepresentedTerminalLoadBranchKeys.Add(BranchKey);
			Plan.Summary.SemanticTerminalDemandWithoutContinuousFitCount +=
				Demand.bHasContinuousCoreFit ? 0 : 1;
			Canonical += FString::Printf(
				TEXT("|T:%d:C=%d:B=%d:V=%d:L=%d:LV=%d:TOP=%d:FIT=%d:MERGED=%d:P=%lld,%lld,%lld,%lld"),
				Demand.DemandId, Demand.ComponentId,
				Demand.TerminalBodyNodeId,
				Demand.TerminalBodySourceVolumeId,
				Demand.TerminalLoadNodeId,
				Demand.TerminalLoadSourceVolumeId,
				Demand.RequiredTopCourse,
				Demand.bHasContinuousCoreFit ? 1 : 0,
				Demand.bSharesMergedCrown ? 1 : 0,
				QHash(Demand.GroundProjectionBounds.Min.X),
				QHash(Demand.GroundProjectionBounds.Min.Y),
				QHash(Demand.GroundProjectionBounds.Max.X),
				QHash(Demand.GroundProjectionBounds.Max.Y));
			for (const int32 NodeId : Demand.LineageNodeIds)
			{
				Canonical += FString::Printf(TEXT(":N%d"), NodeId);
			}
		}
		Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount =
			ExpectedTerminalLoadBranchKeys.Num()
			- RepresentedTerminalLoadBranchKeys.Num();
		if (Plan.Summary.SemanticSupportNodeCount == 0
			|| Plan.Summary.SemanticSupportCourseCount == 0
			|| Plan.Summary.SemanticTerminalLoadBranchCount
				!= Plan.Summary.SemanticTerminalDemandCount
			|| Plan.Summary.UnrepresentedSemanticTerminalLoadBranchCount != 0)
		{
			OutError = TEXT("BeamC3V3SemanticSupportDiagnosticsIncomplete");
			return false;
		}
		Plan.Summary.SemanticSupportDemandHash = HashText(Canonical);
		return Plan.Summary.SemanticSupportDemandHash != 0;
	}

	bool SkeletonV3SupportProvinceWordContains(
		const TArray<uint64>& Words, const int32 BitIndex)
	{
		return BitIndex >= 0 && Words.IsValidIndex(BitIndex >> 6)
			&& (Words[BitIndex >> 6] & (uint64(1) << (BitIndex & 63))) != 0;
	}

	bool BuildSupportProvinceDiagnostics(FPlan& Plan, FString& OutError)
	{
		Plan.SupportProvinces.Reset();
		Plan.Summary.SupportProvinceCount = 0;
		Plan.Summary.MultiDemandSupportProvinceCount = 0;
		Plan.Summary.SupportProvinceGroundCellCount = 0;
		Plan.Summary.SupportProvinceBoundaryCount = 0;
		Plan.Summary.SupportProvinceTieBreakCellCount = 0;
		Plan.Summary.SupportProvinceNearestSeedFallbackCount = 0;
		Plan.Summary.BoundSupportProvinceCount = 0;
		Plan.Summary.DistinctProvinceGroundCoreCount = 0;
		Plan.Summary.SupportProvinceHash = 0;
		Plan.Summary.SupportProvinceMainBindingHash = 0;
		for (FSemanticTerminalDemandDiagnostic& Demand : Plan.SemanticTerminalDemands)
		{
			Demand.SupportProvinceId = INDEX_NONE;
		}
		if (Plan.SemanticTerminalDemands.IsEmpty())
		{
			Plan.Summary.SupportProvinceHash =
				HashText(TEXT("NoTowerChildSupportProvinces"));
			return Plan.Summary.SupportProvinceHash != 0;
		}

		TArray<int32> ComponentIds;
		for (const FSemanticTerminalDemandDiagnostic& Demand : Plan.SemanticTerminalDemands)
		{
			ComponentIds.AddUnique(Demand.ComponentId);
		}
		ComponentIds.Sort();
		for (const int32 ComponentId : ComponentIds)
		{
			const FSemanticSupportCourseOccupancyDiagnostic* GroundOccupancy =
				Plan.SemanticSupportCourseOccupancies.FindByPredicate(
					[ComponentId](const FSemanticSupportCourseOccupancyDiagnostic& Occupancy)
					{
						return Occupancy.ComponentId == ComponentId
							&& Occupancy.CourseIndex == 0;
					});
			if (GroundOccupancy == nullptr)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportProvinceGroundOccupancyMissing:Component=%d"),
					ComponentId);
				return false;
			}
			TArray<int32> DemandIndices;
			for (int32 DemandIndex = 0;
				DemandIndex < Plan.SemanticTerminalDemands.Num(); ++DemandIndex)
			{
				if (Plan.SemanticTerminalDemands[DemandIndex].ComponentId == ComponentId)
				{
					DemandIndices.Add(DemandIndex);
				}
			}
			DemandIndices.Sort([&Plan](const int32 A, const int32 B)
			{
				return Plan.SemanticTerminalDemands[A].DemandId
					< Plan.SemanticTerminalDemands[B].DemandId;
			});
			if (DemandIndices.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportProvinceDemandSetEmpty:Component=%d"),
					ComponentId);
				return false;
			}

			TArray<int32> Parent;
			Parent.SetNum(DemandIndices.Num());
			for (int32 Index = 0; Index < Parent.Num(); ++Index)
			{
				Parent[Index] = Index;
			}
			auto FindRoot = [&Parent](int32 Index)
			{
				while (Parent[Index] != Index)
				{
					Parent[Index] = Parent[Parent[Index]];
					Index = Parent[Index];
				}
				return Index;
			};
			for (int32 A = 0; A < DemandIndices.Num(); ++A)
			{
				const FBox& ABounds =
					Plan.SemanticTerminalDemands[DemandIndices[A]].GroundProjectionBounds;
				for (int32 B = A + 1; B < DemandIndices.Num(); ++B)
				{
					const FBox& BBounds = Plan.SemanticTerminalDemands[
						DemandIndices[B]].GroundProjectionBounds;
					const double XOverlap = FMath::Min(ABounds.Max.X, BBounds.Max.X)
						- FMath::Max(ABounds.Min.X, BBounds.Min.X);
					const double YOverlap = FMath::Min(ABounds.Max.Y, BBounds.Max.Y)
						- FMath::Max(ABounds.Min.Y, BBounds.Min.Y);
					if (XOverlap > GeometryToleranceCM && YOverlap > GeometryToleranceCM)
					{
						const int32 ARoot = FindRoot(A);
						const int32 BRoot = FindRoot(B);
						if (ARoot != BRoot)
						{
							Parent[FMath::Max(ARoot, BRoot)] = FMath::Min(ARoot, BRoot);
						}
					}
				}
			}

			struct FProvinceSeed
			{
				TArray<int32> DemandIndices;
				TArray<int32> CellIndices;
				bool bUsedNearestGroundSeed = false;
			};
			TArray<FProvinceSeed> Seeds;
			TMap<int32, int32> SeedIndexByRoot;
			for (int32 LocalDemandIndex = 0;
				LocalDemandIndex < DemandIndices.Num(); ++LocalDemandIndex)
			{
				const int32 RootIndex = FindRoot(LocalDemandIndex);
				int32* ExistingSeedIndex = SeedIndexByRoot.Find(RootIndex);
				const int32 SeedIndex = ExistingSeedIndex != nullptr
					? *ExistingSeedIndex : Seeds.AddDefaulted();
				if (ExistingSeedIndex == nullptr)
				{
					SeedIndexByRoot.Add(RootIndex, SeedIndex);
				}
				Seeds[SeedIndex].DemandIndices.Add(DemandIndices[LocalDemandIndex]);
			}

			TArray<int32> GroundCells;
			for (int32 BitIndex = 0;
				BitIndex < GroundOccupancy->SizeX * GroundOccupancy->SizeY; ++BitIndex)
			{
				if (SkeletonV3SupportProvinceWordContains(
					GroundOccupancy->OccupiedWords, BitIndex))
				{
					GroundCells.Add(BitIndex);
				}
			}
			for (FProvinceSeed& Seed : Seeds)
			{
				for (const int32 BitIndex : GroundCells)
				{
					const int32 X = BitIndex % GroundOccupancy->SizeX;
					const int32 Y = BitIndex / GroundOccupancy->SizeX;
					const FVector Sample(
						(GroundOccupancy->MinimumXUnit + X) * BlockUnitsCM,
						(GroundOccupancy->MinimumYUnit + Y) * BlockUnitsCM,
						0.0);
					if (Seed.DemandIndices.ContainsByPredicate(
						[&Plan, &Sample](const int32 DemandIndex)
						{
							const FBox& Bounds = Plan.SemanticTerminalDemands[
								DemandIndex].GroundProjectionBounds;
							return Sample.X >= Bounds.Min.X - GeometryToleranceCM
								&& Sample.X <= Bounds.Max.X + GeometryToleranceCM
								&& Sample.Y >= Bounds.Min.Y - GeometryToleranceCM
								&& Sample.Y <= Bounds.Max.Y + GeometryToleranceCM;
						}))
					{
						Seed.CellIndices.Add(BitIndex);
					}
				}
				if (Seed.CellIndices.IsEmpty())
				{
					FVector2D DemandCenter = FVector2D::ZeroVector;
					for (const int32 DemandIndex : Seed.DemandIndices)
					{
						const FVector Center = Plan.SemanticTerminalDemands[
							DemandIndex].BodyBounds.GetCenter();
						DemandCenter += FVector2D(Center.X, Center.Y);
					}
					DemandCenter /= static_cast<double>(Seed.DemandIndices.Num());
					double BestDistanceSquared = DBL_MAX;
					int32 BestCell = INDEX_NONE;
					for (const int32 BitIndex : GroundCells)
					{
						const int32 X = BitIndex % GroundOccupancy->SizeX;
						const int32 Y = BitIndex / GroundOccupancy->SizeX;
						const FVector2D CellCenter(
							(GroundOccupancy->MinimumXUnit + X) * BlockUnitsCM,
							(GroundOccupancy->MinimumYUnit + Y) * BlockUnitsCM);
						const double DistanceSquared = FVector2D::DistSquared(
							CellCenter, DemandCenter);
						if (DistanceSquared < BestDistanceSquared
							|| (FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
								&& BitIndex < BestCell))
						{
							BestDistanceSquared = DistanceSquared;
							BestCell = BitIndex;
						}
					}
					if (BestCell == INDEX_NONE)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SupportProvinceSeedUnavailable:Component=%d"),
							ComponentId);
						return false;
					}
					Seed.CellIndices.Add(BestCell);
					Seed.bUsedNearestGroundSeed = true;
				}
				Seed.CellIndices.Sort();
			}

			TArray<int32> OwnerByCell;
			OwnerByCell.Init(INDEX_NONE,
				GroundOccupancy->SizeX * GroundOccupancy->SizeY);
			TArray<int32> TieBreakCounts;
			TieBreakCounts.Init(0, Seeds.Num());
			for (const int32 BitIndex : GroundCells)
			{
				const int32 CellX = BitIndex % GroundOccupancy->SizeX;
				const int32 CellY = BitIndex / GroundOccupancy->SizeX;
				int32 BestSeed = INDEX_NONE;
				int32 BestDistance = MAX_int32;
				int32 EqualBestCount = 0;
				for (int32 SeedIndex = 0; SeedIndex < Seeds.Num(); ++SeedIndex)
				{
					int32 SeedDistance = MAX_int32;
					for (const int32 SeedCell : Seeds[SeedIndex].CellIndices)
					{
						const int32 SeedX = SeedCell % GroundOccupancy->SizeX;
						const int32 SeedY = SeedCell / GroundOccupancy->SizeX;
						SeedDistance = FMath::Min(SeedDistance,
							FMath::Abs(CellX - SeedX) + FMath::Abs(CellY - SeedY));
					}
					if (SeedDistance < BestDistance)
					{
						BestDistance = SeedDistance;
						BestSeed = SeedIndex;
						EqualBestCount = 1;
					}
					else if (SeedDistance == BestDistance)
					{
						++EqualBestCount;
					}
				}
				if (BestSeed == INDEX_NONE)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SupportProvinceCellUnassigned:Component=%d:Cell=%d"),
						ComponentId, BitIndex);
					return false;
				}
				OwnerByCell[BitIndex] = BestSeed;
				TieBreakCounts[BestSeed] += EqualBestCount > 1 ? 1 : 0;
			}

			const int32 FirstProvinceId = Plan.SupportProvinces.Num();
			for (int32 SeedIndex = 0; SeedIndex < Seeds.Num(); ++SeedIndex)
			{
				FSupportProvinceDiagnostic& Province =
					Plan.SupportProvinces.AddDefaulted_GetRef();
				Province.ProvinceId = Plan.SupportProvinces.Num() - 1;
				Province.ComponentId = ComponentId;
				Province.MinimumXUnit = GroundOccupancy->MinimumXUnit;
				Province.MinimumYUnit = GroundOccupancy->MinimumYUnit;
				Province.SizeX = GroundOccupancy->SizeX;
				Province.SizeY = GroundOccupancy->SizeY;
				Province.GroundCellWords.SetNumZeroed(
					(Province.SizeX * Province.SizeY + 63) / 64);
				Province.TieBreakCellCount = TieBreakCounts[SeedIndex];
				Province.bUsedNearestGroundSeed =
					Seeds[SeedIndex].bUsedNearestGroundSeed;
				FVector CentroidSum = FVector::ZeroVector;
				TArray<int32> ProvinceCells;
				for (const int32 BitIndex : GroundCells)
				{
					if (OwnerByCell[BitIndex] != SeedIndex)
					{
						continue;
					}
					ProvinceCells.Add(BitIndex);
					Province.GroundCellWords[BitIndex >> 6] |=
						uint64(1) << (BitIndex & 63);
					const int32 X = BitIndex % Province.SizeX;
					const int32 Y = BitIndex / Province.SizeX;
					const FVector Center(
						(Province.MinimumXUnit + X) * BlockUnitsCM,
						(Province.MinimumYUnit + Y) * BlockUnitsCM,
						GroundOccupancy->OccupiedBounds.Min.Z + BlockUnitsCM * 0.5);
					CentroidSum += Center;
					Province.GroundBounds += FBox(
						Center - FVector(BlockUnitsCM * 0.5),
						Center + FVector(BlockUnitsCM * 0.5));
				}
				Province.GroundCellCount = ProvinceCells.Num();
				if (Province.GroundCellCount == 0)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SupportProvinceEmpty:Component=%d:Seed=%d"),
						ComponentId, SeedIndex);
					return false;
				}
				Province.GroundCentroid = CentroidSum
					/ static_cast<double>(Province.GroundCellCount);
				double BestAnchorDistanceSquared = DBL_MAX;
				int32 BestAnchorBitIndex = INDEX_NONE;
				for (const int32 BitIndex : ProvinceCells)
				{
					const int32 X = BitIndex % Province.SizeX;
					const int32 Y = BitIndex / Province.SizeX;
					const FVector2D Center(
						(Province.MinimumXUnit + X) * BlockUnitsCM,
						(Province.MinimumYUnit + Y) * BlockUnitsCM);
					const double DistanceSquared = FVector2D::DistSquared(
						Center, FVector2D(Province.GroundCentroid.X,
							Province.GroundCentroid.Y));
					if (DistanceSquared < BestAnchorDistanceSquared
						|| (FMath::IsNearlyEqual(DistanceSquared,
							BestAnchorDistanceSquared)
							&& BitIndex < BestAnchorBitIndex))
					{
						BestAnchorDistanceSquared = DistanceSquared;
						BestAnchorBitIndex = BitIndex;
					}
				}
				if (BestAnchorBitIndex == INDEX_NONE)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SupportProvinceAnchorUnavailable:Province=%d"),
						Province.ProvinceId);
					return false;
				}
				Province.AnchorXUnit = Province.MinimumXUnit
					+ BestAnchorBitIndex % Province.SizeX;
				Province.AnchorYUnit = Province.MinimumYUnit
					+ BestAnchorBitIndex / Province.SizeX;
				Province.bHasAnchorCell = true;
				Province.MinimumRequiredTopCourse = MAX_int32;
				for (const int32 DemandIndex : Seeds[SeedIndex].DemandIndices)
				{
					FSemanticTerminalDemandDiagnostic& Demand =
						Plan.SemanticTerminalDemands[DemandIndex];
					Demand.SupportProvinceId = Province.ProvinceId;
					Province.DemandIds.Add(Demand.DemandId);
					Province.TerminalBodyNodeIds.Add(Demand.TerminalBodyNodeId);
					Province.GroundSourceVolumeIds.Append(
						Demand.GroundSourceVolumeIds);
					Province.MinimumRequiredTopCourse = FMath::Min(
						Province.MinimumRequiredTopCourse, Demand.RequiredTopCourse);
				}
				Province.DemandIds.Sort();
				Province.TerminalBodyNodeIds.Sort();
				Province.GroundSourceVolumeIds.Sort();
				Province.GroundSourceVolumeIds.SetNum(Algo::Unique(
					Province.GroundSourceVolumeIds));
				Province.StableSeedDemandId = Province.DemandIds[0];
				Province.bSyntheticGroundOnly =
					!Province.GroundSourceVolumeIds.IsEmpty();
				for (const int32 SourceVolumeId : Province.GroundSourceVolumeIds)
				{
					const FSemanticSupportVolumeNodeDiagnostic* GroundNode =
						Plan.SemanticSupportVolumeNodes.FindByPredicate(
							[ComponentId, SourceVolumeId](
								const FSemanticSupportVolumeNodeDiagnostic& Node)
							{
								return Node.ComponentId == ComponentId
									&& Node.SourceVolumeId == SourceVolumeId;
							});
					Province.bSyntheticGroundOnly &= GroundNode != nullptr
						&& GroundNode->bSyntheticCoupledGround;
				}

				for (int32 Course = 0; ; ++Course)
				{
					const FSemanticSupportCourseOccupancyDiagnostic* Occupancy =
						Plan.SemanticSupportCourseOccupancies.FindByPredicate(
							[ComponentId, Course](
								const FSemanticSupportCourseOccupancyDiagnostic& Candidate)
							{
								return Candidate.ComponentId == ComponentId
									&& Candidate.CourseIndex == Course;
							});
					if (Occupancy == nullptr || Occupancy->SizeX != Province.SizeX
						|| Occupancy->SizeY != Province.SizeY
						|| Occupancy->MinimumXUnit != Province.MinimumXUnit
						|| Occupancy->MinimumYUnit != Province.MinimumYUnit)
					{
						break;
					}
					bool bFullyOccupied = true;
					for (const int32 BitIndex : ProvinceCells)
					{
						if (!SkeletonV3SupportProvinceWordContains(
							Occupancy->OccupiedWords, BitIndex))
						{
							bFullyOccupied = false;
							break;
						}
					}
					if (!bFullyOccupied)
					{
						break;
					}
					Province.HighestFullyOccupiedTopCourse = Course + 1;
				}
				const int32 MaximumPodiumTop = FMath::Max(
					1, Province.MinimumRequiredTopCourse - 2);
				Province.ProposedPodiumTopCourse = FMath::Clamp(
					Province.HighestFullyOccupiedTopCourse, 1, MaximumPodiumTop);
			}

			for (const int32 BitIndex : GroundCells)
			{
				const int32 Owner = OwnerByCell[BitIndex];
				const int32 X = BitIndex % GroundOccupancy->SizeX;
				const int32 Y = BitIndex / GroundOccupancy->SizeX;
				for (const FIntPoint Delta : {FIntPoint(1, 0), FIntPoint(0, 1)})
				{
					const int32 NX = X + Delta.X;
					const int32 NY = Y + Delta.Y;
					if (NX >= GroundOccupancy->SizeX || NY >= GroundOccupancy->SizeY)
					{
						continue;
					}
					const int32 NeighborBit = NY * GroundOccupancy->SizeX + NX;
					const int32 NeighborOwner = OwnerByCell[NeighborBit];
					if (NeighborOwner != INDEX_NONE && NeighborOwner != Owner)
					{
						FSupportProvinceDiagnostic& A =
							Plan.SupportProvinces[FirstProvinceId + Owner];
						FSupportProvinceDiagnostic& B =
							Plan.SupportProvinces[FirstProvinceId + NeighborOwner];
						A.AdjacentProvinceIds.AddUnique(B.ProvinceId);
						B.AdjacentProvinceIds.AddUnique(A.ProvinceId);
						++Plan.Summary.SupportProvinceBoundaryCount;
					}
				}
			}
			for (int32 ProvinceId = FirstProvinceId;
				ProvinceId < Plan.SupportProvinces.Num(); ++ProvinceId)
			{
				Plan.SupportProvinces[ProvinceId].AdjacentProvinceIds.Sort();
			}
		}

		FString Canonical;
		TSet<int32> AssignedDemandIds;
		for (const FSupportProvinceDiagnostic& Province : Plan.SupportProvinces)
		{
			if (Province.ProvinceId == INDEX_NONE || Province.ComponentId == INDEX_NONE
				|| Province.GroundCellCount <= 0 || !Province.GroundBounds.IsValid
				|| !Province.bHasAnchorCell
				|| Province.DemandIds.IsEmpty()
				|| Province.GroundCellWords.Num()
					!= (Province.SizeX * Province.SizeY + 63) / 64
				|| Province.ProposedPodiumTopCourse <= 0)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportProvinceInvalid:Province=%d:Component=%d:Cells=%d:Demands=%d:Top=%d"),
					Province.ProvinceId, Province.ComponentId,
					Province.GroundCellCount, Province.DemandIds.Num(),
					Province.ProposedPodiumTopCourse);
				return false;
			}
			Plan.Summary.SupportProvinceGroundCellCount += Province.GroundCellCount;
			Plan.Summary.SupportProvinceTieBreakCellCount += Province.TieBreakCellCount;
			Plan.Summary.SupportProvinceNearestSeedFallbackCount +=
				Province.bUsedNearestGroundSeed ? 1 : 0;
			Plan.Summary.MultiDemandSupportProvinceCount +=
				Province.DemandIds.Num() > 1 ? 1 : 0;
			Canonical += FString::Printf(
				TEXT("|P:%d:C=%d:S=%d:N=%d:TIE=%d:FULL=%d:TOP=%d:REQ=%d:FALL=%d:SYN=%d:X=%d:Y=%d:W=%d:H=%d:AX=%d:AY=%d"),
				Province.ProvinceId, Province.ComponentId,
				Province.StableSeedDemandId, Province.GroundCellCount,
				Province.TieBreakCellCount,
				Province.HighestFullyOccupiedTopCourse,
				Province.ProposedPodiumTopCourse,
				Province.MinimumRequiredTopCourse,
				Province.bUsedNearestGroundSeed ? 1 : 0,
				Province.bSyntheticGroundOnly ? 1 : 0,
				Province.MinimumXUnit, Province.MinimumYUnit,
				Province.SizeX, Province.SizeY,
				Province.AnchorXUnit, Province.AnchorYUnit);
			for (const int32 DemandId : Province.DemandIds)
			{
				if (AssignedDemandIds.Contains(DemandId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SupportProvinceDemandMultiplyAssigned:Demand=%d"),
						DemandId);
					return false;
				}
				AssignedDemandIds.Add(DemandId);
				Canonical += FString::Printf(TEXT(":D%d"), DemandId);
			}
			for (const int32 Adjacent : Province.AdjacentProvinceIds)
			{
				Canonical += FString::Printf(TEXT(":A%d"), Adjacent);
			}
			for (const uint64 Word : Province.GroundCellWords)
			{
				Canonical += FString::Printf(TEXT(":%016llx"), Word);
			}
		}
		if (AssignedDemandIds.Num() != Plan.SemanticTerminalDemands.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SupportProvinceDemandCoverageMismatch:Assigned=%d:Demands=%d"),
				AssignedDemandIds.Num(), Plan.SemanticTerminalDemands.Num());
			return false;
		}
		Plan.Summary.SupportProvinceCount = Plan.SupportProvinces.Num();
		Plan.Summary.SupportProvinceHash = HashText(Canonical);
		return Plan.Summary.SupportProvinceCount > 0
			&& Plan.Summary.SupportProvinceGroundCellCount > 0
			&& Plan.Summary.SupportProvinceHash != 0;
	}

	bool FinalizeSupportProvinceGroundCoreBindings(FPlan& Plan, FString& OutError)
	{
		Plan.Summary.BoundSupportProvinceCount = 0;
		Plan.Summary.DistinctProvinceGroundCoreCount = 0;
		Plan.Summary.SupportProvinceMainBindingHash = 0;
		if (Plan.SupportProvinces.IsEmpty())
		{
			Plan.Summary.SupportProvinceMainBindingHash =
				HashText(TEXT("NoTowerChildSupportProvinceBindings"));
			return Plan.Summary.SupportProvinceMainBindingHash != 0;
		}
		TSet<int32> DistinctGroundCoreIds;
		FString Canonical;
		for (FSupportProvinceDiagnostic& Province : Plan.SupportProvinces)
		{
			Province.BoundGroundCoreCellId = INDEX_NONE;
			Province.bAnchorCoveredByBoundCore = false;
			Province.bBoundToPodiumMain = false;
			const bool bComponentHasPodiumMain = Plan.CoreCells.ContainsByPredicate(
				[&Province](const FCoreCellPlan& Core)
				{
					return Core.ComponentId == Province.ComponentId
						&& Core.HierarchyRole == ECoreHierarchyRole::PodiumMain;
				});
			const FVector2D Anchor(
				Province.AnchorXUnit * BlockUnitsCM,
				Province.AnchorYUnit * BlockUnitsCM);
			double BestDistanceSquared = DBL_MAX;
			bool bBestContainsAnchor = false;
			for (const FCoreCellPlan& Core : Plan.CoreCells)
			{
				if (Core.ComponentId != Province.ComponentId
					|| (bComponentHasPodiumMain
						? Core.HierarchyRole != ECoreHierarchyRole::PodiumMain
						: Core.HierarchyRole == ECoreHierarchyRole::TowerChild))
				{
					continue;
				}
				const bool bContainsAnchor =
					Anchor.X >= Core.LocalBounds.Min.X - GeometryToleranceCM
					&& Anchor.X <= Core.LocalBounds.Max.X + GeometryToleranceCM
					&& Anchor.Y >= Core.LocalBounds.Min.Y - GeometryToleranceCM
					&& Anchor.Y <= Core.LocalBounds.Max.Y + GeometryToleranceCM;
				if (bBestContainsAnchor && !bContainsAnchor)
				{
					continue;
				}
				const FVector Center = Core.LocalBounds.GetCenter();
				const double DistanceSquared = FVector2D::DistSquared(
					Anchor, FVector2D(Center.X, Center.Y));
				if (Province.BoundGroundCoreCellId == INDEX_NONE
					|| (bContainsAnchor && !bBestContainsAnchor)
					|| (bContainsAnchor == bBestContainsAnchor
						&& (DistanceSquared < BestDistanceSquared
							|| (FMath::IsNearlyEqual(DistanceSquared,
								BestDistanceSquared)
								&& Core.CoreCellId
									< Province.BoundGroundCoreCellId))))
				{
					Province.BoundGroundCoreCellId = Core.CoreCellId;
					Province.bAnchorCoveredByBoundCore = bContainsAnchor;
					Province.bBoundToPodiumMain =
						Core.HierarchyRole == ECoreHierarchyRole::PodiumMain;
					BestDistanceSquared = DistanceSquared;
					bBestContainsAnchor = bContainsAnchor;
				}
			}
			if (Province.BoundGroundCoreCellId == INDEX_NONE
				|| (bComponentHasPodiumMain
					&& !Province.bAnchorCoveredByBoundCore))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportProvinceGroundCoreUnavailable:Province=%d:Component=%d:Anchor=%d,%d:HasPodiumMain=%d:Selected=%d:Covered=%d"),
					Province.ProvinceId, Province.ComponentId,
					Province.AnchorXUnit, Province.AnchorYUnit,
					bComponentHasPodiumMain ? 1 : 0,
					Province.BoundGroundCoreCellId,
					Province.bAnchorCoveredByBoundCore ? 1 : 0);
				return false;
			}
			++Plan.Summary.BoundSupportProvinceCount;
			DistinctGroundCoreIds.Add(Province.BoundGroundCoreCellId);
			Canonical += FString::Printf(
				TEXT("|P:%d:C:%d:A=%d,%d:G=%d:COVER=%d:MAIN=%d"),
				Province.ProvinceId, Province.ComponentId,
				Province.AnchorXUnit, Province.AnchorYUnit,
				Province.BoundGroundCoreCellId,
				Province.bAnchorCoveredByBoundCore ? 1 : 0,
				Province.bBoundToPodiumMain ? 1 : 0);
		}
		Plan.Summary.DistinctProvinceGroundCoreCount = DistinctGroundCoreIds.Num();
		Plan.Summary.SupportProvinceMainBindingHash = HashText(Canonical);
		return Plan.Summary.BoundSupportProvinceCount
				== Plan.Summary.SupportProvinceCount
			&& Plan.Summary.SupportProvinceMainBindingHash != 0;
	}

	bool BuildLocalPodiumHeightPlanDiagnostics(FPlan& Plan, FString& OutError)
	{
		Plan.LocalPodiumHeightCandidates.Reset();
		Plan.LocalPodiumHeightRegions.Reset();
		Plan.Summary.LocalPodiumHeightCandidateCount = 0;
		Plan.Summary.RejectedLocalPodiumHeightCandidateCount = 0;
		Plan.Summary.LocalPodiumHeightRegionCount = 0;
		Plan.Summary.RaisedLocalPodiumHeightRegionCount = 0;
		Plan.Summary.LocalPodiumHeightPlanHash = 0;
		if (Plan.SupportProvinces.IsEmpty())
		{
			Plan.Summary.LocalPodiumHeightPlanHash =
				HashText(TEXT("NoLocalPodiumHeightRegions"));
			return Plan.Summary.LocalPodiumHeightPlanHash != 0;
		}

		TMap<int32, int32> SelectedTopByProvince;
		TMap<int32, int32> ActualTopByProvince;
		TMap<int32, TSet<int32>> OwnBoundaryCoursesByProvince;
		TMap<int32, int32> StructuralPodiumMainByProvince;
		TMap<int32, TSet<int32>> EventCoursesByStructuralPodiumMain;
		FString Canonical;
		for (const FSupportProvinceDiagnostic& Province : Plan.SupportProvinces)
		{
			TSet<int32> CommonBoundaryCourses;
			bool bFirstDemand = true;
			int32 StructuralPodiumMainCoreCellId = INDEX_NONE;
			for (const int32 DemandId : Province.DemandIds)
			{
				const FCoreCellPlan* DemandChild = Plan.CoreCells.FindByPredicate(
					[DemandId](const FCoreCellPlan& Core)
					{
						return Core.HierarchyRole == ECoreHierarchyRole::TowerChild
							&& Core.SemanticDemandId == DemandId;
					});
				if (DemandChild == nullptr
					|| DemandChild->PodiumMainCoreCellId == INDEX_NONE
					|| (StructuralPodiumMainCoreCellId != INDEX_NONE
						&& StructuralPodiumMainCoreCellId
							!= DemandChild->PodiumMainCoreCellId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3LocalPodiumStructuralMainUnavailable:Province=%d:Demand=%d:ExistingMain=%d:Child=%d:ChildMain=%d"),
						Province.ProvinceId, DemandId,
						StructuralPodiumMainCoreCellId,
						DemandChild != nullptr ? DemandChild->CoreCellId : INDEX_NONE,
						DemandChild != nullptr
							? DemandChild->PodiumMainCoreCellId : INDEX_NONE);
					return false;
				}
				StructuralPodiumMainCoreCellId = DemandChild->PodiumMainCoreCellId;
				const FSemanticTerminalDemandDiagnostic* Demand =
					Plan.SemanticTerminalDemands.FindByPredicate(
						[DemandId](const FSemanticTerminalDemandDiagnostic& Candidate)
						{
							return Candidate.DemandId == DemandId;
						});
				if (Demand == nullptr)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3LocalPodiumDemandUnavailable:Province=%d:Demand=%d"),
						Province.ProvinceId, DemandId);
					return false;
				}
				TSet<int32> DemandBoundaryCourses;
				for (const FSemanticSupportMergeLedgerDiagnostic& Ledger
					: Plan.SemanticSupportMergeLedger)
				{
					if (Ledger.ComponentId != Province.ComponentId)
					{
						continue;
					}
					const bool bTouchesLineage = Ledger.LowerNodeIds.ContainsByPredicate(
						[Demand](const int32 NodeId)
						{
							return Demand->LineageNodeIds.Contains(NodeId);
						}) || Ledger.UpperNodeIds.ContainsByPredicate(
						[Demand](const int32 NodeId)
						{
							return Demand->LineageNodeIds.Contains(NodeId);
						});
					if (bTouchesLineage && Ledger.ContactCourse > 0)
					{
						DemandBoundaryCourses.Add(Ledger.ContactCourse);
					}
				}
				if (bFirstDemand)
				{
					CommonBoundaryCourses = MoveTemp(DemandBoundaryCourses);
					bFirstDemand = false;
				}
				else
				{
					for (auto It = CommonBoundaryCourses.CreateIterator(); It; ++It)
					{
						if (!DemandBoundaryCourses.Contains(*It))
						{
							It.RemoveCurrent();
						}
					}
				}
			}
			OwnBoundaryCoursesByProvince.Add(
				Province.ProvinceId, CommonBoundaryCourses);
			StructuralPodiumMainByProvince.Add(
				Province.ProvinceId, StructuralPodiumMainCoreCellId);
			TSet<int32>& StructuralMainEvents =
				EventCoursesByStructuralPodiumMain.FindOrAdd(
					StructuralPodiumMainCoreCellId);
			for (const int32 Course : CommonBoundaryCourses)
			{
				StructuralMainEvents.Add(Course);
			}
		}
		for (const FSupportProvinceDiagnostic& Province : Plan.SupportProvinces)
		{
			const int32 StructuralPodiumMainCoreCellId =
				StructuralPodiumMainByProvince.FindChecked(Province.ProvinceId);
			const FCoreCellPlan* BoundGroundCore = Plan.CoreCells.FindByPredicate(
				[&Province](const FCoreCellPlan& Core)
				{
					return Core.CoreCellId == Province.BoundGroundCoreCellId;
				});
			const FCoreCellPlan* StructuralPodiumMain = Plan.CoreCells.FindByPredicate(
				[StructuralPodiumMainCoreCellId](const FCoreCellPlan& Core)
				{
					return Core.CoreCellId == StructuralPodiumMainCoreCellId;
				});
			if (BoundGroundCore == nullptr
				|| BoundGroundCore->ComponentId != Province.ComponentId
				|| StructuralPodiumMain == nullptr
				|| StructuralPodiumMain->ComponentId != Province.ComponentId
				|| StructuralPodiumMain->HierarchyRole
					!= ECoreHierarchyRole::PodiumMain
				|| StructuralPodiumMain->TopCourseIndex <= 0)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3LocalPodiumBaselineUnavailable:Province=%d:Component=%d:GroundCore=%d:StructuralMain=%d"),
					Province.ProvinceId, Province.ComponentId,
					Province.BoundGroundCoreCellId,
					StructuralPodiumMainCoreCellId);
				return false;
			}
			const int32 ActualTopCourse = StructuralPodiumMain->TopCourseIndex;
			ActualTopByProvince.Add(Province.ProvinceId, ActualTopCourse);

			const TSet<int32>& CommonBoundaryCourses =
				OwnBoundaryCoursesByProvince.FindChecked(Province.ProvinceId);
			const TSet<int32>& StructuralMainEventCourses =
				EventCoursesByStructuralPodiumMain.FindChecked(
					StructuralPodiumMainCoreCellId);

			TArray<int32> ContinuousTopByCell;
			ContinuousTopByCell.Init(0, Province.SizeX * Province.SizeY);
			for (int32 BitIndex = 0; BitIndex < ContinuousTopByCell.Num(); ++BitIndex)
			{
				if (!SkeletonV3SupportProvinceWordContains(
					Province.GroundCellWords, BitIndex))
				{
					continue;
				}
				for (int32 Course = 0; ; ++Course)
				{
					const FSemanticSupportCourseOccupancyDiagnostic* Occupancy =
						Plan.SemanticSupportCourseOccupancies.FindByPredicate(
							[&Province, Course](
								const FSemanticSupportCourseOccupancyDiagnostic& Candidate)
							{
								return Candidate.ComponentId == Province.ComponentId
									&& Candidate.CourseIndex == Course;
							});
					if (Occupancy == nullptr
						|| Occupancy->MinimumXUnit != Province.MinimumXUnit
						|| Occupancy->MinimumYUnit != Province.MinimumYUnit
						|| Occupancy->SizeX != Province.SizeX
						|| Occupancy->SizeY != Province.SizeY
						|| !SkeletonV3SupportProvinceWordContains(
							Occupancy->OccupiedWords, BitIndex))
					{
						break;
					}
					ContinuousTopByCell[BitIndex] = Course + 1;
				}
			}

			TSet<int32> CandidateCourses = StructuralMainEventCourses;
			CandidateCourses.Add(ActualTopCourse);
			CandidateCourses.Add(Province.ProposedPodiumTopCourse);
			TArray<int32> SortedCandidates = CandidateCourses.Array();
			SortedCandidates.Sort();
			bool bHasLegalCandidate = false;
			for (const int32 CandidateTopCourse : SortedCandidates)
			{
				if (CandidateTopCourse <= 0)
				{
					continue;
				}
				FLocalPodiumHeightCandidateDiagnostic& Candidate =
					Plan.LocalPodiumHeightCandidates.AddDefaulted_GetRef();
				Candidate.ProvinceId = Province.ProvinceId;
				Candidate.ComponentId = Province.ComponentId;
				Candidate.BoundGroundCoreCellId = Province.BoundGroundCoreCellId;
				Candidate.StructuralPodiumMainCoreCellId =
					StructuralPodiumMainCoreCellId;
				Candidate.CandidateTopCourse = CandidateTopCourse;
				Candidate.ActualPodiumTopCourse = ActualTopCourse;
				Candidate.bActualBaseline = CandidateTopCourse == ActualTopCourse;
				Candidate.bOwnSemanticBoundary = Candidate.bActualBaseline
					|| CommonBoundaryCourses.Contains(CandidateTopCourse);
				Candidate.bSharedPodiumMainSemanticEvent = Candidate.bActualBaseline
					|| StructuralMainEventCourses.Contains(CandidateTopCourse);
				Candidate.bCommonSemanticBoundary = Candidate.bActualBaseline
					|| Candidate.bSharedPodiumMainSemanticEvent;
				const int32 WordCount =
					(Province.SizeX * Province.SizeY + 63) / 64;
				TArray<uint64> RawPersistentWords;
				RawPersistentWords.Init(0, WordCount);
				int32 RawPersistentCellCount = 0;
				for (int32 BitIndex = 0; BitIndex < ContinuousTopByCell.Num(); ++BitIndex)
				{
					const bool bPersistent = Candidate.bActualBaseline
						? SkeletonV3SupportProvinceWordContains(
							Province.GroundCellWords, BitIndex)
						: ContinuousTopByCell[BitIndex] >= CandidateTopCourse;
					if (bPersistent)
					{
						RawPersistentWords[BitIndex >> 6] |=
							uint64(1) << (BitIndex & 63);
						++RawPersistentCellCount;
					}
				}
				TArray<int32> DemandSeedBits;
				Candidate.bCoversEveryDemandSeed = RawPersistentCellCount > 0;
				for (const int32 DemandId : Province.DemandIds)
				{
					const FSemanticTerminalDemandDiagnostic* Demand =
						Plan.SemanticTerminalDemands.FindByPredicate(
							[DemandId](const FSemanticTerminalDemandDiagnostic& Entry)
							{
								return Entry.DemandId == DemandId;
							});
					int32 SeedBit = INDEX_NONE;
					if (Demand != nullptr)
					{
						for (int32 BitIndex = 0;
							BitIndex < Province.SizeX * Province.SizeY; ++BitIndex)
						{
							if (!SkeletonV3SupportProvinceWordContains(
								RawPersistentWords, BitIndex))
							{
								continue;
							}
							const int32 X = BitIndex % Province.SizeX;
							const int32 Y = BitIndex / Province.SizeX;
							const FVector2D Center(
								(Province.MinimumXUnit + X) * BlockUnitsCM,
								(Province.MinimumYUnit + Y) * BlockUnitsCM);
							if (Center.X >= Demand->GroundProjectionBounds.Min.X
								- GeometryToleranceCM
								&& Center.X <= Demand->GroundProjectionBounds.Max.X
									+ GeometryToleranceCM
								&& Center.Y >= Demand->GroundProjectionBounds.Min.Y
									- GeometryToleranceCM
								&& Center.Y <= Demand->GroundProjectionBounds.Max.Y
									+ GeometryToleranceCM)
							{
								SeedBit = BitIndex;
								break;
							}
						}
					}
					Candidate.bCoversEveryDemandSeed &= SeedBit != INDEX_NONE;
					if (SeedBit != INDEX_NONE)
					{
						DemandSeedBits.AddUnique(SeedBit);
					}
				}
				Candidate.PersistentCellWords.Init(0, WordCount);
				if (!DemandSeedBits.IsEmpty())
				{
					TArray<int32> PendingBits{DemandSeedBits[0]};
					while (!PendingBits.IsEmpty())
					{
						const int32 BitIndex = PendingBits.Pop(EAllowShrinking::No);
						if (!SkeletonV3SupportProvinceWordContains(
							RawPersistentWords, BitIndex)
							|| SkeletonV3SupportProvinceWordContains(
								Candidate.PersistentCellWords, BitIndex))
						{
							continue;
						}
						Candidate.PersistentCellWords[BitIndex >> 6] |=
							uint64(1) << (BitIndex & 63);
						++Candidate.PersistentCellCount;
						const int32 X = BitIndex % Province.SizeX;
						const int32 Y = BitIndex / Province.SizeX;
						for (const FIntPoint Delta : {
							FIntPoint(-1, 0), FIntPoint(1, 0),
							FIntPoint(0, -1), FIntPoint(0, 1)})
						{
							const int32 NX = X + Delta.X;
							const int32 NY = Y + Delta.Y;
							if (NX >= 0 && NX < Province.SizeX
								&& NY >= 0 && NY < Province.SizeY)
							{
								PendingBits.Add(NY * Province.SizeX + NX);
							}
						}
					}
				}
				for (const int32 SeedBit : DemandSeedBits)
				{
					Candidate.bCoversEveryDemandSeed &=
						SkeletonV3SupportProvinceWordContains(
							Candidate.PersistentCellWords, SeedBit);
				}
				Candidate.bSingleConnectedFootprint =
					Candidate.PersistentCellCount > 0;
				Candidate.bFullyOccupiedThroughCandidate =
					Candidate.bSingleConnectedFootprint
					&& Candidate.bCoversEveryDemandSeed;
				Candidate.bLeavesTwoChildCourses = CandidateTopCourse + 2
					<= Province.MinimumRequiredTopCourse;
				Candidate.bProtectedVoidClear = true;
				if (CandidateTopCourse > ActualTopCourse
					&& Plan.Components.IsValidIndex(Province.ComponentId))
				{
					const double GroundZ =
						Plan.Components[Province.ComponentId].GroundPlaneZCM;
					const double AddedMinimumZ = GroundZ
						+ ActualTopCourse * static_cast<double>(BlockUnitsCM);
					const double AddedMaximumZ = GroundZ
						+ CandidateTopCourse * static_cast<double>(BlockUnitsCM);
					for (int32 BitIndex = 0;
						BitIndex < Province.SizeX * Province.SizeY
							&& Candidate.bProtectedVoidClear; ++BitIndex)
					{
						if (!SkeletonV3SupportProvinceWordContains(
							Candidate.PersistentCellWords, BitIndex))
						{
							continue;
						}
						const int32 X = BitIndex % Province.SizeX;
						const int32 Y = BitIndex / Province.SizeX;
						const double CenterX =
							(Province.MinimumXUnit + X) * BlockUnitsCM;
						const double CenterY =
							(Province.MinimumYUnit + Y) * BlockUnitsCM;
						const FBox AddedColumn(
							FVector(CenterX - BlockUnitsCM * 0.5,
								CenterY - BlockUnitsCM * 0.5, AddedMinimumZ),
							FVector(CenterX + BlockUnitsCM * 0.5,
								CenterY + BlockUnitsCM * 0.5, AddedMaximumZ));
						Candidate.bProtectedVoidClear =
							!Plan.ReservedSupportVoids.ContainsByPredicate(
								[&AddedColumn](const FABTSM73BeamASupportVoid& Void)
								{
									return AddedColumn.IsValid && Void.Bounds.IsValid
										&& AddedColumn.Min.X
											< Void.Bounds.Max.X - GeometryToleranceCM
										&& AddedColumn.Max.X
											> Void.Bounds.Min.X + GeometryToleranceCM
										&& AddedColumn.Min.Y
											< Void.Bounds.Max.Y - GeometryToleranceCM
										&& AddedColumn.Max.Y
											> Void.Bounds.Min.Y + GeometryToleranceCM
										&& AddedColumn.Min.Z
											< Void.Bounds.Max.Z - GeometryToleranceCM
										&& AddedColumn.Max.Z
											> Void.Bounds.Min.Z + GeometryToleranceCM;
								});
					}
				}
				const bool bNotBelowActual = CandidateTopCourse >= ActualTopCourse;
				Candidate.bAccepted = bNotBelowActual
					&& Candidate.bCommonSemanticBoundary
					&& Candidate.bFullyOccupiedThroughCandidate
					&& Candidate.bCoversEveryDemandSeed
					&& Candidate.bSingleConnectedFootprint
					&& Candidate.bLeavesTwoChildCourses
					&& Candidate.bProtectedVoidClear;
				if (Candidate.bAccepted)
				{
					Candidate.DecisionReason = Candidate.bActualBaseline
						? TEXT("AcceptedFrozenActualBaseline")
						: Candidate.bOwnSemanticBoundary
							? TEXT("AcceptedOwnSemanticSeparation")
							: TEXT("AcceptedSiblingPodiumMainSemanticEvent");
					bHasLegalCandidate = true;
				}
				else
				{
					Candidate.DecisionReason = TEXT("Rejected");
					Candidate.DecisionReason += bNotBelowActual
						? TEXT("") : TEXT(":BelowActualPodium");
					Candidate.DecisionReason += Candidate.bCommonSemanticBoundary
						? TEXT("") : TEXT(":NotCommonSemanticBoundary");
					Candidate.DecisionReason += Candidate.bFullyOccupiedThroughCandidate
						? TEXT("") : TEXT(":NoContinuousLocalFootprint");
					Candidate.DecisionReason += Candidate.bCoversEveryDemandSeed
						? TEXT("") : TEXT(":DemandSeedUncovered");
					Candidate.DecisionReason += Candidate.bSingleConnectedFootprint
						? TEXT("") : TEXT(":DisconnectedLocalFootprint");
					Candidate.DecisionReason += Candidate.bLeavesTwoChildCourses
						? TEXT("") : TEXT(":InsufficientChildClearance");
					Candidate.DecisionReason += Candidate.bProtectedVoidClear
						? TEXT("") : TEXT(":ProtectedSpanVoid");
					++Plan.Summary.RejectedLocalPodiumHeightCandidateCount;
				}
				Canonical += FString::Printf(
					TEXT("|C:P=%d:G=%d:M=%d:K=%d:A=%d:OWN=%d:SHARED=%d:B=%d:F=%d:D=%d:N=%d:H=%d:V=%d:CELLS=%d:OK=%d:R=%s"),
					Province.ProvinceId, Province.BoundGroundCoreCellId,
					StructuralPodiumMainCoreCellId,
					CandidateTopCourse,
					Candidate.bActualBaseline ? 1 : 0,
					Candidate.bOwnSemanticBoundary ? 1 : 0,
					Candidate.bSharedPodiumMainSemanticEvent ? 1 : 0,
					Candidate.bCommonSemanticBoundary ? 1 : 0,
					Candidate.bFullyOccupiedThroughCandidate ? 1 : 0,
					Candidate.bCoversEveryDemandSeed ? 1 : 0,
					Candidate.bSingleConnectedFootprint ? 1 : 0,
					Candidate.bLeavesTwoChildCourses ? 1 : 0,
					Candidate.bProtectedVoidClear ? 1 : 0,
					Candidate.PersistentCellCount,
					Candidate.bAccepted ? 1 : 0,
					*Candidate.DecisionReason);
				for (const uint64 Word : Candidate.PersistentCellWords)
				{
					Canonical += FString::Printf(TEXT(":%016llx"), Word);
				}
			}
			if (!bHasLegalCandidate)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3LocalPodiumNoLegalHeight:Province=%d:Actual=%d:FullyOccupied=%d:Required=%d"),
					Province.ProvinceId, ActualTopCourse,
					Province.HighestFullyOccupiedTopCourse,
					Province.MinimumRequiredTopCourse);
				return false;
			}
		}

		auto CandidateFootprintGapUnits = [&Plan](
			const int32 FirstProvinceId,
			const FLocalPodiumHeightCandidateDiagnostic& FirstCandidate,
			const int32 SecondProvinceId,
			const FLocalPodiumHeightCandidateDiagnostic& SecondCandidate) -> int32
		{
			const FSupportProvinceDiagnostic* FirstProvince =
				Plan.SupportProvinces.FindByPredicate(
					[FirstProvinceId](const FSupportProvinceDiagnostic& Province)
					{
						return Province.ProvinceId == FirstProvinceId;
					});
			const FSupportProvinceDiagnostic* SecondProvince =
				Plan.SupportProvinces.FindByPredicate(
					[SecondProvinceId](const FSupportProvinceDiagnostic& Province)
					{
						return Province.ProvinceId == SecondProvinceId;
					});
			if (FirstProvince == nullptr || SecondProvince == nullptr
				|| FirstProvince->ComponentId != SecondProvince->ComponentId
				|| FirstCandidate.StructuralPodiumMainCoreCellId
					!= SecondCandidate.StructuralPodiumMainCoreCellId
				|| FirstCandidate.CandidateTopCourse
					!= SecondCandidate.CandidateTopCourse)
			{
				return INDEX_NONE;
			}
			int32 MinimumGapUnits = MAX_int32;
			for (int32 BitIndex = 0;
				BitIndex < FirstProvince->SizeX * FirstProvince->SizeY; ++BitIndex)
			{
				if (!SkeletonV3SupportProvinceWordContains(
					FirstCandidate.PersistentCellWords, BitIndex))
				{
					continue;
				}
				const int32 X = BitIndex % FirstProvince->SizeX;
				const int32 Y = BitIndex / FirstProvince->SizeX;
				for (int32 SecondBitIndex = 0;
					SecondBitIndex < SecondProvince->SizeX * SecondProvince->SizeY;
					++SecondBitIndex)
				{
					if (!SkeletonV3SupportProvinceWordContains(
						SecondCandidate.PersistentCellWords, SecondBitIndex))
					{
						continue;
					}
					const int32 SecondX = SecondBitIndex % SecondProvince->SizeX;
					const int32 SecondY = SecondBitIndex / SecondProvince->SizeX;
					const int32 GapUnits = FMath::Max(0,
						FMath::Abs(X - SecondX) + FMath::Abs(Y - SecondY) - 1);
					MinimumGapUnits = FMath::Min(MinimumGapUnits, GapUnits);
					if (MinimumGapUnits == 0)
					{
						return 0;
					}
				}
			}
			return MinimumGapUnits == MAX_int32 ? INDEX_NONE : MinimumGapUnits;
		};
		auto CandidateBridgeVoidClear = [&Plan](
			const int32 FirstProvinceId,
			const FLocalPodiumHeightCandidateDiagnostic& FirstCandidate,
			const int32 SecondProvinceId,
			const FLocalPodiumHeightCandidateDiagnostic& SecondCandidate)
		{
			const FSupportProvinceDiagnostic* FirstProvince =
				Plan.SupportProvinces.FindByPredicate(
					[FirstProvinceId](const FSupportProvinceDiagnostic& Province)
					{
						return Province.ProvinceId == FirstProvinceId;
					});
			const FSupportProvinceDiagnostic* SecondProvince =
				Plan.SupportProvinces.FindByPredicate(
					[SecondProvinceId](const FSupportProvinceDiagnostic& Province)
					{
						return Province.ProvinceId == SecondProvinceId;
					});
			if (FirstProvince == nullptr || SecondProvince == nullptr
				|| FirstCandidate.CandidateTopCourse
					!= SecondCandidate.CandidateTopCourse
				|| !Plan.Components.IsValidIndex(FirstProvince->ComponentId))
			{
				return false;
			}
			FIntPoint FirstCell = FIntPoint::ZeroValue;
			FIntPoint SecondCell = FIntPoint::ZeroValue;
			int32 MinimumManhattan = MAX_int32;
			for (int32 FirstBit = 0;
				FirstBit < FirstProvince->SizeX * FirstProvince->SizeY; ++FirstBit)
			{
				if (!SkeletonV3SupportProvinceWordContains(
					FirstCandidate.PersistentCellWords, FirstBit))
				{
					continue;
				}
				const FIntPoint A(
					FirstBit % FirstProvince->SizeX,
					FirstBit / FirstProvince->SizeX);
				for (int32 SecondBit = 0;
					SecondBit < SecondProvince->SizeX * SecondProvince->SizeY;
					++SecondBit)
				{
					if (!SkeletonV3SupportProvinceWordContains(
						SecondCandidate.PersistentCellWords, SecondBit))
					{
						continue;
					}
					const FIntPoint B(
						SecondBit % SecondProvince->SizeX,
						SecondBit / SecondProvince->SizeX);
					const int32 Manhattan = FMath::Abs(A.X - B.X)
						+ FMath::Abs(A.Y - B.Y);
					if (Manhattan < MinimumManhattan)
					{
						MinimumManhattan = Manhattan;
						FirstCell = A;
						SecondCell = B;
					}
				}
			}
			if (MinimumManhattan == MAX_int32 || MinimumManhattan <= 1)
			{
				return MinimumManhattan != MAX_int32;
			}
			const double GroundZ =
				Plan.Components[FirstProvince->ComponentId].GroundPlaneZCM;
			const double MinimumZ = GroundZ
				+ FMath::Max(FirstCandidate.ActualPodiumTopCourse,
					SecondCandidate.ActualPodiumTopCourse)
					* static_cast<double>(BlockUnitsCM);
			const double MaximumZ = GroundZ
				+ FirstCandidate.CandidateTopCourse
					* static_cast<double>(BlockUnitsCM);
			const FVector2D A(
				(FirstProvince->MinimumXUnit + FirstCell.X) * BlockUnitsCM,
				(FirstProvince->MinimumYUnit + FirstCell.Y) * BlockUnitsCM);
			const FVector2D B(
				(SecondProvince->MinimumXUnit + SecondCell.X) * BlockUnitsCM,
				(SecondProvince->MinimumYUnit + SecondCell.Y) * BlockUnitsCM);
			auto SegmentClear = [&Plan, MinimumZ, MaximumZ](
				const FVector2D& Start, const FVector2D& End)
			{
				const double HalfCell = BlockUnitsCM * 0.5;
				const FBox Segment(
					FVector(FMath::Min(Start.X, End.X) - HalfCell,
						FMath::Min(Start.Y, End.Y) - HalfCell, MinimumZ),
					FVector(FMath::Max(Start.X, End.X) + HalfCell,
						FMath::Max(Start.Y, End.Y) + HalfCell, MaximumZ));
				return !Plan.ReservedSupportVoids.ContainsByPredicate(
					[&Segment](const FABTSM73BeamASupportVoid& Void)
					{
						return Segment.IsValid && Void.Bounds.IsValid
							&& Segment.Min.X < Void.Bounds.Max.X - GeometryToleranceCM
							&& Segment.Max.X > Void.Bounds.Min.X + GeometryToleranceCM
							&& Segment.Min.Y < Void.Bounds.Max.Y - GeometryToleranceCM
							&& Segment.Max.Y > Void.Bounds.Min.Y + GeometryToleranceCM
							&& Segment.Min.Z < Void.Bounds.Max.Z - GeometryToleranceCM
							&& Segment.Max.Z > Void.Bounds.Min.Z + GeometryToleranceCM;
					});
			};
			const FVector2D XThenY(B.X, A.Y);
			const FVector2D YThenX(A.X, B.Y);
			return (SegmentClear(A, XThenY) && SegmentClear(XThenY, B))
				|| (SegmentClear(A, YThenX) && SegmentClear(YThenX, B));
		};

		TMap<int32, int32> FirstRaisedCellCountByProvince;
		for (const FLocalPodiumHeightCandidateDiagnostic& Candidate
			: Plan.LocalPodiumHeightCandidates)
		{
			if (!Candidate.bAccepted || Candidate.bActualBaseline)
			{
				continue;
			}
			int32& FirstRaisedCount = FirstRaisedCellCountByProvince.FindOrAdd(
				Candidate.ProvinceId, Candidate.PersistentCellCount);
			const int32 ExistingFirstCourse = Plan.LocalPodiumHeightCandidates.IndexOfByPredicate(
				[&Candidate](const FLocalPodiumHeightCandidateDiagnostic& Entry)
				{
					return Entry.ProvinceId == Candidate.ProvinceId
						&& Entry.bAccepted && !Entry.bActualBaseline
						&& Entry.CandidateTopCourse < Candidate.CandidateTopCourse;
				});
			if (ExistingFirstCourse != INDEX_NONE)
			{
				continue;
			}
			FirstRaisedCount = Candidate.PersistentCellCount;
		}
		for (FLocalPodiumHeightCandidateDiagnostic& Candidate
			: Plan.LocalPodiumHeightCandidates)
		{
			if (!Candidate.bAccepted || Candidate.bActualBaseline)
			{
				continue;
			}
			Candidate.FirstRaisedPersistentCellCount =
				FirstRaisedCellCountByProvince.FindRef(Candidate.ProvinceId);
			Candidate.RetainedFootprintPermille =
				Candidate.FirstRaisedPersistentCellCount > 0
					? FMath::FloorToInt(1000.0
						* static_cast<double>(Candidate.PersistentCellCount)
						/ Candidate.FirstRaisedPersistentCellCount)
					: 0;
			Candidate.bRetainsHalfFirstRaisedFootprint =
				Candidate.RetainedFootprintPermille >= 500;
			const FSupportProvinceDiagnostic* Province =
				Plan.SupportProvinces.FindByPredicate(
					[&Candidate](const FSupportProvinceDiagnostic& Entry)
					{
						return Entry.ProvinceId == Candidate.ProvinceId;
					});
			if (Province == nullptr)
			{
				continue;
			}
			for (const int32 AdjacentId : Province->AdjacentProvinceIds)
			{
				const FLocalPodiumHeightCandidateDiagnostic* Sibling =
					Plan.LocalPodiumHeightCandidates.FindByPredicate(
						[AdjacentId, &Candidate](
							const FLocalPodiumHeightCandidateDiagnostic& Entry)
						{
							return Entry.ProvinceId == AdjacentId
								&& Entry.StructuralPodiumMainCoreCellId
									== Candidate.StructuralPodiumMainCoreCellId
								&& Entry.CandidateTopCourse
									== Candidate.CandidateTopCourse
								&& Entry.bAccepted;
						});
				if (Sibling == nullptr)
				{
					continue;
				}
				const int32 GapUnits = CandidateFootprintGapUnits(
					Candidate.ProvinceId, Candidate, AdjacentId, *Sibling);
				if (GapUnits != INDEX_NONE
					&& (Candidate.MinimumSiblingFootprintGapUnits == INDEX_NONE
						|| GapUnits < Candidate.MinimumSiblingFootprintGapUnits))
				{
					Candidate.MinimumSiblingFootprintGapUnits = GapUnits;
					Candidate.bSiblingBridgeWithinMemberSpan =
						(GapUnits + 1) * BlockUnitsCM
							<= 720.0 + GeometryToleranceCM;
					Candidate.bSiblingBridgeVoidClear = CandidateBridgeVoidClear(
						Candidate.ProvinceId, Candidate, AdjacentId, *Sibling);
				}
			}
		}
		for (const FLocalPodiumHeightCandidateDiagnostic& Candidate
			: Plan.LocalPodiumHeightCandidates)
		{
			Canonical += FString::Printf(
				TEXT("|SEAM:P=%d:M=%d:K=%d:U=%d:FIRST=%d:KEEP=%d:HALF=%d:SPAN=%d:CLEAR=%d"),
				Candidate.ProvinceId,
				Candidate.StructuralPodiumMainCoreCellId,
				Candidate.CandidateTopCourse,
				Candidate.MinimumSiblingFootprintGapUnits,
				Candidate.FirstRaisedPersistentCellCount,
				Candidate.RetainedFootprintPermille,
				Candidate.bRetainsHalfFirstRaisedFootprint ? 1 : 0,
				Candidate.bSiblingBridgeWithinMemberSpan ? 1 : 0,
				Candidate.bSiblingBridgeVoidClear ? 1 : 0);
		}

		struct FLocalPodiumGroupOpportunity
		{
			int32 ComponentId = INDEX_NONE;
			int32 StructuralPodiumMainCoreCellId = INDEX_NONE;
			int32 TopCourse = 0;
			TArray<int32> ProvinceIds;
		};
		TArray<FLocalPodiumGroupOpportunity> Opportunities;
		TSet<uint64> StructuralMainCourseKeys;
		for (const FLocalPodiumHeightCandidateDiagnostic& Candidate
			: Plan.LocalPodiumHeightCandidates)
		{
			if (Candidate.bAccepted && !Candidate.bActualBaseline)
			{
				StructuralMainCourseKeys.Add(
					(static_cast<uint64>(static_cast<uint32>(
						Candidate.StructuralPodiumMainCoreCellId)) << 32)
					| static_cast<uint32>(Candidate.CandidateTopCourse));
			}
		}
		TArray<uint64> SortedStructuralMainCourseKeys =
			StructuralMainCourseKeys.Array();
		SortedStructuralMainCourseKeys.Sort([](const uint64 A, const uint64 B)
		{
			const int32 CourseA = static_cast<int32>(A & 0xffffffffu);
			const int32 CourseB = static_cast<int32>(B & 0xffffffffu);
			return CourseA != CourseB ? CourseA > CourseB : A < B;
		});
		for (const uint64 StructuralMainCourseKey : SortedStructuralMainCourseKeys)
		{
			const int32 StructuralPodiumMainCoreCellId = static_cast<int32>(
				StructuralMainCourseKey >> 32);
			const int32 TopCourse = static_cast<int32>(
				StructuralMainCourseKey & 0xffffffffu);
			TSet<int32> EligibleProvinceIds;
			for (const FLocalPodiumHeightCandidateDiagnostic& Candidate
				: Plan.LocalPodiumHeightCandidates)
			{
				if (Candidate.StructuralPodiumMainCoreCellId
						== StructuralPodiumMainCoreCellId
					&& Candidate.CandidateTopCourse == TopCourse
					&& Candidate.bAccepted && !Candidate.bActualBaseline)
				{
					EligibleProvinceIds.Add(Candidate.ProvinceId);
				}
			}
			TSet<int32> VisitedAtCourse;
			TArray<int32> SortedEligible = EligibleProvinceIds.Array();
			SortedEligible.Sort();
			for (const int32 StartProvinceId : SortedEligible)
			{
				if (VisitedAtCourse.Contains(StartProvinceId))
				{
					continue;
				}
				FLocalPodiumGroupOpportunity Opportunity;
				Opportunity.StructuralPodiumMainCoreCellId =
					StructuralPodiumMainCoreCellId;
				Opportunity.TopCourse = TopCourse;
				TArray<int32> Pending{StartProvinceId};
				while (!Pending.IsEmpty())
				{
					const int32 ProvinceId = Pending.Pop(EAllowShrinking::No);
					if (VisitedAtCourse.Contains(ProvinceId))
					{
						continue;
					}
					const FSupportProvinceDiagnostic* Province =
						Plan.SupportProvinces.FindByPredicate(
							[ProvinceId](const FSupportProvinceDiagnostic& Candidate)
							{
								return Candidate.ProvinceId == ProvinceId;
							});
					const FLocalPodiumHeightCandidateDiagnostic* Candidate =
						Plan.LocalPodiumHeightCandidates.FindByPredicate(
							[ProvinceId, TopCourse](
								const FLocalPodiumHeightCandidateDiagnostic& Entry)
							{
								return Entry.ProvinceId == ProvinceId
									&& Entry.CandidateTopCourse == TopCourse
									&& Entry.bAccepted;
							});
					if (Province == nullptr || Candidate == nullptr)
					{
						continue;
					}
					VisitedAtCourse.Add(ProvinceId);
					Opportunity.ComponentId = Province->ComponentId;
					Opportunity.ProvinceIds.Add(ProvinceId);
					for (const int32 AdjacentId : Province->AdjacentProvinceIds)
					{
						if (!EligibleProvinceIds.Contains(AdjacentId)
							|| VisitedAtCourse.Contains(AdjacentId))
						{
							continue;
						}
						const FLocalPodiumHeightCandidateDiagnostic* AdjacentCandidate =
							Plan.LocalPodiumHeightCandidates.FindByPredicate(
								[AdjacentId, TopCourse](
									const FLocalPodiumHeightCandidateDiagnostic& Entry)
								{
									return Entry.ProvinceId == AdjacentId
										&& Entry.CandidateTopCourse == TopCourse
										&& Entry.bAccepted;
								});
						const int32 GapUnits = AdjacentCandidate != nullptr
							? CandidateFootprintGapUnits(
								ProvinceId, *Candidate,
								AdjacentId, *AdjacentCandidate)
							: INDEX_NONE;
						if (AdjacentCandidate != nullptr
							&& GapUnits != INDEX_NONE
							&& (GapUnits + 1) * BlockUnitsCM
								<= 720.0 + GeometryToleranceCM
							&& Candidate->bRetainsHalfFirstRaisedFootprint
							&& AdjacentCandidate->bRetainsHalfFirstRaisedFootprint
							&& CandidateBridgeVoidClear(
								ProvinceId, *Candidate,
								AdjacentId, *AdjacentCandidate))
						{
							Pending.AddUnique(AdjacentId);
						}
					}
				}
				Opportunity.ProvinceIds.Sort();
				if (Opportunity.ProvinceIds.Num() >= 2)
				{
					Opportunities.Add(MoveTemp(Opportunity));
				}
			}
		}
		Opportunities.Sort([](
			const FLocalPodiumGroupOpportunity& A,
			const FLocalPodiumGroupOpportunity& B)
		{
			if (A.ProvinceIds.Num() != B.ProvinceIds.Num())
			{
				return A.ProvinceIds.Num() > B.ProvinceIds.Num();
			}
			if (A.TopCourse != B.TopCourse)
			{
				return A.TopCourse > B.TopCourse;
			}
			return A.ProvinceIds[0] < B.ProvinceIds[0];
		});
		TSet<int32> RaisedAssignedProvinceIds;
		for (const FLocalPodiumGroupOpportunity& Opportunity : Opportunities)
		{
			if (Opportunity.ProvinceIds.ContainsByPredicate(
				[&RaisedAssignedProvinceIds](const int32 ProvinceId)
				{
					return RaisedAssignedProvinceIds.Contains(ProvinceId);
				}))
			{
				continue;
			}
			for (const int32 ProvinceId : Opportunity.ProvinceIds)
			{
				SelectedTopByProvince.Add(ProvinceId, Opportunity.TopCourse);
				RaisedAssignedProvinceIds.Add(ProvinceId);
			}
		}
		for (const FSupportProvinceDiagnostic& Province : Plan.SupportProvinces)
		{
			if (!SelectedTopByProvince.Contains(Province.ProvinceId))
			{
				SelectedTopByProvince.Add(Province.ProvinceId,
					ActualTopByProvince.FindChecked(Province.ProvinceId));
			}
			FLocalPodiumHeightCandidateDiagnostic* SelectedCandidate =
				Plan.LocalPodiumHeightCandidates.FindByPredicate(
					[&Province, &SelectedTopByProvince](
						const FLocalPodiumHeightCandidateDiagnostic& Candidate)
					{
						return Candidate.ProvinceId == Province.ProvinceId
							&& Candidate.CandidateTopCourse
								== SelectedTopByProvince.FindChecked(Province.ProvinceId)
							&& Candidate.bAccepted;
					});
			if (SelectedCandidate == nullptr)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3LocalPodiumSelectedCandidateUnavailable:Province=%d:Top=%d"),
					Province.ProvinceId,
					SelectedTopByProvince.FindChecked(Province.ProvinceId));
				return false;
			}
			SelectedCandidate->bSelected = true;
			Canonical += FString::Printf(TEXT("|S:P=%d:TOP=%d"),
				Province.ProvinceId, SelectedCandidate->CandidateTopCourse);
		}

		TSet<int32> AssignedProvinceIds;
		for (const FSupportProvinceDiagnostic& StartProvince : Plan.SupportProvinces)
		{
			if (AssignedProvinceIds.Contains(StartProvince.ProvinceId))
			{
				continue;
			}
			FLocalPodiumHeightRegionDiagnostic& Region =
				Plan.LocalPodiumHeightRegions.AddDefaulted_GetRef();
			Region.RegionId = Plan.LocalPodiumHeightRegions.Num() - 1;
			Region.ComponentId = StartProvince.ComponentId;
			Region.StructuralPodiumMainCoreCellId =
				StructuralPodiumMainByProvince.FindChecked(StartProvince.ProvinceId);
			Region.ActualPodiumTopCourse = ActualTopByProvince.FindChecked(
				StartProvince.ProvinceId);
			Region.SelectedTopCourse = SelectedTopByProvince.FindChecked(
				StartProvince.ProvinceId);
			TArray<int32> Pending{StartProvince.ProvinceId};
			while (!Pending.IsEmpty())
			{
				const int32 ProvinceId = Pending.Pop(EAllowShrinking::No);
				if (AssignedProvinceIds.Contains(ProvinceId))
				{
					continue;
				}
				const FSupportProvinceDiagnostic* Province =
					Plan.SupportProvinces.FindByPredicate(
						[ProvinceId](const FSupportProvinceDiagnostic& Candidate)
						{
							return Candidate.ProvinceId == ProvinceId;
						});
				if (Province == nullptr
					|| Province->ComponentId != Region.ComponentId
					|| StructuralPodiumMainByProvince.FindChecked(ProvinceId)
						!= Region.StructuralPodiumMainCoreCellId
					|| SelectedTopByProvince.FindChecked(ProvinceId)
						!= Region.SelectedTopCourse
					|| ActualTopByProvince.FindChecked(ProvinceId)
						!= Region.ActualPodiumTopCourse)
				{
					continue;
				}
				AssignedProvinceIds.Add(ProvinceId);
				Region.ProvinceIds.Add(ProvinceId);
				Region.GroundBounds += Province->GroundBounds;
				for (const int32 AdjacentId : Province->AdjacentProvinceIds)
				{
					if (!AssignedProvinceIds.Contains(AdjacentId)
						&& SelectedTopByProvince.Contains(AdjacentId)
						&& StructuralPodiumMainByProvince.Contains(AdjacentId)
						&& StructuralPodiumMainByProvince.FindChecked(AdjacentId)
							== Region.StructuralPodiumMainCoreCellId
						&& SelectedTopByProvince.FindChecked(AdjacentId)
							== Region.SelectedTopCourse
						&& [&Plan, &CandidateFootprintGapUnits,
							&CandidateBridgeVoidClear, ProvinceId, AdjacentId,
							TopCourse = Region.SelectedTopCourse]()
						{
							const FLocalPodiumHeightCandidateDiagnostic* A =
								Plan.LocalPodiumHeightCandidates.FindByPredicate(
									[ProvinceId, TopCourse](
										const FLocalPodiumHeightCandidateDiagnostic& Candidate)
									{
										return Candidate.ProvinceId == ProvinceId
											&& Candidate.CandidateTopCourse == TopCourse
											&& Candidate.bSelected;
									});
							const FLocalPodiumHeightCandidateDiagnostic* B =
								Plan.LocalPodiumHeightCandidates.FindByPredicate(
									[AdjacentId, TopCourse](
										const FLocalPodiumHeightCandidateDiagnostic& Candidate)
									{
										return Candidate.ProvinceId == AdjacentId
											&& Candidate.CandidateTopCourse == TopCourse
											&& Candidate.bSelected;
									});
							const int32 GapUnits = A != nullptr && B != nullptr
								? CandidateFootprintGapUnits(
									ProvinceId, *A, AdjacentId, *B)
								: INDEX_NONE;
							return A != nullptr && B != nullptr
								&& GapUnits != INDEX_NONE
								&& (GapUnits + 1) * BlockUnitsCM
									<= 720.0 + GeometryToleranceCM
								&& A->bRetainsHalfFirstRaisedFootprint
								&& B->bRetainsHalfFirstRaisedFootprint
								&& CandidateBridgeVoidClear(
									ProvinceId, *A, AdjacentId, *B);
						}())
					{
						Pending.AddUnique(AdjacentId);
					}
				}
			}
			Region.ProvinceIds.Sort();
			Region.bRaisesActualPodium = Region.SelectedTopCourse
				> Region.ActualPodiumTopCourse;
			Plan.Summary.RaisedLocalPodiumHeightRegionCount +=
				Region.bRaisesActualPodium ? 1 : 0;
			Canonical += FString::Printf(
				TEXT("|R:%d:C=%d:M=%d:BASE=%d:TOP=%d:RAISE=%d"),
				Region.RegionId, Region.ComponentId,
				Region.StructuralPodiumMainCoreCellId,
				Region.ActualPodiumTopCourse, Region.SelectedTopCourse,
				Region.bRaisesActualPodium ? 1 : 0);
			for (const int32 ProvinceId : Region.ProvinceIds)
			{
				Canonical += FString::Printf(TEXT(":P%d"), ProvinceId);
			}
		}
		if (AssignedProvinceIds.Num() != Plan.SupportProvinces.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3LocalPodiumProvinceCoverageMismatch:Assigned=%d:Provinces=%d"),
				AssignedProvinceIds.Num(), Plan.SupportProvinces.Num());
			return false;
		}
		Plan.Summary.LocalPodiumHeightCandidateCount =
			Plan.LocalPodiumHeightCandidates.Num();
		Plan.Summary.LocalPodiumHeightRegionCount =
			Plan.LocalPodiumHeightRegions.Num();
		Plan.Summary.LocalPodiumHeightPlanHash = HashText(Canonical);
		return Plan.Summary.LocalPodiumHeightRegionCount > 0
			&& Plan.Summary.LocalPodiumHeightCandidateCount
				>= Plan.Summary.SupportProvinceCount
			&& Plan.Summary.LocalPodiumHeightPlanHash != 0;
	}

	double CellFaceAtOrAbove(const double Value)
	{
		const double FaceOffsetCM = BlockUnitsCM * 0.5;
		const int32 FaceIndex = FMath::CeilToInt(
			(Value - FaceOffsetCM - GeometryToleranceCM)
			/ static_cast<double>(BlockUnitsCM));
		return FaceIndex * static_cast<double>(BlockUnitsCM) + FaceOffsetCM;
	}

	double CellFaceAtOrBelow(const double Value)
	{
		const double FaceOffsetCM = BlockUnitsCM * 0.5;
		const int32 FaceIndex = FMath::FloorToInt(
			(Value - FaceOffsetCM + GeometryToleranceCM)
			/ static_cast<double>(BlockUnitsCM));
		return FaceIndex * static_cast<double>(BlockUnitsCM) + FaceOffsetCM;
	}

	bool MakeCellFaceSegments(
		const double RawMinimum,
		const double RawMaximum,
		TArray<FVector2D>& OutSegments)
	{
		OutSegments.Reset();
		const double Minimum = CellFaceAtOrAbove(RawMinimum);
		const double Maximum = CellFaceAtOrBelow(RawMaximum);
		if (Maximum - Minimum < BlockUnitsCM - GeometryToleranceCM)
		{
			return false;
		}
		const double MaximumLengthCM = MaximumHorizontalUnits
			* static_cast<double>(BlockUnitsCM);
		double Cursor = Minimum;
		while (Cursor < Maximum - GeometryToleranceCM)
		{
			const double End = FMath::Min(Cursor + MaximumLengthCM, Maximum);
			if (End - Cursor < BlockUnitsCM - GeometryToleranceCM)
			{
				return false;
			}
			OutSegments.Emplace(Cursor, End);
			Cursor = End;
		}
		return !OutSegments.IsEmpty();
	}

	const FABTSM73DAG5BV2Volume* FindVolume(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const int32 VolumeId)
	{
		return Silhouette.Volumes.FindByPredicate(
			[VolumeId](const FABTSM73DAG5BV2Volume& Volume)
			{
				return Volume.VolumeId == VolumeId;
			});
	}

	FDensityRecipe ResolveDensityRecipe(const FABTSM73BeamD0ResolvedProfile& Profile)
	{
		// One immutable recipe per resolved Profile/Tier. These are structural
		// subdivisions, not a candidate search and do not depend on Seed/Attempt.
		// The table is derived from the closed-form body term N~1/(H^2*V) and
		// two strict count anchors. Do not turn this back into a neighbourhood scan.
		static const int32 HorizontalByTier[] = {18, 12, 11, 8, 9, 8};
		static const int32 VerticalByTier[] = {18, 14, 10, 8, 7, 5};
		const int32 Tier = FMath::Clamp(Profile.DifficultyTier, 0, 5);
		FDensityRecipe Result;
		Result.HorizontalUnits = HorizontalByTier[Tier];
		Result.VerticalUnits = VerticalByTier[Tier];
		Result.RecipeId = Tier;
		// Higher-tier catalog windows are intentionally profile-specific.
		// Each exception below is one immutable Profile/Tier recipe derived
		// from the exact emitted count, never a Seed retry or neighbourhood scan.
		if (Tier == 1 && Profile.GameplayProfileId == FName(TEXT("TipOver")))
		{
			Result.HorizontalUnits = 10;
		}
		if (Tier == 4 && Profile.GameplayProfileId == FName(TEXT("ColumnBreak")))
		{
			Result.VerticalUnits = 6;
		}
		if (Tier == 3 && Profile.GameplayProfileId == FName(TEXT("TipOver")))
		{
			Result.VerticalUnits = 7;
		}
		if (Tier == 3 && Profile.GameplayProfileId == FName(TEXT("ColumnBreak")))
		{
			// The explicit grounded XY core moves the former H=8 fixture to 1472
			// members; H=9 still emits 1348, outside the non-overlapping
			// 800..1299 E4 window. H=10 is the closed monotone ladder between
			// E3 H11/V10 and E5 H9/V6, not a Seed retry or neighbourhood search.
			Result.HorizontalUnits = 10;
		}
		// This is an explicit Profile/Tier contract, never a Seed retry or a
		// neighbourhood search.
		if (Tier == 4 && Profile.GameplayProfileId == FName(TEXT("TipOver")))
		{
			Result.HorizontalUnits = 7;
			Result.VerticalUnits = 6;
		}
		if (Tier == 5 && Profile.GameplayProfileId == FName(TEXT("TipOver")))
		{
			Result.VerticalUnits = 4;
		}
		return Result;
	}

	bool MakeAxisGrid(
		const TArray<const FABTSM73DAG5BV2Volume*>& Volumes,
		const int32 Axis,
		const int32 MaximumCellUnits,
		TArray<int32>& OutGrid)
	{
		TArray<int32> Anchors;
		for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
		{
			const int32 Minimum = QMin(Volume->LocalBounds.Min[Axis]);
			const int32 Maximum = QMax(Volume->LocalBounds.Max[Axis]);
			if (Maximum <= Minimum)
			{
				return false;
			}
			Anchors.AddUnique(Minimum);
			Anchors.AddUnique(Maximum);
		}
		Anchors.Sort();
		if (Anchors.Num() < 2 || MaximumCellUnits < 1)
		{
			return false;
		}
		OutGrid.Reset();
		OutGrid.Add(Anchors[0]);
		for (int32 AnchorIndex = 1; AnchorIndex < Anchors.Num(); ++AnchorIndex)
		{
			const int32 Span = Anchors[AnchorIndex] - Anchors[AnchorIndex - 1];
			if (Span <= 0)
			{
				continue;
			}
			const int32 SegmentCount = FMath::DivideAndRoundUp(Span, MaximumCellUnits);
			const int32 Small = Span / SegmentCount;
			const int32 Remainder = Span % SegmentCount;
			if (Small < 1)
			{
				return false;
			}
			for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
			{
				OutGrid.Add(OutGrid.Last() + Small + (Segment < Remainder ? 1 : 0));
			}
		}
		return OutGrid.Num() >= 2 && OutGrid.Last() == Anchors.Last();
	}

	bool MakeBandSchedule(
		const int32 FinalBase,
		const int32 MaximumDelta,
		const TArray<int32>& ForcedBases,
		TArray<int32>& OutBases)
	{
		OutBases.Reset();
		if (FinalBase < 0 || MaximumDelta < 2 || (FinalBase & 1) != 0)
		{
			return false;
		}
		TArray<int32> Anchors = ForcedBases;
		Anchors.Add(0);
		Anchors.Add(FinalBase);
		Anchors.Sort();
		for (const int32 Anchor : Anchors)
		{
			// Every structural band must use the same X-even/Y-odd phase as the
			// continuous core. An odd base would place perpendicular members in
			// the same physical course and make the core/shell union penetrate.
			if ((Anchor & 1) != 0)
			{
				return false;
			}
		}
		for (int32 Index = Anchors.Num() - 1; Index > 0; --Index)
		{
			if (Anchors[Index] == Anchors[Index - 1])
			{
				Anchors.RemoveAt(Index);
			}
		}
		if (Anchors.IsEmpty() || Anchors[0] != 0 || Anchors.Last() != FinalBase)
		{
			return false;
		}
		OutBases.Add(0);
		for (int32 AnchorIndex = 1; AnchorIndex < Anchors.Num(); ++AnchorIndex)
		{
			const int32 Span = (Anchors[AnchorIndex] - Anchors[AnchorIndex - 1]) / 2;
			if (Span < 1)
			{
				return false;
			}
			const int32 MaximumHalfDelta = FMath::Max(1, MaximumDelta / 2);
			const int32 IntervalCount = FMath::DivideAndRoundUp(Span, MaximumHalfDelta);
			const int32 Small = Span / IntervalCount;
			const int32 Remainder = Span % IntervalCount;
			if (Small < 1)
			{
				return false;
			}
			for (int32 Index = 0; Index < IntervalCount; ++Index)
			{
				OutBases.Add(OutBases.Last()
					+ 2 * (Small + (Index < Remainder ? 1 : 0)));
			}
		}
		return OutBases.Last() == FinalBase;
	}

	bool SelectSharedRailStations(
		const TArray<int32>& NegativeGrid,
		const TArray<int32>& PositiveGrid,
		const double SemanticMinimumCM,
		const double SemanticMaximumCM,
		TArray<int32>& OutStations)
	{
		OutStations.Reset();
		if (NegativeGrid.Num() < 2 || PositiveGrid.Num() < 2)
		{
			return false;
		}
		const int32 Minimum = FMath::Max3(
			NegativeGrid[0], PositiveGrid[0], QMin(SemanticMinimumCM));
		const int32 Maximum = FMath::Min3(
			NegativeGrid.Last(), PositiveGrid.Last(), QMax(SemanticMaximumCM));
		TArray<int32> Candidates;
		for (int32 Station = Minimum; Station <= Maximum; ++Station)
		{
			if (!NegativeGrid.Contains(Station) && !PositiveGrid.Contains(Station))
			{
				Candidates.Add(Station);
			}
		}
		if (Candidates.Num() < 2)
		{
			return false;
		}
		OutStations.Add(Candidates[0]);
		for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		{
			// One block-width centre spacing is the exact non-penetration
			// boundary for two 36 cm rails; a larger arbitrary spacing would
			// reject narrow but physically valid shared courses.
			if (Candidates[Index] - Candidates[0] >= 1)
			{
				OutStations.Add(Candidates[Index]);
				break;
			}
		}
		return OutStations.Num() == 2;
	}

	FBox PlannedMemberBounds(const FPlannedMember& Member)
	{
		const FVector Center = (Member.LocalStart + Member.LocalEnd) * 0.5;
		FVector Extent(BlockUnitsCM * 0.5, BlockUnitsCM * 0.5, BlockUnitsCM * 0.5);
		const double Length = FVector::Distance(Member.LocalStart, Member.LocalEnd);
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X: Extent.X = Length * 0.5; break;
		case EABTSM73BeamAFrameAxis::Y: Extent.Y = Length * 0.5; break;
		case EABTSM73BeamAFrameAxis::Z: Extent.Z = Length * 0.5; break;
		default: break;
		}
		return FBox(Center - Extent, Center + Extent);
	}

	const FSharedCourseLanePlan* FindSharedLanePlan(
		const FPlan& Plan, const FPlannedMember& Member)
	{
		if (Member.SkeletonKind != ESkeletonMemberKind::SharedCourse)
		{
			return nullptr;
		}
		const FSharedCourseIntent* Intent = Plan.SharedCourseIntents.FindByPredicate(
			[&Member](const FSharedCourseIntent& Candidate)
			{
				return Candidate.SpanVolumeId == Member.OwnerId;
			});
		return Intent == nullptr ? nullptr : Intent->LanePlans.FindByPredicate(
			[&Member](const FSharedCourseLanePlan& Lane)
			{
				return Lane.CourseIndex == Member.CourseIndex
					&& Lane.LaneIndex == Member.SharedLaneIndex;
			});
	}

	void ExpandLogicalSharedMember(
		const FPlan& Plan, const int32 MemberIndex, TArray<int32>& OutMembers)
	{
		OutMembers.Reset();
		if (!Plan.Members.IsValidIndex(MemberIndex))
		{
			return;
		}
		const FPlannedMember& Member = Plan.Members[MemberIndex];
		if (const FSharedCourseLanePlan* Lane = FindSharedLanePlan(Plan, Member))
		{
			OutMembers = Lane->SegmentMemberIndices;
		}
		else
		{
			OutMembers.Add(MemberIndex);
		}
	}

	bool LogicalMembersHaveBearing(
		const FPlan& Plan, const int32 UpperMemberIndex,
		const int32 LowerMemberIndex, double& OutMaximumOverlapX,
		double& OutMaximumOverlapY)
	{
		OutMaximumOverlapX = 0.0;
		OutMaximumOverlapY = 0.0;
		TArray<int32> UpperMembers;
		TArray<int32> LowerMembers;
		ExpandLogicalSharedMember(Plan, UpperMemberIndex, UpperMembers);
		ExpandLogicalSharedMember(Plan, LowerMemberIndex, LowerMembers);
		for (const int32 UpperIndex : UpperMembers)
		{
			if (!Plan.Members.IsValidIndex(UpperIndex))
			{
				continue;
			}
			for (const int32 LowerIndex : LowerMembers)
			{
				if (!Plan.Members.IsValidIndex(LowerIndex))
				{
					continue;
				}
				const FBox UpperBounds = PlannedMemberBounds(Plan.Members[UpperIndex]);
				const FBox LowerBounds = PlannedMemberBounds(Plan.Members[LowerIndex]);
				const double OverlapX = SkeletonV3OverlapLength(
					UpperBounds.Min.X, UpperBounds.Max.X,
					LowerBounds.Min.X, LowerBounds.Max.X);
				const double OverlapY = SkeletonV3OverlapLength(
					UpperBounds.Min.Y, UpperBounds.Max.Y,
					LowerBounds.Min.Y, LowerBounds.Max.Y);
				OutMaximumOverlapX = FMath::Max(OutMaximumOverlapX, OverlapX);
				OutMaximumOverlapY = FMath::Max(OutMaximumOverlapY, OverlapY);
				if (OverlapX >= BlockUnitsCM - GeometryToleranceCM
					&& OverlapY >= BlockUnitsCM - GeometryToleranceCM
					&& Plan.Members[UpperIndex].RequiredLowerMemberIndices.Contains(
						LowerIndex))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool AddPlannedMember(
		FPlan& Plan,
		const EOwnerKind OwnerKind,
		const ESkeletonMemberKind SkeletonKind,
		const int32 OwnerId,
		const int32 ComponentId,
		const int32 SourceVolumeId,
		const int32 OriginCoreCellId,
		const int32 CourseIndex,
		const int32 StationA,
		const int32 StationB,
		const uint8 FaceMask,
		const EABTSM73BeamAFrameAxis Axis,
		const EABTSM73BeamAMemberRole Role,
		const FVector& Start,
		const FVector& End,
		FString& OutError)
	{
		const double Length = FVector::Distance(Start, End);
		const bool bLongSpanMember = SkeletonKind == ESkeletonMemberKind::SharedCourse
			|| SkeletonKind == ESkeletonMemberKind::CoreCourse;
		const double Maximum = Axis == EABTSM73BeamAFrameAxis::Z || bLongSpanMember
			? 720.0 : 648.0;
		if (!FMath::IsFinite(Length)
			|| Length + KINDA_SMALL_NUMBER < BlockUnitsCM
			|| Length > Maximum + KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3MemberSpanExceeded:Axis=%d:Length=%.3f"),
				static_cast<int32>(Axis), Length);
			return false;
		}
		FPlannedMember& Member = Plan.Members.AddDefaulted_GetRef();
		switch (SkeletonKind)
		{
		case ESkeletonMemberKind::CoreCourse:
		case ESkeletonMemberKind::SharedCourse:
		case ESkeletonMemberKind::BridgeDiaphragm:
			Member.ProducedStage = EABTSM73BeamC3GenerationStage::CoreAndShared;
			break;
		case ESkeletonMemberKind::ThroughCourse:
			Member.ProducedStage = EABTSM73BeamC3GenerationStage::CouplingCourses;
			break;
		case ESkeletonMemberKind::FacadeCourse:
		case ESkeletonMemberKind::ExteriorPost:
			Member.ProducedStage = EABTSM73BeamC3GenerationStage::CommonExteriorFrame;
			break;
		case ESkeletonMemberKind::FloorCourse:
		case ESkeletonMemberKind::RoofCourse:
			Member.ProducedStage = EABTSM73BeamC3GenerationStage::FloorInfillRoof;
			break;
		}
		Member.OwnerKind = OwnerKind;
		Member.SkeletonKind = SkeletonKind;
		Member.OwnerId = OwnerId;
		Member.ComponentId = ComponentId;
		Member.SourceVolumeId = SourceVolumeId;
		Member.OriginCoreCellId = OriginCoreCellId;
		Member.CourseIndex = CourseIndex;
		Member.StationA = StationA;
		Member.StationB = StationB;
		Member.FaceMask = FaceMask;
		Member.Axis = Axis;
		Member.Role = Role;
		Member.LocalStart = Start;
		Member.LocalEnd = End;
		Plan.Summary.MaximumMemberLengthCM = FMath::Max(
			Plan.Summary.MaximumMemberLengthCM, static_cast<float>(Length));
		if (Axis == EABTSM73BeamAFrameAxis::Z)
		{
			Plan.Summary.MaximumPostSegmentSpanCM = FMath::Max(
				Plan.Summary.MaximumPostSegmentSpanCM, static_cast<float>(Length));
		}
		return true;
	}

	void AddSeat(FPlan& Plan, const int32 UpperIndex, const int32 LowerIndex)
	{
		if (Plan.Members.IsValidIndex(UpperIndex)
			&& Plan.Members.IsValidIndex(LowerIndex)
			&& UpperIndex != LowerIndex)
		{
			Plan.Members[UpperIndex].RequiredLowerMemberIndices.AddUnique(LowerIndex);
		}
	}

	bool CompactPlanMembers(
		FPlan& Plan,
		const TSet<int32>& RemovedMembers,
		const TMap<int32, int32>& ReplacementMembers,
		FString& OutError)
	{
		if (RemovedMembers.IsEmpty())
		{
			return true;
		}
		TArray<int32> OldToNew;
		OldToNew.Init(INDEX_NONE, Plan.Members.Num());
		TArray<FPlannedMember> Compacted;
		Compacted.Reserve(Plan.Members.Num() - RemovedMembers.Num());
		for (int32 OldIndex = 0; OldIndex < Plan.Members.Num(); ++OldIndex)
		{
			if (!RemovedMembers.Contains(OldIndex))
			{
				OldToNew[OldIndex] = Compacted.Add(Plan.Members[OldIndex]);
			}
		}
		auto ResolveOldIndex = [&ReplacementMembers, &RemovedMembers](int32 Index) -> int32
		{
			TSet<int32> Visited;
			while (RemovedMembers.Contains(Index))
			{
				if (Visited.Contains(Index))
				{
					return INDEX_NONE;
				}
				Visited.Add(Index);
				const int32* Replacement = ReplacementMembers.Find(Index);
				if (Replacement == nullptr)
				{
					return INDEX_NONE;
				}
				Index = *Replacement;
			}
			return Index;
		};
		auto RemapArray = [&OldToNew, &ResolveOldIndex](TArray<int32>& Indices,
			const bool bPreserveSlots)
		{
			for (int32& Index : Indices)
			{
				const int32 Resolved = ResolveOldIndex(Index);
				Index = OldToNew.IsValidIndex(Resolved) ? OldToNew[Resolved] : INDEX_NONE;
			}
			if (!bPreserveSlots)
			{
				Indices.RemoveAll([](const int32 Index) { return Index == INDEX_NONE; });
				TSet<int32> Seen;
				Indices.RemoveAll([&Seen](const int32 Index)
				{
					if (Seen.Contains(Index))
					{
						return true;
					}
					Seen.Add(Index);
					return false;
				});
			}
		};
		for (FPlannedMember& Member : Compacted)
		{
			RemapArray(Member.RequiredLowerMemberIndices, false);
			RemapArray(Member.RequiredInwardMemberIndices, false);
		}
		for (FCoreCellPlan& Core : Plan.CoreCells)
		{
			RemapArray(Core.MemberIndices, true);
			if (Core.MemberIndices.Contains(INDEX_NONE))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreSlotRemovedWithoutReplacement:Core=%d"),
					Core.CoreCellId);
				return false;
			}
		}
		for (FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			RemapArray(Group.MemberIndices, false);
		}
		for (FSharedCourseIntent& Intent : Plan.SharedCourseIntents)
		{
			for (FSharedCourseLanePlan& Lane : Intent.LanePlans)
			{
				RemapArray(Lane.SegmentMemberIndices, false);
				TArray<int32> CrossSegment = {Lane.CrossCoreSegmentMemberIndex};
				RemapArray(CrossSegment, true);
				Lane.CrossCoreSegmentMemberIndex = CrossSegment[0];
				if (Lane.CrossCoreSegmentMemberIndex == INDEX_NONE)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedCrossSegmentRemoved:Span=%d:Course=%d:Lane=%d"),
						Intent.SpanVolumeId, Lane.CourseIndex, Lane.LaneIndex);
					return false;
				}
			}
		}
		Plan.Members = MoveTemp(Compacted);
		return true;
	}

	bool RebuildPlannedSeatDAG(FPlan& Plan, FString& OutError)
	{
		TArray<FBox> Bounds;
		TMap<int64, TArray<int32>> MembersByTop;
		const double SeatBucketScale = 1.0 / GeometryToleranceCM;
		Bounds.Reserve(Plan.Members.Num());
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			FPlannedMember& Member = Plan.Members[MemberIndex];
			Member.bRequiresGroundSeat = false;
			Member.RequiredLowerMemberIndices.Reset();
			const FBox MemberBounds = PlannedMemberBounds(Member);
			Bounds.Add(MemberBounds);
			MembersByTop.FindOrAdd(FMath::RoundToInt64(
				MemberBounds.Max.Z * SeatBucketScale)).Add(MemberIndex);
		}
		for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
		{
			FPlannedMember& Upper = Plan.Members[UpperIndex];
			double GroundZ = 0.0;
			if (Plan.Components.IsValidIndex(Upper.ComponentId))
			{
				GroundZ = Plan.Components[Upper.ComponentId].GroundPlaneZCM;
			}
			else if (Upper.OwnerKind == EOwnerKind::BuildingGroupShell
				&& Plan.BuildingGroups.IsValidIndex(Upper.OwnerId))
			{
				GroundZ = Plan.BuildingGroups[Upper.OwnerId].GroundPlaneZCM;
			}
			if (FMath::Abs(Bounds[UpperIndex].Min.Z - GroundZ) <= GeometryToleranceCM)
			{
				Upper.bRequiresGroundSeat = true;
				continue;
			}
			const int64 BottomBucket = FMath::RoundToInt64(
				Bounds[UpperIndex].Min.Z * SeatBucketScale);
			for (int64 BucketOffset = -1; BucketOffset <= 1; ++BucketOffset)
			{
				const TArray<int32>* LowerCandidates = MembersByTop.Find(
					BottomBucket + BucketOffset);
				if (LowerCandidates == nullptr)
				{
					continue;
				}
				for (const int32 LowerIndex : *LowerCandidates)
				{
					if (LowerIndex == UpperIndex
						|| FMath::Abs(Bounds[LowerIndex].Max.Z
							- Bounds[UpperIndex].Min.Z) > GeometryToleranceCM)
					{
						continue;
					}
					if (SkeletonV3OverlapLength(Bounds[LowerIndex].Min.X,
							Bounds[LowerIndex].Max.X, Bounds[UpperIndex].Min.X,
							Bounds[UpperIndex].Max.X) > GeometryToleranceCM
						&& SkeletonV3OverlapLength(Bounds[LowerIndex].Min.Y,
							Bounds[LowerIndex].Max.Y, Bounds[UpperIndex].Min.Y,
							Bounds[UpperIndex].Max.Y) > GeometryToleranceCM)
					{
						Upper.RequiredLowerMemberIndices.AddUnique(LowerIndex);
					}
				}
			}
			if (Upper.RequiredLowerMemberIndices.IsEmpty())
			{
				FString LowerDiagnostics;
				FString CollinearDiagnostics;
				for (int32 CandidateIndex = 0;
					CandidateIndex < Plan.Members.Num() && LowerDiagnostics.Len() < 1200;
					++CandidateIndex)
				{
					if (CandidateIndex == UpperIndex
						|| FMath::Abs(Bounds[CandidateIndex].Max.Z
							- Bounds[UpperIndex].Min.Z) > GeometryToleranceCM)
					{
						continue;
					}
					const double OverlapX = SkeletonV3OverlapLength(
						Bounds[CandidateIndex].Min.X, Bounds[CandidateIndex].Max.X,
						Bounds[UpperIndex].Min.X, Bounds[UpperIndex].Max.X);
					const double OverlapY = SkeletonV3OverlapLength(
						Bounds[CandidateIndex].Min.Y, Bounds[CandidateIndex].Max.Y,
						Bounds[UpperIndex].Min.Y, Bounds[UpperIndex].Max.Y);
					if (OverlapX > GeometryToleranceCM || OverlapY > GeometryToleranceCM)
					{
						const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
						LowerDiagnostics += FString::Printf(
							TEXT("|M%d:O%d:K%d:A%d:OX=%.1f:OY=%.1f:B=%.1f,%.1f..%.1f,%.1f"),
							CandidateIndex, static_cast<int32>(Candidate.OwnerKind),
							static_cast<int32>(Candidate.SkeletonKind),
							static_cast<int32>(Candidate.Axis), OverlapX, OverlapY,
							Bounds[CandidateIndex].Min.X, Bounds[CandidateIndex].Min.Y,
							Bounds[CandidateIndex].Max.X, Bounds[CandidateIndex].Max.Y);
					}
				}
				for (int32 CandidateIndex = 0;
					CandidateIndex < Plan.Members.Num() && CollinearDiagnostics.Len() < 1000;
					++CandidateIndex)
				{
					const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
					if (CandidateIndex == UpperIndex || Candidate.Axis != Upper.Axis
						|| Candidate.CourseIndex != Upper.CourseIndex)
					{
						continue;
					}
					const int32 AxisIndex = static_cast<int32>(Upper.Axis);
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					if (FMath::Abs(Bounds[CandidateIndex].GetCenter()[CrossAxisIndex]
							- Bounds[UpperIndex].GetCenter()[CrossAxisIndex])
						> GeometryToleranceCM)
					{
						continue;
					}
					const double Distance = FMath::Min(
						FMath::Abs(Bounds[CandidateIndex].Max[AxisIndex]
							- Bounds[UpperIndex].Min[AxisIndex]),
						FMath::Abs(Bounds[CandidateIndex].Min[AxisIndex]
							- Bounds[UpperIndex].Max[AxisIndex]));
					if (Distance <= BlockUnitsCM + GeometryToleranceCM)
					{
						CollinearDiagnostics += FString::Printf(
							TEXT("|M%d:O%d:K%d:Core=%d:B=%.1f..%.1f:D=%.1f"),
							CandidateIndex, static_cast<int32>(Candidate.OwnerKind),
							static_cast<int32>(Candidate.SkeletonKind),
							Candidate.OriginCoreCellId,
							Bounds[CandidateIndex].Min[AxisIndex],
							Bounds[CandidateIndex].Max[AxisIndex], Distance);
					}
				}
				OutError = FString::Printf(
					TEXT("BeamC3V3RebuiltSeatUnavailable:Member=%d:OwnerKind=%d:Owner=%d:Component=%d:Kind=%d:Axis=%d:Course=%d:Bounds=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f:LowerCandidates=%s:Collinear=%s"),
					UpperIndex, static_cast<int32>(Upper.OwnerKind), Upper.OwnerId,
					Upper.ComponentId, static_cast<int32>(Upper.SkeletonKind),
					static_cast<int32>(Upper.Axis), Upper.CourseIndex,
					Bounds[UpperIndex].Min.X, Bounds[UpperIndex].Min.Y,
					Bounds[UpperIndex].Min.Z, Bounds[UpperIndex].Max.X,
					Bounds[UpperIndex].Max.Y, Bounds[UpperIndex].Max.Z,
					*LowerDiagnostics, *CollinearDiagnostics);
				return false;
			}
		}

		// Stable height order makes every lower index precede its upper index.
		TArray<int32> Order;
		for (int32 Index = 0; Index < Plan.Members.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&Bounds](const int32 A, const int32 B)
		{
			if (!FMath::IsNearlyEqual(Bounds[A].Min.Z, Bounds[B].Min.Z,
				GeometryToleranceCM))
			{
				return Bounds[A].Min.Z < Bounds[B].Min.Z;
			}
			if (!FMath::IsNearlyEqual(Bounds[A].Max.Z, Bounds[B].Max.Z,
				GeometryToleranceCM))
			{
				return Bounds[A].Max.Z < Bounds[B].Max.Z;
			}
			return A < B;
		});
		TArray<int32> OldToNew;
		OldToNew.SetNum(Order.Num());
		TArray<FPlannedMember> Sorted;
		Sorted.Reserve(Order.Num());
		for (const int32 OldIndex : Order)
		{
			OldToNew[OldIndex] = Sorted.Add(Plan.Members[OldIndex]);
		}
		auto Remap = [&OldToNew](TArray<int32>& Indices)
		{
			for (int32& Index : Indices)
			{
				Index = OldToNew.IsValidIndex(Index) ? OldToNew[Index] : INDEX_NONE;
			}
			Indices.RemoveAll([](const int32 Index) { return Index == INDEX_NONE; });
		};
		for (FPlannedMember& Member : Sorted)
		{
			Remap(Member.RequiredLowerMemberIndices);
			Remap(Member.RequiredInwardMemberIndices);
			Member.RequiredLowerMemberIndices.Sort();
			Member.RequiredInwardMemberIndices.Sort();
		}
		for (FCoreCellPlan& Core : Plan.CoreCells)
		{
			Remap(Core.MemberIndices);
		}
		for (FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			Remap(Group.MemberIndices);
		}
		for (FSharedCourseIntent& Intent : Plan.SharedCourseIntents)
		{
			for (FSharedCourseLanePlan& Lane : Intent.LanePlans)
			{
				Remap(Lane.SegmentMemberIndices);
				TArray<int32> CrossSegment = {Lane.CrossCoreSegmentMemberIndex};
				Remap(CrossSegment);
				Lane.CrossCoreSegmentMemberIndex = CrossSegment.IsEmpty()
					? INDEX_NONE : CrossSegment[0];
			}
		}
		Plan.Members = MoveTemp(Sorted);
		for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
		{
			for (const int32 LowerIndex : Plan.Members[UpperIndex].RequiredLowerMemberIndices)
			{
				if (LowerIndex >= UpperIndex)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SeatTopologicalOrderInvalid:Lower=%d:Upper=%d"),
						LowerIndex, UpperIndex);
					return false;
				}
			}
		}
		for (FComponentPlan& Component : Plan.Components)
		{
			Component.FirstPlannedMemberIndex = INDEX_NONE;
			Component.PlannedMemberCount = 0;
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				if (Plan.Members[MemberIndex].ComponentId == Component.ComponentId)
				{
					Component.FirstPlannedMemberIndex = Component.FirstPlannedMemberIndex == INDEX_NONE
						? MemberIndex : Component.FirstPlannedMemberIndex;
					++Component.PlannedMemberCount;
				}
			}
		}
		return true;
	}

	bool ValidateCompositeCoreContract(FPlan& Plan, FString& OutError)
	{
		Plan.Summary.CompositeLaneConflictCount = 0;
		Plan.Summary.CrossCoreBearingContactCount = 0;
		TSet<int32> CompositeGroups;
		for (FCoreCellPlan& Core : Plan.CoreCells)
		{
			Core.CrossCoreBearingContactCount = 0;
			if (Core.CompositeCoreGroupId != INDEX_NONE)
			{
				CompositeGroups.Add(Core.CompositeCoreGroupId);
			}
		}
		Plan.Summary.CompositeCoreGroupCount = CompositeGroups.Num();

		for (int32 AIndex = 0; AIndex < Plan.Members.Num(); ++AIndex)
		{
			const FPlannedMember& A = Plan.Members[AIndex];
			if (A.SkeletonKind != ESkeletonMemberKind::CoreCourse)
			{
				continue;
			}
			const FBox ABounds = PlannedMemberBounds(A);
			for (int32 BIndex = AIndex + 1; BIndex < Plan.Members.Num(); ++BIndex)
			{
				const FPlannedMember& B = Plan.Members[BIndex];
				if (B.SkeletonKind != ESkeletonMemberKind::CoreCourse
					|| A.OriginCoreCellId == B.OriginCoreCellId
					|| A.ComponentId != B.ComponentId || A.CourseIndex != B.CourseIndex
					|| A.Axis != B.Axis)
				{
					continue;
				}
				const FBox BBounds = PlannedMemberBounds(B);
				if (SkeletonV3OverlapLength(ABounds.Min.X, ABounds.Max.X,
						BBounds.Min.X, BBounds.Max.X) > GeometryToleranceCM
					&& SkeletonV3OverlapLength(ABounds.Min.Y, ABounds.Max.Y,
						BBounds.Min.Y, BBounds.Max.Y) > GeometryToleranceCM
					&& SkeletonV3OverlapLength(ABounds.Min.Z, ABounds.Max.Z,
						BBounds.Min.Z, BBounds.Max.Z) > GeometryToleranceCM)
				{
					++Plan.Summary.CompositeLaneConflictCount;
					OutError = FString::Printf(
						TEXT("CompositeCoreLaneConflict:MemberA=%d:CoreA=%d:MemberB=%d:CoreB=%d:Course=%d:Axis=%d"),
						AIndex, A.OriginCoreCellId, BIndex, B.OriginCoreCellId,
						A.CourseIndex, static_cast<int32>(A.Axis));
					return false;
				}
			}
		}

		for (FCoreCellPlan& Child : Plan.CoreCells)
		{
			if (Child.HierarchyRole != ECoreHierarchyRole::TowerChild)
			{
				continue;
			}
			if (!Plan.CoreCells.IsValidIndex(Child.PodiumMainCoreCellId))
			{
				OutError = FString::Printf(
					TEXT("CompositeCoreParentInvalid:Child=%d:Parent=%d"),
					Child.CoreCellId, Child.PodiumMainCoreCellId);
				return false;
			}
			const FCoreCellPlan& Main = Plan.CoreCells[Child.PodiumMainCoreCellId];
			bool bXLowerYUpper = false;
			bool bYLowerXUpper = false;
			for (const FPlannedMember& Upper : Plan.Members)
			{
				if (Upper.SkeletonKind != ESkeletonMemberKind::CoreCourse
					|| (Upper.OriginCoreCellId != Child.CoreCellId
						&& Upper.OriginCoreCellId != Main.CoreCellId))
				{
					continue;
				}
				for (const int32 LowerIndex : Upper.RequiredLowerMemberIndices)
				{
					if (!Plan.Members.IsValidIndex(LowerIndex))
					{
						continue;
					}
					const FPlannedMember& Lower = Plan.Members[LowerIndex];
					const bool bParentChildPair =
						(Lower.OriginCoreCellId == Child.CoreCellId
							&& Upper.OriginCoreCellId == Main.CoreCellId)
						|| (Lower.OriginCoreCellId == Main.CoreCellId
							&& Upper.OriginCoreCellId == Child.CoreCellId);
					if (!bParentChildPair
						|| Lower.SkeletonKind != ESkeletonMemberKind::CoreCourse)
					{
						continue;
					}
					++Child.CrossCoreBearingContactCount;
					++Plan.Summary.CrossCoreBearingContactCount;
					bXLowerYUpper |= Lower.Axis == EABTSM73BeamAFrameAxis::X
						&& Upper.Axis == EABTSM73BeamAFrameAxis::Y;
					bYLowerXUpper |= Lower.Axis == EABTSM73BeamAFrameAxis::Y
						&& Upper.Axis == EABTSM73BeamAFrameAxis::X;
				}
			}
			const int32 CommonTopCourse = FMath::Min(
				Child.TopCourseIndex, Main.TopCourseIndex);
			if (Child.CrossCoreBearingContactCount > 0
				&& CommonTopCourse >= 3
				&& (!bXLowerYUpper || !bYLowerXUpper))
			{
				OutError = FString::Printf(
					TEXT("CompositeCoreBearingMissing:Child=%d:Main=%d:Contacts=%d:XtoY=%d:YtoX=%d:CommonTop=%d"),
					Child.CoreCellId, Main.CoreCellId,
					Child.CrossCoreBearingContactCount,
					bXLowerYUpper ? 1 : 0, bYLowerXUpper ? 1 : 0,
					CommonTopCourse);
				return false;
			}
		}
		return true;
	}

	bool BuildPodiumCoreCoverageDiagnostics(FPlan& Plan, FString& OutError)
	{
		Plan.PodiumCoverageDiagnostics.Reset();
		Plan.PodiumSourceCoverageDiagnostics.Reset();
		Plan.PodiumUncoveredIslandDiagnostics.Reset();
		Plan.PodiumMainSelectionDiagnostics.Reset();
		Plan.PodiumMainOverlapDiagnostics.Reset();
		Plan.Summary.PodiumCoverageDiagnosticCount = 0;
		Plan.Summary.PodiumUncoveredCellCount = 0;
		Plan.Summary.UncoveredPodiumSupportAnchorCount = 0;
		Plan.Summary.MinimumPodiumMainCoverageRatio = 1.0;
		Plan.Summary.MinimumPodiumAnyCoreCoverageRatio = 1.0;
		Plan.Summary.MaximumPodiumCorelessRadiusCM = 0.0;
		Plan.Summary.MaximumPodiumCentroidToNearestCoreCM = 0.0;

		auto ContainsXY = [](const FBox& Bounds, const FVector2D& Point)
		{
			return Point.X >= Bounds.Min.X - GeometryToleranceCM
				&& Point.X <= Bounds.Max.X + GeometryToleranceCM
				&& Point.Y >= Bounds.Min.Y - GeometryToleranceCM
				&& Point.Y <= Bounds.Max.Y + GeometryToleranceCM;
		};
		auto DistanceToXY = [](const FBox& Bounds, const FVector2D& Point)
		{
			const double DX = FMath::Max3(
				Bounds.Min.X - Point.X, 0.0, Point.X - Bounds.Max.X);
			const double DY = FMath::Max3(
				Bounds.Min.Y - Point.Y, 0.0, Point.Y - Bounds.Max.Y);
			return FMath::Sqrt(DX * DX + DY * DY);
		};

		for (const FCoreMergeRegionPlan& Region : Plan.CoreMergeRegions)
		{
			FPodiumCoreCoverageDiagnostic& Diagnostic =
				Plan.PodiumCoverageDiagnostics.AddDefaulted_GetRef();
			Diagnostic.RegionId = Region.RegionId;
			Diagnostic.ComponentId = Region.ComponentId;
			for (const FBox& SourceBounds : Region.GroundSourceBounds)
			{
				Diagnostic.PodiumBounds += SourceBounds;
			}

			TArray<const FCoreCellPlan*> MainCores;
			TArray<const FCoreCellPlan*> AllCores;
			for (const FCoreCellPlan& Core : Plan.CoreCells)
			{
				if (Core.CoreMergeRegionId != Region.RegionId)
				{
					continue;
				}
				AllCores.Add(&Core);
				if (Core.HierarchyRole == ECoreHierarchyRole::PodiumMain)
				{
					MainCores.Add(&Core);
				}
			}
			Diagnostic.PodiumMainCount = MainCores.Num();
			Diagnostic.GroundedCoreCount = AllCores.Num();
			if (!Diagnostic.PodiumBounds.IsValid || AllCores.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3PodiumCoverageSourceInvalid:Region=%d:Bounds=%d:Cores=%d"),
					Region.RegionId, Diagnostic.PodiumBounds.IsValid ? 1 : 0,
					AllCores.Num());
				return false;
			}
			for (const FCoreCellPlan* Core : MainCores)
			{
				if (Core != nullptr)
				{
					Diagnostic.MainCoreUnionBounds += Core->LocalBounds;
				}
			}
			if (Diagnostic.MainCoreUnionBounds.IsValid)
			{
				Diagnostic.MainCoreBoundaryInsetsCM = FVector4(
					FMath::Max(0.0, Diagnostic.MainCoreUnionBounds.Min.X
						- Diagnostic.PodiumBounds.Min.X),
					FMath::Max(0.0, Diagnostic.PodiumBounds.Max.X
						- Diagnostic.MainCoreUnionBounds.Max.X),
					FMath::Max(0.0, Diagnostic.MainCoreUnionBounds.Min.Y
						- Diagnostic.PodiumBounds.Min.Y),
					FMath::Max(0.0, Diagnostic.PodiumBounds.Max.Y
						- Diagnostic.MainCoreUnionBounds.Max.Y));
			}

			const int32 MinimumXCell = FMath::FloorToInt(
				Diagnostic.PodiumBounds.Min.X / BlockUnitsCM);
			const int32 MaximumXCell = FMath::CeilToInt(
				Diagnostic.PodiumBounds.Max.X / BlockUnitsCM) - 1;
			const int32 MinimumYCell = FMath::FloorToInt(
				Diagnostic.PodiumBounds.Min.Y / BlockUnitsCM);
			const int32 MaximumYCell = FMath::CeilToInt(
				Diagnostic.PodiumBounds.Max.Y / BlockUnitsCM) - 1;
			FVector2D PodiumCentroid = FVector2D::ZeroVector;
			TArray<FVector2D> PodiumCells;
			TSet<FIntPoint> UncoveredCells;
			for (int32 XCell = MinimumXCell; XCell <= MaximumXCell; ++XCell)
			{
				for (int32 YCell = MinimumYCell; YCell <= MaximumYCell; ++YCell)
				{
					const FVector2D Point(
						(XCell + 0.5) * BlockUnitsCM,
						(YCell + 0.5) * BlockUnitsCM);
					if (!Region.GroundSourceBounds.ContainsByPredicate(
						[&ContainsXY, &Point](const FBox& Bounds)
						{
							return ContainsXY(Bounds, Point);
						}))
					{
						continue;
					}
					++Diagnostic.TotalPodiumCellCount;
					PodiumCentroid += Point;
					PodiumCells.Add(Point);
					const bool bMainCovered = MainCores.ContainsByPredicate(
						[&ContainsXY, &Point](const FCoreCellPlan* Core)
						{
							return Core != nullptr && ContainsXY(Core->LocalBounds, Point);
						});
					const bool bAnyCovered = AllCores.ContainsByPredicate(
						[&ContainsXY, &Point](const FCoreCellPlan* Core)
						{
							return Core != nullptr && ContainsXY(Core->LocalBounds, Point);
						});
					Diagnostic.MainCoveredCellCount += bMainCovered ? 1 : 0;
					Diagnostic.AnyCoreCoveredCellCount += bAnyCovered ? 1 : 0;
					if (!bAnyCovered)
					{
						UncoveredCells.Add(FIntPoint(XCell, YCell));
						double NearestCoreCM = TNumericLimits<double>::Max();
						for (const FCoreCellPlan* Core : AllCores)
						{
							NearestCoreCM = FMath::Min(
								NearestCoreCM, DistanceToXY(Core->LocalBounds, Point));
						}
						Diagnostic.MaximumCorelessRadiusCM = FMath::Max(
							Diagnostic.MaximumCorelessRadiusCM, NearestCoreCM);
					}
				}
			}
			if (Diagnostic.TotalPodiumCellCount <= 0)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3PodiumCoverageRasterEmpty:Region=%d"),
					Region.RegionId);
				return false;
			}
			Diagnostic.AnyCoreUncoveredCellCount =
				Diagnostic.TotalPodiumCellCount
				- Diagnostic.AnyCoreCoveredCellCount;
			Diagnostic.MainCoverageRatio =
				static_cast<double>(Diagnostic.MainCoveredCellCount)
				/ Diagnostic.TotalPodiumCellCount;
			Diagnostic.AnyCoreCoverageRatio =
				static_cast<double>(Diagnostic.AnyCoreCoveredCellCount)
				/ Diagnostic.TotalPodiumCellCount;
			PodiumCentroid /= Diagnostic.TotalPodiumCellCount;
			Diagnostic.PodiumSupportAnchorCM = PodiumCells[0];
			double BestAnchorDistance = FVector2D::DistSquared(
				Diagnostic.PodiumSupportAnchorCM, PodiumCentroid);
			for (const FVector2D& Point : PodiumCells)
			{
				const double Distance = FVector2D::DistSquared(Point, PodiumCentroid);
				if (Distance < BestAnchorDistance - UE_DOUBLE_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(Distance, BestAnchorDistance)
						&& (Point.Y < Diagnostic.PodiumSupportAnchorCM.Y
							|| (FMath::IsNearlyEqual(
								Point.Y, Diagnostic.PodiumSupportAnchorCM.Y)
								&& Point.X < Diagnostic.PodiumSupportAnchorCM.X))))
				{
					Diagnostic.PodiumSupportAnchorCM = Point;
					BestAnchorDistance = Distance;
				}
			}
			Diagnostic.bPodiumSupportAnchorCovered =
				MainCores.ContainsByPredicate(
					[&ContainsXY, &Diagnostic](const FCoreCellPlan* Core)
					{
						return Core != nullptr && ContainsXY(
							Core->LocalBounds, Diagnostic.PodiumSupportAnchorCM);
					});
			Diagnostic.PodiumCentroidToNearestCoreCM =
				TNumericLimits<double>::Max();
			for (const FCoreCellPlan* Core : AllCores)
			{
				Diagnostic.PodiumCentroidToNearestCoreCM = FMath::Min(
					Diagnostic.PodiumCentroidToNearestCoreCM,
					DistanceToXY(Core->LocalBounds, PodiumCentroid));
			}

			for (int32 SourceIndex = 0;
				SourceIndex < Region.GroundSourceBounds.Num(); ++SourceIndex)
			{
				FPodiumSourceCoverageDiagnostic& SourceDiagnostic =
					Plan.PodiumSourceCoverageDiagnostics.AddDefaulted_GetRef();
				SourceDiagnostic.RegionId = Region.RegionId;
				SourceDiagnostic.ComponentId = Region.ComponentId;
				SourceDiagnostic.SourceVolumeId = Region.SourceVolumeIds.IsValidIndex(
					SourceIndex) ? Region.SourceVolumeIds[SourceIndex] : INDEX_NONE;
				SourceDiagnostic.OriginalGroundComponentId =
					Region.SourceOriginalGroundComponentIds.IsValidIndex(SourceIndex)
						? Region.SourceOriginalGroundComponentIds[SourceIndex] : INDEX_NONE;
				SourceDiagnostic.SourceBounds = Region.GroundSourceBounds[SourceIndex];
				for (const FVector2D& Point : PodiumCells)
				{
					if (!ContainsXY(SourceDiagnostic.SourceBounds, Point))
					{
						continue;
					}
					++SourceDiagnostic.TotalCellCount;
					const bool bMainCovered = MainCores.ContainsByPredicate(
						[&ContainsXY, &Point](const FCoreCellPlan* Core)
						{
							return Core != nullptr
								&& ContainsXY(Core->LocalBounds, Point);
						});
					const bool bAnyCovered = AllCores.ContainsByPredicate(
						[&ContainsXY, &Point](const FCoreCellPlan* Core)
						{
							return Core != nullptr
								&& ContainsXY(Core->LocalBounds, Point);
						});
					SourceDiagnostic.MainCoveredCellCount += bMainCovered ? 1 : 0;
					SourceDiagnostic.AnyCoreCoveredCellCount += bAnyCovered ? 1 : 0;
				}
				SourceDiagnostic.UncoveredCellCount =
					SourceDiagnostic.TotalCellCount
						- SourceDiagnostic.AnyCoreCoveredCellCount;
			}

			int32 IslandId = 0;
			while (!UncoveredCells.IsEmpty())
			{
				const FIntPoint Start = *UncoveredCells.CreateConstIterator();
				UncoveredCells.Remove(Start);
				TArray<FIntPoint> Queue{Start};
				FPodiumUncoveredIslandDiagnostic& Island =
					Plan.PodiumUncoveredIslandDiagnostics.AddDefaulted_GetRef();
				Island.RegionId = Region.RegionId;
				Island.ComponentId = Region.ComponentId;
				Island.IslandId = IslandId++;
				for (int32 Head = 0; Head < Queue.Num(); ++Head)
				{
					const FIntPoint Cell = Queue[Head];
					++Island.CellCount;
					Island.Bounds += FVector2D(
						Cell.X * BlockUnitsCM, Cell.Y * BlockUnitsCM);
					Island.Bounds += FVector2D(
						(Cell.X + 1) * BlockUnitsCM,
						(Cell.Y + 1) * BlockUnitsCM);
					Island.BoundaryDirectionMask |= Cell.X == MinimumXCell ? 1u : 0u;
					Island.BoundaryDirectionMask |= Cell.X == MaximumXCell ? 2u : 0u;
					Island.BoundaryDirectionMask |= Cell.Y == MinimumYCell ? 4u : 0u;
					Island.BoundaryDirectionMask |= Cell.Y == MaximumYCell ? 8u : 0u;
					const FIntPoint Neighbours[] = {
						FIntPoint(Cell.X - 1, Cell.Y),
						FIntPoint(Cell.X + 1, Cell.Y),
						FIntPoint(Cell.X, Cell.Y - 1),
						FIntPoint(Cell.X, Cell.Y + 1)};
					for (const FIntPoint& Neighbour : Neighbours)
					{
						if (UncoveredCells.Remove(Neighbour) > 0)
						{
							Queue.Add(Neighbour);
						}
					}
				}
			}

			for (const FCoreCellPlan* Core : MainCores)
			{
				if (Core == nullptr)
				{
					continue;
				}
				FPodiumMainSelectionDiagnostic& Selection =
					Plan.PodiumMainSelectionDiagnostics.AddDefaulted_GetRef();
				Selection.RegionId = Region.RegionId;
				Selection.ComponentId = Region.ComponentId;
				Selection.CoreCellId = Core->CoreCellId;
				Selection.CoreBounds = Core->LocalBounds;
				Selection.bCoversPodiumSupportAnchor = ContainsXY(
					Core->LocalBounds, Diagnostic.PodiumSupportAnchorCM);
				for (const FVector2D& Point : PodiumCells)
				{
					Selection.CoveredPodiumCellCount +=
						ContainsXY(Core->LocalBounds, Point) ? 1 : 0;
				}
				for (int32 SourceIndex = 0;
					SourceIndex < Region.GroundSourceBounds.Num(); ++SourceIndex)
				{
					const FBox& Source = Region.GroundSourceBounds[SourceIndex];
					const double XOverlap = FMath::Min(
						Core->LocalBounds.Max.X, Source.Max.X) - FMath::Max(
						Core->LocalBounds.Min.X, Source.Min.X);
					const double YOverlap = FMath::Min(
						Core->LocalBounds.Max.Y, Source.Max.Y) - FMath::Max(
						Core->LocalBounds.Min.Y, Source.Min.Y);
					if (XOverlap > GeometryToleranceCM
						&& YOverlap > GeometryToleranceCM
						&& Region.SourceVolumeIds.IsValidIndex(SourceIndex))
					{
						Selection.CoveredGroundSourceVolumeIds.Add(
							Region.SourceVolumeIds[SourceIndex]);
					}
				}
				for (const FHighProjectionRegionPlan& Projection
					: Plan.HighProjectionRegions)
				{
					if (Projection.ComponentId != Region.ComponentId)
					{
						continue;
					}
					const bool bInteriorX = Core->XStations.ContainsByPredicate(
						[&Projection](const int32 Station)
						{
							const double X = Station * static_cast<double>(BlockUnitsCM);
							return X > Projection.LocalBounds.Min.X
								+ GeometryToleranceCM
								&& X < Projection.LocalBounds.Max.X
									- GeometryToleranceCM;
						});
					const bool bInteriorY = Core->YStations.ContainsByPredicate(
						[&Projection](const int32 Station)
						{
							const double Y = Station * static_cast<double>(BlockUnitsCM);
							return Y > Projection.LocalBounds.Min.Y
								+ GeometryToleranceCM
								&& Y < Projection.LocalBounds.Max.Y
									- GeometryToleranceCM;
						});
					if (bInteriorX && bInteriorY)
					{
						Selection.CoveredHighProjectionRegionIds.Add(
							Projection.RegionId);
					}
				}
				Selection.SelectionReason = FString::Printf(
					TEXT("HighProjectionCoverage=%d;Anchor=%d;PodiumCells=%d"),
					Selection.CoveredHighProjectionRegionIds.Num(),
					Selection.bCoversPodiumSupportAnchor ? 1 : 0,
					Selection.CoveredPodiumCellCount);
			}

			for (int32 First = 0; First < MainCores.Num(); ++First)
			{
				for (int32 Second = First + 1; Second < MainCores.Num(); ++Second)
				{
					const FCoreCellPlan* A = MainCores[First];
					const FCoreCellPlan* B = MainCores[Second];
					if (A == nullptr || B == nullptr)
					{
						continue;
					}
					FPodiumMainOverlapDiagnostic& Overlap =
						Plan.PodiumMainOverlapDiagnostics.AddDefaulted_GetRef();
					Overlap.RegionId = Region.RegionId;
					Overlap.ComponentId = Region.ComponentId;
					Overlap.FirstCoreCellId = A->CoreCellId;
					Overlap.SecondCoreCellId = B->CoreCellId;
					Overlap.XOverlapCM = FMath::Max(0.0, FMath::Min(
						A->LocalBounds.Max.X, B->LocalBounds.Max.X) - FMath::Max(
						A->LocalBounds.Min.X, B->LocalBounds.Min.X));
					Overlap.YOverlapCM = FMath::Max(0.0, FMath::Min(
						A->LocalBounds.Max.Y, B->LocalBounds.Max.Y) - FMath::Max(
						A->LocalBounds.Min.Y, B->LocalBounds.Min.Y));
					Overlap.ProjectedOverlapAreaCM2 =
						Overlap.XOverlapCM * Overlap.YOverlapCM;
				}
			}

			if (!FMath::IsFinite(Diagnostic.MainCoverageRatio)
				|| !FMath::IsFinite(Diagnostic.AnyCoreCoverageRatio)
				|| !FMath::IsFinite(Diagnostic.MaximumCorelessRadiusCM)
				|| !FMath::IsFinite(Diagnostic.PodiumCentroidToNearestCoreCM)
				|| Diagnostic.MainCoveredCellCount > Diagnostic.TotalPodiumCellCount
				|| Diagnostic.AnyCoreCoveredCellCount > Diagnostic.TotalPodiumCellCount)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3PodiumCoverageAccountingInvalid:Region=%d"),
					Region.RegionId);
				return false;
			}
			Plan.Summary.PodiumUncoveredCellCount +=
				Diagnostic.AnyCoreUncoveredCellCount;
			Plan.Summary.UncoveredPodiumSupportAnchorCount +=
				Diagnostic.PodiumMainCount > 0
					&& !Diagnostic.bPodiumSupportAnchorCovered ? 1 : 0;
			Plan.Summary.MinimumPodiumMainCoverageRatio = FMath::Min(
				Plan.Summary.MinimumPodiumMainCoverageRatio,
				Diagnostic.MainCoverageRatio);
			Plan.Summary.MinimumPodiumAnyCoreCoverageRatio = FMath::Min(
				Plan.Summary.MinimumPodiumAnyCoreCoverageRatio,
				Diagnostic.AnyCoreCoverageRatio);
			Plan.Summary.MaximumPodiumCorelessRadiusCM = FMath::Max(
				Plan.Summary.MaximumPodiumCorelessRadiusCM,
				Diagnostic.MaximumCorelessRadiusCM);
			Plan.Summary.MaximumPodiumCentroidToNearestCoreCM = FMath::Max(
				Plan.Summary.MaximumPodiumCentroidToNearestCoreCM,
				Diagnostic.PodiumCentroidToNearestCoreCM);
		}
		Plan.Summary.PodiumCoverageDiagnosticCount =
			Plan.PodiumCoverageDiagnostics.Num();
		return Plan.Summary.PodiumCoverageDiagnosticCount
			== Plan.CoreMergeRegions.Num();
	}

	bool BuildHighProjectionDiagnostics(
		const TArray<FRoot>& Roots,
		FPlan& Plan,
		FString& OutError)
	{
		Plan.HighProjectionSeedDiagnostics.Reset();
		Plan.HighProjectionAdjacencyDiagnostics.Reset();
		Plan.HighProjectionSliceComponentDiagnostics.Reset();
		Plan.HighProjectionSplitDiagnostics.Reset();
		Plan.HighProjectionBranchBindingDiagnostics.Reset();

		auto XYConnected = [](const FBox& A, const FBox& B,
			double& OutXOverlap, double& OutYOverlap,
			bool& bOutPositiveArea, bool& bOutFullEdge)
		{
			OutXOverlap = SkeletonV3OverlapLength(
				A.Min.X, A.Max.X, B.Min.X, B.Max.X);
			OutYOverlap = SkeletonV3OverlapLength(
				A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
			bOutPositiveArea = OutXOverlap > WitnessToleranceCM
				&& OutYOverlap > WitnessToleranceCM;
			bOutFullEdge = ((FMath::Abs(A.Max.X - B.Min.X)
					<= WitnessToleranceCM
					|| FMath::Abs(B.Max.X - A.Min.X)
						<= WitnessToleranceCM)
					&& OutYOverlap > WitnessToleranceCM)
				|| ((FMath::Abs(A.Max.Y - B.Min.Y)
						<= WitnessToleranceCM
					|| FMath::Abs(B.Max.Y - A.Min.Y)
						<= WitnessToleranceCM)
					&& OutXOverlap > WitnessToleranceCM);
			return bOutPositiveArea || bOutFullEdge;
		};

		for (int32 ComponentId = 0; ComponentId < Roots.Num(); ++ComponentId)
		{
			const FRoot& Root = Roots[ComponentId];
			TArray<const FHighProjectionRegionPlan*> Regions;
			for (const FHighProjectionRegionPlan& Region : Plan.HighProjectionRegions)
			{
				if (Region.ComponentId == ComponentId)
				{
					Regions.Add(&Region);
				}
			}
			if (Regions.IsEmpty())
			{
				continue;
			}
			const int32 PodiumTopCourse = Regions[0]->PodiumTopCourse;
			const double PodiumTopZ = Root.GroundZCM
				+ PodiumTopCourse * BlockUnitsCM;
			TArray<const FABTSM73DAG5BV2Volume*> ProjectionSeeds = Root.BodyVolumes;
			ProjectionSeeds.Append(Root.CrownVolumes);
			ProjectionSeeds.RemoveAll([PodiumTopZ](
				const FABTSM73DAG5BV2Volume* Volume)
			{
				return Volume == nullptr
					|| Volume->DerivationPath.StartsWith(TEXT("CoupledGround/"))
					|| Volume->LocalBounds.Max.Z
						< PodiumTopZ + WitnessToleranceCM;
			});
			ProjectionSeeds.Sort([](
				const FABTSM73DAG5BV2Volume& A,
				const FABTSM73DAG5BV2Volume& B)
			{
				return A.VolumeId < B.VolumeId;
			});
			for (const FHighProjectionRegionPlan* Region : Regions)
			{
				if (Region == nullptr)
				{
					continue;
				}
				for (const int32 SourceVolumeId : Region->SourceVolumeIds)
				{
					const FABTSM73DAG5BV2Volume* Seed = nullptr;
					for (const FABTSM73DAG5BV2Volume* Candidate : ProjectionSeeds)
					{
						if (Candidate != nullptr
							&& Candidate->VolumeId == SourceVolumeId)
						{
							Seed = Candidate;
							break;
						}
					}
					if (Seed == nullptr)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3HighProjectionSeedSourceMissing:Component=%d:Region=%d:Source=%d"),
							ComponentId, Region->RegionId, SourceVolumeId);
						return false;
					}
					FHighProjectionSeedDiagnostic& Diagnostic =
						Plan.HighProjectionSeedDiagnostics.AddDefaulted_GetRef();
					Diagnostic.ComponentId = ComponentId;
					Diagnostic.RegionId = Region->RegionId;
					Diagnostic.SourceVolumeId = Seed->VolumeId;
					Diagnostic.DerivationPath = Seed->DerivationPath;
					Diagnostic.SourceBounds = Seed->LocalBounds;
				}
			}
			for (int32 AIndex = 0; AIndex < ProjectionSeeds.Num(); ++AIndex)
			{
				for (int32 BIndex = AIndex + 1;
					BIndex < ProjectionSeeds.Num(); ++BIndex)
				{
					const FBox& A = ProjectionSeeds[AIndex]->LocalBounds;
					const FBox& B = ProjectionSeeds[BIndex]->LocalBounds;
					FHighProjectionAdjacencyDiagnostic& Diagnostic =
						Plan.HighProjectionAdjacencyDiagnostics.AddDefaulted_GetRef();
					Diagnostic.ComponentId = ComponentId;
					Diagnostic.FirstSourceVolumeId =
						ProjectionSeeds[AIndex]->VolumeId;
					Diagnostic.SecondSourceVolumeId =
						ProjectionSeeds[BIndex]->VolumeId;
					Diagnostic.ZOverlapCM = SkeletonV3OverlapLength(
						A.Min.Z, A.Max.Z, B.Min.Z, B.Max.Z);
					Diagnostic.bAccepted = XYConnected(A, B,
						Diagnostic.XOverlapCM, Diagnostic.YOverlapCM,
						Diagnostic.bPositiveAreaOverlap,
						Diagnostic.bFullEdgeContact);
					Diagnostic.Reason = Diagnostic.bPositiveAreaOverlap
						? TEXT("PositiveXYArea")
						: (Diagnostic.bFullEdgeContact
							? TEXT("FullXYEdgeContact") : TEXT("NoXYConnection"));
				}
			}

			TArray<const FABTSM73DAG5BV2Volume*> SliceVolumes = Root.BodyVolumes;
			SliceVolumes.Append(Root.CrownVolumes);
			SliceVolumes.RemoveAll([](const FABTSM73DAG5BV2Volume* Volume)
			{
				return Volume == nullptr
					|| Volume->DerivationPath.StartsWith(TEXT("CoupledGround/"));
			});
			const double TopZ = FMath::Max(Root.BodyTopCM, Root.CrownTopCM);
			const int32 TopCourse = FMath::CeilToInt(
				(TopZ - Root.GroundZCM) / BlockUnitsCM);
			TArray<TArray<int32>> SliceDiagnosticIndices;
			SliceDiagnosticIndices.SetNum(FMath::Max(0,
				TopCourse - PodiumTopCourse));
			for (int32 Course = PodiumTopCourse; Course < TopCourse; ++Course)
			{
				const double SliceMinZ = Root.GroundZCM + Course * BlockUnitsCM;
				const double SliceMaxZ = SliceMinZ + BlockUnitsCM;
				const double SampleZ = (SliceMinZ + SliceMaxZ) * 0.5;
				TArray<const FABTSM73DAG5BV2Volume*> Active;
				for (const FABTSM73DAG5BV2Volume* Volume : SliceVolumes)
				{
					if (SampleZ >= Volume->LocalBounds.Min.Z
						- WitnessToleranceCM
						&& SampleZ <= Volume->LocalBounds.Max.Z
							+ WitnessToleranceCM)
					{
						Active.Add(Volume);
					}
				}
				Active.Sort([](const FABTSM73DAG5BV2Volume& A,
					const FABTSM73DAG5BV2Volume& B)
				{
					return A.VolumeId < B.VolumeId;
				});
				TArray<TArray<int32>> Adjacency;
				Adjacency.SetNum(Active.Num());
				for (int32 AIndex = 0; AIndex < Active.Num(); ++AIndex)
				{
					for (int32 BIndex = AIndex + 1;
						BIndex < Active.Num(); ++BIndex)
					{
						double XOverlap = 0.0;
						double YOverlap = 0.0;
						bool bPositiveArea = false;
						bool bFullEdge = false;
						if (XYConnected(Active[AIndex]->LocalBounds,
							Active[BIndex]->LocalBounds, XOverlap, YOverlap,
							bPositiveArea, bFullEdge))
						{
							Adjacency[AIndex].Add(BIndex);
							Adjacency[BIndex].Add(AIndex);
						}
					}
				}
				TArray<bool> Visited;
				Visited.Init(false, Active.Num());
				int32 SliceComponentId = 0;
				for (int32 StartIndex = 0; StartIndex < Active.Num(); ++StartIndex)
				{
					if (Visited[StartIndex])
					{
						continue;
					}
					TArray<int32> Queue{StartIndex};
					Visited[StartIndex] = true;
					FHighProjectionSliceComponentDiagnostic& Slice =
						Plan.HighProjectionSliceComponentDiagnostics
							.AddDefaulted_GetRef();
					Slice.ComponentId = ComponentId;
					Slice.SliceCourse = Course;
					Slice.SliceComponentId = SliceComponentId++;
					Slice.SliceMinZCM = SliceMinZ;
					Slice.SliceMaxZCM = SliceMaxZ;
					for (int32 Head = 0; Head < Queue.Num(); ++Head)
					{
						const int32 ActiveIndex = Queue[Head];
						const FABTSM73DAG5BV2Volume* Volume = Active[ActiveIndex];
						Slice.SourceVolumeIds.Add(Volume->VolumeId);
						FBox Clipped = Volume->LocalBounds;
						Clipped.Min.Z = FMath::Max(Clipped.Min.Z, SliceMinZ);
						Clipped.Max.Z = FMath::Min(Clipped.Max.Z, SliceMaxZ);
						Slice.LocalBounds += Clipped;
						for (const int32 Adjacent : Adjacency[ActiveIndex])
						{
							if (!Visited[Adjacent])
							{
								Visited[Adjacent] = true;
								Queue.Add(Adjacent);
							}
						}
					}
					Slice.SourceVolumeIds.Sort();
					for (const FCoreCellPlan& Core : Plan.CoreCells)
					{
						if (Core.ComponentId != ComponentId
							|| Core.HierarchyRole != ECoreHierarchyRole::TowerChild
							|| Core.TopCourseIndex < Course)
						{
							continue;
						}
						const FVector2D CoreCenter(
							Core.LocalBounds.GetCenter().X,
							Core.LocalBounds.GetCenter().Y);
						if (CoreCenter.X >= Slice.LocalBounds.Min.X
							- GeometryToleranceCM
							&& CoreCenter.X <= Slice.LocalBounds.Max.X
								+ GeometryToleranceCM
							&& CoreCenter.Y >= Slice.LocalBounds.Min.Y
								- GeometryToleranceCM
							&& CoreCenter.Y <= Slice.LocalBounds.Max.Y
								+ GeometryToleranceCM)
						{
							Slice.BoundTowerChildCoreCellIds.Add(Core.CoreCellId);
						}
					}
					SliceDiagnosticIndices[Course - PodiumTopCourse].Add(
						Plan.HighProjectionSliceComponentDiagnostics.Num() - 1);
				}
			}

			TSet<FString> SplitChildren;
			TSet<FString> NonTerminal;
			for (int32 SliceOffset = 0;
				SliceOffset + 1 < SliceDiagnosticIndices.Num(); ++SliceOffset)
			{
				for (const int32 LowerIndex : SliceDiagnosticIndices[SliceOffset])
				{
					const FHighProjectionSliceComponentDiagnostic& Lower =
						Plan.HighProjectionSliceComponentDiagnostics[LowerIndex];
					TArray<int32> UpperIndices;
					for (const int32 UpperIndex
						: SliceDiagnosticIndices[SliceOffset + 1])
					{
						const FHighProjectionSliceComponentDiagnostic& Upper =
							Plan.HighProjectionSliceComponentDiagnostics[UpperIndex];
						double XOverlap = 0.0;
						double YOverlap = 0.0;
						bool bPositiveArea = false;
						bool bFullEdge = false;
						XYConnected(Lower.LocalBounds, Upper.LocalBounds,
							XOverlap, YOverlap, bPositiveArea, bFullEdge);
						if (bPositiveArea)
						{
							UpperIndices.Add(UpperIndex);
						}
					}
					if (!UpperIndices.IsEmpty())
					{
						NonTerminal.Add(FString::Printf(TEXT("%d:%d"),
							Lower.SliceCourse, Lower.SliceComponentId));
					}
					if (UpperIndices.Num() > 1)
					{
						FHighProjectionSplitDiagnostic& Split =
							Plan.HighProjectionSplitDiagnostics.AddDefaulted_GetRef();
						Split.ComponentId = ComponentId;
						Split.LowerSliceCourse = Lower.SliceCourse;
						Split.LowerSliceComponentId = Lower.SliceComponentId;
						for (const int32 UpperIndex : UpperIndices)
						{
							const FHighProjectionSliceComponentDiagnostic& Upper =
								Plan.HighProjectionSliceComponentDiagnostics[UpperIndex];
							Split.UpperSliceComponentIds.Add(Upper.SliceComponentId);
							Split.UpperSourceVolumeIds.Append(Upper.SourceVolumeIds);
							SplitChildren.Add(FString::Printf(TEXT("%d:%d"),
								Upper.SliceCourse, Upper.SliceComponentId));
						}
						Split.UpperSourceVolumeIds.Sort();
					}
				}
			}
			for (const TArray<int32>& SliceIndices : SliceDiagnosticIndices)
			{
				for (const int32 SliceIndex : SliceIndices)
				{
					const FHighProjectionSliceComponentDiagnostic& Slice =
						Plan.HighProjectionSliceComponentDiagnostics[SliceIndex];
					const FString Key = FString::Printf(TEXT("%d:%d"),
						Slice.SliceCourse, Slice.SliceComponentId);
					const bool bSplitChild = SplitChildren.Contains(Key);
					const bool bTerminal = !NonTerminal.Contains(Key);
					if (!bSplitChild && !bTerminal)
					{
						continue;
					}
					FHighProjectionBranchBindingDiagnostic& Branch =
						Plan.HighProjectionBranchBindingDiagnostics
							.AddDefaulted_GetRef();
					Branch.ComponentId = ComponentId;
					Branch.SliceCourse = Slice.SliceCourse;
					Branch.SliceComponentId = Slice.SliceComponentId;
					Branch.bSplitChild = bSplitChild;
					Branch.bTerminal = bTerminal;
					Branch.LocalBounds = Slice.LocalBounds;
					Branch.SourceVolumeIds = Slice.SourceVolumeIds;
					Branch.BoundTowerChildCoreCellIds =
						Slice.BoundTowerChildCoreCellIds;
					for (const FHighProjectionRegionPlan* Region : Regions)
					{
						if (Region != nullptr
							&& Region->TerminalSliceCourse == Slice.SliceCourse
							&& Region->TerminalSliceComponentId
								== Slice.SliceComponentId)
						{
							Branch.bRequiresTowerChild = true;
							Branch.RequiredRegionIds.Add(Region->RegionId);
						}
					}
				}
			}
		}

		for (const FHighProjectionSeedDiagnostic& Seed
			: Plan.HighProjectionSeedDiagnostics)
		{
			if (Seed.RegionId == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3HighProjectionSeedDiagnosticUnbound:Component=%d:Source=%d"),
					Seed.ComponentId, Seed.SourceVolumeId);
				return false;
			}
		}
		for (const FHighProjectionRegionPlan& Region : Plan.HighProjectionRegions)
		{
			const FHighProjectionBranchBindingDiagnostic* Terminal =
				Plan.HighProjectionBranchBindingDiagnostics.FindByPredicate(
					[&Region](const FHighProjectionBranchBindingDiagnostic& Branch)
					{
						return Branch.ComponentId == Region.ComponentId
							&& Branch.SliceCourse == Region.TerminalSliceCourse
							&& Branch.SliceComponentId
								== Region.TerminalSliceComponentId
							&& Branch.bTerminal && Branch.bRequiresTowerChild
							&& Branch.RequiredRegionIds.Contains(Region.RegionId);
					});
			if (Terminal == nullptr)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TerminalBranchIdentityMissing:Region=%d:Component=%d:Course=%d:Slice=%d:Core=%d"),
					Region.RegionId, Region.ComponentId,
					Region.TerminalSliceCourse,
					Region.TerminalSliceComponentId,
					Region.BoundCoreCellId);
				return false;
			}
		}
		return true;
	}

	bool ValidateHighProjectionRegionContract(FPlan& Plan, FString& OutError)
	{
		Plan.Summary.HighProjectionRegionCount = Plan.HighProjectionRegions.Num();
		Plan.Summary.BoundHighProjectionRegionCount = 0;
		Plan.Summary.RequiredTerminalBranchCount =
			Plan.HighProjectionRegions.Num();
		Plan.Summary.BoundTerminalBranchCount = 0;
		TSet<int32> BoundCoreIds;
		TSet<int32> BoundSemanticDemandIds;
		for (int32 RegionIndex = 0;
			RegionIndex < Plan.HighProjectionRegions.Num(); ++RegionIndex)
		{
			const FHighProjectionRegionPlan& Region =
				Plan.HighProjectionRegions[RegionIndex];
			if (Region.RegionId != RegionIndex
				|| !Plan.Components.IsValidIndex(Region.ComponentId)
				|| !Plan.SemanticTerminalDemands.IsValidIndex(
					Region.SemanticDemandId)
				|| Plan.SemanticTerminalDemands[Region.SemanticDemandId]
					.ComponentId != Region.ComponentId
				|| Region.PodiumTopCourse < 2
				|| Region.RequiredTopCourse < Region.PodiumTopCourse + 2
				|| Region.TerminalSliceCourse < Region.PodiumTopCourse
				|| Region.TerminalSliceComponentId == INDEX_NONE
				|| Region.SourceVolumeIds.IsEmpty()
				|| !Region.EntryBounds.IsValid || !Region.TerminalBounds.IsValid
				|| !Region.LocalBounds.IsValid
				|| !Plan.CoreCells.IsValidIndex(
					Region.BoundPodiumMainCoreCellId)
				|| !Plan.CoreCells.IsValidIndex(Region.BoundCoreCellId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3HighProjectionRegionInvalid:Region=%d:Stored=%d:Component=%d:Sources=%d:Core=%d"),
					RegionIndex, Region.RegionId, Region.ComponentId,
					Region.SourceVolumeIds.Num(), Region.BoundCoreCellId);
				return false;
			}
			const FComponentPlan& Component = Plan.Components[Region.ComponentId];
			if (FMath::Abs(Region.LocalBounds.Min.Z
					- (Component.GroundPlaneZCM
						+ Region.PodiumTopCourse * BlockUnitsCM))
					> GeometryToleranceCM)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3HighProjectionRegionBaseInvalid:Region=%d:MinZ=%.3f:Expected=%.3f"),
					Region.RegionId, Region.LocalBounds.Min.Z,
					Component.GroundPlaneZCM
						+ Region.PodiumTopCourse * BlockUnitsCM);
				return false;
			}
			for (const int32 SourceVolumeId : Region.SourceVolumeIds)
			{
				if (!Component.SourceVolumeIds.Contains(SourceVolumeId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3HighProjectionRegionSourceInvalid:Region=%d:Source=%d"),
						Region.RegionId, SourceVolumeId);
					return false;
				}
			}
			const FCoreCellPlan& PodiumMain =
				Plan.CoreCells[Region.BoundPodiumMainCoreCellId];
			const FCoreCellPlan& Core = Plan.CoreCells[Region.BoundCoreCellId];
			const FVector CoreCenter = Core.LocalBounds.GetCenter();
			if (PodiumMain.HierarchyRole != ECoreHierarchyRole::PodiumMain
				|| PodiumMain.ComponentId != Region.ComponentId
				|| PodiumMain.CoreMergeRegionId != Component.CoreMergeRegionId
				|| PodiumMain.CompositeCoreGroupId
					!= Core.CompositeCoreGroupId
				|| PodiumMain.TopCourseIndex != Region.PodiumTopCourse
				|| FMath::Abs(PodiumMain.LocalBounds.Min.Z
					- Component.GroundPlaneZCM) > GeometryToleranceCM
				|| Core.HierarchyRole != ECoreHierarchyRole::TowerChild
				|| Core.ComponentId != Region.ComponentId
				|| Core.HighProjectionRegionId != Region.RegionId
				|| Core.SemanticDemandId != Region.SemanticDemandId
				|| Core.PodiumMainCoreCellId != PodiumMain.CoreCellId
				|| Core.TopCourseIndex != Region.RequiredTopCourse
				|| CoreCenter.X < Region.TerminalBounds.Min.X - GeometryToleranceCM
				|| CoreCenter.X > Region.TerminalBounds.Max.X + GeometryToleranceCM
				|| CoreCenter.Y < Region.TerminalBounds.Min.Y - GeometryToleranceCM
				|| CoreCenter.Y > Region.TerminalBounds.Max.Y + GeometryToleranceCM
				|| BoundCoreIds.Contains(Core.CoreCellId)
				|| BoundSemanticDemandIds.Contains(Region.SemanticDemandId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3HighProjectionRegionBindingInvalid:Region=%d:Component=%d:Main=%d:Core=%d:Role=%d:CoreRegion=%d:Top=%d:PodiumTop=%d"),
					Region.RegionId, Region.ComponentId, PodiumMain.CoreCellId,
					Core.CoreCellId,
					static_cast<int32>(Core.HierarchyRole),
					Core.HighProjectionRegionId, Core.TopCourseIndex,
					Region.PodiumTopCourse);
				return false;
			}
			BoundCoreIds.Add(Core.CoreCellId);
			BoundSemanticDemandIds.Add(Region.SemanticDemandId);
			++Plan.Summary.BoundHighProjectionRegionCount;
			++Plan.Summary.BoundTerminalBranchCount;
		}
		for (const FCoreCellPlan& Core : Plan.CoreCells)
		{
			const bool bTowerChild =
				Core.HierarchyRole == ECoreHierarchyRole::TowerChild;
			if (bTowerChild != Plan.HighProjectionRegions.IsValidIndex(
				Core.HighProjectionRegionId)
				|| (bTowerChild
					&& Plan.HighProjectionRegions[Core.HighProjectionRegionId]
						.BoundCoreCellId != Core.CoreCellId)
				|| (bTowerChild
					&& Core.SemanticDemandId == INDEX_NONE)
				|| (!bTowerChild
					&& Core.SemanticDemandId != INDEX_NONE))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3TowerChildProjectionBindingInvalid:Core=%d:Role=%d:Region=%d"),
					Core.CoreCellId, static_cast<int32>(Core.HierarchyRole),
					Core.HighProjectionRegionId);
				return false;
			}
		}
		if (Plan.Summary.BoundHighProjectionRegionCount
			!= Plan.Summary.HighProjectionRegionCount
			|| Plan.Summary.BoundTerminalBranchCount
				!= Plan.Summary.RequiredTerminalBranchCount
			|| BoundSemanticDemandIds.Num()
				!= Plan.SemanticTerminalDemands.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3HighProjectionCoverageMismatch:Regions=%d:Bound=%d:SemanticBound=%d:SemanticDemands=%d"),
				Plan.Summary.HighProjectionRegionCount,
				Plan.Summary.BoundHighProjectionRegionCount,
				BoundSemanticDemandIds.Num(),
				Plan.SemanticTerminalDemands.Num());
			return false;
		}
		return true;
	}

	bool BuildSemanticDemandCoreBindingDiagnostics(FPlan& Plan, FString& OutError)
	{
		Plan.SemanticDemandCoreBindings.Reset();
		Plan.Summary.SemanticDemandCoreBindingCount = 0;
		Plan.Summary.UnmappedSemanticDemandCount = 0;
		Plan.Summary.AmbiguousSemanticDemandCount = 0;
		Plan.Summary.SemanticDemandChildOutsideBodyCount = 0;
		Plan.Summary.SemanticDemandChildWithoutDirectMainCouplingCount = 0;
		Plan.Summary.ReusedTowerChildBindingCount = 0;
		Plan.Summary.UnreferencedTowerChildCount = 0;
		Plan.Summary.SemanticDemandCoreBindingHash = 0;

		auto XYOverlapArea = [](const FBox& A, const FBox& B)
		{
			return SkeletonV3OverlapLength(A.Min.X, A.Max.X, B.Min.X, B.Max.X)
				* SkeletonV3OverlapLength(A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y);
		};
		auto ContainsXY = [](const FBox& Bounds, const FVector& Point)
		{
			return Bounds.IsValid
				&& Point.X >= Bounds.Min.X - GeometryToleranceCM
				&& Point.X <= Bounds.Max.X + GeometryToleranceCM
				&& Point.Y >= Bounds.Min.Y - GeometryToleranceCM
				&& Point.Y <= Bounds.Max.Y + GeometryToleranceCM;
		};
		auto BoundsContainXY = [](const FBox& Outer, const FBox& Inner)
		{
			return Outer.IsValid && Inner.IsValid
				&& Inner.Min.X >= Outer.Min.X - GeometryToleranceCM
				&& Inner.Max.X <= Outer.Max.X + GeometryToleranceCM
				&& Inner.Min.Y >= Outer.Min.Y - GeometryToleranceCM
				&& Inner.Max.Y <= Outer.Max.Y + GeometryToleranceCM;
		};

		for (const FSemanticTerminalDemandDiagnostic& Demand
			: Plan.SemanticTerminalDemands)
		{
			FSemanticDemandCoreBindingDiagnostic& Diagnostic =
				Plan.SemanticDemandCoreBindings.AddDefaulted_GetRef();
			Diagnostic.DemandId = Demand.DemandId;
			Diagnostic.ComponentId = Demand.ComponentId;
			Diagnostic.SupportProvinceId = Demand.SupportProvinceId;
			Diagnostic.TerminalBodySourceVolumeId =
				Demand.TerminalBodySourceVolumeId;
			Diagnostic.TerminalLoadNodeId = Demand.TerminalLoadNodeId;
			Diagnostic.TerminalLoadSourceVolumeId =
				Demand.TerminalLoadSourceVolumeId;
			Diagnostic.DemandBodyBounds = Demand.BodyBounds;
			Diagnostic.TerminalLoadBounds = Demand.TerminalLoadBounds;
			Diagnostic.ContinuousFitBounds = Demand.ContinuousCoreFitBounds;

			TArray<const FHighProjectionRegionPlan*> CandidateRegions;
			for (const FHighProjectionRegionPlan& Region
				: Plan.HighProjectionRegions)
			{
				if (Region.ComponentId == Demand.ComponentId
					&& Region.SemanticDemandId == Demand.DemandId)
				{
					CandidateRegions.Add(&Region);
				}
			}
			Diagnostic.CandidateRegionCount = CandidateRegions.Num();
			TSet<int32> CandidateChildIds;
			for (const FHighProjectionRegionPlan* Candidate : CandidateRegions)
			{
				if (Candidate != nullptr
					&& Plan.CoreCells.IsValidIndex(Candidate->BoundCoreCellId))
				{
					CandidateChildIds.Add(Candidate->BoundCoreCellId);
				}
			}
			Diagnostic.CandidateChildCount = CandidateChildIds.Num();

			if (CandidateRegions.Num() != 1 || CandidateChildIds.Num() != 1)
			{
				Diagnostic.MappingReason = CandidateRegions.IsEmpty()
					? TEXT("NoAuthoritativeSemanticRegion")
					: TEXT("SemanticDemandNotBijective");
				Plan.Summary.UnmappedSemanticDemandCount +=
					CandidateRegions.IsEmpty() || CandidateChildIds.IsEmpty() ? 1 : 0;
				Plan.Summary.AmbiguousSemanticDemandCount +=
					CandidateRegions.Num() > 1 || CandidateChildIds.Num() > 1 ? 1 : 0;
				Diagnostic.bAmbiguousRegionMatch = CandidateRegions.Num() > 1
					|| CandidateChildIds.Num() > 1;
				continue;
			}

			const FHighProjectionRegionPlan& Selected = *CandidateRegions[0];
			const FCoreCellPlan& Child = Plan.CoreCells[Selected.BoundCoreCellId];
			Diagnostic.BoundHighProjectionRegionId = Selected.RegionId;
			Diagnostic.BoundTowerChildCoreCellId = Child.CoreCellId;
			Diagnostic.AssignedPodiumMainCoreCellId =
				Child.PodiumMainCoreCellId;
			Diagnostic.ChildBounds = Child.LocalBounds;
			FBox ChildFootprint = Child.LocalBounds;
			ChildFootprint.Min.X -= BlockUnitsCM * 0.5;
			ChildFootprint.Min.Y -= BlockUnitsCM * 0.5;
			ChildFootprint.Max.X += BlockUnitsCM * 0.5;
			ChildFootprint.Max.Y += BlockUnitsCM * 0.5;
			Diagnostic.BodyChildXYOverlapAreaCM2 = XYOverlapArea(
				Demand.BodyBounds, ChildFootprint);
			Diagnostic.bChildCenterInsideBodyXY = ContainsXY(
				Demand.BodyBounds, Child.LocalBounds.GetCenter());
			Diagnostic.bChildInsideContinuousFitXY =
				Demand.bHasContinuousCoreFit
				&& BoundsContainXY(Demand.ContinuousCoreFitBounds, ChildFootprint);
			Diagnostic.MappingReason = TEXT("AuthoritativeSemanticDemandId");
			if (Plan.CoreCells.IsValidIndex(
				Diagnostic.AssignedPodiumMainCoreCellId))
			{
				const FCoreCellPlan& Main = Plan.CoreCells[
					Diagnostic.AssignedPodiumMainCoreCellId];
				Diagnostic.MainBounds = Main.LocalBounds;
				Diagnostic.bDirectMainCoupling =
					Child.CrossCoreBearingContactCount > 0;
			}
			if (!Diagnostic.bChildCenterInsideBodyXY
				|| !Diagnostic.bChildInsideContinuousFitXY
				|| Child.SemanticDemandId != Demand.DemandId
				|| Child.TopCourseIndex != Demand.RequiredTopCourse)
			{
				++Plan.Summary.SemanticDemandChildOutsideBodyCount;
			}
			if (!Diagnostic.bDirectMainCoupling)
			{
				++Plan.Summary.SemanticDemandChildWithoutDirectMainCouplingCount;
			}
		}

		TMap<int32, int32> DemandMultiplicityByChild;
		for (const FSemanticDemandCoreBindingDiagnostic& Diagnostic
			: Plan.SemanticDemandCoreBindings)
		{
			if (Diagnostic.BoundTowerChildCoreCellId != INDEX_NONE)
			{
				++DemandMultiplicityByChild.FindOrAdd(
					Diagnostic.BoundTowerChildCoreCellId);
			}
		}
		FString Canonical;
		for (FSemanticDemandCoreBindingDiagnostic& Diagnostic
			: Plan.SemanticDemandCoreBindings)
		{
			Diagnostic.BoundChildDemandMultiplicity =
				DemandMultiplicityByChild.FindRef(
					Diagnostic.BoundTowerChildCoreCellId);
			Canonical += FString::Printf(
				TEXT("D=%d:C=%d:P=%d:S=%d:L=%d:LS=%d:R=%d:Child=%d:Main=%d:Mult=%d:Inside=%d:Fit=%d:Direct=%d:Ambiguous=%d:Reason=%s;"),
				Diagnostic.DemandId, Diagnostic.ComponentId,
				Diagnostic.SupportProvinceId,
				Diagnostic.TerminalBodySourceVolumeId,
				Diagnostic.TerminalLoadNodeId,
				Diagnostic.TerminalLoadSourceVolumeId,
				Diagnostic.BoundHighProjectionRegionId,
				Diagnostic.BoundTowerChildCoreCellId,
				Diagnostic.AssignedPodiumMainCoreCellId,
				Diagnostic.BoundChildDemandMultiplicity,
				Diagnostic.bChildCenterInsideBodyXY ? 1 : 0,
				Diagnostic.bChildInsideContinuousFitXY ? 1 : 0,
				Diagnostic.bDirectMainCoupling ? 1 : 0,
				Diagnostic.bAmbiguousRegionMatch ? 1 : 0,
				*Diagnostic.MappingReason);
		}
		for (const TPair<int32, int32>& Pair : DemandMultiplicityByChild)
		{
			Plan.Summary.ReusedTowerChildBindingCount += Pair.Value > 1 ? 1 : 0;
		}
		for (const FCoreCellPlan& Core : Plan.CoreCells)
		{
			if (Core.HierarchyRole == ECoreHierarchyRole::TowerChild
				&& !DemandMultiplicityByChild.Contains(Core.CoreCellId))
			{
				++Plan.Summary.UnreferencedTowerChildCount;
				Canonical += FString::Printf(TEXT("OrphanChild=%d;"),
					Core.CoreCellId);
			}
		}
		Plan.Summary.SemanticDemandCoreBindingCount =
			Plan.SemanticDemandCoreBindings.Num();
		if (Canonical.IsEmpty())
		{
			Canonical = TEXT("NoTowerChildDemandCoreBindings");
		}
		Plan.Summary.SemanticDemandCoreBindingHash = HashText(Canonical);
		if (Plan.Summary.SemanticDemandCoreBindingCount
			!= Plan.Summary.SemanticTerminalDemandCount
			|| Plan.Summary.SemanticDemandCoreBindingHash == 0
			|| Plan.Summary.UnmappedSemanticDemandCount != 0
			|| Plan.Summary.AmbiguousSemanticDemandCount != 0
			|| Plan.Summary.SemanticDemandChildOutsideBodyCount != 0
			|| Plan.Summary.ReusedTowerChildBindingCount != 0
			|| Plan.Summary.UnreferencedTowerChildCount != 0)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SemanticDemandCoreBijectionInvalid:Rows=%d:Demands=%d:Unmapped=%d:Ambiguous=%d:Outside=%d:Reused=%d:Orphan=%d:Hash=%lld"),
				Plan.Summary.SemanticDemandCoreBindingCount,
				Plan.Summary.SemanticTerminalDemandCount,
				Plan.Summary.UnmappedSemanticDemandCount,
				Plan.Summary.AmbiguousSemanticDemandCount,
				Plan.Summary.SemanticDemandChildOutsideBodyCount,
				Plan.Summary.ReusedTowerChildBindingCount,
				Plan.Summary.UnreferencedTowerChildCount,
				Plan.Summary.SemanticDemandCoreBindingHash);
			return false;
		}
		return true;
	}

	bool RebuildCoreLineage(FPlan& Plan, FString& OutError)
	{
		const int32 MemberCount = Plan.Members.Num();
		TArray<TArray<int32>> Adjacency;
		Adjacency.SetNum(MemberCount);
		auto AddEdge = [&Adjacency](const int32 A, const int32 B)
		{
			if (A != B && Adjacency.IsValidIndex(A) && Adjacency.IsValidIndex(B))
			{
				Adjacency[A].AddUnique(B);
				Adjacency[B].AddUnique(A);
			}
		};
		for (int32 UpperIndex = 0; UpperIndex < MemberCount; ++UpperIndex)
		{
			for (const int32 LowerIndex : Plan.Members[UpperIndex].RequiredLowerMemberIndices)
			{
				AddEdge(UpperIndex, LowerIndex);
			}
		}

		// Same-course collinear members form one physical rail only when their
		// end planes touch. Bucket and sort them so this remains O(N log N), not
		// the O(N^2) scan that made the first Beam-C3 iteration impractical.
		TMap<FString, TArray<int32>> CollinearBuckets;
		TArray<FBox> Bounds;
		Bounds.Reserve(MemberCount);
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			const FPlannedMember& Member = Plan.Members[MemberIndex];
			const FBox MemberBounds = PlannedMemberBounds(Member);
			Bounds.Add(MemberBounds);
			if (Member.Axis == EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			const int32 AxisIndex = static_cast<int32>(Member.Axis);
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			const FString Key = FString::Printf(TEXT("%d:%d:%lld:%lld"),
				Member.CourseIndex, AxisIndex,
				QHash(MemberBounds.GetCenter()[CrossAxisIndex]),
				QHash(MemberBounds.GetCenter().Z));
			CollinearBuckets.FindOrAdd(Key).Add(MemberIndex);
		}
		for (TPair<FString, TArray<int32>>& Bucket : CollinearBuckets)
		{
			Bucket.Value.Sort([&Plan, &Bounds](const int32 A, const int32 B)
			{
				const int32 AxisIndex = static_cast<int32>(Plan.Members[A].Axis);
				return !FMath::IsNearlyEqual(Bounds[A].Min[AxisIndex],
					Bounds[B].Min[AxisIndex], GeometryToleranceCM)
					? Bounds[A].Min[AxisIndex] < Bounds[B].Min[AxisIndex] : A < B;
			});
			for (int32 Index = 1; Index < Bucket.Value.Num(); ++Index)
			{
				const int32 Previous = Bucket.Value[Index - 1];
				const int32 Current = Bucket.Value[Index];
				const int32 AxisIndex = static_cast<int32>(Plan.Members[Current].Axis);
				if (FMath::Abs(Bounds[Previous].Max[AxisIndex]
					- Bounds[Current].Min[AxisIndex]) <= GeometryToleranceCM)
				{
					AddEdge(Previous, Current);
				}
			}
		}

		TArray<int32> StructuralComponent;
		StructuralComponent.Init(INDEX_NONE, MemberCount);
		TArray<TSet<int32>> ComponentCoreIds;
		TArray<TArray<int32>> ComponentMembers;
		for (int32 Start = 0; Start < MemberCount; ++Start)
		{
			if (StructuralComponent[Start] != INDEX_NONE)
			{
				continue;
			}
			const int32 ComponentIndex = ComponentMembers.AddDefaulted();
			ComponentCoreIds.AddDefaulted();
			TArray<int32> Queue = {Start};
			StructuralComponent[Start] = ComponentIndex;
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 MemberIndex = Queue[QueueIndex];
				ComponentMembers[ComponentIndex].Add(MemberIndex);
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
					&& Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId))
				{
					ComponentCoreIds[ComponentIndex].Add(Member.OriginCoreCellId);
				}
				else if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
				{
					for (const int32 CoreCellId : Member.EndpointCoreCellIds)
					{
						if (Plan.CoreCells.IsValidIndex(CoreCellId))
						{
							ComponentCoreIds[ComponentIndex].Add(CoreCellId);
						}
					}
				}
				for (const int32 Neighbour : Adjacency[MemberIndex])
				{
					if (StructuralComponent[Neighbour] == INDEX_NONE)
					{
						StructuralComponent[Neighbour] = ComponentIndex;
						Queue.Add(Neighbour);
					}
				}
			}
		}

		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, MemberCount);
		for (int32 ComponentIndex = 0;
			ComponentIndex < ComponentMembers.Num(); ++ComponentIndex)
		{
			if (ComponentCoreIds[ComponentIndex].IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreLineageDisconnected:StructuralComponent=%d:Members=%d"),
					ComponentIndex, ComponentMembers[ComponentIndex].Num());
				return false;
			}
			TArray<int32> Queue;
			TSet<int32> Reached;
			for (const int32 MemberIndex : ComponentMembers[ComponentIndex])
			{
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
				{
					Reached.Add(MemberIndex);
					Queue.Add(MemberIndex);
				}
			}
			Queue.Sort();
			for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
			{
				const int32 Current = Queue[QueueIndex];
				Adjacency[Current].Sort();
				for (const int32 Neighbour : Adjacency[Current])
				{
					if (!Reached.Contains(Neighbour))
					{
						Reached.Add(Neighbour);
						Parent[Neighbour] = Current;
						Queue.Add(Neighbour);
					}
				}
			}
		}

		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			FPlannedMember& Member = Plan.Members[MemberIndex];
			if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
			{
				continue;
			}
			Member.RequiredInwardMemberIndices.Reset();
			if (Parent[MemberIndex] != INDEX_NONE)
			{
				Member.RequiredInwardMemberIndices.Add(Parent[MemberIndex]);
			}
			const TSet<int32>& CoreIds = ComponentCoreIds[
				StructuralComponent[MemberIndex]];
			int32 SelectedCore = INDEX_NONE;
			for (const int32 CoreCellId : CoreIds)
			{
				if (Plan.CoreCells.IsValidIndex(CoreCellId)
					&& Plan.CoreCells[CoreCellId].ComponentId == Member.ComponentId)
				{
					SelectedCore = SelectedCore == INDEX_NONE
						? CoreCellId : FMath::Min(SelectedCore, CoreCellId);
				}
			}
			if (SelectedCore == INDEX_NONE)
			{
				for (const int32 CoreCellId : CoreIds)
				{
					SelectedCore = SelectedCore == INDEX_NONE
						? CoreCellId : FMath::Min(SelectedCore, CoreCellId);
				}
			}
			Member.OriginCoreCellId = SelectedCore;
		}

		Plan.Summary.CommonShellMemberCount = 0;
		Plan.Summary.CommonShellConnectedCoreCount = 0;
		for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			int32 GroupStructuralComponent = INDEX_NONE;
			for (const int32 MemberIndex : Group.MemberIndices)
			{
				if (!Plan.Members.IsValidIndex(MemberIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3BuildingGroupMemberInvalid:Group=%d:Member=%d"),
						Group.GroupId, MemberIndex);
					return false;
				}
				const int32 Current = StructuralComponent[MemberIndex];
				if (GroupStructuralComponent == INDEX_NONE)
				{
					GroupStructuralComponent = Current;
				}
				else if (Current != GroupStructuralComponent)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CommonShellDisconnected:Group=%d:Member=%d"),
						Group.GroupId, MemberIndex);
					return false;
				}
				Plan.Summary.CommonShellMemberCount +=
					Plan.Members[MemberIndex].OwnerKind == EOwnerKind::BuildingGroupShell ? 1 : 0;
			}
			if (GroupStructuralComponent == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CommonShellEmpty:Group=%d"), Group.GroupId);
				return false;
			}
			const TSet<int32>& ConnectedCoreIds =
				ComponentCoreIds[GroupStructuralComponent];
			for (const int32 CoreCellId : Group.CoreCellIds)
			{
				if (!ConnectedCoreIds.Contains(CoreCellId))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CommonShellCoreDisconnected:Group=%d:Core=%d"),
						Group.GroupId, CoreCellId);
					return false;
				}
				++Plan.Summary.CommonShellConnectedCoreCount;
			}
		}
		return true;
	}

	bool CollectRoots(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		TArray<FRoot>& OutRoots,
		int32& OutUnreachableVolumeCount,
		FString& OutError)
	{
		OutRoots.Reset();
		OutUnreachableVolumeCount = 0;
		TArray<const FABTSM73DAG5BV2Volume*> Ordered;
		double GroundZCM = DBL_MAX;
		for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
		{
			if (IsSpanRole(Volume.Role))
			{
				continue;
			}
			if (!Volume.LocalBounds.IsValid
				|| Volume.LocalBounds.Min.ContainsNaN()
				|| Volume.LocalBounds.Max.ContainsNaN())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3InvalidSemanticVolume:Volume=%d"), Volume.VolumeId);
				return false;
			}
			Ordered.Add(&Volume);
			GroundZCM = FMath::Min(GroundZCM, Volume.LocalBounds.Min.Z);
		}
		Ordered.Sort([](const FABTSM73DAG5BV2Volume& A, const FABTSM73DAG5BV2Volume& B)
		{
			return A.VolumeId < B.VolumeId;
		});
		if (Ordered.IsEmpty() || !FMath::IsFinite(GroundZCM))
		{
			OutError = TEXT("BeamC3V3NoPhysicalSemanticVolume");
			return false;
		}
		if (FMath::Abs(GroundZCM) > WitnessToleranceCM)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3GroundPlaneNotZero:MinZ=%.3f"), GroundZCM);
			return false;
		}

		TArray<TArray<int32>> UndirectedGraph;
		TArray<TArray<int32>> DirectedChildren;
		UndirectedGraph.SetNum(Ordered.Num());
		DirectedChildren.SetNum(Ordered.Num());
		TArray<FVerticalSupportWitness> PairWitnesses;
		for (int32 LowerIndex = 0; LowerIndex < Ordered.Num(); ++LowerIndex)
		{
			for (int32 UpperIndex = 0; UpperIndex < Ordered.Num(); ++UpperIndex)
			{
				if (LowerIndex == UpperIndex)
				{
					continue;
				}
				const FABTSM73DAG5BV2Volume& Lower = *Ordered[LowerIndex];
				const FABTSM73DAG5BV2Volume& Upper = *Ordered[UpperIndex];
				if (FMath::Abs(Lower.LocalBounds.Max.Z - Upper.LocalBounds.Min.Z)
					> WitnessToleranceCM)
				{
					continue;
				}
				const double XMin = FMath::Max(Lower.LocalBounds.Min.X, Upper.LocalBounds.Min.X);
				const double XMax = FMath::Min(Lower.LocalBounds.Max.X, Upper.LocalBounds.Max.X);
				const double YMin = FMath::Max(Lower.LocalBounds.Min.Y, Upper.LocalBounds.Min.Y);
				const double YMax = FMath::Min(Lower.LocalBounds.Max.Y, Upper.LocalBounds.Max.Y);
				if (XMax - XMin <= WitnessToleranceCM || YMax - YMin <= WitnessToleranceCM)
				{
					continue;
				}
				UndirectedGraph[LowerIndex].AddUnique(UpperIndex);
				UndirectedGraph[UpperIndex].AddUnique(LowerIndex);
				DirectedChildren[LowerIndex].AddUnique(UpperIndex);
				FVerticalSupportWitness& Witness = PairWitnesses.AddDefaulted_GetRef();
				Witness.LowerSourceVolumeId = Lower.VolumeId;
				Witness.UpperSourceVolumeId = Upper.VolumeId;
				Witness.ContactZCM = (Lower.LocalBounds.Max.Z + Upper.LocalBounds.Min.Z) * 0.5;
				Witness.PositiveXYOverlap = FBox2D(
					FVector2D(XMin, YMin), FVector2D(XMax, YMax));
			}
		}

		// Grounding is a directed proof: merely sharing an undirected component
		// with a ground volume cannot make a lower-facing/floating branch rooted.
		TArray<bool> DirectedReachable;
		DirectedReachable.Init(false, Ordered.Num());
		TArray<int32> DirectedQueue;
		for (int32 Index = 0; Index < Ordered.Num(); ++Index)
		{
			if (FMath::Abs(Ordered[Index]->LocalBounds.Min.Z - GroundZCM)
				<= WitnessToleranceCM)
			{
				DirectedReachable[Index] = true;
				DirectedQueue.Add(Index);
			}
		}
		for (int32 Head = 0; Head < DirectedQueue.Num(); ++Head)
		{
			for (const int32 Child : DirectedChildren[DirectedQueue[Head]])
			{
				if (!DirectedReachable[Child])
				{
					DirectedReachable[Child] = true;
					DirectedQueue.Add(Child);
				}
			}
		}
		for (const bool bReachable : DirectedReachable)
		{
			OutUnreachableVolumeCount += bReachable ? 0 : 1;
		}
		if (OutUnreachableVolumeCount > 0)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3UnreachableSemanticVolumes:Count=%d"),
				OutUnreachableVolumeCount);
			return false;
		}

		// Preserve the original vertical-only component identity before deriving
		// lateral ground-base merge regions. This keeps the merge auditable and
		// prevents a large core from pretending that WFC emitted one monolith.
		TArray<int32> OriginalVerticalComponent;
		OriginalVerticalComponent.Init(INDEX_NONE, Ordered.Num());
		int32 OriginalComponentCount = 0;
		for (int32 StartIndex = 0; StartIndex < Ordered.Num(); ++StartIndex)
		{
			if (OriginalVerticalComponent[StartIndex] != INDEX_NONE)
			{
				continue;
			}
			TArray<int32> Queue{StartIndex};
			OriginalVerticalComponent[StartIndex] = OriginalComponentCount;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				for (const int32 Adjacent : UndirectedGraph[Queue[Head]])
				{
					if (OriginalVerticalComponent[Adjacent] == INDEX_NONE)
					{
						OriginalVerticalComponent[Adjacent] = OriginalComponentCount;
						Queue.Add(Adjacent);
					}
				}
			}
			++OriginalComponentCount;
		}

		TArray<TArray<int32>> GroupingGraph = UndirectedGraph;
		for (int32 AIndex = 0; AIndex < Ordered.Num(); ++AIndex)
		{
			const FABTSM73DAG5BV2Volume& A = *Ordered[AIndex];
			if (FMath::Abs(A.LocalBounds.Min.Z - GroundZCM) > WitnessToleranceCM)
			{
				continue;
			}
			for (int32 BIndex = AIndex + 1; BIndex < Ordered.Num(); ++BIndex)
			{
				const FABTSM73DAG5BV2Volume& B = *Ordered[BIndex];
				if (OriginalVerticalComponent[AIndex] == OriginalVerticalComponent[BIndex]
					|| FMath::Abs(B.LocalBounds.Min.Z - GroundZCM) > WitnessToleranceCM
					|| BuildingPath(A.DerivationPath) != BuildingPath(B.DerivationPath))
				{
					continue;
				}
				const double XOverlap = FMath::Min(A.LocalBounds.Max.X, B.LocalBounds.Max.X)
					- FMath::Max(A.LocalBounds.Min.X, B.LocalBounds.Min.X);
				const double YOverlap = FMath::Min(A.LocalBounds.Max.Y, B.LocalBounds.Max.Y)
					- FMath::Max(A.LocalBounds.Min.Y, B.LocalBounds.Min.Y);
				const bool bXTouches = FMath::Abs(A.LocalBounds.Max.X - B.LocalBounds.Min.X)
					<= WitnessToleranceCM
					|| FMath::Abs(B.LocalBounds.Max.X - A.LocalBounds.Min.X)
						<= WitnessToleranceCM;
				const bool bYTouches = FMath::Abs(A.LocalBounds.Max.Y - B.LocalBounds.Min.Y)
					<= WitnessToleranceCM
					|| FMath::Abs(B.LocalBounds.Max.Y - A.LocalBounds.Min.Y)
						<= WitnessToleranceCM;
				const bool bPositiveAreaOverlap = XOverlap > WitnessToleranceCM
					&& YOverlap > WitnessToleranceCM;
				const bool bFullEdgeContact = (bXTouches && YOverlap > WitnessToleranceCM)
					|| (bYTouches && XOverlap > WitnessToleranceCM);
				if (bPositiveAreaOverlap || bFullEdgeContact)
				{
					GroupingGraph[AIndex].AddUnique(BIndex);
					GroupingGraph[BIndex].AddUnique(AIndex);
				}
			}
		}

		TArray<bool> Visited;
		Visited.Init(false, Ordered.Num());
		for (int32 StartIndex = 0; StartIndex < Ordered.Num(); ++StartIndex)
		{
			if (Visited[StartIndex])
			{
				continue;
			}
			TArray<int32> Queue;
			TArray<int32> Indices;
			Queue.Add(StartIndex);
			Visited[StartIndex] = true;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				const int32 Index = Queue[Head];
				Indices.Add(Index);
				for (const int32 Adjacent : GroupingGraph[Index])
				{
					if (!Visited[Adjacent])
					{
						Visited[Adjacent] = true;
						Queue.Add(Adjacent);
					}
				}
			}

			FRoot& Root = OutRoots.AddDefaulted_GetRef();
			Root.GroundZCM = GroundZCM;
			for (const int32 Index : Indices)
			{
				const FABTSM73DAG5BV2Volume* Volume = Ordered[Index];
				const FString SemanticRoot = RootPath(Volume->DerivationPath);
				if (Root.Path.IsEmpty() || SemanticRoot < Root.Path)
				{
					Root.Path = SemanticRoot;
				}
				Root.Bounds += Volume->LocalBounds;
				Root.SourceVolumeIds.Add(Volume->VolumeId);
				Root.SourceVolumeOriginalComponentIds.Add(
					Volume->VolumeId, OriginalVerticalComponent[Index]);
				if (FMath::Abs(Volume->LocalBounds.Min.Z - GroundZCM)
					<= WitnessToleranceCM)
				{
					Root.GroundSourceVolumeIds.Add(Volume->VolumeId);
					Root.SourceGroundComponentIds.AddUnique(
						OriginalVerticalComponent[Index]);
				}
				if (Volume->Role == EABTSM73DAG5BV2VolumeRole::Crown)
				{
					Root.CrownVolumes.Add(Volume);
					Root.CrownVolumeIds.Add(Volume->VolumeId);
					Root.CrownTopCM = FMath::Max(Root.CrownTopCM, Volume->LocalBounds.Max.Z);
				}
				else
				{
					Root.BodyVolumes.Add(Volume);
					Root.BodyTopCM = FMath::Max(Root.BodyTopCM, Volume->LocalBounds.Max.Z);
				}
			}
			Root.SourceVolumeIds.Sort();
			Root.CrownVolumeIds.Sort();
			Root.GroundSourceVolumeIds.Sort();
			Root.SourceGroundComponentIds.Sort();
			for (const FVerticalSupportWitness& Witness : PairWitnesses)
			{
				if (Root.SourceVolumeIds.Contains(Witness.LowerSourceVolumeId)
					&& Root.SourceVolumeIds.Contains(Witness.UpperSourceVolumeId))
				{
					Root.Witnesses.Add(Witness);
				}
			}
			Root.Witnesses.Sort([](const FVerticalSupportWitness& A, const FVerticalSupportWitness& B)
			{
				return A.LowerSourceVolumeId != B.LowerSourceVolumeId
					? A.LowerSourceVolumeId < B.LowerSourceVolumeId
					: A.UpperSourceVolumeId < B.UpperSourceVolumeId;
			});
			if (Root.BodyVolumes.IsEmpty() || !FMath::IsFinite(Root.BodyTopCM))
			{
				OutError = FString::Printf(TEXT("BeamC3V3GroundedComponentHasNoBody:%s"), *Root.Path);
				return false;
			}
		}
		OutRoots.Sort([](const FRoot& A, const FRoot& B)
		{
			return A.SourceVolumeIds[0] < B.SourceVolumeIds[0];
		});
		return !OutRoots.IsEmpty();
	}

	int32 CellIndex(const int32 X, const int32 Y, const int32 NX)
	{
		return Y * NX + X;
	}

	int32 NodeIndex(const int32 X, const int32 Y, const int32 NX)
	{
		return Y * (NX + 1) + X;
	}

	bool NodeTouchesOccupied(
		const FBandState& Band,
		const int32 X,
		const int32 Y,
		const int32 NX,
		const int32 NY)
	{
		for (int32 DY = -1; DY <= 0; ++DY)
		{
			for (int32 DX = -1; DX <= 0; ++DX)
			{
				const int32 CX = X + DX;
				const int32 CY = Y + DY;
				if (CX >= 0 && CX < NX && CY >= 0 && CY < NY
					&& Band.OccupiedCells[CellIndex(CX, CY, NX)])
				{
					return true;
				}
			}
		}
		return false;
	}

	int32 SelectProjectionSourceVolume(
		const FRoot& Root,
		const double XCM,
		const double YCM,
		const double SliceZCM)
	{
		int32 Result = MAX_int32;
		const FVector Witness(XCM, YCM, SliceZCM);
		for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
		{
			if (Volume != nullptr
				&& Volume->LocalBounds.ExpandBy(GeometryToleranceCM).IsInsideOrOn(Witness))
			{
				Result = FMath::Min(Result, Volume->VolumeId);
			}
		}
		return Result == MAX_int32 ? INDEX_NONE : Result;
	}

	int32 SelectCoreProjectionSourceVolume(
		const FRoot& Root,
		const double XCM,
		const double YCM,
		const double SliceZCM)
	{
		int32 Result = MAX_int32;
		const FVector Witness(XCM, YCM, SliceZCM);
		// Prefer Body ownership at a Body/Crown seam. A Crown source is legal only
		// for the continuous upper extension required to sandwich a shared course;
		// topology validation proves that the extension is neither suspended nor
		// taller than its declared shared-course requirement.
		for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
		{
			if (Volume != nullptr
				&& Volume->LocalBounds.ExpandBy(GeometryToleranceCM).IsInsideOrOn(Witness))
			{
				Result = FMath::Min(Result, Volume->VolumeId);
			}
		}
		if (Result != MAX_int32)
		{
			return Result;
		}
		for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
		{
			if (Volume != nullptr
				&& Volume->LocalBounds.ExpandBy(GeometryToleranceCM).IsInsideOrOn(Witness))
			{
				Result = FMath::Min(Result, Volume->VolumeId);
			}
		}
		return Result == MAX_int32 ? INDEX_NONE : Result;
	}

	void AppendSharedEndpointReachabilityDiagnostics(
		const FRoot& Root,
		const int32 ComponentId,
		const FABTSM73DAG5BV2Volume& Span,
		const bool bNegativeEndpoint,
		const int32 RequiredTopCourse,
		TArray<FSharedEndpointReachabilityDiagnostic>& OutDiagnostics)
	{
		const int32 AxisIndex = Span.SpanAxisIndex;
		const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
		const int32 MinimumX = QMin(Root.Bounds.Min.X + BlockUnitsCM * 0.5);
		const int32 MaximumX = QMax(Root.Bounds.Max.X - BlockUnitsCM * 0.5);
		const int32 MinimumY = QMin(Root.Bounds.Min.Y + BlockUnitsCM * 0.5);
		const int32 MaximumY = QMax(Root.Bounds.Max.Y - BlockUnitsCM * 0.5);
		TArray<FBox> AllowedBoxes;
		for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
		{
			AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
				BlockUnitsCM * 0.5 + GeometryToleranceCM));
		}
		for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
		{
			AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
				BlockUnitsCM * 0.5 + GeometryToleranceCM));
		}

		for (int32 Y = MinimumY; Y < MaximumY; ++Y)
		{
			for (int32 X = MinimumX; X < MaximumX; ++X)
			{
				FSharedEndpointReachabilityDiagnostic& Diagnostic =
					OutDiagnostics.AddDefaulted_GetRef();
				Diagnostic.SpanVolumeId = Span.VolumeId;
				Diagnostic.ComponentId = ComponentId;
				Diagnostic.bNegativeEndpoint = bNegativeEndpoint;
				Diagnostic.RequiredTopCourse = RequiredTopCourse;
				Diagnostic.CandidateBounds = FBox(
					Position(X * BlockUnitsCM, Y * BlockUnitsCM, Root.GroundZCM),
					Position((X + 1) * BlockUnitsCM, (Y + 1) * BlockUnitsCM,
						Root.GroundZCM + RequiredTopCourse * BlockUnitsCM));
				const double PhysicalMinimumX =
					(X - 0.5) * static_cast<double>(BlockUnitsCM);
				const double PhysicalMaximumX =
					(X + 1.5) * static_cast<double>(BlockUnitsCM);
				const double PhysicalMinimumY =
					(Y - 0.5) * static_cast<double>(BlockUnitsCM);
				const double PhysicalMaximumY =
					(Y + 1.5) * static_cast<double>(BlockUnitsCM);
				const double CandidateCrossMinimum = CrossAxisIndex == 0
					? PhysicalMinimumX : PhysicalMinimumY;
				const double CandidateCrossMaximum = CrossAxisIndex == 0
					? PhysicalMaximumX : PhysicalMaximumY;
				Diagnostic.bTransverseOverlap = SkeletonV3OverlapLength(
					CandidateCrossMinimum, CandidateCrossMaximum,
					Span.LocalBounds.Min[CrossAxisIndex],
					Span.LocalBounds.Max[CrossAxisIndex]) > GeometryToleranceCM;
				Diagnostic.OpeningBoundaryCM = bNegativeEndpoint
					? Span.SpanOpeningMinCM : Span.SpanOpeningMaxCM;
				Diagnostic.CandidateInnerFaceCM = bNegativeEndpoint
					? (AxisIndex == 0 ? PhysicalMaximumX : PhysicalMaximumY)
					: (AxisIndex == 0 ? PhysicalMinimumX : PhysicalMinimumY);
				Diagnostic.EndpointInsetCM = bNegativeEndpoint
					? Diagnostic.OpeningBoundaryCM - Diagnostic.CandidateInnerFaceCM
					: Diagnostic.CandidateInnerFaceCM - Diagnostic.OpeningBoundaryCM;
				Diagnostic.MinimumCrossContributionCM =
					(Span.SpanOpeningMaxCM - Span.SpanOpeningMinCM)
					+ BlockUnitsCM
					+ FMath::Max(0.0, Diagnostic.EndpointInsetCM);

				const double CenterX = (X + 0.5) * BlockUnitsCM;
				const double CenterY = (Y + 0.5) * BlockUnitsCM;
				Diagnostic.CandidateBaseSourceVolumeId = SelectProjectionSourceVolume(
					Root, CenterX, CenterY, Root.GroundZCM + BlockUnitsCM * 0.5);
				Diagnostic.bGrounded =
					Diagnostic.CandidateBaseSourceVolumeId != INDEX_NONE
					&& Root.GroundSourceVolumeIds.Contains(
						Diagnostic.CandidateBaseSourceVolumeId);

				bool bStackCovered = Diagnostic.bGrounded;
				for (int32 Course = 0; Course < RequiredTopCourse && bStackCovered; ++Course)
				{
					const double Z = Root.GroundZCM
						+ (Course + 0.5) * BlockUnitsCM;
					const int32 Source = SelectCoreProjectionSourceVolume(
						Root, CenterX, CenterY, Z);
					if (Source == INDEX_NONE)
					{
						bStackCovered = false;
						break;
					}
					if (Course == RequiredTopCourse - 1)
					{
						Diagnostic.CandidateTopSourceVolumeId = Source;
					}
					const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
						? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
					for (int32 Rail = 0; Rail < 2; ++Rail)
					{
						FPlannedMember Probe;
						Probe.Axis = Axis;
						if (Axis == EABTSM73BeamAFrameAxis::X)
						{
							const double RailY = (Y + Rail) * BlockUnitsCM;
							Probe.LocalStart = Position(PhysicalMinimumX, RailY, Z);
							Probe.LocalEnd = Position(PhysicalMaximumX, RailY, Z);
						}
						else
						{
							const double RailX = (X + Rail) * BlockUnitsCM;
							Probe.LocalStart = Position(RailX, PhysicalMinimumY, Z);
							Probe.LocalEnd = Position(RailX, PhysicalMaximumY, Z);
						}
						FVector UncoveredPoint;
						if (!SolidCoveredByBoxes(
							PlannedMemberBounds(Probe), AllowedBoxes, UncoveredPoint))
						{
							bStackCovered = false;
							break;
						}
					}
				}
				Diagnostic.bBodyCrownStackCovered = bStackCovered;
				if (Diagnostic.CandidateTopSourceVolumeId != INDEX_NONE)
				{
					TArray<const FABTSM73DAG5BV2Volume*> Sources = Root.BodyVolumes;
					Sources.Append(Root.CrownVolumes);
					if (const FABTSM73DAG5BV2Volume* const* TopVolume =
						Sources.FindByPredicate(
							[&Diagnostic](const FABTSM73DAG5BV2Volume* Volume)
							{
								return Volume != nullptr && Volume->VolumeId
									== Diagnostic.CandidateTopSourceVolumeId;
							}))
					{
						Diagnostic.CandidateBranchPath = RootPath(
							(*TopVolume)->DerivationPath);
					}
				}
				if (!Diagnostic.bTransverseOverlap)
				{
					Diagnostic.FirstRejectReason = TEXT("NoTransverseSpanOverlap");
				}
				else if (!Diagnostic.bGrounded)
				{
					Diagnostic.FirstRejectReason = TEXT("NoGroundSource");
				}
				else if (!Diagnostic.bBodyCrownStackCovered)
				{
					Diagnostic.FirstRejectReason = TEXT("BodyCrownStackUncovered");
				}
				else
				{
					Diagnostic.bReachableInWFC = true;
				}
			}
		}
	}

	int32 SelectExteriorPostSourceVolume(
		const FRoot& Root,
		const double XCM,
		const double YCM,
		const double SliceZCM)
	{
		int32 Result = MAX_int32;
		const double XYHaloCM = BlockUnitsCM * 0.5 + GeometryToleranceCM;
		for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
		{
			if (Volume == nullptr
				|| SliceZCM < Volume->LocalBounds.Min.Z - GeometryToleranceCM
				|| SliceZCM > Volume->LocalBounds.Max.Z + GeometryToleranceCM
				|| XCM < Volume->LocalBounds.Min.X - XYHaloCM
				|| XCM > Volume->LocalBounds.Max.X + XYHaloCM
				|| YCM < Volume->LocalBounds.Min.Y - XYHaloCM
				|| YCM > Volume->LocalBounds.Max.Y + XYHaloCM)
			{
				continue;
			}
			Result = FMath::Min(Result, Volume->VolumeId);
		}
		return Result == MAX_int32 ? INDEX_NONE : Result;
	}

	void BuildExteriorEmptyMask(FBandState& Band, const int32 NX, const int32 NY)
	{
		Band.ExteriorEmptyCells.Init(false, NX * NY);
		TArray<int32> Queue;
		for (int32 Y = 0; Y < NY; ++Y)
		{
			for (int32 X = 0; X < NX; ++X)
			{
				const int32 Index = CellIndex(X, Y, NX);
				if (!Band.OccupiedCells[Index]
					&& (X == 0 || X == NX - 1 || Y == 0 || Y == NY - 1))
				{
					Band.ExteriorEmptyCells[Index] = true;
					Queue.Add(Index);
				}
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Index = Queue[Head];
			const int32 X = Index % NX;
			const int32 Y = Index / NX;
			static const int32 DX[] = {-1, 1, 0, 0};
			static const int32 DY[] = {0, 0, -1, 1};
			for (int32 Direction = 0; Direction < 4; ++Direction)
			{
				const int32 NXCell = X + DX[Direction];
				const int32 NYCell = Y + DY[Direction];
				if (NXCell < 0 || NXCell >= NX || NYCell < 0 || NYCell >= NY)
				{
					continue;
				}
				const int32 Adjacent = CellIndex(NXCell, NYCell, NX);
				if (!Band.OccupiedCells[Adjacent] && !Band.ExteriorEmptyCells[Adjacent])
				{
					Band.ExteriorEmptyCells[Adjacent] = true;
					Queue.Add(Adjacent);
				}
			}
		}
	}

	bool IsExteriorEmpty(
		const FBandState& Band,
		const int32 X,
		const int32 Y,
		const int32 NX,
		const int32 NY)
	{
		return X < 0 || X >= NX || Y < 0 || Y >= NY
			|| Band.ExteriorEmptyCells[CellIndex(X, Y, NX)];
	}

	uint8 NodeExteriorFaceMask(
		const FBandState& Band,
		const int32 X,
		const int32 Y,
		const int32 NX,
		const int32 NY)
	{
		uint8 Mask = 0;
		for (int32 DY = -1; DY <= 0; ++DY)
		{
			for (int32 DX = -1; DX <= 0; ++DX)
			{
				const int32 CellX = X + DX;
				const int32 CellY = Y + DY;
				if (CellX < 0 || CellX >= NX || CellY < 0 || CellY >= NY
					|| !Band.OccupiedCells[CellIndex(CellX, CellY, NX)])
				{
					continue;
				}
				if (X == CellX && IsExteriorEmpty(Band, CellX - 1, CellY, NX, NY))
				{
					Mask |= ABTSM73BeamC3V3::NegativeX;
				}
				if (X == CellX + 1 && IsExteriorEmpty(Band, CellX + 1, CellY, NX, NY))
				{
					Mask |= ABTSM73BeamC3V3::PositiveX;
				}
				if (Y == CellY && IsExteriorEmpty(Band, CellX, CellY - 1, NX, NY))
				{
					Mask |= ABTSM73BeamC3V3::NegativeY;
				}
				if (Y == CellY + 1 && IsExteriorEmpty(Band, CellX, CellY + 1, NX, NY))
				{
					Mask |= ABTSM73BeamC3V3::PositiveY;
				}
			}
		}
		return Mask;
	}

	bool PointWithinHorizontalMember(
		const FPlannedMember& Member,
		const double X,
		const double Y)
	{
		if (Member.Axis == EABTSM73BeamAFrameAxis::X)
		{
			return FMath::Abs(Member.LocalStart.Y - Y) <= GeometryToleranceCM
				&& X >= FMath::Min(Member.LocalStart.X, Member.LocalEnd.X) - GeometryToleranceCM
				&& X <= FMath::Max(Member.LocalStart.X, Member.LocalEnd.X) + GeometryToleranceCM;
		}
		if (Member.Axis == EABTSM73BeamAFrameAxis::Y)
		{
			return FMath::Abs(Member.LocalStart.X - X) <= GeometryToleranceCM
				&& Y >= FMath::Min(Member.LocalStart.Y, Member.LocalEnd.Y) - GeometryToleranceCM
				&& Y <= FMath::Max(Member.LocalStart.Y, Member.LocalEnd.Y) + GeometryToleranceCM;
		}
		return false;
	}

	void FindMemberSeatsAtPoint(
		const FPlan& Plan,
		const TArray<int32>& Candidates,
		const double X,
		const double Y,
		TArray<int32>& OutSeats)
	{
		for (const int32 Candidate : Candidates)
		{
			if (Plan.Members.IsValidIndex(Candidate)
				&& PointWithinHorizontalMember(Plan.Members[Candidate], X, Y))
			{
				OutSeats.AddUnique(Candidate);
			}
		}
	}

	bool BoxesPenetrate(const FBox& A, const FBox& B)
	{
		return SkeletonV3OverlapLength(A.Min.X, A.Max.X, B.Min.X, B.Max.X)
				> GeometryToleranceCM
			&& SkeletonV3OverlapLength(A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y)
				> GeometryToleranceCM
			&& SkeletonV3OverlapLength(A.Min.Z, A.Max.Z, B.Min.Z, B.Max.Z)
				> GeometryToleranceCM;
	}

	bool ValidateNoPenetration(FPlan& Plan, FString& OutError)
	{
		TMap<FIntVector, TArray<int32>> SpatialBuckets;
		TArray<FBox> Bounds;
		Bounds.Reserve(Plan.Members.Num());
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FBox Box = PlannedMemberBounds(Plan.Members[MemberIndex]);
			Bounds.Add(Box);
			const FIntVector Minimum(
				FMath::FloorToInt((Box.Min.X + GeometryToleranceCM) / BlockUnitsCM),
				FMath::FloorToInt((Box.Min.Y + GeometryToleranceCM) / BlockUnitsCM),
				FMath::FloorToInt((Box.Min.Z + GeometryToleranceCM) / BlockUnitsCM));
			const FIntVector Maximum(
				FMath::FloorToInt((Box.Max.X - GeometryToleranceCM) / BlockUnitsCM),
				FMath::FloorToInt((Box.Max.Y - GeometryToleranceCM) / BlockUnitsCM),
				FMath::FloorToInt((Box.Max.Z - GeometryToleranceCM) / BlockUnitsCM));
			TSet<int32> Candidates;
			for (int32 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
			{
				for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
				{
					for (int32 X = Minimum.X; X <= Maximum.X; ++X)
					{
						const FIntVector Key(X, Y, Z);
						if (const TArray<int32>* Existing = SpatialBuckets.Find(Key))
						{
							for (const int32 Candidate : *Existing)
							{
								Candidates.Add(Candidate);
							}
						}
					}
				}
			}
			for (const int32 Candidate : Candidates)
			{
				if (BoxesPenetrate(Box, Bounds[Candidate]))
				{
					const FPlannedMember& A = Plan.Members[Candidate];
					const FPlannedMember& B = Plan.Members[MemberIndex];
					const FBox& ABounds = Bounds[Candidate];
					++Plan.Summary.PenetrationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3PositiveVolumePenetration:MemberA=%d:R%d:O%d:C%d:S%d:Q%d:A%d:B=%.1f,%.1f,%.1f..%.1f,%.1f,%.1f:MemberB=%d:R%d:O%d:C%d:S%d:Q%d:A%d:B=%.1f,%.1f,%.1f..%.1f,%.1f,%.1f"),
						Candidate, static_cast<int32>(A.Role), static_cast<int32>(A.OwnerKind),
						A.ComponentId, A.SourceVolumeId, A.CourseIndex,
						static_cast<int32>(A.Axis),
						ABounds.Min.X, ABounds.Min.Y, ABounds.Min.Z,
						ABounds.Max.X, ABounds.Max.Y, ABounds.Max.Z,
						MemberIndex, static_cast<int32>(B.Role), static_cast<int32>(B.OwnerKind),
						B.ComponentId, B.SourceVolumeId, B.CourseIndex,
						static_cast<int32>(B.Axis),
						Box.Min.X, Box.Min.Y, Box.Min.Z,
						Box.Max.X, Box.Max.Y, Box.Max.Z);
					return false;
				}
			}
			for (int32 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
			{
				for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
				{
					for (int32 X = Minimum.X; X <= Maximum.X; ++X)
					{
						SpatialBuckets.FindOrAdd(FIntVector(X, Y, Z)).Add(MemberIndex);
					}
				}
			}
		}
		return true;
	}

	void SortUniqueCuts(TArray<double>& Cuts)
	{
		Cuts.Sort();
		for (int32 Index = Cuts.Num() - 1; Index > 0; --Index)
		{
			if (FMath::Abs(Cuts[Index] - Cuts[Index - 1])
				<= CoverageMachineEpsilon)
			{
				Cuts.RemoveAt(Index);
			}
		}
	}

	/** Exact containment for an axis-aligned member solid in an axis-aligned
	 * semantic union. Splitting at every allowed-box plane makes membership
	 * constant inside each resulting cell, so one interior witness per cell is
	 * sufficient and cannot miss a hole along a long member. */
	bool SolidCoveredByBoxes(
		const FBox& Solid,
		const TArray<FBox>& AllowedBoxes,
		FVector& OutUncoveredPoint)
	{
		if (!Solid.IsValid || AllowedBoxes.IsEmpty())
		{
			OutUncoveredPoint = Solid.GetCenter();
			return false;
		}
		TArray<double> Cuts[3];
		TArray<const FBox*> OverlappingBoxes;
		OverlappingBoxes.Reserve(AllowedBoxes.Num());
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Cuts[Axis] = {Solid.Min[Axis], Solid.Max[Axis]};
		}
		for (const FBox& Box : AllowedBoxes)
		{
			if (!Box.IsValid
				|| SkeletonV3OverlapLength(Solid.Min.X, Solid.Max.X, Box.Min.X, Box.Max.X)
					<= CoverageMachineEpsilon
				|| SkeletonV3OverlapLength(Solid.Min.Y, Solid.Max.Y, Box.Min.Y, Box.Max.Y)
					<= CoverageMachineEpsilon
				|| SkeletonV3OverlapLength(Solid.Min.Z, Solid.Max.Z, Box.Min.Z, Box.Max.Z)
					<= CoverageMachineEpsilon)
			{
				continue;
			}
			OverlappingBoxes.Add(&Box);
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Cuts[Axis].Add(FMath::Clamp(Box.Min[Axis], Solid.Min[Axis], Solid.Max[Axis]));
				Cuts[Axis].Add(FMath::Clamp(Box.Max[Axis], Solid.Min[Axis], Solid.Max[Axis]));
			}
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			SortUniqueCuts(Cuts[Axis]);
		}
		for (int32 X = 0; X + 1 < Cuts[0].Num(); ++X)
		{
			if (Cuts[0][X + 1] - Cuts[0][X] <= CoverageMachineEpsilon)
			{
				continue;
			}
			for (int32 Y = 0; Y + 1 < Cuts[1].Num(); ++Y)
			{
				if (Cuts[1][Y + 1] - Cuts[1][Y] <= CoverageMachineEpsilon)
				{
					continue;
				}
				for (int32 Z = 0; Z + 1 < Cuts[2].Num(); ++Z)
				{
					if (Cuts[2][Z + 1] - Cuts[2][Z] <= CoverageMachineEpsilon)
					{
						continue;
					}
					const FVector Witness(
						(Cuts[0][X] + Cuts[0][X + 1]) * 0.5,
						(Cuts[1][Y] + Cuts[1][Y + 1]) * 0.5,
						(Cuts[2][Z] + Cuts[2][Z + 1]) * 0.5);
					if (!OverlappingBoxes.ContainsByPredicate([&Witness](const FBox* Box)
						{
							return Box != nullptr && Box->IsInsideOrOn(Witness);
						}))
					{
						OutUncoveredPoint = Witness;
						return false;
					}
				}
			}
		}
		return true;
	}

	bool RailSolidCoveredBySemanticUnion(
		const FVector& Start,
		const FVector& End,
		const EABTSM73BeamAFrameAxis Axis,
		const FABTSM73DAG5BV2Volume& Span,
		const FABTSM73DAG5BV2Volume& NegativeSupport,
		const FABTSM73DAG5BV2Volume& PositiveSupport)
	{
		const FABTSM73DAG5BV2Volume* Volumes[] = {&Span, &NegativeSupport, &PositiveSupport};
		for (const double T : {0.0, 0.5, 1.0})
		{
			const FVector Center = FMath::Lerp(Start, End, T);
			for (const double Side : {-BlockUnitsCM * 0.5, BlockUnitsCM * 0.5})
			{
				for (const double Vertical : {-BlockUnitsCM * 0.5, BlockUnitsCM * 0.5})
				{
					FVector Point = Center;
					Point.Z += Vertical;
					Point[Axis == EABTSM73BeamAFrameAxis::X ? 1 : 0] += Side;
					bool bCovered = false;
					for (const FABTSM73DAG5BV2Volume* Volume : Volumes)
					{
						bCovered |= Volume->LocalBounds.ExpandBy(GeometryToleranceCM).IsInsideOrOn(Point);
					}
					if (!bCovered)
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	bool ValidateEnvelopeAndVoids(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const TArray<FRoot>& Roots,
		FPlan& Plan,
		FString& OutError)
	{
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FPlannedMember& Member = Plan.Members[MemberIndex];
			const FBox MemberBox = PlannedMemberBounds(Member);
			TArray<FBox> AllowedBoxes;
			if (Member.OwnerKind != EOwnerKind::SupportedSpan
				&& Member.OwnerKind != EOwnerKind::BuildingGroupShell
				&& !Roots.IsValidIndex(Member.ComponentId))
			{
				++Plan.Summary.EnvelopeViolationCount;
				OutError = FString::Printf(
					TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidNonSpanComponent:Member=%d:Component=%d"),
					MemberIndex, Member.ComponentId);
				return false;
			}
			if (Member.OwnerKind == EOwnerKind::BuildingGroupShell)
			{
				const bool bHorizontal = Member.Axis == EABTSM73BeamAFrameAxis::X
					|| Member.Axis == EABTSM73BeamAFrameAxis::Y;
				const bool bHorizontalKind = Member.SkeletonKind
						== ESkeletonMemberKind::ThroughCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FloorCourse;
				const bool bPost = Member.Axis == EABTSM73BeamAFrameAxis::Z
					&& Member.SkeletonKind == ESkeletonMemberKind::ExteriorPost
					&& Member.Role == EABTSM73BeamAMemberRole::Post;
				if (!Plan.BuildingGroups.IsValidIndex(Member.OwnerId)
					|| Member.ComponentId != INDEX_NONE
					|| Member.SourceVolumeId != INDEX_NONE
					|| !((bHorizontal && bHorizontalKind
						&& (Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
							|| Member.Role == EABTSM73BeamAMemberRole::SecondaryBeam))
						|| bPost))
				{
					++Plan.Summary.EnvelopeViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidBuildingGroupContract:Member=%d:Owner=%d:Kind=%d:Axis=%d"),
						MemberIndex, Member.OwnerId,
						static_cast<int32>(Member.SkeletonKind),
						static_cast<int32>(Member.Axis));
					return false;
				}
				AllowedBoxes.Add(Plan.BuildingGroups[Member.OwnerId].LocalBounds.ExpandBy(
					BlockUnitsCM + GeometryToleranceCM));
			}
			else if (Member.OwnerKind == EOwnerKind::SupportedSpan)
			{
				const FABTSM73DAG5BV2Volume* Span =
					FindVolume(Silhouette, Member.SourceVolumeId);
				const EABTSM73BeamAFrameAxis ExpectedSharedAxis = Span != nullptr
					&& Span->SpanAxisIndex == 0
					? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
				const EABTSM73BeamAFrameAxis ExpectedDiaphragmAxis = Span != nullptr
					&& Span->SpanAxisIndex == 0
					? EABTSM73BeamAFrameAxis::Y : EABTSM73BeamAFrameAxis::X;
				const bool bShared = Member.SkeletonKind
					== ESkeletonMemberKind::SharedCourse;
				const bool bDiaphragm = Member.SkeletonKind
					== ESkeletonMemberKind::BridgeDiaphragm;
				const int32 MinimumSupportedSpanSeatCount = bShared
					? (Member.bSharedCrossCoreSegment ? 2 : 1)
					: 2;
				if (Span == nullptr || !IsSpanRole(Span->Role)
					|| Member.OwnerId != Member.SourceVolumeId
					|| Member.ComponentId != INDEX_NONE
					|| (!bShared && !bDiaphragm)
					|| Member.Role != EABTSM73BeamAMemberRole::BridgeRail
					|| (Span->SpanAxisIndex != 0 && Span->SpanAxisIndex != 1)
					|| (bShared && Member.Axis != ExpectedSharedAxis)
					|| (bDiaphragm && Member.Axis != ExpectedDiaphragmAxis)
					|| Member.EndpointCoreCellIds.Num() != 2
					|| Member.RequiredLowerMemberIndices.Num()
						< MinimumSupportedSpanSeatCount)
				{
					++Plan.Summary.EnvelopeViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidSupportedSpanContract:Member=%d:Owner=%d:Component=%d:Source=%d:Role=%d:Axis=%d:Seats=%d"),
						MemberIndex, Member.OwnerId, Member.ComponentId,
						Member.SourceVolumeId, static_cast<int32>(Member.Role),
						static_cast<int32>(Member.Axis),
						Member.RequiredLowerMemberIndices.Num());
					return false;
				}
				if (Span != nullptr)
				{
					// Span centre-lines are snapped to the 36 cm lattice.  Their physical
					// solids may therefore project by at most one half cross-section beyond
					// the WFC volume while still remaining part of the same bridge envelope.
					AllowedBoxes.Add(Span->LocalBounds.ExpandBy(
						BlockUnitsCM * 0.5 + GeometryToleranceCM));
					// Shared rails and their transverse diaphragms form one bridge lattice.
					// Both must enter the endpoint cores by a real full-face overlap, so the
					// semantic envelope is the span plus those endpoint core/group solids.
					if (bShared || bDiaphragm)
					{
						for (const int32 CoreCellId : Member.EndpointCoreCellIds)
						{
							if (!Plan.CoreCells.IsValidIndex(CoreCellId))
							{
								continue;
							}
							const int32 ComponentId = Plan.CoreCells[CoreCellId].ComponentId;
							if (!Roots.IsValidIndex(ComponentId))
							{
								continue;
							}
							for (const FABTSM73DAG5BV2Volume* Volume : Roots[ComponentId].BodyVolumes)
							{
								AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
									BlockUnitsCM * 0.5 + GeometryToleranceCM));
							}
							for (const FABTSM73DAG5BV2Volume* Volume : Roots[ComponentId].CrownVolumes)
							{
								AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
									BlockUnitsCM * 0.5 + GeometryToleranceCM));
							}
						}
						for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
						{
							if (Group.MemberIndices.Contains(MemberIndex))
							{
								AllowedBoxes.Add(Group.LocalBounds.ExpandBy(
									BlockUnitsCM + GeometryToleranceCM));
							}
						}
					}
				}
			}
			else if (Roots.IsValidIndex(Member.ComponentId))
			{
				const FRoot& Root = Roots[Member.ComponentId];
				if (!Root.SourceVolumeIds.Contains(Member.SourceVolumeId))
				{
					++Plan.Summary.EnvelopeViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3EnvelopeViolation:Reason=SourceOutsideComponent:Member=%d:Component=%d:Source=%d"),
						MemberIndex, Member.ComponentId, Member.SourceVolumeId);
					return false;
				}
				const bool bOwnerMatches = Member.OwnerKind == EOwnerKind::CoreCell
					? Plan.CoreCells.IsValidIndex(Member.OwnerId)
						&& Plan.CoreCells[Member.OwnerId].ComponentId == Member.ComponentId
					: (Member.OwnerKind == EOwnerKind::ShellFace
						|| Member.OwnerKind == EOwnerKind::Floor)
					? Member.OwnerId == Member.ComponentId
					: Member.OwnerKind == EOwnerKind::Roof
						? Member.OwnerId == Member.SourceVolumeId
							&& Root.CrownVolumeIds.Contains(Member.SourceVolumeId)
						: false;
				const bool bRoleMatches =
					(Member.OwnerKind == EOwnerKind::CoreCell
						&& Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
						&& Member.Role == EABTSM73BeamAMemberRole::CoreCourse
						&& (Member.Axis == EABTSM73BeamAFrameAxis::X
							|| Member.Axis == EABTSM73BeamAFrameAxis::Y))
					|| (Member.OwnerKind == EOwnerKind::ShellFace
						&& (Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
							|| Member.Role == EABTSM73BeamAMemberRole::SecondaryBeam
							|| Member.Role == EABTSM73BeamAMemberRole::Post))
					|| (Member.OwnerKind == EOwnerKind::Floor
						&& (Member.Role == EABTSM73BeamAMemberRole::PrimaryBeam
							|| Member.Role == EABTSM73BeamAMemberRole::SecondaryBeam
							|| Member.Role == EABTSM73BeamAMemberRole::Post))
					|| (Member.OwnerKind == EOwnerKind::Roof
						&& Member.Role == EABTSM73BeamAMemberRole::RoofCourse);
				if (!bOwnerMatches || !bRoleMatches)
				{
					++Plan.Summary.EnvelopeViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3EnvelopeViolation:Reason=NonSpanOwnerContractMismatch:Member=%d:OwnerKind=%d:Owner=%d:Component=%d:Source=%d:Role=%d"),
						MemberIndex, static_cast<int32>(Member.OwnerKind), Member.OwnerId,
						Member.ComponentId, Member.SourceVolumeId, static_cast<int32>(Member.Role));
					return false;
				}
				if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse)
				{
					const FCoreCellPlan& OwnerCore = Plan.CoreCells[Member.OwnerId];
					const int32 BodyCourseCount = OwnerCore.BodyTopCourseIndex;
					if (Member.OwnerKind != EOwnerKind::CoreCell
						|| Member.Axis == EABTSM73BeamAFrameAxis::Z
						|| (Member.CourseIndex < BodyCourseCount
							&& Root.CrownVolumeIds.Contains(Member.SourceVolumeId)))
					{
						++Plan.Summary.EnvelopeViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidCoreCourseContract:Member=%d:Owner=%d:Axis=%d:Source=%d"),
							MemberIndex, static_cast<int32>(Member.OwnerKind),
							static_cast<int32>(Member.Axis), Member.SourceVolumeId);
						return false;
					}
					for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
					{
						AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
							BlockUnitsCM * 0.5 + GeometryToleranceCM));
					}
					for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
					{
						if (Group.MemberIndices.Contains(MemberIndex))
						{
							AllowedBoxes.Add(Group.LocalBounds.ExpandBy(
								BlockUnitsCM + GeometryToleranceCM));
						}
					}
					if (Member.CourseIndex >= BodyCourseCount)
					{
						for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
						{
							AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
								BlockUnitsCM * 0.5 + GeometryToleranceCM));
						}
					}
				}
				else if (Member.OwnerKind == EOwnerKind::ShellFace
					&& Member.Role == EABTSM73BeamAMemberRole::Post)
				{
					const FABTSM73DAG5BV2Volume* Source =
						FindVolume(Silhouette, Member.SourceVolumeId);
					const double SliceZCM = Root.GroundZCM
						+ (Member.CourseIndex + 1.0) * BlockUnitsCM;
					const FVector Node = MemberBox.GetCenter();
					const double NodeHaloCM = BlockUnitsCM * 0.5 + GeometryToleranceCM;
					if (Source == nullptr || Member.Axis != EABTSM73BeamAFrameAxis::Z
						|| Member.FaceMask == 0 || Member.RequiredLowerMemberIndices.IsEmpty()
						|| SliceZCM < Source->LocalBounds.Min.Z - GeometryToleranceCM
						|| SliceZCM > Source->LocalBounds.Max.Z + GeometryToleranceCM
						|| Node.X < Source->LocalBounds.Min.X - NodeHaloCM
						|| Node.X > Source->LocalBounds.Max.X + NodeHaloCM
						|| Node.Y < Source->LocalBounds.Min.Y - NodeHaloCM
						|| Node.Y > Source->LocalBounds.Max.Y + NodeHaloCM)
					{
						++Plan.Summary.EnvelopeViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3EnvelopeViolation:Reason=InvalidExteriorPostSource:Member=%d:Source=%d:SliceZ=%.3f"),
							MemberIndex, Member.SourceVolumeId, SliceZCM);
						return false;
					}
					// A legal exterior node may sit half a brick outside the source;
					// include the post's own half-width while binding the projection
					// exclusively to that source rather than the component union.
					const double SolidMarginCM = BlockUnitsCM + GeometryToleranceCM;
					AllowedBoxes.Add(FBox(
						FVector(Source->LocalBounds.Min.X - SolidMarginCM,
							Source->LocalBounds.Min.Y - SolidMarginCM,
							Root.GroundZCM - GeometryToleranceCM),
						FVector(Source->LocalBounds.Max.X + SolidMarginCM,
							Source->LocalBounds.Max.Y + SolidMarginCM,
							Source->LocalBounds.Max.Z + GeometryToleranceCM)));
				}
				else
				{
					TArray<const FABTSM73DAG5BV2Volume*> CoveredVolumes =
						Member.OwnerKind == EOwnerKind::Roof
						? Root.CrownVolumes : Root.BodyVolumes;
					if (Member.OwnerKind == EOwnerKind::Roof
						&& Member.CourseIndex
							== QRelativeFloor(Root.BodyTopCM, Root.GroundZCM))
					{
						// The first roof brick straddles the quantized Body/Crown seam.
						// Its lower solid may be owned by Body while its upper solid is
						// owned by Crown; later roof courses remain Crown-only.
						CoveredVolumes.Append(Root.BodyVolumes);
					}
					for (const FABTSM73DAG5BV2Volume* Volume : CoveredVolumes)
					{
						AllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
							BlockUnitsCM * 0.5 + GeometryToleranceCM));
					}
					if (Member.OwnerKind == EOwnerKind::Roof)
					{
						const FABTSM73DAG5BV2Volume* Source =
							FindVolume(Silhouette, Member.SourceVolumeId);
						if (Source != nullptr
							&& (Member.Axis == EABTSM73BeamAFrameAxis::X
								|| Member.Axis == EABTSM73BeamAFrameAxis::Y))
						{
							// The topology contract permits a roof course to extend
							// only along its bearing axis far enough to cover the two
							// certified lower-seat stations. Mirror that exact exception
							// here instead of increasing the global envelope tolerance.
							FBox SeatEnvelope = Source->LocalBounds;
							const int32 AxisIndex = static_cast<int32>(Member.Axis);
							for (const int32 LowerIndex : Member.RequiredLowerMemberIndices)
							{
								if (!Plan.Members.IsValidIndex(LowerIndex))
								{
									continue;
								}
								const double Station = PlannedMemberBounds(
									Plan.Members[LowerIndex]).GetCenter()[AxisIndex];
								SeatEnvelope.Min[AxisIndex] = FMath::Min(
									SeatEnvelope.Min[AxisIndex], Station);
								SeatEnvelope.Max[AxisIndex] = FMath::Max(
									SeatEnvelope.Max[AxisIndex], Station);
							}
							AllowedBoxes.Add(SeatEnvelope.ExpandBy(
								BlockUnitsCM * 0.5 + GeometryToleranceCM));
						}
					}
				}
			}
			FVector UncoveredPoint = MemberBox.GetCenter();
			const bool bCovered = SolidCoveredByBoxes(
				MemberBox, AllowedBoxes, UncoveredPoint);
			if (!bCovered)
			{
				const FABTSM73DAG5BV2Volume* SourceVolume =
					FindVolume(Silhouette, Member.SourceVolumeId);
				const FBox SourceBounds = SourceVolume != nullptr
					? SourceVolume->LocalBounds : FBox(EForceInit::ForceInit);
				++Plan.Summary.EnvelopeViolationCount;
				OutError = FString::Printf(
					TEXT("BeamC3V3EnvelopeViolation:Member=%d:Source=%d:Owner=%d:Kind=%d:Axis=%d:Course=%d:Uncovered=%.3f,%.3f,%.3f:MemberBounds=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f:SourceBounds=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f"),
					MemberIndex, Member.SourceVolumeId, static_cast<int32>(Member.OwnerKind),
					static_cast<int32>(Member.SkeletonKind), static_cast<int32>(Member.Axis),
					Member.CourseIndex,
					UncoveredPoint.X, UncoveredPoint.Y, UncoveredPoint.Z,
					MemberBox.Min.X, MemberBox.Min.Y, MemberBox.Min.Z,
					MemberBox.Max.X, MemberBox.Max.Y, MemberBox.Max.Z,
					SourceBounds.Min.X, SourceBounds.Min.Y, SourceBounds.Min.Z,
					SourceBounds.Max.X, SourceBounds.Max.Y, SourceBounds.Max.Z);
				return false;
			}
			for (const FABTSM73BeamASupportVoid& Void : Plan.ReservedSupportVoids)
			{
				if (BoxesPenetrate(MemberBox, Void.Bounds))
				{
					++Plan.Summary.ProtectedVoidViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3ProtectedVoidViolation:Member=%d:Span=%d:Role=%d:OwnerKind=%d:Owner=%d:Component=%d:Source=%d:Axis=%d:Course=%d:MemberBounds=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f:VoidBounds=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f"),
						MemberIndex, Void.SpanSourceVolumeId,
						static_cast<int32>(Member.Role),
						static_cast<int32>(Member.OwnerKind), Member.OwnerId,
						Member.ComponentId, Member.SourceVolumeId,
						static_cast<int32>(Member.Axis), Member.CourseIndex,
						MemberBox.Min.X, MemberBox.Min.Y, MemberBox.Min.Z,
						MemberBox.Max.X, MemberBox.Max.Y, MemberBox.Max.Z,
						Void.Bounds.Min.X, Void.Bounds.Min.Y, Void.Bounds.Min.Z,
						Void.Bounds.Max.X, Void.Bounds.Max.Y, Void.Bounds.Max.Z);
					return false;
				}
			}
		}
		return true;
	}

	uint64 BearingPairKey(const int32 LowerMemberId, const int32 UpperMemberId)
	{
		return (static_cast<uint64>(static_cast<uint32>(LowerMemberId)) << 32)
			| static_cast<uint32>(UpperMemberId);
	}

	void PredictBearingPairs(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FPlan& Plan,
		TSet<uint64>& OutPairs,
		int64& OutPairChecks)
	{
		OutPairs.Reset();
		OutPairChecks = 0;
		const double Scale = 1.0 / FMath::Max(0.1,
			static_cast<double>(Settings.JointMergeToleranceCM));
		TMap<int64, TArray<int32>> MembersByTop;
		TMap<int64, TArray<int32>> MembersByBottom;
		TArray<FBox> MemberBounds;
		MemberBounds.Reserve(Plan.Members.Num());
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FBox Bounds = PlannedMemberBounds(Plan.Members[MemberIndex]);
			MemberBounds.Add(Bounds);
			MembersByTop.FindOrAdd(FMath::RoundToInt64(Bounds.Max.Z * Scale)).Add(MemberIndex);
			MembersByBottom.FindOrAdd(FMath::RoundToInt64(Bounds.Min.Z * Scale)).Add(MemberIndex);
		}
		for (const TPair<int64, TArray<int32>>& Top : MembersByTop)
		{
			const TArray<int32>* Bottom = MembersByBottom.Find(Top.Key);
			if (Bottom == nullptr)
			{
				continue;
			}
			for (const int32 LowerIndex : Top.Value)
			{
				for (const int32 UpperIndex : *Bottom)
				{
					++OutPairChecks;
					if (LowerIndex == UpperIndex)
					{
						continue;
					}
					const FBox& Lower = MemberBounds[LowerIndex];
					const FBox& Upper = MemberBounds[UpperIndex];
					if (SkeletonV3OverlapLength(
						Lower.Min.X, Lower.Max.X, Upper.Min.X, Upper.Max.X)
						> Settings.JointMergeToleranceCM
						&& SkeletonV3OverlapLength(
							Lower.Min.Y, Lower.Max.Y, Upper.Min.Y, Upper.Max.Y)
							> Settings.JointMergeToleranceCM)
					{
						OutPairs.Add(BearingPairKey(LowerIndex, UpperIndex));
					}
				}
			}
		}
	}

	bool ValidateActualBearingPairs(
		const FABTSM73BeamAPreviewSettings& Settings,
		const FPlan& Plan,
		const TArray<FABTSM73BeamABearingContact>& Contacts,
		FString& OutError)
	{
		TSet<uint64> PredictedPairs;
		int64 IgnoredPairChecks = 0;
		PredictBearingPairs(Settings, Plan, PredictedPairs, IgnoredPairChecks);
		if (PredictedPairs.Num() != Plan.Summary.PlannedBearingContactCount)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3BearingPredictionDrift:Predicted=%d:Planned=%d"),
				PredictedPairs.Num(), Plan.Summary.PlannedBearingContactCount);
			return false;
		}
		TSet<uint64> ActualPairs;
		for (const FABTSM73BeamABearingContact& Contact : Contacts)
		{
			if (!Plan.Members.IsValidIndex(Contact.LowerMemberId)
				|| !Plan.Members.IsValidIndex(Contact.UpperMemberId)
				|| Contact.LowerMemberId == Contact.UpperMemberId)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3InvalidBearingContact:Lower=%d:Upper=%d"),
					Contact.LowerMemberId, Contact.UpperMemberId);
				return false;
			}
			const uint64 Pair = BearingPairKey(Contact.LowerMemberId, Contact.UpperMemberId);
			if (ActualPairs.Contains(Pair))
			{
				OutError = FString::Printf(TEXT("BeamC3V3DuplicateBearingContact:Pair=%llu"), Pair);
				return false;
			}
			ActualPairs.Add(Pair);
		}
		if (ActualPairs.Num() != PredictedPairs.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3BearingContactSetMismatch:Actual=%d:Predicted=%d"),
				ActualPairs.Num(), PredictedPairs.Num());
			return false;
		}
		for (const uint64 Pair : PredictedPairs)
		{
			if (!ActualPairs.Contains(Pair))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3BearingContactSetMismatch:MissingPair=%llu"), Pair);
				return false;
			}
		}
		return true;
	}

	bool ResolveGroundedMemberMask(
		const FPlan& Plan,
		TArray<bool>& OutGroundedMembers,
		FString& OutError)
	{
		OutGroundedMembers.Init(false, Plan.Members.Num());
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FPlannedMember& Member = Plan.Members[MemberIndex];
			bool bGrounded = Member.bRequiresGroundSeat;
			if (!bGrounded && !Member.RequiredLowerMemberIndices.IsEmpty())
			{
				bGrounded = true;
				for (const int32 Lower : Member.RequiredLowerMemberIndices)
				{
					bGrounded &= OutGroundedMembers.IsValidIndex(Lower)
						&& OutGroundedMembers[Lower];
				}
			}
			OutGroundedMembers[MemberIndex] = bGrounded;
			if (!bGrounded)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SeatDAGUnreachableMember:Member=%d:Component=%d:Owner=%d"),
					MemberIndex, Member.ComponentId, Member.OwnerId);
				return false;
			}
		}
		return true;
	}

	bool ValidateSkeletonTopology(
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const TArray<FRoot>& Roots,
		FPlan& Plan,
		FString& OutError)
	{
		Plan.Summary.ExplicitCoreCellCount = Plan.CoreCells.Num();
		Plan.Summary.CoreCellCount = Plan.CoreCells.Num();
		Plan.Summary.GroundedCoreCellCount = 0;
		Plan.Summary.ShellMemberCount = 0;
		Plan.Summary.CoreDerivedShellMemberCount = 0;
		Plan.Summary.SharedCourseCount = 0;
		Plan.Summary.SharedCourseSegmentCount = 0;
		Plan.Summary.SharedCourseCrossCoreSegmentCount = 0;
		Plan.Summary.SharedCourseConflictOmissionCount = 0;
		Plan.Summary.SharedCourseNonCoreEndpointViolationCount = 0;
		Plan.Summary.SharedCourseReplacementSlotCount = 0;
		Plan.Summary.SharedCourseBandViolationCount = 0;
		Plan.Summary.BuildingGroupCount = Plan.BuildingGroups.Num();
		Plan.Summary.SuspendedCoreCount = 0;
		if (Plan.BuildingGroups.Num() != 1)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3BuildingGroupCountInvalid:Groups=%d"),
				Plan.BuildingGroups.Num());
			return false;
		}
		const FBuildingGroupPlan& BuildingGroup = Plan.BuildingGroups[0];
		TSet<int32> DeclaredGroupComponents;
		TSet<int32> DeclaredGroupCores;
		for (const int32 ComponentId : BuildingGroup.ComponentIds)
		{
			DeclaredGroupComponents.Add(ComponentId);
		}
		for (const int32 CoreCellId : BuildingGroup.CoreCellIds)
		{
			DeclaredGroupCores.Add(CoreCellId);
		}
		if (BuildingGroup.GroupId != 0
			|| BuildingGroup.MemberIndices.IsEmpty()
			|| DeclaredGroupComponents.Num() != Plan.Components.Num()
			|| DeclaredGroupCores.Num() != Plan.CoreCells.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3BuildingGroupMembershipInvalid:Group=%d:Members=%d:Components=%d/%d:Cores=%d/%d"),
				BuildingGroup.GroupId, BuildingGroup.MemberIndices.Num(),
				DeclaredGroupComponents.Num(), Plan.Components.Num(),
				DeclaredGroupCores.Num(), Plan.CoreCells.Num());
			return false;
		}
		for (int32 ComponentId = 0; ComponentId < Plan.Components.Num(); ++ComponentId)
		{
			if (!DeclaredGroupComponents.Contains(ComponentId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3BuildingGroupComponentMissing:Group=0:Component=%d"),
					ComponentId);
				return false;
			}
		}
		for (int32 CoreCellId = 0; CoreCellId < Plan.CoreCells.Num(); ++CoreCellId)
		{
			if (!DeclaredGroupCores.Contains(CoreCellId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3BuildingGroupCoreMissing:Group=0:Core=%d"), CoreCellId);
				return false;
			}
		}
		if (Plan.CoreCells.Num() < Plan.Components.Num())
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3ExplicitCoreCountMismatch:Cores=%d:Components=%d"),
				Plan.CoreCells.Num(), Plan.Components.Num());
			return false;
		}

		TSet<int32> DeclaredCoreMembers;
		TMap<int32, int32> CoreSlotReferenceCounts;
		TArray<int32> CoreCountByComponent;
		CoreCountByComponent.Init(0, Plan.Components.Num());
		TArray<TArray<int32>> RoofCoursesByComponent;
		RoofCoursesByComponent.SetNum(Plan.Components.Num());
		for (const FPlannedMember& Member : Plan.Members)
		{
			if (Member.SkeletonKind == ESkeletonMemberKind::RoofCourse
				&& RoofCoursesByComponent.IsValidIndex(Member.ComponentId))
			{
				RoofCoursesByComponent[Member.ComponentId].AddUnique(Member.CourseIndex);
			}
		}
		for (TArray<int32>& Courses : RoofCoursesByComponent)
		{
			Courses.Sort();
		}
		TArray<int32> RequiredCoreTopCourses;
		RequiredCoreTopCourses.Init(0, Plan.CoreCells.Num());
		for (const FPlannedMember& Member : Plan.Members)
		{
			if (Member.SkeletonKind != ESkeletonMemberKind::SharedCourse)
			{
				continue;
			}
			for (const int32 EndpointCoreCellId : Member.EndpointCoreCellIds)
			{
				if (RequiredCoreTopCourses.IsValidIndex(EndpointCoreCellId))
				{
					RequiredCoreTopCourses[EndpointCoreCellId] = FMath::Max(
						RequiredCoreTopCourses[EndpointCoreCellId], Member.CourseIndex + 2);
				}
			}
		}
		for (int32 CoreIndex = 0; CoreIndex < Plan.CoreCells.Num(); ++CoreIndex)
		{
			const FCoreCellPlan& Core = Plan.CoreCells[CoreIndex];
			if (Core.CoreCellId != CoreIndex
				|| !Plan.Components.IsValidIndex(Core.ComponentId)
				|| !Roots.IsValidIndex(Core.ComponentId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreIdentityInvalid:Core=%d:Stored=%d:Component=%d"),
					CoreIndex, Core.CoreCellId, Core.ComponentId);
				return false;
			}
			const FComponentPlan& Component = Plan.Components[Core.ComponentId];
			const FRoot& Root = Roots[Core.ComponentId];
			++CoreCountByComponent[Core.ComponentId];
			if (!Component.GroundSourceVolumeIds.Contains(Core.BodySourceVolumeId)
				|| Component.CrownVolumeIds.Contains(Core.BodySourceVolumeId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreSourceNotBody:CoreCell=%d:Source=%d"),
					Core.CoreCellId, Core.BodySourceVolumeId);
				return false;
			}
			if (!Core.LocalBounds.IsValid
				|| FMath::Abs(Core.LocalBounds.Min.Z - Component.GroundPlaneZCM)
					> GeometryToleranceCM
				|| Core.CoreMergeRegionId != Component.CoreMergeRegionId
				|| !Plan.CoreMergeRegions.IsValidIndex(Core.CoreMergeRegionId)
				|| Core.RailCount < 2 || Core.RailCount > 5
				|| Core.XStations.Num() != Core.RailCount
				|| Core.YStations.Num() != Core.RailCount
				|| Core.XStations[0] * BlockUnitsCM != Core.LocalBounds.Min.X
				|| Core.XStations.Last() * BlockUnitsCM != Core.LocalBounds.Max.X
				|| Core.YStations[0] * BlockUnitsCM != Core.LocalBounds.Min.Y
				|| Core.YStations.Last() * BlockUnitsCM != Core.LocalBounds.Max.Y
				|| Core.MemberIndices.IsEmpty()
				|| Core.MemberIndices.Num() % Core.RailCount != 0)
			{
				++Plan.Summary.SuspendedCoreCount;
				OutError = FString::Printf(
					TEXT("BeamC3V3SuspendedCore:CoreCell=%d:Ground=%.3f:MinZ=%.3f:Members=%d"),
					Core.CoreCellId, Component.GroundPlaneZCM,
					Core.LocalBounds.IsValid ? Core.LocalBounds.Min.Z : DBL_MAX,
					Core.MemberIndices.Num());
				return false;
			}
			const FVector BodySize = Component.BodyBounds.GetSize();
			const FVector CoreSize = Core.LocalBounds.GetSize();
			if ((BodySize.X + GeometryToleranceCM >= CoreSize.X + 2.0 * BlockUnitsCM
					&& (Core.LocalBounds.Min.X < Component.BodyBounds.Min.X + BlockUnitsCM
						- GeometryToleranceCM
						|| Core.LocalBounds.Max.X > Component.BodyBounds.Max.X - BlockUnitsCM
							+ GeometryToleranceCM))
				|| (BodySize.Y + GeometryToleranceCM >= CoreSize.Y + 2.0 * BlockUnitsCM
					&& (Core.LocalBounds.Min.Y < Component.BodyBounds.Min.Y + BlockUnitsCM
						- GeometryToleranceCM
						|| Core.LocalBounds.Max.Y > Component.BodyBounds.Max.Y - BlockUnitsCM
							+ GeometryToleranceCM)))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreFootprintConsumesBody:CoreCell=%d"), Core.CoreCellId);
				return false;
			}

			TMap<int32, TArray<int32>> MembersByCourse;
			const int32 BodyCourseCount = Core.BodyTopCourseIndex;
			const int32 ExpectedCourseCount = Core.TopCourseIndex;
			const double ExpectedCoreTopZ = Component.GroundPlaneZCM
				+ ExpectedCourseCount * BlockUnitsCM;
			if (FMath::Abs(Core.LocalBounds.Max.Z - ExpectedCoreTopZ)
					> GeometryToleranceCM
				|| BodyCourseCount <= 0 || BodyCourseCount > ExpectedCourseCount
				|| ExpectedCourseCount <= 0
				|| ExpectedCourseCount < RequiredCoreTopCourses[CoreIndex]
				|| Core.MemberIndices.Num() != ExpectedCourseCount * Core.RailCount)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CoreHeightContractInvalid:CoreCell=%d:Members=%d:BodyCourses=%d:RequiredCourses=%d:ActualTop=%.3f:ExpectedTop=%.3f"),
					Core.CoreCellId, Core.MemberIndices.Num(), BodyCourseCount,
					ExpectedCourseCount, Core.LocalBounds.Max.Z, ExpectedCoreTopZ);
				return false;
			}
			for (const int32 MemberIndex : Core.MemberIndices)
			{
				if (!Plan.Members.IsValidIndex(MemberIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CoreMemberReferenceInvalid:CoreCell=%d:Member=%d"),
						Core.CoreCellId, MemberIndex);
					return false;
				}
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bLocalCoreCourse =
					Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
					&& Member.OwnerKind == EOwnerKind::CoreCell
					&& Member.OwnerId == Core.CoreCellId
					&& Member.ComponentId == Core.ComponentId
					&& Member.OriginCoreCellId == Core.CoreCellId
					&& Member.Role == EABTSM73BeamAMemberRole::CoreCourse;
				const bool bSharedCoreSlot =
					Member.SkeletonKind == ESkeletonMemberKind::SharedCourse
					&& Member.OwnerKind == EOwnerKind::SupportedSpan
					&& Member.ComponentId == INDEX_NONE
					&& Member.Role == EABTSM73BeamAMemberRole::BridgeRail
					&& Member.EndpointCoreCellIds.Contains(Core.CoreCellId);
				if ((!bLocalCoreCourse && !bSharedCoreSlot)
					|| (Member.Axis != EABTSM73BeamAFrameAxis::X
						&& Member.Axis != EABTSM73BeamAFrameAxis::Y))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CoreMemberContractInvalid:CoreCell=%d:Member=%d:Kind=%d:Owner=%d:Axis=%d"),
						Core.CoreCellId, MemberIndex, static_cast<int32>(Member.SkeletonKind),
						static_cast<int32>(Member.OwnerKind), static_cast<int32>(Member.Axis));
					return false;
				}
				int32& ReferenceCount = CoreSlotReferenceCounts.FindOrAdd(MemberIndex);
				if (bLocalCoreCourse && ReferenceCount != 0)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3LocalCoreSlotAliased:CoreCell=%d:Member=%d"),
						Core.CoreCellId, MemberIndex);
					return false;
				}
				++ReferenceCount;
				DeclaredCoreMembers.Add(MemberIndex);
				if (bSharedCoreSlot)
				{
					++Plan.Summary.SharedCourseReplacementSlotCount;
					const FBox SharedBounds = PlannedMemberBounds(Member);
					const int32 AxisIndex = static_cast<int32>(Member.Axis);
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					const TArray<int32>& ExpectedStations = AxisIndex == 0
						? Core.YStations : Core.XStations;
					const int32 ActualStation = FMath::RoundToInt(
						SharedBounds.GetCenter()[CrossAxisIndex] / BlockUnitsCM);
					const FSharedCourseLanePlan* Lane = FindSharedLanePlan(Plan, Member);
					if (!ExpectedStations.Contains(ActualStation) || Lane == nullptr
						|| Lane->RequiredMinimumCM
							> Core.LocalBounds.Min[AxisIndex]
								- BlockUnitsCM * 0.5 + GeometryToleranceCM
						|| Lane->RequiredMaximumCM
							< Core.LocalBounds.Max[AxisIndex]
								+ BlockUnitsCM * 0.5 - GeometryToleranceCM)
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedCourseDoesNotTraverseCore:Core=%d:Member=%d:Station=%d:Lane=%.3f..%.3f:Core=%.3f..%.3f"),
							Core.CoreCellId, MemberIndex, ActualStation,
							Lane != nullptr ? Lane->RequiredMinimumCM : DBL_MAX,
							Lane != nullptr ? Lane->RequiredMaximumCM : -DBL_MAX,
							Core.LocalBounds.Min[AxisIndex], Core.LocalBounds.Max[AxisIndex]);
						return false;
					}
				}
				if (bLocalCoreCourse
					&& (!Root.SourceVolumeIds.Contains(Member.SourceVolumeId)
					|| (Member.CourseIndex < BodyCourseCount
						&& Root.CrownVolumeIds.Contains(Member.SourceVolumeId))))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CoreSourceContractInvalid:Member=%d:Course=%d:BodyCourses=%d:Source=%d"),
						MemberIndex, Member.CourseIndex, BodyCourseCount,
						Member.SourceVolumeId);
					return false;
				}
				const FBox Bounds = PlannedMemberBounds(Member);
				if (Member.CourseIndex == 0
					&& (!Member.bRequiresGroundSeat
						|| FMath::Abs(Bounds.Min.Z - Component.GroundPlaneZCM)
							> GeometryToleranceCM))
				{
					++Plan.Summary.SuspendedCoreCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3SuspendedCore:CoreCell=%d:Member=%d:Bottom=%.3f:Ground=%.3f"),
						Core.CoreCellId, MemberIndex, Bounds.Min.Z,
						Component.GroundPlaneZCM);
					return false;
				}
				MembersByCourse.FindOrAdd(Member.CourseIndex).Add(MemberIndex);
			}
			const int32 CourseCount = ExpectedCourseCount;
			for (int32 Course = 0; Course < CourseCount; ++Course)
			{
				const TArray<int32>* CourseMembers = MembersByCourse.Find(Course);
				const EABTSM73BeamAFrameAxis ExpectedAxis = (Course & 1) == 0
					? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
				if (CourseMembers == nullptr || CourseMembers->Num() != Core.RailCount
					|| CourseMembers->ContainsByPredicate(
						[&Plan, ExpectedAxis](const int32 MemberIndex)
						{
							return !Plan.Members.IsValidIndex(MemberIndex)
								|| Plan.Members[MemberIndex].Axis != ExpectedAxis;
						}))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CoreCourseSequenceInvalid:CoreCell=%d:Course=%d"),
						Core.CoreCellId, Course);
					return false;
				}
				if (Course > 0)
				{
					const TArray<int32>* LowerCourse = MembersByCourse.Find(Course - 1);
					for (const int32 MemberIndex : *CourseMembers)
					{
						for (const int32 Lower : *LowerCourse)
						{
							double OverlapX = 0.0;
							double OverlapY = 0.0;
							if (!LogicalMembersHaveBearing(Plan, MemberIndex, Lower,
								OverlapX, OverlapY))
							{
								OutError = FString::Printf(
									TEXT("BeamC3V3CoreBearingAreaInsufficient:Core=%d:Course=%d:Member=%d:Lower=%d:Overlap=%.3fx%.3f:Required=%d"),
									Core.CoreCellId, Course, MemberIndex, Lower,
									OverlapX, OverlapY, BlockUnitsCM);
								return false;
							}
						}
					}
				}
			}
			++Plan.Summary.GroundedCoreCellCount;
		}
		for (int32 ComponentId = 0; ComponentId < CoreCountByComponent.Num(); ++ComponentId)
		{
			if (CoreCountByComponent[ComponentId] < 1)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3ComponentHasNoExplicitCore:Component=%d"), ComponentId);
				return false;
			}
		}

		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FPlannedMember& Member = Plan.Members[MemberIndex];
			if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse)
			{
				if (!DeclaredCoreMembers.Contains(MemberIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3UndeclaredCoreCourse:Member=%d"), MemberIndex);
					return false;
				}
				continue;
			}
			const bool bShell = Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::ExteriorPost
				|| Member.SkeletonKind == ESkeletonMemberKind::FloorCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm;
			if (bShell)
			{
				++Plan.Summary.ShellMemberCount;
				const bool bRequiresLocalCore =
					Member.OwnerKind != EOwnerKind::BuildingGroupShell
					&& Member.OwnerKind != EOwnerKind::SupportedSpan;
				if (!Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId)
					|| (bRequiresLocalCore
						&& Plan.CoreCells[Member.OriginCoreCellId].ComponentId
							!= Member.ComponentId)
					|| Member.RequiredInwardMemberIndices.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3ShellOriginMissing:Member=%d:Origin=%d:Inward=%d"),
						MemberIndex, Member.OriginCoreCellId,
						Member.RequiredInwardMemberIndices.Num());
					return false;
				}
			}
			if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
			{
				++Plan.Summary.SharedCourseSegmentCount;
				const EABTSM73BeamAFrameAxis ExpectedSharedAxis =
					(Member.CourseIndex & 1) == 0
						? EABTSM73BeamAFrameAxis::X
						: EABTSM73BeamAFrameAxis::Y;
				const int32* SlotReferenceCount =
					CoreSlotReferenceCounts.Find(MemberIndex);
				const FSharedCourseLanePlan* Lane = FindSharedLanePlan(Plan, Member);
				if (Member.EndpointCoreCellIds.Num() != 2
					|| Member.EndpointCoreCellIds[0] == Member.EndpointCoreCellIds[1]
					|| Member.CourseIndex <= 0
					|| Member.Axis != ExpectedSharedAxis
					|| Lane == nullptr
					|| Member.SharedSegmentIndex == INDEX_NONE
					|| Member.SharedLaneIndex == INDEX_NONE)
				{
					++Plan.Summary.SharedCourseNonCoreEndpointViolationCount;
					++Plan.Summary.SharedCourseBandViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedCourseEndpointInvalid:Member=%d:Cores=%d:Seats=%d:Axis=%d:ExpectedAxis=%d:Slots=%d"),
						MemberIndex, Member.EndpointCoreCellIds.Num(),
						Member.RequiredLowerMemberIndices.Num(),
						static_cast<int32>(Member.Axis), static_cast<int32>(ExpectedSharedAxis),
						SlotReferenceCount != nullptr ? *SlotReferenceCount : 0);
					return false;
				}
				if (!Member.bSharedCrossCoreSegment)
				{
					if (SlotReferenceCount != nullptr && *SlotReferenceCount != 0)
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedTailClaimsCoreSlot:Member=%d:Slots=%d"),
							MemberIndex, *SlotReferenceCount);
						return false;
					}
					continue;
				}
				++Plan.Summary.SharedCourseCount;
				++Plan.Summary.SharedCourseCrossCoreSegmentCount;
				const int32 ExpectedSlotReferences =
					1 + (Lane->bReceiverSlotReplaced ? 1 : 0);
				if (Lane->CrossCoreSegmentMemberIndex != MemberIndex
					|| SlotReferenceCount == nullptr
					|| *SlotReferenceCount != ExpectedSlotReferences)
				{
					++Plan.Summary.SharedCourseBandViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedCrossSegmentSlotInvalid:Member=%d:Expected=%d:Actual=%d"),
						MemberIndex, ExpectedSlotReferences,
						SlotReferenceCount != nullptr ? *SlotReferenceCount : 0);
					return false;
				}
				TArray<TPair<FVector2D, int32>> LaneIntervals;
				int32 CrossSegmentCount = 0;
				for (const int32 SegmentIndex : Lane->SegmentMemberIndices)
				{
					if (!Plan.Members.IsValidIndex(SegmentIndex))
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedLaneSegmentInvalid:Member=%d:Segment=%d"),
							MemberIndex, SegmentIndex);
						return false;
					}
					const FPlannedMember& Segment = Plan.Members[SegmentIndex];
					const FBox SegmentBounds = PlannedMemberBounds(Segment);
					const int32 LaneAxisIndex = static_cast<int32>(Member.Axis);
					const int32 LaneCrossAxisIndex = LaneAxisIndex == 0 ? 1 : 0;
					if (Segment.SkeletonKind != ESkeletonMemberKind::SharedCourse
						|| Segment.OwnerId != Member.OwnerId
						|| Segment.CourseIndex != Member.CourseIndex
						|| Segment.SharedLaneIndex != Member.SharedLaneIndex
						|| Segment.EndpointCoreCellIds != Member.EndpointCoreCellIds
						|| FMath::Abs(SegmentBounds.GetCenter()[LaneCrossAxisIndex]
							- Lane->CrossStationCM) > GeometryToleranceCM
						|| SegmentBounds.GetSize()[LaneAxisIndex]
							> 720.0 + GeometryToleranceCM)
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedLaneSegmentContractInvalid:Member=%d:Segment=%d"),
							MemberIndex, SegmentIndex);
						return false;
					}
					CrossSegmentCount += Segment.bSharedCrossCoreSegment ? 1 : 0;
					LaneIntervals.Emplace(FVector2D(
						SegmentBounds.Min[LaneAxisIndex],
						SegmentBounds.Max[LaneAxisIndex]), SegmentIndex);
				}
				LaneIntervals.Sort([](const TPair<FVector2D, int32>& A,
					const TPair<FVector2D, int32>& B)
				{
					return !FMath::IsNearlyEqual(A.Key.X, B.Key.X,
						GeometryToleranceCM) ? A.Key.X < B.Key.X : A.Value < B.Value;
				});
				bool bContinuousLane = !LaneIntervals.IsEmpty()
					&& FMath::Abs(LaneIntervals[0].Key.X - Lane->RequiredMinimumCM)
						<= GeometryToleranceCM
					&& FMath::Abs(LaneIntervals.Last().Key.Y - Lane->RequiredMaximumCM)
						<= GeometryToleranceCM;
				for (int32 SegmentOrdinal = 1;
					SegmentOrdinal < LaneIntervals.Num(); ++SegmentOrdinal)
				{
					bContinuousLane &= FMath::Abs(
						LaneIntervals[SegmentOrdinal - 1].Key.Y
						- LaneIntervals[SegmentOrdinal].Key.X) <= GeometryToleranceCM;
				}
				const FBox CrossBounds = PlannedMemberBounds(Member);
				const int32 LaneAxisIndex = static_cast<int32>(Member.Axis);
				for (const int32 EndpointCoreCellId : Member.EndpointCoreCellIds)
				{
					if (!Plan.CoreCells.IsValidIndex(EndpointCoreCellId))
					{
						bContinuousLane = false;
						continue;
					}
					const FCoreCellPlan& Endpoint = Plan.CoreCells[EndpointCoreCellId];
					bContinuousLane &= SkeletonV3OverlapLength(
						CrossBounds.Min[LaneAxisIndex], CrossBounds.Max[LaneAxisIndex],
						Endpoint.LocalBounds.Min[LaneAxisIndex] - BlockUnitsCM * 0.5,
						Endpoint.LocalBounds.Max[LaneAxisIndex] + BlockUnitsCM * 0.5)
						>= BlockUnitsCM - GeometryToleranceCM;
				}
				if (!bContinuousLane || CrossSegmentCount != 1)
				{
					++Plan.Summary.SharedCourseBandViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedLaneContinuityInvalid:Member=%d:Segments=%d:Cross=%d:Required=%.3f..%.3f"),
						MemberIndex, LaneIntervals.Num(), CrossSegmentCount,
						Lane->RequiredMinimumCM, Lane->RequiredMaximumCM);
					return false;
				}
				TArray<int32> SharedBandRails;
				for (int32 CandidateIndex = 0;
					CandidateIndex < Plan.Members.Num(); ++CandidateIndex)
				{
					const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
					if (Candidate.SkeletonKind == ESkeletonMemberKind::SharedCourse
						&& Candidate.OwnerId == Member.OwnerId
						&& Candidate.CourseIndex == Member.CourseIndex
						&& Candidate.Axis == Member.Axis
						&& Candidate.bSharedCrossCoreSegment)
					{
						SharedBandRails.Add(CandidateIndex);
					}
				}
				SharedBandRails.Sort();
				const int32 ExpectedSharedRailCount =
					Plan.CoreCells.IsValidIndex(Lane->DonorCoreCellId)
						? Plan.CoreCells[Lane->DonorCoreCellId].RailCount : 0;
				if (SharedBandRails.Num() != ExpectedSharedRailCount)
				{
					++Plan.Summary.SharedCourseBandViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedCourseRailPairInvalid:Member=%d:Owner=%d:Course=%d:Rails=%d"),
						MemberIndex, Member.OwnerId, Member.CourseIndex,
						SharedBandRails.Num());
					return false;
				}
				for (const int32 EndpointCoreCellId : Member.EndpointCoreCellIds)
				{
					if (!Plan.CoreCells.IsValidIndex(EndpointCoreCellId))
					{
						++Plan.Summary.SharedCourseNonCoreEndpointViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedCourseEndpointCoreInvalid:Member=%d:Core=%d"),
							MemberIndex, EndpointCoreCellId);
						return false;
					}
					const FCoreCellPlan& EndpointCore = Plan.CoreCells[EndpointCoreCellId];
					int32 SharedSlotCount = 0;
					TArray<int32> LowerCoreRails;
					TArray<int32> UpperCoreRails;
					for (const int32 CoreMemberIndex : EndpointCore.MemberIndices)
					{
						SharedSlotCount += CoreMemberIndex == MemberIndex ? 1 : 0;
						if (!Plan.Members.IsValidIndex(CoreMemberIndex))
						{
							continue;
						}
						const FPlannedMember& CoreMember = Plan.Members[CoreMemberIndex];
						if (CoreMember.CourseIndex == Member.CourseIndex - 1)
						{
							LowerCoreRails.Add(CoreMemberIndex);
						}
						else if (CoreMember.CourseIndex == Member.CourseIndex + 1)
						{
							UpperCoreRails.Add(CoreMemberIndex);
						}
					}
					const int32 ExpectedEndpointSlotCount =
						EndpointCoreCellId == Lane->DonorCoreCellId ? 1
						: (Lane->bReceiverSlotReplaced ? 1 : 0);
					if (SharedSlotCount != ExpectedEndpointSlotCount
						|| LowerCoreRails.Num() != EndpointCore.RailCount
						|| UpperCoreRails.Num() != EndpointCore.RailCount)
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedCourseCoreBandIncomplete:Member=%d:Core=%d:Slot=%d:Lower=%d:Upper=%d"),
							MemberIndex, EndpointCoreCellId, SharedSlotCount,
							LowerCoreRails.Num(), UpperCoreRails.Num());
						return false;
					}
					for (const int32 LowerCoreRail : LowerCoreRails)
					{
						double OverlapX = 0.0;
						double OverlapY = 0.0;
						if (!LogicalMembersHaveBearing(Plan, MemberIndex,
							LowerCoreRail, OverlapX, OverlapY))
						{
							++Plan.Summary.SharedCourseNonCoreEndpointViolationCount;
							OutError = FString::Printf(
								TEXT("BeamC3V3SharedCourseLowerCoreSeatMissing:Member=%d:Core=%d:Lower=%d"),
								MemberIndex, EndpointCoreCellId, LowerCoreRail);
							return false;
						}
					}
					for (const int32 UpperCoreRail : UpperCoreRails)
					{
						for (const int32 SharedBandRail : SharedBandRails)
						{
							double OverlapX = 0.0;
							double OverlapY = 0.0;
							if (!LogicalMembersHaveBearing(Plan, UpperCoreRail,
								SharedBandRail, OverlapX, OverlapY))
							{
								++Plan.Summary.SharedCourseBandViolationCount;
								OutError = FString::Printf(
									TEXT("BeamC3V3SharedCourseUpperCoreClampMissing:Member=%d:Core=%d:Upper=%d:Shared=%d"),
									MemberIndex, EndpointCoreCellId,
									UpperCoreRail, SharedBandRail);
								return false;
							}
						}
					}
				}
			}
			if (Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm)
			{
				TArray<int32> SharedBandRails;
				for (int32 CandidateIndex = 0;
					CandidateIndex < Plan.Members.Num(); ++CandidateIndex)
				{
					const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
					if (Candidate.SkeletonKind == ESkeletonMemberKind::SharedCourse
						&& Candidate.OwnerId == Member.OwnerId
						&& Candidate.CourseIndex == Member.CourseIndex - 1
						&& Candidate.bSharedCrossCoreSegment)
					{
						SharedBandRails.Add(CandidateIndex);
					}
				}
				int32 ExpectedSharedRailCount = INDEX_NONE;
				const FSharedCourseIntent* Intent = Plan.SharedCourseIntents.FindByPredicate(
					[&Member](const FSharedCourseIntent& Candidate)
					{
						return Candidate.SpanVolumeId == Member.OwnerId;
					});
				if (Intent != nullptr)
				{
					for (const FSharedCourseLanePlan& Lane : Intent->LanePlans)
					{
						if (Lane.CourseIndex == Member.CourseIndex - 1
							&& Plan.CoreCells.IsValidIndex(Lane.DonorCoreCellId))
						{
							ExpectedSharedRailCount =
								Plan.CoreCells[Lane.DonorCoreCellId].RailCount;
							break;
						}
					}
				}
				if (ExpectedSharedRailCount < 2
					|| SharedBandRails.Num() != ExpectedSharedRailCount)
				{
					++Plan.Summary.SharedCourseBandViolationCount;
					OutError = FString::Printf(
						TEXT("BeamC3V3BridgeDiaphragmBandMissing:Member=%d:Owner=%d:Course=%d:Rails=%d"),
						MemberIndex, Member.OwnerId, Member.CourseIndex,
						SharedBandRails.Num());
					return false;
				}
				for (const int32 SharedBandRail : SharedBandRails)
				{
					if (!Member.RequiredLowerMemberIndices.Contains(SharedBandRail))
					{
						++Plan.Summary.SharedCourseBandViolationCount;
						OutError = FString::Printf(
							TEXT("BeamC3V3BridgeDiaphragmSeatMissing:Member=%d:Shared=%d"),
							MemberIndex, SharedBandRail);
						return false;
					}
				}
			}
			if (Member.SkeletonKind == ESkeletonMemberKind::RoofCourse)
			{
				const FABTSM73DAG5BV2Volume* Source =
					FindVolume(Silhouette, Member.SourceVolumeId);
				if (Member.OwnerKind != EOwnerKind::Roof
					|| !Roots.IsValidIndex(Member.ComponentId)
					|| !Roots[Member.ComponentId].CrownVolumeIds.Contains(Member.SourceVolumeId)
					|| Source == nullptr)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofCoreSeparationInvalid:Member=%d:Source=%d"),
						MemberIndex, Member.SourceVolumeId);
					return false;
				}
				const TArray<int32>& Courses = RoofCoursesByComponent[Member.ComponentId];
				const int32 Ordinal = Courses.IndexOfByKey(Member.CourseIndex);
				const EABTSM73BeamAFrameAxis ExpectedAxis = (Member.CourseIndex & 1) == 0
					? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
				if (Ordinal == INDEX_NONE || Member.Axis != ExpectedAxis
					|| !Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId)
					|| Member.RequiredLowerMemberIndices.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofGroundedCoreLineageInvalid:Member=%d:Course=%d:Ordinal=%d:Axis=%d:ExpectedAxis=%d:Core=%d:Seats=%d"),
						MemberIndex, Member.CourseIndex, Ordinal,
						static_cast<int32>(Member.Axis), static_cast<int32>(ExpectedAxis),
						Member.OriginCoreCellId, Member.RequiredLowerMemberIndices.Num());
					return false;
				}
				TArray<int32> RequiredRoofBearingSeats;
				if (Ordinal == 0)
				{
					for (const int32 CoreMemberIndex :
						Plan.CoreCells[Member.OriginCoreCellId].MemberIndices)
					{
						if (Plan.Members.IsValidIndex(CoreMemberIndex)
							&& Plan.Members[CoreMemberIndex].CourseIndex
								== Member.CourseIndex - 1)
						{
							RequiredRoofBearingSeats.Add(CoreMemberIndex);
						}
					}
				}
				else
				{
					for (int32 CandidateIndex = 0;
						CandidateIndex < Plan.Members.Num(); ++CandidateIndex)
					{
						const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
						if (Candidate.SkeletonKind == ESkeletonMemberKind::RoofCourse
							&& Candidate.ComponentId == Member.ComponentId
							&& Candidate.CourseIndex == Member.CourseIndex - 1)
						{
							RequiredRoofBearingSeats.Add(CandidateIndex);
						}
					}
				}
				const int32 ExpectedRoofSeatCount = Ordinal == 0
					? Plan.CoreCells[Member.OriginCoreCellId].RailCount : 2;
				if (RequiredRoofBearingSeats.Num() != ExpectedRoofSeatCount)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofRequiredBearingPairInvalid:Member=%d:Course=%d:Ordinal=%d:Required=%d"),
						MemberIndex, Member.CourseIndex, Ordinal,
						RequiredRoofBearingSeats.Num());
					return false;
				}
				for (const int32 RequiredSeat : RequiredRoofBearingSeats)
				{
					if (!Member.RequiredLowerMemberIndices.Contains(RequiredSeat))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3RoofGroundedCoreSeatInvalid:Member=%d:RequiredSeat=%d:Ordinal=%d"),
							MemberIndex, RequiredSeat, Ordinal);
						return false;
					}
				}
				FBox Expected = ABTSM73BeamA::SemanticRoofBearingCourseBounds(
					Source->LocalBounds, Source->Primitive, Ordinal, Courses.Num(),
					Member.Axis, BlockUnitsCM);
				const int32 AxisIndex = static_cast<int32>(Member.Axis);
				for (const int32 LowerIndex : RequiredRoofBearingSeats)
				{
					const double LowerStation = PlannedMemberBounds(
						Plan.Members[LowerIndex]).GetCenter()[AxisIndex];
					Expected.Min[AxisIndex] = FMath::Min(
						Expected.Min[AxisIndex], LowerStation);
					Expected.Max[AxisIndex] = FMath::Max(
						Expected.Max[AxisIndex], LowerStation);
				}
				const FBox Actual = PlannedMemberBounds(Member);
				const double Margin = BlockUnitsCM * 0.5 + GeometryToleranceCM;
				if (Ordinal == INDEX_NONE
					|| Actual.Min.X < Expected.Min.X - Margin
					|| Actual.Max.X > Expected.Max.X + Margin
					|| Actual.Min.Y < Expected.Min.Y - Margin
					|| Actual.Max.Y > Expected.Max.Y + Margin)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofTaperEnvelopeViolation:Member=%d:Course=%d:Ordinal=%d"),
						MemberIndex, Member.CourseIndex, Ordinal);
					return false;
				}
			}
		}
		int32 ExpectedSharedReplacementSlots = 0;
		int32 ExpectedSharedLanes = 0;
		int32 ExpectedSharedSegments = 0;
		int32 ExpectedCrossSegments = 0;
		int32 ExpectedConflictOmissions = 0;
		for (const FSharedCourseIntent& Intent : Plan.SharedCourseIntents)
		{
			for (const FSharedCourseLanePlan& Lane : Intent.LanePlans)
			{
				++ExpectedSharedLanes;
				ExpectedSharedSegments += Lane.SegmentMemberIndices.Num();
				++ExpectedCrossSegments;
				ExpectedSharedReplacementSlots +=
					1 + (Lane.bReceiverSlotReplaced ? 1 : 0);
				ExpectedConflictOmissions += Lane.bReceiverSlotReplaced ? 1 : 0;
			}
		}
		Plan.Summary.SharedCourseConflictOmissionCount = ExpectedConflictOmissions;
		if (Plan.Summary.SharedCourseReplacementSlotCount
			!= ExpectedSharedReplacementSlots
			|| Plan.Summary.SharedCourseCount != ExpectedSharedLanes
			|| Plan.Summary.SharedCourseSegmentCount != ExpectedSharedSegments
			|| Plan.Summary.SharedCourseCrossCoreSegmentCount != ExpectedCrossSegments
			|| Plan.Summary.SharedCourseBandViolationCount != 0)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3SharedCourseReplacementContractInvalid:Slots=%d/%d:Lanes=%d/%d:Segments=%d/%d:Cross=%d/%d:BandViolations=%d"),
				Plan.Summary.SharedCourseReplacementSlotCount,
				ExpectedSharedReplacementSlots,
				Plan.Summary.SharedCourseCount, ExpectedSharedLanes,
				Plan.Summary.SharedCourseSegmentCount, ExpectedSharedSegments,
				Plan.Summary.SharedCourseCrossCoreSegmentCount, ExpectedCrossSegments,
				Plan.Summary.SharedCourseBandViolationCount);
			return false;
		}

		// Prove every shell lineage reaches its declared core through an actual
		// seat edge or a collinear end contact.
		TSet<int32> Derived = DeclaredCoreMembers;
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			if (Plan.Members[MemberIndex].SkeletonKind
				== ESkeletonMemberKind::RoofCourse)
			{
				// Roof validation above has already proved its required two-member
				// bearing chain back to the declared core. A common-frame member may
				// therefore use that certified roof node as a real inward seat.
				Derived.Add(MemberIndex);
			}
		}
		bool bProgress = true;
		while (bProgress)
		{
			bProgress = false;
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bShell = Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::ExteriorPost
					|| Member.SkeletonKind == ESkeletonMemberKind::FloorCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm;
				if (!bShell || Derived.Contains(MemberIndex))
				{
					continue;
				}
				for (const int32 Inward : Member.RequiredInwardMemberIndices)
				{
					if (!Plan.Members.IsValidIndex(Inward) || !Derived.Contains(Inward))
					{
						continue;
					}
					const FPlannedMember& Lower = Plan.Members[Inward];
					const bool bSeatEdge = Member.RequiredLowerMemberIndices.Contains(Inward)
						|| Lower.RequiredLowerMemberIndices.Contains(MemberIndex);
					const bool bSameCourse = Member.Axis == Lower.Axis
						&& Member.Axis != EABTSM73BeamAFrameAxis::Z
						&& Member.CourseIndex == Lower.CourseIndex;
					const FBox A = PlannedMemberBounds(Member);
					const FBox B = PlannedMemberBounds(Lower);
					const int32 Axis = static_cast<int32>(Member.Axis);
					const int32 Other = Axis == 0 ? 1 : 0;
					const bool bEndContact = bSameCourse
						&& FMath::Abs(A.GetCenter()[Other] - B.GetCenter()[Other])
							<= GeometryToleranceCM
						&& (FMath::Abs(A.Max[Axis] - B.Min[Axis]) <= GeometryToleranceCM
							|| FMath::Abs(B.Max[Axis] - A.Min[Axis]) <= GeometryToleranceCM);
					if (bSeatEdge || bEndContact)
					{
						Derived.Add(MemberIndex);
						++Plan.Summary.CoreDerivedShellMemberCount;
						bProgress = true;
						break;
					}
				}
			}
		}
		if (Plan.Summary.CoreDerivedShellMemberCount != Plan.Summary.ShellMemberCount)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3ShellLineageDisconnected:Derived=%d:Shell=%d"),
				Plan.Summary.CoreDerivedShellMemberCount, Plan.Summary.ShellMemberCount);
			return false;
		}
		return true;
	}

	bool ValidateGroundedExteriorPostStations(
		FPlan& Plan,
		const TArray<bool>& GroundedMembers,
		FString& OutError)
	{
		constexpr uint8 FaceBits[] = {
			ABTSM73BeamC3V3::NegativeX, ABTSM73BeamC3V3::PositiveX,
			ABTSM73BeamC3V3::NegativeY, ABTSM73BeamC3V3::PositiveY};
		constexpr int32 FaceCount = UE_ARRAY_COUNT(FaceBits);
		TArray<TSet<FIntPoint>> StationSets;
		StationSets.SetNum(Plan.Components.Num() * FaceCount);
		TArray<TSet<FIntPoint>> GroupStationSets;
		GroupStationSets.SetNum(Plan.BuildingGroups.Num() * FaceCount);
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			const FPlannedMember& Member = Plan.Members[MemberIndex];
			if (!GroundedMembers.IsValidIndex(MemberIndex) || !GroundedMembers[MemberIndex]
				|| Member.Axis != EABTSM73BeamAFrameAxis::Z
				|| Member.SkeletonKind != ESkeletonMemberKind::ExteriorPost
				|| Member.Role != EABTSM73BeamAMemberRole::Post)
			{
				continue;
			}
			const FIntPoint Station(Member.StationA, Member.StationB);
			for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
			{
				if ((Member.FaceMask & FaceBits[FaceIndex]) != 0)
				{
					if (Plan.Components.IsValidIndex(Member.ComponentId))
					{
						StationSets[Member.ComponentId * FaceCount + FaceIndex].Add(Station);
					}
					if (Member.OwnerKind == EOwnerKind::BuildingGroupShell
						&& Plan.BuildingGroups.IsValidIndex(Member.OwnerId))
					{
						GroupStationSets[Member.OwnerId * FaceCount + FaceIndex].Add(Station);
					}
				}
			}
		}

		Plan.Summary.MinimumGroundedExteriorPostStationsPerFace = MAX_int32;
		for (FComponentPlan& Component : Plan.Components)
		{
			Component.GroundedExteriorPostStationCounts.Init(0, FaceCount);
			for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
			{
				const int32 Count = StationSets[
					Component.ComponentId * FaceCount + FaceIndex].Num();
				Component.GroundedExteriorPostStationCounts[FaceIndex] = Count;
			}
		}
		for (FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			Group.GroundedExteriorPostStationCounts.Init(0, FaceCount);
			Group.GroundedFaceMask = 0;
			for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
			{
				const int32 Count = GroupStationSets[
					Group.GroupId * FaceCount + FaceIndex].Num();
				Group.GroundedExteriorPostStationCounts[FaceIndex] = Count;
				Plan.Summary.MinimumGroundedExteriorPostStationsPerFace = FMath::Min(
					Plan.Summary.MinimumGroundedExteriorPostStationsPerFace, Count);
				if (Count >= 2)
				{
					Group.GroundedFaceMask |= FaceBits[FaceIndex];
				}
				else
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3BuildingGroupExteriorPostStationsInsufficient:Group=%d:Face=%d:Count=%d"),
						Group.GroupId, FaceIndex, Count);
					return false;
				}
			}
		}
		if (Plan.BuildingGroups.IsEmpty())
		{
			Plan.Summary.MinimumGroundedExteriorPostStationsPerFace = 0;
		}
		return true;
	}

	bool ValidatePreflightCaps(
		const FABTSM73BeamAPreviewSettings& Settings,
		FPlan& Plan,
		FString& OutError)
	{
		Plan.Summary.PlannedBayCount = Plan.Components.Num()
			+ Plan.BuildingGroups.Num() + Plan.Summary.SupportedSpanCount;
		if (Plan.Summary.PlannedBayCount > Settings.MaxBayCount)
		{
			OutError = FString::Printf(TEXT("BeamC3V3PreflightBayCap:%d/%d"),
				Plan.Summary.PlannedBayCount, Settings.MaxBayCount);
			return false;
		}
		if (Plan.Members.Num() > Settings.MaxMemberCount)
		{
			OutError = FString::Printf(TEXT("BeamC3V3PreflightMemberCap:%d/%d"),
				Plan.Members.Num(), Settings.MaxMemberCount);
			return false;
		}
		TSet<FString> JointKeys;
		const double Scale = 1.0 / FMath::Max(0.1, static_cast<double>(Settings.JointMergeToleranceCM));
		TSet<uint64> RequiredPairs;
		for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
		{
			const FPlannedMember& Member = Plan.Members[UpperIndex];
			for (const FVector& Point : {Member.LocalStart, Member.LocalEnd})
			{
				JointKeys.Add(FString::Printf(TEXT("%lld:%lld:%lld"),
					FMath::RoundToInt64(Point.X * Scale), FMath::RoundToInt64(Point.Y * Scale),
					FMath::RoundToInt64(Point.Z * Scale)));
			}
			for (const int32 LowerIndex : Member.RequiredLowerMemberIndices)
			{
				RequiredPairs.Add(BearingPairKey(LowerIndex, UpperIndex));
			}
		}
		Plan.Summary.PlannedJointCount = JointKeys.Num();
		if (Plan.Summary.PlannedJointCount > Settings.MaxJointCount)
		{
			OutError = FString::Printf(TEXT("BeamC3V3PreflightJointCap:%d/%d"),
				Plan.Summary.PlannedJointCount, Settings.MaxJointCount);
			return false;
		}
		TSet<uint64> PredictedContacts;
		int64 PairChecks = 0;
		PredictBearingPairs(Settings, Plan, PredictedContacts, PairChecks);
		Plan.Summary.PlannedBearingContactCount = PredictedContacts.Num();
		for (const uint64 RequiredPair : RequiredPairs)
		{
			if (!PredictedContacts.Contains(RequiredPair))
			{
				const int32 LowerIndex = static_cast<int32>(RequiredPair >> 32);
				const int32 UpperIndex = static_cast<int32>(RequiredPair & 0xffffffffULL);
				const FPlannedMember* Lower = Plan.Members.IsValidIndex(LowerIndex)
					? &Plan.Members[LowerIndex] : nullptr;
				const FPlannedMember* Upper = Plan.Members.IsValidIndex(UpperIndex)
					? &Plan.Members[UpperIndex] : nullptr;
				const FBox LowerBounds = Lower != nullptr
					? PlannedMemberBounds(*Lower) : FBox(EForceInit::ForceInit);
				const FBox UpperBounds = Upper != nullptr
					? PlannedMemberBounds(*Upper) : FBox(EForceInit::ForceInit);
				OutError = FString::Printf(
					TEXT("BeamC3V3PreflightSeatGeometryMismatch:Pair=%llu:Lower=%d,K%d,A%d,C%d,B=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f:Upper=%d,K%d,A%d,C%d,B=%.3f,%.3f,%.3f..%.3f,%.3f,%.3f"),
					RequiredPair, LowerIndex,
					Lower != nullptr ? static_cast<int32>(Lower->SkeletonKind) : INDEX_NONE,
					Lower != nullptr ? static_cast<int32>(Lower->Axis) : INDEX_NONE,
					Lower != nullptr ? Lower->CourseIndex : INDEX_NONE,
					LowerBounds.Min.X, LowerBounds.Min.Y, LowerBounds.Min.Z,
					LowerBounds.Max.X, LowerBounds.Max.Y, LowerBounds.Max.Z,
					UpperIndex,
					Upper != nullptr ? static_cast<int32>(Upper->SkeletonKind) : INDEX_NONE,
					Upper != nullptr ? static_cast<int32>(Upper->Axis) : INDEX_NONE,
					Upper != nullptr ? Upper->CourseIndex : INDEX_NONE,
					UpperBounds.Min.X, UpperBounds.Min.Y, UpperBounds.Min.Z,
					UpperBounds.Max.X, UpperBounds.Max.Y, UpperBounds.Max.Z);
				return false;
			}
		}
		if (Plan.Summary.PlannedBearingContactCount > Settings.MaxBearingContactCount)
		{
			OutError = FString::Printf(TEXT("BeamC3V3PreflightBearingCap:%d/%d"),
				Plan.Summary.PlannedBearingContactCount, Settings.MaxBearingContactCount);
			return false;
		}
		Plan.Summary.PlannedBearingPairCheckCount = static_cast<int32>(
			FMath::Min<int64>(PairChecks, MAX_int32));
		if (PairChecks > Settings.MaxBearingPairChecks)
		{
			OutError = FString::Printf(TEXT("BeamC3V3PreflightBearingPairCap:%lld/%d"),
				PairChecks, Settings.MaxBearingPairChecks);
			return false;
		}
		return true;
	}

	void AppendMemberCanonical(FString& Text, const FPlannedMember& Member)
	{
		Text += FString::Printf(
			TEXT("|M:%d:K=%d:%d:%d:%d:O=%d:%d:%d:%d:%d:%d:R=%d:%lld:%lld:%lld:%lld:%lld:%lld:G=%d"),
			static_cast<int32>(Member.OwnerKind), static_cast<int32>(Member.SkeletonKind),
			Member.OwnerId, Member.ComponentId, Member.SourceVolumeId,
			Member.OriginCoreCellId, Member.CourseIndex, Member.StationA, Member.StationB,
			Member.FaceMask, static_cast<int32>(Member.Axis), static_cast<int32>(Member.Role),
			QHash(Member.LocalStart.X), QHash(Member.LocalStart.Y), QHash(Member.LocalStart.Z),
			QHash(Member.LocalEnd.X), QHash(Member.LocalEnd.Y), QHash(Member.LocalEnd.Z),
			Member.bRequiresGroundSeat ? 1 : 0);
		for (const int32 Lower : Member.RequiredLowerMemberIndices)
		{
			Text += FString::Printf(TEXT(":S%d"), Lower);
		}
		for (const int32 Inward : Member.RequiredInwardMemberIndices)
		{
			Text += FString::Printf(TEXT(":I%d"), Inward);
		}
		for (const int32 Core : Member.EndpointCoreCellIds)
		{
			Text += FString::Printf(TEXT(":E%d"), Core);
		}
		if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
		{
			Text += FString::Printf(TEXT(":L%d:SG%d:X%d"),
				Member.SharedLaneIndex, Member.SharedSegmentIndex,
				Member.bSharedCrossCoreSegment ? 1 : 0);
		}
	}

	bool BuildCandidateCommonFrame(
		const TArray<FRoot>& Roots,
		const FDensityRecipe& Density,
		FPlan& Plan,
		FString& OutError)
	{
		if (Plan.Components.IsEmpty() || Plan.CoreCells.IsEmpty())
		{
			OutError = TEXT("BeamC3V3CommonFrameHasNoGroundedCore");
			return false;
		}
		FBuildingGroupPlan& Group = Plan.BuildingGroups.AddDefaulted_GetRef();
		Group.GroupId = Plan.BuildingGroups.Num() - 1;
		Group.GroundPlaneZCM = Plan.Components[0].GroundPlaneZCM;
		Group.BuildingPath = Roots.IsEmpty() ? TEXT("Candidate") : Roots[0].Path;
		if (int32 Slash = INDEX_NONE; Group.BuildingPath.FindChar(TEXT('/'), Slash))
		{
			Group.BuildingPath.LeftInline(Slash, EAllowShrinking::No);
		}
		int32 FrameMinimumX = MAX_int32;
		int32 FrameMaximumX = MIN_int32;
		int32 FrameMinimumY = MAX_int32;
		int32 FrameMaximumY = MIN_int32;
		int32 GroupTopCourse = 0;
		TArray<int32> ForcedCommonBases;
		for (const FComponentPlan& Component : Plan.Components)
		{
			if (!FMath::IsNearlyEqual(Component.GroundPlaneZCM,
				Group.GroundPlaneZCM, WitnessToleranceCM))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3BuildingGroupGroundPlaneMismatch:Group=%d:Component=%d"),
					Group.GroupId, Component.ComponentId);
				return false;
			}
			Group.ComponentIds.Add(Component.ComponentId);
			FrameMinimumX = FMath::Min(FrameMinimumX, QMin(Component.BodyBounds.Min.X));
			FrameMaximumX = FMath::Max(FrameMaximumX, QMax(Component.BodyBounds.Max.X));
			FrameMinimumY = FMath::Min(FrameMinimumY, QMin(Component.BodyBounds.Min.Y));
			FrameMaximumY = FMath::Max(FrameMaximumY, QMax(Component.BodyBounds.Max.Y));
			GroupTopCourse = FMath::Max(GroupTopCourse,
				QRelativeFloor(Component.BodyBounds.Max.Z, Group.GroundPlaneZCM));
			ForcedCommonBases.Append(Component.BandBaseCourseIndices);
		}
		for (const FCoreCellPlan& Core : Plan.CoreCells)
		{
			Group.CoreCellIds.Add(Core.CoreCellId);
		}
		for (const FPlannedMember& Member : Plan.Members)
		{
			if (Member.OwnerKind == EOwnerKind::SupportedSpan)
			{
				Group.SpanVolumeIds.AddUnique(Member.OwnerId);
			}
		}
		// The common outer frame sits one full block beyond the semantic Body
		// union. This is the visible outward projection requested by the design,
		// and keeps its posts distinct from former per-component facade posts.
		--FrameMinimumX;
		++FrameMaximumX;
		--FrameMinimumY;
		++FrameMaximumY;
		const int32 FinalBandBase = FMath::Max(0, (GroupTopCourse - 2) & ~1);
		ForcedCommonBases.RemoveAll([FinalBandBase](const int32 Base)
		{
			return Base < 0 || Base > FinalBandBase;
		});
		if (!MakeBandSchedule(FinalBandBase, Density.VerticalUnits, ForcedCommonBases,
			Group.CommonBandBaseCourseIndices))
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3CommonFrameBandScheduleUnavailable:Top=%d:Vertical=%d"),
				GroupTopCourse, Density.VerticalUnits);
			return false;
		}
		Group.LocalBounds = FBox(
			Position(FrameMinimumX * BlockUnitsCM, FrameMinimumY * BlockUnitsCM,
				Group.GroundPlaneZCM),
			Position(FrameMaximumX * BlockUnitsCM, FrameMaximumY * BlockUnitsCM,
				Group.GroundPlaneZCM + GroupTopCourse * BlockUnitsCM));

		auto SelectPrimaryCore = [&Plan](
			const EABTSM73BeamAFrameAxis Axis, const double CrossCM)
		{
			int32 BestCore = INDEX_NONE;
			double BestDistance = DBL_MAX;
			const bool bX = Axis == EABTSM73BeamAFrameAxis::X;
			for (const FCoreCellPlan& Core : Plan.CoreCells)
			{
				const TArray<int32>& Stations = bX ? Core.YStations : Core.XStations;
				for (const int32 Station : Stations)
				{
					const double Distance = FMath::Abs(
						Station * static_cast<double>(BlockUnitsCM) - CrossCM);
					if (Distance < BestDistance - UE_DOUBLE_SMALL_NUMBER
						|| (FMath::IsNearlyEqual(Distance, BestDistance)
							&& (BestCore == INDEX_NONE || Core.CoreCellId < BestCore)))
					{
						BestDistance = Distance;
						BestCore = Core.CoreCellId;
					}
				}
			}
			return BestCore;
		};

		struct FResolvedHorizontalCross
		{
			double CrossCM = 0.0;
			int32 Authority = MAX_int32;
			int32 MemberIndex = INDEX_NONE;
		};
		auto ResolveHorizontalCross = [&Plan](
			const int32 Course,
			const EABTSM73BeamAFrameAxis Axis,
			const double RequestedCrossCM,
			const double DesiredMinimum,
			const double DesiredMaximum)
		{
			const int32 AxisIndex = static_cast<int32>(Axis);
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			FResolvedHorizontalCross Result;
			Result.CrossCM = RequestedCrossCM;
			double BestDistanceCM = DBL_MAX;
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Existing = Plan.Members[MemberIndex];
				if (Existing.Axis != Axis || Existing.CourseIndex != Course
					// A roof owns its Crown slice; it is not a common-frame row that may
					// be extended outside the semantic roof interval.
					|| Existing.OwnerKind == EOwnerKind::Roof
					|| Existing.OwnerKind == EOwnerKind::ShellFace
					|| Existing.OwnerKind == EOwnerKind::Floor)
				{
					continue;
				}
				const FBox ExistingBounds = PlannedMemberBounds(Existing);
				if (SkeletonV3OverlapLength(ExistingBounds.Min[AxisIndex],
						ExistingBounds.Max[AxisIndex], DesiredMinimum, DesiredMaximum)
					<= GeometryToleranceCM)
				{
					continue;
				}
				const double CandidateCrossCM =
					ExistingBounds.GetCenter()[CrossAxisIndex];
				const double DistanceCM = FMath::Abs(
					CandidateCrossCM - RequestedCrossCM);
				if (DistanceCM >= BlockUnitsCM - GeometryToleranceCM)
				{
					continue;
				}
				const int32 Authority =
					Existing.SkeletonKind == ESkeletonMemberKind::CoreCourse
						|| Existing.SkeletonKind == ESkeletonMemberKind::SharedCourse ? 0
					: Existing.OwnerKind == EOwnerKind::SupportedSpan ? 1
					: Existing.OwnerKind == EOwnerKind::BuildingGroupShell ? 2 : 3;
				if (Authority < Result.Authority
					|| (Authority == Result.Authority
						&& (DistanceCM < BestDistanceCM - GeometryToleranceCM
							|| (FMath::IsNearlyEqual(DistanceCM, BestDistanceCM,
								GeometryToleranceCM)
								&& (CandidateCrossCM
									< Result.CrossCM - GeometryToleranceCM
									|| (FMath::IsNearlyEqual(CandidateCrossCM,
										Result.CrossCM, GeometryToleranceCM)
										&& (Result.MemberIndex == INDEX_NONE
											|| MemberIndex < Result.MemberIndex)))))))
				{
					Result.CrossCM = CandidateCrossCM;
					Result.Authority = Authority;
					Result.MemberIndex = MemberIndex;
					BestDistanceCM = DistanceCM;
				}
			}
			return Result;
		};

		auto AddMissingHorizontalRun = [&Plan, &Group, &OutError, &SelectPrimaryCore,
			&ResolveHorizontalCross](
			const int32 Course,
			const EABTSM73BeamAFrameAxis Axis,
			const double CrossCM,
			const double DesiredMinimum,
			const double DesiredMaximum,
			const uint8 FaceMask,
			double& OutResolvedCrossCM) -> bool
		{
			const int32 AxisIndex = static_cast<int32>(Axis);
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			// Persistent core/shared/roof/span rows claim the physical centreline
			// before nominal fill rows.  A generated group row may then claim a
			// neighbouring nominal request, preventing sub-brick parallel duplicates.
			const FResolvedHorizontalCross Resolution = ResolveHorizontalCross(
				Course, Axis, CrossCM, DesiredMinimum, DesiredMaximum);
			const double ResolvedCrossCM = Resolution.CrossCM;
			OutResolvedCrossCM = ResolvedCrossCM;
			const bool bResolvedToCore = Plan.Members.IsValidIndex(Resolution.MemberIndex)
				&& (Plan.Members[Resolution.MemberIndex].SkeletonKind
						== ESkeletonMemberKind::CoreCourse
					|| Plan.Members[Resolution.MemberIndex].SkeletonKind
						== ESkeletonMemberKind::SharedCourse);
			if (FaceMask == 0 && !bResolvedToCore)
			{
				for (const FPlannedMember& Existing : Plan.Members)
				{
					if (Existing.OwnerKind != EOwnerKind::Roof
						|| Existing.Axis != Axis || Existing.CourseIndex != Course)
					{
						continue;
					}
					const FBox ExistingBounds = PlannedMemberBounds(Existing);
					if (FMath::Abs(ExistingBounds.GetCenter()[CrossAxisIndex]
							- ResolvedCrossCM) < BlockUnitsCM - GeometryToleranceCM
						&& SkeletonV3OverlapLength(ExistingBounds.Min[AxisIndex],
							ExistingBounds.Max[AxisIndex], DesiredMinimum, DesiredMaximum)
							> GeometryToleranceCM)
					{
						// Do not create a parallel half-block row beside the roof.  The
						// roof remains the visible/structural owner of this interior slice.
						return true;
					}
				}
			}
			const double Z = Group.GroundPlaneZCM
				+ (Course + 0.5) * BlockUnitsCM;
			FPlannedMember Probe;
			Probe.Axis = Axis;
			Probe.LocalStart = Axis == EABTSM73BeamAFrameAxis::X
				? Position(DesiredMinimum, ResolvedCrossCM, Z)
				: Position(ResolvedCrossCM, DesiredMinimum, Z);
			Probe.LocalEnd = Axis == EABTSM73BeamAFrameAxis::X
				? Position(DesiredMaximum, ResolvedCrossCM, Z)
				: Position(ResolvedCrossCM, DesiredMaximum, Z);
			const FBox ProbeBounds = PlannedMemberBounds(Probe);
			TArray<FVector2D> AllowedIntervals = {
				FVector2D(DesiredMinimum, DesiredMaximum)};
			auto SubtractBlockedInterval = [&AllowedIntervals](
				const double BlockedMinimum, const double BlockedMaximum)
			{
				TArray<FVector2D> Split;
				for (const FVector2D& Interval : AllowedIntervals)
				{
					if (BlockedMaximum <= Interval.X + GeometryToleranceCM
						|| BlockedMinimum >= Interval.Y - GeometryToleranceCM)
					{
						Split.Add(Interval);
						continue;
					}
					if (BlockedMinimum - Interval.X
						>= BlockUnitsCM - GeometryToleranceCM)
					{
						Split.Emplace(Interval.X,
							FMath::Min(Interval.Y, BlockedMinimum));
					}
					if (Interval.Y - BlockedMaximum
						>= BlockUnitsCM - GeometryToleranceCM)
					{
						Split.Emplace(FMath::Max(Interval.X, BlockedMaximum),
							Interval.Y);
					}
				}
				AllowedIntervals = MoveTemp(Split);
			};
			// A common row follows the global silhouette; it does not cut through a
			// same-course roof belonging to a shorter neighbouring component.
			for (const FPlannedMember& Existing : Plan.Members)
			{
				if (Existing.OwnerKind != EOwnerKind::Roof
					|| Existing.SkeletonKind != ESkeletonMemberKind::RoofCourse
					|| Existing.Axis != Axis || Existing.CourseIndex != Course)
				{
					continue;
				}
				const FBox ExistingBounds = PlannedMemberBounds(Existing);
				if (FMath::Abs(ExistingBounds.GetCenter()[CrossAxisIndex]
						- ResolvedCrossCM)
					>= BlockUnitsCM - GeometryToleranceCM)
				{
					continue;
				}
				SubtractBlockedInterval(
					ExistingBounds.Min[AxisIndex], ExistingBounds.Max[AxisIndex]);
			}
			for (const FABTSM73BeamASupportVoid& Void : Plan.ReservedSupportVoids)
			{
				if (!Void.Bounds.IsValid
					|| SkeletonV3OverlapLength(ProbeBounds.Min.Z, ProbeBounds.Max.Z,
						Void.Bounds.Min.Z, Void.Bounds.Max.Z) <= GeometryToleranceCM
					|| ResolvedCrossCM + BlockUnitsCM * 0.5
						<= Void.Bounds.Min[CrossAxisIndex] + GeometryToleranceCM
					|| ResolvedCrossCM - BlockUnitsCM * 0.5
						>= Void.Bounds.Max[CrossAxisIndex] - GeometryToleranceCM)
				{
					continue;
				}
				SubtractBlockedInterval(
					Void.Bounds.Min[AxisIndex], Void.Bounds.Max[AxisIndex]);
			}
			for (const FVector2D& Allowed : AllowedIntervals)
			{
				TArray<TPair<FVector2D, int32>> Covered;
				for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
				{
					const FPlannedMember& Existing = Plan.Members[MemberIndex];
					if (Existing.Axis != Axis || Existing.CourseIndex != Course)
					{
						continue;
					}
					if (Existing.OwnerKind == EOwnerKind::ShellFace
						|| Existing.OwnerKind == EOwnerKind::Floor)
					{
						continue;
					}
					const FBox ExistingBounds = PlannedMemberBounds(Existing);
					if (FMath::Abs(ExistingBounds.GetCenter()[CrossAxisIndex]
						- ResolvedCrossCM)
						> GeometryToleranceCM)
					{
						continue;
					}
					const double Minimum = FMath::Max(
						Allowed.X, ExistingBounds.Min[AxisIndex]);
					const double Maximum = FMath::Min(
						Allowed.Y, ExistingBounds.Max[AxisIndex]);
					if (Maximum > Minimum + GeometryToleranceCM)
					{
						Covered.Emplace(FVector2D(Minimum, Maximum), MemberIndex);
						Group.MemberIndices.AddUnique(MemberIndex);
					}
				}
				Covered.Sort([](const TPair<FVector2D, int32>& A,
					const TPair<FVector2D, int32>& B)
				{
					return !FMath::IsNearlyEqual(A.Key.X, B.Key.X, GeometryToleranceCM)
						? A.Key.X < B.Key.X : A.Key.Y < B.Key.Y;
				});
				double Cursor = Allowed.X;
				TArray<FVector2D> Gaps;
				for (const TPair<FVector2D, int32>& Coverage : Covered)
				{
					if (Coverage.Key.X > Cursor + GeometryToleranceCM)
					{
						Gaps.Add(FVector2D(Cursor, Coverage.Key.X));
					}
					Cursor = FMath::Max(Cursor, Coverage.Key.Y);
				}
				if (Cursor < Allowed.Y - GeometryToleranceCM)
				{
					Gaps.Add(FVector2D(Cursor, Allowed.Y));
				}
				for (const FVector2D& Gap : Gaps)
				{
					const double GapLength = Gap.Y - Gap.X;
					TArray<FVector2D> CellFaceSegments;
					if (!MakeCellFaceSegments(Gap.X, Gap.Y, CellFaceSegments))
					{
						// A protected opening may leave less than one complete cell between
						// its semantic boundary and a frozen core cap.  Interior infill does
						// not enter that remainder; perimeter/facade continuity remains a
						// hard contract and therefore still fails below.
						if (FaceMask == 0)
						{
							continue;
						}
						FString CoverageDiagnostics;
						for (const TPair<FVector2D, int32>& Coverage : Covered)
						{
							if (!Plan.Members.IsValidIndex(Coverage.Value)
								|| (FMath::Abs(Coverage.Key.Y - Gap.X)
									> GeometryToleranceCM
									&& FMath::Abs(Coverage.Key.X - Gap.Y)
										> GeometryToleranceCM))
							{
								continue;
							}
							const FPlannedMember& Adjacent = Plan.Members[Coverage.Value];
							CoverageDiagnostics += FString::Printf(
								TEXT("|M%d:O%d:K%d:A%d:Q%d:%.3f..%.3f"),
								Coverage.Value, static_cast<int32>(Adjacent.OwnerKind),
								static_cast<int32>(Adjacent.SkeletonKind),
								static_cast<int32>(Adjacent.Axis), Adjacent.CourseIndex,
								Coverage.Key.X, Coverage.Key.Y);
						}
						OutError = FString::Printf(
							TEXT("BeamC3V3CommonFrameNoCompleteCellGap:Group=%d:Course=%d:Axis=%d:Cross=%.3f:Gap=%.3f..%.3f:Length=%.3f:Adjacent=%s"),
							Group.GroupId, Course, AxisIndex, ResolvedCrossCM,
							Gap.X, Gap.Y, GapLength, *CoverageDiagnostics);
						return false;
					}
					for (int32 SegmentIndex = 0;
						SegmentIndex < CellFaceSegments.Num(); ++SegmentIndex)
					{
						const double StartAlong = CellFaceSegments[SegmentIndex].X;
						const double EndAlong = CellFaceSegments[SegmentIndex].Y;
						const FVector Start = Axis == EABTSM73BeamAFrameAxis::X
							? Position(StartAlong, ResolvedCrossCM, Z)
							: Position(ResolvedCrossCM, StartAlong, Z);
						const FVector End = Axis == EABTSM73BeamAFrameAxis::X
							? Position(EndAlong, ResolvedCrossCM, Z)
							: Position(ResolvedCrossCM, EndAlong, Z);
						const int32 MemberIndex = Plan.Members.Num();
						const int32 PrimaryCore = SelectPrimaryCore(Axis, ResolvedCrossCM);
						if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
							FaceMask != 0 ? ESkeletonMemberKind::FacadeCourse
								: ESkeletonMemberKind::ThroughCourse,
							Group.GroupId, INDEX_NONE, INDEX_NONE, PrimaryCore,
							Course, FMath::RoundToInt(ResolvedCrossCM / BlockUnitsCM),
							SegmentIndex, FaceMask, Axis,
							Axis == EABTSM73BeamAFrameAxis::X
								? EABTSM73BeamAMemberRole::PrimaryBeam
								: EABTSM73BeamAMemberRole::SecondaryBeam,
							Start, End, OutError))
						{
							return false;
						}
						Group.MemberIndices.Add(MemberIndex);
					}
				}
			}
			return true;
		};

		TArray<int32> XRows = {FrameMinimumY, FrameMaximumY};
		TArray<int32> YRows = {FrameMinimumX, FrameMaximumX};
		for (const FCoreCellPlan& Core : Plan.CoreCells)
		{
			XRows.Append(Core.YStations);
			YRows.Append(Core.XStations);
		}
		XRows.Sort();
		YRows.Sort();
		for (int32 Index = XRows.Num() - 1; Index > 0; --Index)
		{
			if (XRows[Index] == XRows[Index - 1]) XRows.RemoveAt(Index);
		}
		for (int32 Index = YRows.Num() - 1; Index > 0; --Index)
		{
			if (YRows[Index] == YRows[Index - 1]) YRows.RemoveAt(Index);
		}
		auto DensifyStations = [](TArray<int32>& Stations)
		{
			TArray<int32> Dense;
			for (int32 AnchorIndex = 0; AnchorIndex < Stations.Num(); ++AnchorIndex)
			{
				if (AnchorIndex == 0)
				{
					Dense.Add(Stations[AnchorIndex]);
					continue;
				}
				const int32 Start = Stations[AnchorIndex - 1];
				const int32 End = Stations[AnchorIndex];
				const int32 SegmentCount = FMath::DivideAndRoundUp(
					End - Start, MaximumHorizontalUnits / 2);
				for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
				{
					Dense.Add(FMath::RoundToInt(FMath::Lerp(
						static_cast<double>(Start), static_cast<double>(End),
						static_cast<double>(Segment) / SegmentCount)));
				}
			}
			Dense.Sort();
			for (int32 Index = Dense.Num() - 1; Index > 0; --Index)
			{
				if (Dense[Index] == Dense[Index - 1]) Dense.RemoveAt(Index);
			}
			Stations = MoveTemp(Dense);
		};
		DensifyStations(XRows);
		DensifyStations(YRows);
		const double XRunMinimum = FrameMinimumX * BlockUnitsCM - BlockUnitsCM * 0.5;
		const double XRunMaximum = FrameMaximumX * BlockUnitsCM + BlockUnitsCM * 0.5;
		const double YRunMinimum = FrameMinimumY * BlockUnitsCM - BlockUnitsCM * 0.5;
		const double YRunMaximum = FrameMaximumY * BlockUnitsCM + BlockUnitsCM * 0.5;
		struct FResolvedBandRows
		{
			int32 BaseCourse = INDEX_NONE;
			/** Physical Y centrelines of the X course, paired with perimeter face bits. */
			TArray<TPair<double, uint8>> XCourseRows;
			/** Physical X centrelines of the Y course, paired with perimeter face bits. */
			TArray<TPair<double, uint8>> YCourseRows;
		};
		auto AddResolvedRow = [](TArray<TPair<double, uint8>>& Rows,
			const double CrossCM, const uint8 FaceMask)
		{
			for (TPair<double, uint8>& Row : Rows)
			{
				if (FMath::Abs(Row.Key - CrossCM) <= GeometryToleranceCM)
				{
					Row.Value |= FaceMask;
					return;
				}
			}
			Rows.Emplace(CrossCM, FaceMask);
		};
		TArray<FResolvedBandRows> ResolvedBands;
		for (const int32 Base : Group.CommonBandBaseCourseIndices)
		{
			FResolvedBandRows& ResolvedBand = ResolvedBands.AddDefaulted_GetRef();
			ResolvedBand.BaseCourse = Base;
			for (const int32 YStation : XRows)
			{
				const uint8 FaceMask = YStation == FrameMinimumY
					? ABTSM73BeamC3V3::NegativeY
					: YStation == FrameMaximumY
						? ABTSM73BeamC3V3::PositiveY : 0;
				double ResolvedCrossCM = YStation * static_cast<double>(BlockUnitsCM);
				if (!AddMissingHorizontalRun(Base, EABTSM73BeamAFrameAxis::X,
					YStation * static_cast<double>(BlockUnitsCM),
					XRunMinimum, XRunMaximum, FaceMask, ResolvedCrossCM))
				{
					return false;
				}
				AddResolvedRow(ResolvedBand.XCourseRows, ResolvedCrossCM, FaceMask);
			}
			for (const int32 XStation : YRows)
			{
				const uint8 FaceMask = XStation == FrameMinimumX
					? ABTSM73BeamC3V3::NegativeX
					: XStation == FrameMaximumX
						? ABTSM73BeamC3V3::PositiveX : 0;
				double ResolvedCrossCM = XStation * static_cast<double>(BlockUnitsCM);
				if (!AddMissingHorizontalRun(Base + 1, EABTSM73BeamAFrameAxis::Y,
					XStation * static_cast<double>(BlockUnitsCM),
					YRunMinimum, YRunMaximum, FaceMask, ResolvedCrossCM))
				{
					return false;
				}
				AddResolvedRow(ResolvedBand.YCourseRows, ResolvedCrossCM, FaceMask);
			}
			ResolvedBand.XCourseRows.Sort([](const TPair<double, uint8>& A,
				const TPair<double, uint8>& B) { return A.Key < B.Key; });
			ResolvedBand.YCourseRows.Sort([](const TPair<double, uint8>& A,
				const TPair<double, uint8>& B) { return A.Key < B.Key; });
		}

		for (int32 BandIndex = 1;
			BandIndex < Group.CommonBandBaseCourseIndices.Num(); ++BandIndex)
		{
			const int32 PreviousBase = Group.CommonBandBaseCourseIndices[BandIndex - 1];
			const int32 CurrentBase = Group.CommonBandBaseCourseIndices[BandIndex];
			const double BottomZ = Group.GroundPlaneZCM
				+ (PreviousBase + 2) * BlockUnitsCM;
			const double TopZ = Group.GroundPlaneZCM + CurrentBase * BlockUnitsCM;
			if (TopZ <= BottomZ + GeometryToleranceCM)
			{
				continue;
			}
			TArray<int32> LowerPostSeatCandidates;
			TArray<int32> UpperPostSeatCandidates;
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				if (Member.Axis == EABTSM73BeamAFrameAxis::Z
					|| Member.OwnerKind == EOwnerKind::ShellFace
					|| Member.OwnerKind == EOwnerKind::Floor)
				{
					continue;
				}
				const FBox Bounds = PlannedMemberBounds(Member);
				if (FMath::Abs(Bounds.Max.Z - BottomZ) <= GeometryToleranceCM)
				{
					LowerPostSeatCandidates.Add(MemberIndex);
				}
				if (FMath::Abs(Bounds.Min.Z - TopZ) <= GeometryToleranceCM)
				{
					UpperPostSeatCandidates.Add(MemberIndex);
				}
			}
			TSet<int32> UpperGroupMembersWithPost;
			// A post is the exact cross-bearing between the previous band's upper
			// Y course and the current band's lower X course.  Use their resolved
			// physical centrelines, not the nominal lattice, so a reused roof row
			// cannot leave a half-contact post behind.
			for (const TPair<double, uint8>& XRow :
				ResolvedBands[BandIndex - 1].YCourseRows)
			{
				for (const TPair<double, uint8>& YRow :
					ResolvedBands[BandIndex].XCourseRows)
				{
					const double StationXCM = XRow.Key;
					const double StationYCM = YRow.Key;
					const uint8 FaceMask = XRow.Value | YRow.Value;
					FPlannedMember PostProbe;
					PostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
					PostProbe.LocalStart = Position(StationXCM, StationYCM, BottomZ);
					PostProbe.LocalEnd = Position(StationXCM, StationYCM, TopZ);
					const FBox PostBounds = PlannedMemberBounds(PostProbe);
					auto HasFullPlanarContact = [&PostBounds](const FBox& Bounds)
					{
						return SkeletonV3OverlapLength(PostBounds.Min.X,
							PostBounds.Max.X, Bounds.Min.X, Bounds.Max.X)
							>= BlockUnitsCM - GeometryToleranceCM
							&& SkeletonV3OverlapLength(PostBounds.Min.Y,
								PostBounds.Max.Y, Bounds.Min.Y, Bounds.Max.Y)
								>= BlockUnitsCM - GeometryToleranceCM;
					};
					bool bHasLowerSeat = false;
					for (const int32 CandidateIndex : LowerPostSeatCandidates)
					{
						if (HasFullPlanarContact(PlannedMemberBounds(
							Plan.Members[CandidateIndex])))
						{
							bHasLowerSeat = true;
							break;
						}
					}
					TArray<int32> UpperGroupMembersAtSeat;
					bool bHasUpperSeat = false;
					for (const int32 CandidateIndex : UpperPostSeatCandidates)
					{
						if (!HasFullPlanarContact(PlannedMemberBounds(
							Plan.Members[CandidateIndex])))
						{
							continue;
						}
						bHasUpperSeat = true;
						const FPlannedMember& UpperMember = Plan.Members[CandidateIndex];
						if (UpperMember.OwnerKind == EOwnerKind::BuildingGroupShell
							&& UpperMember.Axis == EABTSM73BeamAFrameAxis::X
							&& UpperMember.CourseIndex == CurrentBase)
						{
							UpperGroupMembersAtSeat.Add(CandidateIndex);
						}
					}
					bool bSeatsNewUpperGroupMember = false;
					for (const int32 UpperMemberIndex : UpperGroupMembersAtSeat)
					{
						bSeatsNewUpperGroupMember |=
							!UpperGroupMembersWithPost.Contains(UpperMemberIndex);
					}
					// The four facade lines always retain posts.  Interior intersections
					// are emitted only when they give an as-yet-unseated common-frame
					// segment a real lower/upper 36 x 36 bearing.  This preserves the
					// DAG without recreating the former all-by-all interior column forest.
					if (!bHasLowerSeat || !bHasUpperSeat
						|| (FaceMask == 0 && !bSeatsNewUpperGroupMember))
					{
						continue;
					}
					bool bProtected = false;
					for (const FABTSM73BeamASupportVoid& Void : Plan.ReservedSupportVoids)
					{
						bProtected |= BoxesPenetrate(PostBounds, Void.Bounds);
					}
					for (const FPlannedMember& Existing : Plan.Members)
					{
						if (Existing.Axis != EABTSM73BeamAFrameAxis::Z
							&& BoxesPenetrate(PostBounds, PlannedMemberBounds(Existing)))
						{
							bProtected = true;
							break;
						}
					}
					if (bProtected)
					{
						continue;
					}
					int32 ExistingPost = INDEX_NONE;
					for (int32 CandidateIndex = 0;
						CandidateIndex < Plan.Members.Num(); ++CandidateIndex)
					{
						const FPlannedMember& Candidate = Plan.Members[CandidateIndex];
						if (Candidate.Axis != EABTSM73BeamAFrameAxis::Z)
						{
							continue;
						}
						const FBox CandidateBounds = PlannedMemberBounds(Candidate);
						if (FMath::Abs(CandidateBounds.GetCenter().X
								- PostBounds.GetCenter().X) <= GeometryToleranceCM
							&& FMath::Abs(CandidateBounds.GetCenter().Y
								- PostBounds.GetCenter().Y) <= GeometryToleranceCM
							&& CandidateBounds.Min.Z
								<= PostBounds.Min.Z + GeometryToleranceCM
							&& CandidateBounds.Max.Z
								>= PostBounds.Max.Z - GeometryToleranceCM)
						{
							ExistingPost = CandidateIndex;
							break;
						}
					}
					if (ExistingPost != INDEX_NONE)
					{
						FPlannedMember& AdoptedPost = Plan.Members[ExistingPost];
						AdoptedPost.OwnerKind = EOwnerKind::BuildingGroupShell;
						AdoptedPost.SkeletonKind = ESkeletonMemberKind::ExteriorPost;
						AdoptedPost.OwnerId = Group.GroupId;
						AdoptedPost.ComponentId = INDEX_NONE;
						AdoptedPost.SourceVolumeId = INDEX_NONE;
						AdoptedPost.CourseIndex = CurrentBase;
						AdoptedPost.StationA = FMath::RoundToInt(
							StationXCM / BlockUnitsCM);
						AdoptedPost.StationB = FMath::RoundToInt(
							StationYCM / BlockUnitsCM);
						AdoptedPost.FaceMask |= FaceMask;
						AdoptedPost.Role = EABTSM73BeamAMemberRole::Post;
						Group.MemberIndices.AddUnique(ExistingPost);
						for (const int32 UpperMemberIndex : UpperGroupMembersAtSeat)
						{
							UpperGroupMembersWithPost.Add(UpperMemberIndex);
						}
						continue;
					}
					const int32 MemberIndex = Plan.Members.Num();
					if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
						ESkeletonMemberKind::ExteriorPost, Group.GroupId, INDEX_NONE,
						INDEX_NONE, SelectPrimaryCore(EABTSM73BeamAFrameAxis::X,
							StationYCM), CurrentBase,
						FMath::RoundToInt(StationXCM / BlockUnitsCM),
						FMath::RoundToInt(StationYCM / BlockUnitsCM), FaceMask,
						EABTSM73BeamAFrameAxis::Z, EABTSM73BeamAMemberRole::Post,
						Position(StationXCM, StationYCM, BottomZ),
						Position(StationXCM, StationYCM, TopZ), OutError))
					{
						return false;
					}
					Group.MemberIndices.Add(MemberIndex);
					for (const int32 UpperMemberIndex : UpperGroupMembersAtSeat)
					{
						UpperGroupMembersWithPost.Add(UpperMemberIndex);
					}
				}
			}
		}
		Group.GroundedFaceMask = ABTSM73BeamC3V3::AllFaces;
		Plan.Summary.BuildingGroupCount = Plan.BuildingGroups.Num();
		Plan.Summary.CommonShellMemberCount = Group.MemberIndices.Num();
		Plan.Summary.CommonShellConnectedCoreCount = Group.CoreCellIds.Num();
		return true;
	}

	bool CanonicalizeCommonHorizontalRows(FPlan& Plan, FString& OutError)
	{
		TSet<int32> RemovedMembers;
		TMap<int32, int32> NoReplacements;
		for (FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			TMap<FString, TArray<int32>> RowBuckets;
			for (const int32 MemberIndex : Group.MemberIndices)
			{
				if (!Plan.Members.IsValidIndex(MemberIndex))
				{
					continue;
				}
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bEligibleOwner =
					Member.OwnerKind == EOwnerKind::BuildingGroupShell
					|| Member.OwnerKind == EOwnerKind::ShellFace
					|| Member.OwnerKind == EOwnerKind::Floor;
				const bool bEligibleKind =
					Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FloorCourse;
				if (!bEligibleOwner || !bEligibleKind
					|| (Member.Axis != EABTSM73BeamAFrameAxis::X
						&& Member.Axis != EABTSM73BeamAFrameAxis::Y))
				{
					continue;
				}
				const FBox Bounds = PlannedMemberBounds(Member);
				const int32 AxisIndex = static_cast<int32>(Member.Axis);
				const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
				const FString Key = FString::Printf(TEXT("%d:%d:%lld:%lld"),
					Member.CourseIndex, AxisIndex,
					QHash(Bounds.GetCenter()[CrossAxisIndex]),
					QHash(Bounds.GetCenter().Z));
				RowBuckets.FindOrAdd(Key).Add(MemberIndex);
			}

			for (TPair<FString, TArray<int32>>& Bucket : RowBuckets)
			{
				TArray<int32>& Members = Bucket.Value;
				Members.Sort([&Plan](const int32 A, const int32 B)
				{
					const FPlannedMember& AMember = Plan.Members[A];
					const FPlannedMember& BMember = Plan.Members[B];
					const int32 AxisIndex = static_cast<int32>(AMember.Axis);
					const FBox ABounds = PlannedMemberBounds(AMember);
					const FBox BBounds = PlannedMemberBounds(BMember);
					return !FMath::IsNearlyEqual(ABounds.Min[AxisIndex],
						BBounds.Min[AxisIndex], GeometryToleranceCM)
						? ABounds.Min[AxisIndex] < BBounds.Min[AxisIndex]
						: A < B;
				});
				for (int32 ClusterStart = 0; ClusterStart < Members.Num();)
				{
					const FPlannedMember& First = Plan.Members[Members[ClusterStart]];
					const EABTSM73BeamAFrameAxis RowAxis = First.Axis;
					const int32 RowCourse = First.CourseIndex;
					const int32 AxisIndex = static_cast<int32>(RowAxis);
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					const FBox FirstBounds = PlannedMemberBounds(First);
					double Minimum = FirstBounds.Min[AxisIndex];
					double Maximum = FirstBounds.Max[AxisIndex];
					const double CrossCM = FirstBounds.GetCenter()[CrossAxisIndex];
					const double Z = FirstBounds.GetCenter().Z;
					uint8 CommonFaceMask = First.OwnerKind
						== EOwnerKind::BuildingGroupShell ? First.FaceMask : 0;
					int32 PrimaryCore = Plan.CoreCells.IsValidIndex(
						First.OriginCoreCellId) ? First.OriginCoreCellId : INDEX_NONE;
					int32 ClusterEnd = ClusterStart + 1;
					for (; ClusterEnd < Members.Num(); ++ClusterEnd)
					{
						const FPlannedMember& Candidate = Plan.Members[Members[ClusterEnd]];
						const FBox CandidateBounds = PlannedMemberBounds(Candidate);
						if (CandidateBounds.Min[AxisIndex]
							> Maximum + GeometryToleranceCM)
						{
							break;
						}
						Maximum = FMath::Max(Maximum, CandidateBounds.Max[AxisIndex]);
						if (Candidate.OwnerKind == EOwnerKind::BuildingGroupShell)
						{
							CommonFaceMask |= Candidate.FaceMask;
						}
						if (Plan.CoreCells.IsValidIndex(Candidate.OriginCoreCellId))
						{
							PrimaryCore = PrimaryCore == INDEX_NONE
								? Candidate.OriginCoreCellId
								: FMath::Min(PrimaryCore, Candidate.OriginCoreCellId);
						}
					}
					if (PrimaryCore == INDEX_NONE && !Group.CoreCellIds.IsEmpty())
					{
						PrimaryCore = Group.CoreCellIds[0];
					}
					const double LengthCM = Maximum - Minimum;
					TArray<FVector2D> CellFaceSegments;
					if (!MakeCellFaceSegments(Minimum, Maximum, CellFaceSegments))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3CommonRowCanonicalizationNoCompleteCell:Group=%d:Course=%d:Axis=%d:Range=%.3f..%.3f:Length=%.3f"),
							Group.GroupId, RowCourse, AxisIndex,
							Minimum, Maximum, LengthCM);
						return false;
					}
					for (int32 SegmentIndex = 0;
						SegmentIndex < CellFaceSegments.Num(); ++SegmentIndex)
					{
						const double StartAlong = CellFaceSegments[SegmentIndex].X;
						const double EndAlong = CellFaceSegments[SegmentIndex].Y;
						const FVector Start = RowAxis == EABTSM73BeamAFrameAxis::X
							? Position(StartAlong, CrossCM, Z)
							: Position(CrossCM, StartAlong, Z);
						const FVector End = RowAxis == EABTSM73BeamAFrameAxis::X
							? Position(EndAlong, CrossCM, Z)
							: Position(CrossCM, EndAlong, Z);
						const int32 NewMemberIndex = Plan.Members.Num();
						if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
							CommonFaceMask != 0 ? ESkeletonMemberKind::FacadeCourse
								: ESkeletonMemberKind::ThroughCourse,
							Group.GroupId, INDEX_NONE, INDEX_NONE, PrimaryCore,
							RowCourse, FMath::RoundToInt(CrossCM / BlockUnitsCM),
							SegmentIndex, CommonFaceMask, RowAxis,
							RowAxis == EABTSM73BeamAFrameAxis::X
								? EABTSM73BeamAMemberRole::PrimaryBeam
								: EABTSM73BeamAMemberRole::SecondaryBeam,
							Start, End, OutError))
						{
							return false;
						}
						Group.MemberIndices.Add(NewMemberIndex);
					}
					for (int32 Index = ClusterStart; Index < ClusterEnd; ++Index)
					{
						RemovedMembers.Add(Members[Index]);
					}
					ClusterStart = ClusterEnd;
				}
			}
		}

		// Former per-component facade/floor pieces are not six secondary buildings.
		// Any course used by the group was replaced above; every unadopted local
		// shell grid member is obsolete once the common frame exists.  The roof
		// already bears directly on the continuous core and therefore needs no
		// hidden local-frame fallback.
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			if (Plan.Members[MemberIndex].OwnerKind == EOwnerKind::ShellFace
				|| Plan.Members[MemberIndex].OwnerKind == EOwnerKind::Floor)
			{
				RemovedMembers.Add(MemberIndex);
			}
		}
		return CompactPlanMembers(Plan, RemovedMembers, NoReplacements, OutError);
	}

	bool CollapseUnsupportedCommonSpansIntoCores(FPlan& Plan, FString& OutError)
	{
		TSet<int32> RemovedMembers;
		TMap<int32, int32> NoReplacements;
		auto HasGeometricLowerSeat = [&Plan, &RemovedMembers](const int32 UpperIndex)
		{
			if (!Plan.Members.IsValidIndex(UpperIndex))
			{
				return false;
			}
			const FPlannedMember& Upper = Plan.Members[UpperIndex];
			const FBox UpperBounds = PlannedMemberBounds(Upper);
			double GroundZ = 0.0;
			if (Plan.Components.IsValidIndex(Upper.ComponentId))
			{
				GroundZ = Plan.Components[Upper.ComponentId].GroundPlaneZCM;
			}
			else if (Upper.OwnerKind == EOwnerKind::BuildingGroupShell
				&& Plan.BuildingGroups.IsValidIndex(Upper.OwnerId))
			{
				GroundZ = Plan.BuildingGroups[Upper.OwnerId].GroundPlaneZCM;
			}
			if (FMath::Abs(UpperBounds.Min.Z - GroundZ) <= GeometryToleranceCM)
			{
				return true;
			}
			for (int32 LowerIndex = 0; LowerIndex < Plan.Members.Num(); ++LowerIndex)
			{
				if (LowerIndex == UpperIndex || RemovedMembers.Contains(LowerIndex))
				{
					continue;
				}
				const FBox LowerBounds = PlannedMemberBounds(Plan.Members[LowerIndex]);
				if (FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
						<= GeometryToleranceCM
					&& SkeletonV3OverlapLength(LowerBounds.Min.X, LowerBounds.Max.X,
						UpperBounds.Min.X, UpperBounds.Max.X) > GeometryToleranceCM
					&& SkeletonV3OverlapLength(LowerBounds.Min.Y, LowerBounds.Max.Y,
						UpperBounds.Min.Y, UpperBounds.Max.Y) > GeometryToleranceCM)
				{
					return true;
				}
			}
			return false;
		};
		// Freeze removals before classifying horizontal support. Otherwise a
		// through-core legacy post can make a span look supported during this
		// pass and disappear immediately afterwards.
		for (int32 PostIndex = 0; PostIndex < Plan.Members.Num(); ++PostIndex)
		{
			const FPlannedMember& Post = Plan.Members[PostIndex];
			if (Post.OwnerKind == EOwnerKind::BuildingGroupShell
				|| Post.Axis != EABTSM73BeamAFrameAxis::Z
				|| Post.SkeletonKind != ESkeletonMemberKind::ExteriorPost)
			{
				continue;
			}
			const FBox PostBounds = PlannedMemberBounds(Post);
			for (const FPlannedMember& Horizontal : Plan.Members)
			{
				if (Horizontal.Axis != EABTSM73BeamAFrameAxis::Z
					&& BoxesPenetrate(PostBounds, PlannedMemberBounds(Horizontal)))
				{
					RemovedMembers.Add(PostIndex);
					break;
				}
			}
		}
		for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
		{
			FPlannedMember& Member = Plan.Members[MemberIndex];
			const bool bCollapsibleHorizontal =
				Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::FloorCourse;
			if (!bCollapsibleHorizontal
				|| Member.Axis == EABTSM73BeamAFrameAxis::Z)
			{
				continue;
			}
			if (HasGeometricLowerSeat(MemberIndex))
			{
				continue;
			}
			// CoreCourse and SharedCourse are immutable skeleton members.  This pass
			// may discard optional unseated infill, but it never stretches a seated
			// neighbour across the gap; common spokes remain independent members.
			if (Member.OwnerKind == EOwnerKind::BuildingGroupShell
				&& Member.FaceMask == 0)
			{
				RemovedMembers.Add(MemberIndex);
			}
		}
		for (int32 PostIndex = 0; PostIndex < Plan.Members.Num(); ++PostIndex)
		{
			const FPlannedMember& Post = Plan.Members[PostIndex];
			if (Post.OwnerKind == EOwnerKind::BuildingGroupShell
				|| Post.Axis != EABTSM73BeamAFrameAxis::Z
				|| Post.SkeletonKind != ESkeletonMemberKind::ExteriorPost)
			{
				continue;
			}
			const FBox PostBounds = PlannedMemberBounds(Post);
			for (const FPlannedMember& Horizontal : Plan.Members)
			{
				if (Horizontal.Axis != EABTSM73BeamAFrameAxis::Z
					&& BoxesPenetrate(PostBounds, PlannedMemberBounds(Horizontal)))
				{
					RemovedMembers.Add(PostIndex);
					break;
				}
			}
		}
		return CompactPlanMembers(Plan, RemovedMembers, NoReplacements, OutError);
	}

	bool CompleteCommonFrameSupportHulls(
		const FABTSM73BeamCPreviewSettings& BeamCSettings,
		FPlan& Plan,
		FString& OutError)
	{
		struct FPostCandidate
		{
			double StationXCM = 0.0;
			double StationYCM = 0.0;
			double BottomZCM = 0.0;
			double TopZCM = 0.0;
			uint8 FaceMask = 0;
			FVector2D SupportInterval = FVector2D::ZeroVector;
			bool bRequiresLowerTransferRail = false;
			bool bRequiresPost = true;
			int32 LowerTransferCourse = INDEX_NONE;
			FVector LowerTransferStart = FVector::ZeroVector;
			FVector LowerTransferEnd = FVector::ZeroVector;
			int32 ExistingLowerTransferMemberIndex = INDEX_NONE;
			FVector ExistingLowerTransferStart = FVector::ZeroVector;
			FVector ExistingLowerTransferEnd = FVector::ZeroVector;
			int32 ExtendedLowerCarrierMemberIndex = INDEX_NONE;
			FVector ExtendedLowerCarrierStart = FVector::ZeroVector;
			FVector ExtendedLowerCarrierEnd = FVector::ZeroVector;
		};
		struct FYTransferNeed
		{
			int32 UpperIndex = INDEX_NONE;
			int32 GroupId = INDEX_NONE;
			int32 BandIndex = INDEX_NONE;
			int32 BaseCourse = INDEX_NONE;
			double StationXCM = 0.0;
			double StationYCM = 0.0;
			uint8 FaceMask = 0;
		};

		const double StationMergeToleranceCM = FMath::Max(0.01,
			static_cast<double>(BeamCSettings.BeamB.BeamA.JointMergeToleranceCM));
		auto HasFullPlanarContact = [](const FBox& A, const FBox& B)
		{
			return SkeletonV3OverlapLength(A.Min.X, A.Max.X, B.Min.X, B.Max.X)
				>= BlockUnitsCM - GeometryToleranceCM
				&& SkeletonV3OverlapLength(A.Min.Y, A.Max.Y, B.Min.Y, B.Max.Y)
					>= BlockUnitsCM - GeometryToleranceCM;
		};
		auto CollectSupportIntervals = [&Plan](
			const int32 UpperIndex, const TSet<int32>* RemovedMembers,
			TArray<FVector2D>& OutIntervals)
		{
			OutIntervals.Reset();
			if (!Plan.Members.IsValidIndex(UpperIndex))
			{
				return;
			}
			const FPlannedMember& Upper = Plan.Members[UpperIndex];
			const FBox UpperBounds = PlannedMemberBounds(Upper);
			const int32 AxisIndex = static_cast<int32>(Upper.Axis);
			for (int32 LowerIndex = 0; LowerIndex < Plan.Members.Num(); ++LowerIndex)
			{
				if (LowerIndex == UpperIndex
					|| (RemovedMembers != nullptr && RemovedMembers->Contains(LowerIndex)))
				{
					continue;
				}
				const FBox LowerBounds = PlannedMemberBounds(Plan.Members[LowerIndex]);
				// Existing canonical rows may meet under one upper rail as adjacent
				// half patches.  Preserve their real contact intervals here so their
				// union is judged like Beam-C; every newly emitted transfer/post seat
				// below is still required to provide one full 36 x 36 contact.
				if (FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
						> GeometryToleranceCM
					|| SkeletonV3OverlapLength(LowerBounds.Min.X, LowerBounds.Max.X,
						UpperBounds.Min.X, UpperBounds.Max.X) <= GeometryToleranceCM
					|| SkeletonV3OverlapLength(LowerBounds.Min.Y, LowerBounds.Max.Y,
						UpperBounds.Min.Y, UpperBounds.Max.Y) <= GeometryToleranceCM)
				{
					continue;
				}
				OutIntervals.Emplace(
					FMath::Max(LowerBounds.Min[AxisIndex], UpperBounds.Min[AxisIndex]),
					FMath::Min(LowerBounds.Max[AxisIndex], UpperBounds.Max[AxisIndex]));
			}
		};
		auto CollectRequiredLoadRange = [&Plan](
			const int32 UpperIndex, const TSet<int32>* RemovedMembers,
			double& OutMinimum, double& OutMaximum)
		{
			if (!Plan.Members.IsValidIndex(UpperIndex))
			{
				OutMinimum = 0.0;
				OutMaximum = 0.0;
				return;
			}
			const FPlannedMember& Upper = Plan.Members[UpperIndex];
			const FBox UpperBounds = PlannedMemberBounds(Upper);
			const int32 AxisIndex = static_cast<int32>(Upper.Axis);
			OutMinimum = UpperBounds.GetCenter()[AxisIndex];
			OutMaximum = OutMinimum;
			for (int32 LoadIndex = 0; LoadIndex < Plan.Members.Num(); ++LoadIndex)
			{
				if (LoadIndex == UpperIndex
					|| (RemovedMembers != nullptr && RemovedMembers->Contains(LoadIndex)))
				{
					continue;
				}
				const FBox LoadBounds = PlannedMemberBounds(Plan.Members[LoadIndex]);
				if (FMath::Abs(LoadBounds.Min.Z - UpperBounds.Max.Z)
						> GeometryToleranceCM
					|| SkeletonV3OverlapLength(LoadBounds.Min.X, LoadBounds.Max.X,
						UpperBounds.Min.X, UpperBounds.Max.X) <= GeometryToleranceCM
					|| SkeletonV3OverlapLength(LoadBounds.Min.Y, LoadBounds.Max.Y,
						UpperBounds.Min.Y, UpperBounds.Max.Y) <= GeometryToleranceCM)
				{
					continue;
				}
				const double ContactMinimum = FMath::Max(
					LoadBounds.Min[AxisIndex], UpperBounds.Min[AxisIndex]);
				const double ContactMaximum = FMath::Min(
					LoadBounds.Max[AxisIndex], UpperBounds.Max[AxisIndex]);
				const double LoadWitness = (ContactMinimum + ContactMaximum) * 0.5;
				OutMinimum = FMath::Min(OutMinimum, LoadWitness);
				OutMaximum = FMath::Max(OutMaximum, LoadWitness);
			}
		};
		auto SupportHullSatisfies = [&BeamCSettings, StationMergeToleranceCM](
			const FPlannedMember& Upper, TArray<FVector2D> Intervals,
			const bool bAllowSingleHorizontalBearing,
			const double RequiredMinimum, const double RequiredMaximum)
		{
			if (Intervals.IsEmpty())
			{
				return false;
			}
			Intervals.Sort([](const FVector2D& A, const FVector2D& B)
			{
				return A.X < B.X
					|| (FMath::IsNearlyEqual(A.X, B.X, UE_DOUBLE_SMALL_NUMBER)
						&& A.Y < B.Y);
			});
			TArray<FVector2D> Merged;
			for (const FVector2D& Interval : Intervals)
			{
				if (Merged.IsEmpty()
					|| Interval.X > Merged.Last().Y + StationMergeToleranceCM)
				{
					Merged.Add(Interval);
				}
				else
				{
					Merged.Last().Y = FMath::Max(Merged.Last().Y, Interval.Y);
				}
			}
			const FBox UpperBounds = PlannedMemberBounds(Upper);
			const int32 AxisIndex = static_cast<int32>(Upper.Axis);
			const double UpperLengthCM = UpperBounds.Max[AxisIndex]
				- UpperBounds.Min[AxisIndex];
			const double RequiredCentre = UpperBounds.GetCenter()[AxisIndex];
			const double ConstrainedMinimum = FMath::Min(
				RequiredCentre, RequiredMinimum);
			const double ConstrainedMaximum = FMath::Max(
				RequiredCentre, RequiredMaximum);
			const double SupportMinimum = Merged[0].X;
			const double SupportMaximum = Merged.Last().Y;
			if (ConstrainedMinimum < SupportMinimum
					+ BeamCSettings.SupportResultantMarginCM
				|| ConstrainedMaximum > SupportMaximum
					- BeamCSettings.SupportResultantMarginCM)
			{
				return false;
			}
			const double LongMemberThreshold =
				BeamCSettings.BeamB.BeamA.BlockCrossSectionCM
					* BeamCSettings.MaximumSingleSupportMemberLengthRatio;
			if (UpperLengthCM <= LongMemberThreshold)
			{
				return true;
			}
			double TotalSupportLength = 0.0;
			for (const FVector2D& Interval : Merged)
			{
				TotalSupportLength += FMath::Max(0.0, Interval.Y - Interval.X);
			}
			const double CoverageRatio = TotalSupportLength
				/ FMath::Max(UpperLengthCM, 1.0);
			const double SpanRatio = (SupportMaximum - SupportMinimum)
				/ FMath::Max(UpperLengthCM, 1.0);
			return (Merged.Num() == 1
					&& (CoverageRatio >= BeamCSettings.MinimumSingleSupportCoverageRatio
						|| bAllowSingleHorizontalBearing))
				|| (Merged.Num() >= 2
					&& SpanRatio >= BeamCSettings.MinimumSeparatedSupportSpanRatio);
		};

		// A Y course is the upper half of one common XY band.  Close it first with
		// a real same-band X bearing.  Neighbouring Y members share one transfer
		// rail; that rail is then carried by Z posts which stop at the X-course
		// bottom and land on the previous band's upper Y course.
		TArray<FYTransferNeed> YTransferNeeds;
		for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			for (const int32 MemberIndex : Group.MemberIndices)
			{
				if (!Plan.Members.IsValidIndex(MemberIndex))
				{
					continue;
				}
				const FPlannedMember& Upper = Plan.Members[MemberIndex];
				if (Upper.OwnerKind != EOwnerKind::BuildingGroupShell
					|| Upper.OwnerId != Group.GroupId
					|| Upper.Axis != EABTSM73BeamAFrameAxis::Y
					|| (Upper.SkeletonKind != ESkeletonMemberKind::ThroughCourse
						&& Upper.SkeletonKind != ESkeletonMemberKind::FacadeCourse))
				{
					continue;
				}
				const int32 BaseCourse = Upper.CourseIndex - 1;
				const int32 BandIndex = Group.CommonBandBaseCourseIndices.Find(BaseCourse);
				if (BandIndex == INDEX_NONE)
				{
					continue;
				}
				TArray<FVector2D> ExistingIntervals;
				CollectSupportIntervals(MemberIndex, nullptr, ExistingIntervals);
				double RequiredMinimum = 0.0;
				double RequiredMaximum = 0.0;
				CollectRequiredLoadRange(MemberIndex, nullptr,
					RequiredMinimum, RequiredMaximum);
				if (SupportHullSatisfies(Upper, ExistingIntervals, false,
					RequiredMinimum, RequiredMaximum))
				{
					continue;
				}
				const FBox UpperBounds = PlannedMemberBounds(Upper);
				TArray<double> CandidateStations = {
					UpperBounds.Min.Y + BlockUnitsCM * 0.5,
					UpperBounds.Max.Y - BlockUnitsCM * 0.5};
				CandidateStations.Sort([&ExistingIntervals](const double A, const double B)
				{
					auto SpanWith = [&ExistingIntervals](const double Station)
					{
						double Minimum = Station - BlockUnitsCM * 0.5;
						double Maximum = Station + BlockUnitsCM * 0.5;
						for (const FVector2D& Interval : ExistingIntervals)
						{
							Minimum = FMath::Min(Minimum, Interval.X);
							Maximum = FMath::Max(Maximum, Interval.Y);
						}
						return Maximum - Minimum;
					};
					const double ASpan = SpanWith(A);
					const double BSpan = SpanWith(B);
					return !FMath::IsNearlyEqual(ASpan, BSpan, GeometryToleranceCM)
						? ASpan > BSpan : A < B;
				});
				if (CandidateStations.Num() > 1
					&& FMath::IsNearlyEqual(CandidateStations[0], CandidateStations[1],
						GeometryToleranceCM))
				{
					CandidateStations.SetNum(1);
				}
				bool bFoundStation = false;
				for (const double StationYCM : CandidateStations)
				{
					TArray<FVector2D> WithCandidate = ExistingIntervals;
					WithCandidate.Emplace(StationYCM - BlockUnitsCM * 0.5,
						StationYCM + BlockUnitsCM * 0.5);
					if (!SupportHullSatisfies(Upper, MoveTemp(WithCandidate), false,
						RequiredMinimum, RequiredMaximum))
					{
						continue;
					}
					FYTransferNeed& Need = YTransferNeeds.AddDefaulted_GetRef();
					Need.UpperIndex = MemberIndex;
					Need.GroupId = Group.GroupId;
					Need.BandIndex = BandIndex;
					Need.BaseCourse = BaseCourse;
					Need.StationXCM = UpperBounds.GetCenter().X;
					Need.StationYCM = StationYCM;
					Need.FaceMask = Upper.FaceMask;
					bFoundStation = true;
					break;
				}
				if (!bFoundStation && CandidateStations.Num() > 1)
				{
					TArray<FVector2D> WithBothCandidates = ExistingIntervals;
					for (const double StationYCM : CandidateStations)
					{
						WithBothCandidates.Emplace(
							StationYCM - BlockUnitsCM * 0.5,
							StationYCM + BlockUnitsCM * 0.5);
					}
					if (SupportHullSatisfies(Upper, MoveTemp(WithBothCandidates), false,
						RequiredMinimum, RequiredMaximum))
					{
						for (const double StationYCM : CandidateStations)
						{
							FYTransferNeed& Need = YTransferNeeds.AddDefaulted_GetRef();
							Need.UpperIndex = MemberIndex;
							Need.GroupId = Group.GroupId;
							Need.BandIndex = BandIndex;
							Need.BaseCourse = BaseCourse;
							Need.StationXCM = UpperBounds.GetCenter().X;
							Need.StationYCM = StationYCM;
							Need.FaceMask = Upper.FaceMask;
						}
						bFoundStation = true;
					}
				}
				if (!bFoundStation)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CommonFrameYBearingStationUnavailable:Member=%d:Course=%d:Length=%.1f"),
						MemberIndex, Upper.CourseIndex,
						UpperBounds.Max.Y - UpperBounds.Min.Y);
					return false;
				}
			}
		}
		YTransferNeeds.Sort([](const FYTransferNeed& A, const FYTransferNeed& B)
		{
			if (A.GroupId != B.GroupId) return A.GroupId < B.GroupId;
			if (A.BaseCourse != B.BaseCourse) return A.BaseCourse < B.BaseCourse;
			if (!FMath::IsNearlyEqual(A.StationYCM, B.StationYCM,
				GeometryToleranceCM)) return A.StationYCM < B.StationYCM;
			return A.StationXCM < B.StationXCM;
		});
		for (int32 NeedCursor = 0; NeedCursor < YTransferNeeds.Num();)
		{
			const FYTransferNeed& FirstNeed = YTransferNeeds[NeedCursor];
			if (!Plan.BuildingGroups.IsValidIndex(FirstNeed.GroupId))
			{
				OutError = TEXT("BeamC3V3CommonFrameYTransferOwnerInvalid");
				return false;
			}
			int32 NeedEnd = NeedCursor + 1;
			while (NeedEnd < YTransferNeeds.Num()
				&& NeedEnd - NeedCursor < 2)
			{
				const FYTransferNeed& Candidate = YTransferNeeds[NeedEnd];
				if (Candidate.GroupId != FirstNeed.GroupId
					|| Candidate.BaseCourse != FirstNeed.BaseCourse
					|| FMath::Abs(Candidate.StationYCM - FirstNeed.StationYCM)
						> GeometryToleranceCM
					|| Candidate.StationXCM - FirstNeed.StationXCM + BlockUnitsCM
						> MaximumHorizontalUnits * BlockUnitsCM)
				{
					break;
				}
				++NeedEnd;
			}
			FBuildingGroupPlan& Group = Plan.BuildingGroups[FirstNeed.GroupId];
			const double RailMinimumX = FirstNeed.StationXCM - BlockUnitsCM * 0.5;
			const double RailMaximumX = YTransferNeeds[NeedEnd - 1].StationXCM
				+ BlockUnitsCM * 0.5;
			const double RailZCM = Group.GroundPlaneZCM
				+ (FirstNeed.BaseCourse + 0.5) * BlockUnitsCM;
			uint8 RailFaceMask = 0;
			for (int32 NeedIndex = NeedCursor; NeedIndex < NeedEnd; ++NeedIndex)
			{
				RailFaceMask |= YTransferNeeds[NeedIndex].FaceMask;
			}
			FPlannedMember RailProbe;
			RailProbe.Axis = EABTSM73BeamAFrameAxis::X;
			RailProbe.LocalStart = Position(RailMinimumX, FirstNeed.StationYCM, RailZCM);
			RailProbe.LocalEnd = Position(RailMaximumX, FirstNeed.StationYCM, RailZCM);
			const FBox RailBounds = PlannedMemberBounds(RailProbe);
			const bool bVoidBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
				[&RailBounds](const FABTSM73BeamASupportVoid& Void)
				{
					return BoxesPenetrate(RailBounds, Void.Bounds);
				});
			bool bBlocked = bVoidBlocked;
			int32 BlockingMemberIndex = INDEX_NONE;
			for (int32 ExistingIndex = 0; ExistingIndex < Plan.Members.Num(); ++ExistingIndex)
			{
				if (BoxesPenetrate(RailBounds,
					PlannedMemberBounds(Plan.Members[ExistingIndex])))
				{
					bBlocked = true;
					BlockingMemberIndex = ExistingIndex;
					break;
				}
			}
			int32 AdoptedRailIndex = INDEX_NONE;
			if (!bVoidBlocked && Plan.Members.IsValidIndex(BlockingMemberIndex))
			{
				const FPlannedMember& Blocking = Plan.Members[BlockingMemberIndex];
				const FBox BlockingBounds = PlannedMemberBounds(Blocking);
				const bool bAdoptableOwner =
					Blocking.OwnerKind == EOwnerKind::BuildingGroupShell
					|| Blocking.OwnerKind == EOwnerKind::CoreCell
					|| Blocking.OwnerKind == EOwnerKind::SupportedSpan;
				const double ExtendedMinimumX = FMath::Min(
					BlockingBounds.Min.X, RailMinimumX);
				const double ExtendedMaximumX = FMath::Max(
					BlockingBounds.Max.X, RailMaximumX);
				if (bAdoptableOwner
					&& Blocking.Axis == EABTSM73BeamAFrameAxis::X
					&& Blocking.CourseIndex == FirstNeed.BaseCourse
					&& FMath::Abs(BlockingBounds.GetCenter().Y - FirstNeed.StationYCM)
						< BlockUnitsCM - GeometryToleranceCM
					&& ExtendedMaximumX - ExtendedMinimumX
						<= MaximumHorizontalUnits * BlockUnitsCM + GeometryToleranceCM)
				{
					FPlannedMember Extended = Blocking;
					if (Extended.LocalStart.X <= Extended.LocalEnd.X)
					{
						Extended.LocalStart.X = ExtendedMinimumX;
						Extended.LocalEnd.X = ExtendedMaximumX;
					}
					else
					{
						Extended.LocalEnd.X = ExtendedMinimumX;
						Extended.LocalStart.X = ExtendedMaximumX;
					}
					const FBox ExtendedBounds = PlannedMemberBounds(Extended);
					bool bExtensionBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
						[&ExtendedBounds](const FABTSM73BeamASupportVoid& Void)
						{
							return BoxesPenetrate(ExtendedBounds, Void.Bounds);
						});
					for (int32 ExistingIndex = 0;
						ExistingIndex < Plan.Members.Num() && !bExtensionBlocked;
						++ExistingIndex)
					{
						if (ExistingIndex != BlockingMemberIndex
							&& BoxesPenetrate(ExtendedBounds,
								PlannedMemberBounds(Plan.Members[ExistingIndex])))
						{
							bExtensionBlocked = true;
						}
					}
					for (int32 NeedIndex = NeedCursor;
						NeedIndex < NeedEnd && !bExtensionBlocked; ++NeedIndex)
					{
						const int32 UpperIndex = YTransferNeeds[NeedIndex].UpperIndex;
						bExtensionBlocked = !Plan.Members.IsValidIndex(UpperIndex)
							|| !HasFullPlanarContact(ExtendedBounds,
								PlannedMemberBounds(Plan.Members[UpperIndex]));
					}
					if (!bExtensionBlocked)
					{
						FPlannedMember& Adopted = Plan.Members[BlockingMemberIndex];
						Adopted.LocalStart = Extended.LocalStart;
						Adopted.LocalEnd = Extended.LocalEnd;
						Adopted.FaceMask |= RailFaceMask;
						Group.MemberIndices.AddUnique(BlockingMemberIndex);
						Plan.Summary.MaximumMemberLengthCM = FMath::Max(
							Plan.Summary.MaximumMemberLengthCM,
							static_cast<float>(ExtendedMaximumX - ExtendedMinimumX));
						AdoptedRailIndex = BlockingMemberIndex;
						bBlocked = false;
					}
				}
			}
			if (AdoptedRailIndex != INDEX_NONE)
			{
				NeedCursor = NeedEnd;
				continue;
			}
			if (bBlocked)
			{
				const FPlannedMember* BlockingMember =
					Plan.Members.IsValidIndex(BlockingMemberIndex)
						? &Plan.Members[BlockingMemberIndex] : nullptr;
				OutError = FString::Printf(
					TEXT("BeamC3V3CommonFrameYTransferBlocked:Course=%d:Y=%.1f:X=%.1f..%.1f:Blocker=%d:O=%d:K=%d:A=%d:From=%s:To=%s"),
					FirstNeed.BaseCourse, FirstNeed.StationYCM,
					RailMinimumX, RailMaximumX, BlockingMemberIndex,
					BlockingMember != nullptr
						? static_cast<int32>(BlockingMember->OwnerKind) : INDEX_NONE,
					BlockingMember != nullptr
						? static_cast<int32>(BlockingMember->SkeletonKind) : INDEX_NONE,
					BlockingMember != nullptr
						? static_cast<int32>(BlockingMember->Axis) : INDEX_NONE,
					BlockingMember != nullptr
						? *BlockingMember->LocalStart.ToCompactString() : TEXT("None"),
					BlockingMember != nullptr
						? *BlockingMember->LocalEnd.ToCompactString() : TEXT("None"));
				return false;
			}
			const double PostTopZCM = Group.GroundPlaneZCM
				+ FirstNeed.BaseCourse * BlockUnitsCM;
			const int32 PreviousBase = FirstNeed.BandIndex > 0
				? Group.CommonBandBaseCourseIndices[FirstNeed.BandIndex - 1]
				: INDEX_NONE;
			const double PostBottomZCM = FirstNeed.BandIndex > 0
				? Group.GroundPlaneZCM + (PreviousBase + 2) * BlockUnitsCM
				: Group.GroundPlaneZCM;
			TArray<FPostCandidate> TransferPosts;
			for (int32 NeedIndex = NeedCursor; NeedIndex < NeedEnd; ++NeedIndex)
			{
				const FYTransferNeed& Need = YTransferNeeds[NeedIndex];
				if (Need.BandIndex == 0)
				{
					continue;
				}
				const double PostLengthCM = PostTopZCM - PostBottomZCM;
				if (FMath::Abs(PostLengthCM) <= GeometryToleranceCM)
				{
					bool bHasDirectLowerSeat = false;
					for (const FPlannedMember& Lower : Plan.Members)
					{
						if (Lower.Axis == EABTSM73BeamAFrameAxis::Y
							&& Lower.CourseIndex == PreviousBase + 1)
						{
							const FBox LowerBounds = PlannedMemberBounds(Lower);
							bHasDirectLowerSeat |=
								FMath::Abs(LowerBounds.Max.Z - RailBounds.Min.Z)
									<= GeometryToleranceCM
								&& HasFullPlanarContact(LowerBounds, RailBounds);
						}
					}
					if (!bHasDirectLowerSeat)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3CommonFrameYTransferDirectSeatUnavailable:Course=%d:X=%.1f:Y=%.1f"),
							Need.BaseCourse, Need.StationXCM, Need.StationYCM);
						return false;
					}
					continue;
				}
				if (PostLengthCM < BlockUnitsCM - GeometryToleranceCM)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CommonFrameYTransferPostTooShort:Course=%d:Length=%.1f"),
						Need.BaseCourse, PostLengthCM);
					return false;
				}
				FPlannedMember PostProbe;
				PostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
				PostProbe.LocalStart = Position(Need.StationXCM, Need.StationYCM,
					PostBottomZCM);
				PostProbe.LocalEnd = Position(Need.StationXCM, Need.StationYCM,
					PostTopZCM);
				const FBox PostBounds = PlannedMemberBounds(PostProbe);
				bool bHasLowerSeat = false;
				for (const FPlannedMember& Lower : Plan.Members)
				{
					if (Lower.Axis == EABTSM73BeamAFrameAxis::Y
						&& Lower.CourseIndex == PreviousBase + 1)
					{
						const FBox LowerBounds = PlannedMemberBounds(Lower);
						bHasLowerSeat |= FMath::Abs(LowerBounds.Max.Z - PostBounds.Min.Z)
							<= GeometryToleranceCM
							&& HasFullPlanarContact(LowerBounds, PostBounds);
					}
				}
				bool bPostBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
					[&PostBounds](const FABTSM73BeamASupportVoid& Void)
					{
						return BoxesPenetrate(PostBounds, Void.Bounds);
					});
				for (const FPlannedMember& Existing : Plan.Members)
				{
					bPostBlocked |= BoxesPenetrate(PostBounds,
						PlannedMemberBounds(Existing));
				}
				if (!bHasLowerSeat || bPostBlocked)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3CommonFrameYTransferPostUnavailable:Course=%d:X=%.1f:Y=%.1f:Seat=%d:Blocked=%d"),
						Need.BaseCourse, Need.StationXCM, Need.StationYCM,
						bHasLowerSeat ? 1 : 0, bPostBlocked ? 1 : 0);
					return false;
				}
				FPostCandidate& Post = TransferPosts.AddDefaulted_GetRef();
				Post.StationXCM = Need.StationXCM;
				Post.StationYCM = Need.StationYCM;
				Post.BottomZCM = PostBottomZCM;
				Post.TopZCM = PostTopZCM;
				Post.FaceMask = Need.FaceMask;
			}
			const int32 OriginCore = Plan.Members.IsValidIndex(FirstNeed.UpperIndex)
				? Plan.Members[FirstNeed.UpperIndex].OriginCoreCellId : INDEX_NONE;
			const int32 RailIndex = Plan.Members.Num();
			if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
				RailFaceMask != 0 ? ESkeletonMemberKind::FacadeCourse
					: ESkeletonMemberKind::ThroughCourse,
				Group.GroupId, INDEX_NONE, INDEX_NONE, OriginCore,
				FirstNeed.BaseCourse,
				FMath::RoundToInt(FirstNeed.StationYCM / BlockUnitsCM),
				FMath::RoundToInt(FirstNeed.StationXCM / BlockUnitsCM),
				RailFaceMask, EABTSM73BeamAFrameAxis::X,
				EABTSM73BeamAMemberRole::PrimaryBeam,
				RailProbe.LocalStart, RailProbe.LocalEnd, OutError))
			{
				return false;
			}
			Group.MemberIndices.Add(RailIndex);
			for (const FPostCandidate& Post : TransferPosts)
			{
				const int32 PostIndex = Plan.Members.Num();
				if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
					ESkeletonMemberKind::ExteriorPost, Group.GroupId, INDEX_NONE,
					INDEX_NONE, OriginCore, FirstNeed.BaseCourse,
					FMath::RoundToInt(Post.StationXCM / BlockUnitsCM),
					FMath::RoundToInt(Post.StationYCM / BlockUnitsCM),
					Post.FaceMask, EABTSM73BeamAFrameAxis::Z,
					EABTSM73BeamAMemberRole::Post,
					Position(Post.StationXCM, Post.StationYCM, Post.BottomZCM),
					Position(Post.StationXCM, Post.StationYCM, Post.TopZCM), OutError))
				{
					return false;
				}
				Group.MemberIndices.Add(PostIndex);
			}
			NeedCursor = NeedEnd;
		}

		TArray<int32> FinalUpperMembers;
		TMap<int32, int32> FinalUpperMemberGroups;
		for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bBelongsToGroup = Group.MemberIndices.Contains(MemberIndex)
					|| (Member.OwnerKind == EOwnerKind::BuildingGroupShell
						&& Member.OwnerId == Group.GroupId)
					|| (Member.ComponentId != INDEX_NONE
						&& Group.ComponentIds.Contains(Member.ComponentId))
					|| (Member.OwnerKind == EOwnerKind::CoreCell
						&& Group.CoreCellIds.Contains(Member.OwnerId))
					|| Member.EndpointCoreCellIds.ContainsByPredicate(
						[&Group](const int32 CoreCellId)
						{
							return Group.CoreCellIds.Contains(CoreCellId);
						});
				const bool bClosureKind =
					Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::SharedCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::RoofCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm;
				if (bBelongsToGroup
					&& Member.Axis == EABTSM73BeamAFrameAxis::X
					&& bClosureKind
					&& Group.CommonBandBaseCourseIndices.Contains(Member.CourseIndex))
				{
					if (const int32* ExistingGroup = FinalUpperMemberGroups.Find(MemberIndex);
						ExistingGroup != nullptr && *ExistingGroup != Group.GroupId)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3CommonFrameSupportGroupAmbiguous:Member=%d:A=%d:B=%d"),
							MemberIndex, *ExistingGroup, Group.GroupId);
						return false;
					}
					FinalUpperMembers.AddUnique(MemberIndex);
					FinalUpperMemberGroups.Add(MemberIndex, Group.GroupId);
				}
			}
		}
		TSet<int32> RemovedMembers;
		TMap<int32, int32> NoReplacements;
		auto ComputeReactionWeightedLoadResultants =
			[&Plan, &BeamCSettings, StationMergeToleranceCM](
				const TSet<int32>& IgnoredMembers,
				TArray<FVector>& OutResultants,
				TArray<bool>& OutValid)
		{
			struct FLoadContact
			{
				int32 LowerIndex = INDEX_NONE;
				FVector Position = FVector::ZeroVector;
				double AreaCM2 = 0.0;
			};
			struct FLoadStation
			{
				double Coordinate = 0.0;
				TArray<int32> ContactIndices;
				double AreaCM2 = 0.0;
			};

			const int32 MemberCount = Plan.Members.Num();
			OutResultants.Init(FVector::ZeroVector, MemberCount);
			OutValid.Init(false, MemberCount);
			TArray<FBox> Bounds;
			Bounds.SetNum(MemberCount);
			TArray<double> AccumulatedLoads;
			AccumulatedLoads.Init(0.0, MemberCount);
			TArray<FVector> FirstMoments;
			FirstMoments.Init(FVector::ZeroVector, MemberCount);
			TArray<TArray<FLoadContact>> ContactsByUpper;
			ContactsByUpper.SetNum(MemberCount);
			TMap<int64, TArray<int32>> MembersByTop;
			const double BucketScale = 1.0 / GeometryToleranceCM;
			for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
			{
				if (IgnoredMembers.Contains(MemberIndex))
				{
					continue;
				}
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				Bounds[MemberIndex] = PlannedMemberBounds(Member);
				const double SelfLoad = FMath::Max(UE_DOUBLE_SMALL_NUMBER,
					FVector::Distance(Member.LocalStart, Member.LocalEnd));
				AccumulatedLoads[MemberIndex] = SelfLoad;
				FirstMoments[MemberIndex] =
					(Member.LocalStart + Member.LocalEnd) * 0.5 * SelfLoad;
				MembersByTop.FindOrAdd(FMath::RoundToInt64(
					Bounds[MemberIndex].Max.Z * BucketScale)).Add(MemberIndex);
			}
			for (int32 UpperIndex = 0; UpperIndex < MemberCount; ++UpperIndex)
			{
				if (IgnoredMembers.Contains(UpperIndex))
				{
					continue;
				}
				const FBox& UpperBounds = Bounds[UpperIndex];
				const int64 BottomBucket = FMath::RoundToInt64(
					UpperBounds.Min.Z * BucketScale);
				for (int64 BucketOffset = -1; BucketOffset <= 1; ++BucketOffset)
				{
					const TArray<int32>* LowerCandidates =
						MembersByTop.Find(BottomBucket + BucketOffset);
					if (LowerCandidates == nullptr)
					{
						continue;
					}
					for (const int32 LowerIndex : *LowerCandidates)
					{
						if (LowerIndex == UpperIndex
							|| IgnoredMembers.Contains(LowerIndex))
						{
							continue;
						}
						const FBox& LowerBounds = Bounds[LowerIndex];
						const double OverlapX = SkeletonV3OverlapLength(
							LowerBounds.Min.X, LowerBounds.Max.X,
							UpperBounds.Min.X, UpperBounds.Max.X);
						const double OverlapY = SkeletonV3OverlapLength(
							LowerBounds.Min.Y, LowerBounds.Max.Y,
							UpperBounds.Min.Y, UpperBounds.Max.Y);
						if (FMath::Abs(LowerBounds.Max.Z - UpperBounds.Min.Z)
								> GeometryToleranceCM
							|| OverlapX <= GeometryToleranceCM
							|| OverlapY <= GeometryToleranceCM)
						{
							continue;
						}
						FLoadContact& Contact =
							ContactsByUpper[UpperIndex].AddDefaulted_GetRef();
						Contact.LowerIndex = LowerIndex;
						Contact.Position = FVector(
							(FMath::Max(LowerBounds.Min.X, UpperBounds.Min.X)
								+ FMath::Min(LowerBounds.Max.X, UpperBounds.Max.X)) * 0.5,
							(FMath::Max(LowerBounds.Min.Y, UpperBounds.Min.Y)
								+ FMath::Min(LowerBounds.Max.Y, UpperBounds.Max.Y)) * 0.5,
							LowerBounds.Max.Z);
						Contact.AreaCM2 = OverlapX * OverlapY;
					}
				}
			}

			TArray<int32> TopologicalOrder;
			for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
			{
				if (!IgnoredMembers.Contains(MemberIndex))
				{
					TopologicalOrder.Add(MemberIndex);
				}
			}
			TopologicalOrder.Sort([&Bounds](const int32 A, const int32 B)
			{
				if (!FMath::IsNearlyEqual(Bounds[A].Min.Z, Bounds[B].Min.Z,
					GeometryToleranceCM))
				{
					return Bounds[A].Min.Z > Bounds[B].Min.Z;
				}
				if (!FMath::IsNearlyEqual(Bounds[A].Max.Z, Bounds[B].Max.Z,
					GeometryToleranceCM))
				{
					return Bounds[A].Max.Z > Bounds[B].Max.Z;
				}
				return A < B;
			});

			for (const int32 UpperIndex : TopologicalOrder)
			{
				const FPlannedMember& Upper = Plan.Members[UpperIndex];
				const double SafeLoad = FMath::Max(
					AccumulatedLoads[UpperIndex], UE_DOUBLE_SMALL_NUMBER);
				OutResultants[UpperIndex] = FirstMoments[UpperIndex] / SafeLoad;
				OutValid[UpperIndex] = true;
				if (Bounds[UpperIndex].Min.Z
						<= BeamCSettings.BeamB.BeamA.JointMergeToleranceCM
					|| ContactsByUpper[UpperIndex].IsEmpty())
				{
					continue;
				}

				const TArray<FLoadContact>& Contacts = ContactsByUpper[UpperIndex];
				TArray<double> Shares;
				Shares.Init(0.0, Contacts.Num());
				if (Upper.Axis == EABTSM73BeamAFrameAxis::X
					|| Upper.Axis == EABTSM73BeamAFrameAxis::Y)
				{
					const int32 AxisIndex = static_cast<int32>(Upper.Axis);
					TArray<FLoadStation> Stations;
					for (int32 ContactIndex = 0;
						ContactIndex < Contacts.Num(); ++ContactIndex)
					{
						const double Coordinate =
							Contacts[ContactIndex].Position[AxisIndex];
						FLoadStation* Station = Stations.FindByPredicate(
							[Coordinate, StationMergeToleranceCM](
								const FLoadStation& Existing)
							{
								return FMath::Abs(Existing.Coordinate - Coordinate)
									<= StationMergeToleranceCM;
							});
						if (Station == nullptr)
						{
							Station = &Stations.AddDefaulted_GetRef();
							Station->Coordinate = Coordinate;
						}
						Station->ContactIndices.Add(ContactIndex);
						Station->AreaCM2 += Contacts[ContactIndex].AreaCM2;
					}
					Stations.Sort([](const FLoadStation& A, const FLoadStation& B)
					{
						return A.Coordinate < B.Coordinate;
					});
					auto ApplyStationShare = [&Shares, &Contacts](
						const FLoadStation& Station, const double StationShare)
					{
						const double SafeArea = FMath::Max(
							Station.AreaCM2, UE_DOUBLE_SMALL_NUMBER);
						for (const int32 ContactIndex : Station.ContactIndices)
						{
							Shares[ContactIndex] = StationShare
								* Contacts[ContactIndex].AreaCM2 / SafeArea;
						}
					};
					const double ResultantCoordinate =
						OutResultants[UpperIndex][AxisIndex];
					if (Stations.Num() == 1
						|| ResultantCoordinate <= Stations[0].Coordinate)
					{
						ApplyStationShare(Stations[0], 1.0);
					}
					else if (ResultantCoordinate >= Stations.Last().Coordinate)
					{
						ApplyStationShare(Stations.Last(), 1.0);
					}
					else
					{
						for (int32 StationIndex = 0;
							StationIndex + 1 < Stations.Num(); ++StationIndex)
						{
							const FLoadStation& Left = Stations[StationIndex];
							const FLoadStation& Right = Stations[StationIndex + 1];
							if (ResultantCoordinate < Left.Coordinate
								|| ResultantCoordinate > Right.Coordinate)
							{
								continue;
							}
							const double Denominator = FMath::Max(
								Right.Coordinate - Left.Coordinate,
								StationMergeToleranceCM);
							const double RightShare =
								(ResultantCoordinate - Left.Coordinate) / Denominator;
							ApplyStationShare(Left, 1.0 - RightShare);
							ApplyStationShare(Right, RightShare);
							break;
						}
					}
				}
				else
				{
					double TotalArea = 0.0;
					for (const FLoadContact& Contact : Contacts)
					{
						TotalArea += Contact.AreaCM2;
					}
					for (int32 ContactIndex = 0;
						ContactIndex < Contacts.Num(); ++ContactIndex)
					{
						Shares[ContactIndex] = Contacts[ContactIndex].AreaCM2
							/ FMath::Max(TotalArea, UE_DOUBLE_SMALL_NUMBER);
					}
				}

				for (int32 ContactIndex = 0;
					ContactIndex < Contacts.Num(); ++ContactIndex)
				{
					const double Reaction =
						Shares[ContactIndex] * AccumulatedLoads[UpperIndex];
					if (Reaction <= UE_DOUBLE_SMALL_NUMBER)
					{
						continue;
					}
					const FLoadContact& Contact = Contacts[ContactIndex];
					AccumulatedLoads[Contact.LowerIndex] += Reaction;
					FirstMoments[Contact.LowerIndex] += Contact.Position * Reaction;
				}
			}
		};
		TArray<FVector> ClosureLoadResultants;
		TArray<bool> ClosureLoadResultantValid;
		// One bounded two-sweep closure replaces open-ended retry.  The ascending
		// sweep first makes every carrier stable.  The descending sweep then sees
		// every newly added upper bearing before it reaches the lower rail that must
		// carry that load, so no third convergence pass is permitted or required.
		for (int32 ClosurePass = 0; ClosurePass < 2; ++ClosurePass)
		{
			if (ClosurePass == 1)
			{
				ComputeReactionWeightedLoadResultants(RemovedMembers,
					ClosureLoadResultants, ClosureLoadResultantValid);
			}
			FinalUpperMembers.Sort([&Plan, ClosurePass](const int32 A, const int32 B)
			{
				if (Plan.Members[A].CourseIndex != Plan.Members[B].CourseIndex)
				{
					return ClosurePass == 0
						? Plan.Members[A].CourseIndex < Plan.Members[B].CourseIndex
						: Plan.Members[A].CourseIndex > Plan.Members[B].CourseIndex;
				}
				return A < B;
			});
			for (const int32 UpperIndex : FinalUpperMembers)
			{
			if (!Plan.Members.IsValidIndex(UpperIndex)
				|| RemovedMembers.Contains(UpperIndex))
			{
				continue;
			}
			const FPlannedMember Upper = Plan.Members[UpperIndex];
			const int32* GroupId = FinalUpperMemberGroups.Find(UpperIndex);
			if (GroupId == nullptr || !Plan.BuildingGroups.IsValidIndex(*GroupId))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CommonFrameSupportOwnerInvalid:Member=%d:OwnerKind=%d:Owner=%d"),
					UpperIndex, static_cast<int32>(Upper.OwnerKind), Upper.OwnerId);
				return false;
			}
			FBuildingGroupPlan& Group = Plan.BuildingGroups[*GroupId];
			const int32 BandIndex = Group.CommonBandBaseCourseIndices.Find(
				Upper.CourseIndex);
			if (BandIndex <= 0)
			{
				continue;
			}
			const int32 PreviousBase = Group.CommonBandBaseCourseIndices[BandIndex - 1];
			const FBox UpperBounds = PlannedMemberBounds(Upper);
			const int32 AxisIndex = static_cast<int32>(Upper.Axis);
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			const double UpperLengthCM = UpperBounds.Max[AxisIndex]
				- UpperBounds.Min[AxisIndex];
			const double BottomZCM = Group.GroundPlaneZCM
				+ (PreviousBase + 2) * BlockUnitsCM;
			const double TopZCM = UpperBounds.Min.Z;

			TArray<FVector2D> ExistingSupportIntervals;
			CollectSupportIntervals(UpperIndex, &RemovedMembers,
				ExistingSupportIntervals);
			double RequiredMinimum = 0.0;
			double RequiredMaximum = 0.0;
			if (ClosurePass == 1
				&& ClosureLoadResultantValid.IsValidIndex(UpperIndex)
				&& ClosureLoadResultantValid[UpperIndex])
			{
				RequiredMinimum = ClosureLoadResultants[UpperIndex][AxisIndex];
				RequiredMaximum = RequiredMinimum;
			}
			else
			{
				CollectRequiredLoadRange(UpperIndex, &RemovedMembers,
					RequiredMinimum, RequiredMaximum);
			}
			// This preflight proves the mass-independent self-load centre and
			// separated-support geometry.  The weighted resultant of the complete
			// upper DAG remains the responsibility of the read-only Beam-C audit.

			if (SupportHullSatisfies(Upper, ExistingSupportIntervals, false,
				RequiredMinimum, RequiredMaximum))
			{
				continue;
			}

			TArray<FPostCandidate> Candidates;
			TSet<int64> CandidateStationKeys;
			FString CandidateDiagnostics;
			for (const FPlannedMember& LowerRail : Plan.Members)
			{
				const bool bCoreDerivedLower =
					LowerRail.OwnerKind == EOwnerKind::BuildingGroupShell
					|| LowerRail.OwnerKind == EOwnerKind::CoreCell
					|| LowerRail.OwnerKind == EOwnerKind::SupportedSpan;
				if (!bCoreDerivedLower
					|| LowerRail.Axis != EABTSM73BeamAFrameAxis::Y
					|| LowerRail.CourseIndex != PreviousBase + 1)
				{
					continue;
				}
				const FBox LowerBounds = PlannedMemberBounds(LowerRail);
				if (FMath::Abs(LowerBounds.Max.Z - BottomZCM) > GeometryToleranceCM)
				{
					continue;
				}
				const double StationXCM = LowerBounds.GetCenter().X;
				const double StationYCM = UpperBounds.GetCenter()[CrossAxisIndex];
				const int64 StationKey = FMath::RoundToInt64(
					StationXCM / GeometryToleranceCM);
				if (CandidateStationKeys.Contains(StationKey))
				{
					continue;
				}
				if (TopZCM - BottomZCM
					< BlockUnitsCM - GeometryToleranceCM)
				{
					continue;
				}
				FPlannedMember PostProbe;
				PostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
				PostProbe.LocalStart = Position(StationXCM, StationYCM, BottomZCM);
				PostProbe.LocalEnd = Position(StationXCM, StationYCM, TopZCM);
				const FBox PostBounds = PlannedMemberBounds(PostProbe);
				const bool bFullLowerSeat =
					SkeletonV3OverlapLength(PostBounds.Min.X, PostBounds.Max.X,
						LowerBounds.Min.X, LowerBounds.Max.X)
						>= BlockUnitsCM - GeometryToleranceCM
					&& SkeletonV3OverlapLength(PostBounds.Min.Y, PostBounds.Max.Y,
						LowerBounds.Min.Y, LowerBounds.Max.Y)
						>= BlockUnitsCM - GeometryToleranceCM;
				const bool bFullUpperSeat =
					SkeletonV3OverlapLength(PostBounds.Min.X, PostBounds.Max.X,
						UpperBounds.Min.X, UpperBounds.Max.X)
						>= BlockUnitsCM - GeometryToleranceCM
					&& SkeletonV3OverlapLength(PostBounds.Min.Y, PostBounds.Max.Y,
						UpperBounds.Min.Y, UpperBounds.Max.Y)
						>= BlockUnitsCM - GeometryToleranceCM;
				if (!bFullLowerSeat || !bFullUpperSeat)
				{
					continue;
				}
				bool bBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
					[&PostBounds](const FABTSM73BeamASupportVoid& Void)
					{
						return BoxesPenetrate(PostBounds, Void.Bounds);
					});
				for (const FPlannedMember& Existing : Plan.Members)
				{
					if (BoxesPenetrate(PostBounds, PlannedMemberBounds(Existing)))
					{
						bBlocked = true;
						break;
					}
				}
				if (bBlocked)
				{
					continue;
				}
				CandidateStationKeys.Add(StationKey);
				const FVector2D CandidateInterval(
					FMath::Max(PostBounds.Min[AxisIndex], UpperBounds.Min[AxisIndex]),
					FMath::Min(PostBounds.Max[AxisIndex], UpperBounds.Max[AxisIndex]));
				if (ExistingSupportIntervals.ContainsByPredicate(
					[&CandidateInterval](const FVector2D& ExistingInterval)
					{
						return FMath::IsNearlyEqual(ExistingInterval.X,
							CandidateInterval.X, GeometryToleranceCM)
							&& FMath::IsNearlyEqual(ExistingInterval.Y,
								CandidateInterval.Y, GeometryToleranceCM);
					}))
				{
					continue;
				}
				FPostCandidate& Candidate = Candidates.AddDefaulted_GetRef();
				Candidate.StationXCM = StationXCM;
				Candidate.StationYCM = StationYCM;
				Candidate.BottomZCM = BottomZCM;
				Candidate.TopZCM = TopZCM;
				Candidate.FaceMask = Upper.FaceMask;
				if (FMath::Abs(StationXCM - Group.LocalBounds.Min.X)
					<= GeometryToleranceCM)
				{
					Candidate.FaceMask |= ABTSM73BeamC3V3::NegativeX;
				}
				if (FMath::Abs(StationXCM - Group.LocalBounds.Max.X)
					<= GeometryToleranceCM)
				{
					Candidate.FaceMask |= ABTSM73BeamC3V3::PositiveX;
				}
				Candidate.SupportInterval = CandidateInterval;
			}

			// If the previous band's upper Y course has no farther station, grow one
			// short Y bearing cap from the previous X course.  The cap is structural:
			// Previous-X -> cap-Y -> optional Z -> Current-X.  This avoids both an
			// unsupported free-standing post and rounding a just-short support span up.
			TArray<double> TransferStations = {
				UpperBounds.Min.X + BlockUnitsCM * 0.5,
				UpperBounds.Max.X - BlockUnitsCM * 0.5,
				UpperBounds.GetCenter().X};
			const double MinimumUsableStation = UpperBounds.Min.X
				+ BlockUnitsCM * 0.5;
			const double MaximumUsableStation = UpperBounds.Max.X
				- BlockUnitsCM * 0.5;
			TransferStations.Add(FMath::Clamp(
				FMath::GridSnap(RequiredMinimum, static_cast<double>(BlockUnitsCM)),
				MinimumUsableStation, MaximumUsableStation));
			TransferStations.Add(FMath::Clamp(
				FMath::GridSnap(RequiredMaximum, static_cast<double>(BlockUnitsCM)),
				MinimumUsableStation, MaximumUsableStation));
			if (!ExistingSupportIntervals.IsEmpty())
			{
				double SupportMinimum = DBL_MAX;
				double SupportMaximum = -DBL_MAX;
				for (const FVector2D& Interval : ExistingSupportIntervals)
				{
					SupportMinimum = FMath::Min(SupportMinimum, Interval.X);
					SupportMaximum = FMath::Max(SupportMaximum, Interval.Y);
				}
				TransferStations.Add(FMath::Clamp(
					SupportMinimum - BlockUnitsCM * 0.5,
					MinimumUsableStation, MaximumUsableStation));
				TransferStations.Add(FMath::Clamp(
					SupportMaximum + BlockUnitsCM * 0.5,
					MinimumUsableStation, MaximumUsableStation));
			}
			TransferStations.Sort();
			for (int32 StationIndex = TransferStations.Num() - 1;
				StationIndex > 0; --StationIndex)
			{
				if (FMath::IsNearlyEqual(TransferStations[StationIndex],
					TransferStations[StationIndex - 1], GeometryToleranceCM))
				{
					TransferStations.RemoveAt(StationIndex);
				}
			}
			const double LowerTransferZCM = Group.GroundPlaneZCM
				+ (PreviousBase + 1.5) * BlockUnitsCM;
			for (const double StationXCM : TransferStations)
			{
				const int64 StationKey = FMath::RoundToInt64(
					StationXCM / GeometryToleranceCM);
				if (CandidateStationKeys.Contains(StationKey))
				{
					continue;
				}
				const double StationYCM = UpperBounds.GetCenter().Y;
				TArray<int32> IntermediateIndices;
				for (int32 IntermediateIndex = 0;
					IntermediateIndex < Plan.Members.Num(); ++IntermediateIndex)
				{
					if (RemovedMembers.Contains(IntermediateIndex))
					{
						continue;
					}
					const FPlannedMember& Intermediate = Plan.Members[IntermediateIndex];
					const bool bStructuralHorizontal =
						Intermediate.Axis != EABTSM73BeamAFrameAxis::Z
						&& (Intermediate.SkeletonKind == ESkeletonMemberKind::CoreCourse
							|| Intermediate.SkeletonKind == ESkeletonMemberKind::ThroughCourse
							|| Intermediate.SkeletonKind == ESkeletonMemberKind::FacadeCourse
							|| Intermediate.SkeletonKind == ESkeletonMemberKind::SharedCourse
							|| Intermediate.SkeletonKind == ESkeletonMemberKind::RoofCourse);
					if (!bStructuralHorizontal)
					{
						continue;
					}
					const FBox IntermediateBounds = PlannedMemberBounds(Intermediate);
					if (IntermediateBounds.Max.Z > TopZCM + GeometryToleranceCM
						|| TopZCM - IntermediateBounds.Max.Z
							< BlockUnitsCM - GeometryToleranceCM
						|| StationXCM - BlockUnitsCM * 0.5
							< IntermediateBounds.Min.X - GeometryToleranceCM
						|| StationXCM + BlockUnitsCM * 0.5
							> IntermediateBounds.Max.X + GeometryToleranceCM
						|| StationYCM - BlockUnitsCM * 0.5
							< IntermediateBounds.Min.Y - GeometryToleranceCM
						|| StationYCM + BlockUnitsCM * 0.5
							> IntermediateBounds.Max.Y + GeometryToleranceCM)
					{
						continue;
					}
					IntermediateIndices.Add(IntermediateIndex);
				}
				IntermediateIndices.Sort([&Plan](const int32 A, const int32 B)
				{
					const double ATop = PlannedMemberBounds(Plan.Members[A]).Max.Z;
					const double BTop = PlannedMemberBounds(Plan.Members[B]).Max.Z;
					return !FMath::IsNearlyEqual(ATop, BTop, GeometryToleranceCM)
						? ATop > BTop : A < B;
				});
				bool bUsedIntermediateBearing = false;
				for (const int32 IntermediateIndex : IntermediateIndices)
				{
					const FPlannedMember& Intermediate = Plan.Members[IntermediateIndex];
					const FBox IntermediateBounds = PlannedMemberBounds(Intermediate);
					TArray<FVector2D> IntermediateSupports;
					CollectSupportIntervals(IntermediateIndex, &RemovedMembers,
						IntermediateSupports);
					const int32 IntermediateAxisIndex =
						static_cast<int32>(Intermediate.Axis);
					double IntermediateRequiredMinimum = 0.0;
					double IntermediateRequiredMaximum = 0.0;
					CollectRequiredLoadRange(IntermediateIndex, &RemovedMembers,
						IntermediateRequiredMinimum, IntermediateRequiredMaximum);
					const double NewLoadStation = IntermediateAxisIndex == 0
						? StationXCM : StationYCM;
					IntermediateRequiredMinimum = FMath::Min(
						IntermediateRequiredMinimum, NewLoadStation);
					IntermediateRequiredMaximum = FMath::Max(
						IntermediateRequiredMaximum, NewLoadStation);
					if (!SupportHullSatisfies(Intermediate,
						MoveTemp(IntermediateSupports), false,
						IntermediateRequiredMinimum, IntermediateRequiredMaximum))
					{
						continue;
					}
					FPlannedMember IntermediatePostProbe;
					IntermediatePostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
					IntermediatePostProbe.LocalStart = Position(
						StationXCM, StationYCM, IntermediateBounds.Max.Z);
					IntermediatePostProbe.LocalEnd = Position(
						StationXCM, StationYCM, TopZCM);
					const FBox IntermediatePostBounds =
						PlannedMemberBounds(IntermediatePostProbe);
					if (!HasFullPlanarContact(IntermediateBounds, IntermediatePostBounds)
						|| !HasFullPlanarContact(IntermediatePostBounds, UpperBounds))
					{
						continue;
					}
					bool bIntermediatePostBlocked =
						Plan.ReservedSupportVoids.ContainsByPredicate(
							[&IntermediatePostBounds](const FABTSM73BeamASupportVoid& Void)
							{
								return BoxesPenetrate(IntermediatePostBounds, Void.Bounds);
							});
					for (int32 ExistingIndex = 0;
						ExistingIndex < Plan.Members.Num() && !bIntermediatePostBlocked;
						++ExistingIndex)
					{
						if (ExistingIndex != IntermediateIndex
							&& !RemovedMembers.Contains(ExistingIndex)
							&& BoxesPenetrate(IntermediatePostBounds,
								PlannedMemberBounds(Plan.Members[ExistingIndex])))
						{
							bIntermediatePostBlocked = true;
						}
					}
					if (bIntermediatePostBlocked)
					{
						continue;
					}
					CandidateStationKeys.Add(StationKey);
					FPostCandidate& Candidate = Candidates.AddDefaulted_GetRef();
					Candidate.StationXCM = StationXCM;
					Candidate.StationYCM = StationYCM;
					Candidate.BottomZCM = IntermediateBounds.Max.Z;
					Candidate.TopZCM = TopZCM;
					Candidate.FaceMask = Upper.FaceMask;
					Candidate.SupportInterval = FVector2D(
						FMath::Max(IntermediatePostBounds.Min[AxisIndex],
							UpperBounds.Min[AxisIndex]),
						FMath::Min(IntermediatePostBounds.Max[AxisIndex],
							UpperBounds.Max[AxisIndex]));
					Candidate.bRequiresLowerTransferRail = false;
					Candidate.bRequiresPost = true;
					bUsedIntermediateBearing = true;
					break;
				}
				if (bUsedIntermediateBearing)
				{
					continue;
				}
				TArray<int32> LowerXIndices;
				for (int32 LowerIndex = 0; LowerIndex < Plan.Members.Num(); ++LowerIndex)
				{
					if (RemovedMembers.Contains(LowerIndex))
					{
						continue;
					}
					const FPlannedMember& LowerX = Plan.Members[LowerIndex];
					const bool bCoreDerivedLower =
						LowerX.OwnerKind == EOwnerKind::BuildingGroupShell
						|| LowerX.OwnerKind == EOwnerKind::CoreCell
						|| LowerX.OwnerKind == EOwnerKind::SupportedSpan;
					if (!bCoreDerivedLower
						|| LowerX.Axis != EABTSM73BeamAFrameAxis::X
						|| LowerX.CourseIndex != PreviousBase)
					{
						continue;
					}
					const FBox LowerBounds = PlannedMemberBounds(LowerX);
					if (FMath::Abs(LowerBounds.Max.Z
						- (LowerTransferZCM - BlockUnitsCM * 0.5))
						> GeometryToleranceCM
						|| StationXCM - BlockUnitsCM * 0.5
							< LowerBounds.Min.X - GeometryToleranceCM
						|| StationXCM + BlockUnitsCM * 0.5
							> LowerBounds.Max.X + GeometryToleranceCM)
					{
						continue;
					}
					LowerXIndices.Add(LowerIndex);
				}
				LowerXIndices.Sort([&Plan, StationYCM](const int32 A, const int32 B)
				{
					const double ADistance = FMath::Abs(
						PlannedMemberBounds(Plan.Members[A]).GetCenter().Y - StationYCM);
					const double BDistance = FMath::Abs(
						PlannedMemberBounds(Plan.Members[B]).GetCenter().Y - StationYCM);
					return !FMath::IsNearlyEqual(ADistance, BDistance,
						GeometryToleranceCM) ? ADistance < BDistance : A < B;
				});
				FPlannedMember CapProbe;
				FBox CapBounds(ForceInit);
				bool bHasLowerXSeat = false;
				int32 LastCapBlockingMember = INDEX_NONE;
				int32 ExistingTransferMemberIndex = INDEX_NONE;
				FVector ExistingTransferStart = FVector::ZeroVector;
				FVector ExistingTransferEnd = FVector::ZeroVector;
				int32 ExtendedLowerCarrierMemberIndex = INDEX_NONE;
				FVector ExtendedLowerCarrierStart = FVector::ZeroVector;
				FVector ExtendedLowerCarrierEnd = FVector::ZeroVector;
				auto TryMergeExistingTransfer = [&](const int32 BlockingMemberIndex,
					const FPlannedMember& DesiredCap, const FBox& DesiredCapBounds)
				{
					if (!Plan.Members.IsValidIndex(BlockingMemberIndex)
						|| RemovedMembers.Contains(BlockingMemberIndex))
					{
						return false;
					}
					const FPlannedMember& ExistingTransfer =
						Plan.Members[BlockingMemberIndex];
					const FBox ExistingBounds = PlannedMemberBounds(ExistingTransfer);
					const bool bGroupShellTransfer =
						ExistingTransfer.OwnerKind == EOwnerKind::BuildingGroupShell
						&& ExistingTransfer.OwnerId == Group.GroupId
						&& (ExistingTransfer.SkeletonKind
								== ESkeletonMemberKind::ThroughCourse
							|| ExistingTransfer.SkeletonKind
								== ESkeletonMemberKind::FacadeCourse);
					const bool bCoreTransfer =
						ExistingTransfer.OwnerKind == EOwnerKind::CoreCell
						&& Group.CoreCellIds.Contains(ExistingTransfer.OwnerId)
						&& ExistingTransfer.SkeletonKind
							== ESkeletonMemberKind::CoreCourse;
					const bool bSharedTransfer =
						ExistingTransfer.OwnerKind == EOwnerKind::SupportedSpan
						&& ExistingTransfer.SkeletonKind
							== ESkeletonMemberKind::SharedCourse
						&& ExistingTransfer.EndpointCoreCellIds.ContainsByPredicate(
							[&Group](const int32 CoreCellId)
							{
								return Group.CoreCellIds.Contains(CoreCellId);
							});
					const bool bCompatibleTransfer =
						(bGroupShellTransfer || bCoreTransfer || bSharedTransfer)
						&& ExistingTransfer.Axis == EABTSM73BeamAFrameAxis::Y
						&& ExistingTransfer.CourseIndex == PreviousBase + 1
						&& FMath::Abs(ExistingBounds.GetCenter().X - StationXCM)
							<= GeometryToleranceCM
						&& FMath::Abs(ExistingBounds.GetCenter().Z
							- DesiredCapBounds.GetCenter().Z) <= GeometryToleranceCM;
					if (!bCompatibleTransfer)
					{
						return false;
					}

					FPlannedMember MergedTransfer = ExistingTransfer;
					const double MergedMinimumY = FMath::Min(
						ExistingBounds.Min.Y, DesiredCapBounds.Min.Y);
					const double MergedMaximumY = FMath::Max(
						ExistingBounds.Max.Y, DesiredCapBounds.Max.Y);
					const double MaximumMergedLengthCM =
						bCoreTransfer || bSharedTransfer
							? 720.0
							: MaximumHorizontalUnits * static_cast<double>(BlockUnitsCM);
					if (MergedMaximumY - MergedMinimumY
						> MaximumMergedLengthCM + GeometryToleranceCM)
					{
						return false;
					}
					if (MergedTransfer.LocalStart.Y <= MergedTransfer.LocalEnd.Y)
					{
						MergedTransfer.LocalStart.Y = MergedMinimumY;
						MergedTransfer.LocalEnd.Y = MergedMaximumY;
					}
					else
					{
						MergedTransfer.LocalEnd.Y = MergedMinimumY;
						MergedTransfer.LocalStart.Y = MergedMaximumY;
					}
					MergedTransfer.FaceMask |= DesiredCap.FaceMask;
					const FBox MergedBounds = PlannedMemberBounds(MergedTransfer);
					bool bMergedBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
						[&MergedBounds](const FABTSM73BeamASupportVoid& Void)
						{
							return BoxesPenetrate(MergedBounds, Void.Bounds);
						});
					for (int32 ExistingIndex = 0;
						ExistingIndex < Plan.Members.Num() && !bMergedBlocked;
						++ExistingIndex)
					{
						if (ExistingIndex != BlockingMemberIndex
							&& !RemovedMembers.Contains(ExistingIndex)
							&& BoxesPenetrate(MergedBounds,
								PlannedMemberBounds(Plan.Members[ExistingIndex])))
						{
							bMergedBlocked = true;
						}
					}
					if (bMergedBlocked)
					{
						return false;
					}

					TArray<FVector2D> MergedSupports;
					for (int32 LowerIndex = 0; LowerIndex < Plan.Members.Num(); ++LowerIndex)
					{
						if (LowerIndex == BlockingMemberIndex
							|| RemovedMembers.Contains(LowerIndex))
						{
							continue;
						}
						const FBox LowerBounds = PlannedMemberBounds(Plan.Members[LowerIndex]);
						if (FMath::Abs(LowerBounds.Max.Z - MergedBounds.Min.Z)
								> GeometryToleranceCM
							|| SkeletonV3OverlapLength(LowerBounds.Min.X,
								LowerBounds.Max.X, MergedBounds.Min.X, MergedBounds.Max.X)
								<= GeometryToleranceCM
							|| SkeletonV3OverlapLength(LowerBounds.Min.Y,
								LowerBounds.Max.Y, MergedBounds.Min.Y, MergedBounds.Max.Y)
								<= GeometryToleranceCM)
						{
							continue;
						}
						MergedSupports.Emplace(
							FMath::Max(LowerBounds.Min.Y, MergedBounds.Min.Y),
							FMath::Min(LowerBounds.Max.Y, MergedBounds.Max.Y));
					}
					double MergedRequiredMinimum = 0.0;
					double MergedRequiredMaximum = 0.0;
					CollectRequiredLoadRange(BlockingMemberIndex, &RemovedMembers,
						MergedRequiredMinimum, MergedRequiredMaximum);
					MergedRequiredMinimum = FMath::Min(MergedRequiredMinimum, StationYCM);
					MergedRequiredMaximum = FMath::Max(MergedRequiredMaximum, StationYCM);
					if (!SupportHullSatisfies(MergedTransfer, MoveTemp(MergedSupports),
						false, MergedRequiredMinimum, MergedRequiredMaximum))
					{
						return false;
					}

					CapProbe = MergedTransfer;
					CapBounds = MergedBounds;
					ExistingTransferMemberIndex = BlockingMemberIndex;
					ExistingTransferStart = MergedTransfer.LocalStart;
					ExistingTransferEnd = MergedTransfer.LocalEnd;
					return true;
				};

				// A reaction target may lie exactly one cell beyond an otherwise stable,
				// collinear carrier.  Extend that carrier by one complete 36 cm cell and
				// put the transverse cap on the new full-contact crossing.  This is a
				// bounded geometry operation, not a cantilevered post or a tolerance
				// relaxation; the lower carrier is processed later in the descending
				// sweep and is independently checked again by the final load audit.
				const double SnappedRequiredMinimum = FMath::GridSnap(
					RequiredMinimum, static_cast<double>(BlockUnitsCM));
				const double SnappedRequiredMaximum = FMath::GridSnap(
					RequiredMaximum, static_cast<double>(BlockUnitsCM));
				const bool bReactionTargetStation = ClosurePass == 1
					&& (FMath::Abs(StationXCM - SnappedRequiredMinimum)
							<= GeometryToleranceCM
						|| FMath::Abs(StationXCM - SnappedRequiredMaximum)
							<= GeometryToleranceCM);
				if (bReactionTargetStation)
				{
					for (int32 CarrierIndex = 0;
						CarrierIndex < Plan.Members.Num() && !bHasLowerXSeat;
						++CarrierIndex)
					{
						if (RemovedMembers.Contains(CarrierIndex))
						{
							continue;
						}
						const FPlannedMember& Carrier = Plan.Members[CarrierIndex];
						const bool bCompatibleOwner =
							(Carrier.OwnerKind == EOwnerKind::BuildingGroupShell
								&& Carrier.OwnerId == Group.GroupId)
							|| (Carrier.OwnerKind == EOwnerKind::CoreCell
								&& Group.CoreCellIds.Contains(Carrier.OwnerId))
							|| (Carrier.OwnerKind == EOwnerKind::SupportedSpan
								&& Carrier.EndpointCoreCellIds.ContainsByPredicate(
									[&Group](const int32 CoreCellId)
									{
										return Group.CoreCellIds.Contains(CoreCellId);
									}));
						if (!bCompatibleOwner
							|| Carrier.Axis != EABTSM73BeamAFrameAxis::X
							|| Carrier.CourseIndex != PreviousBase)
						{
							continue;
						}
						const FBox CarrierBounds = PlannedMemberBounds(Carrier);
						if (FMath::Abs(CarrierBounds.GetCenter().Y - StationYCM)
							> GeometryToleranceCM)
						{
							continue;
						}
						const double DesiredMinimumX = StationXCM
							- BlockUnitsCM * 0.5;
						const double DesiredMaximumX = StationXCM
							+ BlockUnitsCM * 0.5;
						const double ExtendedMinimumX = FMath::Min(
							CarrierBounds.Min.X, DesiredMinimumX);
						const double ExtendedMaximumX = FMath::Max(
							CarrierBounds.Max.X, DesiredMaximumX);
						const double AddedReachCM = FMath::Max(
							CarrierBounds.Min.X - ExtendedMinimumX,
							ExtendedMaximumX - CarrierBounds.Max.X);
						const double MaximumCarrierLengthCM =
							Carrier.SkeletonKind == ESkeletonMemberKind::CoreCourse
								|| Carrier.SkeletonKind == ESkeletonMemberKind::SharedCourse
								? 720.0
								: MaximumHorizontalUnits
									* static_cast<double>(BlockUnitsCM);
						if (AddedReachCM <= GeometryToleranceCM
							|| AddedReachCM > BlockUnitsCM + GeometryToleranceCM
							|| ExtendedMaximumX - ExtendedMinimumX
								> MaximumCarrierLengthCM + GeometryToleranceCM)
						{
							continue;
						}
						FPlannedMember ExtendedCarrier = Carrier;
						if (ExtendedCarrier.LocalStart.X
							<= ExtendedCarrier.LocalEnd.X)
						{
							ExtendedCarrier.LocalStart.X = ExtendedMinimumX;
							ExtendedCarrier.LocalEnd.X = ExtendedMaximumX;
						}
						else
						{
							ExtendedCarrier.LocalEnd.X = ExtendedMinimumX;
							ExtendedCarrier.LocalStart.X = ExtendedMaximumX;
						}
						const FBox ExtendedBounds = PlannedMemberBounds(ExtendedCarrier);
						const bool bExtensionVoidBlocked =
							Plan.ReservedSupportVoids.ContainsByPredicate(
								[&ExtendedBounds](const FABTSM73BeamASupportVoid& Void)
								{
									return BoxesPenetrate(ExtendedBounds, Void.Bounds);
								});
						bool bExtensionBlocked = bExtensionVoidBlocked;
						int32 ExtensionBlockingMember = INDEX_NONE;
						for (int32 ExistingIndex = 0;
							ExistingIndex < Plan.Members.Num() && !bExtensionBlocked;
							++ExistingIndex)
						{
							if (ExistingIndex != CarrierIndex
								&& !RemovedMembers.Contains(ExistingIndex)
								&& BoxesPenetrate(ExtendedBounds,
									PlannedMemberBounds(Plan.Members[ExistingIndex])))
							{
								bExtensionBlocked = true;
								ExtensionBlockingMember = ExistingIndex;
							}
						}
						TArray<FVector2D> CarrierSupports;
						CollectSupportIntervals(CarrierIndex, &RemovedMembers,
							CarrierSupports);
						double CarrierRequired = CarrierBounds.GetCenter().X;
						if (ClosureLoadResultantValid.IsValidIndex(CarrierIndex)
							&& ClosureLoadResultantValid[CarrierIndex])
						{
							CarrierRequired = ClosureLoadResultants[CarrierIndex].X;
						}
						const bool bCarrierSupportValid = SupportHullSatisfies(
							ExtendedCarrier, CarrierSupports, false,
							CarrierRequired, CarrierRequired);
						CandidateDiagnostics += FString::Printf(
							TEXT("|X%.1f:CarrierExtend=M%d:Q%d:B%.1f..%.1f=>%.1f..%.1f:Add=%.1f:Blocked=%d:Void=%d:Blocker=%d:Supported=%d:R=%.1f"),
							StationXCM, CarrierIndex, Carrier.CourseIndex,
							CarrierBounds.Min.X, CarrierBounds.Max.X,
							ExtendedBounds.Min.X, ExtendedBounds.Max.X,
							AddedReachCM, bExtensionBlocked ? 1 : 0,
							bExtensionVoidBlocked ? 1 : 0, ExtensionBlockingMember,
							bCarrierSupportValid ? 1 : 0, CarrierRequired);
						if (bExtensionBlocked || !bCarrierSupportValid)
						{
							continue;
						}

						FPlannedMember DirectCap;
						DirectCap.Axis = EABTSM73BeamAFrameAxis::Y;
						DirectCap.FaceMask = Upper.FaceMask;
						DirectCap.LocalStart = Position(StationXCM,
							StationYCM - BlockUnitsCM * 0.5, LowerTransferZCM);
						DirectCap.LocalEnd = Position(StationXCM,
							StationYCM + BlockUnitsCM * 0.5, LowerTransferZCM);
						const FBox DirectCapBounds = PlannedMemberBounds(DirectCap);
						if (!HasFullPlanarContact(ExtendedBounds, DirectCapBounds))
						{
							continue;
						}
						bool bCapBlocked =
							Plan.ReservedSupportVoids.ContainsByPredicate(
								[&DirectCapBounds](const FABTSM73BeamASupportVoid& Void)
								{
									return BoxesPenetrate(DirectCapBounds, Void.Bounds);
								});
						int32 CapBlocker = INDEX_NONE;
						for (int32 ExistingIndex = 0;
							ExistingIndex < Plan.Members.Num() && !bCapBlocked;
							++ExistingIndex)
						{
							if (!RemovedMembers.Contains(ExistingIndex)
								&& BoxesPenetrate(DirectCapBounds,
									PlannedMemberBounds(Plan.Members[ExistingIndex])))
							{
								bCapBlocked = true;
								CapBlocker = ExistingIndex;
							}
						}
						if (bCapBlocked
							&& !TryMergeExistingTransfer(
								CapBlocker, DirectCap, DirectCapBounds))
						{
							continue;
						}
						if (!bCapBlocked)
						{
							CapProbe = DirectCap;
							CapBounds = DirectCapBounds;
						}
						bHasLowerXSeat = true;
						ExtendedLowerCarrierMemberIndex = CarrierIndex;
						ExtendedLowerCarrierStart = ExtendedCarrier.LocalStart;
						ExtendedLowerCarrierEnd = ExtendedCarrier.LocalEnd;
					}
				}
				for (int32 LowerA = 0;
					LowerA < LowerXIndices.Num() && !bHasLowerXSeat; ++LowerA)
				{
					const FBox LowerABounds = PlannedMemberBounds(
						Plan.Members[LowerXIndices[LowerA]]);
					const double LowerAYCM = LowerABounds.GetCenter().Y;
					for (int32 LowerB = LowerA + 1;
						LowerB < LowerXIndices.Num(); ++LowerB)
					{
						const FBox LowerBBounds = PlannedMemberBounds(
							Plan.Members[LowerXIndices[LowerB]]);
						const double LowerBYCM = LowerBBounds.GetCenter().Y;
						if (FMath::Abs(LowerAYCM - LowerBYCM)
							< BlockUnitsCM - GeometryToleranceCM
							|| StationYCM < FMath::Min(LowerAYCM, LowerBYCM)
								- GeometryToleranceCM
							|| StationYCM > FMath::Max(LowerAYCM, LowerBYCM)
								+ GeometryToleranceCM)
						{
							continue;
						}
						FPlannedMember PairCapProbe;
						PairCapProbe.Axis = EABTSM73BeamAFrameAxis::Y;
						PairCapProbe.LocalStart = Position(StationXCM,
							FMath::Min3(StationYCM, LowerAYCM, LowerBYCM)
								- BlockUnitsCM * 0.5,
							LowerTransferZCM);
						PairCapProbe.LocalEnd = Position(StationXCM,
							FMath::Max3(StationYCM, LowerAYCM, LowerBYCM)
								+ BlockUnitsCM * 0.5,
							LowerTransferZCM);
						const FBox PairCapBounds = PlannedMemberBounds(PairCapProbe);
						if (PairCapBounds.Max.Y - PairCapBounds.Min.Y
								> MaximumHorizontalUnits * BlockUnitsCM
								+ GeometryToleranceCM
							|| !HasFullPlanarContact(LowerABounds, PairCapBounds)
							|| !HasFullPlanarContact(LowerBBounds, PairCapBounds))
						{
							continue;
						}
						TArray<FVector2D> PairSupportIntervals = {
							FVector2D(
								FMath::Max(LowerABounds.Min.Y, PairCapBounds.Min.Y),
								FMath::Min(LowerABounds.Max.Y, PairCapBounds.Max.Y)),
							FVector2D(
								FMath::Max(LowerBBounds.Min.Y, PairCapBounds.Min.Y),
								FMath::Min(LowerBBounds.Max.Y, PairCapBounds.Max.Y))};
						if (!SupportHullSatisfies(PairCapProbe,
							MoveTemp(PairSupportIntervals), false,
							StationYCM, StationYCM))
						{
							continue;
						}
						bool bPairCapBlocked =
							Plan.ReservedSupportVoids.ContainsByPredicate(
								[&PairCapBounds](const FABTSM73BeamASupportVoid& Void)
								{
									return BoxesPenetrate(PairCapBounds, Void.Bounds);
								});
						for (int32 ExistingIndex = 0;
							ExistingIndex < Plan.Members.Num() && !bPairCapBlocked;
							++ExistingIndex)
						{
							if (!RemovedMembers.Contains(ExistingIndex)
								&& BoxesPenetrate(PairCapBounds,
									PlannedMemberBounds(Plan.Members[ExistingIndex])))
							{
								bPairCapBlocked = true;
								LastCapBlockingMember = ExistingIndex;
							}
						}
						if (!bPairCapBlocked)
						{
							CapProbe = PairCapProbe;
							CapBounds = PairCapBounds;
							bHasLowerXSeat = true;
							break;
						}
						else if (TryMergeExistingTransfer(LastCapBlockingMember,
							PairCapProbe, PairCapBounds))
						{
							bHasLowerXSeat = true;
							break;
						}
					}
				}
				for (const int32 LowerIndex : LowerXIndices)
				{
					if (bHasLowerXSeat)
					{
						break;
					}
					const FBox LowerBounds = PlannedMemberBounds(Plan.Members[LowerIndex]);
					const double LowerStationYCM = LowerBounds.GetCenter().Y;
					double CapMinimumYCM = FMath::Min(StationYCM, LowerStationYCM)
						- BlockUnitsCM * 0.5;
					double CapMaximumYCM = FMath::Max(StationYCM, LowerStationYCM)
						+ BlockUnitsCM * 0.5;
					if (LowerStationYCM < StationYCM - GeometryToleranceCM)
					{
						CapMinimumYCM -= BlockUnitsCM * 0.5;
					}
					else if (LowerStationYCM > StationYCM + GeometryToleranceCM)
					{
						CapMaximumYCM += BlockUnitsCM * 0.5;
					}
					CapProbe = FPlannedMember();
					CapProbe.Axis = EABTSM73BeamAFrameAxis::Y;
					CapProbe.LocalStart = Position(StationXCM, CapMinimumYCM,
						LowerTransferZCM);
					CapProbe.LocalEnd = Position(StationXCM, CapMaximumYCM,
						LowerTransferZCM);
					CapBounds = PlannedMemberBounds(CapProbe);
					if (!HasFullPlanarContact(LowerBounds, CapBounds))
					{
						continue;
					}
					TArray<FVector2D> CapSupportIntervals = {
						FVector2D(
							FMath::Max(LowerBounds.Min.Y, CapBounds.Min.Y),
							FMath::Min(LowerBounds.Max.Y, CapBounds.Max.Y))};
					const double CapCentreYCM = CapBounds.GetCenter().Y;
					if (!SupportHullSatisfies(CapProbe,
						MoveTemp(CapSupportIntervals), false,
						CapCentreYCM, CapCentreYCM))
					{
						continue;
					}
					bool bCapBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
						[&CapBounds](const FABTSM73BeamASupportVoid& Void)
						{
							return BoxesPenetrate(CapBounds, Void.Bounds);
						});
					for (int32 ExistingIndex = 0;
						ExistingIndex < Plan.Members.Num() && !bCapBlocked;
						++ExistingIndex)
					{
						if (!RemovedMembers.Contains(ExistingIndex)
							&& BoxesPenetrate(CapBounds,
								PlannedMemberBounds(Plan.Members[ExistingIndex])))
						{
							bCapBlocked = true;
							LastCapBlockingMember = ExistingIndex;
						}
					}
					if (!bCapBlocked)
					{
						bHasLowerXSeat = true;
						break;
					}
					if (TryMergeExistingTransfer(LastCapBlockingMember,
						CapProbe, CapBounds))
					{
						bHasLowerXSeat = true;
						break;
					}
				}
				if (!bHasLowerXSeat)
				{
					FString AlignedLowerDiagnostics;
					for (int32 AlignedIndex = 0;
						AlignedIndex < Plan.Members.Num(); ++AlignedIndex)
					{
						const FPlannedMember& Aligned = Plan.Members[AlignedIndex];
						if (Aligned.Axis != EABTSM73BeamAFrameAxis::X
							|| Aligned.CourseIndex != PreviousBase)
						{
							continue;
						}
						const FBox AlignedBounds = PlannedMemberBounds(Aligned);
						if (FMath::Abs(AlignedBounds.GetCenter().Y - StationYCM)
								> GeometryToleranceCM
							|| StationXCM - BlockUnitsCM * 0.5
								< AlignedBounds.Min.X - GeometryToleranceCM
							|| StationXCM + BlockUnitsCM * 0.5
								> AlignedBounds.Max.X + GeometryToleranceCM)
						{
							continue;
						}
						AlignedLowerDiagnostics += FString::Printf(
							TEXT("|M%d:%s:O%d:K%d:B%.1f..%.1f"), AlignedIndex,
							RemovedMembers.Contains(AlignedIndex) ? TEXT("Removed")
								: TEXT("Live"), static_cast<int32>(Aligned.OwnerKind),
							static_cast<int32>(Aligned.SkeletonKind),
							AlignedBounds.Min.X, AlignedBounds.Max.X);
					}
					if (Plan.Members.IsValidIndex(LastCapBlockingMember))
					{
						const FPlannedMember& Blocking =
							Plan.Members[LastCapBlockingMember];
						const FBox BlockingBounds = PlannedMemberBounds(Blocking);
						CandidateDiagnostics += FString::Printf(
							TEXT("|X%.1f:LowerX=%d:Aligned=%s:CapBlocker=M%d:O%d:K%d:A%d:Q%d:B%.1f,%.1f,%.1f..%.1f,%.1f,%.1f"),
							StationXCM, LowerXIndices.Num(), *AlignedLowerDiagnostics,
							LastCapBlockingMember,
							static_cast<int32>(Blocking.OwnerKind),
							static_cast<int32>(Blocking.SkeletonKind),
							static_cast<int32>(Blocking.Axis), Blocking.CourseIndex,
							BlockingBounds.Min.X, BlockingBounds.Min.Y, BlockingBounds.Min.Z,
							BlockingBounds.Max.X, BlockingBounds.Max.Y, BlockingBounds.Max.Z);
					}
					else
					{
						CandidateDiagnostics += FString::Printf(
							TEXT("|X%.1f:LowerX=%d:Aligned=%s:CapBlocker=%d"),
							StationXCM, LowerXIndices.Num(), *AlignedLowerDiagnostics,
							LastCapBlockingMember);
					}
					continue;
				}
				const double PostLengthCM = TopZCM - BottomZCM;
				const bool bDirectBearing = FMath::Abs(PostLengthCM)
					<= GeometryToleranceCM;
				if (PostLengthCM < -GeometryToleranceCM
					|| (!bDirectBearing
						&& PostLengthCM < BlockUnitsCM - GeometryToleranceCM))
				{
					CandidateDiagnostics += FString::Printf(
						TEXT("|X%.1f:PostLength=%.1f"), StationXCM, PostLengthCM);
					continue;
				}
				FBox SupportBounds = CapBounds;
				if (!bDirectBearing)
				{
					FPlannedMember PostProbe;
					PostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
					PostProbe.LocalStart = Position(StationXCM, StationYCM, BottomZCM);
					PostProbe.LocalEnd = Position(StationXCM, StationYCM, TopZCM);
					SupportBounds = PlannedMemberBounds(PostProbe);
					if (!HasFullPlanarContact(CapBounds, SupportBounds)
						|| !HasFullPlanarContact(SupportBounds, UpperBounds))
					{
						CandidateDiagnostics += FString::Printf(
							TEXT("|X%.1f:PostContact"), StationXCM);
						continue;
					}
					bool bPostBlocked = Plan.ReservedSupportVoids.ContainsByPredicate(
						[&SupportBounds](const FABTSM73BeamASupportVoid& Void)
						{
							return BoxesPenetrate(SupportBounds, Void.Bounds);
						});
					int32 PostBlockingMember = INDEX_NONE;
					for (int32 ExistingIndex = 0;
						ExistingIndex < Plan.Members.Num() && !bPostBlocked;
						++ExistingIndex)
					{
						if (!RemovedMembers.Contains(ExistingIndex)
							&& BoxesPenetrate(SupportBounds,
								PlannedMemberBounds(Plan.Members[ExistingIndex])))
						{
							bPostBlocked = true;
							PostBlockingMember = ExistingIndex;
						}
					}
					if (bPostBlocked)
					{
						bool bRecoveredByIntermediateBearing = false;
						if (Plan.Members.IsValidIndex(PostBlockingMember))
						{
							const FPlannedMember& Intermediate =
								Plan.Members[PostBlockingMember];
							const FBox IntermediateBounds = PlannedMemberBounds(Intermediate);
							const bool bStructuralHorizontal =
								Intermediate.Axis != EABTSM73BeamAFrameAxis::Z
								&& (Intermediate.SkeletonKind == ESkeletonMemberKind::CoreCourse
									|| Intermediate.SkeletonKind == ESkeletonMemberKind::ThroughCourse
									|| Intermediate.SkeletonKind == ESkeletonMemberKind::FacadeCourse
									|| Intermediate.SkeletonKind == ESkeletonMemberKind::SharedCourse
									|| Intermediate.SkeletonKind == ESkeletonMemberKind::RoofCourse);
							TArray<FVector2D> IntermediateSupports;
							CollectSupportIntervals(PostBlockingMember, &RemovedMembers,
								IntermediateSupports);
							const int32 IntermediateAxisIndex =
								static_cast<int32>(Intermediate.Axis);
							double IntermediateRequiredMinimum = 0.0;
							double IntermediateRequiredMaximum = 0.0;
							CollectRequiredLoadRange(PostBlockingMember, &RemovedMembers,
								IntermediateRequiredMinimum, IntermediateRequiredMaximum);
							const double NewLoadStation = IntermediateAxisIndex == 0
								? StationXCM : StationYCM;
							IntermediateRequiredMinimum = FMath::Min(
								IntermediateRequiredMinimum, NewLoadStation);
							IntermediateRequiredMaximum = FMath::Max(
								IntermediateRequiredMaximum, NewLoadStation);
							const double RecoveredBottomZCM = IntermediateBounds.Max.Z;
							const double RecoveredPostLengthCM = TopZCM - RecoveredBottomZCM;
							FPlannedMember RecoveredPostProbe;
							RecoveredPostProbe.Axis = EABTSM73BeamAFrameAxis::Z;
							RecoveredPostProbe.LocalStart = Position(
								StationXCM, StationYCM, RecoveredBottomZCM);
							RecoveredPostProbe.LocalEnd = Position(
								StationXCM, StationYCM, TopZCM);
							const FBox RecoveredPostBounds =
								PlannedMemberBounds(RecoveredPostProbe);
							bool bRecoveredPostBlocked =
								Plan.ReservedSupportVoids.ContainsByPredicate(
									[&RecoveredPostBounds](const FABTSM73BeamASupportVoid& Void)
									{
										return BoxesPenetrate(RecoveredPostBounds, Void.Bounds);
									});
							for (int32 ExistingIndex = 0;
								ExistingIndex < Plan.Members.Num() && !bRecoveredPostBlocked;
								++ExistingIndex)
							{
								if (ExistingIndex != PostBlockingMember
									&& !RemovedMembers.Contains(ExistingIndex)
									&& BoxesPenetrate(RecoveredPostBounds,
										PlannedMemberBounds(Plan.Members[ExistingIndex])))
								{
									bRecoveredPostBlocked = true;
								}
							}
							if (bStructuralHorizontal
								&& RecoveredPostLengthCM
									>= BlockUnitsCM - GeometryToleranceCM
								&& HasFullPlanarContact(IntermediateBounds,
									RecoveredPostBounds)
								&& HasFullPlanarContact(RecoveredPostBounds, UpperBounds)
								&& SupportHullSatisfies(Intermediate,
									MoveTemp(IntermediateSupports), false,
									IntermediateRequiredMinimum,
									IntermediateRequiredMaximum)
								&& !bRecoveredPostBlocked)
							{
								CandidateStationKeys.Add(StationKey);
								FPostCandidate& Candidate = Candidates.AddDefaulted_GetRef();
								Candidate.StationXCM = StationXCM;
								Candidate.StationYCM = StationYCM;
								Candidate.BottomZCM = RecoveredBottomZCM;
								Candidate.TopZCM = TopZCM;
								Candidate.FaceMask = Upper.FaceMask;
								Candidate.SupportInterval = FVector2D(
									FMath::Max(RecoveredPostBounds.Min[AxisIndex],
										UpperBounds.Min[AxisIndex]),
									FMath::Min(RecoveredPostBounds.Max[AxisIndex],
										UpperBounds.Max[AxisIndex]));
								Candidate.bRequiresLowerTransferRail = false;
								Candidate.bRequiresPost = true;
								bRecoveredByIntermediateBearing = true;
							}
						}
						if (bRecoveredByIntermediateBearing)
						{
							continue;
						}
						if (Plan.Members.IsValidIndex(PostBlockingMember))
						{
							const FPlannedMember& Blocking = Plan.Members[PostBlockingMember];
							const FBox BlockingBounds = PlannedMemberBounds(Blocking);
							CandidateDiagnostics += FString::Printf(
								TEXT("|X%.1f:PostBlocker=M%d:O%d:K%d:A%d:Q%d:B%.1f,%.1f,%.1f..%.1f,%.1f,%.1f"),
								StationXCM, PostBlockingMember,
								static_cast<int32>(Blocking.OwnerKind),
								static_cast<int32>(Blocking.SkeletonKind),
								static_cast<int32>(Blocking.Axis), Blocking.CourseIndex,
								BlockingBounds.Min.X, BlockingBounds.Min.Y, BlockingBounds.Min.Z,
								BlockingBounds.Max.X, BlockingBounds.Max.Y, BlockingBounds.Max.Z);
						}
						else
						{
							CandidateDiagnostics += FString::Printf(
								TEXT("|X%.1f:PostBlocker=%d"),
								StationXCM, PostBlockingMember);
						}
						continue;
					}
				}
				else if (!HasFullPlanarContact(CapBounds, UpperBounds))
				{
					CandidateDiagnostics += FString::Printf(
						TEXT("|X%.1f:DirectContact"), StationXCM);
					continue;
				}
				CandidateStationKeys.Add(StationKey);
				FPostCandidate& Candidate = Candidates.AddDefaulted_GetRef();
				Candidate.StationXCM = StationXCM;
				Candidate.StationYCM = StationYCM;
				Candidate.BottomZCM = BottomZCM;
				Candidate.TopZCM = TopZCM;
				Candidate.FaceMask = Upper.FaceMask;
				if (FMath::Abs(StationXCM - Group.LocalBounds.Min.X)
					<= GeometryToleranceCM)
				{
					Candidate.FaceMask |= ABTSM73BeamC3V3::NegativeX;
				}
				if (FMath::Abs(StationXCM - Group.LocalBounds.Max.X)
					<= GeometryToleranceCM)
				{
					Candidate.FaceMask |= ABTSM73BeamC3V3::PositiveX;
				}
				Candidate.SupportInterval = FVector2D(
					FMath::Max(SupportBounds.Min.X, UpperBounds.Min.X),
					FMath::Min(SupportBounds.Max.X, UpperBounds.Max.X));
				Candidate.bRequiresLowerTransferRail =
					ExistingTransferMemberIndex == INDEX_NONE;
				Candidate.bRequiresPost = !bDirectBearing;
				Candidate.LowerTransferCourse = PreviousBase + 1;
				Candidate.LowerTransferStart = CapProbe.LocalStart;
				Candidate.LowerTransferEnd = CapProbe.LocalEnd;
				Candidate.ExistingLowerTransferMemberIndex =
					ExistingTransferMemberIndex;
				Candidate.ExistingLowerTransferStart = ExistingTransferStart;
				Candidate.ExistingLowerTransferEnd = ExistingTransferEnd;
				Candidate.ExtendedLowerCarrierMemberIndex =
					ExtendedLowerCarrierMemberIndex;
				Candidate.ExtendedLowerCarrierStart = ExtendedLowerCarrierStart;
				Candidate.ExtendedLowerCarrierEnd = ExtendedLowerCarrierEnd;
			}
			Candidates.Sort([](const FPostCandidate& A, const FPostCandidate& B)
			{
				return A.StationXCM < B.StationXCM;
			});

			TArray<int32> CandidateOrder;
			for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
			{
				CandidateOrder.Add(CandidateIndex);
			}
			double ExistingMinimum = DBL_MAX;
			double ExistingMaximum = -DBL_MAX;
			for (const FVector2D& Interval : ExistingSupportIntervals)
			{
				ExistingMinimum = FMath::Min(ExistingMinimum, Interval.X);
				ExistingMaximum = FMath::Max(ExistingMaximum, Interval.Y);
			}
			CandidateOrder.Sort([&Candidates, ExistingMinimum, ExistingMaximum](
				const int32 A, const int32 B)
			{
				const double ASpan = FMath::Max(ExistingMaximum,
					Candidates[A].SupportInterval.Y)
					- FMath::Min(ExistingMinimum, Candidates[A].SupportInterval.X);
				const double BSpan = FMath::Max(ExistingMaximum,
					Candidates[B].SupportInterval.Y)
					- FMath::Min(ExistingMinimum, Candidates[B].SupportInterval.X);
				return !FMath::IsNearlyEqual(ASpan, BSpan, GeometryToleranceCM)
					? ASpan > BSpan
					: Candidates[A].StationXCM < Candidates[B].StationXCM;
			});
			TArray<int32> SelectedCandidates;
			for (const int32 CandidateIndex : CandidateOrder)
			{
				TArray<FVector2D> WithCandidate = ExistingSupportIntervals;
				WithCandidate.Add(Candidates[CandidateIndex].SupportInterval);
				if (SupportHullSatisfies(Upper, MoveTemp(WithCandidate), false,
					RequiredMinimum, RequiredMaximum))
				{
					SelectedCandidates.Add(CandidateIndex);
					break;
				}
			}
			if (ClosurePass == 0 && SelectedCandidates.IsEmpty())
			{
				const double SelfCentre = UpperBounds.GetCenter()[AxisIndex];
				for (const int32 CandidateIndex : CandidateOrder)
				{
					TArray<FVector2D> WithCandidate = ExistingSupportIntervals;
					WithCandidate.Add(Candidates[CandidateIndex].SupportInterval);
					if (SupportHullSatisfies(Upper, MoveTemp(WithCandidate), false,
						SelfCentre, SelfCentre))
					{
						SelectedCandidates.Add(CandidateIndex);
						break;
					}
				}
			}
			if (SelectedCandidates.IsEmpty() && !Candidates.IsEmpty())
			{
				SelectedCandidates.Add(0);
				if (Candidates.Num() > 1
					&& FMath::Abs(Candidates.Last().StationXCM
						- Candidates[0].StationXCM)
						>= BlockUnitsCM - GeometryToleranceCM)
				{
					SelectedCandidates.Add(Candidates.Num() - 1);
				}
				TArray<FVector2D> WithExtremes = ExistingSupportIntervals;
				for (const int32 CandidateIndex : SelectedCandidates)
				{
					WithExtremes.Add(Candidates[CandidateIndex].SupportInterval);
				}
				if (!SupportHullSatisfies(Upper, MoveTemp(WithExtremes), false,
					RequiredMinimum, RequiredMaximum))
				{
					if (ClosurePass == 1)
					{
						SelectedCandidates.Reset();
					}
					else
					{
						TArray<FVector2D> SelfLoadFallback = ExistingSupportIntervals;
						for (const int32 CandidateIndex : SelectedCandidates)
						{
							SelfLoadFallback.Add(
								Candidates[CandidateIndex].SupportInterval);
						}
						const double SelfCentre = UpperBounds.GetCenter()[AxisIndex];
						if (!SupportHullSatisfies(Upper,
							MoveTemp(SelfLoadFallback), false,
							SelfCentre, SelfCentre))
						{
							SelectedCandidates.Reset();
						}
					}
				}
			}

			if (SelectedCandidates.IsEmpty())
			{
				const double SelfCentre = UpperBounds.GetCenter()[AxisIndex];
				if (ClosurePass == 0
					&& SupportHullSatisfies(Upper, ExistingSupportIntervals, false,
						SelfCentre, SelfCentre))
				{
					// Direct upper-contact witnesses are a conservative placement hint,
					// not a substitute for Beam-C's reaction-weighted resultant.  When
					// occupied geometry leaves no legal extra seat, retain a genuinely
					// self-supported rail and let the final read-only Beam-C audit decide.
					continue;
				}
				if (ClosurePass == 0
					&& Upper.OwnerKind == EOwnerKind::BuildingGroupShell
					&& Upper.SkeletonKind == ESkeletonMemberKind::ThroughCourse
					&& Upper.FaceMask == 0)
				{
					RemovedMembers.Add(UpperIndex);
					continue;
				}
				OutError = FString::Printf(
					TEXT("BeamC3V3CommonFrameSeparatedSeatUnavailable:Member=%d:Course=%d:Face=%u:Length=%.1f:Required=%.1f..%.1f:Existing=%s:Candidates=%s:Rejected=%s"),
					UpperIndex, Upper.CourseIndex, Upper.FaceMask, UpperLengthCM,
					RequiredMinimum, RequiredMaximum,
					*([&ExistingSupportIntervals]()
					{
						FString Text;
						for (const FVector2D& Interval : ExistingSupportIntervals)
						{
							Text += FString::Printf(TEXT("|%.1f..%.1f"),
								Interval.X, Interval.Y);
						}
						return Text;
					})(),
					*([&Candidates]()
					{
						FString Text;
						for (const FPostCandidate& Candidate : Candidates)
						{
							Text += FString::Printf(TEXT("|X%.1f=%.1f..%.1f"),
								Candidate.StationXCM, Candidate.SupportInterval.X,
								Candidate.SupportInterval.Y);
						}
						return Text;
					})(), *CandidateDiagnostics);
				return false;
			}

			for (const int32 CandidateIndex : SelectedCandidates)
			{
				const FPostCandidate& Candidate = Candidates[CandidateIndex];
				const int32 OriginCore = Plan.CoreCells.IsValidIndex(
					Upper.OriginCoreCellId) ? Upper.OriginCoreCellId
					: (!Group.CoreCellIds.IsEmpty() ? Group.CoreCellIds[0] : INDEX_NONE);
				if (Plan.Members.IsValidIndex(
					Candidate.ExtendedLowerCarrierMemberIndex))
				{
					FPlannedMember& Carrier = Plan.Members[
						Candidate.ExtendedLowerCarrierMemberIndex];
					const FBox ExistingBounds = PlannedMemberBounds(Carrier);
					FPlannedMember CandidateCarrier = Carrier;
					CandidateCarrier.LocalStart = Candidate.ExtendedLowerCarrierStart;
					CandidateCarrier.LocalEnd = Candidate.ExtendedLowerCarrierEnd;
					const FBox CandidateBounds = PlannedMemberBounds(CandidateCarrier);
					const double MergedMinimumX = FMath::Min(
						ExistingBounds.Min.X, CandidateBounds.Min.X);
					const double MergedMaximumX = FMath::Max(
						ExistingBounds.Max.X, CandidateBounds.Max.X);
					if (Carrier.LocalStart.X <= Carrier.LocalEnd.X)
					{
						Carrier.LocalStart.X = MergedMinimumX;
						Carrier.LocalEnd.X = MergedMaximumX;
					}
					else
					{
						Carrier.LocalEnd.X = MergedMinimumX;
						Carrier.LocalStart.X = MergedMaximumX;
					}
					Group.MemberIndices.AddUnique(
						Candidate.ExtendedLowerCarrierMemberIndex);
					Plan.Summary.MaximumMemberLengthCM = FMath::Max(
						Plan.Summary.MaximumMemberLengthCM,
						static_cast<float>(MergedMaximumX - MergedMinimumX));
				}
				if (Candidate.bRequiresLowerTransferRail)
				{
					const int32 TransferIndex = Plan.Members.Num();
					if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
						Candidate.FaceMask != 0 ? ESkeletonMemberKind::FacadeCourse
							: ESkeletonMemberKind::ThroughCourse,
						Group.GroupId, INDEX_NONE, INDEX_NONE, OriginCore,
						Candidate.LowerTransferCourse,
						FMath::RoundToInt(Candidate.StationXCM / BlockUnitsCM),
						FMath::RoundToInt(Candidate.StationYCM / BlockUnitsCM),
						Candidate.FaceMask, EABTSM73BeamAFrameAxis::Y,
						EABTSM73BeamAMemberRole::SecondaryBeam,
						Candidate.LowerTransferStart, Candidate.LowerTransferEnd,
						OutError))
					{
						return false;
					}
					Group.MemberIndices.Add(TransferIndex);
				}
				else if (Plan.Members.IsValidIndex(
					Candidate.ExistingLowerTransferMemberIndex))
				{
					FPlannedMember& ExistingTransfer = Plan.Members[
						Candidate.ExistingLowerTransferMemberIndex];
					const FBox ExistingBounds = PlannedMemberBounds(ExistingTransfer);
					FPlannedMember CandidateTransfer = ExistingTransfer;
					CandidateTransfer.LocalStart =
						Candidate.ExistingLowerTransferStart;
					CandidateTransfer.LocalEnd =
						Candidate.ExistingLowerTransferEnd;
					const FBox CandidateBounds = PlannedMemberBounds(CandidateTransfer);
					const double MergedMinimumY = FMath::Min(
						ExistingBounds.Min.Y, CandidateBounds.Min.Y);
					const double MergedMaximumY = FMath::Max(
						ExistingBounds.Max.Y, CandidateBounds.Max.Y);
					if (ExistingTransfer.LocalStart.Y <= ExistingTransfer.LocalEnd.Y)
					{
						ExistingTransfer.LocalStart.Y = MergedMinimumY;
						ExistingTransfer.LocalEnd.Y = MergedMaximumY;
					}
					else
					{
						ExistingTransfer.LocalEnd.Y = MergedMinimumY;
						ExistingTransfer.LocalStart.Y = MergedMaximumY;
					}
					ExistingTransfer.FaceMask |= Candidate.FaceMask;
					Group.MemberIndices.AddUnique(
						Candidate.ExistingLowerTransferMemberIndex);
					Plan.Summary.MaximumMemberLengthCM = FMath::Max(
						Plan.Summary.MaximumMemberLengthCM,
						static_cast<float>(MergedMaximumY - MergedMinimumY));
				}
				if (!Candidate.bRequiresPost)
				{
					continue;
				}
				const int32 PostIndex = Plan.Members.Num();
				if (!AddPlannedMember(Plan, EOwnerKind::BuildingGroupShell,
					ESkeletonMemberKind::ExteriorPost, Group.GroupId, INDEX_NONE,
					INDEX_NONE, OriginCore, Upper.CourseIndex,
					FMath::RoundToInt(Candidate.StationXCM / BlockUnitsCM),
					FMath::RoundToInt(Candidate.StationYCM / BlockUnitsCM),
					Candidate.FaceMask, EABTSM73BeamAFrameAxis::Z,
					EABTSM73BeamAMemberRole::Post,
					Position(Candidate.StationXCM, Candidate.StationYCM,
						Candidate.BottomZCM),
					Position(Candidate.StationXCM, Candidate.StationYCM,
						Candidate.TopZCM), OutError))
				{
					return false;
				}
				Group.MemberIndices.Add(PostIndex);
			}
		}
		}

		for (int32 PostIndex = 0; PostIndex < Plan.Members.Num(); ++PostIndex)
		{
			const FPlannedMember& Post = Plan.Members[PostIndex];
			if (Post.OwnerKind != EOwnerKind::BuildingGroupShell
				|| Post.Axis != EABTSM73BeamAFrameAxis::Z
				|| Post.FaceMask != 0)
			{
				continue;
			}
			const FBox PostBounds = PlannedMemberBounds(Post);
			bool bSupportsRemainingHorizontal = false;
			for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
			{
				if (RemovedMembers.Contains(UpperIndex)
					|| Plan.Members[UpperIndex].Axis == EABTSM73BeamAFrameAxis::Z)
				{
					continue;
				}
				const FBox Bounds = PlannedMemberBounds(Plan.Members[UpperIndex]);
				if (FMath::Abs(Bounds.Min.Z - PostBounds.Max.Z)
						<= GeometryToleranceCM
					&& SkeletonV3OverlapLength(Bounds.Min.X, Bounds.Max.X,
						PostBounds.Min.X, PostBounds.Max.X) > GeometryToleranceCM
					&& SkeletonV3OverlapLength(Bounds.Min.Y, Bounds.Max.Y,
						PostBounds.Min.Y, PostBounds.Max.Y) > GeometryToleranceCM)
				{
					bSupportsRemainingHorizontal = true;
					break;
				}
			}
			if (!bSupportsRemainingHorizontal)
			{
				RemovedMembers.Add(PostIndex);
			}
		}

		// Re-read the surviving geometry after both bounded closure sweeps and post
		// pruning.  This is deliberately independent of the original worklist: a
		// transfer rail may be adopted or materialised while closing another member,
		// and no such late carrier may escape the same support-hull contract that it
		// was created to satisfy.
		TArray<FVector> FinalLoadResultants;
		TArray<bool> FinalLoadResultantValid;
		ComputeReactionWeightedLoadResultants(RemovedMembers,
			FinalLoadResultants, FinalLoadResultantValid);
		for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
		{
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				if (RemovedMembers.Contains(MemberIndex))
				{
					continue;
				}
				const FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bBelongsToGroup = Group.MemberIndices.Contains(MemberIndex)
					|| (Member.OwnerKind == EOwnerKind::BuildingGroupShell
						&& Member.OwnerId == Group.GroupId)
					|| (Member.ComponentId != INDEX_NONE
						&& Group.ComponentIds.Contains(Member.ComponentId))
					|| (Member.OwnerKind == EOwnerKind::CoreCell
						&& Group.CoreCellIds.Contains(Member.OwnerId))
					|| Member.EndpointCoreCellIds.ContainsByPredicate(
						[&Group](const int32 CoreCellId)
						{
							return Group.CoreCellIds.Contains(CoreCellId);
						});
				const bool bClosureKind =
					Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::ThroughCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::FacadeCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::SharedCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::RoofCourse
					|| Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm;
				const int32 BandIndex = Group.CommonBandBaseCourseIndices.Find(
					Member.CourseIndex);
				if (!bBelongsToGroup
					|| Member.Axis != EABTSM73BeamAFrameAxis::X
					|| !bClosureKind || BandIndex <= 0)
				{
					continue;
				}

				const FBox Bounds = PlannedMemberBounds(Member);
				const double LengthCM = Bounds.Max.X - Bounds.Min.X;
				TArray<FVector2D> Intervals;
				CollectSupportIntervals(MemberIndex, &RemovedMembers, Intervals);
				double RequiredMinimum = 0.0;
				double RequiredMaximum = 0.0;
				if (FinalLoadResultantValid.IsValidIndex(MemberIndex)
					&& FinalLoadResultantValid[MemberIndex])
				{
					RequiredMinimum = FinalLoadResultants[MemberIndex].X;
					RequiredMaximum = RequiredMinimum;
				}
				else
				{
					CollectRequiredLoadRange(MemberIndex, &RemovedMembers,
						RequiredMinimum, RequiredMaximum);
				}
				if (SupportHullSatisfies(Member, Intervals, false,
					RequiredMinimum, RequiredMaximum))
				{
					continue;
				}

				FString IntervalText;
				for (const FVector2D& Interval : Intervals)
				{
					IntervalText += FString::Printf(TEXT("|%.1f..%.1f"),
						Interval.X, Interval.Y);
				}
				OutError = FString::Printf(
					TEXT("BeamC3V3FinalSupportHullOpen:Member=%d:Worklist=%d:Group=%d:Owner=%d:Kind=%d:Course=%d:Face=%u:Length=%.1f:Bounds=%.1f..%.1f:Required=%.1f..%.1f:Supports=%s"),
					MemberIndex, FinalUpperMembers.Contains(MemberIndex) ? 1 : 0,
					Group.GroupId, static_cast<int32>(Member.OwnerKind),
					static_cast<int32>(Member.SkeletonKind), Member.CourseIndex,
					Member.FaceMask, LengthCM, Bounds.Min.X, Bounds.Max.X,
					RequiredMinimum, RequiredMaximum, *IntervalText);
				return false;
			}
		}
		return RemovedMembers.IsEmpty()
			|| CompactPlanMembers(Plan, RemovedMembers, NoReplacements, OutError);
	}

	bool BuildCandidate(
		const FABTSM73BeamD0ResolvedProfile& Profile,
		const FABTSM73DAG5BV2GenerationResult& Silhouette,
		const TArray<FRoot>& Roots,
		const FDensityRecipe& Density,
		const EGenerationStage Stage,
		const double StageStartSeconds,
		FPlan& OutPlan,
		FString& OutError)
	{
		OutPlan = FPlan();
		OutPlan.GameplayProfileId = Profile.GameplayProfileId;
		OutPlan.DifficultyTier = Profile.DifficultyTier;
		OutPlan.ProfileCatalogHash = Profile.ProfileCatalogHash;
		OutPlan.ResolvedSettingsHash = Profile.ResolvedSettingsHash;
		OutPlan.GrammarHash = Silhouette.Summary.GrammarHash;
		OutPlan.WFCHash = Silhouette.Summary.WFCHash;
		OutPlan.Summary.MinimumBrickCount = Profile.VisualComplexity.MinimumBrickCount;
		OutPlan.Summary.MaximumBrickCount = Profile.VisualComplexity.MaximumBrickCount;
		OutPlan.Summary.DensityLevel = Density.RecipeId;
		OutPlan.Summary.HorizontalCellUnits = Density.HorizontalUnits;
		OutPlan.Summary.VerticalBandUnits = Density.VerticalUnits;
		OutPlan.Summary.bPhysicalStabilityEvaluated = false;
		OutPlan.Summary.Stage1TimeBudgetMilliseconds =
			Stage1LeafTimeBudgetMilliseconds;

		struct FSpanInput
		{
			const FABTSM73DAG5BV2Volume* Volume = nullptr;
			int32 NegativeComponentId = INDEX_NONE;
			int32 PositiveComponentId = INDEX_NONE;
			int32 SpanAxis = INDEX_NONE;
			int32 SeatBandBase = INDEX_NONE;
			int32 RailBottomCourse = INDEX_NONE;
			/** Same-axis shared course levels in one deterministic bridge band. */
			TArray<int32> SharedCourses;
		};
		TArray<TArray<int32>> ForcedBandBases;
		ForcedBandBases.SetNum(Roots.Num());
		TArray<FSpanInput> Spans;
		for (const FABTSM73DAG5BV2Volume& Volume : Silhouette.Volumes)
		{
			if (!IsSpanRole(Volume.Role))
			{
				continue;
			}
			if (!Volume.LocalBounds.IsValid
				|| Volume.LocalBounds.Min.ContainsNaN()
				|| Volume.LocalBounds.Max.ContainsNaN()
				|| !FMath::IsFinite(Volume.LocalBounds.Min.X)
				|| !FMath::IsFinite(Volume.LocalBounds.Min.Y)
				|| !FMath::IsFinite(Volume.LocalBounds.Min.Z)
				|| !FMath::IsFinite(Volume.LocalBounds.Max.X)
				|| !FMath::IsFinite(Volume.LocalBounds.Max.Y)
				|| !FMath::IsFinite(Volume.LocalBounds.Max.Z)
				|| !FMath::IsFinite(Volume.SpanOpeningMinCM)
				|| !FMath::IsFinite(Volume.SpanOpeningMaxCM)
				|| (Volume.SpanAxisIndex != 0 && Volume.SpanAxisIndex != 1)
				|| Volume.NegativeSupportVolumeId == INDEX_NONE
				|| Volume.PositiveSupportVolumeId == INDEX_NONE
				|| Volume.NegativeSupportVolumeId == Volume.PositiveSupportVolumeId
				|| Volume.SpanOpeningMinCM >= Volume.SpanOpeningMaxCM
				|| Volume.SpanOpeningMinCM < Volume.LocalBounds.Min[Volume.SpanAxisIndex]
					- WitnessToleranceCM
				|| Volume.SpanOpeningMaxCM > Volume.LocalBounds.Max[Volume.SpanAxisIndex]
					+ WitnessToleranceCM)
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanSemanticContractInvalid:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			FSpanInput& Span = Spans.AddDefaulted_GetRef();
			Span.Volume = &Volume;
			Span.SpanAxis = Volume.SpanAxisIndex;
			for (int32 ComponentIndex = 0; ComponentIndex < Roots.Num(); ++ComponentIndex)
			{
				if (Roots[ComponentIndex].SourceVolumeIds.Contains(Volume.NegativeSupportVolumeId))
				{
					Span.NegativeComponentId = ComponentIndex;
				}
				if (Roots[ComponentIndex].SourceVolumeIds.Contains(Volume.PositiveSupportVolumeId))
				{
					Span.PositiveComponentId = ComponentIndex;
				}
			}
			if ((Span.SpanAxis != 0 && Span.SpanAxis != 1)
				|| Span.NegativeComponentId == INDEX_NONE
				|| Span.PositiveComponentId == INDEX_NONE
				|| Span.NegativeComponentId == Span.PositiveComponentId)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportedSpanEndpointComponentInvalid:Volume=%d:Negative=%d:Positive=%d"),
					Volume.VolumeId, Span.NegativeComponentId, Span.PositiveComponentId);
				return false;
			}
			const FABTSM73DAG5BV2Volume* NegativeSupport =
				FindVolume(Silhouette, Volume.NegativeSupportVolumeId);
			const FABTSM73DAG5BV2Volume* PositiveSupport =
				FindVolume(Silhouette, Volume.PositiveSupportVolumeId);
			if (NegativeSupport == nullptr || PositiveSupport == nullptr
				|| IsSpanRole(NegativeSupport->Role) || IsSpanRole(PositiveSupport->Role)
				|| NegativeSupport->LocalBounds.GetCenter()[Span.SpanAxis]
					>= PositiveSupport->LocalBounds.GetCenter()[Span.SpanAxis]
				|| NegativeSupport->LocalBounds.Max[Span.SpanAxis]
					> Volume.SpanOpeningMinCM + WitnessToleranceCM
				|| PositiveSupport->LocalBounds.Min[Span.SpanAxis]
					< Volume.SpanOpeningMaxCM - WitnessToleranceCM)
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanEndpointOrientationInvalid:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			const int32 TransverseAxis = Span.SpanAxis == 0 ? 1 : 0;
			double ExpectedOpeningMin = NegativeSupport->LocalBounds.Max[Span.SpanAxis];
			double ExpectedOpeningMax = PositiveSupport->LocalBounds.Min[Span.SpanAxis];
			auto ExpandOpeningToEndpointRoot = [&Volume, &Span, TransverseAxis](
				const FRoot& Root, const bool bNegative, double& InOutBoundary)
			{
				TArray<const FABTSM73DAG5BV2Volume*> RootVolumes = Root.BodyVolumes;
				RootVolumes.Append(Root.CrownVolumes);
				for (const FABTSM73DAG5BV2Volume* Candidate : RootVolumes)
				{
					if (Candidate->LocalBounds.Min.Z
						>= Volume.LocalBounds.Min.Z - WitnessToleranceCM
						|| SkeletonV3OverlapLength(
							Volume.LocalBounds.Min[TransverseAxis],
							Volume.LocalBounds.Max[TransverseAxis],
							Candidate->LocalBounds.Min[TransverseAxis],
							Candidate->LocalBounds.Max[TransverseAxis])
							<= WitnessToleranceCM)
					{
						continue;
					}
					InOutBoundary = bNegative
						? FMath::Max(InOutBoundary,
							Candidate->LocalBounds.Max[Span.SpanAxis])
						: FMath::Min(InOutBoundary,
							Candidate->LocalBounds.Min[Span.SpanAxis]);
				}
			};
			ExpandOpeningToEndpointRoot(
				Roots[Span.NegativeComponentId], true, ExpectedOpeningMin);
			ExpandOpeningToEndpointRoot(
				Roots[Span.PositiveComponentId], false, ExpectedOpeningMax);
			if (FMath::Abs(Volume.SpanOpeningMinCM - ExpectedOpeningMin)
				> WitnessToleranceCM
				|| FMath::Abs(Volume.SpanOpeningMaxCM - ExpectedOpeningMax)
					> WitnessToleranceCM)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportedSpanOpeningDoesNotCoverEndpointGap:Volume=%d:Actual=%.3f..%.3f:Expected=%.3f..%.3f"),
					Volume.VolumeId, Volume.SpanOpeningMinCM,
					Volume.SpanOpeningMaxCM, ExpectedOpeningMin,
					ExpectedOpeningMax);
				return false;
			}
			const double GroundZ = Roots[Span.NegativeComponentId].GroundZCM;
			if (!FMath::IsNearlyEqual(GroundZ, Roots[Span.PositiveComponentId].GroundZCM,
				WitnessToleranceCM))
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanGroundPlaneMismatch:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			Span.RailBottomCourse = QRelativeCeil(Volume.LocalBounds.Min.Z, GroundZ);
			const int32 RequiredCourseParity = Span.SpanAxis == 0 ? 0 : 1;
			if ((Span.RailBottomCourse & 1) != RequiredCourseParity)
			{
				++Span.RailBottomCourse;
			}
			if (GroundZ + (Span.RailBottomCourse + 1) * BlockUnitsCM
				> Volume.LocalBounds.Max.Z + BlockUnitsCM * 0.5)
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanCourseOutsideEnvelope:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			Span.SharedCourses.Add(Span.RailBottomCourse);
			// Prefer a three-course bridge band: long shared rails at C and C+2,
			// with transverse diaphragms at C+1. If the semantic span is only one
			// course thick, the lower shared course remains legal and fail-closed
			// validation still prevents pretending that a second level exists.
			if (GroundZ + (Span.RailBottomCourse + 3) * BlockUnitsCM
				<= Volume.LocalBounds.Max.Z + GeometryToleranceCM)
			{
				Span.SharedCourses.Add(Span.RailBottomCourse + 2);
			}
			Span.SeatBandBase = Span.RailBottomCourse - (Span.SpanAxis == 0 ? 2 : 1);
			if (Span.SeatBandBase < 0)
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanSeatBelowGround:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			// Shared courses seat directly on the continuous endpoint cores. They
			// therefore never force a sparse shell-band phase or height.
		}

		TArray<TArray<FBandState>> ComponentBands;
		ComponentBands.SetNum(Roots.Num());
		if (Stage == EGenerationStage::CoreAndShared)
		{
			FStage1PhaseTimer PhaseTimer(
				OutPlan.Summary.TerminalDemandMilliseconds);
			for (int32 RootIndex = 0; RootIndex < Roots.Num(); ++RootIndex)
			{
				if (!BuildSemanticSupportDemandDiagnostics(
					Roots[RootIndex], RootIndex, OutPlan, OutError))
				{
					PhaseTimer.Stop();
					OutError = FString::Printf(
						TEXT("BeamC3V3SemanticSupportDemandBuildFailed:Component=%d:%s"),
						RootIndex, *OutError);
					return false;
				}
			}
			if (!FinalizeSemanticSupportDemandDiagnostics(OutPlan, OutError)
				|| !BuildSupportProvinceDiagnostics(OutPlan, OutError))
			{
				PhaseTimer.Stop();
				return false;
			}
			PhaseTimer.Stop();
			if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
				TEXT("TerminalDemand"), OutPlan, OutError))
			{
				return false;
			}
		}
		for (int32 RootIndex = 0; RootIndex < Roots.Num(); ++RootIndex)
		{
			const FRoot& Root = Roots[RootIndex];
			FComponentPlan& Component = OutPlan.Components.AddDefaulted_GetRef();
			Component.ComponentId = RootIndex;
			Component.SemanticRootPath = Root.Path;
			Component.OccupiedBounds = Root.Bounds;
			Component.GroundPlaneZCM = Root.GroundZCM;
			Component.SourceVolumeIds = Root.SourceVolumeIds;
			Component.CrownVolumeIds = Root.CrownVolumeIds;
			Component.GroundSourceVolumeIds = Root.GroundSourceVolumeIds;
			Component.SourceGroundComponentCount =
				FMath::Max(1, Root.SourceGroundComponentIds.Num());
			Component.CoreMergeRegionId = OutPlan.CoreMergeRegions.Num();
			Component.VerticalSupportWitnesses = Root.Witnesses;
			FCoreMergeRegionPlan& MergeRegion =
				OutPlan.CoreMergeRegions.AddDefaulted_GetRef();
			MergeRegion.RegionId = Component.CoreMergeRegionId;
			MergeRegion.ComponentId = RootIndex;
			MergeRegion.SourceGroundComponentCount =
				Component.SourceGroundComponentCount;
			MergeRegion.SourceVolumeIds = Root.GroundSourceVolumeIds;
			for (const int32 SourceVolumeId : MergeRegion.SourceVolumeIds)
			{
				MergeRegion.SourceOriginalGroundComponentIds.Add(
					Root.SourceVolumeOriginalComponentIds.FindRef(SourceVolumeId));
			}
			for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
			{
				if (Volume == nullptr
					|| !Root.GroundSourceVolumeIds.Contains(Volume->VolumeId))
				{
					continue;
				}
				FBox Base = Volume->LocalBounds;
				Base.Min.Z = Root.GroundZCM;
				// The merge diagnostic is the complete derived podium volume. Showing
				// only a half-course ground slice hid the actual coupled-base height.
				Base.Max.Z = Volume->LocalBounds.Max.Z;
				MergeRegion.GroundSourceBounds.Add(Base);
				MergeRegion.LocalBounds += Volume->LocalBounds;
			}
			bool bPotentialCompositeHierarchy =
				Stage == EGenerationStage::CoreAndShared;
			bPotentialCompositeHierarchy &= Root.BodyVolumes.ContainsByPredicate(
				[&Root](const FABTSM73DAG5BV2Volume* Volume)
				{
					return Volume != nullptr
						&& Root.GroundSourceVolumeIds.Contains(Volume->VolumeId)
						&& Volume->DerivationPath.StartsWith(TEXT("CoupledGround/"));
				});
			const int32 DesiredRailCount = FMath::Max(
				bPotentialCompositeHierarchy ? 3 : 2,
				FMath::Clamp(
				FMath::CeilToInt(2.0 * FMath::Sqrt(
					static_cast<double>(Component.SourceGroundComponentCount))), 2, 5));
			MergeRegion.SelectedRailCount = DesiredRailCount;
			OutPlan.Summary.MaximumCoreRailCount = FMath::Max(
				OutPlan.Summary.MaximumCoreRailCount, DesiredRailCount);
			OutPlan.Summary.CoreBearingPatchCountPerInterface +=
				DesiredRailCount * DesiredRailCount;
			OutPlan.Summary.VerticalWitnessCount += Root.Witnesses.Num();
			if (!MakeAxisGrid(Root.BodyVolumes, 0, Density.HorizontalUnits, Component.XGridUnits)
				|| !MakeAxisGrid(Root.BodyVolumes, 1, Density.HorizontalUnits, Component.YGridUnits))
			{
				OutError = FString::Printf(TEXT("BeamC3V3RootRasterGridUnavailable:%s"), *Root.Path);
				return false;
			}
			auto ClipGridAgainstSpanVoids = [&Spans, RootIndex](
				TArray<int32>& Grid, const int32 Axis)
			{
				if (Grid.Num() < 2)
				{
					return false;
				}
				int32 Minimum = Grid[0];
				int32 Maximum = Grid.Last();
				for (const FSpanInput& Span : Spans)
				{
					if (Span.Volume == nullptr || Span.SpanAxis != Axis)
					{
						continue;
					}
					if (Span.NegativeComponentId == RootIndex)
					{
						Maximum = FMath::Min(Maximum,
							QMax(Span.Volume->SpanOpeningMinCM - BlockUnitsCM * 0.5));
					}
					if (Span.PositiveComponentId == RootIndex)
					{
						Minimum = FMath::Max(Minimum,
							QMin(Span.Volume->SpanOpeningMaxCM + BlockUnitsCM * 0.5));
					}
				}
				if (Maximum <= Minimum)
				{
					return false;
				}
				TArray<int32> Clipped;
				Clipped.Add(Minimum);
				for (const int32 Station : Grid)
				{
					if (Station > Minimum && Station < Maximum)
					{
						Clipped.AddUnique(Station);
					}
				}
				Clipped.AddUnique(Maximum);
				Clipped.Sort();
				Grid = MoveTemp(Clipped);
				return Grid.Num() >= 2;
			};
			if (!ClipGridAgainstSpanVoids(Component.XGridUnits, 0)
				|| !ClipGridAgainstSpanVoids(Component.YGridUnits, 1))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SpanVoidConsumesComponentRaster:%s"), *Root.Path);
				return false;
			}
			const int32 BodyTopCourse = QRelativeFloor(Root.BodyTopCM, Root.GroundZCM);
			int32 PodiumTopCourse = BodyTopCourse;
			bool bHasDerivedCoupledPodium = false;
			for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
			{
				if (Volume != nullptr
					&& Root.GroundSourceVolumeIds.Contains(Volume->VolumeId)
					&& Volume->DerivationPath.StartsWith(TEXT("CoupledGround/")))
				{
					bHasDerivedCoupledPodium = true;
					PodiumTopCourse = FMath::Min(PodiumTopCourse,
						QRelativeFloor(Volume->LocalBounds.Max.Z, Root.GroundZCM));
				}
			}
			bool bIncidentSupportedSpan = false;
			for (const FSpanInput& Span : Spans)
			{
				bIncidentSupportedSpan |= Span.NegativeComponentId == RootIndex
					|| Span.PositiveComponentId == RootIndex;
			}
			const bool bUseGroundedCoreHierarchy =
				Stage == EGenerationStage::CoreAndShared
					&& bHasDerivedCoupledPodium
					&& PodiumTopCourse >= 2 && PodiumTopCourse + 2 <= BodyTopCourse;
			int32 CoreTopCourse = bUseGroundedCoreHierarchy
				? PodiumTopCourse : BodyTopCourse;
			for (const FSpanInput& Span : Spans)
			{
				if (!bUseGroundedCoreHierarchy
					&& (Span.NegativeComponentId == RootIndex
					|| Span.PositiveComponentId == RootIndex)
					)
				{
					// The shared course occupies C; its two endpoint cores must also
					// provide the perpendicular lower C-1 and upper C+1 courses.
					const int32 HighestSharedCourse = Span.SharedCourses.IsEmpty()
						? Span.RailBottomCourse : Span.SharedCourses.Last();
					CoreTopCourse = FMath::Max(CoreTopCourse, HighestSharedCourse + 2);
				}
			}
			TArray<FBox> BodyAllowedBoxes;
			for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
			{
				BodyAllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
					BlockUnitsCM * 0.5 + GeometryToleranceCM));
			}
			TArray<FBox> CoreAllowedBoxes = BodyAllowedBoxes;
			for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
			{
				CoreAllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
					BlockUnitsCM * 0.5 + GeometryToleranceCM));
			}
			// Jointly plan against the independent terminal load branches, not only
			// the XY components visible at the podium seam. A single broad entry may
			// split higher into several towers; each terminal branch remains a
			// separate child/main demand even when its entry footprint is shared.
			TArray<FRequiredHighProjectionDemand> RequiredHighProjectionDemands;
			TArray<FBox> RequiredHighProjectionEntryBounds;
			TArray<FBox> RequiredHighProjectionTerminalBounds;
			TArray<FBox> RequiredHighProjectionBranchBounds;
			TArray<TArray<int32>> RequiredHighProjectionSourceVolumeIds;
			TArray<int32> RequiredHighProjectionSemanticDemandIds;
			TArray<TArray<TArray<int32>>>
				RequiredHighProjectionCourseSourceVolumeIds;
			TArray<int32> RequiredHighProjectionTopCourses;
			if (Stage == EGenerationStage::CoreAndShared)
			{
				FStage1PhaseTimer PhaseTimer(
					OutPlan.Summary.TerminalDemandMilliseconds);
				TArray<FRequiredHighProjectionDemand> LegacyProjectionDemands;
				const bool bBuiltLegacyDemands = !bUseGroundedCoreHierarchy
					|| BuildRequiredHighProjectionDemands(
						Root, PodiumTopCourse,
						LegacyProjectionDemands, OutError);
				const bool bBuiltDemands = bBuiltLegacyDemands
					&& (!bUseGroundedCoreHierarchy
						|| BuildSemanticChildProjectionDemands(
							Root, RootIndex, PodiumTopCourse, OutPlan,
							LegacyProjectionDemands,
							RequiredHighProjectionDemands, OutError));
				PhaseTimer.Stop();
				if (!bBuiltDemands)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3TerminalDemandBuildFailed:Component=%d:%s"),
						RootIndex, *OutError);
					return false;
				}
				if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
					TEXT("TerminalDemand"), OutPlan, OutError))
				{
					return false;
				}
				for (const FRequiredHighProjectionDemand& Demand
					: RequiredHighProjectionDemands)
				{
					RequiredHighProjectionSemanticDemandIds.Add(
						Demand.SemanticDemandId);
					RequiredHighProjectionEntryBounds.Add(Demand.EntryBounds);
					RequiredHighProjectionTerminalBounds.Add(Demand.TerminalBounds);
					RequiredHighProjectionBranchBounds.Add(Demand.BranchBounds);
					RequiredHighProjectionSourceVolumeIds.Add(Demand.SourceVolumeIds);
					RequiredHighProjectionCourseSourceVolumeIds.Add(
						Demand.CourseSourceVolumeIds);
					RequiredHighProjectionTopCourses.Add(Demand.RequiredTopCourse);
				}
			}
			if (bUseGroundedCoreHierarchy
				&& RequiredHighProjectionEntryBounds.Num() > 30)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3HighProjectionCoverageCardinalityInvalid:Component=%d:Regions=%d"),
					RootIndex, RequiredHighProjectionEntryBounds.Num());
				return false;
			}

			struct FJointChildFootprint
			{
				int32 MinimumX = INDEX_NONE;
				int32 MaximumX = INDEX_NONE;
				int32 MinimumY = INDEX_NONE;
				int32 MaximumY = INDEX_NONE;
				int32 BaseSource = INDEX_NONE;
				int32 TopCourse = 0;
				int32 BodyTopCourse = 0;
				int32 MinimumSpan = 0;
				int32 Imbalance = 0;
				int32 Area = 0;
				double Distance = DBL_MAX;
				TArray<int32> XStations;
				TArray<int32> YStations;
			};
			TArray<TArray<FJointChildFootprint>>
				FullHeightChildCandidatesByProjection;
			TArray<int32> RequiredFullHeightCourses;
			auto JointFootprintsConflict = [](
				const int32 AMinimumX, const int32 AMaximumX,
				const int32 AMinimumY, const int32 AMaximumY,
				const TArray<int32>& AXStations,
				const TArray<int32>& AYStations,
				const int32 BMinimumX, const int32 BMaximumX,
				const int32 BMinimumY, const int32 BMaximumY,
				const TArray<int32>& BXStations,
				const TArray<int32>& BYStations)
			{
				const double XOverlap = SkeletonV3OverlapLength(
					(AMinimumX - 0.5) * BlockUnitsCM,
					(AMaximumX + 0.5) * BlockUnitsCM,
					(BMinimumX - 0.5) * BlockUnitsCM,
					(BMaximumX + 0.5) * BlockUnitsCM);
				const double YOverlap = SkeletonV3OverlapLength(
					(AMinimumY - 0.5) * BlockUnitsCM,
					(AMaximumY + 0.5) * BlockUnitsCM,
					(BMinimumY - 0.5) * BlockUnitsCM,
					(BMaximumY + 0.5) * BlockUnitsCM);
				return (XOverlap > GeometryToleranceCM
						&& AYStations.ContainsByPredicate(
							[&BYStations](const int32 Station)
							{
								return BYStations.Contains(Station);
							}))
					|| (YOverlap > GeometryToleranceCM
						&& AXStations.ContainsByPredicate(
							[&BXStations](const int32 Station)
							{
								return BXStations.Contains(Station);
							}));
			};
			if (bUseGroundedCoreHierarchy)
			{
				FStage1PhaseTimer ChildCandidateTimer(
					OutPlan.Summary.ChildCandidateMilliseconds);
				TMap<FMainSourceProbeKey, int32> ChildSourceProbeCache;
				auto SelectCachedChildSource = [&Root, &ChildSourceProbeCache](
					const int32 CenterXTwiceUnits,
					const int32 CenterYTwiceUnits,
					const int32 Course,
					const bool bAllowCrown)
				{
					const FMainSourceProbeKey Key{
						CenterXTwiceUnits, CenterYTwiceUnits, Course, bAllowCrown};
					if (const int32* Cached = ChildSourceProbeCache.Find(Key))
					{
						return *Cached;
					}
					const double CenterX = CenterXTwiceUnits * 0.5 * BlockUnitsCM;
					const double CenterY = CenterYTwiceUnits * 0.5 * BlockUnitsCM;
					const double Z = Root.GroundZCM + (Course + 0.5) * BlockUnitsCM;
					const int32 Source = bAllowCrown
						? SelectCoreProjectionSourceVolume(Root, CenterX, CenterY, Z)
						: SelectProjectionSourceVolume(Root, CenterX, CenterY, Z);
					ChildSourceProbeCache.Add(Key, Source);
					return Source;
				};
				FullHeightChildCandidatesByProjection.SetNum(
					RequiredHighProjectionEntryBounds.Num());
				RequiredFullHeightCourses.SetNumZeroed(
					RequiredHighProjectionEntryBounds.Num());
				for (int32 ProjectionIndex = 0;
					ProjectionIndex < RequiredHighProjectionEntryBounds.Num();
					++ProjectionIndex)
				{
					const FBox& ProjectionBounds =
						RequiredHighProjectionTerminalBounds[ProjectionIndex];
					FFullHeightChildCandidateDiagnostic& Diagnostic =
						OutPlan.FullHeightChildCandidateDiagnostics
							.AddDefaulted_GetRef();
					Diagnostic.ComponentId = RootIndex;
					Diagnostic.SemanticDemandId =
						RequiredHighProjectionSemanticDemandIds[ProjectionIndex];
					Diagnostic.LocalProjectionIndex = ProjectionIndex;
					Diagnostic.RegionId = ProjectionIndex;
					Diagnostic.PodiumTopCourse = PodiumTopCourse;
					const int32 SeedMinimumX = QMin(
						ProjectionBounds.Min.X + BlockUnitsCM * 0.5);
					const int32 SeedMaximumX = QMax(
						ProjectionBounds.Max.X - BlockUnitsCM * 0.5);
					const int32 SeedMinimumY = QMin(
						ProjectionBounds.Min.Y + BlockUnitsCM * 0.5);
					const int32 SeedMaximumY = QMax(
						ProjectionBounds.Max.Y - BlockUnitsCM * 0.5);
					const int32 MaximumSpanX = FMath::Min(
						MaximumHorizontalUnits, SeedMaximumX - SeedMinimumX);
					const int32 MaximumSpanY = FMath::Min(
						MaximumHorizontalUnits, SeedMaximumY - SeedMinimumY);
					const int32 MaximumCandidateTopCourse =
						RequiredHighProjectionTopCourses[ProjectionIndex];
					TArray<TArray<FBox>> ProjectionAllowedBoxesByCourse;
					ProjectionAllowedBoxesByCourse.SetNum(MaximumCandidateTopCourse);
					for (int32 Course = PodiumTopCourse;
						Course < MaximumCandidateTopCourse; ++Course)
					{
						if (!RequiredHighProjectionCourseSourceVolumeIds
							.IsValidIndex(ProjectionIndex)
							|| !RequiredHighProjectionCourseSourceVolumeIds[ProjectionIndex]
								.IsValidIndex(Course))
						{
							continue;
						}
						const TArray<int32>& AllowedSourceIds =
							RequiredHighProjectionCourseSourceVolumeIds[ProjectionIndex][Course];
						for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
						{
							if (Volume != nullptr && AllowedSourceIds.Contains(Volume->VolumeId))
							{
								ProjectionAllowedBoxesByCourse[Course].Add(
									Volume->LocalBounds.ExpandBy(
										BlockUnitsCM * 0.5 + GeometryToleranceCM));
							}
						}
						for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
						{
							if (Volume != nullptr && AllowedSourceIds.Contains(Volume->VolumeId))
							{
								ProjectionAllowedBoxesByCourse[Course].Add(
									Volume->LocalBounds.ExpandBy(
										BlockUnitsCM * 0.5 + GeometryToleranceCM));
							}
						}
					}
					TMap<FMainRailCoverageKey, bool> ChildRailCoverageCache;
					auto IsCachedChildRailCovered = [&Root, &BodyAllowedBoxes,
						&CoreAllowedBoxes, &ProjectionAllowedBoxesByCourse,
						PodiumTopCourse, &ChildRailCoverageCache](
						const int32 Course,
						const EABTSM73BeamAFrameAxis Axis,
						const int32 AlongMinimum,
						const int32 AlongMaximum,
						const int32 CrossStation,
						const bool bBodyAllowedBoxes)
					{
						const FMainRailCoverageKey Key{
							Course, AlongMinimum, AlongMaximum, CrossStation,
							static_cast<uint8>(Axis), bBodyAllowedBoxes};
						if (const bool* Cached = ChildRailCoverageCache.Find(Key))
						{
							return *Cached;
						}
						const double Z = Root.GroundZCM
							+ (Course + 0.5) * BlockUnitsCM;
						FPlannedMember Probe;
						Probe.Axis = Axis;
						Probe.LocalStart = Axis == EABTSM73BeamAFrameAxis::X
							? Position(AlongMinimum * BlockUnitsCM - BlockUnitsCM * 0.5,
								CrossStation * BlockUnitsCM, Z)
							: Position(CrossStation * BlockUnitsCM,
								AlongMinimum * BlockUnitsCM - BlockUnitsCM * 0.5, Z);
						Probe.LocalEnd = Axis == EABTSM73BeamAFrameAxis::X
							? Position(AlongMaximum * BlockUnitsCM + BlockUnitsCM * 0.5,
								CrossStation * BlockUnitsCM, Z)
							: Position(CrossStation * BlockUnitsCM,
								AlongMaximum * BlockUnitsCM + BlockUnitsCM * 0.5, Z);
						const TArray<FBox>& AllowedBoxes = bBodyAllowedBoxes
							? BodyAllowedBoxes
							: Course >= PodiumTopCourse
								? ProjectionAllowedBoxesByCourse[Course]
								: CoreAllowedBoxes;
						FVector UncoveredPoint;
						const bool bCovered = SolidCoveredByBoxes(
							PlannedMemberBounds(Probe), AllowedBoxes, UncoveredPoint);
						ChildRailCoverageCache.Add(Key, bCovered);
						return bCovered;
					};
					TArray<FJointChildFootprint> WFCFeasibleCandidates;
					for (int32 SpanX = 1; SpanX <= MaximumSpanX; ++SpanX)
					{
						for (int32 SpanY = 1; SpanY <= MaximumSpanY; ++SpanY)
						{
							for (int32 MinimumY = SeedMinimumY;
								MinimumY + SpanY <= SeedMaximumY; ++MinimumY)
							{
								for (int32 MinimumX = SeedMinimumX;
									MinimumX + SpanX <= SeedMaximumX; ++MinimumX)
								{
									++Diagnostic.EnumeratedFootprintCount;
									if ((Diagnostic.EnumeratedFootprintCount & 0xFF) == 0)
									{
										ChildCandidateTimer.Checkpoint();
										if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
											TEXT("ChildCandidate"), OutPlan, OutError))
										{
											return false;
										}
									}
									const int32 MaximumX = MinimumX + SpanX;
									const int32 MaximumY = MinimumY + SpanY;
									const TArray<int32> XStations = MakeUniformStations(
										MinimumX, MaximumX, 2);
									const TArray<int32> YStations = MakeUniformStations(
										MinimumY, MaximumY, 2);
									if (XStations.Num() != 2 || YStations.Num() != 2)
									{
										++Diagnostic.InvalidLatticeRejectCount;
										continue;
									}
									const int32 CenterXTwiceUnits = MinimumX + MaximumX;
									const int32 CenterYTwiceUnits = MinimumY + MaximumY;
									const double CenterX = CenterXTwiceUnits
										* 0.5 * BlockUnitsCM;
									const double CenterY = CenterYTwiceUnits
										* 0.5 * BlockUnitsCM;
									const int32 BaseSource = SelectCachedChildSource(
										CenterXTwiceUnits, CenterYTwiceUnits, 0, false);
									if (BaseSource == INDEX_NONE
										|| !Root.GroundSourceVolumeIds.Contains(BaseSource))
									{
										++Diagnostic.GroundSourceRejectCount;
										continue;
									}
									int32 ContinuousTopCourse = 0;
									int32 FirstNonBodyCourse = INDEX_NONE;
									for (int32 Course = 0;
										Course < MaximumCandidateTopCourse; ++Course)
									{
										if (SelectCachedChildSource(CenterXTwiceUnits,
											CenterYTwiceUnits, Course, true) == INDEX_NONE)
										{
											break;
										}
										const EABTSM73BeamAFrameAxis Axis =
											(Course & 1) == 0
												? EABTSM73BeamAFrameAxis::X
												: EABTSM73BeamAFrameAxis::Y;
										const TArray<int32>& CrossStations =
											Axis == EABTSM73BeamAFrameAxis::X
												? YStations : XStations;
										bool bCourseCovered = true;
										bool bBodyCourseCovered = SelectCachedChildSource(
											CenterXTwiceUnits, CenterYTwiceUnits, Course, false)
											!= INDEX_NONE;
										for (const int32 CrossStation : CrossStations)
										{
											if (!IsCachedChildRailCovered(Course, Axis,
												Axis == EABTSM73BeamAFrameAxis::X
													? MinimumX : MinimumY,
												Axis == EABTSM73BeamAFrameAxis::X
													? MaximumX : MaximumY,
												CrossStation, false))
											{
												bCourseCovered = false;
												break;
											}
											bBodyCourseCovered &= IsCachedChildRailCovered(
												Course, Axis,
												Axis == EABTSM73BeamAFrameAxis::X
													? MinimumX : MinimumY,
												Axis == EABTSM73BeamAFrameAxis::X
													? MaximumX : MaximumY,
												CrossStation, true);
										}
										if (!bCourseCovered)
										{
											break;
										}
										if (!bBodyCourseCovered
											&& FirstNonBodyCourse == INDEX_NONE)
										{
											FirstNonBodyCourse = Course;
										}
										ContinuousTopCourse = Course + 1;
									}
									if (ContinuousTopCourse != MaximumCandidateTopCourse)
									{
										++Diagnostic.WFCEnvelopeRejectCount;
										continue;
									}
									FJointChildFootprint Candidate;
									Candidate.MinimumX = MinimumX;
									Candidate.MaximumX = MaximumX;
									Candidate.MinimumY = MinimumY;
									Candidate.MaximumY = MaximumY;
									Candidate.BaseSource = BaseSource;
									Candidate.TopCourse = ContinuousTopCourse;
									Candidate.BodyTopCourse =
										FirstNonBodyCourse == INDEX_NONE
											? ContinuousTopCourse : FirstNonBodyCourse;
									Candidate.MinimumSpan = FMath::Min(SpanX, SpanY);
									Candidate.Imbalance = FMath::Abs(SpanX - SpanY);
									Candidate.Area = SpanX * SpanY;
									Candidate.Distance = FVector2D::DistSquared(
										FVector2D(CenterX, CenterY),
										FVector2D(ProjectionBounds.GetCenter().X,
											ProjectionBounds.GetCenter().Y));
									Candidate.XStations = XStations;
									Candidate.YStations = YStations;
									WFCFeasibleCandidates.Add(MoveTemp(Candidate));
								}
							}
						}
					}
					const int32 RequiredTopCourse = MaximumCandidateTopCourse;
					Diagnostic.RequiredFullHeightCourse = RequiredTopCourse;
					RequiredFullHeightCourses[ProjectionIndex] = RequiredTopCourse;
					for (FJointChildFootprint& Candidate : WFCFeasibleCandidates)
					{
						if (Candidate.TopCourse == RequiredTopCourse)
						{
							FullHeightChildCandidatesByProjection[ProjectionIndex]
								.Add(MoveTemp(Candidate));
						}
					}
					auto& FullHeightCandidates =
						FullHeightChildCandidatesByProjection[ProjectionIndex];
					FullHeightCandidates.Sort([](
						const FJointChildFootprint& A,
						const FJointChildFootprint& B)
						{
							if (A.MinimumSpan != B.MinimumSpan)
							{
								return A.MinimumSpan > B.MinimumSpan;
							}
							if (A.Imbalance != B.Imbalance)
							{
								return A.Imbalance < B.Imbalance;
							}
							if (A.Area != B.Area)
							{
								return A.Area > B.Area;
							}
							if (!FMath::IsNearlyEqual(A.Distance, B.Distance))
							{
								return A.Distance < B.Distance;
							}
							return A.MinimumY != B.MinimumY
								? A.MinimumY < B.MinimumY
								: A.MinimumX < B.MinimumX;
						});
					Diagnostic.WFCFullHeightWitnessCount =
						FullHeightCandidates.Num();
					Diagnostic.SelectionReason = TEXT("AwaitingJointMainSelection");
					if (RequiredTopCourse < PodiumTopCourse + 2
						|| FullHeightCandidates.IsEmpty())
					{
						Diagnostic.SelectionReason =
							TEXT("NoFixedFootprintWFCFullHeightWitness");
						OutError = FString::Printf(
							TEXT("BeamC3V3FullHeightChildWFCUnavailable:Component=%d:Projection=%d:PodiumTop=%d:RequiredTop=%d:Candidates=%d:Ground=%d:Envelope=%d"),
							RootIndex, ProjectionIndex, PodiumTopCourse,
							RequiredTopCourse, Diagnostic.EnumeratedFootprintCount,
							Diagnostic.GroundSourceRejectCount,
							Diagnostic.WFCEnvelopeRejectCount);
						return false;
					}
				}
				ChildCandidateTimer.Stop();
				if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
					TEXT("ChildCandidate"), OutPlan, OutError))
				{
					return false;
				}
			}
			int32 CoreMinimumXUnits = INDEX_NONE;
			int32 CoreMaximumXUnits = INDEX_NONE;
			int32 CoreMinimumYUnits = INDEX_NONE;
			int32 CoreMaximumYUnits = INDEX_NONE;
			int32 CoreBaseSource = INDEX_NONE;
			int32 BestCoreMarginClass = -1;
			int32 BestCoreMinimumSpan = -1;
			int32 BestCoreSpanImbalance = MAX_int32;
			int32 BestCoreArea = -1;
			double BestCoreDistance = DBL_MAX;
			FStage1PhaseTimer MainCandidateTimer(
				OutPlan.Summary.PodiumMainCandidateMilliseconds);
			int64 MainCandidatePollCount = 0;
			int64 MainRailCoverageCacheHitCount = 0;
			int64 MainRailCoverageCacheMissCount = 0;
			int64 MainSourceProbeCacheHitCount = 0;
			int64 MainSourceProbeCacheMissCount = 0;
			int64 MainCompleteStackCandidateCount = 0;
			int64 MainChildCompatibilityProbeCount = 0;
			TMap<FMainRailCoverageRowKey, FMainRailCoverageRow>
				MainRailCoverageCache;
			TMap<FMainSourceProbeKey, int32> MainSourceProbeCache;
			auto AppendMainPerformanceEvidence = [&OutPlan, &OutError,
				&MainCandidatePollCount, &MainRailCoverageCache,
				&MainSourceProbeCache]()
			{
				OutError += FString::Printf(
					TEXT(":MainStates=%lld:RailCache=%d:SourceCache=%d"),
					MainCandidatePollCount, MainRailCoverageCache.Num(),
					MainSourceProbeCache.Num());
				OutPlan.Summary.RejectReason = OutError;
			};
			auto SelectCachedMainSource = [&Root, &MainSourceProbeCache,
				&MainSourceProbeCacheHitCount, &MainSourceProbeCacheMissCount](
				const int32 CenterXTwiceUnits,
				const int32 CenterYTwiceUnits,
				const int32 Course,
				const bool bAllowCrown)
			{
				const FMainSourceProbeKey Key{
					CenterXTwiceUnits, CenterYTwiceUnits, Course, bAllowCrown};
				if (const int32* Cached = MainSourceProbeCache.Find(Key))
				{
					++MainSourceProbeCacheHitCount;
					return *Cached;
				}
				++MainSourceProbeCacheMissCount;
				const double CenterX = CenterXTwiceUnits * 0.5 * BlockUnitsCM;
				const double CenterY = CenterYTwiceUnits * 0.5 * BlockUnitsCM;
				const double Z = Root.GroundZCM + (Course + 0.5) * BlockUnitsCM;
				const int32 Source = bAllowCrown
					? SelectCoreProjectionSourceVolume(Root, CenterX, CenterY, Z)
					: SelectProjectionSourceVolume(Root, CenterX, CenterY, Z);
				MainSourceProbeCache.Add(Key, Source);
				return Source;
			};
			auto IsCachedMainRailCovered = [&Root, &BodyAllowedBoxes,
				&CoreAllowedBoxes, &MainRailCoverageCache,
				&MainRailCoverageCacheHitCount,
				&MainRailCoverageCacheMissCount](
				const int32 Course,
				const EABTSM73BeamAFrameAxis Axis,
				const int32 AlongMinimum,
				const int32 AlongMaximum,
				const int32 CrossStation,
				const bool bBodyAllowedBoxes)
			{
				const FMainRailCoverageRowKey Key{
					Course, CrossStation, static_cast<uint8>(Axis),
					bBodyAllowedBoxes};
				FMainRailCoverageRow* Row = MainRailCoverageCache.Find(Key);
				if (Row != nullptr)
				{
					++MainRailCoverageCacheHitCount;
				}
				else
				{
					++MainRailCoverageCacheMissCount;
					FMainRailCoverageRow& NewRow =
						MainRailCoverageCache.Add(Key);
					Row = &NewRow;
					Row->AlongMinimum = Axis == EABTSM73BeamAFrameAxis::X
						? QMin(Root.Bounds.Min.X + BlockUnitsCM * 0.5)
						: QMin(Root.Bounds.Min.Y + BlockUnitsCM * 0.5);
					Row->AlongMaximum = Axis == EABTSM73BeamAFrameAxis::X
						? QMax(Root.Bounds.Max.X - BlockUnitsCM * 0.5)
						: QMax(Root.Bounds.Max.Y - BlockUnitsCM * 0.5);
					// Component raster endpoints can intentionally extend through a
					// coupled seam beyond Root.Bounds. Keep a bounded guard band so
					// those exact candidate intervals are represented by the same
					// atomic coverage table instead of being rejected by the cache.
					Row->AlongMinimum -= MaximumHorizontalUnits;
					Row->AlongMaximum += MaximumHorizontalUnits;
					const int32 CellCount = Row->AlongMaximum
						- Row->AlongMinimum + 1;
					Row->UncoveredPrefix.SetNumZeroed(CellCount + 1);
					const double Z = Root.GroundZCM
						+ (Course + 0.5) * BlockUnitsCM;
					const TArray<FBox>& AllowedBoxes = bBodyAllowedBoxes
						? BodyAllowedBoxes : CoreAllowedBoxes;
					for (int32 CellIndex = 0; CellIndex < CellCount; ++CellIndex)
					{
						const int32 AlongStation = Row->AlongMinimum + CellIndex;
						FPlannedMember Probe;
						Probe.Axis = Axis;
						Probe.LocalStart = Axis == EABTSM73BeamAFrameAxis::X
							? Position((AlongStation - 0.5) * BlockUnitsCM,
								CrossStation * BlockUnitsCM, Z)
							: Position(CrossStation * BlockUnitsCM,
								(AlongStation - 0.5) * BlockUnitsCM, Z);
						Probe.LocalEnd = Axis == EABTSM73BeamAFrameAxis::X
							? Position((AlongStation + 0.5) * BlockUnitsCM,
								CrossStation * BlockUnitsCM, Z)
							: Position(CrossStation * BlockUnitsCM,
								(AlongStation + 0.5) * BlockUnitsCM, Z);
						FVector UncoveredPoint;
						const bool bCellCovered = SolidCoveredByBoxes(
							PlannedMemberBounds(Probe), AllowedBoxes,
							UncoveredPoint);
						Row->UncoveredPrefix[CellIndex + 1] =
							Row->UncoveredPrefix[CellIndex]
							+ (bCellCovered ? 0 : 1);
					}
				}
				if (AlongMinimum < Row->AlongMinimum
					|| AlongMaximum > Row->AlongMaximum
					|| AlongMaximum < AlongMinimum)
				{
					return false;
				}
				const int32 PrefixMinimum = AlongMinimum - Row->AlongMinimum;
				const int32 PrefixMaximum = AlongMaximum - Row->AlongMinimum + 1;
				return Row->UncoveredPrefix[PrefixMaximum]
					== Row->UncoveredPrefix[PrefixMinimum];
			};
			struct FPodiumMainCandidate
			{
				int32 MinimumX = INDEX_NONE;
				int32 MaximumX = INDEX_NONE;
				int32 MinimumY = INDEX_NONE;
				int32 MaximumY = INDEX_NONE;
				int32 BaseSource = INDEX_NONE;
				int32 MarginClass = -1;
				int32 MinimumSpan = -1;
				int32 SpanImbalance = MAX_int32;
				int32 Area = -1;
				double Distance = DBL_MAX;
				uint32 CoverageMask = 0;
				uint32 FullHeightCompatibilityMask = 0;
				uint32 ProvinceCoverageMask = 0;
				bool bCoversPodiumSupportAnchor = false;
				TArray<int32> XStations;
				TArray<int32> YStations;
				/** Exact retained child compatibility, built only after retention. */
				TArray<TArray<uint64>> CompatibleChildWordsByProjection;
			};
			TMap<uint32, TMap<uint64, TArray<FPodiumMainCandidate>>>
				PodiumCandidatesByProvinceAndCoverage;
			TArray<FPodiumMainCandidate> SelectedCoreCandidates;
			FVector2D CoreTarget(Root.Bounds.GetCenter().X, Root.Bounds.GetCenter().Y);
			if (!bUseGroundedCoreHierarchy && !Root.CrownVolumes.IsEmpty())
			{
				const FABTSM73DAG5BV2Volume* LowestCrown = Root.CrownVolumes[0];
				for (const FABTSM73DAG5BV2Volume* Crown : Root.CrownVolumes)
				{
					if (Crown->LocalBounds.Min.Z < LowestCrown->LocalBounds.Min.Z
						|| (FMath::IsNearlyEqual(Crown->LocalBounds.Min.Z,
							LowestCrown->LocalBounds.Min.Z)
							&& Crown->VolumeId < LowestCrown->VolumeId))
					{
						LowestCrown = Crown;
					}
				}
				CoreTarget = FVector2D(LowestCrown->LocalBounds.GetCenter().X,
					LowestCrown->LocalBounds.GetCenter().Y);
			}
			const int32 RasterMinimumX = Component.XGridUnits[0];
			const int32 RasterMaximumX = Component.XGridUnits.Last();
			const int32 RasterMinimumY = Component.YGridUnits[0];
			const int32 RasterMaximumY = Component.YGridUnits.Last();
			TArray<FVector2D> PodiumSupportCells;
			FBox PodiumRasterBounds(EForceInit::ForceInit);
			for (const FBox& Bounds : MergeRegion.GroundSourceBounds)
			{
				PodiumRasterBounds += Bounds;
			}
			if (bUseGroundedCoreHierarchy && PodiumRasterBounds.IsValid)
			{
				const int32 MinimumXCell = FMath::FloorToInt(
					PodiumRasterBounds.Min.X / BlockUnitsCM);
				const int32 MaximumXCell = FMath::CeilToInt(
					PodiumRasterBounds.Max.X / BlockUnitsCM) - 1;
				const int32 MinimumYCell = FMath::FloorToInt(
					PodiumRasterBounds.Min.Y / BlockUnitsCM);
				const int32 MaximumYCell = FMath::CeilToInt(
					PodiumRasterBounds.Max.Y / BlockUnitsCM) - 1;
				for (int32 XCell = MinimumXCell; XCell <= MaximumXCell; ++XCell)
				{
					for (int32 YCell = MinimumYCell; YCell <= MaximumYCell; ++YCell)
					{
						const FVector2D Point(
							(XCell + 0.5) * BlockUnitsCM,
							(YCell + 0.5) * BlockUnitsCM);
						if (MergeRegion.GroundSourceBounds.ContainsByPredicate(
							[&Point](const FBox& Bounds)
							{
								return Point.X >= Bounds.Min.X - GeometryToleranceCM
									&& Point.X <= Bounds.Max.X + GeometryToleranceCM
									&& Point.Y >= Bounds.Min.Y - GeometryToleranceCM
									&& Point.Y <= Bounds.Max.Y + GeometryToleranceCM;
							}))
						{
							PodiumSupportCells.Add(Point);
						}
					}
				}
			}
			if (bUseGroundedCoreHierarchy && PodiumSupportCells.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3PodiumSupportRasterEmpty:Component=%d"), RootIndex);
				return false;
			}
			FVector2D PodiumSupportAnchor = CoreTarget;
			if (!PodiumSupportCells.IsEmpty())
			{
				FVector2D PodiumCentroid = FVector2D::ZeroVector;
				for (const FVector2D& Point : PodiumSupportCells)
				{
					PodiumCentroid += Point;
				}
				PodiumCentroid /= PodiumSupportCells.Num();
				PodiumSupportAnchor = PodiumSupportCells[0];
				double BestAnchorDistance = FVector2D::DistSquared(
					PodiumSupportAnchor, PodiumCentroid);
				for (const FVector2D& Point : PodiumSupportCells)
				{
					const double Distance = FVector2D::DistSquared(Point, PodiumCentroid);
					if (Distance < BestAnchorDistance - UE_DOUBLE_SMALL_NUMBER
						|| (FMath::IsNearlyEqual(Distance, BestAnchorDistance)
							&& (Point.Y < PodiumSupportAnchor.Y
								|| (FMath::IsNearlyEqual(Point.Y, PodiumSupportAnchor.Y)
									&& Point.X < PodiumSupportAnchor.X))))
					{
						PodiumSupportAnchor = Point;
						BestAnchorDistance = Distance;
					}
				}
			}
			const int32 MaximumCoreStationSpan = bUseGroundedCoreHierarchy
				? MaximumHorizontalUnits : 12;
			const int32 MaximumCoreSpanX = FMath::Min(
				MaximumCoreStationSpan, RasterMaximumX - RasterMinimumX);
			const int32 MaximumCoreSpanY = FMath::Min(
				MaximumCoreStationSpan, RasterMaximumY - RasterMinimumY);
			struct FProjectionGridBounds
			{
				int32 MinimumX = 0;
				int32 MaximumX = 0;
				int32 MinimumY = 0;
				int32 MaximumY = 0;
			};
			TArray<FProjectionGridBounds> ProjectionGridBounds;
			ProjectionGridBounds.Reserve(RequiredHighProjectionEntryBounds.Num());
			for (const FBox& ProjectionBounds : RequiredHighProjectionEntryBounds)
			{
				FProjectionGridBounds& GridBounds =
					ProjectionGridBounds.AddDefaulted_GetRef();
				GridBounds.MinimumX = QMin(
					ProjectionBounds.Min.X + BlockUnitsCM * 0.5);
				GridBounds.MaximumX = QMax(
					ProjectionBounds.Max.X - BlockUnitsCM * 0.5);
				GridBounds.MinimumY = QMin(
					ProjectionBounds.Min.Y + BlockUnitsCM * 0.5);
				GridBounds.MaximumY = QMax(
					ProjectionBounds.Max.Y - BlockUnitsCM * 0.5);
			}
			const uint32 RequiredCoverageMask = ProjectionGridBounds.IsEmpty()
				? 0u : (1u << ProjectionGridBounds.Num()) - 1u;
			TArray<int32> RequiredSupportProvinceIds;
			for (const FSupportProvinceDiagnostic& Province : OutPlan.SupportProvinces)
			{
				if (Province.ComponentId == RootIndex)
				{
					RequiredSupportProvinceIds.Add(Province.ProvinceId);
				}
			}
			RequiredSupportProvinceIds.Sort();
			if (bUseGroundedCoreHierarchy
				&& (RequiredSupportProvinceIds.IsEmpty()
					|| RequiredSupportProvinceIds.Num() > 30))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportProvinceCoverageCardinalityInvalid:Component=%d:Provinces=%d"),
					RootIndex, RequiredSupportProvinceIds.Num());
				return false;
			}
			const uint32 RequiredProvinceCoverageMask =
				RequiredSupportProvinceIds.IsEmpty()
					? 0u : (1u << RequiredSupportProvinceIds.Num()) - 1u;
				auto IsBetterPodiumCandidate = [](const FPodiumMainCandidate& A,
				const FPodiumMainCandidate& B)
			{
				if (A.bCoversPodiumSupportAnchor
					!= B.bCoversPodiumSupportAnchor)
				{
					return A.bCoversPodiumSupportAnchor;
				}
				if (A.MarginClass != B.MarginClass)
				{
					return A.MarginClass > B.MarginClass;
				}
				if (A.MinimumSpan != B.MinimumSpan)
				{
					return A.MinimumSpan > B.MinimumSpan;
				}
				if (A.SpanImbalance != B.SpanImbalance)
				{
					return A.SpanImbalance < B.SpanImbalance;
				}
				if (A.Area != B.Area)
				{
					return A.Area > B.Area;
				}
				if (!FMath::IsNearlyEqual(A.Distance, B.Distance))
				{
					return A.Distance < B.Distance;
				}
				return A.MinimumY != B.MinimumY
					? A.MinimumY < B.MinimumY : A.MinimumX < B.MinimumX;
			};
			struct FJointSharedEndpointRequirement
			{
				int32 SpanVolumeId = INDEX_NONE;
				TArray<FJointChildFootprint> Candidates;
			};
			TArray<FJointSharedEndpointRequirement> SharedEndpointRequirements;
			if (bUseGroundedCoreHierarchy)
			{
				for (const FSpanInput& Span : Spans)
				{
					if (Span.Volume == nullptr
						|| (Span.NegativeComponentId != RootIndex
							&& Span.PositiveComponentId != RootIndex))
					{
						continue;
					}
					const bool bNegativeEndpoint =
						Span.NegativeComponentId == RootIndex;
					const int32 HighestIncidentCourse = Span.SharedCourses.IsEmpty()
						? Span.RailBottomCourse : Span.SharedCourses.Last();
					TArray<FSharedEndpointReachabilityDiagnostic> LocalReachability;
					AppendSharedEndpointReachabilityDiagnostics(
						Root, RootIndex, *Span.Volume, bNegativeEndpoint,
						HighestIncidentCourse + 2, LocalReachability);
					const int32 OtherComponentId = bNegativeEndpoint
						? Span.PositiveComponentId : Span.NegativeComponentId;
					TArray<FSharedEndpointReachabilityDiagnostic> OtherReachability;
					AppendSharedEndpointReachabilityDiagnostics(
						Roots[OtherComponentId], OtherComponentId, *Span.Volume,
						!bNegativeEndpoint, HighestIncidentCourse + 2,
						OtherReachability);
					double MinimumOtherContribution = DBL_MAX;
					for (const FSharedEndpointReachabilityDiagnostic& Other
						: OtherReachability)
					{
						if (Other.bReachableInWFC && Other.EndpointInsetCM
							>= -GeometryToleranceCM)
						{
							MinimumOtherContribution = FMath::Min(
								MinimumOtherContribution,
								Other.MinimumCrossContributionCM);
						}
					}
					FJointSharedEndpointRequirement& Requirement =
						SharedEndpointRequirements.AddDefaulted_GetRef();
					Requirement.SpanVolumeId = Span.Volume->VolumeId;
					for (const FSharedEndpointReachabilityDiagnostic& Reachable
						: LocalReachability)
					{
						if (!Reachable.bReachableInWFC
							|| Reachable.EndpointInsetCM < -GeometryToleranceCM
							|| MinimumOtherContribution == DBL_MAX
							|| Reachable.MinimumCrossContributionCM
								+ MinimumOtherContribution
								- (Span.Volume->SpanOpeningMaxCM
									- Span.Volume->SpanOpeningMinCM)
								> 720.0 + GeometryToleranceCM)
						{
							continue;
						}
						FJointChildFootprint Candidate;
						Candidate.MinimumX = FMath::RoundToInt(
							Reachable.CandidateBounds.Min.X / BlockUnitsCM);
						Candidate.MaximumX = FMath::RoundToInt(
							Reachable.CandidateBounds.Max.X / BlockUnitsCM);
						Candidate.MinimumY = FMath::RoundToInt(
							Reachable.CandidateBounds.Min.Y / BlockUnitsCM);
						Candidate.MaximumY = FMath::RoundToInt(
							Reachable.CandidateBounds.Max.Y / BlockUnitsCM);
						Candidate.BaseSource = Reachable.CandidateBaseSourceVolumeId;
						Candidate.TopCourse = HighestIncidentCourse + 2;
						Candidate.XStations = MakeUniformStations(
							Candidate.MinimumX, Candidate.MaximumX, 2);
						Candidate.YStations = MakeUniformStations(
							Candidate.MinimumY, Candidate.MaximumY, 2);
						Requirement.Candidates.Add(MoveTemp(Candidate));
					}
					const FCoreCellPlan* FrozenOtherEndpoint =
						OutPlan.CoreCells.FindByPredicate(
							[OtherComponentId, &Span](const FCoreCellPlan& Candidate)
							{
								return Candidate.ComponentId == OtherComponentId
									&& Candidate.HierarchyRole
										== ECoreHierarchyRole::SharedEndpoint
									&& Candidate.SharedEndpointSpanVolumeId
										== Span.Volume->VolumeId;
							});
					if (FrozenOtherEndpoint != nullptr)
					{
						Requirement.Candidates.Reset();
						const int32 AxisIndex = Span.SpanAxis;
						const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
						const double OpeningLength =
							Span.Volume->SpanOpeningMaxCM
								- Span.Volume->SpanOpeningMinCM;
						const int32 CandidateMinimumX = QMin(
							Root.Bounds.Min.X + BlockUnitsCM * 0.5);
						const int32 CandidateMaximumX = QMax(
							Root.Bounds.Max.X - BlockUnitsCM * 0.5);
						const int32 CandidateMinimumY = QMin(
							Root.Bounds.Min.Y + BlockUnitsCM * 0.5);
						const int32 CandidateMaximumY = QMax(
							Root.Bounds.Max.Y - BlockUnitsCM * 0.5);
						const int32 MaximumSpanX = FMath::Min(MaximumHorizontalUnits,
							CandidateMaximumX - CandidateMinimumX);
						const int32 MaximumSpanY = FMath::Min(MaximumHorizontalUnits,
							CandidateMaximumY - CandidateMinimumY);
						const TArray<int32>& OtherCrossStations = AxisIndex == 0
							? FrozenOtherEndpoint->YStations
							: FrozenOtherEndpoint->XStations;
						const FVector FrozenOtherSize =
							FrozenOtherEndpoint->LocalBounds.GetSize();
						const int32 FrozenOtherMinimumSpan = FMath::RoundToInt(
							FMath::Min(FrozenOtherSize.X, FrozenOtherSize.Y)
								/ BlockUnitsCM);
						const double OtherPhysicalMinimum =
							FrozenOtherEndpoint->LocalBounds.Min[AxisIndex]
								- BlockUnitsCM * 0.5;
						const double OtherPhysicalMaximum =
							FrozenOtherEndpoint->LocalBounds.Max[AxisIndex]
								+ BlockUnitsCM * 0.5;
						const bool bOtherNegative = !bNegativeEndpoint;
						const double OtherOpeningBoundary = bOtherNegative
							? Span.Volume->SpanOpeningMinCM
							: Span.Volume->SpanOpeningMaxCM;
						const double OtherInnerFace = bOtherNegative
							? OtherPhysicalMaximum : OtherPhysicalMinimum;
						const double OtherInset = FMath::Max(0.0, bOtherNegative
							? OtherOpeningBoundary - OtherInnerFace
							: OtherInnerFace - OtherOpeningBoundary);
						const double OtherOuterReach = FMath::Max(0.0, bOtherNegative
							? OtherOpeningBoundary - OtherPhysicalMinimum
							: OtherPhysicalMaximum - OtherOpeningBoundary);
						for (int32 SpanX = 1; SpanX <= MaximumSpanX; ++SpanX)
						{
							for (int32 SpanY = 1; SpanY <= MaximumSpanY; ++SpanY)
							{
								for (int32 MinimumY = CandidateMinimumY;
									MinimumY + SpanY <= CandidateMaximumY; ++MinimumY)
								{
									for (int32 MinimumX = CandidateMinimumX;
										MinimumX + SpanX <= CandidateMaximumX; ++MinimumX)
									{
										++MainCandidatePollCount;
										if ((MainCandidatePollCount & 0xFF) == 0)
										{
											MainCandidateTimer.Checkpoint();
											if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
												TEXT("PodiumMainCandidate"), OutPlan, OutError))
											{
												AppendMainPerformanceEvidence();
												return false;
											}
										}
										const int32 MaximumX = MinimumX + SpanX;
										const int32 MaximumY = MinimumY + SpanY;
										const TArray<int32> XStations = MakeUniformStations(
											MinimumX, MaximumX, 2);
										const TArray<int32> YStations = MakeUniformStations(
											MinimumY, MaximumY, 2);
										const TArray<int32>& CrossStations = AxisIndex == 0
											? YStations : XStations;
										if (CrossStations != OtherCrossStations
											|| FMath::Min(SpanX, SpanY)
												< FrozenOtherMinimumSpan)
										{
											continue;
										}
										const double PhysicalMinimumX =
											(MinimumX - 0.5) * BlockUnitsCM;
										const double PhysicalMaximumX =
											(MaximumX + 0.5) * BlockUnitsCM;
										const double PhysicalMinimumY =
											(MinimumY - 0.5) * BlockUnitsCM;
										const double PhysicalMaximumY =
											(MaximumY + 0.5) * BlockUnitsCM;
										const double CrossMinimum = CrossAxisIndex == 0
											? PhysicalMinimumX : PhysicalMinimumY;
										const double CrossMaximum = CrossAxisIndex == 0
											? PhysicalMaximumX : PhysicalMaximumY;
										if (SkeletonV3OverlapLength(CrossMinimum, CrossMaximum,
											Span.Volume->LocalBounds.Min[CrossAxisIndex],
											Span.Volume->LocalBounds.Max[CrossAxisIndex])
											<= GeometryToleranceCM)
										{
											continue;
										}
										const double CurrentPhysicalMinimum = AxisIndex == 0
											? PhysicalMinimumX : PhysicalMinimumY;
										const double CurrentPhysicalMaximum = AxisIndex == 0
											? PhysicalMaximumX : PhysicalMaximumY;
										const double OpeningBoundary = bNegativeEndpoint
											? Span.Volume->SpanOpeningMinCM
											: Span.Volume->SpanOpeningMaxCM;
										const double InnerFace = bNegativeEndpoint
											? CurrentPhysicalMaximum : CurrentPhysicalMinimum;
										const double CurrentInset = FMath::Max(0.0,
											bNegativeEndpoint
												? OpeningBoundary - InnerFace
												: InnerFace - OpeningBoundary);
										const double CurrentOuterReach = FMath::Max(0.0,
											bNegativeEndpoint
												? OpeningBoundary - CurrentPhysicalMinimum
												: CurrentPhysicalMaximum - OpeningBoundary);
										if (OpeningLength + 2.0 * BlockUnitsCM
												+ CurrentInset + OtherInset
												> 720.0 + GeometryToleranceCM
											|| OpeningLength + CurrentOuterReach + OtherOuterReach
												> 720.0 + GeometryToleranceCM)
										{
											continue;
										}
										const int32 CenterXTwiceUnits = MinimumX + MaximumX;
										const int32 CenterYTwiceUnits = MinimumY + MaximumY;
										const int32 BaseSource = SelectCachedMainSource(
											CenterXTwiceUnits, CenterYTwiceUnits, 0, false);
										if (BaseSource == INDEX_NONE
											|| !Root.GroundSourceVolumeIds.Contains(BaseSource))
										{
											continue;
										}
										bool bCovered = true;
										for (int32 Course = 0;
											Course < HighestIncidentCourse + 2 && bCovered;
											++Course)
										{
											const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
												? EABTSM73BeamAFrameAxis::X
												: EABTSM73BeamAFrameAxis::Y;
											const TArray<int32>& CourseCrossStations =
												Axis == EABTSM73BeamAFrameAxis::X
													? YStations : XStations;
											for (const int32 CrossStation : CourseCrossStations)
											{
												if (!IsCachedMainRailCovered(Course, Axis,
													Axis == EABTSM73BeamAFrameAxis::X
														? MinimumX : MinimumY,
													Axis == EABTSM73BeamAFrameAxis::X
														? MaximumX : MaximumY,
													CrossStation, false))
												{
													bCovered = false;
													break;
												}
											}
										}
										if (!bCovered)
										{
											continue;
										}
										FJointChildFootprint Candidate;
										Candidate.MinimumX = MinimumX;
										Candidate.MaximumX = MaximumX;
										Candidate.MinimumY = MinimumY;
										Candidate.MaximumY = MaximumY;
										Candidate.BaseSource = BaseSource;
										Candidate.TopCourse = HighestIncidentCourse + 2;
										Candidate.XStations = XStations;
										Candidate.YStations = YStations;
										Requirement.Candidates.Add(MoveTemp(Candidate));
									}
								}
							}
						}
					}
				}
			}
			auto FitsIncidentSharedTraversal = [&Spans, &OutPlan, RootIndex,
				DesiredRailCount, bUseGroundedCoreHierarchy,
				&SharedEndpointRequirements, &JointFootprintsConflict](
				const int32 MinimumX, const int32 MaximumX,
				const int32 MinimumY, const int32 MaximumY,
				const TArray<int32>& CandidateXStations,
				const TArray<int32>& CandidateYStations)
			{
				if (CandidateXStations.Num() != DesiredRailCount
					|| CandidateYStations.Num() != DesiredRailCount)
				{
					return false;
				}
				if (bUseGroundedCoreHierarchy)
				{
					for (const FJointSharedEndpointRequirement& Requirement
						: SharedEndpointRequirements)
					{
						const bool bHasCompatibleEndpoint =
							Requirement.Candidates.ContainsByPredicate(
								[MinimumX, MaximumX, MinimumY, MaximumY,
									&CandidateXStations, &CandidateYStations,
									&JointFootprintsConflict](
										const FJointChildFootprint& Endpoint)
								{
									return !JointFootprintsConflict(
										MinimumX, MaximumX, MinimumY, MaximumY,
										CandidateXStations, CandidateYStations,
										Endpoint.MinimumX, Endpoint.MaximumX,
										Endpoint.MinimumY, Endpoint.MaximumY,
										Endpoint.XStations, Endpoint.YStations);
								});
						if (!bHasCompatibleEndpoint)
						{
							return false;
						}
					}
					return true;
				}
				for (const FSpanInput& Span : Spans)
				{
					if (Span.Volume == nullptr
						|| (Span.NegativeComponentId != RootIndex
							&& Span.PositiveComponentId != RootIndex))
					{
						continue;
					}
					const double OpeningLengthCM =
						Span.Volume->SpanOpeningMaxCM - Span.Volume->SpanOpeningMinCM;
					// Each endpoint receives half of the length left after the WFC
					// opening. Reserve another half block because a full-bearing core
					// cap may absorb the adjacent 18 cm shell remnant later.
					const double MaximumEndpointDepthCM =
						(720.0 - OpeningLengthCM) * 0.5 - BlockUnitsCM * 0.5;
					if (MaximumEndpointDepthCM
						< BlockUnitsCM * static_cast<double>(DesiredRailCount - 1)
							- GeometryToleranceCM)
					{
						return false;
					}
					// Core cells are selected component-by-component. Once one endpoint
					// exists, the other endpoint must reuse every physical cross-axis
					// stations; otherwise no single long member can occupy both core
					// slots and a later short bridge would only imitate sharing.
					const int32 OtherComponentId = Span.NegativeComponentId == RootIndex
						? Span.PositiveComponentId : Span.NegativeComponentId;
					const FCoreCellPlan* OtherCore = OutPlan.CoreCells.FindByPredicate(
						[OtherComponentId](const FCoreCellPlan& Core)
						{
							return Core.ComponentId == OtherComponentId;
						});
					if (OtherCore != nullptr)
					{
						const TArray<int32>& CandidateCrossStations = Span.SpanAxis == 0
							? CandidateYStations : CandidateXStations;
						const TArray<int32>& OtherCrossStations = Span.SpanAxis == 0
							? OtherCore->YStations : OtherCore->XStations;
						if (OtherCore->RailCount != DesiredRailCount
							|| CandidateCrossStations != OtherCrossStations)
						{
							return false;
						}
					}
					const int32 CandidateMinimum = Span.SpanAxis == 0
						? MinimumX : MinimumY;
					const int32 CandidateMaximum = Span.SpanAxis == 0
						? MaximumX : MaximumY;
					if (Span.NegativeComponentId == RootIndex)
					{
						const double OuterPhysicalPlaneCM =
							CandidateMinimum * static_cast<double>(BlockUnitsCM)
							- BlockUnitsCM * 0.5;
						if (Span.Volume->SpanOpeningMinCM - OuterPhysicalPlaneCM
							> MaximumEndpointDepthCM + GeometryToleranceCM)
						{
							return false;
						}
					}
					if (Span.PositiveComponentId == RootIndex)
					{
						const double OuterPhysicalPlaneCM =
							CandidateMaximum * static_cast<double>(BlockUnitsCM)
							+ BlockUnitsCM * 0.5;
						if (OuterPhysicalPlaneCM - Span.Volume->SpanOpeningMaxCM
							> MaximumEndpointDepthCM + GeometryToleranceCM)
						{
							return false;
						}
					}
				}
				return true;
			};
			for (int32 SpanX = DesiredRailCount - 1;
				SpanX <= MaximumCoreSpanX; ++SpanX)
			{
				for (int32 SpanY = DesiredRailCount - 1;
					SpanY <= MaximumCoreSpanY; ++SpanY)
				{
					for (int32 MinimumY = RasterMinimumY;
						MinimumY + SpanY <= RasterMaximumY; ++MinimumY)
					{
						for (int32 MinimumX = RasterMinimumX;
							MinimumX + SpanX <= RasterMaximumX; ++MinimumX)
						{
							++MainCandidatePollCount;
							if ((MainCandidatePollCount & 0xFF) == 0)
							{
								MainCandidateTimer.Checkpoint();
								if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
									TEXT("PodiumMainCandidate"), OutPlan, OutError))
								{
									AppendMainPerformanceEvidence();
									return false;
								}
							}
							const int32 MaximumX = MinimumX + SpanX;
							const int32 MaximumY = MinimumY + SpanY;
							const TArray<int32> CandidateXStations = MakeUniformStations(
								MinimumX, MaximumX, DesiredRailCount);
							const TArray<int32> CandidateYStations = MakeUniformStations(
								MinimumY, MaximumY, DesiredRailCount);
							uint32 HighProjectionCoverageMask = 0;
							for (int32 ProjectionIndex = 0;
								ProjectionIndex < RequiredHighProjectionEntryBounds.Num();
								++ProjectionIndex)
							{
								const FProjectionGridBounds& GridBounds =
									ProjectionGridBounds[ProjectionIndex];
								const bool bHasInteriorXStation =
									CandidateXStations.ContainsByPredicate(
										[&GridBounds](
											const int32 Station)
										{
											return Station > GridBounds.MinimumX
												&& Station < GridBounds.MaximumX;
										});
								const bool bHasInteriorYStation =
									CandidateYStations.ContainsByPredicate(
										[&GridBounds](
											const int32 Station)
										{
											return Station > GridBounds.MinimumY
												&& Station < GridBounds.MaximumY;
										});
								if (bHasInteriorXStation && bHasInteriorYStation)
								{
									HighProjectionCoverageMask |= 1u << ProjectionIndex;
								}
							}
							const bool bCoversPodiumSupportAnchor =
								PodiumSupportAnchor.X
									>= MinimumX * static_cast<double>(BlockUnitsCM)
										- GeometryToleranceCM
								&& PodiumSupportAnchor.X
									<= MaximumX * static_cast<double>(BlockUnitsCM)
										+ GeometryToleranceCM
								&& PodiumSupportAnchor.Y
									>= MinimumY * static_cast<double>(BlockUnitsCM)
										- GeometryToleranceCM
								&& PodiumSupportAnchor.Y
									<= MaximumY * static_cast<double>(BlockUnitsCM)
										+ GeometryToleranceCM;
							if (!bUseGroundedCoreHierarchy
									&& HighProjectionCoverageMask != RequiredCoverageMask)
							{
								continue;
							}
							if (!FitsIncidentSharedTraversal(
								MinimumX, MaximumX, MinimumY, MaximumY,
								CandidateXStations, CandidateYStations))
							{
								continue;
							}
							const int32 CenterXTwiceUnits = MinimumX + MaximumX;
							const int32 CenterYTwiceUnits = MinimumY + MaximumY;
							const double CenterX = CenterXTwiceUnits * 0.5 * BlockUnitsCM;
							const double CenterY = CenterYTwiceUnits * 0.5 * BlockUnitsCM;
							const int32 BaseSource = SelectCachedMainSource(
								CenterXTwiceUnits, CenterYTwiceUnits, 0, false);
							if (BaseSource == INDEX_NONE
								|| !Root.GroundSourceVolumeIds.Contains(BaseSource))
							{
								continue;
							}
							TSet<int32> FootprintGroundComponents;
							bool bTouchesGroundComponent = false;
							for (const FABTSM73DAG5BV2Volume* GroundVolume : Root.BodyVolumes)
							{
								if (GroundVolume == nullptr
									|| !Root.GroundSourceVolumeIds.Contains(GroundVolume->VolumeId))
								{
									continue;
								}
								const double XOverlap = FMath::Min(
									MaximumX * static_cast<double>(BlockUnitsCM),
									GroundVolume->LocalBounds.Max.X)
									- FMath::Max(MinimumX * static_cast<double>(BlockUnitsCM),
										GroundVolume->LocalBounds.Min.X);
								const double YOverlap = FMath::Min(
									MaximumY * static_cast<double>(BlockUnitsCM),
									GroundVolume->LocalBounds.Max.Y)
									- FMath::Max(MinimumY * static_cast<double>(BlockUnitsCM),
										GroundVolume->LocalBounds.Min.Y);
								if (XOverlap > GeometryToleranceCM
									&& YOverlap > GeometryToleranceCM)
								{
									bTouchesGroundComponent = true;
									if (bUseGroundedCoreHierarchy)
									{
										break;
									}
									if (const int32* OriginalComponent =
										Root.SourceVolumeOriginalComponentIds.Find(
											GroundVolume->VolumeId))
									{
										FootprintGroundComponents.Add(*OriginalComponent);
									}
								}
							}
							if ((!bUseGroundedCoreHierarchy
									&& FootprintGroundComponents.Num()
										!= Root.SourceGroundComponentIds.Num())
								|| (bUseGroundedCoreHierarchy
									&& !bTouchesGroundComponent))
							{
								continue;
							}
							const int32 Margin = FMath::Min(
								FMath::Min(MinimumX - RasterMinimumX,
									RasterMaximumX - MaximumX),
								FMath::Min(MinimumY - RasterMinimumY,
									RasterMaximumY - MaximumY));
							const int32 MarginClass = Margin >= 1 ? 1 : 0;
							const int32 MinimumSpan = FMath::Min(SpanX, SpanY);
							const int32 SpanImbalance = FMath::Abs(SpanX - SpanY);
							const int32 Area = SpanX * SpanY;
							const double Distance = FVector2D::DistSquared(
								FVector2D(CenterX, CenterY), CoreTarget);
							FPodiumMainCandidate GroundedCandidate;
							uint64 GroundedRetentionKey = 0;
							if (bUseGroundedCoreHierarchy)
							{
								GroundedCandidate.MinimumX = MinimumX;
								GroundedCandidate.MaximumX = MaximumX;
								GroundedCandidate.MinimumY = MinimumY;
								GroundedCandidate.MaximumY = MaximumY;
								GroundedCandidate.BaseSource = BaseSource;
								GroundedCandidate.MarginClass = MarginClass;
								GroundedCandidate.MinimumSpan = MinimumSpan;
								GroundedCandidate.SpanImbalance = SpanImbalance;
								GroundedCandidate.Area = Area;
								GroundedCandidate.Distance = Distance;
								GroundedCandidate.CoverageMask =
									HighProjectionCoverageMask;
								for (int32 ProvinceIndex = 0;
									ProvinceIndex < RequiredSupportProvinceIds.Num();
									++ProvinceIndex)
								{
									const int32 ProvinceId =
										RequiredSupportProvinceIds[ProvinceIndex];
									if (OutPlan.SupportProvinces.IsValidIndex(ProvinceId))
									{
										const FSupportProvinceDiagnostic& Province =
											OutPlan.SupportProvinces[ProvinceId];
										if (Province.AnchorXUnit >= MinimumX
											&& Province.AnchorXUnit <= MaximumX
											&& Province.AnchorYUnit >= MinimumY
											&& Province.AnchorYUnit <= MaximumY)
										{
											GroundedCandidate.ProvinceCoverageMask
												|= 1u << ProvinceIndex;
										}
									}
								}
								GroundedCandidate.XStations = CandidateXStations;
								GroundedCandidate.YStations = CandidateYStations;
								for (int32 ProjectionIndex = 0;
									ProjectionIndex
										< FullHeightChildCandidatesByProjection.Num();
									++ProjectionIndex)
								{
									const bool bSupportsFullHeightChild =
										FullHeightChildCandidatesByProjection[ProjectionIndex]
											.ContainsByPredicate(
											[&GroundedCandidate, &JointFootprintsConflict,
												&MainChildCompatibilityProbeCount](
												const FJointChildFootprint& Child)
											{
												++MainChildCompatibilityProbeCount;
												return !JointFootprintsConflict(
													GroundedCandidate.MinimumX,
													GroundedCandidate.MaximumX,
													GroundedCandidate.MinimumY,
													GroundedCandidate.MaximumY,
													GroundedCandidate.XStations,
													GroundedCandidate.YStations,
													Child.MinimumX, Child.MaximumX,
													Child.MinimumY, Child.MaximumY,
													Child.XStations, Child.YStations);
											});
									if (bSupportsFullHeightChild)
									{
										GroundedCandidate.FullHeightCompatibilityMask
											|= 1u << ProjectionIndex;
									}
								}
								GroundedCandidate.bCoversPodiumSupportAnchor =
									bCoversPodiumSupportAnchor;
								if (GroundedCandidate.FullHeightCompatibilityMask == 0u
									&& GroundedCandidate.ProvinceCoverageMask == 0u
									&& !GroundedCandidate.bCoversPodiumSupportAnchor)
								{
									continue;
								}
								GroundedRetentionKey =
									(static_cast<uint64>(GroundedCandidate.CoverageMask) << 32)
									| GroundedCandidate.FullHeightCompatibilityMask;
								constexpr int32 MaximumCandidatesPerCoverage = 12;
								const TMap<uint64, TArray<FPodiumMainCandidate>>*
									SameProvinceCoverage =
										PodiumCandidatesByProvinceAndCoverage.Find(
											GroundedCandidate.ProvinceCoverageMask);
								if (const TArray<FPodiumMainCandidate>* SameCoverage =
									SameProvinceCoverage != nullptr
										? SameProvinceCoverage->Find(GroundedRetentionKey)
										: nullptr;
									SameCoverage != nullptr
									&& SameCoverage->Num() >= MaximumCandidatesPerCoverage
									&& IsBetterPodiumCandidate(
										SameCoverage->Last(), GroundedCandidate))
								{
									continue;
								}
							}
							bool bCompleteCoreStack = true;
							for (int32 Course = 0;
								Course < CoreTopCourse && bCompleteCoreStack; ++Course)
							{
								const int32 CourseSource = SelectCachedMainSource(
									CenterXTwiceUnits, CenterYTwiceUnits, Course,
									Course >= BodyTopCourse);
								if (CourseSource == INDEX_NONE)
								{
									bCompleteCoreStack = false;
									break;
								}
								const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
									? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
								const TArray<int32>& CrossStations =
									Axis == EABTSM73BeamAFrameAxis::X
										? CandidateYStations : CandidateXStations;
								for (const int32 CrossStation : CrossStations)
								{
									if (!IsCachedMainRailCovered(Course, Axis,
										Axis == EABTSM73BeamAFrameAxis::X
											? MinimumX : MinimumY,
										Axis == EABTSM73BeamAFrameAxis::X
											? MaximumX : MaximumY,
										CrossStation, Course < BodyTopCourse))
									{
										bCompleteCoreStack = false;
										break;
									}
								}
							}
							if (!bCompleteCoreStack)
							{
								continue;
							}
							++MainCompleteStackCandidateCount;
							if (bUseGroundedCoreHierarchy)
							{
								TArray<FPodiumMainCandidate>& SameCoverage =
									PodiumCandidatesByProvinceAndCoverage.FindOrAdd(
										GroundedCandidate.ProvinceCoverageMask).FindOrAdd(
											GroundedRetentionKey);
								SameCoverage.Add(MoveTemp(GroundedCandidate));
								SameCoverage.Sort(IsBetterPodiumCandidate);
								constexpr int32 MaximumCandidatesPerCoverage = 12;
								if (SameCoverage.Num() > MaximumCandidatesPerCoverage)
								{
									SameCoverage.SetNum(MaximumCandidatesPerCoverage);
								}
								continue;
							}
							bool bBetter = MarginClass > BestCoreMarginClass;
							if (MarginClass == BestCoreMarginClass)
							{
								bBetter = MinimumSpan > BestCoreMinimumSpan;
								if (MinimumSpan == BestCoreMinimumSpan)
								{
									bBetter = SpanImbalance < BestCoreSpanImbalance;
									if (SpanImbalance == BestCoreSpanImbalance)
									{
										bBetter = Area > BestCoreArea;
										if (Area == BestCoreArea)
										{
											bBetter = Distance < BestCoreDistance - UE_DOUBLE_SMALL_NUMBER;
											if (FMath::IsNearlyEqual(Distance, BestCoreDistance))
											{
												bBetter = CoreMinimumYUnits == INDEX_NONE
													|| MinimumY < CoreMinimumYUnits
													|| (MinimumY == CoreMinimumYUnits
														&& MinimumX < CoreMinimumXUnits);
											}
										}
									}
								}
							}
							if (bBetter)
							{
								CoreMinimumXUnits = MinimumX;
								CoreMaximumXUnits = MaximumX;
								CoreMinimumYUnits = MinimumY;
								CoreMaximumYUnits = MaximumY;
								CoreBaseSource = BaseSource;
								BestCoreMarginClass = MarginClass;
								BestCoreMinimumSpan = MinimumSpan;
								BestCoreSpanImbalance = SpanImbalance;
								BestCoreArea = Area;
								BestCoreDistance = Distance;
							}
						}
					}
				}
			}
			MainCandidateTimer.Stop();
			int32 MainRetentionBucketCount = 0;
			for (const TPair<uint32,
				TMap<uint64, TArray<FPodiumMainCandidate>>>& ProvincePair
				: PodiumCandidatesByProvinceAndCoverage)
			{
				MainRetentionBucketCount += ProvincePair.Value.Num();
			}
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-C3V3][Stage1MainSearch] Component=%d States=%lld Complete=%lld RailCache=%d RailHit=%lld RailMiss=%lld SourceCache=%d SourceHit=%lld SourceMiss=%lld ChildCompatibilityProbes=%lld RetentionBuckets=%d"),
				RootIndex, MainCandidatePollCount, MainCompleteStackCandidateCount,
				MainRailCoverageCache.Num(), MainRailCoverageCacheHitCount,
				MainRailCoverageCacheMissCount, MainSourceProbeCache.Num(),
				MainSourceProbeCacheHitCount, MainSourceProbeCacheMissCount,
				MainChildCompatibilityProbeCount,
				MainRetentionBucketCount);
			if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
				TEXT("PodiumMainCandidate"), OutPlan, OutError))
			{
				AppendMainPerformanceEvidence();
				return false;
			}
			FStage1PhaseTimer JointSelectionTimer(
				OutPlan.Summary.JointSelectionMilliseconds);
			if (bUseGroundedCoreHierarchy)
			{
				if (RequiredHighProjectionEntryBounds.IsEmpty()
					|| RequiredHighProjectionEntryBounds.Num() > 30)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3HighProjectionCoverageCardinalityInvalid:Component=%d:Regions=%d"),
						RootIndex, RequiredHighProjectionEntryBounds.Num());
					return false;
				}
				TArray<FPodiumMainCandidate> AllCandidates;
				for (const TPair<uint32,
					TMap<uint64, TArray<FPodiumMainCandidate>>>& ProvincePair
					: PodiumCandidatesByProvinceAndCoverage)
				{
					for (const TPair<uint64, TArray<FPodiumMainCandidate>>& Pair
						: ProvincePair.Value)
					{
						AllCandidates.Append(Pair.Value);
					}
				}
				const int32 RetainedMainCandidateCountBeforeCompatibilityPrune =
					AllCandidates.Num();
				AllCandidates.RemoveAll([RequiredCoverageMask](
					const FPodiumMainCandidate& Candidate)
				{
					return (Candidate.FullHeightCompatibilityMask
						& RequiredCoverageMask) != RequiredCoverageMask;
				});
				const int32 RejectedPartialCompatibilityMainCount =
					RetainedMainCandidateCountBeforeCompatibilityPrune
						- AllCandidates.Num();
				int64 IndividualSharedEndpointCompatibilityProbeCount = 0;
				const int32 MainCandidateCountBeforeSharedEndpointPrune =
					AllCandidates.Num();
				AllCandidates.RemoveAll([
					&SharedEndpointRequirements, &JointFootprintsConflict,
					&IndividualSharedEndpointCompatibilityProbeCount](
						const FPodiumMainCandidate& Candidate)
				{
					for (const FJointSharedEndpointRequirement& Requirement
						: SharedEndpointRequirements)
					{
						const bool bHasCompatibleEndpoint =
							Requirement.Candidates.ContainsByPredicate(
							[&Candidate, &JointFootprintsConflict,
								&IndividualSharedEndpointCompatibilityProbeCount](
								const FJointChildFootprint& Endpoint)
							{
								++IndividualSharedEndpointCompatibilityProbeCount;
								return !JointFootprintsConflict(
									Candidate.MinimumX, Candidate.MaximumX,
									Candidate.MinimumY, Candidate.MaximumY,
									Candidate.XStations, Candidate.YStations,
									Endpoint.MinimumX, Endpoint.MaximumX,
									Endpoint.MinimumY, Endpoint.MaximumY,
									Endpoint.XStations, Endpoint.YStations);
							});
						if (!bHasCompatibleEndpoint)
						{
							return true;
						}
					}
					return false;
				});
				const int32 RejectedIndividualSharedEndpointMainCount =
					MainCandidateCountBeforeSharedEndpointPrune
						- AllCandidates.Num();
				AllCandidates.Sort([&IsBetterPodiumCandidate](
					const FPodiumMainCandidate& A, const FPodiumMainCandidate& B)
				{
					const int32 ACoverage = FPlatformMath::CountBits(A.CoverageMask);
					const int32 BCoverage = FPlatformMath::CountBits(B.CoverageMask);
					if (ACoverage != BCoverage)
					{
						return ACoverage > BCoverage;
					}
					const int32 AProvinceCoverage = FPlatformMath::CountBits(
						A.ProvinceCoverageMask);
					const int32 BProvinceCoverage = FPlatformMath::CountBits(
						B.ProvinceCoverageMask);
					if (AProvinceCoverage != BProvinceCoverage)
					{
						return AProvinceCoverage > BProvinceCoverage;
					}
					const int32 ACompatibility = FPlatformMath::CountBits(
						A.FullHeightCompatibilityMask);
					const int32 BCompatibility = FPlatformMath::CountBits(
						B.FullHeightCompatibilityMask);
					if (ACompatibility != BCompatibility)
					{
						return ACompatibility > BCompatibility;
					}
					return IsBetterPodiumCandidate(A, B);
				});
				auto CandidatesConflict = [&JointFootprintsConflict](
					const FPodiumMainCandidate& A,
					const FPodiumMainCandidate& B)
				{
					return JointFootprintsConflict(
						A.MinimumX, A.MaximumX, A.MinimumY, A.MaximumY,
						A.XStations, A.YStations,
						B.MinimumX, B.MaximumX, B.MinimumY, B.MaximumY,
						B.XStations, B.YStations);
				};
				int64 PrecomputedMainChildCompatibilityProbeCount = 0;
				for (FPodiumMainCandidate& Main : AllCandidates)
				{
					Main.CompatibleChildWordsByProjection.SetNum(
						FullHeightChildCandidatesByProjection.Num());
				}
				auto EnsureMainChildCompatibilityWords = [
					&AllCandidates, &FullHeightChildCandidatesByProjection,
					&JointFootprintsConflict,
					&PrecomputedMainChildCompatibilityProbeCount](
						const int32 MainIndex, const int32 ProjectionIndex)
						-> const TArray<uint64>*
				{
					if (!AllCandidates.IsValidIndex(MainIndex)
						|| !FullHeightChildCandidatesByProjection.IsValidIndex(
							ProjectionIndex))
					{
						return nullptr;
					}
					FPodiumMainCandidate& Main = AllCandidates[MainIndex];
					if (!Main.CompatibleChildWordsByProjection.IsValidIndex(
						ProjectionIndex))
					{
						return nullptr;
					}
					TArray<uint64>& CompatibleWords =
						Main.CompatibleChildWordsByProjection[ProjectionIndex];
					if (CompatibleWords.IsEmpty())
					{
						const TArray<FJointChildFootprint>& Children =
							FullHeightChildCandidatesByProjection[ProjectionIndex];
						CompatibleWords.SetNumZeroed((Children.Num() + 63) / 64);
						for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
						{
							const FJointChildFootprint& Child = Children[ChildIndex];
							++PrecomputedMainChildCompatibilityProbeCount;
							if (!JointFootprintsConflict(
								Main.MinimumX, Main.MaximumX,
								Main.MinimumY, Main.MaximumY,
								Main.XStations, Main.YStations,
								Child.MinimumX, Child.MaximumX,
								Child.MinimumY, Child.MaximumY,
								Child.XStations, Child.YStations))
							{
								CompatibleWords[ChildIndex >> 6]
									|= uint64(1) << (ChildIndex & 63);
							}
						}
					}
					return &CompatibleWords;
				};
				const uint32 FullRegionCoverageMask =
					(1u << RequiredHighProjectionEntryBounds.Num()) - 1u;
				const uint64 FullJointCoverageMask =
					static_cast<uint64>(FullRegionCoverageMask)
					| (static_cast<uint64>(RequiredProvinceCoverageMask) << 32);
				FJointCoreSelectionDiagnostic& JointDiagnostic =
					OutPlan.JointCoreSelectionDiagnostics.AddDefaulted_GetRef();
				JointDiagnostic.ComponentId = RootIndex;
				JointDiagnostic.HighProjectionRegionCount =
					RequiredHighProjectionEntryBounds.Num();
				JointDiagnostic.SupportProvinceCount =
					RequiredSupportProvinceIds.Num();
				JointDiagnostic.PodiumMainCandidateCount = AllCandidates.Num();
				JointDiagnostic.MainCandidateWithoutFullHeightCompatibilityCount =
					RejectedPartialCompatibilityMainCount;
				JointDiagnostic.CompatibleMainCandidateCountByRegion.SetNumZeroed(
					RequiredHighProjectionEntryBounds.Num());
				JointDiagnostic.PodiumCoverageMainCandidateCountByRegion.SetNumZeroed(
					RequiredHighProjectionEntryBounds.Num());
				JointDiagnostic.MainCandidateCountBySupportProvince.SetNumZeroed(
					RequiredSupportProvinceIds.Num());
				for (const FPodiumMainCandidate& Candidate : AllCandidates)
				{
					for (int32 ProjectionIndex = 0;
						ProjectionIndex
							< JointDiagnostic.CompatibleMainCandidateCountByRegion.Num();
						++ProjectionIndex)
					{
						JointDiagnostic.CompatibleMainCandidateCountByRegion[ProjectionIndex]
							+= (Candidate.FullHeightCompatibilityMask
								& (1u << ProjectionIndex)) != 0u ? 1 : 0;
						JointDiagnostic.PodiumCoverageMainCandidateCountByRegion[ProjectionIndex]
							+= (Candidate.CoverageMask
								& (1u << ProjectionIndex)) != 0u ? 1 : 0;
					}
					for (int32 ProvinceIndex = 0;
						ProvinceIndex
							< JointDiagnostic.MainCandidateCountBySupportProvince.Num();
						++ProvinceIndex)
					{
						JointDiagnostic.MainCandidateCountBySupportProvince[ProvinceIndex]
							+= (Candidate.ProvinceCoverageMask
								& (1u << ProvinceIndex)) != 0u ? 1 : 0;
					}
				}
				TArray<int32> CurrentSelection;
				TArray<int32> BestSelection;
				bool bBestSelectionCoversPodiumSupportAnchor = false;
				auto MainSelectionKey = [](const TArray<int32>& MainSelection)
				{
					TArray<int32> Canonical = MainSelection;
					Canonical.Sort();
					return Canonical;
				};
				auto JoinIntegerArray = [](const TArray<int32>& Values)
				{
					return FString::JoinBy(Values, TEXT(","),
						[](const int32 Value)
						{
							return FString::FromInt(Value);
						});
				};
				TMap<TArray<int32>, bool> FullHeightSelectionCache;
				int64 FullHeightFeasibilityCallCount = 0;
				int64 FullHeightMainConflictProbeCount = 0;
				int64 FullHeightSiblingConflictProbeCount = 0;
				int64 FullHeightAssignmentCandidateVisitCount = 0;
				auto SelectionSupportsAllFullHeightChildren = [
					&AllCandidates, &FullHeightChildCandidatesByProjection,
					&JointFootprintsConflict,
					&EnsureMainChildCompatibilityWords,
					&MainSelectionKey, &FullHeightSelectionCache,
					&FullHeightFeasibilityCallCount,
					&FullHeightMainConflictProbeCount,
					&FullHeightSiblingConflictProbeCount,
					&FullHeightAssignmentCandidateVisitCount](
						const TArray<int32>& MainSelection)
				{
					++FullHeightFeasibilityCallCount;
					const TArray<int32> SelectionKey = MainSelectionKey(MainSelection);
					if (const bool* Cached = FullHeightSelectionCache.Find(SelectionKey))
					{
						return *Cached;
					}
					TArray<TArray<const FJointChildFootprint*>>
						CompatibleChildrenByProjection;
					CompatibleChildrenByProjection.SetNum(
						FullHeightChildCandidatesByProjection.Num());
					for (int32 ProjectionIndex = 0;
						ProjectionIndex < FullHeightChildCandidatesByProjection.Num();
						++ProjectionIndex)
					{
						TArray<uint64> CompatibleWords;
						bool bInitializedWords = false;
						for (const int32 MainIndex : MainSelection)
						{
							const TArray<uint64>* MainWordsPtr =
								EnsureMainChildCompatibilityWords(
									MainIndex, ProjectionIndex);
							if (MainWordsPtr == nullptr)
							{
								FullHeightSelectionCache.Add(SelectionKey, false);
								return false;
							}
							const TArray<uint64>& MainWords = *MainWordsPtr;
							if (!bInitializedWords)
							{
								CompatibleWords = MainWords;
								bInitializedWords = true;
							}
							else if (CompatibleWords.Num() != MainWords.Num())
							{
								FullHeightSelectionCache.Add(SelectionKey, false);
								return false;
							}
							else
							{
								for (int32 WordIndex = 0;
									WordIndex < CompatibleWords.Num(); ++WordIndex)
								{
									CompatibleWords[WordIndex] &= MainWords[WordIndex];
									++FullHeightMainConflictProbeCount;
								}
							}
						}
						const TArray<FJointChildFootprint>& Children =
							FullHeightChildCandidatesByProjection[ProjectionIndex];
						for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
						{
							if (CompatibleWords.IsValidIndex(ChildIndex >> 6)
								&& (CompatibleWords[ChildIndex >> 6]
									& (uint64(1) << (ChildIndex & 63))) != 0)
							{
								CompatibleChildrenByProjection[ProjectionIndex].Add(
									&Children[ChildIndex]);
							}
						}
						if (CompatibleChildrenByProjection[ProjectionIndex].IsEmpty())
						{
							FullHeightSelectionCache.Add(SelectionKey, false);
							return false;
						}
					}
					TArray<int32> ProjectionOrder;
					for (int32 ProjectionIndex = 0;
						ProjectionIndex < CompatibleChildrenByProjection.Num();
						++ProjectionIndex)
					{
						ProjectionOrder.Add(ProjectionIndex);
					}
					ProjectionOrder.Sort([&CompatibleChildrenByProjection](
						const int32 A, const int32 B)
					{
						const int32 ACount = CompatibleChildrenByProjection[A].Num();
						const int32 BCount = CompatibleChildrenByProjection[B].Num();
						return ACount != BCount ? ACount < BCount : A < B;
					});
					TArray<const FJointChildFootprint*> SelectedChildren;
					TFunction<bool(int32)> AssignChild = [&](const int32 OrderIndex)
					{
						if (OrderIndex >= ProjectionOrder.Num())
						{
							return true;
						}
						const int32 ProjectionIndex = ProjectionOrder[OrderIndex];
						// FullHeightCandidates are ordered from largest/best-looking to
						// smallest. This predicate only proves that an assignment exists;
						// it does not choose the emitted child. Visit the least-conflicting
						// (normally smallest) witnesses first so adjacent terminal regions
						// do not enumerate a large Cartesian prefix before finding the same
						// exact feasibility result.
						const TArray<const FJointChildFootprint*>& CompatibleChildren =
							CompatibleChildrenByProjection[ProjectionIndex];
						for (int32 ChildIndex = CompatibleChildren.Num() - 1;
							ChildIndex >= 0; --ChildIndex)
						{
							const FJointChildFootprint* Child =
								CompatibleChildren[ChildIndex];
							++FullHeightAssignmentCandidateVisitCount;
							if (Child == nullptr)
							{
								continue;
							}
							const bool bConflictsSibling =
								SelectedChildren.ContainsByPredicate(
									[Child, &JointFootprintsConflict,
										&FullHeightSiblingConflictProbeCount](
										const FJointChildFootprint* Sibling)
									{
										++FullHeightSiblingConflictProbeCount;
										return Sibling != nullptr
											&& JointFootprintsConflict(
												Child->MinimumX, Child->MaximumX,
												Child->MinimumY, Child->MaximumY,
												Child->XStations, Child->YStations,
												Sibling->MinimumX, Sibling->MaximumX,
												Sibling->MinimumY, Sibling->MaximumY,
												Sibling->XStations, Sibling->YStations);
									});
							if (bConflictsSibling)
							{
								continue;
							}
							SelectedChildren.Add(Child);
							if (AssignChild(OrderIndex + 1))
							{
								return true;
							}
							SelectedChildren.Pop(EAllowShrinking::No);
						}
						return false;
					};
					const bool bSupported = AssignChild(0);
					FullHeightSelectionCache.Add(SelectionKey, bSupported);
					return bSupported;
				};
				auto SelectionSupportsAllSharedEndpoints = [
					&AllCandidates, &SharedEndpointRequirements,
					&JointFootprintsConflict](
						const TArray<int32>& MainSelection)
				{
					for (const FJointSharedEndpointRequirement& Requirement
						: SharedEndpointRequirements)
					{
						bool bRequirementSatisfied = false;
						for (const FJointChildFootprint& Endpoint
							: Requirement.Candidates)
						{
							bool bConflictsMain = false;
							for (const int32 MainIndex : MainSelection)
							{
								if (!AllCandidates.IsValidIndex(MainIndex))
								{
									return false;
								}
								const FPodiumMainCandidate& Main = AllCandidates[MainIndex];
								bConflictsMain |= JointFootprintsConflict(
									Main.MinimumX, Main.MaximumX,
									Main.MinimumY, Main.MaximumY,
									Main.XStations, Main.YStations,
									Endpoint.MinimumX, Endpoint.MaximumX,
									Endpoint.MinimumY, Endpoint.MaximumY,
									Endpoint.XStations, Endpoint.YStations);
							}
							if (!bConflictsMain)
							{
								bRequirementSatisfied = true;
								break;
							}
						}
						if (!bRequirementSatisfied)
						{
							return false;
						}
					}
					return true;
				};
				TSet<TArray<int32>> VisitedMainSelectionStates;
				TArray<uint64> CandidateJointCoverageMasks;
				CandidateJointCoverageMasks.Reserve(AllCandidates.Num());
				TArray<TArray<int32>> CandidateIndicesByRequirement;
				CandidateIndicesByRequirement.SetNum(
					RequiredHighProjectionEntryBounds.Num()
					+ RequiredSupportProvinceIds.Num());
				for (int32 CandidateIndex = 0;
					CandidateIndex < AllCandidates.Num(); ++CandidateIndex)
				{
					const uint64 CandidateJointCoverage =
						static_cast<uint64>(AllCandidates[CandidateIndex].CoverageMask)
						| (static_cast<uint64>(
							AllCandidates[CandidateIndex].ProvinceCoverageMask) << 32);
					CandidateJointCoverageMasks.Add(CandidateJointCoverage);
					for (int32 RegionIndex = 0;
						RegionIndex < RequiredHighProjectionEntryBounds.Num();
						++RegionIndex)
					{
						if ((CandidateJointCoverage & (1ull << RegionIndex)) != 0ull)
						{
							CandidateIndicesByRequirement[RegionIndex].Add(CandidateIndex);
						}
					}
					for (int32 ProvinceIndex = 0;
						ProvinceIndex < RequiredSupportProvinceIds.Num();
						++ProvinceIndex)
					{
						if ((AllCandidates[CandidateIndex].ProvinceCoverageMask
							& (1u << ProvinceIndex)) != 0u)
						{
							CandidateIndicesByRequirement[
								RequiredHighProjectionEntryBounds.Num()
									+ ProvinceIndex].Add(CandidateIndex);
						}
					}
				}
				// Seed branch-and-bound with a deterministic feasible witness. This does
				// not replace the exact search below: it only gives that search an early
				// cardinality upper bound. Every witness still passes the same full-height
				// child, sibling and shared-endpoint contracts.
				auto TryGreedyMainSelection = [
					&AllCandidates, &CandidateJointCoverageMasks,
					&CandidateIndicesByRequirement,
					&RequiredHighProjectionEntryBounds,
					FullJointCoverageMask, &CandidatesConflict,
					&SelectionSupportsAllFullHeightChildren,
					&SelectionSupportsAllSharedEndpoints,
					&BestSelection, &bBestSelectionCoversPodiumSupportAnchor,
					&JointDiagnostic](const int32 SeedCandidateIndex)
				{
					TArray<int32> GreedySelection;
					uint64 GreedyCoveredMask = 0ull;
					if (SeedCandidateIndex != INDEX_NONE)
					{
						if (!AllCandidates.IsValidIndex(SeedCandidateIndex))
						{
							return;
						}
						GreedySelection.Add(SeedCandidateIndex);
						GreedyCoveredMask |=
							CandidateJointCoverageMasks[SeedCandidateIndex];
					}
					while (GreedyCoveredMask != FullJointCoverageMask)
					{
						int32 RarestRequirementIndex = INDEX_NONE;
						int32 RarestAvailableCount = MAX_int32;
						for (int32 RequirementIndex = 0;
							RequirementIndex < CandidateIndicesByRequirement.Num();
							++RequirementIndex)
						{
							const int32 BitIndex = RequirementIndex
								< RequiredHighProjectionEntryBounds.Num()
								? RequirementIndex
								: 32 + RequirementIndex
									- RequiredHighProjectionEntryBounds.Num();
							if ((GreedyCoveredMask & (1ull << BitIndex)) != 0ull)
							{
								continue;
							}
							int32 AvailableCount = 0;
							for (const int32 CandidateIndex
								: CandidateIndicesByRequirement[RequirementIndex])
							{
								if (!GreedySelection.Contains(CandidateIndex)
									&& !GreedySelection.ContainsByPredicate(
										[CandidateIndex, &AllCandidates,
											&CandidatesConflict](const int32 SelectedIndex)
										{
											return CandidatesConflict(
												AllCandidates[CandidateIndex],
												AllCandidates[SelectedIndex]);
										}))
								{
									++AvailableCount;
								}
							}
							if (AvailableCount < RarestAvailableCount)
							{
								RarestRequirementIndex = RequirementIndex;
								RarestAvailableCount = AvailableCount;
							}
						}
						if (RarestRequirementIndex == INDEX_NONE
							|| RarestAvailableCount <= 0)
						{
							return;
						}
						int32 BestCandidateIndex = INDEX_NONE;
						int32 BestNewCoverageCount = -1;
						const bool bGreedyAlreadyCoversAnchor =
							GreedySelection.ContainsByPredicate(
							[&AllCandidates](const int32 CandidateIndex)
							{
								return AllCandidates[CandidateIndex]
									.bCoversPodiumSupportAnchor;
							});
						for (const int32 CandidateIndex
							: CandidateIndicesByRequirement[RarestRequirementIndex])
						{
							if (GreedySelection.Contains(CandidateIndex)
								|| GreedySelection.ContainsByPredicate(
									[CandidateIndex, &AllCandidates,
										&CandidatesConflict](const int32 SelectedIndex)
									{
										return CandidatesConflict(
											AllCandidates[CandidateIndex],
											AllCandidates[SelectedIndex]);
									}))
							{
								continue;
							}
							const int32 NewCoverageCount = FPlatformMath::CountBits64(
								CandidateJointCoverageMasks[CandidateIndex]
									& ~GreedyCoveredMask);
							if (NewCoverageCount > BestNewCoverageCount
								|| (NewCoverageCount == BestNewCoverageCount
									&& !bGreedyAlreadyCoversAnchor
									&& AllCandidates[CandidateIndex]
										.bCoversPodiumSupportAnchor
									&& (BestCandidateIndex == INDEX_NONE
										|| !AllCandidates[BestCandidateIndex]
											.bCoversPodiumSupportAnchor)))
							{
								BestCandidateIndex = CandidateIndex;
								BestNewCoverageCount = NewCoverageCount;
							}
						}
						if (BestCandidateIndex == INDEX_NONE
							|| BestNewCoverageCount <= 0)
						{
							return;
						}
						GreedySelection.Add(BestCandidateIndex);
						GreedyCoveredMask |=
							CandidateJointCoverageMasks[BestCandidateIndex];
					}
					if (!GreedySelection.ContainsByPredicate(
						[&AllCandidates](const int32 CandidateIndex)
						{
							return AllCandidates[CandidateIndex]
								.bCoversPodiumSupportAnchor;
						}))
					{
						int32 AnchorCandidateIndex = INDEX_NONE;
						for (int32 CandidateIndex = 0;
							CandidateIndex < AllCandidates.Num(); ++CandidateIndex)
						{
							if (!AllCandidates[CandidateIndex].bCoversPodiumSupportAnchor
								|| GreedySelection.Contains(CandidateIndex)
								|| GreedySelection.ContainsByPredicate(
									[CandidateIndex, &AllCandidates,
										&CandidatesConflict](const int32 SelectedIndex)
									{
										return CandidatesConflict(
											AllCandidates[CandidateIndex],
											AllCandidates[SelectedIndex]);
									}))
							{
								continue;
							}
							AnchorCandidateIndex = CandidateIndex;
							break;
						}
						if (AnchorCandidateIndex == INDEX_NONE)
						{
							return;
						}
						GreedySelection.Add(AnchorCandidateIndex);
					}
					++JointDiagnostic.MainSelectionsVisited;
					GreedySelection.Sort();
					if (!SelectionSupportsAllFullHeightChildren(GreedySelection)
						|| !SelectionSupportsAllSharedEndpoints(GreedySelection))
					{
						return;
					}
					++JointDiagnostic.FullHeightFeasibleMainSelectionCount;
					const bool bCoversAnchor = GreedySelection.ContainsByPredicate(
						[&AllCandidates](const int32 CandidateIndex)
						{
							return AllCandidates[CandidateIndex]
								.bCoversPodiumSupportAnchor;
						});
					if (BestSelection.IsEmpty()
						|| GreedySelection.Num() < BestSelection.Num()
						|| (GreedySelection.Num() == BestSelection.Num()
							&& bCoversAnchor
							&& !bBestSelectionCoversPodiumSupportAnchor))
					{
						BestSelection = MoveTemp(GreedySelection);
						bBestSelectionCoversPodiumSupportAnchor = bCoversAnchor;
					}
				};
				TryGreedyMainSelection(INDEX_NONE);
				int32 InitialRarestRequirement = INDEX_NONE;
				int32 InitialRarestCount = MAX_int32;
				for (int32 RequirementIndex = 0;
					RequirementIndex < CandidateIndicesByRequirement.Num();
					++RequirementIndex)
				{
					if (CandidateIndicesByRequirement[RequirementIndex].Num()
						< InitialRarestCount)
					{
						InitialRarestRequirement = RequirementIndex;
						InitialRarestCount =
							CandidateIndicesByRequirement[RequirementIndex].Num();
					}
				}
				if (InitialRarestRequirement != INDEX_NONE)
				{
					constexpr int32 MaximumGreedySeeds = 64;
					const TArray<int32>& Seeds =
						CandidateIndicesByRequirement[InitialRarestRequirement];
					for (int32 SeedIndex = 0;
						SeedIndex < FMath::Min(Seeds.Num(), MaximumGreedySeeds);
						++SeedIndex)
					{
						TryGreedyMainSelection(Seeds[SeedIndex]);
					}
				}
				constexpr uint64 PodiumAnchorRequirementBit = uint64(1) << 63;
				const uint64 FullOptimizationCoverageMask =
					FullJointCoverageMask | PodiumAnchorRequirementBit;
				TSet<uint64> UniqueOptimizationCoverageSet;
				for (int32 CandidateIndex = 0;
					CandidateIndex < AllCandidates.Num(); ++CandidateIndex)
				{
					UniqueOptimizationCoverageSet.Add(
						CandidateJointCoverageMasks[CandidateIndex]
						| (AllCandidates[CandidateIndex].bCoversPodiumSupportAnchor
							? PodiumAnchorRequirementBit : 0ull));
				}
				TArray<uint64> UniqueOptimizationCoverageMasks =
					UniqueOptimizationCoverageSet.Array();
				UniqueOptimizationCoverageMasks.Sort();
				int32 CoverageOnlyMinimumMainCount = INDEX_NONE;
				bool bCoverageLowerBoundStateCapReached = false;
				TSet<uint64> CoverageVisited;
				TSet<uint64> CoverageFrontier;
				CoverageVisited.Add(0ull);
				CoverageFrontier.Add(0ull);
				constexpr int32 MaximumCoverageLowerBoundStates = 262144;
				const int32 MaximumUsefulCoverageDepth = BestSelection.IsEmpty()
					? CandidateIndicesByRequirement.Num() + 1
					: BestSelection.Num();
				for (int32 Depth = 1;
					Depth <= MaximumUsefulCoverageDepth
						&& CoverageOnlyMinimumMainCount == INDEX_NONE
						&& !bCoverageLowerBoundStateCapReached;
					++Depth)
				{
					TSet<uint64> NextFrontier;
					for (const uint64 CoveredMask : CoverageFrontier)
					{
						for (const uint64 CandidateMask
							: UniqueOptimizationCoverageMasks)
						{
							const uint64 NextMask = CoveredMask | CandidateMask;
							if (NextMask == FullOptimizationCoverageMask)
							{
								CoverageOnlyMinimumMainCount = Depth;
								break;
							}
							if (!CoverageVisited.Contains(NextMask))
							{
								CoverageVisited.Add(NextMask);
								NextFrontier.Add(NextMask);
								if (CoverageVisited.Num()
									> MaximumCoverageLowerBoundStates)
								{
									bCoverageLowerBoundStateCapReached = true;
									break;
								}
							}
						}
						if (CoverageOnlyMinimumMainCount != INDEX_NONE
							|| bCoverageLowerBoundStateCapReached)
						{
							break;
						}
					}
					CoverageFrontier = MoveTemp(NextFrontier);
					if (CoverageFrontier.IsEmpty())
					{
						break;
					}
				}
				const bool bCoverageCardinalityProven =
					!BestSelection.IsEmpty()
					&& CoverageOnlyMinimumMainCount != INDEX_NONE
					&& BestSelection.Num() == CoverageOnlyMinimumMainCount;
				bool bJointSearchTimedOut = false;
				TFunction<void(uint64)> SearchCoverage = [&](const uint64 CoveredMask)
				{
					if ((JointDiagnostic.MainSelectionStateCount & 0xFF) == 0)
					{
						JointSelectionTimer.Checkpoint();
					}
					if (bJointSearchTimedOut
						|| ((JointDiagnostic.MainSelectionStateCount & 0xFF) == 0
							&& !CheckStage1TimeBudget(Stage, StageStartSeconds,
								TEXT("JointSelection"), OutPlan, OutError)))
					{
						bJointSearchTimedOut = true;
						return;
					}
					TArray<int32> StateKey = MainSelectionKey(CurrentSelection);
					if (VisitedMainSelectionStates.Contains(StateKey))
					{
						return;
					}
					VisitedMainSelectionStates.Add(MoveTemp(StateKey));
					++JointDiagnostic.MainSelectionStateCount;
					const uint32 CoveredRegionMask =
						static_cast<uint32>(CoveredMask & 0xFFFFFFFFull);
					const uint32 CoveredProvinceMask =
						static_cast<uint32>(CoveredMask >> 32);
					const int32 CoveredRegionCount =
						FPlatformMath::CountBits(CoveredRegionMask);
					if (CoveredRegionCount > JointDiagnostic.MaximumCoveredRegionCount)
					{
						JointDiagnostic.MaximumCoveredRegionCount = CoveredRegionCount;
						JointDiagnostic.MaximumCoveredRegionMask = CoveredRegionMask;
						JointDiagnostic.BestPartialMainCandidateIndices = CurrentSelection;
						JointDiagnostic.BestPartialMainCandidateIndices.Sort();
					}
					const int32 CoveredProvinceCount =
						FPlatformMath::CountBits(CoveredProvinceMask);
					if (CoveredProvinceCount
						> JointDiagnostic.MaximumCoveredSupportProvinceCount)
					{
						JointDiagnostic.MaximumCoveredSupportProvinceCount =
							CoveredProvinceCount;
						JointDiagnostic.MaximumCoveredSupportProvinceMask =
							CoveredProvinceMask;
					}
					if (CoveredMask == FullJointCoverageMask)
					{
						++JointDiagnostic.MainSelectionsVisited;
						TArray<int32> Canonical = CurrentSelection;
						Canonical.Sort();
						if (!SelectionSupportsAllFullHeightChildren(Canonical)
							|| !SelectionSupportsAllSharedEndpoints(Canonical))
						{
							return;
						}
						++JointDiagnostic.FullHeightFeasibleMainSelectionCount;
						const bool bCoversPodiumSupportAnchor =
							Canonical.ContainsByPredicate(
								[&AllCandidates](const int32 CandidateIndex)
								{
									return AllCandidates.IsValidIndex(CandidateIndex)
										&& AllCandidates[CandidateIndex]
											.bCoversPodiumSupportAnchor;
								});
						if (BestSelection.IsEmpty()
							|| Canonical.Num() < BestSelection.Num()
							|| (Canonical.Num() == BestSelection.Num()
								&& bCoversPodiumSupportAnchor
								&& !bBestSelectionCoversPodiumSupportAnchor))
						{
							BestSelection = MoveTemp(Canonical);
							bBestSelectionCoversPodiumSupportAnchor =
								bCoversPodiumSupportAnchor;
						}
						return;
					}
					// Every incomplete state needs at least one more main. Candidate order is
					// deterministic and already ranks coverage/shape quality, so once an
					// equally small solution that also covers the legacy podium anchor exists,
					// enumerating every other equal-cardinality set cannot improve a structural
					// contract. Keep searching equal-cardinality sets only while an anchor-
					// covering witness is still missing; always keep searching for fewer mains.
					if (!BestSelection.IsEmpty()
						&& (CurrentSelection.Num() + 1 > BestSelection.Num()
							|| (CurrentSelection.Num() + 1 == BestSelection.Num()
								&& bBestSelectionCoversPodiumSupportAnchor)))
					{
						return;
					}
					int32 NextRequirementIndex = INDEX_NONE;
					int32 NextRequirementCandidateCount = MAX_int32;
					for (int32 RequirementIndex = 0;
						RequirementIndex < CandidateIndicesByRequirement.Num();
						++RequirementIndex)
					{
						const int32 BitIndex = RequirementIndex
							< RequiredHighProjectionEntryBounds.Num()
							? RequirementIndex
							: 32 + RequirementIndex
								- RequiredHighProjectionEntryBounds.Num();
						if ((CoveredMask & (1ull << BitIndex)) == 0ull
							&& CandidateIndicesByRequirement[RequirementIndex].Num()
								< NextRequirementCandidateCount)
						{
							NextRequirementIndex = RequirementIndex;
							NextRequirementCandidateCount =
								CandidateIndicesByRequirement[RequirementIndex].Num();
						}
					}
					if (NextRequirementIndex == INDEX_NONE
						|| NextRequirementCandidateCount <= 0)
					{
						return;
					}
					for (const int32 CandidateIndex
						: CandidateIndicesByRequirement[NextRequirementIndex])
					{
						const FPodiumMainCandidate& Candidate =
							AllCandidates[CandidateIndex];
						const uint64 CandidateJointCoverage =
							CandidateJointCoverageMasks[CandidateIndex];
						if (CurrentSelection.Contains(CandidateIndex))
						{
							continue;
						}
						bool bConflict = false;
						for (const int32 SelectedIndex : CurrentSelection)
						{
							bConflict |= CandidatesConflict(
								Candidate, AllCandidates[SelectedIndex]);
						}
						if (bConflict)
						{
							continue;
						}
						CurrentSelection.Add(CandidateIndex);
						SearchCoverage(CoveredMask | CandidateJointCoverage);
						CurrentSelection.Pop(EAllowShrinking::No);
					}
				};
				if (!bCoverageCardinalityProven)
				{
					SearchCoverage(0ull);
				}
				UE_LOG(LogABTSRuntime, Display,
					TEXT("[ABTS][M7.3-Beam-C3V3][Stage1JointSearch] Component=%d Regions=%d Provinces=%d CandidateCounts=%s MainSelections=%d FeasibilityCalls=%lld LazyMainChild=%lld MainWordIntersections=%lld AssignmentVisits=%lld SiblingConflictProbes=%lld SharedIndividualRejected=%d SharedIndividualProbes=%lld GreedyBest=%d CoverageLowerBound=%d CardinalityProven=%d CoverageStates=%d CoverageStateCap=%d"),
					RootIndex, FullHeightChildCandidatesByProjection.Num(),
					RequiredSupportProvinceIds.Num(),
					*FString::JoinBy(FullHeightChildCandidatesByProjection, TEXT(","),
						[](const TArray<FJointChildFootprint>& Candidates)
						{
							return FString::FromInt(Candidates.Num());
						}),
					JointDiagnostic.MainSelectionsVisited,
					FullHeightFeasibilityCallCount,
					PrecomputedMainChildCompatibilityProbeCount,
					FullHeightMainConflictProbeCount,
					FullHeightAssignmentCandidateVisitCount,
					FullHeightSiblingConflictProbeCount,
					RejectedIndividualSharedEndpointMainCount,
					IndividualSharedEndpointCompatibilityProbeCount,
					BestSelection.Num(), CoverageOnlyMinimumMainCount,
					bCoverageCardinalityProven ? 1 : 0,
					CoverageVisited.Num(),
					bCoverageLowerBoundStateCapReached ? 1 : 0);
				if (bJointSearchTimedOut)
				{
					return false;
				}
				if (BestSelection.IsEmpty())
				{
					JointDiagnostic.SelectionReason =
						TEXT("NoMainSetSupportsEveryFullHeightChild");
					OutError = FString::Printf(
						TEXT("BeamC3V3JointCoreSelectionUnavailable:Component=%d:Regions=%d:Provinces=%d:MainCandidates=%d:NoCompatibility=%d:States=%d:MaximumCovered=%d:MaximumMask=0x%08x:MaximumProvinceCovered=%d:MaximumProvinceMask=0x%08x:BestPartial=%s:CompatibilityPerRegion=%s:PodiumCoveragePerRegion=%s:MainCoveragePerProvince=%s:Visited=%d"),
						RootIndex, RequiredHighProjectionEntryBounds.Num(),
						RequiredSupportProvinceIds.Num(),
						AllCandidates.Num(),
						JointDiagnostic
							.MainCandidateWithoutFullHeightCompatibilityCount,
						JointDiagnostic.MainSelectionStateCount,
						JointDiagnostic.MaximumCoveredRegionCount,
						JointDiagnostic.MaximumCoveredRegionMask,
						JointDiagnostic.MaximumCoveredSupportProvinceCount,
						JointDiagnostic.MaximumCoveredSupportProvinceMask,
						*JoinIntegerArray(JointDiagnostic.BestPartialMainCandidateIndices),
						*JoinIntegerArray(
							JointDiagnostic.CompatibleMainCandidateCountByRegion),
						*JoinIntegerArray(
							JointDiagnostic.PodiumCoverageMainCandidateCountByRegion),
						*JoinIntegerArray(
							JointDiagnostic.MainCandidateCountBySupportProvince),
						JointDiagnostic.MainSelectionsVisited);
					return false;
				}
				if (!BestSelection.IsEmpty()
					&& !bBestSelectionCoversPodiumSupportAnchor)
				{
					int32 AnchorCandidateIndex = INDEX_NONE;
					for (int32 CandidateIndex = 0;
						CandidateIndex < AllCandidates.Num(); ++CandidateIndex)
					{
						const FPodiumMainCandidate& Candidate =
							AllCandidates[CandidateIndex];
						if (!Candidate.bCoversPodiumSupportAnchor
							|| BestSelection.Contains(CandidateIndex))
						{
							continue;
						}
						const bool bConflict = BestSelection.ContainsByPredicate(
							[&Candidate, &AllCandidates, &CandidatesConflict](
								const int32 SelectedIndex)
							{
								return AllCandidates.IsValidIndex(SelectedIndex)
									&& CandidatesConflict(
										Candidate, AllCandidates[SelectedIndex]);
							});
						if (!bConflict)
						{
							TArray<int32> TrialSelection = BestSelection;
							TrialSelection.Add(CandidateIndex);
							TrialSelection.Sort();
							if (SelectionSupportsAllFullHeightChildren(TrialSelection)
								&& SelectionSupportsAllSharedEndpoints(TrialSelection))
							{
								AnchorCandidateIndex = CandidateIndex;
								break;
							}
						}
					}
					if (AnchorCandidateIndex == INDEX_NONE)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3PodiumSupportAnchorCoreUnavailable:Component=%d:Candidates=%d:Selected=%d"),
							RootIndex, AllCandidates.Num(), BestSelection.Num());
						return false;
					}
					BestSelection.Add(AnchorCandidateIndex);
					bBestSelectionCoversPodiumSupportAnchor = true;
				}
				for (const int32 CandidateIndex : BestSelection)
				{
					SelectedCoreCandidates.Add(AllCandidates[CandidateIndex]);
				}
				JointDiagnostic.SelectedPodiumMainCount = BestSelection.Num();
				JointDiagnostic.bEveryRegionHasFullHeightChild = true;
				JointDiagnostic.bEverySupportProvinceCovered = true;
				JointDiagnostic.SelectionReason =
					TEXT("AllRegionsHaveWFCFullHeightCompatibleMainAndProvinceAnchorCoverage");
				SelectedCoreCandidates.Sort([](const FPodiumMainCandidate& A,
					const FPodiumMainCandidate& B)
				{
					return A.MinimumY != B.MinimumY
						? A.MinimumY < B.MinimumY : A.MinimumX < B.MinimumX;
				});
			}
			else if (CoreMinimumXUnits != INDEX_NONE && CoreMinimumYUnits != INDEX_NONE)
			{
				FPodiumMainCandidate Candidate;
				Candidate.MinimumX = CoreMinimumXUnits;
				Candidate.MaximumX = CoreMaximumXUnits;
				Candidate.MinimumY = CoreMinimumYUnits;
				Candidate.MaximumY = CoreMaximumYUnits;
				Candidate.BaseSource = CoreBaseSource;
				Candidate.XStations = MakeUniformStations(
					CoreMinimumXUnits, CoreMaximumXUnits, DesiredRailCount);
				Candidate.YStations = MakeUniformStations(
					CoreMinimumYUnits, CoreMaximumYUnits, DesiredRailCount);
				SelectedCoreCandidates.Add(MoveTemp(Candidate));
			}
			JointSelectionTimer.Stop();
			if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
				TEXT("JointSelection"), OutPlan, OutError))
			{
				return false;
			}
			FStage1PhaseTimer CorePlanEmissionTimer(
				OutPlan.Summary.MemberEmissionMilliseconds);
			if (SelectedCoreCandidates.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3ContinuousGroundedCoreUnavailable:Component=%d:BodyCourses=%d:RequiredCourses=%d:HighProjectionEntries=%d"),
					RootIndex, BodyTopCourse, CoreTopCourse,
					RequiredHighProjectionEntryBounds.Num());
				return false;
			}
			if (!bUseGroundedCoreHierarchy && SelectedCoreCandidates.Num() == 1)
			{
				const FPodiumMainCandidate& Candidate = SelectedCoreCandidates[0];
				Component.XGridUnits.RemoveAll([&Candidate](const int32 Station)
				{
					return Station > Candidate.MinimumX && Station < Candidate.MaximumX;
				});
				Component.YGridUnits.RemoveAll([&Candidate](const int32 Station)
				{
					return Station > Candidate.MinimumY && Station < Candidate.MaximumY;
				});
			}
			for (const FPodiumMainCandidate& Candidate : SelectedCoreCandidates)
			{
				Component.XGridUnits.AddUnique(Candidate.MinimumX);
				Component.XGridUnits.AddUnique(Candidate.MaximumX);
				Component.YGridUnits.AddUnique(Candidate.MinimumY);
				Component.YGridUnits.AddUnique(Candidate.MaximumY);
			}
			Component.XGridUnits.Sort();
			Component.YGridUnits.Sort();
			Component.BodyBounds = FBox(
				Position(Component.XGridUnits[0] * BlockUnitsCM,
					Component.YGridUnits[0] * BlockUnitsCM, Root.GroundZCM),
				Position(Component.XGridUnits.Last() * BlockUnitsCM,
					Component.YGridUnits.Last() * BlockUnitsCM,
					Root.GroundZCM + BodyTopCourse * BlockUnitsCM));
			Component.FirstPlannedMemberIndex = OutPlan.Members.Num();
			const int32 NX = Component.XGridUnits.Num() - 1;
			const int32 NY = Component.YGridUnits.Num() - 1;
			TArray<FBandState>& Bands = ComponentBands[RootIndex];
			// Stage 1 owns only the grounded core skeleton and shared bridge courses.
			// Shell/floor band scheduling and occupied-cell rasterization are Stage 3+
			// inputs; running them here makes a deliberately empty facade slice reject
			// a core plan that never consumes that slice.
			if (Stage == EGenerationStage::CompleteStaticDAG)
			{
				const int32 FinalBandBase = FMath::Max(0, (BodyTopCourse - 2) & ~1);
				if (!MakeBandSchedule(FinalBandBase, Density.VerticalUnits,
					ForcedBandBases[RootIndex], Component.BandBaseCourseIndices))
				{
					FString ForcedText;
					for (const int32 Forced : ForcedBandBases[RootIndex])
					{
						ForcedText += FString::Printf(TEXT("%s%d"),
							ForcedText.IsEmpty() ? TEXT("") : TEXT(","), Forced);
					}
					FString SpanText;
					for (const FSpanInput& Span : Spans)
					{
						if (Span.NegativeComponentId != RootIndex
							&& Span.PositiveComponentId != RootIndex)
						{
							continue;
						}
						SpanText += FString::Printf(
							TEXT("|V%d:Axis=%d:RailBottom=%d:SeatBase=%d:Negative=%d:Positive=%d"),
							Span.Volume != nullptr ? Span.Volume->VolumeId : INDEX_NONE,
							Span.SpanAxis, Span.RailBottomCourse, Span.SeatBandBase,
							Span.NegativeComponentId, Span.PositiveComponentId);
					}
					OutError = FString::Printf(
						TEXT("BeamC3V3RootBandScheduleUnavailable:%s:Final=%d:Vertical=%d:Forced=%s:Spans=%s"),
						*Root.Path, FinalBandBase, Density.VerticalUnits,
						*ForcedText, *SpanText);
					return false;
				}
				Bands.SetNum(Component.BandBaseCourseIndices.Num());
			}
			TArray<FBox> RootAllowedBoxes;
			for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
			{
				const FBox Allowed = Volume->LocalBounds.ExpandBy(
					BlockUnitsCM * 0.5 + GeometryToleranceCM);
				RootAllowedBoxes.Add(Allowed);
			}
			for (const FABTSM73DAG5BV2Volume* Volume : Root.CrownVolumes)
			{
				RootAllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
					BlockUnitsCM * 0.5 + GeometryToleranceCM));
			}

			// Rasterize the actual occupied union independently at every band.
			for (int32 BandIndex = 0; BandIndex < Bands.Num(); ++BandIndex)
			{
				FBandState& Band = Bands[BandIndex];
				Band.BaseCourse = Component.BandBaseCourseIndices[BandIndex];
				Band.OccupiedCells.Init(false, NX * NY);
				Band.CellSourceVolumeIds.Init(INDEX_NONE, NX * NY);
				const double XCourseCenterZ = Root.GroundZCM
					+ (Band.BaseCourse + 0.5) * BlockUnitsCM;
				const double YCourseCenterZ = Root.GroundZCM
					+ (Band.BaseCourse + 1.5) * BlockUnitsCM;
				const double SliceZ = Root.GroundZCM
					+ (Band.BaseCourse + 1.0) * BlockUnitsCM;
				for (int32 Y = 0; Y < NY; ++Y)
				{
					for (int32 X = 0; X < NX; ++X)
					{
						const double CellMinX = Component.XGridUnits[X] * BlockUnitsCM;
						const double CellMaxX = Component.XGridUnits[X + 1] * BlockUnitsCM;
						const double CellMinY = Component.YGridUnits[Y] * BlockUnitsCM;
						const double CellMaxY = Component.YGridUnits[Y + 1] * BlockUnitsCM;
						const double CenterX = (CellMinX + CellMaxX) * 0.5;
						const double CenterY = (CellMinY + CellMaxY) * 0.5;
						int32 Source = MAX_int32;
						for (const FABTSM73DAG5BV2Volume* Volume : Root.BodyVolumes)
						{
							if (CenterX >= Volume->LocalBounds.Min.X - GeometryToleranceCM
								&& CenterX <= Volume->LocalBounds.Max.X + GeometryToleranceCM
								&& CenterY >= Volume->LocalBounds.Min.Y - GeometryToleranceCM
								&& CenterY <= Volume->LocalBounds.Max.Y + GeometryToleranceCM
								&& SliceZ >= Volume->LocalBounds.Min.Z - GeometryToleranceCM
								&& SliceZ <= Volume->LocalBounds.Max.Z + GeometryToleranceCM)
							{
								Source = FMath::Min(Source, Volume->VolumeId);
							}
						}
						if (Source != MAX_int32)
						{
							auto EdgeCovered = [&RootAllowedBoxes](
								const EABTSM73BeamAFrameAxis Axis,
								const FVector& Start,
								const FVector& End)
							{
								FPlannedMember Edge;
								Edge.Axis = Axis;
								Edge.LocalStart = Start;
								Edge.LocalEnd = End;
								FVector UncoveredPoint;
								return SolidCoveredByBoxes(
									PlannedMemberBounds(Edge), RootAllowedBoxes, UncoveredPoint);
							};
							const bool bAllEdgesCovered =
								EdgeCovered(EABTSM73BeamAFrameAxis::X,
									Position(CellMinX, CellMinY, XCourseCenterZ),
									Position(CellMaxX, CellMinY, XCourseCenterZ))
								&& EdgeCovered(EABTSM73BeamAFrameAxis::X,
									Position(CellMinX, CellMaxY, XCourseCenterZ),
									Position(CellMaxX, CellMaxY, XCourseCenterZ))
								&& EdgeCovered(EABTSM73BeamAFrameAxis::Y,
									Position(CellMinX, CellMinY, YCourseCenterZ),
									Position(CellMinX, CellMaxY, YCourseCenterZ))
								&& EdgeCovered(EABTSM73BeamAFrameAxis::Y,
									Position(CellMaxX, CellMinY, YCourseCenterZ),
									Position(CellMaxX, CellMaxY, YCourseCenterZ));
							if (!bAllEdgesCovered)
							{
								continue;
							}
							const int32 Index = CellIndex(X, Y, NX);
							Band.OccupiedCells[Index] = true;
							Band.CellSourceVolumeIds[Index] = Source;
						}
					}
				}
				if (!Band.OccupiedCells.Contains(true))
				{
					OutError = FString::Printf(TEXT("BeamC3V3EmptyOccupiedBand:Component=%d:Band=%d"),
						RootIndex, Band.BaseCourse);
					return false;
				}
				BuildExteriorEmptyMask(Band, NX, NY);
				Band.XMemberIndices.Init(INDEX_NONE, (NY + 1) * NX);
				Band.YMemberIndices.Init(INDEX_NONE, (NX + 1) * NY);
				Band.PostMemberIndices.Init(INDEX_NONE, (NX + 1) * (NY + 1));
			}

			TArray<int32> InitialCoreCellIds;
			for (const FPodiumMainCandidate& Candidate : SelectedCoreCandidates)
			{
				if (Candidate.BaseSource == INDEX_NONE
					|| Candidate.XStations.Num() != DesiredRailCount
					|| Candidate.YStations.Num() != DesiredRailCount)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3ContinuousGroundedCoreUnavailable:Component=%d:BodyCourses=%d:RequiredCourses=%d"),
						RootIndex, BodyTopCourse, CoreTopCourse);
					return false;
				}
				FCoreCellPlan& Core = OutPlan.CoreCells.AddDefaulted_GetRef();
				Core.CoreCellId = OutPlan.CoreCells.Num() - 1;
				InitialCoreCellIds.Add(Core.CoreCellId);
				Core.ComponentId = RootIndex;
				Core.BodySourceVolumeId = Candidate.BaseSource;
				Core.CoreMergeRegionId = Component.CoreMergeRegionId;
				Core.HierarchyRole = bUseGroundedCoreHierarchy
					? ECoreHierarchyRole::PodiumMain
					: (bIncidentSupportedSpan
						? ECoreHierarchyRole::SharedEndpoint
						: ECoreHierarchyRole::Continuous);
				Core.TopCourseIndex = CoreTopCourse;
				Core.BodyTopCourseIndex = FMath::Min(BodyTopCourse, CoreTopCourse);
				Core.CompositeCoreGroupId = Component.CoreMergeRegionId;
				Core.RailCount = DesiredRailCount;
				Core.XStations = Candidate.XStations;
				Core.YStations = Candidate.YStations;
				Core.LocalBounds = FBox(
					Position(Core.XStations[0] * BlockUnitsCM,
						Core.YStations[0] * BlockUnitsCM, Root.GroundZCM),
					Position(Core.XStations.Last() * BlockUnitsCM,
						Core.YStations.Last() * BlockUnitsCM,
						Root.GroundZCM + CoreTopCourse * BlockUnitsCM));
				TArray<int32> PreviousCoreCourse;
				for (int32 Course = 0; Course < CoreTopCourse; ++Course)
				{
					const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
						? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
					const double Z = Root.GroundZCM + (Course + 0.5) * BlockUnitsCM;
					const double CenterX = Core.LocalBounds.GetCenter().X;
					const double CenterY = Core.LocalBounds.GetCenter().Y;
					const int32 Source = Course < BodyTopCourse
						? SelectProjectionSourceVolume(Root, CenterX, CenterY, Z)
						: SelectCoreProjectionSourceVolume(Root, CenterX, CenterY, Z);
					if (Source == INDEX_NONE)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3GroundedCoreCourseSourceUnavailable:Component=%d:Core=%d:Course=%d:BodyCourses=%d:RequiredCourses=%d"),
							RootIndex, Core.CoreCellId, Course, BodyTopCourse, CoreTopCourse);
						return false;
					}
					TArray<int32> CurrentCoreCourse;
					const TArray<int32>& CrossStations =
						Axis == EABTSM73BeamAFrameAxis::X ? Core.YStations : Core.XStations;
					for (int32 Rail = 0; Rail < CrossStations.Num(); ++Rail)
					{
						const FVector Start = Axis == EABTSM73BeamAFrameAxis::X
							? Position(Core.LocalBounds.Min.X - BlockUnitsCM * 0.5,
								CrossStations[Rail] * BlockUnitsCM, Z)
							: Position(CrossStations[Rail] * BlockUnitsCM,
								Core.LocalBounds.Min.Y - BlockUnitsCM * 0.5, Z);
						const FVector End = Axis == EABTSM73BeamAFrameAxis::X
							? Position(Core.LocalBounds.Max.X + BlockUnitsCM * 0.5,
								CrossStations[Rail] * BlockUnitsCM, Z)
							: Position(CrossStations[Rail] * BlockUnitsCM,
								Core.LocalBounds.Max.Y + BlockUnitsCM * 0.5, Z);
						const int32 MemberIndex = OutPlan.Members.Num();
						if (!AddPlannedMember(OutPlan, EOwnerKind::CoreCell,
							ESkeletonMemberKind::CoreCourse, Core.CoreCellId, RootIndex,
							Source, Core.CoreCellId, Course, Rail, INDEX_NONE, 0, Axis,
							EABTSM73BeamAMemberRole::CoreCourse, Start, End, OutError))
						{
							return false;
						}
						if (Course == 0)
						{
							OutPlan.Members[MemberIndex].bRequiresGroundSeat = true;
						}
						else
						{
							for (const int32 Lower : PreviousCoreCourse)
							{
								AddSeat(OutPlan, MemberIndex, Lower);
							}
						}
						Core.MemberIndices.Add(MemberIndex);
						CurrentCoreCourse.Add(MemberIndex);
					}
					PreviousCoreCourse = MoveTemp(CurrentCoreCourse);
				}
			}
			CorePlanEmissionTimer.Stop();
			if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
				TEXT("MemberEmission"), OutPlan, OutError))
			{
				return false;
			}

			if (bUseGroundedCoreHierarchy)
			{
				FStage1PhaseTimer CompositePlanningTimer(
					OutPlan.Summary.JointSelectionMilliseconds);
				const TArray<int32> PodiumMainCoreCellIds = InitialCoreCellIds;
				struct FReservedSharedEndpointCell
				{
					int32 SpanVolumeId = INDEX_NONE;
					bool bNegativeEndpoint = false;
					int32 BaseSourceVolumeId = INDEX_NONE;
					int32 PodiumMainCoreCellId = INDEX_NONE;
					/** A legal existing main may itself be the bridge endpoint. */
					int32 ExistingCoreCellId = INDEX_NONE;
					FBox Bounds = FBox(EForceInit::ForceInit);
				};
				TArray<FReservedSharedEndpointCell> ReservedSharedEndpointCells;
				auto HasCompositeLaneConflict = [&OutPlan, RootIndex](
					const int32 MinimumX, const int32 MaximumX,
					const int32 MinimumY, const int32 MaximumY,
					const TArray<int32>& XStations, const TArray<int32>& YStations)
				{
					for (const FCoreCellPlan& Existing : OutPlan.CoreCells)
					{
						if (Existing.ComponentId != RootIndex)
						{
							continue;
						}
						const double XOverlap = SkeletonV3OverlapLength(
							(MinimumX - 0.5) * BlockUnitsCM,
							(MaximumX + 0.5) * BlockUnitsCM,
							Existing.LocalBounds.Min.X - BlockUnitsCM * 0.5,
							Existing.LocalBounds.Max.X + BlockUnitsCM * 0.5);
						const double YOverlap = SkeletonV3OverlapLength(
							(MinimumY - 0.5) * BlockUnitsCM,
							(MaximumY + 0.5) * BlockUnitsCM,
							Existing.LocalBounds.Min.Y - BlockUnitsCM * 0.5,
							Existing.LocalBounds.Max.Y + BlockUnitsCM * 0.5);
						if ((XOverlap > GeometryToleranceCM
								&& YStations.ContainsByPredicate(
									[&Existing](const int32 Station)
									{
										return Existing.YStations.Contains(Station);
									}))
							|| (YOverlap > GeometryToleranceCM
								&& XStations.ContainsByPredicate(
									[&Existing](const int32 Station)
									{
										return Existing.XStations.Contains(Station);
									})))
						{
							return true;
						}
					}
					return false;
				};
				TMap<const FRoot*, TMap<FMainSourceProbeKey, int32>>
					EndpointSourceProbeCaches;
				TMap<const FRoot*,
					TMap<FMainRailCoverageRowKey, FMainRailCoverageRow>>
					EndpointRailCoverageCaches;
				auto EvaluateSharedEndpointFootprint = [
					&EndpointSourceProbeCaches, &EndpointRailCoverageCaches](
					const FRoot& EndpointRoot,
					const TArray<FBox>& EndpointAllowedBoxes,
					const int32 MinimumX, const int32 MaximumX,
					const int32 MinimumY, const int32 MaximumY,
					const int32 RequiredTopCourse,
					int32& OutBaseSourceVolumeId)
				{
					OutBaseSourceVolumeId = INDEX_NONE;
					const int32 SpanX = MaximumX - MinimumX;
					const int32 SpanY = MaximumY - MinimumY;
					if (SpanX < 1 || SpanY < 1
						|| SpanX > MaximumHorizontalUnits
						|| SpanY > MaximumHorizontalUnits)
					{
						return false;
					}
					const TArray<int32> XStations = MakeUniformStations(
						MinimumX, MaximumX, 2);
					const TArray<int32> YStations = MakeUniformStations(
						MinimumY, MaximumY, 2);
					if (XStations.Num() != 2 || YStations.Num() != 2)
					{
						return false;
					}
					const int32 CenterXTwiceUnits = MinimumX + MaximumX;
					const int32 CenterYTwiceUnits = MinimumY + MaximumY;
					TMap<FMainSourceProbeKey, int32>& SourceProbeCache =
						EndpointSourceProbeCaches.FindOrAdd(&EndpointRoot);
					auto SelectCachedEndpointSource = [
						&EndpointRoot, &SourceProbeCache,
						CenterXTwiceUnits, CenterYTwiceUnits](
							const int32 Course, const bool bAllowCrown)
					{
						const FMainSourceProbeKey Key{CenterXTwiceUnits,
							CenterYTwiceUnits, Course, bAllowCrown};
						if (const int32* Cached = SourceProbeCache.Find(Key))
						{
							return *Cached;
						}
						const double CenterX = CenterXTwiceUnits
							* 0.5 * BlockUnitsCM;
						const double CenterY = CenterYTwiceUnits
							* 0.5 * BlockUnitsCM;
						const double Z = EndpointRoot.GroundZCM
							+ (Course + 0.5) * BlockUnitsCM;
						const int32 Source = bAllowCrown
							? SelectCoreProjectionSourceVolume(
								EndpointRoot, CenterX, CenterY, Z)
							: SelectProjectionSourceVolume(
								EndpointRoot, CenterX, CenterY, Z);
						SourceProbeCache.Add(Key, Source);
						return Source;
					};
					TMap<FMainRailCoverageRowKey, FMainRailCoverageRow>&
						RailCoverageCache =
							EndpointRailCoverageCaches.FindOrAdd(&EndpointRoot);
					auto IsCachedEndpointRailCovered = [
						&EndpointRoot, &EndpointAllowedBoxes,
						&RailCoverageCache](
							const int32 Course,
							const EABTSM73BeamAFrameAxis Axis,
							const int32 AlongMinimum,
							const int32 AlongMaximum,
							const int32 CrossStation)
					{
						const FMainRailCoverageRowKey Key{Course, CrossStation,
							static_cast<uint8>(Axis), false};
						FMainRailCoverageRow* Row = RailCoverageCache.Find(Key);
						if (Row == nullptr)
						{
							Row = &RailCoverageCache.Add(Key);
							Row->AlongMinimum = Axis == EABTSM73BeamAFrameAxis::X
								? QMin(EndpointRoot.Bounds.Min.X
									+ BlockUnitsCM * 0.5)
								: QMin(EndpointRoot.Bounds.Min.Y
									+ BlockUnitsCM * 0.5);
							Row->AlongMaximum = Axis == EABTSM73BeamAFrameAxis::X
								? QMax(EndpointRoot.Bounds.Max.X
									- BlockUnitsCM * 0.5)
								: QMax(EndpointRoot.Bounds.Max.Y
									- BlockUnitsCM * 0.5);
							Row->AlongMinimum -= MaximumHorizontalUnits;
							Row->AlongMaximum += MaximumHorizontalUnits;
							const int32 CellCount = Row->AlongMaximum
								- Row->AlongMinimum + 1;
							Row->UncoveredPrefix.SetNumZeroed(CellCount + 1);
							const double Z = EndpointRoot.GroundZCM
								+ (Course + 0.5) * BlockUnitsCM;
							for (int32 CellIndex = 0;
								CellIndex < CellCount; ++CellIndex)
							{
								const int32 AlongStation =
									Row->AlongMinimum + CellIndex;
								FPlannedMember Probe;
								Probe.Axis = Axis;
								Probe.LocalStart =
									Axis == EABTSM73BeamAFrameAxis::X
									? Position((AlongStation - 0.5) * BlockUnitsCM,
										CrossStation * BlockUnitsCM, Z)
									: Position(CrossStation * BlockUnitsCM,
										(AlongStation - 0.5) * BlockUnitsCM, Z);
								Probe.LocalEnd =
									Axis == EABTSM73BeamAFrameAxis::X
									? Position((AlongStation + 0.5) * BlockUnitsCM,
										CrossStation * BlockUnitsCM, Z)
									: Position(CrossStation * BlockUnitsCM,
										(AlongStation + 0.5) * BlockUnitsCM, Z);
								FVector UncoveredPoint;
								const bool bCellCovered = SolidCoveredByBoxes(
									PlannedMemberBounds(Probe), EndpointAllowedBoxes,
									UncoveredPoint);
								Row->UncoveredPrefix[CellIndex + 1] =
									Row->UncoveredPrefix[CellIndex]
									+ (bCellCovered ? 0 : 1);
							}
						}
						if (AlongMinimum < Row->AlongMinimum
							|| AlongMaximum > Row->AlongMaximum
							|| AlongMaximum < AlongMinimum)
						{
							return false;
						}
						const int32 PrefixMinimum =
							AlongMinimum - Row->AlongMinimum;
						const int32 PrefixMaximum =
							AlongMaximum - Row->AlongMinimum + 1;
						return Row->UncoveredPrefix[PrefixMaximum]
							== Row->UncoveredPrefix[PrefixMinimum];
					};
					OutBaseSourceVolumeId = SelectCachedEndpointSource(0, false);
					if (OutBaseSourceVolumeId == INDEX_NONE
						|| !EndpointRoot.GroundSourceVolumeIds.Contains(
							OutBaseSourceVolumeId))
					{
						return false;
					}
					for (int32 Course = 0; Course < RequiredTopCourse; ++Course)
					{
						if (SelectCachedEndpointSource(Course, true) == INDEX_NONE)
						{
							return false;
						}
						const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
							? EABTSM73BeamAFrameAxis::X
							: EABTSM73BeamAFrameAxis::Y;
						const TArray<int32>& CrossStations =
							Axis == EABTSM73BeamAFrameAxis::X
								? YStations : XStations;
						for (const int32 CrossStation : CrossStations)
						{
							if (!IsCachedEndpointRailCovered(Course, Axis,
								Axis == EABTSM73BeamAFrameAxis::X
									? MinimumX : MinimumY,
								Axis == EABTSM73BeamAFrameAxis::X
									? MaximumX : MaximumY,
								CrossStation))
							{
								return false;
							}
						}
					}
					return true;
				};
				auto CountOrthogonalPatches = [](const FCoreCellPlan& XCore,
					const int32 YMinimumY, const int32 YMaximumY,
					const TArray<int32>& YCoreXStations)
				{
					int32 Count = 0;
					for (const int32 XRailY : XCore.YStations)
					{
						for (const int32 YRailX : YCoreXStations)
						{
							const double OverlapX = SkeletonV3OverlapLength(
								XCore.LocalBounds.Min.X - BlockUnitsCM * 0.5,
								XCore.LocalBounds.Max.X + BlockUnitsCM * 0.5,
								(YRailX - 0.5) * BlockUnitsCM,
								(YRailX + 0.5) * BlockUnitsCM);
							const double OverlapY = SkeletonV3OverlapLength(
								(XRailY - 0.5) * BlockUnitsCM,
								(XRailY + 0.5) * BlockUnitsCM,
								(YMinimumY - 0.5) * BlockUnitsCM,
								(YMaximumY + 0.5) * BlockUnitsCM);
							Count += OverlapX > GeometryToleranceCM
								&& OverlapY > GeometryToleranceCM ? 1 : 0;
						}
					}
					return Count;
				};
				auto FindBestCoupledPodiumMain = [&OutPlan, &PodiumMainCoreCellIds,
					&CountOrthogonalPatches](
					const int32 MinimumX, const int32 MaximumX,
					const int32 MinimumY, const int32 MaximumY,
					const TArray<int32>& XStations,
					const TArray<int32>& YStations,
					int32& OutCouplingPatches)
				{
					int32 BestMainCoreCellId = INDEX_NONE;
					OutCouplingPatches = 0;
					double BestCenterDistance = DBL_MAX;
					FCoreCellPlan CandidateXCore;
					CandidateXCore.LocalBounds = FBox(
						Position(MinimumX * BlockUnitsCM,
							MinimumY * BlockUnitsCM, 0.0),
						Position(MaximumX * BlockUnitsCM,
							MaximumY * BlockUnitsCM, BlockUnitsCM));
					CandidateXCore.YStations = YStations;
					for (const int32 MainCoreCellId : PodiumMainCoreCellIds)
					{
						if (!OutPlan.CoreCells.IsValidIndex(MainCoreCellId))
						{
							continue;
						}
						const FCoreCellPlan& Main = OutPlan.CoreCells[MainCoreCellId];
						const int32 CouplingPatches = FMath::Min(
							CountOrthogonalPatches(Main, MinimumY, MaximumY, XStations),
							CountOrthogonalPatches(CandidateXCore,
								FMath::RoundToInt(Main.LocalBounds.Min.Y / BlockUnitsCM),
								FMath::RoundToInt(Main.LocalBounds.Max.Y / BlockUnitsCM),
								Main.XStations));
						const double CenterDistance = FVector2D::DistSquared(
							FVector2D(CandidateXCore.LocalBounds.GetCenter()),
							FVector2D(Main.LocalBounds.GetCenter()));
						if (BestMainCoreCellId == INDEX_NONE
							|| CouplingPatches > OutCouplingPatches
							|| (CouplingPatches == OutCouplingPatches
								&& (CenterDistance
									< BestCenterDistance - UE_DOUBLE_SMALL_NUMBER
									|| (FMath::IsNearlyEqual(
										CenterDistance, BestCenterDistance)
										&& MainCoreCellId < BestMainCoreCellId))))
						{
							OutCouplingPatches = CouplingPatches;
							BestMainCoreCellId = MainCoreCellId;
							BestCenterDistance = CenterDistance;
						}
					}
					return BestMainCoreCellId;
				};
				auto ConflictsReservedSharedEndpoint =
					[&ReservedSharedEndpointCells](
						const int32 MinimumX, const int32 MaximumX,
						const int32 MinimumY, const int32 MaximumY,
						const TArray<int32>& XStations,
						const TArray<int32>& YStations)
				{
					for (const FReservedSharedEndpointCell& Reserved
						: ReservedSharedEndpointCells)
					{
						const int32 ReservedMinimumX = FMath::RoundToInt(
							Reserved.Bounds.Min.X / BlockUnitsCM);
						const int32 ReservedMaximumX = FMath::RoundToInt(
							Reserved.Bounds.Max.X / BlockUnitsCM);
						const int32 ReservedMinimumY = FMath::RoundToInt(
							Reserved.Bounds.Min.Y / BlockUnitsCM);
						const int32 ReservedMaximumY = FMath::RoundToInt(
							Reserved.Bounds.Max.Y / BlockUnitsCM);
						const TArray<int32> ReservedXStations = MakeUniformStations(
							ReservedMinimumX, ReservedMaximumX, 2);
						const TArray<int32> ReservedYStations = MakeUniformStations(
							ReservedMinimumY, ReservedMaximumY, 2);
						const double XOverlap = SkeletonV3OverlapLength(
							(MinimumX - 0.5) * BlockUnitsCM,
							(MaximumX + 0.5) * BlockUnitsCM,
							(ReservedMinimumX - 0.5) * BlockUnitsCM,
							(ReservedMaximumX + 0.5) * BlockUnitsCM);
						const double YOverlap = SkeletonV3OverlapLength(
							(MinimumY - 0.5) * BlockUnitsCM,
							(MaximumY + 0.5) * BlockUnitsCM,
							(ReservedMinimumY - 0.5) * BlockUnitsCM,
							(ReservedMaximumY + 0.5) * BlockUnitsCM);
						if ((XOverlap > GeometryToleranceCM
								&& YStations.ContainsByPredicate(
									[&ReservedYStations](const int32 Station)
									{
										return ReservedYStations.Contains(Station);
									}))
							|| (YOverlap > GeometryToleranceCM
								&& XStations.ContainsByPredicate(
									[&ReservedXStations](const int32 Station)
									{
										return ReservedXStations.Contains(Station);
									})))
						{
							return true;
						}
					}
					return false;
				};

				// Freeze bridge-facing endpoint footprints before ordinary tower children.
				// The minimum 1x1 WFC witness remains a fast feasibility proof; production
				// geometry is selected independently by this bounded footprint search.
				for (const FSpanInput& Incident : Spans)
				{
					if (Incident.Volume == nullptr
						|| (Incident.NegativeComponentId != RootIndex
							&& Incident.PositiveComponentId != RootIndex))
					{
						continue;
					}
					const bool bNegativeEndpoint =
						Incident.NegativeComponentId == RootIndex;
					const int32 HighestIncidentCourse = Incident.SharedCourses.IsEmpty()
						? Incident.RailBottomCourse : Incident.SharedCourses.Last();
					TArray<FSharedEndpointReachabilityDiagnostic> Reachability;
					AppendSharedEndpointReachabilityDiagnostics(
						Root, RootIndex, *Incident.Volume, bNegativeEndpoint,
						HighestIncidentCourse + 2, Reachability);
					FBox BestBounds(EForceInit::ForceInit);
					int32 BestBaseSourceVolumeId = INDEX_NONE;
					int32 BestPodiumMainCoreCellId = INDEX_NONE;
					int32 BestCouplingPatches = -1;
					int32 BestMinimumSpan = -1;
					int32 BestImbalance = MAX_int32;
					int32 BestArea = -1;
					double BestEndpointInset = DBL_MAX;
					double BestCrossCenterDistance = DBL_MAX;
					int32 WFCReachableCount = 0;
					int32 MainConflictCount = 0;
					int32 ReservationConflictCount = 0;
					int32 NoMainCouplingCount = 0;
					for (const FSharedEndpointReachabilityDiagnostic& Candidate : Reachability)
					{
						WFCReachableCount += Candidate.bReachableInWFC ? 1 : 0;
					}
					const int32 OtherComponentId = bNegativeEndpoint
						? Incident.PositiveComponentId : Incident.NegativeComponentId;
					TArray<FSharedEndpointReachabilityDiagnostic> OtherReachability;
					AppendSharedEndpointReachabilityDiagnostics(
						Roots[OtherComponentId], OtherComponentId, *Incident.Volume,
						!bNegativeEndpoint, HighestIncidentCourse + 2,
						OtherReachability);
					double OtherMinimumInset = DBL_MAX;
					for (const FSharedEndpointReachabilityDiagnostic& Candidate
						: OtherReachability)
					{
						if (Candidate.bReachableInWFC)
						{
							OtherMinimumInset = FMath::Min(OtherMinimumInset,
								FMath::Max(0.0, Candidate.EndpointInsetCM));
						}
					}
					if (OtherMinimumInset == DBL_MAX)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedEndpointOppositeWitnessUnavailable:Volume=%d:Component=%d:Side=%s"),
							Incident.Volume->VolumeId, RootIndex,
							bNegativeEndpoint ? TEXT("Negative") : TEXT("Positive"));
						return false;
					}
					const double OpeningLength =
						Incident.Volume->SpanOpeningMaxCM
						- Incident.Volume->SpanOpeningMinCM;
					const double MaximumEndpointInset = 720.0 - OpeningLength
						- 2.0 * BlockUnitsCM - OtherMinimumInset;
					const int32 CandidateMinimumX = QMin(
						Root.Bounds.Min.X + BlockUnitsCM * 0.5);
					const int32 CandidateMaximumX = QMax(
						Root.Bounds.Max.X - BlockUnitsCM * 0.5);
					const int32 CandidateMinimumY = QMin(
						Root.Bounds.Min.Y + BlockUnitsCM * 0.5);
					const int32 CandidateMaximumY = QMax(
						Root.Bounds.Max.Y - BlockUnitsCM * 0.5);
					const int32 MaximumSpanX = FMath::Min(MaximumHorizontalUnits,
						CandidateMaximumX - CandidateMinimumX);
					const int32 MaximumSpanY = FMath::Min(MaximumHorizontalUnits,
						CandidateMaximumY - CandidateMinimumY);
					const int32 AxisIndex = Incident.SpanAxis;
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					const double SpanCrossCenter =
						Incident.Volume->LocalBounds.GetCenter()[CrossAxisIndex];
					const FCoreCellPlan* FrozenOtherEndpoint =
						OutPlan.CoreCells.FindByPredicate(
							[OtherComponentId, &Incident](const FCoreCellPlan& Candidate)
							{
								return Candidate.ComponentId == OtherComponentId
									&& Candidate.HierarchyRole
										== ECoreHierarchyRole::SharedEndpoint
									&& Candidate.SharedEndpointSpanVolumeId
										== Incident.Volume->VolumeId;
							});
					const FRoot& OtherRoot = Roots[OtherComponentId];
					TArray<FBox> OtherAllowedBoxes;
					for (const FABTSM73DAG5BV2Volume* Volume : OtherRoot.BodyVolumes)
					{
						OtherAllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
							BlockUnitsCM * 0.5 + GeometryToleranceCM));
					}
					for (const FABTSM73DAG5BV2Volume* Volume : OtherRoot.CrownVolumes)
					{
						OtherAllowedBoxes.Add(Volume->LocalBounds.ExpandBy(
							BlockUnitsCM * 0.5 + GeometryToleranceCM));
					}
					TMap<uint64, TArray<FVector>> OtherCrossProfiles;
					auto HasCompatibleOtherProduction = [
						&OtherCrossProfiles, &OtherRoot, &OtherAllowedBoxes,
						&EvaluateSharedEndpointFootprint, &Incident,
						bNegativeEndpoint, AxisIndex](
						const int32 CrossMinimumStation,
						const int32 CrossMaximumStation,
						const double CurrentInset,
						const double CurrentOuterReach,
						const int32 RequiredOtherMinimumSpan)
					{
						const uint64 Key = (static_cast<uint64>(
							static_cast<uint32>(CrossMinimumStation)) << 32)
							| static_cast<uint32>(CrossMaximumStation);
						TArray<FVector>* Profiles = OtherCrossProfiles.Find(Key);
						if (Profiles == nullptr)
						{
							Profiles = &OtherCrossProfiles.Add(Key);
							const int32 OtherLongMinimum = AxisIndex == 0
								? QMin(OtherRoot.Bounds.Min.X + BlockUnitsCM * 0.5)
								: QMin(OtherRoot.Bounds.Min.Y + BlockUnitsCM * 0.5);
							const int32 OtherLongMaximum = AxisIndex == 0
								? QMax(OtherRoot.Bounds.Max.X - BlockUnitsCM * 0.5)
								: QMax(OtherRoot.Bounds.Max.Y - BlockUnitsCM * 0.5);
							const int32 OtherMaximumLongSpan = FMath::Min(
								MaximumHorizontalUnits,
								OtherLongMaximum - OtherLongMinimum);
							for (int32 LongSpan = 1;
								LongSpan <= OtherMaximumLongSpan; ++LongSpan)
							{
								for (int32 LongMinimum = OtherLongMinimum;
									LongMinimum + LongSpan <= OtherLongMaximum; ++LongMinimum)
								{
									const int32 LongMaximum = LongMinimum + LongSpan;
									const int32 MinimumX = AxisIndex == 0
										? LongMinimum : CrossMinimumStation;
									const int32 MaximumX = AxisIndex == 0
										? LongMaximum : CrossMaximumStation;
									const int32 MinimumY = AxisIndex == 0
										? CrossMinimumStation : LongMinimum;
									const int32 MaximumY = AxisIndex == 0
										? CrossMaximumStation : LongMaximum;
									int32 BaseSourceVolumeId = INDEX_NONE;
									if (!EvaluateSharedEndpointFootprint(OtherRoot,
										OtherAllowedBoxes, MinimumX, MaximumX,
										MinimumY, MaximumY,
										(Incident.SharedCourses.IsEmpty()
											? Incident.RailBottomCourse
											: Incident.SharedCourses.Last()) + 2,
										BaseSourceVolumeId))
									{
										continue;
									}
									const double PhysicalMinimum =
										(LongMinimum - 0.5) * BlockUnitsCM;
									const double PhysicalMaximum =
										(LongMaximum + 0.5) * BlockUnitsCM;
									const bool bOtherNegative = !bNegativeEndpoint;
									const double OpeningBoundary = bOtherNegative
										? Incident.Volume->SpanOpeningMinCM
										: Incident.Volume->SpanOpeningMaxCM;
									const double InnerFace = bOtherNegative
										? PhysicalMaximum : PhysicalMinimum;
									const double Inset = FMath::Max(0.0, bOtherNegative
										? OpeningBoundary - InnerFace
										: InnerFace - OpeningBoundary);
									const double OuterReach = FMath::Max(0.0, bOtherNegative
										? OpeningBoundary - PhysicalMinimum
										: PhysicalMaximum - OpeningBoundary);
									Profiles->Add(FVector(Inset, OuterReach,
										FMath::Min(LongSpan,
											CrossMaximumStation - CrossMinimumStation)));
								}
							}
						}
						for (const FVector& Profile : *Profiles)
						{
							if (Profile.Z >= RequiredOtherMinimumSpan
								&& Incident.Volume->SpanOpeningMaxCM
									- Incident.Volume->SpanOpeningMinCM
									+ 2.0 * BlockUnitsCM + FMath::Max(0.0, CurrentInset)
									+ Profile.X <= 720.0 + GeometryToleranceCM
								&& Incident.Volume->SpanOpeningMaxCM
									- Incident.Volume->SpanOpeningMinCM
									+ CurrentOuterReach + Profile.Y
									<= 720.0 + GeometryToleranceCM)
							{
								return true;
							}
						}
						return false;
					};
					for (int32 SpanX = 1; SpanX <= MaximumSpanX; ++SpanX)
					{
						for (int32 SpanY = 1; SpanY <= MaximumSpanY; ++SpanY)
						{
							for (int32 MinimumY = CandidateMinimumY;
								MinimumY + SpanY <= CandidateMaximumY; ++MinimumY)
							{
								for (int32 MinimumX = CandidateMinimumX;
									MinimumX + SpanX <= CandidateMaximumX; ++MinimumX)
								{
									const int32 MaximumX = MinimumX + SpanX;
									const int32 MaximumY = MinimumY + SpanY;
									const double PhysicalMinimumX =
										(MinimumX - 0.5) * BlockUnitsCM;
									const double PhysicalMaximumX =
										(MaximumX + 0.5) * BlockUnitsCM;
									const double PhysicalMinimumY =
										(MinimumY - 0.5) * BlockUnitsCM;
									const double PhysicalMaximumY =
										(MaximumY + 0.5) * BlockUnitsCM;
									const double CrossMinimum = CrossAxisIndex == 0
										? PhysicalMinimumX : PhysicalMinimumY;
									const double CrossMaximum = CrossAxisIndex == 0
										? PhysicalMaximumX : PhysicalMaximumY;
									if (SkeletonV3OverlapLength(CrossMinimum, CrossMaximum,
										Incident.Volume->LocalBounds.Min[CrossAxisIndex],
										Incident.Volume->LocalBounds.Max[CrossAxisIndex])
										<= GeometryToleranceCM)
									{
										continue;
									}
									const double InnerFace = bNegativeEndpoint
										? (AxisIndex == 0 ? PhysicalMaximumX : PhysicalMaximumY)
										: (AxisIndex == 0 ? PhysicalMinimumX : PhysicalMinimumY);
									const double OpeningBoundary = bNegativeEndpoint
										? Incident.Volume->SpanOpeningMinCM
										: Incident.Volume->SpanOpeningMaxCM;
									const double EndpointInset = bNegativeEndpoint
										? OpeningBoundary - InnerFace
										: InnerFace - OpeningBoundary;
									if (EndpointInset > MaximumEndpointInset
										+ GeometryToleranceCM)
									{
										continue;
									}
									int32 BaseSourceVolumeId = INDEX_NONE;
									if (!EvaluateSharedEndpointFootprint(
										Root, CoreAllowedBoxes, MinimumX, MaximumX,
										MinimumY, MaximumY, HighestIncidentCourse + 2,
										BaseSourceVolumeId))
									{
										continue;
									}
									const TArray<int32> XStations = MakeUniformStations(
										MinimumX, MaximumX, 2);
									const TArray<int32> YStations = MakeUniformStations(
										MinimumY, MaximumY, 2);
									const int32 MinimumSpan = FMath::Min(SpanX, SpanY);
									const TArray<int32>& CandidateCrossStations =
										AxisIndex == 0 ? YStations : XStations;
									const double CurrentPhysicalMinimum = AxisIndex == 0
										? PhysicalMinimumX : PhysicalMinimumY;
									const double CurrentPhysicalMaximum = AxisIndex == 0
										? PhysicalMaximumX : PhysicalMaximumY;
									const double CurrentOuterReach = FMath::Max(0.0,
										bNegativeEndpoint
											? OpeningBoundary - CurrentPhysicalMinimum
											: CurrentPhysicalMaximum - OpeningBoundary);
									bool bCompatibleOtherProduction = false;
									if (FrozenOtherEndpoint != nullptr)
									{
										const TArray<int32>& OtherCrossStations =
											AxisIndex == 0
												? FrozenOtherEndpoint->YStations
												: FrozenOtherEndpoint->XStations;
										if (CandidateCrossStations != OtherCrossStations)
										{
											continue;
										}
										const FVector FrozenOtherSize =
											FrozenOtherEndpoint->LocalBounds.GetSize();
										const int32 FrozenOtherMinimumSpan = FMath::RoundToInt(
											FMath::Min(FrozenOtherSize.X, FrozenOtherSize.Y)
												/ BlockUnitsCM);
										if (MinimumSpan < FrozenOtherMinimumSpan)
										{
											continue;
										}
										const double OtherPhysicalMinimum =
											FrozenOtherEndpoint->LocalBounds.Min[AxisIndex]
												- BlockUnitsCM * 0.5;
										const double OtherPhysicalMaximum =
											FrozenOtherEndpoint->LocalBounds.Max[AxisIndex]
												+ BlockUnitsCM * 0.5;
										const bool bOtherNegative = !bNegativeEndpoint;
										const double OtherInnerFace = bOtherNegative
											? OtherPhysicalMaximum : OtherPhysicalMinimum;
										const double OtherOpeningBoundary = bOtherNegative
											? Incident.Volume->SpanOpeningMinCM
											: Incident.Volume->SpanOpeningMaxCM;
										const double CompatibleOtherInset = FMath::Max(0.0,
											bOtherNegative
												? OtherOpeningBoundary - OtherInnerFace
												: OtherInnerFace - OtherOpeningBoundary);
										const double CompatibleOtherOuterReach = FMath::Max(0.0,
											bOtherNegative
												? OtherOpeningBoundary - OtherPhysicalMinimum
												: OtherPhysicalMaximum - OtherOpeningBoundary);
										bCompatibleOtherProduction =
											OpeningLength + 2.0 * BlockUnitsCM
												+ FMath::Max(0.0, EndpointInset)
												+ CompatibleOtherInset
												<= 720.0 + GeometryToleranceCM
											&& OpeningLength + CurrentOuterReach
												+ CompatibleOtherOuterReach
												<= 720.0 + GeometryToleranceCM;
									}
									else
									{
										const int32 CrossMinimumStation = AxisIndex == 0
											? MinimumY : MinimumX;
										const int32 CrossMaximumStation = AxisIndex == 0
											? MaximumY : MaximumX;
										bCompatibleOtherProduction =
											HasCompatibleOtherProduction(CrossMinimumStation,
												CrossMaximumStation, EndpointInset,
												CurrentOuterReach, MinimumSpan);
									}
									if (!bCompatibleOtherProduction)
									{
										continue;
									}
									if (HasCompositeLaneConflict(MinimumX, MaximumX,
										MinimumY, MaximumY, XStations, YStations))
									{
										++MainConflictCount;
										continue;
									}
									if (ConflictsReservedSharedEndpoint(MinimumX, MaximumX,
										MinimumY, MaximumY, XStations, YStations))
									{
										++ReservationConflictCount;
										continue;
									}
									FCoreCellPlan CandidateXCore;
									CandidateXCore.LocalBounds = FBox(
										Position(MinimumX * BlockUnitsCM,
											MinimumY * BlockUnitsCM, Root.GroundZCM),
										Position(MaximumX * BlockUnitsCM,
											MaximumY * BlockUnitsCM,
											Root.GroundZCM
												+ (HighestIncidentCourse + 2) * BlockUnitsCM));
									CandidateXCore.YStations = YStations;
									int32 CouplingPatches = 0;
									int32 CandidatePodiumMainCoreCellId =
										FindBestCoupledPodiumMain(MinimumX, MaximumX,
											MinimumY, MaximumY, XStations, YStations,
											CouplingPatches);
									if (CandidatePodiumMainCoreCellId == INDEX_NONE
										&& !PodiumMainCoreCellIds.IsEmpty())
									{
										CandidatePodiumMainCoreCellId = PodiumMainCoreCellIds[0];
									}
									NoMainCouplingCount += CouplingPatches <= 0 ? 1 : 0;
									const int32 Imbalance = FMath::Abs(SpanX - SpanY);
									const int32 Area = SpanX * SpanY;
									const double CrossCenterDistance = FMath::Abs(
										(CrossMinimum + CrossMaximum) * 0.5 - SpanCrossCenter);
									bool bBetter = MinimumSpan > BestMinimumSpan;
									if (MinimumSpan == BestMinimumSpan)
									{
										bBetter = Imbalance < BestImbalance;
										if (Imbalance == BestImbalance)
										{
											bBetter = Area > BestArea;
											if (Area == BestArea)
											{
												bBetter = EndpointInset < BestEndpointInset
													- GeometryToleranceCM;
												if (FMath::IsNearlyEqual(EndpointInset,
													BestEndpointInset, GeometryToleranceCM))
												{
													bBetter = CrossCenterDistance
														< BestCrossCenterDistance - GeometryToleranceCM
														|| (FMath::IsNearlyEqual(CrossCenterDistance,
															BestCrossCenterDistance, GeometryToleranceCM)
															&& CouplingPatches > BestCouplingPatches);
												}
											}
										}
									}
									if (bBetter)
									{
										BestBounds = CandidateXCore.LocalBounds;
										BestBaseSourceVolumeId = BaseSourceVolumeId;
										BestPodiumMainCoreCellId =
											CandidatePodiumMainCoreCellId;
										BestCouplingPatches = CouplingPatches;
										BestMinimumSpan = MinimumSpan;
										BestImbalance = Imbalance;
										BestArea = Area;
										BestEndpointInset = EndpointInset;
										BestCrossCenterDistance = CrossCenterDistance;
									}
								}
							}
						}
					}
					int32 ExistingEndpointCoreCellId = INDEX_NONE;
					if ((!BestBounds.IsValid || BestBaseSourceVolumeId == INDEX_NONE)
						&& FrozenOtherEndpoint != nullptr)
					{
						double BestExistingDistance = DBL_MAX;
						double BestExistingArea = -DBL_MAX;
						for (const int32 MainCoreCellId : PodiumMainCoreCellIds)
						{
							if (!OutPlan.CoreCells.IsValidIndex(MainCoreCellId))
							{
								continue;
							}
							const FCoreCellPlan& Main = OutPlan.CoreCells[MainCoreCellId];
							if (Main.TopCourseIndex < HighestIncidentCourse + 2)
							{
								continue;
							}
							const double CrossOverlap = SkeletonV3OverlapLength(
								Main.LocalBounds.Min[CrossAxisIndex] - BlockUnitsCM * 0.5,
								Main.LocalBounds.Max[CrossAxisIndex] + BlockUnitsCM * 0.5,
								Incident.Volume->LocalBounds.Min[CrossAxisIndex],
								Incident.Volume->LocalBounds.Max[CrossAxisIndex]);
							if (CrossOverlap <= GeometryToleranceCM)
							{
								continue;
							}
							const double CurrentPhysicalMinimum =
								Main.LocalBounds.Min[AxisIndex] - BlockUnitsCM * 0.5;
							const double CurrentPhysicalMaximum =
								Main.LocalBounds.Max[AxisIndex] + BlockUnitsCM * 0.5;
							const double CurrentInnerFace = bNegativeEndpoint
								? CurrentPhysicalMaximum : CurrentPhysicalMinimum;
							const double CurrentOpeningBoundary = bNegativeEndpoint
								? Incident.Volume->SpanOpeningMinCM
								: Incident.Volume->SpanOpeningMaxCM;
							const double CurrentInset = bNegativeEndpoint
								? CurrentOpeningBoundary - CurrentInnerFace
								: CurrentInnerFace - CurrentOpeningBoundary;
							if (CurrentInset < -GeometryToleranceCM
								|| CurrentInset > MaximumEndpointInset
									+ GeometryToleranceCM)
							{
								continue;
							}
							const double CurrentOuterReach = FMath::Max(0.0,
								bNegativeEndpoint
									? CurrentOpeningBoundary - CurrentPhysicalMinimum
									: CurrentPhysicalMaximum - CurrentOpeningBoundary);
							const double OtherPhysicalMinimum =
								FrozenOtherEndpoint->LocalBounds.Min[AxisIndex]
									- BlockUnitsCM * 0.5;
							const double OtherPhysicalMaximum =
								FrozenOtherEndpoint->LocalBounds.Max[AxisIndex]
									+ BlockUnitsCM * 0.5;
							const bool bOtherNegative = !bNegativeEndpoint;
							const double OtherOpeningBoundary = bOtherNegative
								? Incident.Volume->SpanOpeningMinCM
								: Incident.Volume->SpanOpeningMaxCM;
							const double OtherInnerFace = bOtherNegative
								? OtherPhysicalMaximum : OtherPhysicalMinimum;
							const double OtherInset = FMath::Max(0.0, bOtherNegative
								? OtherOpeningBoundary - OtherInnerFace
								: OtherInnerFace - OtherOpeningBoundary);
							const double OtherOuterReach = FMath::Max(0.0, bOtherNegative
								? OtherOpeningBoundary - OtherPhysicalMinimum
								: OtherPhysicalMaximum - OtherOpeningBoundary);
							if (OpeningLength + 2.0 * BlockUnitsCM
									+ FMath::Max(0.0, CurrentInset) + OtherInset
									> 720.0 + GeometryToleranceCM
								|| OpeningLength + CurrentOuterReach + OtherOuterReach
									> 720.0 + GeometryToleranceCM)
							{
								continue;
							}
							const double Area = Main.LocalBounds.GetSize().X
								* Main.LocalBounds.GetSize().Y;
							const double Distance = FMath::Abs(CurrentInset);
							if (Distance < BestExistingDistance - GeometryToleranceCM
								|| (FMath::IsNearlyEqual(Distance, BestExistingDistance,
									GeometryToleranceCM)
									&& (Area > BestExistingArea + GeometryToleranceCM
										|| (FMath::IsNearlyEqual(Area, BestExistingArea,
											GeometryToleranceCM)
											&& (ExistingEndpointCoreCellId == INDEX_NONE
												|| MainCoreCellId < ExistingEndpointCoreCellId)))))
							{
								ExistingEndpointCoreCellId = MainCoreCellId;
								BestExistingDistance = Distance;
								BestExistingArea = Area;
								BestBounds = Main.LocalBounds;
								BestBaseSourceVolumeId = Main.BodySourceVolumeId;
								BestPodiumMainCoreCellId = MainCoreCellId;
							}
						}
					}
					if (!BestBounds.IsValid || BestBaseSourceVolumeId == INDEX_NONE)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedEndpointReservationUnavailable:Volume=%d:Component=%d:Side=%s:WFC=%d:MainConflict=%d:ReservationConflict=%d:NoMainCoupling=%d:MaximumInset=%.3f"),
							Incident.Volume->VolumeId, RootIndex,
							bNegativeEndpoint ? TEXT("Negative") : TEXT("Positive"),
							WFCReachableCount, MainConflictCount,
							ReservationConflictCount, NoMainCouplingCount,
							MaximumEndpointInset);
						return false;
					}
					FReservedSharedEndpointCell& Reserved =
						ReservedSharedEndpointCells.AddDefaulted_GetRef();
					Reserved.SpanVolumeId = Incident.Volume->VolumeId;
					Reserved.bNegativeEndpoint = bNegativeEndpoint;
					Reserved.BaseSourceVolumeId = BestBaseSourceVolumeId;
					Reserved.PodiumMainCoreCellId = BestPodiumMainCoreCellId;
					Reserved.ExistingCoreCellId = ExistingEndpointCoreCellId;
					Reserved.Bounds = BestBounds;
				}
				TArray<int32> ComponentHighProjectionRegionIds;
				for (int32 DemandIndex = 0;
					DemandIndex < RequiredHighProjectionDemands.Num(); ++DemandIndex)
				{
					const FRequiredHighProjectionDemand& Demand =
						RequiredHighProjectionDemands[DemandIndex];
					FHighProjectionRegionPlan& Region =
						OutPlan.HighProjectionRegions.AddDefaulted_GetRef();
					Region.RegionId = OutPlan.HighProjectionRegions.Num() - 1;
					Region.ComponentId = RootIndex;
					Region.SemanticDemandId = Demand.SemanticDemandId;
					Region.PodiumTopCourse = PodiumTopCourse;
					Region.RequiredTopCourse = Demand.RequiredTopCourse;
					Region.TerminalSliceCourse = Demand.TerminalSliceCourse;
					Region.TerminalSliceComponentId =
						Demand.TerminalSliceComponentId;
					Region.SourceVolumeIds = Demand.SourceVolumeIds;
					Region.EntryBounds = Demand.EntryBounds;
					Region.TerminalBounds = Demand.TerminalBounds;
					Region.LocalBounds = Demand.BranchBounds;
					ComponentHighProjectionRegionIds.Add(Region.RegionId);
				}

				int32 AddedChildCount = 0;
				if (ComponentHighProjectionRegionIds.Num()
					!= RequiredFullHeightCourses.Num())
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3JointProjectionIdentityMismatch:Component=%d:Regions=%d:FullHeight=%d"),
						RootIndex, ComponentHighProjectionRegionIds.Num(),
						RequiredFullHeightCourses.Num());
					return false;
				}
				for (int32 ProjectionLocalIndex = 0;
					ProjectionLocalIndex < ComponentHighProjectionRegionIds.Num();
					++ProjectionLocalIndex)
				{
					const int32 ProjectionRegionId =
						ComponentHighProjectionRegionIds[ProjectionLocalIndex];
					if (!OutPlan.HighProjectionRegions.IsValidIndex(
						ProjectionRegionId))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3HighProjectionRegionIdentityInvalid:Component=%d:Region=%d"),
							RootIndex, ProjectionRegionId);
						return false;
					}
					FHighProjectionRegionPlan& ProjectionRegion =
						OutPlan.HighProjectionRegions[ProjectionRegionId];
					FFullHeightChildCandidateDiagnostic* FullHeightDiagnostic =
						OutPlan.FullHeightChildCandidateDiagnostics.FindByPredicate(
							[RootIndex, ProjectionLocalIndex](
								FFullHeightChildCandidateDiagnostic& Candidate)
							{
								return Candidate.ComponentId == RootIndex
									&& Candidate.LocalProjectionIndex
										== ProjectionLocalIndex;
							});
					if (FullHeightDiagnostic == nullptr)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3FullHeightDiagnosticMissing:Component=%d:Projection=%d"),
							RootIndex, ProjectionLocalIndex);
						return false;
					}
					FullHeightDiagnostic->RegionId = ProjectionRegionId;

					int32 ChildMinimumX = INDEX_NONE;
					int32 ChildMaximumX = INDEX_NONE;
					int32 ChildMinimumY = INDEX_NONE;
					int32 ChildMaximumY = INDEX_NONE;
					int32 ChildTopCourse = INDEX_NONE;
					int32 ChildBodyTopCourse = INDEX_NONE;
					int32 ChildBaseSource = INDEX_NONE;
					int32 ChildPodiumMainCoreCellId = INDEX_NONE;
					int32 BestChildCouplingPatches = -1;
					int32 BestChildMinimumSpan = -1;
					int32 BestChildImbalance = MAX_int32;
					int32 BestChildArea = -1;
					double BestChildDistance = DBL_MAX;
					bool bChildSelected = false;
					int32 ChildCandidateCount = 0;
					int32 ChildLaneConflictRejectCount = 0;
					int32 ChildReservationRejectCount = 0;
					int32 ChildNoDirectMainCouplingCount = 0;
					int32 ChildBaseRejectCount = 0;
					int32 ChildCoverageRejectCount = 0;
					for (const FJointChildFootprint& FullHeightCandidate
						: FullHeightChildCandidatesByProjection[ProjectionLocalIndex])
					{
						const int32 MinimumX = FullHeightCandidate.MinimumX;
						const int32 MaximumX = FullHeightCandidate.MaximumX;
						const int32 MinimumY = FullHeightCandidate.MinimumY;
						const int32 MaximumY = FullHeightCandidate.MaximumY;
									++ChildCandidateCount;
									if ((ChildCandidateCount & 0xFF) == 0)
									{
										CompositePlanningTimer.Checkpoint();
										if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
											TEXT("JointSelection"), OutPlan, OutError))
										{
											return false;
										}
									}
									const TArray<int32>& XStations =
										FullHeightCandidate.XStations;
									const TArray<int32>& YStations =
										FullHeightCandidate.YStations;
									if (XStations.Num() != 2 || YStations.Num() != 2)
									{
										++ChildLaneConflictRejectCount;
										continue;
									}
									if (HasCompositeLaneConflict(MinimumX, MaximumX,
										MinimumY, MaximumY, XStations, YStations))
									{
										++ChildLaneConflictRejectCount;
										bool bConflictsMain = false;
										for (const FCoreCellPlan& Existing : OutPlan.CoreCells)
										{
											if (Existing.ComponentId != RootIndex)
											{
												continue;
											}
											const int32 ExistingMinimumX = FMath::RoundToInt(
												Existing.LocalBounds.Min.X / BlockUnitsCM);
											const int32 ExistingMaximumX = FMath::RoundToInt(
												Existing.LocalBounds.Max.X / BlockUnitsCM);
											const int32 ExistingMinimumY = FMath::RoundToInt(
												Existing.LocalBounds.Min.Y / BlockUnitsCM);
											const int32 ExistingMaximumY = FMath::RoundToInt(
												Existing.LocalBounds.Max.Y / BlockUnitsCM);
											if (JointFootprintsConflict(
												MinimumX, MaximumX, MinimumY, MaximumY,
												XStations, YStations,
												ExistingMinimumX, ExistingMaximumX,
												ExistingMinimumY, ExistingMaximumY,
												Existing.XStations, Existing.YStations))
											{
												bConflictsMain |= Existing.HierarchyRole
													== ECoreHierarchyRole::PodiumMain;
											}
										}
										FullHeightDiagnostic->MainLaneConflictRejectCount +=
											bConflictsMain ? 1 : 0;
										FullHeightDiagnostic->SiblingLaneConflictRejectCount +=
											bConflictsMain ? 0 : 1;
										continue;
									}
									if (ConflictsReservedSharedEndpoint(
										MinimumX, MaximumX, MinimumY, MaximumY,
										XStations, YStations))
									{
										++ChildReservationRejectCount;
										++FullHeightDiagnostic
											->SharedReservationRejectCount;
										continue;
									}
									int32 CouplingPatches = 0;
									const int32 CandidatePodiumMainCoreCellId =
										FindBestCoupledPodiumMain(MinimumX, MaximumX,
											MinimumY, MaximumY, XStations, YStations,
											CouplingPatches);
									if (CouplingPatches <= 0)
									{
										++ChildNoDirectMainCouplingCount;
										++FullHeightDiagnostic
											->NoDirectMainCouplingCandidateCount;
									}

									const int32 BaseSource = FullHeightCandidate.BaseSource;
									if (BaseSource == INDEX_NONE
										|| !Root.GroundSourceVolumeIds.Contains(BaseSource))
									{
										++ChildBaseRejectCount;
										continue;
									}

									// WFC/source/whole-rail coverage was proved before any main
									// occupied a lane. Joint selection only adds lane, sibling and
									// shared-reservation constraints; repeating the course sweep here
									// is both redundant and a major interactive-time cost.
									const int32 ContinuousTopCourse = FullHeightCandidate.TopCourse;
									if (ContinuousTopCourse
										!= RequiredFullHeightCourses[ProjectionLocalIndex])
									{
										++ChildCoverageRejectCount;
										continue;
									}
									++FullHeightDiagnostic->JointFeasibleCandidateCount;
									const int32 MinimumSpan = FullHeightCandidate.MinimumSpan;
									const int32 Imbalance = FullHeightCandidate.Imbalance;
									const int32 Area = FullHeightCandidate.Area;
									const double Distance = FullHeightCandidate.Distance;
									bool bBetter = ContinuousTopCourse > ChildTopCourse;
									if (ContinuousTopCourse == ChildTopCourse)
									{
										bBetter = MinimumSpan > BestChildMinimumSpan;
										if (MinimumSpan == BestChildMinimumSpan)
										{
											bBetter = Imbalance < BestChildImbalance;
											if (Imbalance == BestChildImbalance)
											{
												bBetter = CouplingPatches > BestChildCouplingPatches;
												if (CouplingPatches == BestChildCouplingPatches)
												{
													bBetter = Area > BestChildArea
														|| (Area == BestChildArea
															&& Distance < BestChildDistance);
												}
											}
										}
									}
									if (bBetter)
									{
										bChildSelected = true;
										ChildMinimumX = MinimumX;
										ChildMaximumX = MaximumX;
										ChildMinimumY = MinimumY;
										ChildMaximumY = MaximumY;
										ChildTopCourse = ContinuousTopCourse;
										ChildBodyTopCourse = FullHeightCandidate.BodyTopCourse;
										ChildBaseSource = BaseSource;
										ChildPodiumMainCoreCellId =
											CandidatePodiumMainCoreCellId;
										BestChildCouplingPatches = CouplingPatches;
										BestChildMinimumSpan = MinimumSpan;
										BestChildImbalance = Imbalance;
										BestChildArea = Area;
										BestChildDistance = Distance;
									}
					}

					if (!bChildSelected)
					{
						FullHeightDiagnostic->SelectionReason =
							TEXT("FullHeightWitnessRejectedByJointConstraints");
						OutError = FString::Printf(
							TEXT("BeamC3V3FullHeightChildJointSelectionUnavailable:Component=%d:HighProjectionRegion=%d:Sources=%d:PodiumTop=%d:RequiredTop=%d:WFCFullHeight=%d:Candidates=%d:Lane=%d:MainLane=%d:SiblingLane=%d:Reservation=%d:NoDirectMainCoupling=%d:Base=%d:NotFullHeight=%d:Bounds=%s"),
							RootIndex, ProjectionRegion.RegionId,
							ProjectionRegion.SourceVolumeIds.Num(), PodiumTopCourse,
							RequiredFullHeightCourses[ProjectionLocalIndex],
							FullHeightDiagnostic->WFCFullHeightWitnessCount,
							ChildCandidateCount, ChildLaneConflictRejectCount,
							FullHeightDiagnostic->MainLaneConflictRejectCount,
							FullHeightDiagnostic->SiblingLaneConflictRejectCount,
							ChildReservationRejectCount,
							ChildNoDirectMainCouplingCount, ChildBaseRejectCount,
							ChildCoverageRejectCount,
							*ProjectionRegion.LocalBounds.ToString());
						return false;
					}

					FCoreCellPlan& Child = OutPlan.CoreCells.AddDefaulted_GetRef();
					Child.CoreCellId = OutPlan.CoreCells.Num() - 1;
					Child.ComponentId = RootIndex;
					Child.BodySourceVolumeId = ChildBaseSource;
					Child.CoreMergeRegionId = Component.CoreMergeRegionId;
					Child.HierarchyRole = ECoreHierarchyRole::TowerChild;
					Child.HighProjectionRegionId = ProjectionRegion.RegionId;
					Child.SemanticDemandId = ProjectionRegion.SemanticDemandId;
					Child.PodiumMainCoreCellId = ChildPodiumMainCoreCellId;
					Child.TopCourseIndex = ChildTopCourse;
					Child.BodyTopCourseIndex = ChildBodyTopCourse;
					Child.CompositeCoreGroupId = Component.CoreMergeRegionId;
					Child.RailCount = 2;
					Child.XStations = MakeUniformStations(
						ChildMinimumX, ChildMaximumX, Child.RailCount);
					Child.YStations = MakeUniformStations(
						ChildMinimumY, ChildMaximumY, Child.RailCount);
					Child.LocalBounds = FBox(
						Position(Child.XStations[0] * BlockUnitsCM,
							Child.YStations[0] * BlockUnitsCM, Root.GroundZCM),
						Position(Child.XStations.Last() * BlockUnitsCM,
							Child.YStations.Last() * BlockUnitsCM,
							Root.GroundZCM + ChildTopCourse * BlockUnitsCM));
					FullHeightDiagnostic->SelectedPodiumMainCoreCellId =
						Child.PodiumMainCoreCellId;
					FullHeightDiagnostic->SelectedChildBounds = Child.LocalBounds;
					FullHeightDiagnostic->SelectionReason =
						TEXT("SelectedWFCFullHeightFixedFootprint");
					TArray<int32> PreviousChildCourse;
					for (int32 Course = 0; Course < ChildTopCourse; ++Course)
					{
						const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
							? EABTSM73BeamAFrameAxis::X
							: EABTSM73BeamAFrameAxis::Y;
						const double Z = Root.GroundZCM
							+ (Course + 0.5) * BlockUnitsCM;
						const double CenterX =
							(Child.LocalBounds.Min.X + Child.LocalBounds.Max.X) * 0.5;
						const double CenterY =
							(Child.LocalBounds.Min.Y + Child.LocalBounds.Max.Y) * 0.5;
						const int32 Source = Course < Child.BodyTopCourseIndex
							? SelectProjectionSourceVolume(Root, CenterX, CenterY, Z)
							: SelectCoreProjectionSourceVolume(Root, CenterX, CenterY, Z);
						if (Source == INDEX_NONE)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3CoreSourceUnavailable:Core=%d:Course=%d:BodyTop=%d"),
								Child.CoreCellId, Course, Child.BodyTopCourseIndex);
							return false;
						}
						TArray<int32> CurrentChildCourse;
						const TArray<int32>& CrossStations =
							Axis == EABTSM73BeamAFrameAxis::X
								? Child.YStations : Child.XStations;
						for (int32 Rail = 0; Rail < CrossStations.Num(); ++Rail)
						{
							const FVector Start = Axis == EABTSM73BeamAFrameAxis::X
								? Position(Child.LocalBounds.Min.X - BlockUnitsCM * 0.5,
									CrossStations[Rail] * BlockUnitsCM, Z)
								: Position(CrossStations[Rail] * BlockUnitsCM,
									Child.LocalBounds.Min.Y - BlockUnitsCM * 0.5, Z);
							const FVector End = Axis == EABTSM73BeamAFrameAxis::X
								? Position(Child.LocalBounds.Max.X + BlockUnitsCM * 0.5,
									CrossStations[Rail] * BlockUnitsCM, Z)
								: Position(CrossStations[Rail] * BlockUnitsCM,
									Child.LocalBounds.Max.Y + BlockUnitsCM * 0.5, Z);
							const int32 MemberIndex = OutPlan.Members.Num();
							if (!AddPlannedMember(OutPlan, EOwnerKind::CoreCell,
								ESkeletonMemberKind::CoreCourse, Child.CoreCellId,
								RootIndex, Source, Child.CoreCellId, Course, Rail,
								INDEX_NONE, 0, Axis, EABTSM73BeamAMemberRole::CoreCourse,
								Start, End, OutError))
							{
								return false;
							}
							if (Course == 0)
							{
								OutPlan.Members[MemberIndex].bRequiresGroundSeat = true;
							}
							else
							{
								for (const int32 Lower : PreviousChildCourse)
								{
									AddSeat(OutPlan, MemberIndex, Lower);
								}
							}
							Child.MemberIndices.Add(MemberIndex);
							CurrentChildCourse.Add(MemberIndex);
						}
						PreviousChildCourse = MoveTemp(CurrentChildCourse);
					}
					OutPlan.Summary.MaximumCoreRailCount = FMath::Max(
						OutPlan.Summary.MaximumCoreRailCount, Child.RailCount);
					OutPlan.Summary.CoreBearingPatchCountPerInterface +=
						Child.RailCount * Child.RailCount;
					ProjectionRegion.BoundCoreCellId = Child.CoreCellId;
					ProjectionRegion.BoundPodiumMainCoreCellId =
						Child.PodiumMainCoreCellId;
					++AddedChildCount;
				}
				if (AddedChildCount != ComponentHighProjectionRegionIds.Num()
					|| AddedChildCount == 0)
				{
					OutError = FString::Printf(
						TEXT("CompositeCoreLaneUnavailable:Component=%d:HighProjectionRegions=%d:Bound=%d:PodiumTop=%d"),
						RootIndex, ComponentHighProjectionRegionIds.Num(),
						AddedChildCount, PodiumTopCourse);
					return false;
				}
				CompositePlanningTimer.Checkpoint();
				if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
					TEXT("JointSelection"), OutPlan, OutError))
				{
					return false;
				}

				// Consume the production footprint frozen before ordinary child planning.
				// The 1x1 WFC witness is diagnostic-only and is never promoted here.
				for (const FSpanInput& Incident : Spans)
				{
					if (Incident.Volume == nullptr
						|| (Incident.NegativeComponentId != RootIndex
							&& Incident.PositiveComponentId != RootIndex))
					{
						continue;
					}
					const bool bNegativeEndpoint =
						Incident.NegativeComponentId == RootIndex;
					const int32 HighestIncidentCourse = Incident.SharedCourses.IsEmpty()
						? Incident.RailBottomCourse : Incident.SharedCourses.Last();
					const int32 RequiredEndpointTopCourse = HighestIncidentCourse + 2;
					const FReservedSharedEndpointCell* ReservedEndpoint =
						ReservedSharedEndpointCells.FindByPredicate(
							[&Incident, bNegativeEndpoint](
								const FReservedSharedEndpointCell& Reserved)
							{
								return Reserved.SpanVolumeId == Incident.Volume->VolumeId
									&& Reserved.bNegativeEndpoint == bNegativeEndpoint;
							});
					if (ReservedEndpoint == nullptr || !ReservedEndpoint->Bounds.IsValid
						|| ReservedEndpoint->BaseSourceVolumeId == INDEX_NONE)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedEndpointReservationMissing:Volume=%d:Component=%d:Side=%s:RequiredTop=%d"),
							Incident.Volume->VolumeId, RootIndex,
							bNegativeEndpoint ? TEXT("Negative") : TEXT("Positive"),
							RequiredEndpointTopCourse);
						return false;
					}
					if (ReservedEndpoint->ExistingCoreCellId != INDEX_NONE)
					{
						if (!OutPlan.CoreCells.IsValidIndex(
							ReservedEndpoint->ExistingCoreCellId)
							|| OutPlan.CoreCells[ReservedEndpoint->ExistingCoreCellId]
								.TopCourseIndex < RequiredEndpointTopCourse)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3SharedEndpointExistingCoreInvalid:Volume=%d:Component=%d:Side=%s:Core=%d:RequiredTop=%d"),
								Incident.Volume->VolumeId, RootIndex,
								bNegativeEndpoint ? TEXT("Negative") : TEXT("Positive"),
								ReservedEndpoint->ExistingCoreCellId,
								RequiredEndpointTopCourse);
							return false;
						}
						continue;
					}
					const int32 SelectedMinimumX = FMath::RoundToInt(
						ReservedEndpoint->Bounds.Min.X / BlockUnitsCM);
					const int32 SelectedMaximumX = FMath::RoundToInt(
						ReservedEndpoint->Bounds.Max.X / BlockUnitsCM);
					const int32 SelectedMinimumY = FMath::RoundToInt(
						ReservedEndpoint->Bounds.Min.Y / BlockUnitsCM);
					const int32 SelectedMaximumY = FMath::RoundToInt(
						ReservedEndpoint->Bounds.Max.Y / BlockUnitsCM);
					const TArray<int32> ReservedXStations = MakeUniformStations(
						SelectedMinimumX, SelectedMaximumX, 2);
					const TArray<int32> ReservedYStations = MakeUniformStations(
						SelectedMinimumY, SelectedMaximumY, 2);
					if (ReservedXStations.Num() != 2 || ReservedYStations.Num() != 2
						|| HasCompositeLaneConflict(SelectedMinimumX, SelectedMaximumX,
							SelectedMinimumY, SelectedMaximumY,
							ReservedXStations, ReservedYStations))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedEndpointCompositeLaneUnavailable:Volume=%d:Component=%d:Side=%s:RequiredTop=%d:Bounds=%s"),
							Incident.Volume->VolumeId, RootIndex,
							bNegativeEndpoint ? TEXT("Negative") : TEXT("Positive"),
							RequiredEndpointTopCourse,
							*ReservedEndpoint->Bounds.ToString());
						return false;
					}

					FCoreCellPlan& Endpoint = OutPlan.CoreCells.AddDefaulted_GetRef();
					Endpoint.CoreCellId = OutPlan.CoreCells.Num() - 1;
					Endpoint.ComponentId = RootIndex;
					Endpoint.BodySourceVolumeId =
						ReservedEndpoint->BaseSourceVolumeId;
					Endpoint.CoreMergeRegionId = Component.CoreMergeRegionId;
					Endpoint.HierarchyRole = ECoreHierarchyRole::SharedEndpoint;
					Endpoint.SharedEndpointSpanVolumeId = Incident.Volume->VolumeId;
					Endpoint.bNegativeSharedEndpoint = bNegativeEndpoint;
					Endpoint.PodiumMainCoreCellId =
						ReservedEndpoint->PodiumMainCoreCellId;
					Endpoint.TopCourseIndex = RequiredEndpointTopCourse;
					Endpoint.BodyTopCourseIndex = RequiredEndpointTopCourse;
					Endpoint.CompositeCoreGroupId = Component.CoreMergeRegionId;
					Endpoint.RailCount = 2;
					Endpoint.XStations = MakeUniformStations(
						SelectedMinimumX, SelectedMaximumX, Endpoint.RailCount);
					Endpoint.YStations = MakeUniformStations(
						SelectedMinimumY, SelectedMaximumY, Endpoint.RailCount);
					Endpoint.LocalBounds = ReservedEndpoint->Bounds;
					TArray<int32> PreviousEndpointCourse;
					for (int32 Course = 0; Course < Endpoint.TopCourseIndex; ++Course)
					{
						const EABTSM73BeamAFrameAxis Axis = (Course & 1) == 0
							? EABTSM73BeamAFrameAxis::X
							: EABTSM73BeamAFrameAxis::Y;
						const double Z = Root.GroundZCM
							+ (Course + 0.5) * BlockUnitsCM;
						const double CenterX = Endpoint.LocalBounds.GetCenter().X;
						const double CenterY = Endpoint.LocalBounds.GetCenter().Y;
						const int32 BodySource = SelectProjectionSourceVolume(
							Root, CenterX, CenterY, Z);
						const int32 Source = BodySource != INDEX_NONE
							? BodySource : SelectCoreProjectionSourceVolume(
								Root, CenterX, CenterY, Z);
						if (Source == INDEX_NONE)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3SharedEndpointSourceUnavailable:Volume=%d:Core=%d:Course=%d"),
								Incident.Volume->VolumeId, Endpoint.CoreCellId, Course);
							return false;
						}
						if (BodySource == INDEX_NONE
							&& Endpoint.BodyTopCourseIndex == RequiredEndpointTopCourse)
						{
							Endpoint.BodyTopCourseIndex = Course;
						}
						TArray<int32> CurrentEndpointCourse;
						const TArray<int32>& CrossStations =
							Axis == EABTSM73BeamAFrameAxis::X
								? Endpoint.YStations : Endpoint.XStations;
						for (int32 Rail = 0; Rail < CrossStations.Num(); ++Rail)
						{
							const FVector Start = Axis == EABTSM73BeamAFrameAxis::X
								? Position(Endpoint.LocalBounds.Min.X
										- BlockUnitsCM * 0.5,
									CrossStations[Rail] * BlockUnitsCM, Z)
								: Position(CrossStations[Rail] * BlockUnitsCM,
									Endpoint.LocalBounds.Min.Y
										- BlockUnitsCM * 0.5, Z);
							const FVector End = Axis == EABTSM73BeamAFrameAxis::X
								? Position(Endpoint.LocalBounds.Max.X
										+ BlockUnitsCM * 0.5,
									CrossStations[Rail] * BlockUnitsCM, Z)
								: Position(CrossStations[Rail] * BlockUnitsCM,
									Endpoint.LocalBounds.Max.Y
										+ BlockUnitsCM * 0.5, Z);
							const int32 MemberIndex = OutPlan.Members.Num();
							if (!AddPlannedMember(OutPlan, EOwnerKind::CoreCell,
								ESkeletonMemberKind::CoreCourse, Endpoint.CoreCellId,
								RootIndex, Source, Endpoint.CoreCellId, Course, Rail,
								INDEX_NONE, 0, Axis,
								EABTSM73BeamAMemberRole::CoreCourse,
								Start, End, OutError))
							{
								return false;
							}
							if (Course == 0)
							{
								OutPlan.Members[MemberIndex].bRequiresGroundSeat = true;
							}
							else
							{
								for (const int32 Lower : PreviousEndpointCourse)
								{
									AddSeat(OutPlan, MemberIndex, Lower);
								}
							}
							Endpoint.MemberIndices.Add(MemberIndex);
							CurrentEndpointCourse.Add(MemberIndex);
						}
						PreviousEndpointCourse = MoveTemp(CurrentEndpointCourse);
					}
					OutPlan.Summary.MaximumCoreRailCount = FMath::Max(
						OutPlan.Summary.MaximumCoreRailCount, Endpoint.RailCount);
					OutPlan.Summary.CoreBearingPatchCountPerInterface +=
						Endpoint.RailCount * Endpoint.RailCount;
				}
				CompositePlanningTimer.Stop();
				if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
					TEXT("JointSelection"), OutPlan, OutError))
				{
					return false;
				}
			}
			if (Stage == EGenerationStage::CoreAndShared)
			{
				Component.GroundedFaceMask = 0;
				Component.PlannedMemberCount =
					OutPlan.Members.Num() - Component.FirstPlannedMemberIndex;
				continue;
			}
			if (InitialCoreCellIds.Num() != 1
				|| !OutPlan.CoreCells.IsValidIndex(InitialCoreCellIds[0]))
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3CompleteStagePrimaryCoreInvalid:Component=%d:Cores=%d"),
					RootIndex, InitialCoreCellIds.Num());
				return false;
			}
			FCoreCellPlan& Core = OutPlan.CoreCells[InitialCoreCellIds[0]];
			TArray<int32> PreviousCoreCourse;
			for (const int32 MemberIndex : Core.MemberIndices)
			{
				if (OutPlan.Members.IsValidIndex(MemberIndex)
					&& OutPlan.Members[MemberIndex].CourseIndex == CoreTopCourse - 1)
				{
					PreviousCoreCourse.Add(MemberIndex);
				}
			}

			auto FindMatchingCoreMember = [&OutPlan, &Core](
				const int32 Course, const EABTSM73BeamAFrameAxis Axis,
				const FVector& Start, const FVector& End) -> int32
			{
				FPlannedMember Requested;
				Requested.Axis = Axis;
				Requested.LocalStart = Start;
				Requested.LocalEnd = End;
				const FBox RequestedBounds = PlannedMemberBounds(Requested);
				const int32 AxisIndex = static_cast<int32>(Axis);
				const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
				for (const int32 CandidateIndex : Core.MemberIndices)
				{
					if (!OutPlan.Members.IsValidIndex(CandidateIndex))
					{
						continue;
					}
					const FPlannedMember& Candidate = OutPlan.Members[CandidateIndex];
					if (Candidate.CourseIndex == Course && Candidate.Axis == Axis)
					{
						const FBox CandidateBounds = PlannedMemberBounds(Candidate);
						if (FMath::Abs(CandidateBounds.GetCenter()[CrossAxisIndex]
								- RequestedBounds.GetCenter()[CrossAxisIndex])
								<= GeometryToleranceCM
							&& RequestedBounds.Min[AxisIndex]
								>= CandidateBounds.Min[AxisIndex] - GeometryToleranceCM
							&& RequestedBounds.Max[AxisIndex]
								<= CandidateBounds.Max[AxisIndex] + GeometryToleranceCM)
						{
							return CandidateIndex;
						}
					}
				}
				return INDEX_NONE;
			};
			auto TrimShellSegmentToCoreCap = [&OutPlan, &Core, &OutError](
				const int32 Course, const EABTSM73BeamAFrameAxis Axis,
				FVector& InOutStart, FVector& InOutEnd,
				int32& OutAbsorbingCoreMember) -> bool
			{
				OutAbsorbingCoreMember = INDEX_NONE;
				const int32 AxisIndex = static_cast<int32>(Axis);
				const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
				const double OriginalMinimum = FMath::Min(
					InOutStart[AxisIndex], InOutEnd[AxisIndex]);
				const double OriginalMaximum = FMath::Max(
					InOutStart[AxisIndex], InOutEnd[AxisIndex]);
				double Minimum = OriginalMinimum;
				double Maximum = OriginalMaximum;
				for (const int32 CandidateIndex : Core.MemberIndices)
				{
					if (!OutPlan.Members.IsValidIndex(CandidateIndex))
					{
						continue;
					}
					const FPlannedMember& Candidate = OutPlan.Members[CandidateIndex];
					if (Candidate.CourseIndex != Course || Candidate.Axis != Axis)
					{
						continue;
					}
					const FBox CandidateBounds = PlannedMemberBounds(Candidate);
					if (FMath::Abs(CandidateBounds.GetCenter()[CrossAxisIndex]
							- InOutStart[CrossAxisIndex]) > GeometryToleranceCM)
					{
						continue;
					}
					const double CoreMinimum = CandidateBounds.Min[AxisIndex];
					const double CoreMaximum = CandidateBounds.Max[AxisIndex];
					if (Maximum <= CoreMinimum + GeometryToleranceCM
						|| Minimum >= CoreMaximum - GeometryToleranceCM)
					{
						continue;
					}
					if (Minimum < CoreMinimum - GeometryToleranceCM
						&& Maximum <= CoreMaximum + GeometryToleranceCM)
					{
						Maximum = CoreMinimum;
						OutAbsorbingCoreMember = CandidateIndex;
					}
					else if (Maximum > CoreMaximum + GeometryToleranceCM
						&& Minimum >= CoreMinimum - GeometryToleranceCM)
					{
						Minimum = CoreMaximum;
						OutAbsorbingCoreMember = CandidateIndex;
					}
					else
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3ShellCrossesCoreCap:Core=%d:Course=%d:Axis=%d:Segment=%.3f..%.3f:Core=%.3f..%.3f"),
							Core.CoreCellId, Course, AxisIndex, Minimum, Maximum,
							CoreMinimum, CoreMaximum);
						return false;
					}
				}
				if (Maximum - Minimum < BlockUnitsCM - GeometryToleranceCM)
				{
					if (!OutPlan.Members.IsValidIndex(OutAbsorbingCoreMember))
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3CoreCapConsumesShellSegmentWithoutCore:Core=%d:Course=%d:Axis=%d:Remaining=%.3f"),
							Core.CoreCellId, Course, AxisIndex, Maximum - Minimum);
						return false;
					}
					// A half-block remnant cannot become a legal brick. Absorb the
					// complete adjacent cell into the core rail, yielding the requested
					// outward projection while preserving an exact end contact to the
					// next shell run.
					FPlannedMember& CoreMember = OutPlan.Members[OutAbsorbingCoreMember];
					const bool bForward = CoreMember.LocalStart[AxisIndex]
						<= CoreMember.LocalEnd[AxisIndex];
					if (OriginalMinimum < FMath::Min(CoreMember.LocalStart[AxisIndex],
							CoreMember.LocalEnd[AxisIndex]))
					{
						(bForward ? CoreMember.LocalStart : CoreMember.LocalEnd)[AxisIndex]
							= OriginalMinimum;
					}
					if (OriginalMaximum > FMath::Max(CoreMember.LocalStart[AxisIndex],
							CoreMember.LocalEnd[AxisIndex]))
					{
						(bForward ? CoreMember.LocalEnd : CoreMember.LocalStart)[AxisIndex]
							= OriginalMaximum;
					}
					const double ProjectedLength = FVector::Distance(
						CoreMember.LocalStart, CoreMember.LocalEnd);
					if (ProjectedLength > 720.0 + GeometryToleranceCM)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3CoreProjectedCourseTooLong:Core=%d:Member=%d:Length=%.3f"),
							Core.CoreCellId, OutAbsorbingCoreMember, ProjectedLength);
						return false;
					}
					OutPlan.Summary.MaximumMemberLengthCM = FMath::Max(
						OutPlan.Summary.MaximumMemberLengthCM,
						static_cast<float>(ProjectedLength));
					return true;
				}
				OutAbsorbingCoreMember = INDEX_NONE;
				if (InOutStart[AxisIndex] <= InOutEnd[AxisIndex])
				{
					InOutStart[AxisIndex] = Minimum;
					InOutEnd[AxisIndex] = Maximum;
				}
				else
				{
					InOutStart[AxisIndex] = Maximum;
					InOutEnd[AxisIndex] = Minimum;
				}
				return true;
			};
			auto AddCoreLineage = [&OutPlan, &Core](const int32 MemberIndex, const int32 Course)
			{
				if (!OutPlan.Members.IsValidIndex(MemberIndex))
				{
					return;
				}
				int32 SelectedCourse = INDEX_NONE;
				for (const int32 CoreMemberIndex : Core.MemberIndices)
				{
					if (OutPlan.Members.IsValidIndex(CoreMemberIndex))
					{
						const int32 CandidateCourse = OutPlan.Members[CoreMemberIndex].CourseIndex;
						if (CandidateCourse <= Course)
						{
							SelectedCourse = FMath::Max(SelectedCourse, CandidateCourse);
						}
					}
				}
				if (SelectedCourse == INDEX_NONE && !Core.MemberIndices.IsEmpty())
				{
					SelectedCourse = OutPlan.Members[Core.MemberIndices[0]].CourseIndex;
				}
				for (const int32 CoreMemberIndex : Core.MemberIndices)
				{
					if (OutPlan.Members.IsValidIndex(CoreMemberIndex)
						&& OutPlan.Members[CoreMemberIndex].CourseIndex == SelectedCourse)
					{
						OutPlan.Members[MemberIndex].RequiredInwardMemberIndices.Add(CoreMemberIndex);
					}
				}
			};

			for (int32 BandIndex = 0; BandIndex < Bands.Num(); ++BandIndex)
			{
				FBandState& Band = Bands[BandIndex];
				const int32 Base = Band.BaseCourse;
				bool bDirectPreviousBandSeat = false;
				if (BandIndex > 0)
				{
					const FBandState& Previous = Bands[BandIndex - 1];
					const double PreviousTopZ = Root.GroundZCM
						+ (Previous.BaseCourse + 2.0) * BlockUnitsCM;
					const double CurrentBottomZ = Root.GroundZCM + Base * BlockUnitsCM;
					bDirectPreviousBandSeat = FMath::IsNearlyEqual(
						PreviousTopZ, CurrentBottomZ, GeometryToleranceCM);
					if (CurrentBottomZ + GeometryToleranceCM < PreviousTopZ)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3BandOverlap:Component=%d:Previous=%d:Current=%d"),
							RootIndex, Previous.BaseCourse, Base);
						return false;
					}
					if (!bDirectPreviousBandSeat)
					{
						for (int32 Y = 0; Y <= NY; ++Y)
						{
							for (int32 X = 0; X <= NX; ++X)
							{
								if (!NodeTouchesOccupied(Band, X, Y, NX, NY)
									|| !NodeTouchesOccupied(Previous, X, Y, NX, NY))
								{
									continue;
								}
								if (Core.XStations.Contains(Component.XGridUnits[X])
									&& Core.YStations.Contains(Component.YGridUnits[Y]))
								{
									// The uninterrupted XY core already carries this corner on every
									// course. A Z member here would penetrate the core courses.
									continue;
								}
								const double PX = Component.XGridUnits[X] * BlockUnitsCM;
								const double PY = Component.YGridUnits[Y] * BlockUnitsCM;
								const uint8 FaceMask = NodeExteriorFaceMask(Band, X, Y, NX, NY)
									& NodeExteriorFaceMask(Previous, X, Y, NX, NY);
								int32 Source = SelectProjectionSourceVolume(
									Root, PX, PY, Root.GroundZCM + (Base + 1.0) * BlockUnitsCM);
								EOwnerKind OwnerKind = FaceMask != 0
									? EOwnerKind::ShellFace : EOwnerKind::Floor;
								ESkeletonMemberKind SkeletonKind = FaceMask != 0
									? ESkeletonMemberKind::ExteriorPost
									: ESkeletonMemberKind::ThroughCourse;
								EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::Post;
								if (Source == INDEX_NONE)
								{
									// A facade station may lie up to half a block outside the WFC
									// volume because the fixed 36 cm shell raster straddles its
									// boundary. Such a member is an exterior post, not a core
									// projection. Interior nodes never receive this fallback.
									Source = SelectExteriorPostSourceVolume(
										Root, PX, PY,
										Root.GroundZCM + (Base + 1.0) * BlockUnitsCM);
									if (FaceMask == 0 || Source == INDEX_NONE)
									{
										continue;
									}
								}
								TArray<int32> LowerSeats;
								FindMemberSeatsAtPoint(OutPlan, Previous.YMemberIndices, PX, PY, LowerSeats);
								if (LowerSeats.IsEmpty())
								{
									continue;
								}
								const int32 MemberIndex = OutPlan.Members.Num();
								if (!AddPlannedMember(OutPlan, OwnerKind, SkeletonKind,
									RootIndex, RootIndex, Source, Core.CoreCellId, Base, X, Y, FaceMask,
									EABTSM73BeamAFrameAxis::Z, Role,
									Position(PX, PY, PreviousTopZ), Position(PX, PY, CurrentBottomZ), OutError))
								{
									return false;
								}
								for (const int32 Lower : LowerSeats)
								{
									AddSeat(OutPlan, MemberIndex, Lower);
								}
								AddCoreLineage(MemberIndex, Base);
								Band.PostMemberIndices[NodeIndex(X, Y, NX)] = MemberIndex;
							}
						}
					}
				}

				const double XZ = Root.GroundZCM + (Base + 0.5) * BlockUnitsCM;
				for (int32 Y = 0; Y <= NY; ++Y)
				{
					for (int32 X = 0; X < NX; ++X)
					{
						const bool bSouth = Y > 0 && Band.OccupiedCells[CellIndex(X, Y - 1, NX)];
						const bool bNorth = Y < NY && Band.OccupiedCells[CellIndex(X, Y, NX)];
						if (!bSouth && !bNorth)
						{
							continue;
						}
						uint8 Mask = 0;
						if (bNorth && !bSouth && IsExteriorEmpty(Band, X, Y - 1, NX, NY))
						{
							Mask |= ABTSM73BeamC3V3::NegativeY;
						}
						if (bSouth && !bNorth && IsExteriorEmpty(Band, X, Y, NX, NY))
						{
							Mask |= ABTSM73BeamC3V3::PositiveY;
						}
						const int32 Source = bSouth
							? Band.CellSourceVolumeIds[CellIndex(X, Y - 1, NX)]
							: Band.CellSourceVolumeIds[CellIndex(X, Y, NX)];
						FVector Start = Position(Component.XGridUnits[X] * BlockUnitsCM,
							Component.YGridUnits[Y] * BlockUnitsCM, XZ);
						FVector End = Position(Component.XGridUnits[X + 1] * BlockUnitsCM,
							Component.YGridUnits[Y] * BlockUnitsCM, XZ);
						const int32 ExistingCore = FindMatchingCoreMember(
							Base, EABTSM73BeamAFrameAxis::X, Start, End);
						if (ExistingCore != INDEX_NONE)
						{
							OutPlan.Members[ExistingCore].FaceMask |= Mask;
							Band.XMemberIndices[Y * NX + X] = ExistingCore;
							continue;
						}
						int32 AbsorbingCoreMember = INDEX_NONE;
						if (!TrimShellSegmentToCoreCap(
							Base, EABTSM73BeamAFrameAxis::X, Start, End,
							AbsorbingCoreMember))
						{
							return false;
						}
						if (AbsorbingCoreMember != INDEX_NONE)
						{
							OutPlan.Members[AbsorbingCoreMember].FaceMask |= Mask;
							Band.XMemberIndices[Y * NX + X] = AbsorbingCoreMember;
							continue;
						}
						const ESkeletonMemberKind SkeletonKind = Mask != 0
							? ESkeletonMemberKind::FacadeCourse
							: Core.YStations.Contains(Component.YGridUnits[Y])
								? ESkeletonMemberKind::ThroughCourse
								: ESkeletonMemberKind::FloorCourse;
						const int32 MemberIndex = OutPlan.Members.Num();
						if (!AddPlannedMember(OutPlan,
							Mask ? EOwnerKind::ShellFace : EOwnerKind::Floor, SkeletonKind,
							RootIndex, RootIndex, Source, Core.CoreCellId, Base, X, Y, Mask,
							EABTSM73BeamAFrameAxis::X,
							EABTSM73BeamAMemberRole::PrimaryBeam, Start, End, OutError))
						{
							return false;
						}
						if (BandIndex == 0)
						{
							OutPlan.Members[MemberIndex].bRequiresGroundSeat = true;
						}
						else
						{
							if (bDirectPreviousBandSeat)
							{
								const FBandState& Previous = Bands[BandIndex - 1];
								for (const int32 NodeX : {X, X + 1})
								{
									FindMemberSeatsAtPoint(
										OutPlan, Previous.YMemberIndices,
										Component.XGridUnits[NodeX] * BlockUnitsCM,
										Component.YGridUnits[Y] * BlockUnitsCM,
										OutPlan.Members[MemberIndex].RequiredLowerMemberIndices);
								}
							}
							else for (const int32 NodeX : {X, X + 1})
							{
								const int32 Post = Band.PostMemberIndices[NodeIndex(NodeX, Y, NX)];
								if (Post != INDEX_NONE)
								{
									AddSeat(OutPlan, MemberIndex, Post);
								}
							}
							if (OutPlan.Members[MemberIndex].RequiredLowerMemberIndices.IsEmpty())
							{
								FString NodeDiagnostics;
								const FBandState& Previous = Bands[BandIndex - 1];
								for (const int32 NodeX : {X, X + 1})
								{
									const double PX = Component.XGridUnits[NodeX] * BlockUnitsCM;
									const double PY = Component.YGridUnits[Y] * BlockUnitsCM;
									TArray<int32> PreviousSeats;
									FindMemberSeatsAtPoint(OutPlan, Previous.YMemberIndices,
										PX, PY, PreviousSeats);
									NodeDiagnostics += FString::Printf(
										TEXT("|N%d:P=%.1f,%.1f:Source=%d:PreviousSeats=%d:Post=%d"),
										NodeX, PX, PY,
										SelectProjectionSourceVolume(Root, PX, PY,
											Root.GroundZCM + (Base + 1.0) * BlockUnitsCM),
										PreviousSeats.Num(),
										Band.PostMemberIndices[NodeIndex(NodeX, Y, NX)]);
								}
								OutError = FString::Printf(TEXT("BeamC3V3ProjectedCellHasNoPostSeat:Component=%d:Band=%d:X=%d:Y=%d%s"),
									RootIndex, Base, X, Y, *NodeDiagnostics);
								return false;
							}
						}
						AddCoreLineage(MemberIndex, Base);
						Band.XMemberIndices[Y * NX + X] = MemberIndex;
					}
				}

				const double YZ = Root.GroundZCM + (Base + 1.5) * BlockUnitsCM;
				for (int32 X = 0; X <= NX; ++X)
				{
					for (int32 Y = 0; Y < NY; ++Y)
					{
						const bool bWest = X > 0 && Band.OccupiedCells[CellIndex(X - 1, Y, NX)];
						const bool bEast = X < NX && Band.OccupiedCells[CellIndex(X, Y, NX)];
						if (!bWest && !bEast)
						{
							continue;
						}
						uint8 Mask = 0;
						if (bEast && !bWest && IsExteriorEmpty(Band, X - 1, Y, NX, NY))
						{
							Mask |= ABTSM73BeamC3V3::NegativeX;
						}
						if (bWest && !bEast && IsExteriorEmpty(Band, X, Y, NX, NY))
						{
							Mask |= ABTSM73BeamC3V3::PositiveX;
						}
						const int32 Source = bWest
							? Band.CellSourceVolumeIds[CellIndex(X - 1, Y, NX)]
							: Band.CellSourceVolumeIds[CellIndex(X, Y, NX)];
						FVector Start = Position(Component.XGridUnits[X] * BlockUnitsCM,
							Component.YGridUnits[Y] * BlockUnitsCM, YZ);
						FVector End = Position(Component.XGridUnits[X] * BlockUnitsCM,
							Component.YGridUnits[Y + 1] * BlockUnitsCM, YZ);
						const int32 ExistingCore = FindMatchingCoreMember(
							Base + 1, EABTSM73BeamAFrameAxis::Y, Start, End);
						if (ExistingCore != INDEX_NONE)
						{
							OutPlan.Members[ExistingCore].FaceMask |= Mask;
							Band.YMemberIndices[X * NY + Y] = ExistingCore;
							continue;
						}
						int32 AbsorbingCoreMember = INDEX_NONE;
						if (!TrimShellSegmentToCoreCap(
							Base + 1, EABTSM73BeamAFrameAxis::Y, Start, End,
							AbsorbingCoreMember))
						{
							return false;
						}
						if (AbsorbingCoreMember != INDEX_NONE)
						{
							OutPlan.Members[AbsorbingCoreMember].FaceMask |= Mask;
							Band.YMemberIndices[X * NY + Y] = AbsorbingCoreMember;
							continue;
						}
						const ESkeletonMemberKind SkeletonKind = Mask != 0
							? ESkeletonMemberKind::FacadeCourse
							: Core.XStations.Contains(Component.XGridUnits[X])
								? ESkeletonMemberKind::ThroughCourse
								: ESkeletonMemberKind::FloorCourse;
						const int32 MemberIndex = OutPlan.Members.Num();
						if (!AddPlannedMember(OutPlan,
							Mask ? EOwnerKind::ShellFace : EOwnerKind::Floor, SkeletonKind,
							RootIndex, RootIndex, Source, Core.CoreCellId, Base + 1, X, Y, Mask,
							EABTSM73BeamAFrameAxis::Y,
							EABTSM73BeamAMemberRole::SecondaryBeam, Start, End, OutError))
						{
							return false;
						}
						TArray<int32> Seats;
						const double PX = Component.XGridUnits[X] * BlockUnitsCM;
						for (const int32 NodeY : {Y, Y + 1})
						{
							FindMemberSeatsAtPoint(OutPlan, Band.XMemberIndices, PX,
								Component.YGridUnits[NodeY] * BlockUnitsCM, Seats);
						}
						for (const int32 Lower : Seats)
						{
							AddSeat(OutPlan, MemberIndex, Lower);
						}
						if (OutPlan.Members[MemberIndex].RequiredLowerMemberIndices.IsEmpty())
						{
							OutError = FString::Printf(TEXT("BeamC3V3UpperCourseHasNoCrossSeat:Component=%d:Band=%d:X=%d:Y=%d"),
								RootIndex, Base, X, Y);
							return false;
						}
						AddCoreLineage(MemberIndex, Base + 1);
						Band.YMemberIndices[X * NY + Y] = MemberIndex;
					}
				}
				OutPlan.Summary.SupportPlaneCount += 2;
			}

			// Convert the provisional lineage into a real, contact-by-contact tree.
			// Edges are either declared bearing contacts or collinear end contacts;
			// metadata alone may never claim that a remote facade came from the core.
			TSet<int32> CoreDerivedMembers;
			for (const int32 CoreMember : Core.MemberIndices)
			{
				CoreDerivedMembers.Add(CoreMember);
			}
			auto CollinearEndContact = [&OutPlan](const int32 AIndex, const int32 BIndex)
			{
				const FPlannedMember& A = OutPlan.Members[AIndex];
				const FPlannedMember& B = OutPlan.Members[BIndex];
				if (A.Axis != B.Axis || A.Axis == EABTSM73BeamAFrameAxis::Z
					|| A.CourseIndex != B.CourseIndex)
				{
					return false;
				}
				const FBox ABounds = PlannedMemberBounds(A);
				const FBox BBounds = PlannedMemberBounds(B);
				const int32 AxisIndex = static_cast<int32>(A.Axis);
				const int32 OtherAxis = AxisIndex == 0 ? 1 : 0;
				return FMath::Abs(ABounds.GetCenter()[OtherAxis]
						- BBounds.GetCenter()[OtherAxis]) <= GeometryToleranceCM
					&& FMath::Abs(ABounds.GetCenter().Z - BBounds.GetCenter().Z)
						<= GeometryToleranceCM
					&& (FMath::Abs(ABounds.Max[AxisIndex] - BBounds.Min[AxisIndex])
							<= GeometryToleranceCM
						|| FMath::Abs(BBounds.Max[AxisIndex] - ABounds.Min[AxisIndex])
							<= GeometryToleranceCM);
			};
			const int32 BodyMemberEnd = OutPlan.Members.Num();
			bool bLineageProgress = true;
			while (bLineageProgress)
			{
				bLineageProgress = false;
				for (int32 MemberIndex = Component.FirstPlannedMemberIndex;
					MemberIndex < BodyMemberEnd; ++MemberIndex)
				{
					FPlannedMember& Member = OutPlan.Members[MemberIndex];
					if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
						|| CoreDerivedMembers.Contains(MemberIndex))
					{
						continue;
					}
					int32 Inward = INDEX_NONE;
					for (const int32 Candidate : CoreDerivedMembers)
					{
						const bool bBearingAdjacent =
							Member.RequiredLowerMemberIndices.Contains(Candidate)
							|| OutPlan.Members[Candidate].RequiredLowerMemberIndices.Contains(MemberIndex);
						if (bBearingAdjacent || CollinearEndContact(MemberIndex, Candidate))
						{
							Inward = Inward == INDEX_NONE ? Candidate : FMath::Min(Inward, Candidate);
						}
					}
					if (Inward != INDEX_NONE)
					{
						Member.RequiredInwardMemberIndices = {Inward};
						CoreDerivedMembers.Add(MemberIndex);
						bLineageProgress = true;
					}
				}
			}
			for (int32 MemberIndex = Component.FirstPlannedMemberIndex;
				MemberIndex < BodyMemberEnd; ++MemberIndex)
			{
				const FPlannedMember& Member = OutPlan.Members[MemberIndex];
				if (Member.SkeletonKind != ESkeletonMemberKind::CoreCourse
					&& !CoreDerivedMembers.Contains(MemberIndex))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3ShellLineageDisconnected:Component=%d:Member=%d"),
						RootIndex, MemberIndex);
					return false;
				}
			}

			// The grounded Body core is the roof's first and only root. Do not create
			// a synthetic Crown seat, because doing so turns the roof into a second,
			// suspended crib that reads as an antenna in the editor. The first roof
			// course bears directly on the final CoreCourse; later courses bear on the
			// immediately preceding tapered roof course.
			if (!Root.CrownVolumeIds.IsEmpty())
			{
				auto SelectCrownForCourse = [&Root](const double CourseZ)
					-> const FABTSM73DAG5BV2Volume*
				{
					const FABTSM73DAG5BV2Volume* Exact = nullptr;
					const FABTSM73DAG5BV2Volume* Transition = nullptr;
					double TransitionDistance = DBL_MAX;
					for (const FABTSM73DAG5BV2Volume* Crown : Root.CrownVolumes)
					{
						if (CourseZ >= Crown->LocalBounds.Min.Z - GeometryToleranceCM
							&& CourseZ <= Crown->LocalBounds.Max.Z + GeometryToleranceCM)
						{
							if (Exact == nullptr || Crown->VolumeId < Exact->VolumeId)
							{
								Exact = Crown;
							}
							continue;
						}
						const double Distance = CourseZ < Crown->LocalBounds.Min.Z
							? Crown->LocalBounds.Min.Z - CourseZ
							: CourseZ - Crown->LocalBounds.Max.Z;
						if (Distance <= BlockUnitsCM * 0.5 + GeometryToleranceCM
							&& (Distance < TransitionDistance - UE_DOUBLE_SMALL_NUMBER
								|| (FMath::IsNearlyEqual(Distance, TransitionDistance)
									&& (Transition == nullptr
										|| Crown->VolumeId < Transition->VolumeId))))
						{
							Transition = Crown;
							TransitionDistance = Distance;
						}
					}
					return Exact != nullptr ? Exact : Transition;
				};
				const int32 RoofBottomCourse = CoreTopCourse;
				const double RoofBottomZ = Root.GroundZCM
					+ RoofBottomCourse * BlockUnitsCM;
				const FABTSM73DAG5BV2Volume* RoofBaseCrown = SelectCrownForCourse(
					RoofBottomZ + BlockUnitsCM * 0.5);
				if (RoofBaseCrown == nullptr)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofSeatUnavailable:Component=%d:BodyTop=%d:CoreTop=%d"),
						RootIndex, BodyTopCourse, CoreTopCourse);
					return false;
				}
				if (RoofBottomCourse != CoreTopCourse
					|| FMath::Abs(Core.LocalBounds.Max.Z - RoofBottomZ)
						> GeometryToleranceCM
					|| PreviousCoreCourse.Num() != Core.RailCount)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofCoreCourseDiscontinuity:Component=%d:BodyTop=%d:RequiredCoreTop=%d:RoofBottom=%d:CoreSeats=%d:CoreTop=%.3f:RoofZ=%.3f"),
						RootIndex, BodyTopCourse, CoreTopCourse, RoofBottomCourse,
						PreviousCoreCourse.Num(), Core.LocalBounds.Max.Z,
						RoofBottomZ);
					return false;
				}
				FBox RoofBaseBounds = RoofBaseCrown->LocalBounds;
				const double MaximumHorizontalCM = MaximumHorizontalUnits
					* static_cast<double>(BlockUnitsCM);
				for (const int32 AxisIndex : {0, 1})
				{
					if (RoofBaseBounds.Max[AxisIndex] - RoofBaseBounds.Min[AxisIndex]
						<= MaximumHorizontalCM + GeometryToleranceCM)
					{
						continue;
					}
					const double CoreCenter = Core.LocalBounds.GetCenter()[AxisIndex];
					const double WindowMinimum = FMath::Clamp(
						CoreCenter - MaximumHorizontalCM * 0.5,
						RoofBaseBounds.Min[AxisIndex],
						RoofBaseBounds.Max[AxisIndex] - MaximumHorizontalCM);
					RoofBaseBounds.Min[AxisIndex] = WindowMinimum;
					RoofBaseBounds.Max[AxisIndex] = WindowMinimum + MaximumHorizontalCM;
				}
				const int32 AvailableCourses = QRelativeFloor(
					Root.CrownTopCM, Root.GroundZCM) - RoofBottomCourse;
				if (AvailableCourses < 1)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofCourseEnvelopeInsufficient:Volume=%d:Required=1:Available=%d"),
						RoofBaseCrown->VolumeId, AvailableCourses);
					return false;
				}
				const int32 RequiredCourses = Profile.VisualComplexity.SingleTerminalRoofCourseCount > 0
					? Profile.VisualComplexity.SingleTerminalRoofCourseCount
					: FMath::Min(Profile.VisualComplexity.MaximumRoofCourseCount, AvailableCourses);
				if (RequiredCourses > AvailableCourses)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3RoofCourseEnvelopeInsufficient:Volume=%d:Required=%d:Available=%d"),
						RoofBaseCrown->VolumeId, RequiredCourses, AvailableCourses);
					return false;
				}
				const int32 RoofCourses = FMath::Clamp(RequiredCourses, 1, AvailableCourses);
				TArray<int32> PreviousCourse = PreviousCoreCourse;
				for (int32 RoofIndex = 0; RoofIndex < RoofCourses; ++RoofIndex)
				{
					const int32 AbsoluteCourse = RoofBottomCourse + RoofIndex;
					const EABTSM73BeamAFrameAxis Axis = (AbsoluteCourse & 1) == 0
						? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
					const double Z = Root.GroundZCM
						+ (AbsoluteCourse + 0.5) * BlockUnitsCM;
					const FABTSM73DAG5BV2Volume* CourseCrown =
						SelectCrownForCourse(Z);
					if (CourseCrown == nullptr)
					{
						FString CrownDiagnostics;
						for (const FABTSM73DAG5BV2Volume* Crown : Root.CrownVolumes)
						{
							CrownDiagnostics += FString::Printf(
								TEXT("|V%d=%.1f,%.1f,%.1f..%.1f,%.1f,%.1f"),
								Crown->VolumeId,
								Crown->LocalBounds.Min.X, Crown->LocalBounds.Min.Y,
								Crown->LocalBounds.Min.Z, Crown->LocalBounds.Max.X,
								Crown->LocalBounds.Max.Y, Crown->LocalBounds.Max.Z);
						}
						OutError = FString::Printf(
							TEXT("BeamC3V3RoofCourseTransitionUnavailable:Component=%d:Course=%d:Z=%.3f:Crowns=%s"),
							RootIndex, RoofIndex, Z, *CrownDiagnostics);
						return false;
					}
					TArray<int32> CurrentCourse;
					const FBox TaperedBounds = ABTSM73BeamA::SemanticRoofBearingCourseBounds(
						CourseCrown->LocalBounds, CourseCrown->Primitive, RoofIndex,
						RoofCourses, Axis, BlockUnitsCM);
					double XMinimum = FMath::Max(
						TaperedBounds.Min.X, RoofBaseBounds.Min.X);
					double XMaximum = FMath::Min(
						TaperedBounds.Max.X, RoofBaseBounds.Max.X);
					double YMinimum = FMath::Max(
						TaperedBounds.Min.Y, RoofBaseBounds.Min.Y);
					double YMaximum = FMath::Min(
						TaperedBounds.Max.Y, RoofBaseBounds.Max.Y);
					if (RoofIndex + 1 < RoofCourses)
					{
						const int32 NextAbsoluteCourse = AbsoluteCourse + 1;
						const EABTSM73BeamAFrameAxis NextAxis =
							(NextAbsoluteCourse & 1) == 0
								? EABTSM73BeamAFrameAxis::X
								: EABTSM73BeamAFrameAxis::Y;
						const double NextZ = Root.GroundZCM
							+ (NextAbsoluteCourse + 0.5) * BlockUnitsCM;
						const FABTSM73DAG5BV2Volume* NextCrown =
							SelectCrownForCourse(NextZ);
						if (NextCrown == nullptr)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3RoofCourseTransitionUnavailable:Component=%d:Course=%d:Z=%.3f"),
								RootIndex, RoofIndex + 1, NextZ);
							return false;
						}
						const FBox NextTaperedBounds =
							ABTSM73BeamA::SemanticRoofBearingCourseBounds(
								NextCrown->LocalBounds, NextCrown->Primitive,
								RoofIndex + 1, RoofCourses, NextAxis, BlockUnitsCM);
						if (Axis == EABTSM73BeamAFrameAxis::X)
						{
							YMinimum = FMath::Max(YMinimum,
								FMath::Max(NextTaperedBounds.Min.Y, RoofBaseBounds.Min.Y));
							YMaximum = FMath::Min(YMaximum,
								FMath::Min(NextTaperedBounds.Max.Y, RoofBaseBounds.Max.Y));
						}
						else
						{
							XMinimum = FMath::Max(XMinimum,
								FMath::Max(NextTaperedBounds.Min.X, RoofBaseBounds.Min.X));
							XMaximum = FMath::Min(XMaximum,
								FMath::Min(NextTaperedBounds.Max.X, RoofBaseBounds.Max.X));
						}
					}
					const int32 AxisIndex = static_cast<int32>(Axis);
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					double SupportAxisMinimum = DBL_MAX;
					double SupportAxisMaximum = -DBL_MAX;
					double SupportCrossMinimum = -DBL_MAX;
					double SupportCrossMaximum = DBL_MAX;
					for (const int32 LowerIndex : PreviousCourse)
					{
						if (!OutPlan.Members.IsValidIndex(LowerIndex)
							|| OutPlan.Members[LowerIndex].Axis == Axis
							|| OutPlan.Members[LowerIndex].Axis == EABTSM73BeamAFrameAxis::Z)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3RoofCourseSeatAxisInvalid:Component=%d:Course=%d:Seat=%d"),
								RootIndex, AbsoluteCourse, LowerIndex);
							return false;
						}
						const FPlannedMember& Lower = OutPlan.Members[LowerIndex];
						const double FixedAxisStation = Lower.LocalStart[AxisIndex];
						SupportAxisMinimum = FMath::Min(
							SupportAxisMinimum, FixedAxisStation);
						SupportAxisMaximum = FMath::Max(
							SupportAxisMaximum, FixedAxisStation);
						SupportCrossMinimum = FMath::Max(SupportCrossMinimum,
							FMath::Min(Lower.LocalStart[CrossAxisIndex],
								Lower.LocalEnd[CrossAxisIndex]));
						SupportCrossMaximum = FMath::Min(SupportCrossMaximum,
							FMath::Max(Lower.LocalStart[CrossAxisIndex],
								Lower.LocalEnd[CrossAxisIndex]));
					}
					double* CourseAxisMinimum = AxisIndex == 0 ? &XMinimum : &YMinimum;
					double* CourseAxisMaximum = AxisIndex == 0 ? &XMaximum : &YMaximum;
					double* CourseCrossMinimum = AxisIndex == 0 ? &YMinimum : &XMinimum;
					double* CourseCrossMaximum = AxisIndex == 0 ? &YMaximum : &XMaximum;
					// The current course crosses the two fixed stations of the lower
					// orthogonal rails.  Extending half a block past each station gives
					// both crossings a complete 36 x 36 bearing instead of the former
					// half-block endpoint contact.
					*CourseAxisMinimum = FMath::Min(*CourseAxisMinimum,
						SupportAxisMinimum - BlockUnitsCM * 0.5);
					*CourseAxisMaximum = FMath::Max(*CourseAxisMaximum,
						SupportAxisMaximum + BlockUnitsCM * 0.5);
					*CourseCrossMinimum = FMath::Max(
						*CourseCrossMinimum,
						SupportCrossMinimum + BlockUnitsCM * 0.5);
					*CourseCrossMaximum = FMath::Min(
						*CourseCrossMaximum,
						SupportCrossMaximum - BlockUnitsCM * 0.5);
					if (*CourseAxisMaximum - *CourseAxisMinimum
						> MaximumHorizontalCM + GeometryToleranceCM
						|| *CourseCrossMaximum - *CourseCrossMinimum
							< BlockUnitsCM - GeometryToleranceCM)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3RoofCourseSeatFootprintUnavailable:Component=%d:Course=%d:Axis=%d:Along=%.3f..%.3f:Cross=%.3f..%.3f:SupportAlong=%.3f..%.3f:SupportCross=%.3f..%.3f"),
							RootIndex, AbsoluteCourse, AxisIndex,
							*CourseAxisMinimum, *CourseAxisMaximum,
							*CourseCrossMinimum, *CourseCrossMaximum,
							SupportAxisMinimum, SupportAxisMaximum,
							SupportCrossMinimum, SupportCrossMaximum);
						return false;
					}
					TArray<double> CrossCandidates = {
						*CourseCrossMinimum, *CourseCrossMaximum};
					TArray<FBox> RoofObstacleBounds;
					const FBox CourseSlab(
						Axis == EABTSM73BeamAFrameAxis::X
							? FVector(*CourseAxisMinimum - BlockUnitsCM * 0.5,
								*CourseCrossMinimum - BlockUnitsCM * 0.5,
								Z - BlockUnitsCM * 0.5)
							: FVector(*CourseCrossMinimum - BlockUnitsCM * 0.5,
								*CourseAxisMinimum - BlockUnitsCM * 0.5,
								Z - BlockUnitsCM * 0.5),
						Axis == EABTSM73BeamAFrameAxis::X
							? FVector(*CourseAxisMaximum + BlockUnitsCM * 0.5,
								*CourseCrossMaximum + BlockUnitsCM * 0.5,
								Z + BlockUnitsCM * 0.5)
							: FVector(*CourseCrossMaximum + BlockUnitsCM * 0.5,
								*CourseAxisMaximum + BlockUnitsCM * 0.5,
								Z + BlockUnitsCM * 0.5));
					for (const FPlannedMember& Existing : OutPlan.Members)
					{
						const FBox ExistingBounds = PlannedMemberBounds(Existing);
						if (SkeletonV3OverlapLength(CourseSlab.Min[AxisIndex],
							CourseSlab.Max[AxisIndex], ExistingBounds.Min[AxisIndex],
							ExistingBounds.Max[AxisIndex]) <= GeometryToleranceCM
							|| SkeletonV3OverlapLength(CourseSlab.Min.Z, CourseSlab.Max.Z,
								ExistingBounds.Min.Z, ExistingBounds.Max.Z)
								<= GeometryToleranceCM)
						{
							continue;
						}
						RoofObstacleBounds.Add(ExistingBounds);
						CrossCandidates.Add(FMath::Clamp(
							ExistingBounds.Min[CrossAxisIndex] - BlockUnitsCM * 0.5
								+ GeometryToleranceCM,
							*CourseCrossMinimum, *CourseCrossMaximum));
						CrossCandidates.Add(FMath::Clamp(
							ExistingBounds.Max[CrossAxisIndex] + BlockUnitsCM * 0.5
								- GeometryToleranceCM,
							*CourseCrossMinimum, *CourseCrossMaximum));
					}
					CrossCandidates.Sort();
					for (int32 CandidateIndex = CrossCandidates.Num() - 1;
						CandidateIndex > 0; --CandidateIndex)
					{
						if (FMath::Abs(CrossCandidates[CandidateIndex]
							- CrossCandidates[CandidateIndex - 1]) <= UE_DOUBLE_SMALL_NUMBER)
						{
							CrossCandidates.RemoveAt(CandidateIndex);
						}
					}
					auto MakeRoofProbe = [Axis, Z, CourseAxisMinimum, CourseAxisMaximum](
						const double CrossStation)
					{
						FPlannedMember Probe;
						Probe.Axis = Axis;
						Probe.LocalStart = Axis == EABTSM73BeamAFrameAxis::X
							? Position(*CourseAxisMinimum, CrossStation, Z)
							: Position(CrossStation, *CourseAxisMinimum, Z);
						Probe.LocalEnd = Axis == EABTSM73BeamAFrameAxis::X
							? Position(*CourseAxisMaximum, CrossStation, Z)
							: Position(CrossStation, *CourseAxisMaximum, Z);
						return Probe;
					};
					TArray<double> FeasibleCrossCandidates;
					for (const double Candidate : CrossCandidates)
					{
						const FBox CandidateBounds = PlannedMemberBounds(
							MakeRoofProbe(Candidate));
						bool bPenetrates = false;
						for (const FBox& ExistingBounds : RoofObstacleBounds)
						{
							if (BoxesPenetrate(CandidateBounds, ExistingBounds))
							{
								bPenetrates = true;
								break;
							}
						}
						if (!bPenetrates)
						{
							FeasibleCrossCandidates.Add(Candidate);
						}
					}
					const double SelectedCrossA = FeasibleCrossCandidates.IsEmpty()
						? 0.0 : FeasibleCrossCandidates[0];
					const double SelectedCrossB = FeasibleCrossCandidates.IsEmpty()
						? 0.0 : FeasibleCrossCandidates.Last();
					const double BestSpread = SelectedCrossB - SelectedCrossA;
					if (FeasibleCrossCandidates.Num() < 2
						|| BestSpread < BlockUnitsCM - GeometryToleranceCM)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3RoofCourseNonPenetratingStationsUnavailable:Component=%d:Course=%d:Axis=%d:Along=%.3f..%.3f:Cross=%.3f..%.3f:Candidates=%d"),
							RootIndex, AbsoluteCourse, AxisIndex,
							*CourseAxisMinimum, *CourseAxisMaximum,
							*CourseCrossMinimum, *CourseCrossMaximum,
							FeasibleCrossCandidates.Num());
						return false;
					}
					for (int32 RailIndex = 0; RailIndex < 2; ++RailIndex)
					{
						const double CrossStation = RailIndex == 0
							? SelectedCrossA : SelectedCrossB;
						const FVector Start = Axis == EABTSM73BeamAFrameAxis::X
							? Position(*CourseAxisMinimum, CrossStation, Z)
							: Position(CrossStation, *CourseAxisMinimum, Z);
						const FVector End = Axis == EABTSM73BeamAFrameAxis::X
							? Position(*CourseAxisMaximum, CrossStation, Z)
							: Position(CrossStation, *CourseAxisMaximum, Z);
						const int32 MemberIndex = OutPlan.Members.Num();
						if (!AddPlannedMember(OutPlan, EOwnerKind::Roof,
							ESkeletonMemberKind::RoofCourse, CourseCrown->VolumeId,
							RootIndex, CourseCrown->VolumeId, Core.CoreCellId,
							AbsoluteCourse,
							RailIndex, 0, 0, Axis, EABTSM73BeamAMemberRole::RoofCourse,
							Start, End, OutError))
						{
							return false;
						}
						for (const int32 Lower : PreviousCourse)
						{
							AddSeat(OutPlan, MemberIndex, Lower);
						}
						OutPlan.Members[MemberIndex].RequiredInwardMemberIndices = {
							PreviousCourse[0]};
						CurrentCourse.Add(MemberIndex);
						++OutPlan.Summary.RoofMemberCount;
					}
					PreviousCourse = MoveTemp(CurrentCourse);
				}
			}

			Component.GroundedFaceMask = 0;
			Component.PlannedMemberCount = OutPlan.Members.Num() - Component.FirstPlannedMemberIndex;
		}

		TSet<int32> RemovedMembers;
		TMap<int32, int32> ReplacementMembers;
		for (const FSpanInput& Span : Spans)
		{
			const FABTSM73DAG5BV2Volume& Volume = *Span.Volume;
			const int32 AxisIndex = Span.SpanAxis;
			const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
			const EABTSM73BeamAFrameAxis RailAxis = AxisIndex == 0
				? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
			const EABTSM73BeamAFrameAxis DiaphragmAxis = AxisIndex == 0
				? EABTSM73BeamAFrameAxis::Y : EABTSM73BeamAFrameAxis::X;
			const int32 HighestSharedCourse = Span.SharedCourses.IsEmpty()
				? Span.RailBottomCourse : Span.SharedCourses.Last();
			AppendSharedEndpointReachabilityDiagnostics(
				Roots[Span.NegativeComponentId], Span.NegativeComponentId,
				Volume, true, HighestSharedCourse + 2,
				OutPlan.SharedEndpointReachabilityDiagnostics);
			AppendSharedEndpointReachabilityDiagnostics(
				Roots[Span.PositiveComponentId], Span.PositiveComponentId,
				Volume, false, HighestSharedCourse + 2,
				OutPlan.SharedEndpointReachabilityDiagnostics);
			auto SelectEndpointCore = [&OutPlan, &Span, RailAxis,
				HighestSharedCourse, AxisIndex](const int32 ComponentId,
				const bool bNegative) -> FCoreCellPlan*
			{
				FCoreCellPlan* Best = nullptr;
				bool bBestDedicatedEndpoint = false;
				double BestDistance = DBL_MAX;
				double BestArea = -DBL_MAX;
				for (FCoreCellPlan& Candidate : OutPlan.CoreCells)
				{
					if (Candidate.ComponentId != ComponentId
						|| Candidate.TopCourseIndex < HighestSharedCourse + 2)
					{
						continue;
					}
					const bool bDedicatedEndpoint =
						Candidate.HierarchyRole == ECoreHierarchyRole::SharedEndpoint
						&& Candidate.SharedEndpointSpanVolumeId == Span.Volume->VolumeId
						&& Candidate.bNegativeSharedEndpoint == bNegative;
					if (Candidate.HierarchyRole == ECoreHierarchyRole::SharedEndpoint
						&& Candidate.SharedEndpointSpanVolumeId != INDEX_NONE
						&& !bDedicatedEndpoint)
					{
						continue;
					}
					const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
					if (SkeletonV3OverlapLength(
						Candidate.LocalBounds.Min[CrossAxisIndex] - BlockUnitsCM * 0.5,
						Candidate.LocalBounds.Max[CrossAxisIndex] + BlockUnitsCM * 0.5,
						Span.Volume->LocalBounds.Min[CrossAxisIndex],
						Span.Volume->LocalBounds.Max[CrossAxisIndex])
						<= GeometryToleranceCM)
					{
						continue;
					}
					bool bAllCoursesPresent = true;
					for (const int32 Course : Span.SharedCourses)
					{
						int32 RailCount = 0;
						for (const int32 MemberIndex : Candidate.MemberIndices)
						{
							RailCount += OutPlan.Members.IsValidIndex(MemberIndex)
								&& OutPlan.Members[MemberIndex].SkeletonKind
									== ESkeletonMemberKind::CoreCourse
								&& OutPlan.Members[MemberIndex].CourseIndex == Course
								&& OutPlan.Members[MemberIndex].Axis == RailAxis ? 1 : 0;
						}
						bAllCoursesPresent &= RailCount == Candidate.RailCount;
					}
					if (!bAllCoursesPresent)
					{
						continue;
					}
					const double InnerPlane = bNegative
						? Candidate.LocalBounds.Max[AxisIndex] + BlockUnitsCM * 0.5
						: Candidate.LocalBounds.Min[AxisIndex] - BlockUnitsCM * 0.5;
					const double OpeningPlane = bNegative
						? Span.Volume->SpanOpeningMinCM : Span.Volume->SpanOpeningMaxCM;
					const double Distance = FMath::Abs(OpeningPlane - InnerPlane);
					const FVector Size = Candidate.LocalBounds.GetSize();
					const double Area = Size.X * Size.Y;
					const bool bBetterWithinPriority =
						Distance < BestDistance - GeometryToleranceCM
						|| (FMath::IsNearlyEqual(Distance, BestDistance,
							GeometryToleranceCM)
							&& (Area > BestArea + GeometryToleranceCM
								|| (FMath::IsNearlyEqual(Area, BestArea,
									GeometryToleranceCM)
									&& (Best == nullptr
										|| Candidate.CoreCellId < Best->CoreCellId))));
					if ((bDedicatedEndpoint && !bBestDedicatedEndpoint)
						|| (bDedicatedEndpoint == bBestDedicatedEndpoint
							&& bBetterWithinPriority))
					{
						Best = &Candidate;
						bBestDedicatedEndpoint = bDedicatedEndpoint;
						BestDistance = Distance;
						BestArea = Area;
					}
				}
				return Best;
			};
			FCoreCellPlan* NegativeCore = SelectEndpointCore(
				Span.NegativeComponentId, true);
			FCoreCellPlan* PositiveCore = SelectEndpointCore(
				Span.PositiveComponentId, false);
			if (NegativeCore == nullptr || PositiveCore == nullptr)
			{
				OutError = FString::Printf(
					TEXT("BeamC3V3SharedCourseEndpointCoreMissing:Volume=%d:RequiredTop=%d:Negative=%d:Positive=%d"),
					Volume.VolumeId, HighestSharedCourse + 2,
					NegativeCore != nullptr ? NegativeCore->CoreCellId : INDEX_NONE,
					PositiveCore != nullptr ? PositiveCore->CoreCellId : INDEX_NONE);
				return false;
			}
			FSharedCourseIntent& Intent =
				OutPlan.SharedCourseIntents.AddDefaulted_GetRef();
			Intent.SpanVolumeId = Volume.VolumeId;
			Intent.NegativeCoreCellId = NegativeCore->CoreCellId;
			Intent.PositiveCoreCellId = PositiveCore->CoreCellId;
			Intent.Axis = Span.SpanAxis == 0
				? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
			Intent.CourseIndices = Span.SharedCourses;
			Intent.OpeningMinimumCM = Volume.SpanOpeningMinCM;
			Intent.OpeningMaximumCM = Volume.SpanOpeningMaxCM;
			Intent.PredictedBounds = NegativeCore->LocalBounds + PositiveCore->LocalBounds;
			Intent.PredictedBounds += Volume.LocalBounds;
			const double NegativeCrossWidth = NegativeCore->LocalBounds.GetSize()[CrossAxisIndex];
			const double PositiveCrossWidth = PositiveCore->LocalBounds.GetSize()[CrossAxisIndex];
			const double NegativeArea = NegativeCore->LocalBounds.GetSize().X
				* NegativeCore->LocalBounds.GetSize().Y;
			const double PositiveArea = PositiveCore->LocalBounds.GetSize().X
				* PositiveCore->LocalBounds.GetSize().Y;
			const bool bNegativeIsDonor =
				NegativeCrossWidth < PositiveCrossWidth - GeometryToleranceCM
				|| (FMath::IsNearlyEqual(NegativeCrossWidth, PositiveCrossWidth,
					GeometryToleranceCM)
					&& (NegativeArea < PositiveArea - GeometryToleranceCM
						|| (FMath::IsNearlyEqual(NegativeArea, PositiveArea,
							GeometryToleranceCM)
							&& NegativeCore->CoreCellId < PositiveCore->CoreCellId)));
			FCoreCellPlan* DonorCore = bNegativeIsDonor ? NegativeCore : PositiveCore;
			FCoreCellPlan* ReceiverCore = bNegativeIsDonor ? PositiveCore : NegativeCore;
			TArray<int32> FirstSharedRails;
			for (const int32 SharedCourse : Span.SharedCourses)
			{
				auto CollectLocalRails = [&OutPlan, RailAxis, SharedCourse](
					const FCoreCellPlan& Core, TArray<int32>& OutRails)
				{
					OutRails.Reset();
					for (const int32 MemberIndex : Core.MemberIndices)
					{
						if (!OutPlan.Members.IsValidIndex(MemberIndex))
						{
							continue;
						}
						const FPlannedMember& Member = OutPlan.Members[MemberIndex];
						if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
							&& Member.CourseIndex == SharedCourse
							&& Member.Axis == RailAxis)
						{
							OutRails.Add(MemberIndex);
						}
					}
				};
				TArray<int32> DonorRails;
				TArray<int32> ReceiverRails;
				CollectLocalRails(*DonorCore, DonorRails);
				CollectLocalRails(*ReceiverCore, ReceiverRails);
				if (DonorRails.Num() != DonorCore->RailCount
					|| ReceiverRails.Num() != ReceiverCore->RailCount)
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SharedCourseEndpointSlotUnavailable:Volume=%d:Course=%d:Donor=%d/%d:Receiver=%d/%d"),
						Volume.VolumeId, SharedCourse,
						DonorRails.Num(), DonorCore->RailCount,
						ReceiverRails.Num(), ReceiverCore->RailCount);
					return false;
				}
				auto SortByCrossStation = [&OutPlan, CrossAxisIndex](
					const int32 A, const int32 B)
				{
					const double ACross = PlannedMemberBounds(
						OutPlan.Members[A]).GetCenter()[CrossAxisIndex];
					const double BCross = PlannedMemberBounds(
						OutPlan.Members[B]).GetCenter()[CrossAxisIndex];
					return !FMath::IsNearlyEqual(ACross, BCross, GeometryToleranceCM)
						? ACross < BCross : A < B;
				};
				DonorRails.Sort(SortByCrossStation);
				ReceiverRails.Sort(SortByCrossStation);
				TArray<int32> CurrentSharedRails;
				for (int32 RailIndex = 0; RailIndex < DonorRails.Num(); ++RailIndex)
				{
					const int32 SharedIndex = DonorRails[RailIndex];
					const FBox DonorBounds = PlannedMemberBounds(OutPlan.Members[SharedIndex]);
					const double Cross = DonorBounds.GetCenter()[CrossAxisIndex];
					const double ReceiverCrossMinimum = ReceiverCore->LocalBounds.Min[CrossAxisIndex]
						- BlockUnitsCM * 0.5;
					const double ReceiverCrossMaximum = ReceiverCore->LocalBounds.Max[CrossAxisIndex]
						+ BlockUnitsCM * 0.5;
					if (Cross - BlockUnitsCM * 0.5
						< ReceiverCrossMinimum - GeometryToleranceCM
						|| Cross + BlockUnitsCM * 0.5
							> ReceiverCrossMaximum + GeometryToleranceCM)
					{
						OutError = FString::Printf(
							TEXT("BeamC3V3SharedCourseCrossLaneUnavailable:Volume=%d:Course=%d:Lane=%d:Cross=%.3f:Receiver=%d:Range=%.3f..%.3f"),
							Volume.VolumeId, SharedCourse, RailIndex, Cross,
							ReceiverCore->CoreCellId, ReceiverCrossMinimum,
							ReceiverCrossMaximum);
						return false;
					}

					int32 ReceiverConflict = INDEX_NONE;
					for (const int32 ReceiverRail : ReceiverRails)
					{
						const FBox ReceiverBounds = PlannedMemberBounds(
							OutPlan.Members[ReceiverRail]);
						if (FMath::Abs(ReceiverBounds.GetCenter()[CrossAxisIndex] - Cross)
							<= GeometryToleranceCM)
						{
							ReceiverConflict = ReceiverRail;
							break;
						}
					}

					const double NegativeMinimum = NegativeCore->LocalBounds.Min[AxisIndex]
						- BlockUnitsCM * 0.5;
					const double NegativeMaximum = NegativeCore->LocalBounds.Max[AxisIndex]
						+ BlockUnitsCM * 0.5;
					const double PositiveMinimum = PositiveCore->LocalBounds.Min[AxisIndex]
						- BlockUnitsCM * 0.5;
					const double PositiveMaximum = PositiveCore->LocalBounds.Max[AxisIndex]
						+ BlockUnitsCM * 0.5;
					const double Minimum = FMath::Min(NegativeMinimum, PositiveMinimum);
					const double Maximum = FMath::Max(NegativeMaximum, PositiveMaximum);
					TArray<FVector2D> Segments;
					int32 CrossSegmentOrdinal = 0;
					if (Maximum - Minimum <= 720.0 + GeometryToleranceCM)
					{
						Segments.Emplace(Minimum, Maximum);
					}
					else
					{
						const double CrossMinimum = NegativeMaximum - BlockUnitsCM;
						const double CrossMaximum = PositiveMinimum + BlockUnitsCM;
						if (CrossMaximum - CrossMinimum > 720.0 + GeometryToleranceCM)
						{
							OutError = FString::Printf(
								TEXT("BeamC3V3SharedCourseCrossSegmentTooLong:Volume=%d:Course=%d:Lane=%d:Length=%.3f:Opening=%.3f..%.3f"),
								Volume.VolumeId, SharedCourse, RailIndex,
								CrossMaximum - CrossMinimum,
								Volume.SpanOpeningMinCM, Volume.SpanOpeningMaxCM);
							return false;
						}
						auto AppendTail = [&Segments](double TailMinimum,
							double TailMaximum)
						{
							while (TailMaximum - TailMinimum
								> 720.0 + GeometryToleranceCM)
							{
								Segments.Emplace(TailMinimum, TailMinimum + 720.0);
								TailMinimum += 720.0;
							}
							if (TailMaximum - TailMinimum
								>= BlockUnitsCM - GeometryToleranceCM)
							{
								Segments.Emplace(TailMinimum, TailMaximum);
							}
						};
						AppendTail(Minimum, CrossMinimum);
						CrossSegmentOrdinal = Segments.Num();
						Segments.Emplace(CrossMinimum, CrossMaximum);
						AppendTail(CrossMaximum, Maximum);
					}

					FSharedCourseLanePlan& Lane = Intent.LanePlans.AddDefaulted_GetRef();
					Lane.CourseIndex = SharedCourse;
					Lane.LaneIndex = RailIndex;
					Lane.DonorCoreCellId = DonorCore->CoreCellId;
					Lane.ReceiverCoreCellId = ReceiverCore->CoreCellId;
					Lane.CrossStationCM = Cross;
					Lane.RequiredMinimumCM = Minimum;
					Lane.RequiredMaximumCM = Maximum;
					for (int32 SegmentOrdinal = 0;
						SegmentOrdinal < Segments.Num(); ++SegmentOrdinal)
					{
						const FVector Start = RailAxis == EABTSM73BeamAFrameAxis::X
							? Position(Segments[SegmentOrdinal].X, Cross,
								DonorBounds.GetCenter().Z)
							: Position(Cross, Segments[SegmentOrdinal].X,
								DonorBounds.GetCenter().Z);
						const FVector End = RailAxis == EABTSM73BeamAFrameAxis::X
							? Position(Segments[SegmentOrdinal].Y, Cross,
								DonorBounds.GetCenter().Z)
							: Position(Cross, Segments[SegmentOrdinal].Y,
								DonorBounds.GetCenter().Z);
						int32 SegmentMemberIndex = SharedIndex;
						if (SegmentOrdinal != CrossSegmentOrdinal)
						{
							SegmentMemberIndex = OutPlan.Members.Num();
							if (!AddPlannedMember(OutPlan, EOwnerKind::SupportedSpan,
								ESkeletonMemberKind::SharedCourse, Volume.VolumeId,
								INDEX_NONE, Volume.VolumeId, INDEX_NONE, SharedCourse,
								RailIndex, SegmentOrdinal, 0, RailAxis,
								EABTSM73BeamAMemberRole::BridgeRail, Start, End, OutError))
							{
								return false;
							}
						}
						FPlannedMember& Segment = OutPlan.Members[SegmentMemberIndex];
						Segment.OwnerKind = EOwnerKind::SupportedSpan;
						Segment.SkeletonKind = ESkeletonMemberKind::SharedCourse;
						Segment.OwnerId = Volume.VolumeId;
						Segment.ComponentId = INDEX_NONE;
						Segment.SourceVolumeId = Volume.VolumeId;
						Segment.OriginCoreCellId = INDEX_NONE;
						Segment.CourseIndex = SharedCourse;
						Segment.StationA = RailIndex;
						Segment.StationB = SegmentOrdinal;
						Segment.FaceMask = 0;
						Segment.Axis = RailAxis;
						Segment.Role = EABTSM73BeamAMemberRole::BridgeRail;
						Segment.LocalStart = Start;
						Segment.LocalEnd = End;
						Segment.EndpointCoreCellIds = {
							NegativeCore->CoreCellId, PositiveCore->CoreCellId};
						Segment.RequiredInwardMemberIndices.Reset();
						Segment.SharedLaneIndex = RailIndex;
						Segment.SharedSegmentIndex = SegmentOrdinal;
						Segment.bSharedCrossCoreSegment =
							SegmentOrdinal == CrossSegmentOrdinal;
						Lane.SegmentMemberIndices.Add(SegmentMemberIndex);
						if (Segment.bSharedCrossCoreSegment)
						{
							Lane.CrossCoreSegmentMemberIndex = SegmentMemberIndex;
						}
						OutPlan.Summary.MaximumMemberLengthCM = FMath::Max(
							OutPlan.Summary.MaximumMemberLengthCM,
							static_cast<float>(Segments[SegmentOrdinal].Y
								- Segments[SegmentOrdinal].X));
					}

					if (ReceiverConflict != INDEX_NONE)
					{
						RemovedMembers.Add(ReceiverConflict);
						ReplacementMembers.Add(
							ReceiverConflict, Lane.CrossCoreSegmentMemberIndex);
						for (int32& MemberIndex : ReceiverCore->MemberIndices)
						{
							if (MemberIndex == ReceiverConflict)
							{
								MemberIndex = Lane.CrossCoreSegmentMemberIndex;
							}
						}
						Lane.bReceiverSlotReplaced = true;
					}
					CurrentSharedRails.Add(Lane.CrossCoreSegmentMemberIndex);
				}
				if (SharedCourse == Span.SharedCourses[0])
				{
					FirstSharedRails = CurrentSharedRails;
				}
			}

			// Rungs above the first long pair make the bridge a real course band
			// instead of two isolated rails. They remain inside the semantic span
			// and do not masquerade as endpoint-core slots.
			const double GroundZ = OutPlan.Components[Span.NegativeComponentId].GroundPlaneZCM;
			const int32 DiaphragmCourse = Span.SharedCourses[0] + 1;
			const double DiaphragmTopZ = GroundZ
				+ (DiaphragmCourse + 1) * BlockUnitsCM;
			if (FirstSharedRails.Num() >= 2
				&& DiaphragmTopZ <= Volume.LocalBounds.Max.Z + GeometryToleranceCM)
			{
				const FBox RailA = PlannedMemberBounds(OutPlan.Members[FirstSharedRails[0]]);
				const FBox RailB = PlannedMemberBounds(OutPlan.Members[FirstSharedRails.Last()]);
				const double CrossMinimum = FMath::Min(
					RailA.GetCenter()[CrossAxisIndex], RailB.GetCenter()[CrossAxisIndex])
					- BlockUnitsCM * 0.5;
				const double CrossMaximum = FMath::Max(
					RailA.GetCenter()[CrossAxisIndex], RailB.GetCenter()[CrossAxisIndex])
					+ BlockUnitsCM * 0.5;
				const int32 MinimumStation = QMin(
					Volume.SpanOpeningMinCM + BlockUnitsCM * 0.5);
				const int32 MaximumStation = QMax(
					Volume.SpanOpeningMaxCM - BlockUnitsCM * 0.5);
				TArray<int32> DiaphragmStations;
				if (MaximumStation >= MinimumStation)
				{
					DiaphragmStations.Add(MinimumStation);
					DiaphragmStations.AddUnique((MinimumStation + MaximumStation) / 2);
					DiaphragmStations.AddUnique(MaximumStation);
					DiaphragmStations.Sort();
				}
				for (int32 DiaphragmIndex = 0;
					DiaphragmIndex < DiaphragmStations.Num(); ++DiaphragmIndex)
				{
					const double Longitudinal = DiaphragmStations[DiaphragmIndex]
						* static_cast<double>(BlockUnitsCM);
					const double Z = GroundZ
						+ (DiaphragmCourse + 0.5) * BlockUnitsCM;
					const FVector Start = AxisIndex == 0
						? Position(Longitudinal, CrossMinimum, Z)
						: Position(CrossMinimum, Longitudinal, Z);
					const FVector End = AxisIndex == 0
						? Position(Longitudinal, CrossMaximum, Z)
						: Position(CrossMaximum, Longitudinal, Z);
					const int32 MemberIndex = OutPlan.Members.Num();
					if (!AddPlannedMember(OutPlan, EOwnerKind::SupportedSpan,
						ESkeletonMemberKind::BridgeDiaphragm, Volume.VolumeId,
						INDEX_NONE, Volume.VolumeId, INDEX_NONE, DiaphragmCourse,
						DiaphragmStations[DiaphragmIndex], DiaphragmIndex, 0,
						DiaphragmAxis, EABTSM73BeamAMemberRole::BridgeRail,
						Start, End, OutError))
					{
						return false;
					}
					OutPlan.Members[MemberIndex].EndpointCoreCellIds = {
						NegativeCore->CoreCellId, PositiveCore->CoreCellId};
				}
			}
			FABTSM73BeamASupportVoid& Void =
				OutPlan.ReservedSupportVoids.AddDefaulted_GetRef();
			FVector VoidMinimum = Volume.LocalBounds.Min;
			FVector VoidMaximum = Volume.LocalBounds.Max;
			VoidMinimum.Z = GroundZ;
			VoidMaximum.Z = GroundZ + Span.RailBottomCourse * BlockUnitsCM;
			VoidMinimum[AxisIndex] = Volume.SpanOpeningMinCM;
			VoidMaximum[AxisIndex] = Volume.SpanOpeningMaxCM;
			Void.Bounds = FBox(VoidMinimum, VoidMaximum);
			Void.SpanAxisIndex = AxisIndex;
			Void.SpanSourceVolumeId = Volume.VolumeId;
			++OutPlan.Summary.SupportedSpanCount;
		}
		if (!CompactPlanMembers(
			OutPlan, RemovedMembers, ReplacementMembers, OutError))
		{
			return false;
		}
		OutPlan.Summary.CoreMergeRegionCount = OutPlan.CoreMergeRegions.Num();
		OutPlan.Summary.MergedGroundComponentCount = 0;
		for (const FCoreMergeRegionPlan& Region : OutPlan.CoreMergeRegions)
		{
			if (Region.SourceGroundComponentCount > 1)
			{
				OutPlan.Summary.MergedGroundComponentCount +=
					Region.SourceGroundComponentCount;
			}
		}
		if (Stage == EGenerationStage::CoreAndShared)
		{
			if (!OutPlan.BuildingGroups.IsEmpty())
			{
				OutError = TEXT("BeamC3Stage1UnexpectedBuildingGroup");
				return false;
			}
			if (!RebuildPlannedSeatDAG(OutPlan, OutError))
			{
				return false;
			}
			if (!ValidateCompositeCoreContract(OutPlan, OutError))
			{
				return false;
			}
			if (!ValidateHighProjectionRegionContract(OutPlan, OutError))
			{
				return false;
			}
			if (!FinalizeSupportProvinceGroundCoreBindings(OutPlan, OutError))
			{
				return false;
			}
			if (!BuildLocalPodiumHeightPlanDiagnostics(OutPlan, OutError))
			{
				return false;
			}
			if (!BuildPodiumCoreCoverageDiagnostics(OutPlan, OutError))
			{
				return false;
			}
			if (!BuildHighProjectionDiagnostics(Roots, OutPlan, OutError))
			{
				return false;
			}
			if (!BuildSemanticDemandCoreBindingDiagnostics(OutPlan, OutError))
			{
				return false;
			}
			OutPlan.Summary.ExplicitCoreCellCount = OutPlan.CoreCells.Num();
			OutPlan.Summary.CoreCellCount = OutPlan.CoreCells.Num();
			OutPlan.Summary.PodiumMainCoreCellCount = 0;
			OutPlan.Summary.TowerChildCoreCellCount = 0;
			OutPlan.Summary.GroundedCoreCellCount = 0;
			OutPlan.Summary.SuspendedCoreCount = 0;
			OutPlan.Summary.ShellMemberCount = 0;
			OutPlan.Summary.CoreDerivedShellMemberCount = 0;
			OutPlan.Summary.SharedCourseCount = 0;
			OutPlan.Summary.SharedCourseSegmentCount = 0;
			OutPlan.Summary.SharedCourseCrossCoreSegmentCount = 0;
			OutPlan.Summary.SharedCourseConflictOmissionCount = 0;
			OutPlan.Summary.SharedCourseReplacementSlotCount = 0;
			OutPlan.Summary.SharedCourseNonCoreEndpointViolationCount = 0;
			OutPlan.Summary.SharedCourseBandViolationCount = 0;
			OutPlan.Summary.BuildingGroupCount = 0;
			OutPlan.Summary.CommonShellMemberCount = 0;
			OutPlan.Summary.CommonShellConnectedCoreCount = 0;
			OutPlan.Summary.GroundedComponentCount = 0;
			OutPlan.Summary.SupportPlaneCount = 0;
			TSet<int32> GroundedComponents;
			TMap<int32, int32> SharedCoreMembership;
			for (const FCoreCellPlan& Core : OutPlan.CoreCells)
			{
				OutPlan.Summary.PodiumMainCoreCellCount +=
					Core.HierarchyRole == ECoreHierarchyRole::PodiumMain ? 1 : 0;
				OutPlan.Summary.TowerChildCoreCellCount +=
					Core.HierarchyRole == ECoreHierarchyRole::TowerChild ? 1 : 0;
				bool bHasGroundCourse = false;
				TSet<int32> Courses;
				for (const int32 MemberIndex : Core.MemberIndices)
				{
					if (!OutPlan.Members.IsValidIndex(MemberIndex))
					{
						OutError = FString::Printf(
							TEXT("BeamC3Stage1CoreMemberInvalid:Core=%d:Member=%d"),
							Core.CoreCellId, MemberIndex);
						return false;
					}
					const FPlannedMember& Member = OutPlan.Members[MemberIndex];
					if (Member.SkeletonKind != ESkeletonMemberKind::CoreCourse
						&& Member.SkeletonKind != ESkeletonMemberKind::SharedCourse)
					{
						OutError = FString::Printf(
							TEXT("BeamC3Stage1CoreSlotKindInvalid:Core=%d:Member=%d:Kind=%d"),
							Core.CoreCellId, MemberIndex,
							static_cast<int32>(Member.SkeletonKind));
						return false;
					}
					Courses.Add(Member.CourseIndex);
					bHasGroundCourse |= Member.CourseIndex == 0
						&& Member.bRequiresGroundSeat;
					if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
					{
						SharedCoreMembership.FindOrAdd(MemberIndex)++;
					}
				}
				OutPlan.Summary.SupportPlaneCount += Courses.Num();
				if (bHasGroundCourse)
				{
					++OutPlan.Summary.GroundedCoreCellCount;
					GroundedComponents.Add(Core.ComponentId);
				}
				else
				{
					++OutPlan.Summary.SuspendedCoreCount;
				}
			}
			OutPlan.Summary.GroundedComponentCount = GroundedComponents.Num();
			if (OutPlan.Summary.GroundedCoreCellCount != OutPlan.CoreCells.Num()
				|| OutPlan.Summary.GroundedComponentCount != OutPlan.Components.Num())
			{
				OutError = FString::Printf(
					TEXT("BeamC3Stage1GroundedCoreMismatch:Cores=%d/%d:Components=%d/%d"),
					OutPlan.Summary.GroundedCoreCellCount, OutPlan.CoreCells.Num(),
					OutPlan.Summary.GroundedComponentCount, OutPlan.Components.Num());
				return false;
			}

			TMap<int32, int32> SharedBySpan;
			TMap<int32, int32> DiaphragmsBySpan;
			for (int32 MemberIndex = 0; MemberIndex < OutPlan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Member = OutPlan.Members[MemberIndex];
				if (Member.ProducedStage != EABTSM73BeamC3GenerationStage::CoreAndShared
					|| (Member.SkeletonKind != ESkeletonMemberKind::CoreCourse
						&& Member.SkeletonKind != ESkeletonMemberKind::SharedCourse
						&& Member.SkeletonKind != ESkeletonMemberKind::BridgeDiaphragm))
				{
					OutError = FString::Printf(
						TEXT("BeamC3Stage1LaterStageMemberEmitted:Member=%d:Stage=%d:Kind=%d"),
						MemberIndex, static_cast<int32>(Member.ProducedStage),
						static_cast<int32>(Member.SkeletonKind));
					return false;
				}
				if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
				{
					++OutPlan.Summary.SharedCourseSegmentCount;
					if (Member.bSharedCrossCoreSegment)
					{
						++OutPlan.Summary.SharedCourseCount;
						++OutPlan.Summary.SharedCourseCrossCoreSegmentCount;
						SharedBySpan.FindOrAdd(Member.OwnerId)++;
					}
					const int32 Membership = SharedCoreMembership.FindRef(MemberIndex);
					OutPlan.Summary.SharedCourseReplacementSlotCount += Membership;
					const FSharedCourseLanePlan* Lane = FindSharedLanePlan(OutPlan, Member);
					const int32 ExpectedMembership = Member.bSharedCrossCoreSegment
						&& Lane != nullptr
						? 1 + (Lane->bReceiverSlotReplaced ? 1 : 0) : 0;
					if (Member.EndpointCoreCellIds.Num() != 2
						|| Lane == nullptr || Membership != ExpectedMembership)
					{
						++OutPlan.Summary.SharedCourseNonCoreEndpointViolationCount;
					}
				}
				else if (Member.SkeletonKind == ESkeletonMemberKind::BridgeDiaphragm)
				{
					DiaphragmsBySpan.FindOrAdd(Member.OwnerId)++;
				}
				if (!Member.bRequiresGroundSeat
					&& Member.RequiredLowerMemberIndices.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("BeamC3Stage1UnsupportedMember:Member=%d:Kind=%d"),
						MemberIndex, static_cast<int32>(Member.SkeletonKind));
					return false;
				}
			}
			if (OutPlan.SharedCourseIntents.Num() != Spans.Num())
			{
				OutError = FString::Printf(
					TEXT("BeamC3Stage1SharedIntentMismatch:Intents=%d:Spans=%d"),
					OutPlan.SharedCourseIntents.Num(), Spans.Num());
				return false;
			}
			for (const FSharedCourseIntent& Intent : OutPlan.SharedCourseIntents)
			{
				const int32 SharedCount = SharedBySpan.FindRef(Intent.SpanVolumeId);
				const int32 DiaphragmCount = DiaphragmsBySpan.FindRef(Intent.SpanVolumeId);
				int32 ExpectedRailsPerCourse = INDEX_NONE;
				int32 ExpectedReplacementSlots = 0;
				for (const FSharedCourseLanePlan& Lane : Intent.LanePlans)
				{
					if (OutPlan.CoreCells.IsValidIndex(Lane.DonorCoreCellId))
					{
						const int32 DonorRails =
							OutPlan.CoreCells[Lane.DonorCoreCellId].RailCount;
						ExpectedRailsPerCourse = ExpectedRailsPerCourse == INDEX_NONE
							? DonorRails : ExpectedRailsPerCourse;
						if (ExpectedRailsPerCourse != DonorRails)
						{
							++OutPlan.Summary.SharedCourseBandViolationCount;
						}
					}
					ExpectedReplacementSlots +=
						1 + (Lane.bReceiverSlotReplaced ? 1 : 0);
					OutPlan.Summary.SharedCourseConflictOmissionCount +=
						Lane.bReceiverSlotReplaced ? 1 : 0;
				}
				if (ExpectedRailsPerCourse < 2
					|| !OutPlan.CoreCells.IsValidIndex(Intent.PositiveCoreCellId)
					|| SharedCount != Intent.CourseIndices.Num() * ExpectedRailsPerCourse
					|| Intent.LanePlans.Num() != SharedCount
					|| ExpectedReplacementSlots <= 0
					|| DiaphragmCount < 1)
				{
					++OutPlan.Summary.SharedCourseBandViolationCount;
				}
			}
			if (OutPlan.Summary.SharedCourseNonCoreEndpointViolationCount != 0
				|| OutPlan.Summary.SharedCourseBandViolationCount != 0)
			{
				OutError = FString::Printf(
					TEXT("BeamC3Stage1SharedContractInvalid:Endpoint=%d:Band=%d"),
					OutPlan.Summary.SharedCourseNonCoreEndpointViolationCount,
					OutPlan.Summary.SharedCourseBandViolationCount);
				return false;
			}

			TArray<bool> GroundedMembers;
			if (!ResolveGroundedMemberMask(OutPlan, GroundedMembers, OutError))
			{
				return false;
			}
			for (int32 MemberIndex = 0; MemberIndex < GroundedMembers.Num(); ++MemberIndex)
			{
				if (!GroundedMembers[MemberIndex])
				{
					OutError = FString::Printf(
						TEXT("BeamC3Stage1MemberNotGroundReachable:Member=%d"), MemberIndex);
					return false;
				}
			}

			OutPlan.Summary.GroundSeatCount = 0;
			OutPlan.Summary.PlannedSeatCount = 0;
			OutPlan.Summary.TotalMemberLengthCM = 0.0;
			for (const FPlannedMember& Member : OutPlan.Members)
			{
				OutPlan.Summary.GroundSeatCount += Member.bRequiresGroundSeat ? 1 : 0;
				OutPlan.Summary.PlannedSeatCount += Member.RequiredLowerMemberIndices.Num();
				OutPlan.Summary.TotalMemberLengthCM +=
					FVector::Distance(Member.LocalStart, Member.LocalEnd);
			}
			OutPlan.Summary.PlannedMemberCount = OutPlan.Members.Num();
			OutPlan.Summary.BudgetMargin =
				OutPlan.Summary.MaximumBrickCount - OutPlan.Members.Num();
			OutPlan.Summary.VisibleFeatureCount =
				OutPlan.CoreCells.Num() + OutPlan.SharedCourseIntents.Num();
			OutPlan.Summary.GroundedFaceMask = 0;
			OutPlan.Summary.MinimumGroundedExteriorPostStationsPerFace = 0;

			if (!ValidateEnvelopeAndVoids(Silhouette, Roots, OutPlan, OutError)
				|| !ValidateNoPenetration(OutPlan, OutError)
				|| !ValidatePreflightCaps(
					Profile.BeamSettings.BeamB.BeamA, OutPlan, OutError))
			{
				return false;
			}

			FString MergeCanonical;
			for (const FCoreMergeRegionPlan& Region : OutPlan.CoreMergeRegions)
			{
				MergeCanonical += FString::Printf(
					TEXT("|R:%d:C=%d:N=%d:Rail=%d:B=%lld,%lld,%lld,%lld,%lld,%lld"),
					Region.RegionId, Region.ComponentId,
					Region.SourceGroundComponentCount, Region.SelectedRailCount,
					QHash(Region.LocalBounds.Min.X), QHash(Region.LocalBounds.Min.Y),
					QHash(Region.LocalBounds.Min.Z), QHash(Region.LocalBounds.Max.X),
					QHash(Region.LocalBounds.Max.Y), QHash(Region.LocalBounds.Max.Z));
				for (const int32 Source : Region.SourceVolumeIds)
				{
					MergeCanonical += FString::Printf(TEXT(":V%d"), Source);
				}
			}
			OutPlan.Summary.CoreMergeRegionHash = HashText(FString::Printf(
				TEXT("Envelope=%lld"), ComputeEnvelopeHash(Silhouette))
				+ MergeCanonical);
			FString CoreCanonical = FString::Printf(
				TEXT("Stage1:Recipe=%d:H=%d:V=%d:Merge=%lld"),
				Density.RecipeId, Density.HorizontalUnits, Density.VerticalUnits,
				OutPlan.Summary.CoreMergeRegionHash);
			FString SupportCanonical;
			for (const FHighProjectionRegionPlan& Region
				: OutPlan.HighProjectionRegions)
			{
				CoreCanonical += FString::Printf(
					TEXT("|HIGH:%d:C=%d:P=%d:T=%d:TS=%d,%d:MAIN=%d:CORE=%d:B=%lld,%lld,%lld,%lld,%lld,%lld:E=%lld,%lld,%lld,%lld:Z=%lld,%lld,%lld,%lld"),
					Region.RegionId, Region.ComponentId, Region.PodiumTopCourse,
					Region.RequiredTopCourse, Region.TerminalSliceCourse,
					Region.TerminalSliceComponentId,
					Region.BoundPodiumMainCoreCellId, Region.BoundCoreCellId,
					QHash(Region.LocalBounds.Min.X), QHash(Region.LocalBounds.Min.Y),
					QHash(Region.LocalBounds.Min.Z), QHash(Region.LocalBounds.Max.X),
					QHash(Region.LocalBounds.Max.Y), QHash(Region.LocalBounds.Max.Z),
					QHash(Region.EntryBounds.Min.X), QHash(Region.EntryBounds.Min.Y),
					QHash(Region.EntryBounds.Max.X), QHash(Region.EntryBounds.Max.Y),
					QHash(Region.TerminalBounds.Min.X), QHash(Region.TerminalBounds.Min.Y),
					QHash(Region.TerminalBounds.Max.X), QHash(Region.TerminalBounds.Max.Y));
				for (const int32 Source : Region.SourceVolumeIds)
				{
					CoreCanonical += FString::Printf(TEXT(":V%d"), Source);
				}
			}
			for (const FCoreCellPlan& Core : OutPlan.CoreCells)
			{
				CoreCanonical += FString::Printf(
					TEXT("|CORE:%d:C=%d:R=%d:M=%d:S=%d:H=%d:HP=%d:P=%d:T=%d:BT=%d:G=%d:XB=%d:B=%lld,%lld,%lld,%lld,%lld,%lld"),
					Core.CoreCellId, Core.ComponentId, Core.RailCount,
					Core.CoreMergeRegionId, Core.BodySourceVolumeId,
					static_cast<int32>(Core.HierarchyRole),
					Core.HighProjectionRegionId, Core.PodiumMainCoreCellId,
					Core.TopCourseIndex,
					Core.BodyTopCourseIndex, Core.CompositeCoreGroupId,
					Core.CrossCoreBearingContactCount,
					QHash(Core.LocalBounds.Min.X), QHash(Core.LocalBounds.Min.Y),
					QHash(Core.LocalBounds.Min.Z), QHash(Core.LocalBounds.Max.X),
					QHash(Core.LocalBounds.Max.Y), QHash(Core.LocalBounds.Max.Z));
				for (const int32 Station : Core.XStations)
				{
					CoreCanonical += FString::Printf(TEXT(":X%d"), Station);
				}
				for (const int32 Station : Core.YStations)
				{
					CoreCanonical += FString::Printf(TEXT(":Y%d"), Station);
				}
			}
			for (const FSharedCourseIntent& Intent : OutPlan.SharedCourseIntents)
			{
				CoreCanonical += FString::Printf(
					TEXT("|PAIR:%d:%d>%d:A=%d:O=%lld..%lld"),
					Intent.SpanVolumeId, Intent.NegativeCoreCellId,
					Intent.PositiveCoreCellId, static_cast<int32>(Intent.Axis),
					QHash(Intent.OpeningMinimumCM), QHash(Intent.OpeningMaximumCM));
				for (const int32 Course : Intent.CourseIndices)
				{
					CoreCanonical += FString::Printf(TEXT(":Q%d"), Course);
				}
				for (const FSharedCourseLanePlan& Lane : Intent.LanePlans)
				{
					CoreCanonical += FString::Printf(
						TEXT(":L%d@Q%d:D%d>R%d:X=%lld:A=%lld..%lld:C=%d:O=%d"),
						Lane.LaneIndex, Lane.CourseIndex, Lane.DonorCoreCellId,
						Lane.ReceiverCoreCellId, QHash(Lane.CrossStationCM),
						QHash(Lane.RequiredMinimumCM), QHash(Lane.RequiredMaximumCM),
						Lane.CrossCoreSegmentMemberIndex,
						Lane.bReceiverSlotReplaced ? 1 : 0);
					for (const int32 Segment : Lane.SegmentMemberIndices)
					{
						CoreCanonical += FString::Printf(TEXT(":M%d"), Segment);
					}
				}
			}
			for (int32 MemberIndex = 0; MemberIndex < OutPlan.Members.Num(); ++MemberIndex)
			{
				AppendMemberCanonical(CoreCanonical, OutPlan.Members[MemberIndex]);
				const FPlannedMember& Member = OutPlan.Members[MemberIndex];
				for (const int32 Lower : Member.RequiredLowerMemberIndices)
				{
					SupportCanonical += FString::Printf(
						TEXT("|S:%d>%d"), Lower, MemberIndex);
				}
			}
			for (const FABTSM73BeamASupportVoid& Void : OutPlan.ReservedSupportVoids)
			{
				SupportCanonical += FString::Printf(
					TEXT("|V:%d:%d:%lld:%lld:%lld:%lld:%lld:%lld"),
					Void.SpanSourceVolumeId, Void.SpanAxisIndex,
					QHash(Void.Bounds.Min.X), QHash(Void.Bounds.Min.Y),
					QHash(Void.Bounds.Min.Z), QHash(Void.Bounds.Max.X),
					QHash(Void.Bounds.Max.Y), QHash(Void.Bounds.Max.Z));
			}
			OutPlan.Summary.CorePlanHash = HashText(CoreCanonical);
			OutPlan.Summary.SupportPlanHash = HashText(
				CoreCanonical + SupportCanonical);
			OutPlan.Summary.FinalGeometryHash = HashText(
				CoreCanonical + SupportCanonical + TEXT("|STOP=Stage1"));
			OutPlan.Summary.bAccepted = true;
			return true;
		}

		// Legacy appended-rail implementation retained as unreachable reference
		// until the new common-frame path has completed visual acceptance.
		if (false)
		{
		for (const FSpanInput& Span : Spans)
		{
			const FABTSM73DAG5BV2Volume& Volume = *Span.Volume;
			const FABTSM73DAG5BV2Volume* NegativeSupport =
				FindVolume(Silhouette, Volume.NegativeSupportVolumeId);
			const FABTSM73DAG5BV2Volume* PositiveSupport =
				FindVolume(Silhouette, Volume.PositiveSupportVolumeId);
			if (NegativeSupport == nullptr || PositiveSupport == nullptr)
			{
				OutError = FString::Printf(TEXT("BeamC3V3SupportedSpanEndpointVolumeMissing:Volume=%d"),
					Volume.VolumeId);
				return false;
			}
			const FComponentPlan& Negative = OutPlan.Components[Span.NegativeComponentId];
			const FComponentPlan& Positive = OutPlan.Components[Span.PositiveComponentId];
			const int32 TransverseAxis = Span.SpanAxis == 0 ? 1 : 0;
			const EABTSM73BeamAFrameAxis RailAxis = Span.SpanAxis == 0
				? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
			const EABTSM73BeamAFrameAxis SeatAxis = Span.SpanAxis == 0
				? EABTSM73BeamAFrameAxis::Y : EABTSM73BeamAFrameAxis::X;
			const double GroundZ = Negative.GroundPlaneZCM;
			const double RailBottomZ = GroundZ + Span.RailBottomCourse * BlockUnitsCM;
			const double RailCenterZ = RailBottomZ + BlockUnitsCM * 0.5;
			struct FSpanSeatCandidate
			{
				int32 MemberIndex = INDEX_NONE;
				FBox Bounds = FBox(EForceInit::ForceInit);
				double LongitudinalCM = 0.0;
			};
			TArray<FSpanSeatCandidate> NegativeSeats;
			TArray<FSpanSeatCandidate> PositiveSeats;
			int32 NegativeAtHeight = 0;
			int32 PositiveAtHeight = 0;
			int32 NegativeOtherAxisAtHeight = 0;
			int32 PositiveOtherAxisAtHeight = 0;
			int32 NegativeSupportRejected = 0;
			int32 PositiveSupportRejected = 0;
			int32 NegativeClearanceRejected = 0;
			int32 PositiveClearanceRejected = 0;
			FString SeatDiagnostics;
			for (int32 MemberIndex = 0; MemberIndex < OutPlan.Members.Num(); ++MemberIndex)
			{
				const FPlannedMember& Candidate = OutPlan.Members[MemberIndex];
				if (Candidate.ComponentId != Span.NegativeComponentId
					&& Candidate.ComponentId != Span.PositiveComponentId)
				{
					continue;
				}
				if (Candidate.SkeletonKind != ESkeletonMemberKind::CoreCourse)
				{
					continue;
				}
				const FBox CandidateBounds = PlannedMemberBounds(Candidate);
				if (FMath::Abs(CandidateBounds.Max.Z - RailBottomZ)
					> GeometryToleranceCM)
				{
					continue;
				}
				const bool bNegative = Candidate.ComponentId == Span.NegativeComponentId;
				if (Candidate.Axis != SeatAxis)
				{
					(bNegative ? NegativeOtherAxisAtHeight : PositiveOtherAxisAtHeight)++;
					continue;
				}
				(bNegative ? NegativeAtHeight : PositiveAtHeight)++;
				const FABTSM73DAG5BV2Volume& Support = bNegative
					? *NegativeSupport : *PositiveSupport;
				const FVector Center = CandidateBounds.GetCenter();
				const bool bInsideSupport = !(Center.X < Support.LocalBounds.Min.X - BlockUnitsCM * 0.5
					|| Center.X > Support.LocalBounds.Max.X + BlockUnitsCM * 0.5
					|| Center.Y < Support.LocalBounds.Min.Y - BlockUnitsCM * 0.5
					|| Center.Y > Support.LocalBounds.Max.Y + BlockUnitsCM * 0.5);
				const bool bClearsOpening = !((bNegative
						&& CandidateBounds.Max[Span.SpanAxis]
							> Volume.SpanOpeningMinCM + GeometryToleranceCM)
					|| (!bNegative
						&& CandidateBounds.Min[Span.SpanAxis]
							< Volume.SpanOpeningMaxCM - GeometryToleranceCM));
				if (!bInsideSupport || !bClearsOpening)
				{
					if (!bInsideSupport)
					{
						(bNegative ? NegativeSupportRejected : PositiveSupportRejected)++;
					}
					if (!bClearsOpening)
					{
						(bNegative ? NegativeClearanceRejected : PositiveClearanceRejected)++;
					}
					if (SeatDiagnostics.Len() < 1200)
					{
						SeatDiagnostics += FString::Printf(
							TEXT("|M%d:C%d:S%d:Inside=%d:Clear=%d:B=%.1f,%.1f..%.1f,%.1f"),
							MemberIndex, Candidate.ComponentId, Candidate.SourceVolumeId,
							bInsideSupport ? 1 : 0, bClearsOpening ? 1 : 0,
							CandidateBounds.Min[Span.SpanAxis], CandidateBounds.Min[TransverseAxis],
							CandidateBounds.Max[Span.SpanAxis], CandidateBounds.Max[TransverseAxis]);
					}
					continue;
				}
				FSpanSeatCandidate Seat;
				Seat.MemberIndex = MemberIndex;
				Seat.Bounds = CandidateBounds;
				Seat.LongitudinalCM = Center[Span.SpanAxis];
				(bNegative ? NegativeSeats : PositiveSeats).Add(Seat);
			}

			int32 NegativeLower = INDEX_NONE;
			int32 PositiveLower = INDEX_NONE;
			double NegativeLongCM = 0.0;
			double PositiveLongCM = 0.0;
			TArray<int32> RailStations;
			double BestRailLengthCM = DBL_MAX;
			FString RailStationDiagnostics;
			for (const FSpanSeatCandidate& NegativeCandidate : NegativeSeats)
			{
				for (const FSpanSeatCandidate& PositiveCandidate : PositiveSeats)
				{
					const double CandidateLength = PositiveCandidate.LongitudinalCM
						- NegativeCandidate.LongitudinalCM;
					if (CandidateLength + KINDA_SMALL_NUMBER < BlockUnitsCM
						|| CandidateLength > 720.0 + KINDA_SMALL_NUMBER)
					{
						continue;
					}
					const double MinimumTransverse = FMath::Max3(
						NegativeCandidate.Bounds.Min[TransverseAxis],
						PositiveCandidate.Bounds.Min[TransverseAxis],
						Volume.LocalBounds.Min[TransverseAxis] + BlockUnitsCM * 0.5);
					const double MaximumTransverse = FMath::Min3(
						NegativeCandidate.Bounds.Max[TransverseAxis],
						PositiveCandidate.Bounds.Max[TransverseAxis],
						Volume.LocalBounds.Max[TransverseAxis] - BlockUnitsCM * 0.5);
					const TArray<int32>& NegativeTransverseGrid = TransverseAxis == 0
						? Negative.XGridUnits : Negative.YGridUnits;
					const TArray<int32>& PositiveTransverseGrid = TransverseAxis == 0
						? Positive.XGridUnits : Positive.YGridUnits;
					TArray<int32> CandidateRailStations;
					// A shared rail bears on the transverse endpoint courses, but must
					// not occupy a lattice node where either core projects a Z post.
					// Select two deterministic in-course stations that are absent from
					// both endpoint grids instead of reusing the seat bounds themselves.
					if (!SelectSharedRailStations(
						NegativeTransverseGrid, PositiveTransverseGrid,
						MinimumTransverse, MaximumTransverse, CandidateRailStations))
					{
						if (RailStationDiagnostics.Len() < 1200)
						{
							FString NegativeGridText;
							FString PositiveGridText;
							for (const int32 Station : NegativeTransverseGrid)
							{
								NegativeGridText += FString::Printf(TEXT("%s%d"),
									NegativeGridText.IsEmpty() ? TEXT("") : TEXT(","), Station);
							}
							for (const int32 Station : PositiveTransverseGrid)
							{
								PositiveGridText += FString::Printf(TEXT("%s%d"),
									PositiveGridText.IsEmpty() ? TEXT("") : TEXT(","), Station);
							}
							RailStationDiagnostics += FString::Printf(
								TEXT("|N%d:P%d:T=%.1f..%.1f:NG=%s:PG=%s"),
								NegativeCandidate.MemberIndex, PositiveCandidate.MemberIndex,
								MinimumTransverse, MaximumTransverse,
								*NegativeGridText, *PositiveGridText);
						}
						continue;
					}
					if (CandidateLength < BestRailLengthCM - UE_DOUBLE_SMALL_NUMBER
						|| (FMath::IsNearlyEqual(CandidateLength, BestRailLengthCM)
							&& (NegativeLower == INDEX_NONE
								|| NegativeCandidate.MemberIndex < NegativeLower)))
					{
						NegativeLower = NegativeCandidate.MemberIndex;
						PositiveLower = PositiveCandidate.MemberIndex;
						NegativeLongCM = NegativeCandidate.LongitudinalCM;
						PositiveLongCM = PositiveCandidate.LongitudinalCM;
						RailStations = MoveTemp(CandidateRailStations);
						BestRailLengthCM = CandidateLength;
					}
				}
			}
			if (!OutPlan.Members.IsValidIndex(NegativeLower)
				|| !OutPlan.Members.IsValidIndex(PositiveLower)
				|| RailStations.Num() != 2)
			{
				const FRoot& NegativeRoot = Roots[Span.NegativeComponentId];
				const FRoot& PositiveRoot = Roots[Span.PositiveComponentId];
				OutError = FString::Printf(
					TEXT("BeamC3V3SupportedSpanEndpointSeatUnavailable:Volume=%d:NegativeCandidates=%d:PositiveCandidates=%d:AtHeight=%d,%d:OtherAxisAtHeight=%d,%d:SupportRejected=%d,%d:ClearanceRejected=%d,%d:RailBottom=%.1f:Support=%d,R%d,Z%.1f..%.1f|%d,R%d,Z%.1f..%.1f:Roots=Body%.1f,Crown%.1f,Bands%d|Body%.1f,Crown%.1f,Bands%d:SupportBounds=%.1f,%.1f..%.1f,%.1f|%.1f,%.1f..%.1f,%.1f:Seats=%s:RailSearch=%s"),
					Volume.VolumeId, NegativeSeats.Num(), PositiveSeats.Num(),
					NegativeAtHeight, PositiveAtHeight,
					NegativeOtherAxisAtHeight, PositiveOtherAxisAtHeight,
					NegativeSupportRejected, PositiveSupportRejected,
					NegativeClearanceRejected, PositiveClearanceRejected,
					RailBottomZ,
					NegativeSupport->VolumeId, static_cast<int32>(NegativeSupport->Role),
					NegativeSupport->LocalBounds.Min.Z, NegativeSupport->LocalBounds.Max.Z,
					PositiveSupport->VolumeId, static_cast<int32>(PositiveSupport->Role),
					PositiveSupport->LocalBounds.Min.Z, PositiveSupport->LocalBounds.Max.Z,
					NegativeRoot.BodyTopCM, NegativeRoot.CrownTopCM,
					Negative.BandBaseCourseIndices.Num(),
					PositiveRoot.BodyTopCM, PositiveRoot.CrownTopCM,
					Positive.BandBaseCourseIndices.Num(),
					NegativeSupport->LocalBounds.Min[Span.SpanAxis],
					NegativeSupport->LocalBounds.Min[TransverseAxis],
					NegativeSupport->LocalBounds.Max[Span.SpanAxis],
					NegativeSupport->LocalBounds.Max[TransverseAxis],
					PositiveSupport->LocalBounds.Min[Span.SpanAxis],
					PositiveSupport->LocalBounds.Min[TransverseAxis],
					PositiveSupport->LocalBounds.Max[Span.SpanAxis],
					PositiveSupport->LocalBounds.Max[TransverseAxis],
					*SeatDiagnostics, *RailStationDiagnostics);
				return false;
			}
			for (int32 RailIndex = 0; RailIndex < RailStations.Num(); ++RailIndex)
			{
				const double TransverseCM = RailStations[RailIndex] * BlockUnitsCM;
				const FVector Start = Span.SpanAxis == 0
					? Position(NegativeLongCM, TransverseCM, RailCenterZ)
					: Position(TransverseCM, NegativeLongCM, RailCenterZ);
				const FVector End = Span.SpanAxis == 0
					? Position(PositiveLongCM, TransverseCM, RailCenterZ)
					: Position(TransverseCM, PositiveLongCM, RailCenterZ);
				if (!RailSolidCoveredBySemanticUnion(Start, End, RailAxis,
					Volume, *NegativeSupport, *PositiveSupport))
				{
					OutError = FString::Printf(
						TEXT("BeamC3V3SupportedSpanRailEnvelopeViolation:Volume=%d:Rail=%d"),
						Volume.VolumeId, RailIndex);
					return false;
				}
				const int32 MemberIndex = OutPlan.Members.Num();
				if (!AddPlannedMember(OutPlan, EOwnerKind::SupportedSpan,
					ESkeletonMemberKind::SharedCourse, Volume.VolumeId, INDEX_NONE,
					Volume.VolumeId, INDEX_NONE, Span.RailBottomCourse,
					RailIndex, INDEX_NONE, 0, RailAxis,
					EABTSM73BeamAMemberRole::BridgeRail, Start, End, OutError))
				{
					return false;
				}
				AddSeat(OutPlan, MemberIndex, NegativeLower);
				AddSeat(OutPlan, MemberIndex, PositiveLower);
				OutPlan.Members[MemberIndex].RequiredInwardMemberIndices = {
					NegativeLower, PositiveLower};
				OutPlan.Members[MemberIndex].EndpointCoreCellIds = {
					OutPlan.Members[NegativeLower].OriginCoreCellId,
					OutPlan.Members[PositiveLower].OriginCoreCellId};
			}
			FABTSM73BeamASupportVoid& Void = OutPlan.ReservedSupportVoids.AddDefaulted_GetRef();
			FVector VoidMinimum = Volume.LocalBounds.Min;
			FVector VoidMaximum = Volume.LocalBounds.Max;
			VoidMinimum.Z = GroundZ;
			VoidMaximum.Z = RailBottomZ;
			VoidMinimum[Span.SpanAxis] = Volume.SpanOpeningMinCM;
			VoidMaximum[Span.SpanAxis] = Volume.SpanOpeningMaxCM;
			Void.Bounds = FBox(VoidMinimum, VoidMaximum);
			Void.SpanAxisIndex = Span.SpanAxis;
			Void.SpanSourceVolumeId = Volume.VolumeId;
			++OutPlan.Summary.SupportedSpanCount;
		}
		}
		if (!BuildCandidateCommonFrame(Roots, Density, OutPlan, OutError)
			|| !CanonicalizeCommonHorizontalRows(OutPlan, OutError)
			|| !CollapseUnsupportedCommonSpansIntoCores(OutPlan, OutError)
			|| !CompleteCommonFrameSupportHulls(
				Profile.BeamSettings, OutPlan, OutError)
			|| !RebuildPlannedSeatDAG(OutPlan, OutError)
			|| !RebuildCoreLineage(OutPlan, OutError))
		{
			return false;
		}

		if (!ValidateSkeletonTopology(Silhouette, Roots, OutPlan, OutError))
		{
			return false;
		}

		// Reconstruct grounded shell reachability from the explicit seat DAG.
		// Face bits are never granted from a constant or an unrooted member.
		TArray<bool> GroundedMembers;
		if (!ResolveGroundedMemberMask(OutPlan, GroundedMembers, OutError))
		{
			return false;
		}
		for (int32 MemberIndex = 0; MemberIndex < OutPlan.Members.Num(); ++MemberIndex)
		{
			const FPlannedMember& Member = OutPlan.Members[MemberIndex];
			if (GroundedMembers[MemberIndex]
				&& OutPlan.Components.IsValidIndex(Member.ComponentId))
			{
				OutPlan.Components[Member.ComponentId].GroundedFaceMask |= Member.FaceMask;
			}
		}
		if (!ValidateGroundedExteriorPostStations(OutPlan, GroundedMembers, OutError))
		{
			return false;
		}

		OutPlan.Summary.GroundedComponentCount = 0;
		for (const FComponentPlan& Component : OutPlan.Components)
		{
			if (Component.PlannedMemberCount > 0)
			{
				++OutPlan.Summary.GroundedComponentCount;
			}
			OutPlan.Summary.ComponentGroundedFaceMasks.Add(Component.GroundedFaceMask);
		}
		OutPlan.Summary.GroundedFaceMask = ABTSM73BeamC3V3::AllFaces;
		for (const FBuildingGroupPlan& Group : OutPlan.BuildingGroups)
		{
			OutPlan.Summary.GroundedFaceMask &= Group.GroundedFaceMask;
		}
		for (const FPlannedMember& Member : OutPlan.Members)
		{
			OutPlan.Summary.GroundSeatCount += Member.bRequiresGroundSeat ? 1 : 0;
			OutPlan.Summary.PlannedSeatCount += Member.RequiredLowerMemberIndices.Num();
			if (!Member.bRequiresGroundSeat && Member.RequiredLowerMemberIndices.IsEmpty())
			{
				OutError = TEXT("BeamC3V3UnsupportedPlannedMember");
				return false;
			}
		}
		OutPlan.Summary.PlannedMemberCount = OutPlan.Members.Num();
		OutPlan.Summary.BudgetMargin = OutPlan.Summary.MaximumBrickCount - OutPlan.Members.Num();
		OutPlan.Summary.VisibleFeatureCount = 0;
		for (const uint8 Face : {ABTSM73BeamC3V3::NegativeX,
			ABTSM73BeamC3V3::PositiveX, ABTSM73BeamC3V3::NegativeY,
			ABTSM73BeamC3V3::PositiveY})
		{
			OutPlan.Summary.VisibleFeatureCount +=
				(OutPlan.Summary.GroundedFaceMask & Face) != 0 ? 1 : 0;
		}
		TSet<int32> VisibleComponents;
		TSet<int32> VisibleCrowns;
		TSet<int32> VisibleSpans;
		for (const FPlannedMember& Member : OutPlan.Members)
		{
			if (OutPlan.Components.IsValidIndex(Member.ComponentId))
			{
				VisibleComponents.Add(Member.ComponentId);
			}
			if (Member.OwnerKind == EOwnerKind::Roof)
			{
				VisibleCrowns.Add(Member.SourceVolumeId);
			}
			if (Member.OwnerKind == EOwnerKind::SupportedSpan)
			{
				VisibleSpans.Add(Member.OwnerId);
			}
		}
		OutPlan.Summary.VisibleFeatureCount += VisibleComponents.Num()
			+ VisibleCrowns.Num() + VisibleSpans.Num()
			+ OutPlan.BuildingGroups.Num();

		for (FComponentPlan& Component : OutPlan.Components)
		{
			FString ComponentCanonical = FString::Printf(
				TEXT("C:%d:P=%s:G=%lld:F=%d:N=%d:O=%lld,%lld,%lld,%lld,%lld,%lld:D=%lld,%lld,%lld,%lld,%lld,%lld"),
				Component.ComponentId,
				*Component.SemanticRootPath, QHash(Component.GroundPlaneZCM),
				Component.GroundedFaceMask, Component.PlannedMemberCount,
				QHash(Component.OccupiedBounds.Min.X), QHash(Component.OccupiedBounds.Min.Y),
				QHash(Component.OccupiedBounds.Min.Z), QHash(Component.OccupiedBounds.Max.X),
				QHash(Component.OccupiedBounds.Max.Y), QHash(Component.OccupiedBounds.Max.Z),
				QHash(Component.BodyBounds.Min.X), QHash(Component.BodyBounds.Min.Y),
				QHash(Component.BodyBounds.Min.Z), QHash(Component.BodyBounds.Max.X),
				QHash(Component.BodyBounds.Max.Y), QHash(Component.BodyBounds.Max.Z));
			for (const int32 Count : Component.GroundedExteriorPostStationCounts)
			{
				ComponentCanonical += FString::Printf(TEXT(":FP%d"), Count);
			}
			for (const int32 Source : Component.SourceVolumeIds)
			{
				ComponentCanonical += FString::Printf(TEXT(":V%d"), Source);
			}
			for (const int32 Source : Component.CrownVolumeIds)
			{
				ComponentCanonical += FString::Printf(TEXT(":C%d"), Source);
			}
			for (const int32 Source : Component.GroundSourceVolumeIds)
			{
				ComponentCanonical += FString::Printf(TEXT(":G%d"), Source);
			}
			for (const FVerticalSupportWitness& Witness : Component.VerticalSupportWitnesses)
			{
				ComponentCanonical += FString::Printf(
					TEXT(":W%d>%d@%lld[%lld,%lld,%lld,%lld]"),
					Witness.LowerSourceVolumeId, Witness.UpperSourceVolumeId,
					QHash(Witness.ContactZCM), QHash(Witness.PositiveXYOverlap.Min.X),
					QHash(Witness.PositiveXYOverlap.Min.Y),
					QHash(Witness.PositiveXYOverlap.Max.X),
					QHash(Witness.PositiveXYOverlap.Max.Y));
			}
			for (const int32 Station : Component.XGridUnits)
			{
				ComponentCanonical += FString::Printf(TEXT(":X%d"), Station);
			}
			for (const int32 Station : Component.YGridUnits)
			{
				ComponentCanonical += FString::Printf(TEXT(":Y%d"), Station);
			}
			for (const int32 Base : Component.BandBaseCourseIndices)
			{
				ComponentCanonical += FString::Printf(TEXT(":B%d"), Base);
			}
			TArray<FString> MemberTokens;
			for (const FPlannedMember& Member : OutPlan.Members)
			{
				if (Member.ComponentId != Component.ComponentId)
				{
					continue;
				}
				FPlannedMember GeometryOnly = Member;
				GeometryOnly.RequiredLowerMemberIndices.Reset();
				FString Token;
				AppendMemberCanonical(Token, GeometryOnly);
				MemberTokens.Add(MoveTemp(Token));
			}
			MemberTokens.Sort();
			for (const FString& Token : MemberTokens)
			{
				ComponentCanonical += Token;
			}
			Component.ComponentCrc32 = FCrc::StrCrc32(*ComponentCanonical);
		}
		for (FBuildingGroupPlan& Group : OutPlan.BuildingGroups)
		{
			FString GroupCanonical = FString::Printf(
				TEXT("BG:%d:P=%s:G=%lld:F=%d:B=%lld,%lld,%lld,%lld,%lld,%lld"),
				Group.GroupId, *Group.BuildingPath, QHash(Group.GroundPlaneZCM),
				Group.GroundedFaceMask,
				QHash(Group.LocalBounds.Min.X), QHash(Group.LocalBounds.Min.Y),
				QHash(Group.LocalBounds.Min.Z), QHash(Group.LocalBounds.Max.X),
				QHash(Group.LocalBounds.Max.Y), QHash(Group.LocalBounds.Max.Z));
			for (const int32 ComponentId : Group.ComponentIds)
			{
				GroupCanonical += FString::Printf(TEXT(":C%d"), ComponentId);
			}
			for (const int32 CoreCellId : Group.CoreCellIds)
			{
				GroupCanonical += FString::Printf(TEXT(":K%d"), CoreCellId);
			}
			for (const int32 VolumeId : Group.SpanVolumeIds)
			{
				GroupCanonical += FString::Printf(TEXT(":S%d"), VolumeId);
			}
			for (const int32 Base : Group.CommonBandBaseCourseIndices)
			{
				GroupCanonical += FString::Printf(TEXT(":B%d"), Base);
			}
			for (const int32 Count : Group.GroundedExteriorPostStationCounts)
			{
				GroupCanonical += FString::Printf(TEXT(":FP%d"), Count);
			}
			TArray<int32> GroupMembers = Group.MemberIndices;
			GroupMembers.Sort();
			for (const int32 MemberIndex : GroupMembers)
			{
				GroupCanonical += FString::Printf(TEXT(":M%d"), MemberIndex);
			}
			Group.GroupCrc32 = FCrc::StrCrc32(*GroupCanonical);
		}

		if (!ValidateEnvelopeAndVoids(Silhouette, Roots, OutPlan, OutError)
			|| !ValidateNoPenetration(OutPlan, OutError)
			|| !ValidatePreflightCaps(Profile.BeamSettings.BeamB.BeamA, OutPlan, OutError))
		{
			return false;
		}

		FString CoreCanonical = FString::Printf(TEXT("Recipe:%d:H=%d:V=%d"),
			Density.RecipeId, Density.HorizontalUnits, Density.VerticalUnits);
		FString SupportCanonical;
		FString FinalCanonical;
		for (const FComponentPlan& Component : OutPlan.Components)
		{
			CoreCanonical += FString::Printf(TEXT("|C:%d:%s:G=%lld:F=%d"),
				Component.ComponentId, *Component.SemanticRootPath,
				QHash(Component.GroundPlaneZCM), Component.GroundedFaceMask);
			for (const int32 Source : Component.SourceVolumeIds)
			{
				CoreCanonical += FString::Printf(TEXT(":V%d"), Source);
			}
			for (const FVerticalSupportWitness& Witness : Component.VerticalSupportWitnesses)
			{
				CoreCanonical += FString::Printf(TEXT(":W%d>%d@%lld[%lld,%lld,%lld,%lld]"),
					Witness.LowerSourceVolumeId, Witness.UpperSourceVolumeId, QHash(Witness.ContactZCM),
					QHash(Witness.PositiveXYOverlap.Min.X), QHash(Witness.PositiveXYOverlap.Min.Y),
					QHash(Witness.PositiveXYOverlap.Max.X), QHash(Witness.PositiveXYOverlap.Max.Y));
			}
		}
		for (const FCoreCellPlan& Core : OutPlan.CoreCells)
		{
			CoreCanonical += FString::Printf(
				TEXT("|CORE:%d:C=%d:S=%d:H=%d:HP=%d:P=%d:T=%d:BT=%d:G=%d:XB=%d:B=%lld,%lld,%lld,%lld,%lld,%lld"),
				Core.CoreCellId, Core.ComponentId, Core.BodySourceVolumeId,
				static_cast<int32>(Core.HierarchyRole),
				Core.HighProjectionRegionId, Core.PodiumMainCoreCellId,
				Core.TopCourseIndex,
				Core.BodyTopCourseIndex, Core.CompositeCoreGroupId,
				Core.CrossCoreBearingContactCount,
				QHash(Core.LocalBounds.Min.X), QHash(Core.LocalBounds.Min.Y),
				QHash(Core.LocalBounds.Min.Z), QHash(Core.LocalBounds.Max.X),
				QHash(Core.LocalBounds.Max.Y), QHash(Core.LocalBounds.Max.Z));
			for (const int32 Station : Core.XStations)
			{
				CoreCanonical += FString::Printf(TEXT(":X%d"), Station);
			}
			for (const int32 Station : Core.YStations)
			{
				CoreCanonical += FString::Printf(TEXT(":Y%d"), Station);
			}
		}
		for (const FPlannedMember& Member : OutPlan.Members)
		{
			AppendMemberCanonical(FinalCanonical, Member);
			if (Member.SkeletonKind == ESkeletonMemberKind::CoreCourse
				|| Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
			{
				AppendMemberCanonical(CoreCanonical, Member);
			}
			else
			{
				AppendMemberCanonical(SupportCanonical, Member);
			}
		}
		for (const FABTSM73BeamASupportVoid& Void : OutPlan.ReservedSupportVoids)
		{
			SupportCanonical += FString::Printf(TEXT("|V:%d:%d:%lld:%lld:%lld:%lld:%lld:%lld"),
				Void.SpanSourceVolumeId, Void.SpanAxisIndex,
				QHash(Void.Bounds.Min.X), QHash(Void.Bounds.Min.Y), QHash(Void.Bounds.Min.Z),
				QHash(Void.Bounds.Max.X), QHash(Void.Bounds.Max.Y), QHash(Void.Bounds.Max.Z));
		}
		OutPlan.Summary.CorePlanHash = HashText(CoreCanonical);
		OutPlan.Summary.SupportPlanHash = HashText(SupportCanonical);
		OutPlan.Summary.FinalGeometryHash = HashText(CoreCanonical + SupportCanonical + FinalCanonical);
		OutPlan.Summary.bAccepted = true;
		return true;
	}

	void RefreshAssemblySummary(FABTSM73BeamAGenerationResult& Assembly)
	{
		FABTSM73BeamAPreviewSummary& Summary = Assembly.Summary;
		Summary.BayCount = Assembly.Bays.Num();
		Summary.JointCount = Assembly.Joints.Num();
		Summary.MemberCount = Assembly.Members.Num();
		Summary.AssemblyCount = Assembly.Assemblies.Num();
		Summary.BearingContactCount = Assembly.BearingContacts.Num();
		Summary.XMemberCount = Summary.YMemberCount = Summary.ZMemberCount = Summary.DiagonalMemberCount = 0;
		for (const FABTSM73BeamAMember& Member : Assembly.Members)
		{
			switch (Member.Axis)
			{
			case EABTSM73BeamAFrameAxis::X: ++Summary.XMemberCount; break;
			case EABTSM73BeamAFrameAxis::Y: ++Summary.YMemberCount; break;
			case EABTSM73BeamAFrameAxis::Z: ++Summary.ZMemberCount; break;
			default: ++Summary.DiagonalMemberCount; break;
			}
		}
	}
}

int64 FABTSM73BeamC3V3SkeletonFirstGenerator::ComputeEnvelopeHashForDiagnostics(
	const FABTSM73DAG5BV2GenerationResult& Silhouette) const
{
	return Silhouette.Summary.bAccepted ? ComputeEnvelopeHash(Silhouette) : 0;
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::BuildPlan(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	ABTSM73BeamC3V3::FPlan& OutPlan,
	FString& OutError) const
{
	return BuildPlanForStage(Profile, Silhouette,
		ABTSM73BeamC3V3::EGenerationStage::CompleteStaticDAG,
		OutPlan, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::BuildPlanForStage(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	const ABTSM73BeamC3V3::EGenerationStage Stage,
	ABTSM73BeamC3V3::FPlan& OutPlan,
	FString& OutError) const
{
	using namespace ABTSM73BeamC3V3;
	OutPlan = FPlan();
	OutError.Reset();
	const double StageStartSeconds = FPlatformTime::Seconds();
	if (!Profile.bAccepted || !Silhouette.Summary.bAccepted)
	{
		OutError = TEXT("BeamC3V3UpstreamRejected");
		return false;
	}
	if (!FMath::IsNearlyEqual(Profile.BeamSettings.BeamB.BeamA.BlockCrossSectionCM,
		static_cast<float>(BlockUnitsCM), KINDA_SMALL_NUMBER))
	{
		OutError = TEXT("BeamC3V3Requires36CMSection");
		return false;
	}
	if (Profile.VisualComplexity.MinimumBrickCount <= 0
		|| Profile.VisualComplexity.MaximumBrickCount < Profile.VisualComplexity.MinimumBrickCount)
	{
		OutError = TEXT("BeamC3V3InvalidBrickWindow");
		return false;
	}
	const int64 EnvelopeHash = ComputeEnvelopeHash(Silhouette);
	OutPlan.Summary.EnvelopeHash = EnvelopeHash;
	TArray<FRoot> Roots;
	int32 UnreachableVolumeCount = 0;
	if (!CollectRoots(Silhouette, Roots, UnreachableVolumeCount, OutError))
	{
		OutPlan.Summary.UnreachableVolumeCount = UnreachableVolumeCount;
		OutPlan.Summary.RejectReason = OutError;
		return false;
	}
	const FDensityRecipe Density = ResolveDensityRecipe(Profile);
	const bool bBuilt = BuildCandidate(
		Profile, Silhouette, Roots, Density, Stage, StageStartSeconds,
		OutPlan, OutError);
	// BuildCandidate resets its output before planning. Restore the canonical
	// envelope identity for both accepted and fail-closed structural results.
	OutPlan.Summary.EnvelopeHash = EnvelopeHash;
	if (!bBuilt)
	{
		OutPlan.Summary.RejectReason = OutError;
		return false;
	}
	if (Stage == EGenerationStage::CompleteStaticDAG
		&& (OutPlan.Members.Num() < Profile.VisualComplexity.MinimumBrickCount
		|| OutPlan.Members.Num() > Profile.VisualComplexity.MaximumBrickCount)
		)
	{
		int32 CoreOwnedCount = 0;
		int32 GroupHorizontalCount = 0;
		int32 GroupPostCount = 0;
		int32 RoofCount = 0;
		int32 SharedCount = 0;
		int32 OtherCount = 0;
		for (const FPlannedMember& Member : OutPlan.Members)
		{
			if (Member.OwnerKind == EOwnerKind::CoreCell)
			{
				++CoreOwnedCount;
			}
			else if (Member.OwnerKind == EOwnerKind::BuildingGroupShell)
			{
				(Member.Axis == EABTSM73BeamAFrameAxis::Z
					? GroupPostCount : GroupHorizontalCount)++;
			}
			else if (Member.OwnerKind == EOwnerKind::Roof)
			{
				++RoofCount;
			}
			else if (Member.SkeletonKind == ESkeletonMemberKind::SharedCourse)
			{
				++SharedCount;
			}
			else
			{
				++OtherCount;
			}
		}
		OutError = FString::Printf(
			TEXT("BeamC3V3ResolvedDensityOutsideBrickWindow:Recipe=%d:H=%d:V=%d:Count=%d:Window=%d..%d:Breakdown=Core%d,GroupH%d,GroupZ%d,Roof%d,Shared%d,Other%d"),
			Density.RecipeId, Density.HorizontalUnits, Density.VerticalUnits, OutPlan.Members.Num(),
			Profile.VisualComplexity.MinimumBrickCount, Profile.VisualComplexity.MaximumBrickCount,
			CoreOwnedCount, GroupHorizontalCount, GroupPostCount, RoofCount,
			SharedCount, OtherCount);
		OutPlan.Summary.bAccepted = false;
		OutPlan.Summary.RejectReason = OutError;
		return false;
	}

	FString SeatCanonical;
	for (int32 MemberIndex = 0; MemberIndex < OutPlan.Members.Num(); ++MemberIndex)
	{
		const FPlannedMember& Member = OutPlan.Members[MemberIndex];
		if (Member.bRequiresGroundSeat)
		{
			SeatCanonical += FString::Printf(TEXT("|G:%d:C=%d"),
				MemberIndex, Member.ComponentId);
		}
		for (const int32 Lower : Member.RequiredLowerMemberIndices)
		{
			SeatCanonical += FString::Printf(TEXT("|S:%d>%d"), Lower, MemberIndex);
		}
	}
	const FString Identity = FString::Printf(
		TEXT("Profile=%s:Tier=%d:Catalog=%lld:Resolved=%lld:Grammar=%lld:WFC=%lld:Result=%lld:Envelope=%lld"),
		*Profile.GameplayProfileId.ToString(), Profile.DifficultyTier,
		Profile.ProfileCatalogHash, Profile.ResolvedSettingsHash,
		Silhouette.Summary.GrammarHash, Silhouette.Summary.WFCHash,
		Silhouette.Summary.ResultHash, OutPlan.Summary.EnvelopeHash);
	OutPlan.Summary.CorePlanHash = HashText(FString::Printf(TEXT("%s:Core=%lld"),
		*Identity, OutPlan.Summary.CorePlanHash));
	OutPlan.Summary.SupportPlanHash = HashText(FString::Printf(
		TEXT("%s:Core=%lld:Support=%lld:Seats=%s"), *Identity,
		OutPlan.Summary.CorePlanHash, OutPlan.Summary.SupportPlanHash, *SeatCanonical));
	OutPlan.Summary.FinalGeometryHash = HashText(FString::Printf(
		TEXT("%s:Core=%lld:Support=%lld:Geometry=%lld"), *Identity,
		OutPlan.Summary.CorePlanHash, OutPlan.Summary.SupportPlanHash,
		OutPlan.Summary.FinalGeometryHash));
	if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
		TEXT("PlanFinalization"), OutPlan, OutError))
	{
		return false;
	}
	if (Stage == EGenerationStage::CoreAndShared)
	{
		OutPlan.Summary.bStage1WithinTimeBudget = true;
	}
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateStage1TimingBudgetForTesting(
	const double Elapsed,
	ABTSM73BeamC3V3::FPlan& InOutPlan,
	FString& OutError) const
{
	InOutPlan = ABTSM73BeamC3V3::FPlan();
	OutError.Reset();
	return ValidateStage1ElapsedBudget(
		ABTSM73BeamC3V3::EGenerationStage::CoreAndShared,
		Elapsed, TEXT("AutomationFixture"), InOutPlan, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::EvaluateSharedEndpointReachabilityForTesting(
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	TArray<ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic>& OutDiagnostics,
	FString& OutError) const
{
	OutDiagnostics.Reset();
	OutError.Reset();
	TArray<FRoot> Roots;
	int32 UnreachableVolumeCount = 0;
	if (!Silhouette.Summary.bAccepted
		|| !CollectRoots(Silhouette, Roots, UnreachableVolumeCount, OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("BeamC3V3ReachabilityUpstreamRejected");
		}
		return false;
	}
	for (const FABTSM73DAG5BV2Volume& Span : Silhouette.Volumes)
	{
		if (!IsSpanRole(Span.Role))
		{
			continue;
		}
		if ((Span.SpanAxisIndex != 0 && Span.SpanAxisIndex != 1)
			|| Span.NegativeSupportVolumeId == INDEX_NONE
			|| Span.PositiveSupportVolumeId == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3ReachabilitySpanInvalid:Volume=%d"), Span.VolumeId);
			return false;
		}
		int32 NegativeComponent = INDEX_NONE;
		int32 PositiveComponent = INDEX_NONE;
		for (int32 ComponentIndex = 0; ComponentIndex < Roots.Num(); ++ComponentIndex)
		{
			NegativeComponent = Roots[ComponentIndex].SourceVolumeIds.Contains(
				Span.NegativeSupportVolumeId) ? ComponentIndex : NegativeComponent;
			PositiveComponent = Roots[ComponentIndex].SourceVolumeIds.Contains(
				Span.PositiveSupportVolumeId) ? ComponentIndex : PositiveComponent;
		}
		if (NegativeComponent == INDEX_NONE || PositiveComponent == INDEX_NONE
			|| NegativeComponent == PositiveComponent)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V3ReachabilityEndpointComponentInvalid:Volume=%d:Negative=%d:Positive=%d"),
				Span.VolumeId, NegativeComponent, PositiveComponent);
			return false;
		}
		const double GroundZCM = Roots[NegativeComponent].GroundZCM;
		int32 RailBottomCourse = QRelativeCeil(Span.LocalBounds.Min.Z, GroundZCM);
		const int32 RequiredParity = Span.SpanAxisIndex == 0 ? 0 : 1;
		if ((RailBottomCourse & 1) != RequiredParity)
		{
			++RailBottomCourse;
		}
		int32 HighestSharedCourse = RailBottomCourse;
		if (GroundZCM + (RailBottomCourse + 3) * BlockUnitsCM
			<= Span.LocalBounds.Max.Z + GeometryToleranceCM)
		{
			HighestSharedCourse += 2;
		}
		const int32 RequiredTopCourse = HighestSharedCourse + 2;
		AppendSharedEndpointReachabilityDiagnostics(
			Roots[NegativeComponent], NegativeComponent, Span, true,
			RequiredTopCourse, OutDiagnostics);
		AppendSharedEndpointReachabilityDiagnostics(
			Roots[PositiveComponent], PositiveComponent, Span, false,
			RequiredTopCourse, OutDiagnostics);
	}
	OutDiagnostics.Sort([](
		const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic& A,
		const ABTSM73BeamC3V3::FSharedEndpointReachabilityDiagnostic& B)
	{
		if (A.SpanVolumeId != B.SpanVolumeId)
		{
			return A.SpanVolumeId < B.SpanVolumeId;
		}
		if (A.bNegativeEndpoint != B.bNegativeEndpoint)
		{
			return A.bNegativeEndpoint;
		}
		if (!FMath::IsNearlyEqual(A.EndpointInsetCM, B.EndpointInsetCM,
			GeometryToleranceCM))
		{
			return A.EndpointInsetCM < B.EndpointInsetCM;
		}
		if (!FMath::IsNearlyEqual(A.CandidateBounds.Min.Y,
			B.CandidateBounds.Min.Y, GeometryToleranceCM))
		{
			return A.CandidateBounds.Min.Y < B.CandidateBounds.Min.Y;
		}
		return A.CandidateBounds.Min.X < B.CandidateBounds.Min.X;
	});
	if (OutDiagnostics.IsEmpty())
	{
		OutError = TEXT("BeamC3V3ReachabilityNoSupportedSpan");
		return false;
	}
	return true;
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateSolidCoverageForTesting(
	const FBox& Solid,
	const TArray<FBox>& AllowedBoxes,
	FVector& OutUncoveredPoint) const
{
	return SolidCoveredByBoxes(Solid, AllowedBoxes, OutUncoveredPoint);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateBearingContactsForTesting(
	const FABTSM73BeamAPreviewSettings& Settings,
	const ABTSM73BeamC3V3::FPlan& Plan,
	const TArray<FABTSM73BeamABearingContact>& ActualContacts,
	FString& OutError) const
{
	return ValidateActualBearingPairs(Settings, Plan, ActualContacts, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateExteriorPostStationsForTesting(
	ABTSM73BeamC3V3::FPlan& InOutPlan,
	FString& OutError) const
{
	TArray<bool> GroundedMembers;
	return ResolveGroundedMemberMask(InOutPlan, GroundedMembers, OutError)
		&& ValidateGroundedExteriorPostStations(InOutPlan, GroundedMembers, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateSkeletonTopologyForTesting(
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	ABTSM73BeamC3V3::FPlan& InOutPlan,
	FString& OutError) const
{
	TArray<FRoot> Roots;
	int32 UnreachableVolumeCount = 0;
	OutError.Reset();
	if (!CollectRoots(Silhouette, Roots, UnreachableVolumeCount, OutError))
	{
		return false;
	}
	return ValidateSkeletonTopology(Silhouette, Roots, InOutPlan, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::ValidateGeometryForTesting(
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	ABTSM73BeamC3V3::FPlan& InOutPlan,
	FString& OutError) const
{
	TArray<FRoot> Roots;
	int32 UnreachableVolumeCount = 0;
	OutError.Reset();
	InOutPlan.Summary.EnvelopeViolationCount = 0;
	InOutPlan.Summary.ProtectedVoidViolationCount = 0;
	InOutPlan.Summary.PenetrationCount = 0;
	if (!CollectRoots(Silhouette, Roots, UnreachableVolumeCount, OutError))
	{
		return false;
	}
	return ValidateEnvelopeAndVoids(
			Silhouette, Roots, InOutPlan, OutError)
		&& ValidateNoPenetration(InOutPlan, OutError);
}
#endif

bool FABTSM73BeamC3V3SkeletonFirstGenerator::Generate(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	ABTSM73BeamC3V3::FGenerationResult& OutResult,
	FString& OutError) const
{
	return GenerateForStage(Profile, Silhouette,
		ABTSM73BeamC3V3::EGenerationStage::CompleteStaticDAG,
		OutResult, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::GenerateStage1(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	ABTSM73BeamC3V3::FGenerationResult& OutResult,
	FString& OutError) const
{
	return GenerateForStage(Profile, Silhouette,
		ABTSM73BeamC3V3::EGenerationStage::CoreAndShared,
		OutResult, OutError);
}

bool FABTSM73BeamC3V3SkeletonFirstGenerator::GenerateForStage(
	const FABTSM73BeamD0ResolvedProfile& Profile,
	const FABTSM73DAG5BV2GenerationResult& Silhouette,
	const ABTSM73BeamC3V3::EGenerationStage Stage,
	ABTSM73BeamC3V3::FGenerationResult& OutResult,
	FString& OutError) const
{
	using namespace ABTSM73BeamC3V3;
	OutResult = FGenerationResult();
	OutError.Reset();
	if (!BuildPlanForStage(Profile, Silhouette, Stage, OutResult.Plan, OutError))
	{
		return false;
	}
	FPlan& Plan = OutResult.Plan;
	const double StageStartSeconds = FPlatformTime::Seconds()
		- Plan.Summary.Stage1TotalMilliseconds / 1000.0;
	FStage1PhaseTimer MemberEmissionTimer(
		Plan.Summary.MemberEmissionMilliseconds);
	FABTSM73BeamAGenerationResult& Assembly = OutResult.Assembly;
	Assembly.ReservedSupportVoids = Plan.ReservedSupportVoids;
	TMap<FString, int32> JointByPosition;
	const double JointScale = 1.0 / FMath::Max(0.1,
		static_cast<double>(Profile.BeamSettings.BeamB.BeamA.JointMergeToleranceCM));
	auto AddJoint = [&Assembly, &JointByPosition, JointScale](
		const FVector& Point, const EABTSM73BeamAJointRole Role)
	{
		const FString Key = FString::Printf(TEXT("%lld:%lld:%lld"),
			FMath::RoundToInt64(Point.X * JointScale), FMath::RoundToInt64(Point.Y * JointScale),
			FMath::RoundToInt64(Point.Z * JointScale));
		if (const int32* Existing = JointByPosition.Find(Key))
		{
			return *Existing;
		}
		FABTSM73BeamAJoint& Joint = Assembly.Joints.AddDefaulted_GetRef();
		Joint.JointId = Assembly.Joints.Num() - 1;
		Joint.LocalPosition = Point;
		Joint.Role = Role;
		JointByPosition.Add(Key, Joint.JointId);
		return Joint.JointId;
	};

	for (const FComponentPlan& Component : Plan.Components)
	{
		FABTSM73BeamABay& Bay = Assembly.Bays.AddDefaulted_GetRef();
		Bay.BayId = Assembly.Bays.Num() - 1;
		Bay.SourceVolumeId = Component.SourceVolumeIds.IsEmpty() ? INDEX_NONE : Component.SourceVolumeIds[0];
		Bay.LocalBounds = Component.OccupiedBounds;
		Bay.PreferredAxis = EABTSM73BeamAFrameAxis::X;
		FABTSM73BeamAAssembly& Owner = Assembly.Assemblies.AddDefaulted_GetRef();
		Owner.AssemblyId = Assembly.Assemblies.Num() - 1;
		Owner.BayId = Bay.BayId;
		Owner.Type = EABTSM73BeamAAssemblyType::CribCore;
	}
	TMap<int32, int32> GroupAssemblyById;
	for (const FBuildingGroupPlan& Group : Plan.BuildingGroups)
	{
		FABTSM73BeamABay& Bay = Assembly.Bays.AddDefaulted_GetRef();
		Bay.BayId = Assembly.Bays.Num() - 1;
		Bay.SourceVolumeId = INDEX_NONE;
		Bay.LocalBounds = Group.LocalBounds;
		Bay.PreferredAxis = EABTSM73BeamAFrameAxis::X;
		FABTSM73BeamAAssembly& Owner = Assembly.Assemblies.AddDefaulted_GetRef();
		Owner.AssemblyId = Assembly.Assemblies.Num() - 1;
		Owner.BayId = Bay.BayId;
		Owner.Type = EABTSM73BeamAAssemblyType::StackedFrameBay;
		GroupAssemblyById.Add(Group.GroupId, Owner.AssemblyId);
	}
	TMap<int32, int32> SpanAssemblyByVolume;
	for (const FPlannedMember& Planned : Plan.Members)
	{
		if (Planned.OwnerKind != EOwnerKind::SupportedSpan
			|| SpanAssemblyByVolume.Contains(Planned.OwnerId))
		{
			continue;
		}
		const FABTSM73DAG5BV2Volume* SpanVolume = FindVolume(Silhouette, Planned.OwnerId);
		if (SpanVolume == nullptr)
		{
			OutError = TEXT("BeamC3V3SupportedSpanOwnerVolumeMissing");
			return false;
		}
		FABTSM73BeamABay& Bay = Assembly.Bays.AddDefaulted_GetRef();
		Bay.BayId = Assembly.Bays.Num() - 1;
		Bay.SourceVolumeId = SpanVolume->VolumeId;
		Bay.LocalBounds = SpanVolume->LocalBounds;
		Bay.PreferredAxis = SpanVolume->SpanAxisIndex == 0
			? EABTSM73BeamAFrameAxis::X : EABTSM73BeamAFrameAxis::Y;
		FABTSM73BeamAAssembly& Owner = Assembly.Assemblies.AddDefaulted_GetRef();
		Owner.AssemblyId = Assembly.Assemblies.Num() - 1;
		Owner.BayId = Bay.BayId;
		Owner.Type = EABTSM73BeamAAssemblyType::BridgeFrameBay;
		SpanAssemblyByVolume.Add(SpanVolume->VolumeId, Owner.AssemblyId);
	}
	if (Assembly.Bays.Num() != Plan.Summary.PlannedBayCount)
	{
		OutError = TEXT("BeamC3V3PlannedEmittedBayMismatch");
		return false;
	}
	for (const FPlannedMember& Planned : Plan.Members)
	{
		if ((Assembly.Members.Num() & 0xFF) == 0)
		{
			MemberEmissionTimer.Checkpoint();
			if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
				TEXT("MemberEmission"), Plan, OutError))
			{
				return false;
			}
		}
		const int32 OwnerAssemblyId = Planned.OwnerKind == EOwnerKind::SupportedSpan
			? SpanAssemblyByVolume.FindRef(Planned.OwnerId)
			: Planned.OwnerKind == EOwnerKind::BuildingGroupShell
				? GroupAssemblyById.FindRef(Planned.OwnerId) : Planned.ComponentId;
		if (!Assembly.Assemblies.IsValidIndex(OwnerAssemblyId))
		{
			OutError = TEXT("BeamC3V3InvalidMemberOwner");
			return false;
		}
		FABTSM73BeamAMember& Member = Assembly.Members.AddDefaulted_GetRef();
		Member.MemberId = Assembly.Members.Num() - 1;
		Member.JointA = AddJoint(Planned.LocalStart,
			Planned.bRequiresGroundSeat ? EABTSM73BeamAJointRole::GroundFoot
			: Planned.Axis == EABTSM73BeamAFrameAxis::Z
				? EABTSM73BeamAJointRole::CrossBearing : EABTSM73BeamAJointRole::BeamEnd);
		Member.JointB = AddJoint(Planned.LocalEnd,
			Planned.Role == EABTSM73BeamAMemberRole::RoofCourse
				? EABTSM73BeamAJointRole::RoofNode : EABTSM73BeamAJointRole::CrossBearing);
		Member.Axis = Planned.Axis;
		Member.Role = Planned.Role;
		Member.LengthCM = FVector::Distance(Planned.LocalStart, Planned.LocalEnd);
		FABTSM73BeamAAssembly& Owner = Assembly.Assemblies[OwnerAssemblyId];
		Owner.MemberIds.Add(Member.MemberId);
		Owner.JointIds.AddUnique(Member.JointA);
		Owner.JointIds.AddUnique(Member.JointB);
	}
	if (Assembly.Joints.Num() != Plan.Summary.PlannedJointCount
		|| Assembly.Members.Num() != Plan.Summary.PlannedMemberCount)
	{
		OutError = FString::Printf(TEXT("BeamC3V3PlannedEmittedIRMismatch:Joints=%d/%d:Members=%d/%d"),
			Assembly.Joints.Num(), Plan.Summary.PlannedJointCount,
			Assembly.Members.Num(), Plan.Summary.PlannedMemberCount);
		return false;
	}
	Assembly.Summary.SourceVolumeCount = Silhouette.Volumes.Num();
	Assembly.Summary.BayGraphHash = Plan.Summary.CorePlanHash;
	Assembly.Summary.BeamGraphHash = Plan.Summary.FinalGeometryHash;
	MemberEmissionTimer.Stop();
	if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
		TEXT("MemberEmission"), Plan, OutError))
	{
		return false;
	}
	if (!ABTSM73BeamA::RebuildBearingContacts(Profile.BeamSettings.BeamB.BeamA, Assembly, OutError))
	{
		return false;
	}
	if (!ValidateActualBearingPairs(Profile.BeamSettings.BeamB.BeamA,
		Plan, Assembly.BearingContacts, OutError))
	{
		return false;
	}

	TSet<uint64> ActualContacts;
	for (const FABTSM73BeamABearingContact& Contact : Assembly.BearingContacts)
	{
		ActualContacts.Add(BearingPairKey(Contact.LowerMemberId, Contact.UpperMemberId));
	}
	Plan.Summary.VerifiedSeatCount = 0;
	Plan.Summary.SeatMismatchCount = 0;
	for (int32 UpperIndex = 0; UpperIndex < Plan.Members.Num(); ++UpperIndex)
	{
		const FPlannedMember& Planned = Plan.Members[UpperIndex];
		if (Planned.bRequiresGroundSeat)
		{
			const FBox Bounds = PlannedMemberBounds(Planned);
			double GroundZ = DBL_MAX;
			if (Plan.Components.IsValidIndex(Planned.ComponentId))
			{
				GroundZ = Plan.Components[Planned.ComponentId].GroundPlaneZCM;
			}
			else if (Planned.OwnerKind == EOwnerKind::BuildingGroupShell
				&& Plan.BuildingGroups.IsValidIndex(Planned.OwnerId))
			{
				GroundZ = Plan.BuildingGroups[Planned.OwnerId].GroundPlaneZCM;
			}
			if (FMath::Abs(Bounds.Min.Z - GroundZ) <= GeometryToleranceCM)
			{
				++Plan.Summary.VerifiedSeatCount;
			}
			else
			{
				++Plan.Summary.SeatMismatchCount;
			}
		}
		for (const int32 LowerIndex : Planned.RequiredLowerMemberIndices)
		{
			const uint64 Key = BearingPairKey(LowerIndex, UpperIndex);
			if (ActualContacts.Contains(Key))
			{
				++Plan.Summary.VerifiedSeatCount;
			}
			else
			{
				++Plan.Summary.SeatMismatchCount;
			}
		}
	}
	if (Plan.Summary.SeatMismatchCount > 0
		|| Plan.Summary.VerifiedSeatCount
			!= Plan.Summary.GroundSeatCount + Plan.Summary.PlannedSeatCount)
	{
		OutError = FString::Printf(TEXT("BeamC3V3SeatVerificationFailed:Verified=%d:Expected=%d:Mismatch=%d"),
			Plan.Summary.VerifiedSeatCount,
			Plan.Summary.GroundSeatCount + Plan.Summary.PlannedSeatCount,
			Plan.Summary.SeatMismatchCount);
		return false;
	}
	RefreshAssemblySummary(Assembly);
	Assembly.Summary.bAccepted = true;
	Plan.Summary.EmittedMemberCount = Assembly.Members.Num();
	if (!CheckStage1TimeBudget(Stage, StageStartSeconds,
		TEXT("AssemblyValidation"), Plan, OutError))
	{
		return false;
	}
	if (Stage == EGenerationStage::CoreAndShared)
	{
		Plan.Summary.bStage1WithinTimeBudget = true;
	}
	return true;
}
