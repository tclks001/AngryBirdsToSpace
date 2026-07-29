// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGFailurePatternRewriter.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73PostFailureValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Misc/Crc.h"

namespace
{
	const FABTSM73BrickNode* FindPatternNode(
		const FABTSM73StructureData& Data,
		const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](
			const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	}

	const FABTSM73DAGMacroLayout* FindPatternLayout(
		const FABTSM73DAGSpatialLayout& Layout,
		const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate([MacroNodeId](
			const FABTSM73DAGMacroLayout& Candidate)
		{
			return Candidate.MacroNodeId == MacroNodeId;
		});
	}

	const FABTSM73DAGSelectedSupport* FindPatternSupport(
		const FABTSM73DAGSpatialLayout& Layout,
		const int32 SupportMacroNodeId,
		const int32 LoadMacroNodeId)
	{
		return Layout.SelectedSupports.FindByPredicate(
			[SupportMacroNodeId, LoadMacroNodeId](
				const FABTSM73DAGSelectedSupport& Candidate)
			{
				return Candidate.SupportMacroNodeId == SupportMacroNodeId
					&& Candidate.LoadMacroNodeId == LoadMacroNodeId;
			});
	}

	void SortUniquePatternIds(TArray<int32>& NodeIds)
	{
		NodeIds.Sort();
		for (int32 Index = NodeIds.Num() - 1; Index > 0; --Index)
		{
			if (NodeIds[Index] == NodeIds[Index - 1]) NodeIds.RemoveAt(Index);
		}
	}

	bool MappingOwnsGeneralizedFrontier(
		const FABTSM73DAGPhysicalSupportMapping& Mapping,
		const FABTSM73DAGFailureFrontierCandidate& Frontier)
	{
		auto OwnsNode = [&Mapping](const int32 NodeId)
		{
			return NodeId == Mapping.SupportPlateNodeId
				|| NodeId == Mapping.LoadPlateNodeId
				|| Mapping.ColumnNodeIds.Contains(NodeId);
		};
		for (const int32 NodeId : Frontier.CandidateNodeIds)
		{
			if (!OwnsNode(NodeId))
			{
				return false;
			}
		}
		for (const FABTSM73DAGFailureEdgeRef& Edge
			: Frontier.CandidateEdges)
		{
			if (!OwnsNode(Edge.LowerNodeId)
				|| !OwnsNode(Edge.UpperNodeId))
			{
				return false;
			}
		}
		for (const int32 NodeId : Frontier.ProtectedRootNodeIds)
		{
			if (!OwnsNode(NodeId))
			{
				return false;
			}
		}
		return true;
	}

	double PatternNodeMass(
		const FABTSM73BrickNode& Node,
		const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles)
	{
		const FABTSM7MaterialProfile* Profile =
			FABTSM7MaterialProfileLibrary::FindProfile(
				MaterialProfiles,
				Node.Material);
		if (Profile == nullptr
			|| !FMath::IsFinite(Profile->DensityGPerCubicCM)
			|| Profile->DensityGPerCubicCM <= 0.0f)
		{
			return -1.0;
		}
		const FVector Dimensions = Node.DimensionsCM.ComponentMax(FVector(1.0f));
		return static_cast<double>(Dimensions.X)
			* Dimensions.Y
			* Dimensions.Z
			* Profile->DensityGPerCubicCM;
	}

	void GatherMacroClosure(
		const FABTSM73DAGGenerationResult& Graph,
		const int32 RootMacroNodeId,
		TArray<int32>& OutMacroNodeIds)
	{
		TSet<int32> Visited;
		Visited.Add(RootMacroNodeId);
		OutMacroNodeIds.Reset();
		OutMacroNodeIds.Add(RootMacroNodeId);
		for (int32 Head = 0; Head < OutMacroNodeIds.Num(); ++Head)
		{
			for (const FABTSM73DAGSupportEdge& Edge : Graph.SupportEdges)
			{
				if (Edge.SupportNodeId != OutMacroNodeIds[Head]
					|| Visited.Contains(Edge.LoadNodeId))
				{
					continue;
				}
				Visited.Add(Edge.LoadNodeId);
				OutMacroNodeIds.Add(Edge.LoadNodeId);
			}
		}
		SortUniquePatternIds(OutMacroNodeIds);
	}

	void GatherPhysicalClosure(
		const FABTSM73StructureData& Data,
		const int32 RootNodeId,
		TArray<int32>& OutNodeIds)
	{
		TSet<int32> Visited;
		Visited.Add(RootNodeId);
		OutNodeIds.Reset();
		OutNodeIds.Add(RootNodeId);
		for (int32 Head = 0; Head < OutNodeIds.Num(); ++Head)
		{
			for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
			{
				if (Edge.LowerNodeId != OutNodeIds[Head]
					|| Visited.Contains(Edge.UpperNodeId))
				{
					continue;
				}
				Visited.Add(Edge.UpperNodeId);
				OutNodeIds.Add(Edge.UpperNodeId);
			}
		}
		SortUniquePatternIds(OutNodeIds);
	}

	void GatherReachableWithout(
		const FABTSM73StructureData& Data,
		const TSet<int32>& RemovedNodeIds,
		TSet<int32>& OutReachable)
	{
		TArray<int32> Queue;
		OutReachable.Reset();
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			if (RemovedNodeIds.Contains(GroundNodeId)
				|| OutReachable.Contains(GroundNodeId))
			{
				continue;
			}
			OutReachable.Add(GroundNodeId);
			Queue.Add(GroundNodeId);
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
			{
				if (Edge.LowerNodeId != Queue[Head]
					|| RemovedNodeIds.Contains(Edge.UpperNodeId)
					|| OutReachable.Contains(Edge.UpperNodeId))
				{
					continue;
				}
				OutReachable.Add(Edge.UpperNodeId);
				Queue.Add(Edge.UpperNodeId);
			}
		}
	}

	float PatternCross(
		const FVector2D& Origin,
		const FVector2D& A,
		const FVector2D& B)
	{
		return (A.X - Origin.X) * (B.Y - Origin.Y)
			- (A.Y - Origin.Y) * (B.X - Origin.X);
	}

	TArray<FVector2D> BuildPatternHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B)
		{
			return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y;
		});
		for (int32 Index = Points.Num() - 1; Index > 0; --Index)
		{
			if (Points[Index].Equals(Points[Index - 1], KINDA_SMALL_NUMBER))
			{
				Points.RemoveAt(Index);
			}
		}
		if (Points.Num() <= 2) return Points;
		TArray<FVector2D> Lower;
		for (const FVector2D& Point : Points)
		{
			while (Lower.Num() >= 2
				&& PatternCross(Lower[Lower.Num() - 2], Lower.Last(), Point)
					<= KINDA_SMALL_NUMBER)
			{
				Lower.Pop();
			}
			Lower.Add(Point);
		}
		TArray<FVector2D> Upper;
		for (int32 Index = Points.Num() - 1; Index >= 0; --Index)
		{
			const FVector2D& Point = Points[Index];
			while (Upper.Num() >= 2
				&& PatternCross(Upper[Upper.Num() - 2], Upper.Last(), Point)
					<= KINDA_SMALL_NUMBER)
			{
				Upper.Pop();
			}
			Upper.Add(Point);
		}
		Lower.Pop();
		Upper.Pop();
		Lower.Append(Upper);
		return Lower;
	}

	float PatternPointSegmentDistance(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B)
	{
		const FVector2D Segment = B - A;
		const float Denominator = Segment.SizeSquared();
		const float T = Denominator > SMALL_NUMBER
			? FMath::Clamp(
				FVector2D::DotProduct(Point - A, Segment) / Denominator,
				0.0f,
				1.0f)
			: 0.0f;
		return FVector2D::Distance(Point, A + Segment * T);
	}

	/** Positive inside, negative outside. */
	float PatternInsideMargin(
		const FVector2D& Point,
		const TArray<FVector2D>& Hull)
	{
		if (Hull.Num() < 3) return -BIG_NUMBER;
		bool bInside = true;
		float MinimumDistance = BIG_NUMBER;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D& A = Hull[Index];
			const FVector2D& B = Hull[(Index + 1) % Hull.Num()];
			if (PatternCross(A, B, Point) < -KINDA_SMALL_NUMBER) bInside = false;
			MinimumDistance = FMath::Min(
				MinimumDistance,
				PatternPointSegmentDistance(Point, A, B));
		}
		return bInside ? MinimumDistance : -MinimumDistance;
	}

	void AddPatternContactCorners(
		const FABTSM73BrickNode& Lower,
		const FABTSM73BrickNode& Upper,
		TArray<FVector2D>& OutPoints)
	{
		const float XMin = FMath::Max(
			Lower.LocalCenter.X - Lower.DimensionsCM.X * 0.5f,
			Upper.LocalCenter.X - Upper.DimensionsCM.X * 0.5f);
		const float XMax = FMath::Min(
			Lower.LocalCenter.X + Lower.DimensionsCM.X * 0.5f,
			Upper.LocalCenter.X + Upper.DimensionsCM.X * 0.5f);
		const float YMin = FMath::Max(
			Lower.LocalCenter.Y - Lower.DimensionsCM.Y * 0.5f,
			Upper.LocalCenter.Y - Upper.DimensionsCM.Y * 0.5f);
		const float YMax = FMath::Min(
			Lower.LocalCenter.Y + Lower.DimensionsCM.Y * 0.5f,
			Upper.LocalCenter.Y + Upper.DimensionsCM.Y * 0.5f);
		if (XMax <= XMin || YMax <= YMin) return;
		OutPoints.Add(FVector2D(XMin, YMin));
		OutPoints.Add(FVector2D(XMin, YMax));
		OutPoints.Add(FVector2D(XMax, YMin));
		OutPoints.Add(FVector2D(XMax, YMax));
	}

	uint32 BuildRealizedPatternHash(
		const FABTSM73DAGFailurePatternResult& Result)
	{
		FString Canonical = FString::Printf(
			TEXT("P=%d|S=%u|SM=%d|LM=%d|SP=%d|LP=%d|D=%d,%d|I=%d|T=%d|R=%d|O=%d|"),
			static_cast<int32>(Result.Pattern),
			Result.SourceFrontierHash,
			Result.SupportMacroNodeId,
			Result.LoadMacroNodeId,
			Result.SupportPlateNodeId,
			Result.LoadPlateNodeId,
			FMath::RoundToInt(Result.ExpectedFailureDirectionLocal.X * 1000.0f),
			FMath::RoundToInt(Result.ExpectedFailureDirectionLocal.Y * 1000.0f),
			FMath::RoundToInt(Result.InitialSupportMarginCM * 1000.0f),
			FMath::RoundToInt(Result.PostFailureTipMarginCM * 1000.0f),
			FMath::RoundToInt(Result.ReseatRisk * 10000.0f),
			FMath::RoundToInt(Result.OffsetSeamShiftCM * 1000.0f));
		auto AppendIds = [&Canonical](const TCHAR* Prefix, const TArray<int32>& Ids)
		{
			Canonical += Prefix;
			for (const int32 NodeId : Ids)
			{
				Canonical += FString::Printf(TEXT("%d,"), NodeId);
			}
			Canonical += TEXT("|");
		};
		AppendIds(TEXT("W="), Result.WeakNodeIds);
		AppendIds(TEXT("P="), Result.RemainingSupportNodeIds);
		AppendIds(TEXT("A="), Result.AffectedMainBodyNodeIds);
		const uint32 Hash = FCrc::StrCrc32(*Canonical);
		return Hash != 0 ? Hash : 1u;
	}

	bool IsSupportedFailurePattern(const EABTSM73DAGFailurePattern Pattern)
	{
		return Pattern == EABTSM73DAGFailurePattern::InternalSingleSupport
			|| Pattern
				== EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport
			|| Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam;
	}
}

bool FABTSM73DAGFailurePatternRewriter::MakeIntent(
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const FABTSM73DAGFailureFrontierCandidate& SourceFrontier,
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGSpatialLayout& BaselineLayout,
	const FABTSM73StructureData& BaselineData,
	const EABTSM73DAGFailurePattern Pattern,
	FABTSM73DAGFailureRewriteIntent& OutIntent,
	FString& OutError,
	const FVector2D& PreferredFailureDirectionXY,
	const bool bMirrorPreferredDirection) const
{
	OutIntent = FABTSM73DAGFailureRewriteIntent();
	OutError.Reset();
	if (!PatternSettings.bEnableGeometryRewrite
		|| !IsSupportedFailurePattern(Pattern)
		|| !SourceFrontier.bAccepted
		|| SourceFrontier.FrontierHash == 0
		|| !Graph.bAccepted
		|| !BaselineLayout.bAccepted
		|| PatternSettings.MaxRewriteAttemptCount < 1
		|| PatternSettings.MaxRewriteAttemptCount > 128
		|| !FMath::IsFinite(PatternSettings.ContactAreaSafetyFactor)
		|| PatternSettings.ContactAreaSafetyFactor <= 1.0f
		|| PatternSettings.ContactAreaSafetyFactor > 2.0f
		|| !FMath::IsFinite(PatternSettings.OffsetSeamShiftRatio)
		|| PatternSettings.OffsetSeamShiftRatio <= 0.0f
		|| PatternSettings.OffsetSeamShiftRatio > 0.35f
		|| !FMath::IsFinite(PatternSettings.MinOffsetSeamShiftCM)
		|| PatternSettings.MinOffsetSeamShiftCM < 0.0f
		|| !FMath::IsFinite(DifficultySettings.MinInitialSupportMarginCM)
		|| DifficultySettings.MinInitialSupportMarginCM < 0.0f
		|| !FMath::IsFinite(DifficultySettings.MinTipMarginCM)
		|| DifficultySettings.MinTipMarginCM < 0.0f
		|| !FMath::IsFinite(DifficultySettings.MaxReseatRisk)
		|| DifficultySettings.MaxReseatRisk < 0.0f
		|| DifficultySettings.MaxReseatRisk > 1.0f)
	{
		OutError = TEXT("DAG3BSettingsOrSourceInvalid");
		return false;
	}
	const FABTSM73DAGFailureFrontierCandidate* OwnedSourceFrontier =
		BaselineData.DAGFailureFrontierAnalysis.Candidates.FindByPredicate(
			[&SourceFrontier](
				const FABTSM73DAGFailureFrontierCandidate& Candidate)
			{
				return Candidate.bAccepted
					&& Candidate.FrontierHash == SourceFrontier.FrontierHash
					&& Candidate.Kind == SourceFrontier.Kind
					&& Candidate.CandidateNodeIds
						== SourceFrontier.CandidateNodeIds
					&& Candidate.CandidateEdges
						== SourceFrontier.CandidateEdges
					&& Candidate.ProtectedRootNodeIds
						== SourceFrontier.ProtectedRootNodeIds
					&& Candidate.ExpectedAffectedNodeIds
						== SourceFrontier.ExpectedAffectedNodeIds
					&& Candidate.AffectedMainBodyNodeIds
						== SourceFrontier.AffectedMainBodyNodeIds
					&& Candidate.AffectedMacroNodeIds
						== SourceFrontier.AffectedMacroNodeIds;
			});
	if (!BaselineData.DAGFailureFrontierAnalysis.bAccepted
		|| OwnedSourceFrontier == nullptr)
	{
		OutError = TEXT("DAG3BStaleSourceFrontier");
		return false;
	}

	struct FMappingChoice
	{
		const FABTSM73DAGPhysicalSupportMapping* Mapping = nullptr;
		int32 Priority = MAX_int32;
	};
	TArray<FMappingChoice> Choices;
	const bool bGeneralizedFrontier =
		!SourceFrontier.CandidateEdges.IsEmpty();
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping
		: BaselineData.DAGPhysicalSupportMappings)
	{
		if (bGeneralizedFrontier
			&& !MappingOwnsGeneralizedFrontier(
				Mapping,
				SourceFrontier))
		{
			continue;
		}
		int32 Priority = MAX_int32;
		if (bGeneralizedFrontier)
		{
			Priority = 0;
		}
		else
		{
			for (const int32 CandidateNodeId
				: SourceFrontier.CandidateNodeIds)
			{
				if (Mapping.ColumnNodeIds.Contains(CandidateNodeId))
				{
					Priority = 0;
				}
				else if (Mapping.SupportPlateNodeId == CandidateNodeId)
				{
					Priority = FMath::Min(Priority, 1);
				}
				else if (Mapping.LoadPlateNodeId == CandidateNodeId)
				{
					Priority = FMath::Min(Priority, 3);
				}
			}
		}
		if (SourceFrontier.ProtectedRootNodeIds.Contains(Mapping.LoadPlateNodeId))
		{
			Priority = FMath::Min(Priority, 2);
		}
		if (Priority == MAX_int32) continue;
		FMappingChoice& Choice = Choices.AddDefaulted_GetRef();
		Choice.Mapping = &Mapping;
		Choice.Priority = Priority;
	}
	Choices.Sort([](const FMappingChoice& A, const FMappingChoice& B)
	{
		if (A.Priority != B.Priority) return A.Priority < B.Priority;
		if (A.Mapping->LoadMacroNodeId != B.Mapping->LoadMacroNodeId)
		{
			return A.Mapping->LoadMacroNodeId < B.Mapping->LoadMacroNodeId;
		}
		return A.Mapping->SupportMacroNodeId < B.Mapping->SupportMacroNodeId;
	});
	if (Choices.IsEmpty() || Choices[0].Mapping == nullptr)
	{
		OutError = bGeneralizedFrontier
			? TEXT("DAG3CGeneralizedCutNotRewritable")
			: TEXT("DAG3BFrontierMacroInterfaceMissing");
		return false;
	}
	if (Choices.Num() > 1
		&& Choices[1].Priority == Choices[0].Priority
		&& (Choices[1].Mapping->SupportMacroNodeId
				!= Choices[0].Mapping->SupportMacroNodeId
			|| Choices[1].Mapping->LoadMacroNodeId
				!= Choices[0].Mapping->LoadMacroNodeId))
	{
		OutError = TEXT("DAG3BFrontierMacroInterfaceAmbiguous");
		return false;
	}
	const FABTSM73DAGPhysicalSupportMapping& Mapping = *Choices[0].Mapping;
	const FABTSM73DAGSelectedSupport* BaselineSupport = FindPatternSupport(
		BaselineLayout,
		Mapping.SupportMacroNodeId,
		Mapping.LoadMacroNodeId);
	const FABTSM73DAGMacroLayout* SupportLayout = FindPatternLayout(
		BaselineLayout,
		Mapping.SupportMacroNodeId);
	const FABTSM73DAGMacroLayout* LoadLayout = FindPatternLayout(
		BaselineLayout,
		Mapping.LoadMacroNodeId);
	if (BaselineSupport == nullptr || SupportLayout == nullptr || LoadLayout == nullptr)
	{
		OutError = TEXT("DAG3BBaselineInterfaceLayoutMissing");
		return false;
	}

	OutIntent.bEnabled = true;
	OutIntent.Pattern = Pattern;
	OutIntent.SourceFrontierHash = SourceFrontier.FrontierHash;
	OutIntent.SupportMacroNodeId = Mapping.SupportMacroNodeId;
	OutIntent.LoadMacroNodeId = Mapping.LoadMacroNodeId;
	GatherMacroClosure(Graph, Mapping.LoadMacroNodeId, OutIntent.AffectedMacroNodeIds);
	if (OutIntent.AffectedMacroNodeIds.Contains(Mapping.SupportMacroNodeId)
		|| OutIntent.AffectedMacroNodeIds.Num() < 2)
	{
		OutError = TEXT("DAG3BMacroClosureInvalid");
		OutIntent = FABTSM73DAGFailureRewriteIntent();
		return false;
	}
	const FVector2D RegionSize = BaselineSupport->FeasibleColumnRegion.GetSize();
	const bool bAlongX = RegionSize.X >= RegionSize.Y;
	float Sign = 0.0f;
	const FVector2D PreferredDirection =
		PreferredFailureDirectionXY.GetSafeNormal();
	if (!PreferredDirection.IsNearlyZero())
	{
		const FVector2D PositiveAxis = bAlongX
			? FVector2D(1.0f, 0.0f)
			: FVector2D(0.0f, 1.0f);
		Sign = FVector2D::DotProduct(
			PreferredDirection,
			PositiveAxis) >= 0.0f ? 1.0f : -1.0f;
		if (bMirrorPreferredDirection) Sign *= -1.0f;
	}
	else
	{
		const uint32 DirectionBit = SourceFrontier.FrontierHash
			^ static_cast<uint32>(Mapping.SupportMacroNodeId * 196613)
			^ static_cast<uint32>(Mapping.LoadMacroNodeId * 3145739);
		Sign = (DirectionBit & 1u) != 0 ? 1.0f : -1.0f;
	}
	OutIntent.ExpectedFailureDirectionXY = bAlongX
		? FVector2D(Sign, 0.0f)
		: FVector2D(0.0f, Sign);
	OutIntent.ContactAreaSafetyFactor =
		PatternSettings.ContactAreaSafetyFactor;
	OutIntent.MinInitialSupportMarginCM =
		DifficultySettings.MinInitialSupportMarginCM;
	OutIntent.MinPostFailureTipMarginCM =
		DifficultySettings.MinTipMarginCM;
	OutIntent.MaxReseatRisk = DifficultySettings.MaxReseatRisk;
	OutIntent.BaselineInterfaceOffsetAlongDirectionCM =
		FVector2D::DotProduct(
			FVector2D(LoadLayout->PlateCenter - SupportLayout->PlateCenter),
			OutIntent.ExpectedFailureDirectionXY);
	if (Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam)
	{
		const float LongExtent = FMath::Max(RegionSize.X, RegionSize.Y);
		OutIntent.OffsetSeamShiftCM = FMath::Max(
			PatternSettings.MinOffsetSeamShiftCM,
			LongExtent * PatternSettings.OffsetSeamShiftRatio);
	}
	return true;
}

bool FABTSM73DAGFailurePatternRewriter::ValidateRealizedPattern(
	const FABTSM73DAGFailureRewriteIntent& Intent,
	const FABTSM73DAGFailurePatternSettings& PatternSettings,
	const FABTSM73GenerationSettings& GenerationSettings,
	const FABTSM73DifficultySettings& DifficultySettings,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles,
	const FABTSM73StructureData& Data,
	FABTSM73DAGFailurePatternResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73DAGFailurePatternResult();
	OutResult.bEnabled = PatternSettings.bEnableGeometryRewrite;
	OutResult.Pattern = Intent.Pattern;
	OutResult.SourceFrontierHash = Intent.SourceFrontierHash;
	OutResult.SupportMacroNodeId = Intent.SupportMacroNodeId;
	OutResult.LoadMacroNodeId = Intent.LoadMacroNodeId;
	OutResult.ExpectedFailureDirectionLocal = FVector(
		Intent.ExpectedFailureDirectionXY,
		0.0f);
	OutResult.OffsetSeamShiftCM = Intent.OffsetSeamShiftCM;
	OutError.Reset();
	auto Reject = [&OutResult, &OutError](const FString& Reason)
	{
		OutError = Reason;
		OutResult.RejectReason = Reason;
		return false;
	};
	if (!Intent.bEnabled
		|| !IsSupportedFailurePattern(Intent.Pattern)
		|| Data.Bricks.IsEmpty()
		|| Data.DAGMissingRequiredContactCount != 0
		|| Data.DAGUnexpectedBypassCount != 0)
	{
		return Reject(TEXT("DAG3BRealizedInputInvalid"));
	}
	if (Data.Bricks.Num() > GenerationSettings.MaxBrickCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG3BBrickBudgetExceeded:%d:%d"),
			Data.Bricks.Num(),
			GenerationSettings.MaxBrickCount));
	}
	for (int32 Index = 0; Index < Data.Bricks.Num(); ++Index)
	{
		const FABTSM73BrickNode& Node = Data.Bricks[Index];
		if (Node.NodeId != Index
			|| Node.Material != GenerationSettings.PrimaryMaterial
			|| Node.OriginalMaterial != Node.Material
			|| Node.bWeakPoint)
		{
			return Reject(TEXT("DAG3BMaterialOrIdentityChanged"));
		}
	}
	if (!Data.WeakPoints.IsEmpty())
	{
		return Reject(TEXT("DAG3BWeakPointRecordForbidden"));
	}

	const FABTSM73DAGPhysicalSupportMapping* Mapping =
		Data.DAGPhysicalSupportMappings.FindByPredicate([&Intent](
			const FABTSM73DAGPhysicalSupportMapping& Candidate)
		{
			return Candidate.SupportMacroNodeId == Intent.SupportMacroNodeId
				&& Candidate.LoadMacroNodeId == Intent.LoadMacroNodeId;
		});
	if (Mapping == nullptr
		|| Mapping->ColumnNodeIds.IsEmpty()
		|| Mapping->ColumnNodeIds.Num() != Mapping->ColumnRoles.Num())
	{
		return Reject(TEXT("DAG3BRealizedMappingMissing"));
	}
	OutResult.SupportPlateNodeId = Mapping->SupportPlateNodeId;
	OutResult.LoadPlateNodeId = Mapping->LoadPlateNodeId;
	for (int32 Index = 0; Index < Mapping->ColumnNodeIds.Num(); ++Index)
	{
		switch (Mapping->ColumnRoles[Index])
		{
		case EABTSM73DAGRealizedColumnRole::FailureWeak:
		case EABTSM73DAGRealizedColumnRole::FailureSeamKey:
			OutResult.WeakNodeIds.Add(Mapping->ColumnNodeIds[Index]);
			break;
		case EABTSM73DAGRealizedColumnRole::FailureStrongPivot:
			OutResult.RemainingSupportNodeIds.Add(Mapping->ColumnNodeIds[Index]);
			break;
		default:
			return Reject(TEXT("DAG3BUnexpectedOrdinaryRewriteColumn"));
		}
	}
	SortUniquePatternIds(OutResult.WeakNodeIds);
	SortUniquePatternIds(OutResult.RemainingSupportNodeIds);
	const bool bSingle =
		Intent.Pattern == EABTSM73DAGFailurePattern::InternalSingleSupport;
	if (OutResult.WeakNodeIds.Num() != 1
		|| (bSingle && !OutResult.RemainingSupportNodeIds.IsEmpty())
		|| (!bSingle && OutResult.RemainingSupportNodeIds.Num() != 1)
		|| Mapping->ColumnNodeIds.Num() != (bSingle ? 1 : 2))
	{
		return Reject(TEXT("DAG3BPatternColumnCardinalityInvalid"));
	}

	const FABTSM73BrickNode* LoadPlate =
		FindPatternNode(Data, Mapping->LoadPlateNodeId);
	const FABTSM73BrickNode* SupportPlate =
		FindPatternNode(Data, Mapping->SupportPlateNodeId);
	if (LoadPlate == nullptr || SupportPlate == nullptr)
	{
		return Reject(TEXT("DAG3BInterfacePlateMissing"));
	}
	TArray<int32> AffectedNodeIds;
	GatherPhysicalClosure(Data, Mapping->LoadPlateNodeId, AffectedNodeIds);
	double AffectedMass = 0.0;
	FVector AffectedCenterOfMass = FVector::ZeroVector;
	for (const int32 NodeId : AffectedNodeIds)
	{
		const FABTSM73BrickNode* Node = FindPatternNode(Data, NodeId);
		if (Node == nullptr) return Reject(TEXT("DAG3BAffectedNodeMissing"));
		const double Mass = PatternNodeMass(*Node, MaterialProfiles);
		if (Mass <= 0.0) return Reject(TEXT("DAG3BMaterialProfileInvalid"));
		AffectedMass += Mass;
		AffectedCenterOfMass += Node->LocalCenter * Mass;
		if (Node->bFailureFrontierMainBody)
		{
			OutResult.AffectedMainBodyNodeIds.Add(NodeId);
		}
	}
	if (AffectedMass <= SMALL_NUMBER
		|| OutResult.AffectedMainBodyNodeIds.IsEmpty())
	{
		return Reject(TEXT("DAG3BAffectedMassInvalid"));
	}
	AffectedCenterOfMass /= AffectedMass;
	SortUniquePatternIds(OutResult.AffectedMainBodyNodeIds);
	const FVector2D AffectedCOM(
		AffectedCenterOfMass.X,
		AffectedCenterOfMass.Y);

	TArray<FVector2D> FullContactPoints;
	TArray<FVector2D> RemainingContactPoints;
	for (const int32 ColumnNodeId : Mapping->ColumnNodeIds)
	{
		const FABTSM73BrickNode* Column = FindPatternNode(Data, ColumnNodeId);
		if (Column == nullptr) return Reject(TEXT("DAG3BColumnNodeMissing"));
		AddPatternContactCorners(*Column, *LoadPlate, FullContactPoints);
		if (OutResult.RemainingSupportNodeIds.Contains(ColumnNodeId))
		{
			AddPatternContactCorners(*Column, *LoadPlate, RemainingContactPoints);
		}
	}
	const TArray<FVector2D> FullHull = BuildPatternHull(FullContactPoints);
	OutResult.InitialSupportMarginCM = PatternInsideMargin(AffectedCOM, FullHull);
	if (OutResult.InitialSupportMarginCM + KINDA_SMALL_NUMBER
		< DifficultySettings.MinInitialSupportMarginCM)
	{
		return Reject(FString::Printf(
			TEXT("DAG3BInitialSupportMarginTooSmall:%.3f:%.3f"),
			OutResult.InitialSupportMarginCM,
			DifficultySettings.MinInitialSupportMarginCM));
	}

	TSet<int32> FullRemoved;
	for (const int32 NodeId : OutResult.WeakNodeIds) FullRemoved.Add(NodeId);
	for (const int32 NodeId : OutResult.RemainingSupportNodeIds)
	{
		FullRemoved.Add(NodeId);
	}
	TSet<int32> ReachableAfterFullCut;
	GatherReachableWithout(Data, FullRemoved, ReachableAfterFullCut);
	for (const int32 NodeId : AffectedNodeIds)
	{
		if (ReachableAfterFullCut.Contains(NodeId))
		{
			++OutResult.BypassSupportEdgeCount;
		}
	}
	if (OutResult.BypassSupportEdgeCount != 0)
	{
		return Reject(FString::Printf(
			TEXT("DAG3BFullFrontierBypass:%d"),
			OutResult.BypassSupportEdgeCount));
	}

	const int32 WeakNodeId = OutResult.WeakNodeIds[0];
	FABTSM73PostFailureValidator ReseatValidator;
	if (bSingle)
	{
		OutResult.ExpectedMotion = EABTSM73DAGFailureMotion::Drop;
		OutResult.RemovedColumnCount = 1;
		OutResult.PostFailureTipMarginCM = 0.0f;
		OutResult.ReseatRisk = ReseatValidator.EstimateVerticalReseatRisk(
			MaterialProfiles,
			Data,
			WeakNodeId,
			AffectedNodeIds);
	}
	else
	{
		TSet<int32> WeakRemoved;
		WeakRemoved.Add(WeakNodeId);
		TSet<int32> ReachableAfterWeakCut;
		GatherReachableWithout(Data, WeakRemoved, ReachableAfterWeakCut);
		if (!ReachableAfterWeakCut.Contains(Mapping->LoadPlateNodeId))
		{
			return Reject(TEXT("DAG3BRemainingPivotLostGroundPath"));
		}
		const TArray<FVector2D> RemainingHull =
			BuildPatternHull(RemainingContactPoints);
		const float RemainingInsideMargin =
			PatternInsideMargin(AffectedCOM, RemainingHull);
		OutResult.PostFailureTipMarginCM = -RemainingInsideMargin;
		if (OutResult.PostFailureTipMarginCM + KINDA_SMALL_NUMBER
			< DifficultySettings.MinTipMarginCM)
		{
			return Reject(FString::Printf(
				TEXT("DAG3BTipMarginTooSmall:%.3f:%.3f"),
				OutResult.PostFailureTipMarginCM,
				DifficultySettings.MinTipMarginCM));
		}
		FVector2D RemainingCenter = FVector2D::ZeroVector;
		for (const FVector2D& Point : RemainingHull) RemainingCenter += Point;
		if (!RemainingHull.IsEmpty()) RemainingCenter /= RemainingHull.Num();
		const FVector2D PredictedDirection =
			(AffectedCOM - RemainingCenter).GetSafeNormal();
		if (FVector2D::DotProduct(
			PredictedDirection,
			Intent.ExpectedFailureDirectionXY.GetSafeNormal()) < 0.25f)
		{
			return Reject(TEXT("DAG3BFailureDirectionMismatch"));
		}
		OutResult.ReseatRisk = ReseatValidator.EstimateVerticalReseatRisk(
			MaterialProfiles,
			Data,
			WeakNodeId,
			AffectedNodeIds);
		if (OutResult.ReseatRisk > DifficultySettings.MaxReseatRisk)
		{
			return Reject(FString::Printf(
				TEXT("DAG3BReseatRiskTooHigh:%.3f:%.3f"),
				OutResult.ReseatRisk,
				DifficultySettings.MaxReseatRisk));
		}
		OutResult.ExpectedMotion =
			Intent.Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam
			? EABTSM73DAGFailureMotion::SlideThenTip
			: EABTSM73DAGFailureMotion::Tip;
		OutResult.RemovedColumnCount = 1;
	}

	if (Intent.Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam)
	{
		const float RealizedInterfaceOffset =
			FVector2D::DotProduct(
				FVector2D(LoadPlate->LocalCenter - SupportPlate->LocalCenter),
				Intent.ExpectedFailureDirectionXY.GetSafeNormal());
		const float RealizedShift = RealizedInterfaceOffset
			- Intent.BaselineInterfaceOffsetAlongDirectionCM;
		if (RealizedShift + KINDA_SMALL_NUMBER
			< PatternSettings.MinOffsetSeamShiftCM
			|| RealizedShift + KINDA_SMALL_NUMBER
				< Intent.OffsetSeamShiftCM * 0.95f)
		{
			return Reject(FString::Printf(
				TEXT("DAG3BOffsetSeamShiftNotRealized:%.3f:%.3f"),
				RealizedShift,
				Intent.OffsetSeamShiftCM));
		}
		OutResult.OffsetSeamShiftCM = RealizedShift;
	}

	OutResult.RealizedPatternHash = BuildRealizedPatternHash(OutResult);
	OutResult.bApplied = true;
	return true;
}
