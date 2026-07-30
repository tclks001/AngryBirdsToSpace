// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Algo/Reverse.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGContactGraphBuilder.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureData.h"
#include "Game/ABTSM7GameMode.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 AddFrontierTestNode(
		FABTSM73StructureData& Data,
		const int32 MacroNodeId,
		const FVector& Center,
		const FVector& Dimensions,
		const EABTSM73BrickSemanticRole Role,
		const bool bMainBody = true)
	{
		FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
		Node.NodeId = Data.Bricks.Num() - 1;
		Node.MacroNodeId = MacroNodeId;
		Node.Material = EABTSM7BuildingMaterial::Wood;
		Node.OriginalMaterial = Node.Material;
		Node.LocalCenter = Center;
		Node.DimensionsCM = Dimensions;
		Node.SemanticRole = Role;
		Node.bFailureFrontierMainBody = bMainBody;
		return Node.NodeId;
	}

	void AddFrontierTestEdge(
		FABTSM73StructureData& Data,
		const int32 LowerNodeId,
		const int32 UpperNodeId)
	{
		FABTSM73SupportEdge& Edge = Data.SupportEdges.AddDefaulted_GetRef();
		Edge.LowerNodeId = LowerNodeId;
		Edge.UpperNodeId = UpperNodeId;
		Edge.ContactAreaCM2 = 400.0f;
	}

	void AddFrontierTestMapping(
		FABTSM73StructureData& Data,
		const int32 SupportMacroNodeId,
		const int32 LoadMacroNodeId,
		const int32 SupportPlateNodeId,
		const int32 LoadPlateNodeId,
		const TArray<int32>& ColumnNodeIds)
	{
		FABTSM73DAGPhysicalSupportMapping& Mapping =
			Data.DAGPhysicalSupportMappings.AddDefaulted_GetRef();
		Mapping.SupportMacroNodeId = SupportMacroNodeId;
		Mapping.LoadMacroNodeId = LoadMacroNodeId;
		Mapping.SupportPlateNodeId = SupportPlateNodeId;
		Mapping.LoadPlateNodeId = LoadPlateNodeId;
		if (ColumnNodeIds.Num() <= 1)
		{
			Mapping.SupportPattern = EABTSM73DAGSupportPattern::SingleColumnInterface;
		}
		else if (ColumnNodeIds.Num() == 2)
		{
			Mapping.SupportPattern = EABTSM73DAGSupportPattern::TwoColumnLine;
		}
		else
		{
			Mapping.SupportPattern = EABTSM73DAGSupportPattern::ThreeColumnTripod;
		}
		Mapping.RealizedColumnWidthCM = 20.0f;
		Mapping.ColumnNodeIds = ColumnNodeIds;
	}

	FABTSM73StructureData MakeFrontierChain()
	{
		FABTSM73StructureData Data;
		const int32 GroundPlate = AddFrontierTestNode(
			Data, 0, FVector(0.0f, 0.0f, 10.0f), FVector(120.0f, 120.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		const int32 ColumnA = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(0.0f, 0.0f, 60.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 PlateA = AddFrontierTestNode(
			Data, 1, FVector(0.0f, 0.0f, 110.0f), FVector(120.0f, 120.0f, 20.0f),
			EABTSM73BrickSemanticRole::Carrier);
		const int32 ColumnB = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(0.0f, 0.0f, 160.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 PlateB = AddFrontierTestNode(
			Data, 2, FVector(0.0f, 0.0f, 210.0f), FVector(120.0f, 120.0f, 20.0f),
			EABTSM73BrickSemanticRole::Carrier);
		const int32 ColumnC = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(0.0f, 0.0f, 260.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 TopPlate = AddFrontierTestNode(
			Data, 3, FVector(0.0f, 0.0f, 310.0f), FVector(120.0f, 120.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		Data.GroundNodeIds.Add(GroundPlate);
		AddFrontierTestEdge(Data, GroundPlate, ColumnA);
		AddFrontierTestEdge(Data, ColumnA, PlateA);
		AddFrontierTestEdge(Data, PlateA, ColumnB);
		AddFrontierTestEdge(Data, ColumnB, PlateB);
		AddFrontierTestEdge(Data, PlateB, ColumnC);
		AddFrontierTestEdge(Data, ColumnC, TopPlate);
		AddFrontierTestMapping(Data, 0, 1, GroundPlate, PlateA, {ColumnA});
		AddFrontierTestMapping(Data, 1, 2, PlateA, PlateB, {ColumnB});
		AddFrontierTestMapping(Data, 2, 3, PlateB, TopPlate, {ColumnC});
		Data.DAGMacroNodeCount = 4;
		Data.DAGSelectedSupportCount = 3;
		Data.DAGTopologyHash = 0xD3A00001u;
		return Data;
	}

	FABTSM73StructureData MakeFrontierDiamond()
	{
		FABTSM73StructureData Data;
		const int32 GroundLeft = AddFrontierTestNode(
			Data, 0, FVector(-50.0f, 0.0f, 10.0f), FVector(80.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		const int32 GroundRight = AddFrontierTestNode(
			Data, 1, FVector(50.0f, 0.0f, 10.0f), FVector(80.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		const int32 LeftColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(-45.0f, 0.0f, 60.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 RightColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(45.0f, 0.0f, 60.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 Carrier = AddFrontierTestNode(
			Data, 2, FVector(0.0f, 0.0f, 110.0f), FVector(180.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Carrier);
		const int32 UpperColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(0.0f, 0.0f, 160.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 TopPlate = AddFrontierTestNode(
			Data, 3, FVector(0.0f, 0.0f, 210.0f), FVector(140.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		Data.GroundNodeIds = {GroundLeft, GroundRight};
		AddFrontierTestEdge(Data, GroundLeft, LeftColumn);
		AddFrontierTestEdge(Data, LeftColumn, Carrier);
		AddFrontierTestEdge(Data, GroundRight, RightColumn);
		AddFrontierTestEdge(Data, RightColumn, Carrier);
		AddFrontierTestEdge(Data, Carrier, UpperColumn);
		AddFrontierTestEdge(Data, UpperColumn, TopPlate);
		AddFrontierTestMapping(Data, 0, 2, GroundLeft, Carrier, {LeftColumn});
		AddFrontierTestMapping(Data, 1, 2, GroundRight, Carrier, {RightColumn});
		AddFrontierTestMapping(Data, 2, 3, Carrier, TopPlate, {UpperColumn});
		Data.DAGMacroNodeCount = 4;
		Data.DAGSelectedSupportCount = 3;
		Data.DAGTopologyHash = 0xD3A00002u;
		return Data;
	}

	FABTSM73StructureData MakeFrontierDualColumnInterface()
	{
		FABTSM73StructureData Data;
		const int32 GroundPlate = AddFrontierTestNode(
			Data, 0, FVector(0.0f, 0.0f, 10.0f), FVector(160.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		const int32 LeftColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(-40.0f, 0.0f, 60.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 RightColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(40.0f, 0.0f, 60.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 Carrier = AddFrontierTestNode(
			Data, 1, FVector(0.0f, 0.0f, 110.0f), FVector(160.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Carrier);
		const int32 UpperColumn = AddFrontierTestNode(
			Data, INDEX_NONE, FVector(0.0f, 0.0f, 160.0f), FVector(24.0f, 24.0f, 80.0f),
			EABTSM73BrickSemanticRole::Column);
		const int32 TopPlate = AddFrontierTestNode(
			Data, 2, FVector(0.0f, 0.0f, 210.0f), FVector(120.0f, 100.0f, 20.0f),
			EABTSM73BrickSemanticRole::Deck);
		Data.GroundNodeIds.Add(GroundPlate);
		AddFrontierTestEdge(Data, GroundPlate, LeftColumn);
		AddFrontierTestEdge(Data, GroundPlate, RightColumn);
		AddFrontierTestEdge(Data, LeftColumn, Carrier);
		AddFrontierTestEdge(Data, RightColumn, Carrier);
		AddFrontierTestEdge(Data, Carrier, UpperColumn);
		AddFrontierTestEdge(Data, UpperColumn, TopPlate);
		AddFrontierTestMapping(
			Data, 0, 1, GroundPlate, Carrier, {LeftColumn, RightColumn});
		AddFrontierTestMapping(Data, 1, 2, Carrier, TopPlate, {UpperColumn});
		Data.DAGMacroNodeCount = 3;
		Data.DAGSelectedSupportCount = 2;
		Data.DAGTopologyHash = 0xD3A00003u;
		return Data;
	}

	bool RebuildSyntheticContactGraph(
		FABTSM73StructureData& Data,
		FString& OutError)
	{
		FABTSM73DAGContactGraphBuilder ContactGraphBuilder;
		const FABTSM73DAGLayoutSettings LayoutSettings;
		return ContactGraphBuilder.RebuildAndAudit(
			LayoutSettings,
			Data,
			OutError);
	}

	FABTSM73DAGFailureFrontierSettings MakePermissiveFrontierSettings()
	{
		FABTSM73DAGFailureFrontierSettings Settings;
		Settings.bEnableAnalysis = true;
		Settings.MinNormalizedHeight = 0.0f;
		Settings.MaxNormalizedHeight = 1.0f;
		Settings.MinMainBodyAffectedMassRatio = 0.0f;
		Settings.TargetMainBodyAffectedMassRatio = 0.45f;
		Settings.MaxMainBodyAffectedMassRatio = 1.0f;
		Settings.MinAffectedHeightSpanNormalized = 0.0f;
		Settings.MinAffectedMacroNodeCount = 1;
		return Settings;
	}

	const FABTSM73DAGFailureFrontierCandidate* FindFrontierCandidate(
		const FABTSM73DAGFailureFrontierAnalysis& Analysis,
		const EABTSM73DAGFailureCandidateKind Kind,
		TArray<int32> CandidateNodeIds)
	{
		CandidateNodeIds.Sort();
		return Analysis.Candidates.FindByPredicate([Kind, &CandidateNodeIds](
			const FABTSM73DAGFailureFrontierCandidate& Candidate)
		{
			return Candidate.Kind == Kind && Candidate.CandidateNodeIds == CandidateNodeIds;
		});
	}

	bool EqualFrontierCandidate(
		const FABTSM73DAGFailureFrontierCandidate& A,
		const FABTSM73DAGFailureFrontierCandidate& B)
	{
		return A.bAccepted == B.bAccepted
			&& A.bDirectedDominator == B.bDirectedDominator
			&& A.Kind == B.Kind
			&& A.CandidateNodeIds == B.CandidateNodeIds
			&& A.ProtectedRootNodeIds == B.ProtectedRootNodeIds
			&& A.ExpectedAffectedNodeIds == B.ExpectedAffectedNodeIds
			&& A.AffectedMainBodyNodeIds == B.AffectedMainBodyNodeIds
			&& A.AffectedMacroNodeIds == B.AffectedMacroNodeIds
			&& FMath::IsNearlyEqual(A.NormalizedHeight, B.NormalizedHeight)
			&& FMath::IsNearlyEqual(
				A.MainBodyAffectedMassRatio,
				B.MainBodyAffectedMassRatio)
			&& FMath::IsNearlyEqual(
				A.AffectedHeightSpanNormalized,
				B.AffectedHeightSpanNormalized)
			&& A.BypassSupportEdgeCount == B.BypassSupportEdgeCount
			&& A.FrontierHash == B.FrontierHash
			&& A.RejectReason == B.RejectReason;
	}

	struct FRewritePatternCase
	{
		const TCHAR* Name;
		EABTSM73DAGFailurePattern Pattern;
		EABTSM73DAGFailureMotion ExpectedMotion;
		EABTSM73DAGRealizedColumnRole ExpectedWeakRole;
		int32 ExpectedColumnCount;
	};

	const FRewritePatternCase RewritePatternCases[] = {
		{
			TEXT("InternalSingleSupport"),
			EABTSM73DAGFailurePattern::InternalSingleSupport,
			EABTSM73DAGFailureMotion::Drop,
			EABTSM73DAGRealizedColumnRole::FailureWeak,
			1
		},
		{
			TEXT("InternalAsymmetricDualSupport"),
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			EABTSM73DAGFailureMotion::Tip,
			EABTSM73DAGRealizedColumnRole::FailureWeak,
			2
		},
		{
			TEXT("InternalOffsetSeam"),
			EABTSM73DAGFailurePattern::InternalOffsetSeam,
			EABTSM73DAGFailureMotion::SlideThenTip,
			EABTSM73DAGRealizedColumnRole::FailureSeamKey,
			2
		}
	};

	FABTSM7TaskGraphBuildingProfile MakeRewriteTestProfile()
	{
		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				EABTSM3TaskType::Workshop,
				EABTSM7BuildingMaterial::Wood);
		Profile.GenerationSettings.BuildingSeed = 1034266606;
		Profile.DAGGenerationSettings.BuildingSeed = 1034266606;
		return Profile;
	}

	bool BuildRewriteProfile(
		const FABTSM7TaskGraphBuildingProfile& Profile,
		FABTSM73StructureData& OutData,
		FString& OutError)
	{
		const TArray<FABTSM7MaterialProfile> MaterialProfiles =
			FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
		FABTSM73DAGBuildingPipeline Pipeline;
		return Pipeline.BuildWithFailurePattern(
			Profile.DAGGenerationSettings,
			Profile.DAGLayoutSettings,
			Profile.GenerationSettings,
			Profile.DAGFailureFrontierSettings,
			Profile.DAGFailurePatternSettings,
			Profile.DifficultySettings,
			MaterialProfiles,
			OutData,
			OutError);
	}

	bool BuildRewritePattern(
		const EABTSM73DAGFailurePattern Pattern,
		FABTSM73StructureData& OutData,
		FString& OutError,
		FABTSM7TaskGraphBuildingProfile* OutProfile = nullptr)
	{
		FABTSM7TaskGraphBuildingProfile Profile = MakeRewriteTestProfile();
		Profile.DAGFailureFrontierSettings.bEnableAnalysis = true;
		Profile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
		Profile.DAGFailurePatternSettings.Pattern = Pattern;
		if (OutProfile != nullptr) *OutProfile = Profile;
		return BuildRewriteProfile(Profile, OutData, OutError);
	}

	const FABTSM73BrickNode* FindRewriteNode(
		const FABTSM73StructureData& Data,
		const int32 NodeId)
	{
		if (Data.Bricks.IsValidIndex(NodeId)
			&& Data.Bricks[NodeId].NodeId == NodeId)
		{
			return &Data.Bricks[NodeId];
		}
		return Data.Bricks.FindByPredicate([NodeId](
			const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	}

	const FABTSM73DAGPhysicalSupportMapping* FindRewriteMapping(
		const FABTSM73StructureData& Data,
		const FABTSM73DAGFailurePatternResult& Result)
	{
		return Data.DAGPhysicalSupportMappings.FindByPredicate([&Result](
			const FABTSM73DAGPhysicalSupportMapping& Mapping)
		{
			return Mapping.SupportMacroNodeId == Result.SupportMacroNodeId
				&& Mapping.LoadMacroNodeId == Result.LoadMacroNodeId;
		});
	}

	bool HasRewriteSupportEdge(
		const FABTSM73StructureData& Data,
		const int32 LowerNodeId,
		const int32 UpperNodeId)
	{
		return Data.SupportEdges.ContainsByPredicate(
			[LowerNodeId, UpperNodeId](const FABTSM73SupportEdge& Edge)
			{
				return Edge.LowerNodeId == LowerNodeId
					&& Edge.UpperNodeId == UpperNodeId;
			});
	}

	bool HasRewriteGroundPathWithout(
		const FABTSM73StructureData& Data,
		const int32 TargetNodeId,
		const TArray<int32>& RemovedNodeIds)
	{
		TSet<int32> Removed;
		for (const int32 NodeId : RemovedNodeIds) Removed.Add(NodeId);
		if (Removed.Contains(TargetNodeId)) return false;
		TSet<int32> Reachable;
		TArray<int32> Queue;
		for (const int32 GroundNodeId : Data.GroundNodeIds)
		{
			if (Removed.Contains(GroundNodeId)
				|| Reachable.Contains(GroundNodeId))
			{
				continue;
			}
			Reachable.Add(GroundNodeId);
			Queue.Add(GroundNodeId);
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
			{
				if (Edge.LowerNodeId != Queue[Head]
					|| Removed.Contains(Edge.UpperNodeId)
					|| Reachable.Contains(Edge.UpperNodeId))
				{
					continue;
				}
				Reachable.Add(Edge.UpperNodeId);
				Queue.Add(Edge.UpperNodeId);
			}
		}
		return Reachable.Contains(TargetNodeId);
	}

	bool EqualRewriteBrick(
		const FABTSM73BrickNode& A,
		const FABTSM73BrickNode& B)
	{
		return A.NodeId == B.NodeId
			&& A.MacroNodeId == B.MacroNodeId
			&& A.Material == B.Material
			&& A.OriginalMaterial == B.OriginalMaterial
			&& A.LocalCenter == B.LocalCenter
			&& A.DimensionsCM == B.DimensionsCM
			&& A.SemanticRole == B.SemanticRole
			&& A.StoreyIndex == B.StoreyIndex
			&& A.BayIndex == B.BayIndex
			&& A.WeakPointRole == B.WeakPointRole
			&& A.WeakPointScore == B.WeakPointScore
			&& A.UnsupportedMassRatio == B.UnsupportedMassRatio
			&& A.AttackExposure == B.AttackExposure
			&& A.EstimatedHits == B.EstimatedHits
			&& A.bFailureFrontierMainBody == B.bFailureFrontierMainBody
			&& A.bWeakPoint == B.bWeakPoint
			&& A.bReinforcedCriticalNode == B.bReinforcedCriticalNode;
	}

	bool EqualRewriteGeometry(
		const FABTSM73StructureData& A,
		const FABTSM73StructureData& B)
	{
		if (A.Bricks.Num() != B.Bricks.Num()
			|| A.SupportEdges.Num() != B.SupportEdges.Num()
			|| A.DAGPhysicalSupportMappings.Num()
				!= B.DAGPhysicalSupportMappings.Num()
			|| A.GroundNodeIds != B.GroundNodeIds
			|| A.DAGMacroNodeCount != B.DAGMacroNodeCount
			|| A.DAGSelectedSupportCount != B.DAGSelectedSupportCount
			|| A.DAGMissingRequiredContactCount
				!= B.DAGMissingRequiredContactCount
			|| A.DAGUnexpectedBypassCount != B.DAGUnexpectedBypassCount
			|| A.DAGMinSupportContactAreaRatio
				!= B.DAGMinSupportContactAreaRatio
			|| A.DAGTopologyHash != B.DAGTopologyHash)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Bricks.Num(); ++Index)
		{
			if (!EqualRewriteBrick(A.Bricks[Index], B.Bricks[Index]))
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.SupportEdges.Num(); ++Index)
		{
			const FABTSM73SupportEdge& EdgeA = A.SupportEdges[Index];
			const FABTSM73SupportEdge& EdgeB = B.SupportEdges[Index];
			if (EdgeA.LowerNodeId != EdgeB.LowerNodeId
				|| EdgeA.UpperNodeId != EdgeB.UpperNodeId
				|| EdgeA.ContactAreaCM2 != EdgeB.ContactAreaCM2)
			{
				return false;
			}
		}
		for (int32 Index = 0;
			Index < A.DAGPhysicalSupportMappings.Num();
			++Index)
		{
			const FABTSM73DAGPhysicalSupportMapping& MappingA =
				A.DAGPhysicalSupportMappings[Index];
			const FABTSM73DAGPhysicalSupportMapping& MappingB =
				B.DAGPhysicalSupportMappings[Index];
			if (MappingA.SupportMacroNodeId != MappingB.SupportMacroNodeId
				|| MappingA.LoadMacroNodeId != MappingB.LoadMacroNodeId
				|| MappingA.SupportPlateNodeId != MappingB.SupportPlateNodeId
				|| MappingA.LoadPlateNodeId != MappingB.LoadPlateNodeId
				|| MappingA.SupportPattern != MappingB.SupportPattern
				|| MappingA.RealizedColumnWidthCM
					!= MappingB.RealizedColumnWidthCM
				|| MappingA.ColumnNodeIds != MappingB.ColumnNodeIds
				|| MappingA.ColumnRoles != MappingB.ColumnRoles)
			{
				return false;
			}
		}
		return true;
	}

	bool EqualRewritePatternResult(
		const FABTSM73DAGFailurePatternResult& A,
		const FABTSM73DAGFailurePatternResult& B)
	{
		return A.bEnabled == B.bEnabled
			&& A.bApplied == B.bApplied
			&& A.Pattern == B.Pattern
			&& A.ExpectedMotion == B.ExpectedMotion
			&& A.SourceFrontierHash == B.SourceFrontierHash
			&& A.RealizedPatternHash == B.RealizedPatternHash
			&& A.SupportMacroNodeId == B.SupportMacroNodeId
			&& A.LoadMacroNodeId == B.LoadMacroNodeId
			&& A.SupportPlateNodeId == B.SupportPlateNodeId
			&& A.LoadPlateNodeId == B.LoadPlateNodeId
			&& A.RewriteAttemptCount == B.RewriteAttemptCount
			&& A.RemovedColumnCount == B.RemovedColumnCount
			&& A.WeakNodeIds == B.WeakNodeIds
			&& A.RemainingSupportNodeIds == B.RemainingSupportNodeIds
			&& A.AffectedMainBodyNodeIds == B.AffectedMainBodyNodeIds
			&& A.ExpectedFailureDirectionLocal
				== B.ExpectedFailureDirectionLocal
			&& A.InitialSupportMarginCM == B.InitialSupportMarginCM
			&& A.PostFailureTipMarginCM == B.PostFailureTipMarginCM
			&& A.ReseatRisk == B.ReseatRisk
			&& A.OffsetSeamShiftCM == B.OffsetSeamShiftCM
			&& A.BypassSupportEdgeCount == B.BypassSupportEdgeCount
			&& A.RejectReason == B.RejectReason;
	}

	bool EqualRewriteAnalysis(
		const FABTSM73DAGFailureFrontierAnalysis& A,
		const FABTSM73DAGFailureFrontierAnalysis& B)
	{
		if (A.bEnabled != B.bEnabled
			|| A.bAccepted != B.bAccepted
			|| A.AcceptedCandidateCount != B.AcceptedCandidateCount
			|| A.SelectedCandidateIndex != B.SelectedCandidateIndex
			|| A.SelectedFrontierHash != B.SelectedFrontierHash
			|| A.RejectReason != B.RejectReason
			|| A.Candidates.Num() != B.Candidates.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Candidates.Num(); ++Index)
		{
			if (!EqualFrontierCandidate(
				A.Candidates[Index],
				B.Candidates[Index]))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3DirectedCutSemanticsTest,
	"ABTS.M73DAG3.DirectedCutSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3DirectedCutSemanticsTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis ChainAnalysis;
	FString Error;
	FABTSM73StructureData Chain = MakeFrontierChain();
	TestTrue(
		FString::Printf(TEXT("Chain passes the real contact audit: %s"), *Error),
		RebuildSyntheticContactGraph(Chain, Error));
	if (!Error.IsEmpty()) return false;
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakePermissiveFrontierSettings();
	TestTrue(
		FString::Printf(TEXT("Directed chain produces a frontier: %s"), *Error),
		Analyzer.Analyze(Settings, Profiles, Chain, ChainAnalysis, Error));
	const FABTSM73DAGFailureFrontierCandidate* ChainMiddle =
		FindFrontierCandidate(
			ChainAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Internal chain node is enumerated"), ChainMiddle);
	if (ChainMiddle != nullptr)
	{
		TestTrue(TEXT("Internal chain node is a directed dominator"),
			ChainMiddle->bDirectedDominator);
		TestTrue(TEXT("Internal chain cut is accepted"), ChainMiddle->bAccepted);
	}

	FABTSM73StructureData Diamond = MakeFrontierDiamond();
	FABTSM73DAGFailureFrontierAnalysis DiamondAnalysis;
	Error.Reset();
	TestTrue(
		FString::Printf(TEXT("Multi-ground diamond passes the real contact audit: %s"), *Error),
		RebuildSyntheticContactGraph(Diamond, Error));
	if (!Error.IsEmpty()) return false;
	TestTrue(
		FString::Printf(TEXT("Multi-ground diamond has one valid combined cut set: %s"), *Error),
		Analyzer.Analyze(Settings, Profiles, Diamond, DiamondAnalysis, Error));
	const FABTSM73DAGFailureFrontierCandidate* LeftOnly =
		FindFrontierCandidate(
			DiamondAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{2});
	TestNotNull(TEXT("One branch of a diamond is enumerated"), LeftOnly);
	if (LeftOnly != nullptr)
	{
		TestFalse(TEXT("One branch is not a directed dominator"),
			LeftOnly->bDirectedDominator);
		TestFalse(TEXT("One branch is rejected as an incomplete cut"),
			LeftOnly->bAccepted);
		TestTrue(TEXT("Incomplete branch reports a bypass"),
			LeftOnly->BypassSupportEdgeCount > 0);
	}
	const FABTSM73DAGFailureFrontierCandidate* JointCut =
		FindFrontierCandidate(
			DiamondAnalysis,
			EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet,
			{2, 3});
	TestNotNull(TEXT("Two real Mappings are aggregated into one interface cut set"), JointCut);
	if (JointCut != nullptr)
	{
		TestTrue(TEXT("Joint cut disconnects the protected load"),
			JointCut->bDirectedDominator);
		TestTrue(TEXT("Joint cut is accepted"), JointCut->bAccepted);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3DeterminismTest,
	"ABTS.M73DAG3.FrontierEnumerationDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3DeterminismTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakePermissiveFrontierSettings();
	FABTSM73StructureData Forward = MakeFrontierDiamond();
	FABTSM73StructureData Reordered = Forward;
	Algo::Reverse(Reordered.Bricks);
	Algo::Reverse(Reordered.SupportEdges);
	Algo::Reverse(Reordered.GroundNodeIds);
	Algo::Reverse(Reordered.DAGPhysicalSupportMappings);
	for (FABTSM73DAGPhysicalSupportMapping& Mapping : Reordered.DAGPhysicalSupportMappings)
	{
		Algo::Reverse(Mapping.ColumnNodeIds);
	}

	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis A;
	FABTSM73DAGFailureFrontierAnalysis B;
	FString ErrorA;
	FString ErrorB;
	TestTrue(TEXT("Forward analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, Forward, A, ErrorA));
	TestTrue(TEXT("Reordered input analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, Reordered, B, ErrorB));
	TestEqual(TEXT("Accepted candidate count is stable"),
		A.AcceptedCandidateCount, B.AcceptedCandidateCount);
	TestEqual(TEXT("Selected Frontier hash is stable"),
		A.SelectedFrontierHash, B.SelectedFrontierHash);
	TestEqual(TEXT("Candidate count is stable"), A.Candidates.Num(), B.Candidates.Num());
	for (int32 Index = 0; Index < A.Candidates.Num() && B.Candidates.IsValidIndex(Index); ++Index)
	{
		TestTrue(TEXT("Every sorted candidate is deterministic"),
			EqualFrontierCandidate(A.Candidates[Index], B.Candidates[Index]));
	}

	FABTSM73StructureData DualForward = MakeFrontierDualColumnInterface();
	FString DualAuditError;
	TestTrue(TEXT("Two-column interface passes the real contact audit"),
		RebuildSyntheticContactGraph(DualForward, DualAuditError));
	if (!DualAuditError.IsEmpty()) return false;
	FABTSM73StructureData DualReordered = DualForward;
	Algo::Reverse(DualReordered.Bricks);
	Algo::Reverse(DualReordered.SupportEdges);
	Algo::Reverse(DualReordered.DAGPhysicalSupportMappings);
	for (FABTSM73DAGPhysicalSupportMapping& Mapping : DualReordered.DAGPhysicalSupportMappings)
	{
		Algo::Reverse(Mapping.ColumnNodeIds);
	}
	FABTSM73DAGFailureFrontierAnalysis DualA;
	FABTSM73DAGFailureFrontierAnalysis DualB;
	ErrorA.Reset();
	ErrorB.Reset();
	TestTrue(TEXT("Two-column interface forward analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, DualForward, DualA, ErrorA));
	TestTrue(TEXT("Two-column interface permutation succeeds"),
		Analyzer.Analyze(Settings, Profiles, DualReordered, DualB, ErrorB));
	TestEqual(TEXT("Column-node permutation preserves selected hash"),
		DualA.SelectedFrontierHash, DualB.SelectedFrontierHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3MainBodyMetricsTest,
	"ABTS.M73DAG3.MainBodyMassAndSpanGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3MainBodyMetricsTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakePermissiveFrontierSettings();
	FABTSM73StructureData Baseline = MakeFrontierChain();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis Before;
	FString Error;
	TestTrue(TEXT("Baseline analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, Baseline, Before, Error));
	const FABTSM73DAGFailureFrontierCandidate* BeforeCandidate =
		FindFrontierCandidate(
			Before,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Baseline middle cut exists"), BeforeCandidate);
	if (BeforeCandidate == nullptr) return false;

	FABTSM73StructureData WithHelper = Baseline;
	const int32 HelperNode = AddFrontierTestNode(
		WithHelper,
		INDEX_NONE,
		FVector(0.0f, 0.0f, 500.0f),
		FVector(400.0f, 400.0f, 360.0f),
		EABTSM73BrickSemanticRole::Payload,
		false);
	AddFrontierTestEdge(WithHelper, 6, HelperNode);
	FABTSM73DAGFailureFrontierAnalysis After;
	Error.Reset();
	TestTrue(TEXT("Helper-augmented analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, WithHelper, After, Error));
	const FABTSM73DAGFailureFrontierCandidate* AfterCandidate =
		FindFrontierCandidate(
			After,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Middle cut survives helper injection"), AfterCandidate);
	if (AfterCandidate != nullptr)
	{
		TestTrue(TEXT("Helper/Payload mass cannot inflate the main-body ratio"),
			FMath::IsNearlyEqual(
				BeforeCandidate->MainBodyAffectedMassRatio,
				AfterCandidate->MainBodyAffectedMassRatio,
				KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Helper/Payload cannot move normalized frontier height"),
			FMath::IsNearlyEqual(
				BeforeCandidate->NormalizedHeight,
				AfterCandidate->NormalizedHeight,
				KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Helper/Payload cannot shrink normalized body span"),
			FMath::IsNearlyEqual(
				BeforeCandidate->AffectedHeightSpanNormalized,
				AfterCandidate->AffectedHeightSpanNormalized,
				KINDA_SMALL_NUMBER));
		TestEqual(TEXT("Helper/Payload cannot change the main-body frontier identity"),
			BeforeCandidate->FrontierHash,
			AfterCandidate->FrontierHash);
		TestFalse(TEXT("Helper/Payload is excluded from affected main body"),
			AfterCandidate->AffectedMainBodyNodeIds.Contains(HelperNode));
	}

	FABTSM73DAGFailureFrontierSettings Strict = Settings;
	Strict.MinAffectedMacroNodeCount = 4;
	FABTSM73DAGFailureFrontierAnalysis StrictAnalysis;
	Error.Reset();
	TestFalse(TEXT("Impossible macro-span gate rejects the structure"),
		Analyzer.Analyze(Strict, Profiles, Baseline, StrictAnalysis, Error));
	TestEqual(TEXT("Strict gate has no accepted candidate"),
		StrictAnalysis.AcceptedCandidateCount, 0);

	FABTSM73DAGFailureFrontierSettings Exact = Settings;
	Exact.MinNormalizedHeight = BeforeCandidate->NormalizedHeight;
	Exact.MaxNormalizedHeight = BeforeCandidate->NormalizedHeight;
	Exact.MinMainBodyAffectedMassRatio =
		BeforeCandidate->MainBodyAffectedMassRatio;
	Exact.TargetMainBodyAffectedMassRatio =
		BeforeCandidate->MainBodyAffectedMassRatio;
	Exact.MaxMainBodyAffectedMassRatio =
		BeforeCandidate->MainBodyAffectedMassRatio;
	Exact.MinAffectedHeightSpanNormalized =
		BeforeCandidate->AffectedHeightSpanNormalized;
	Exact.MinAffectedMacroNodeCount =
		BeforeCandidate->AffectedMacroNodeIds.Num();
	FABTSM73DAGFailureFrontierAnalysis ExactAnalysis;
	Error.Reset();
	TestTrue(TEXT("Height, mass, span and Macro gates are inclusive at equality"),
		Analyzer.Analyze(Exact, Profiles, Baseline, ExactAnalysis, Error));
	const FABTSM73DAGFailureFrontierCandidate* ExactCandidate =
		FindFrontierCandidate(
			ExactAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Equality-gated candidate remains present"), ExactCandidate);
	if (ExactCandidate != nullptr)
	{
		TestTrue(TEXT("Equality-gated candidate is accepted"),
			ExactCandidate->bAccepted);
	}

	FABTSM73DAGFailureFrontierSettings HeightOutside = Settings;
	HeightOutside.MinNormalizedHeight =
		FMath::Min(1.0f, BeforeCandidate->NormalizedHeight + 0.01f);
	FABTSM73DAGFailureFrontierAnalysis HeightOutsideAnalysis;
	Error.Reset();
	Analyzer.Analyze(
		HeightOutside,
		Profiles,
		Baseline,
		HeightOutsideAnalysis,
		Error);
	const FABTSM73DAGFailureFrontierCandidate* HeightOutsideCandidate =
		FindFrontierCandidate(
			HeightOutsideAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Height-rejected candidate remains diagnosable"),
		HeightOutsideCandidate);
	if (HeightOutsideCandidate != nullptr)
	{
		TestTrue(TEXT("Height violation has an explicit reason"),
			HeightOutsideCandidate->RejectReason.StartsWith(
				TEXT("DAG3HeightOutsideRange")));
	}

	FABTSM73DAGFailureFrontierSettings MassOutside = Settings;
	MassOutside.MinMainBodyAffectedMassRatio = FMath::Min(
		1.0f,
		BeforeCandidate->MainBodyAffectedMassRatio + 0.01f);
	MassOutside.TargetMainBodyAffectedMassRatio =
		MassOutside.MinMainBodyAffectedMassRatio;
	FABTSM73DAGFailureFrontierAnalysis MassOutsideAnalysis;
	Error.Reset();
	Analyzer.Analyze(
		MassOutside,
		Profiles,
		Baseline,
		MassOutsideAnalysis,
		Error);
	const FABTSM73DAGFailureFrontierCandidate* MassOutsideCandidate =
		FindFrontierCandidate(
			MassOutsideAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Mass-rejected candidate remains diagnosable"),
		MassOutsideCandidate);
	if (MassOutsideCandidate != nullptr)
	{
		TestTrue(TEXT("Mass violation has an explicit reason"),
			MassOutsideCandidate->RejectReason.StartsWith(
				TEXT("DAG3MainBodyMassOutsideRange")));
	}

	FABTSM73DAGFailureFrontierSettings SpanOutside = Settings;
	SpanOutside.MinAffectedHeightSpanNormalized = FMath::Min(
		1.0f,
		BeforeCandidate->AffectedHeightSpanNormalized + 0.01f);
	FABTSM73DAGFailureFrontierAnalysis SpanOutsideAnalysis;
	Error.Reset();
	Analyzer.Analyze(
		SpanOutside,
		Profiles,
		Baseline,
		SpanOutsideAnalysis,
		Error);
	const FABTSM73DAGFailureFrontierCandidate* SpanOutsideCandidate =
		FindFrontierCandidate(
			SpanOutsideAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{3});
	TestNotNull(TEXT("Span-rejected candidate remains diagnosable"),
		SpanOutsideCandidate);
	if (SpanOutsideCandidate != nullptr)
	{
		TestTrue(TEXT("Span violation has an explicit reason"),
			SpanOutsideCandidate->RejectReason.StartsWith(
				TEXT("DAG3AffectedHeightSpanTooSmall")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3BypassAuditTest,
	"ABTS.M73DAG3.FrontierBypassAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3BypassAuditTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakePermissiveFrontierSettings();
	const FABTSM73StructureData Diamond = MakeFrontierDiamond();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis Analysis;
	FString Error;
	TestTrue(TEXT("Diamond analysis succeeds"),
		Analyzer.Analyze(Settings, Profiles, Diamond, Analysis, Error));
	const FABTSM73DAGFailureFrontierCandidate* Bypassed =
		FindFrontierCandidate(
			Analysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{2});
	TestNotNull(TEXT("Bypassed branch candidate exists"), Bypassed);
	if (Bypassed != nullptr)
	{
		TestEqual(TEXT("Exactly one alternate Ground path crosses the protected cut"),
			Bypassed->BypassSupportEdgeCount, 1);
		TestTrue(TEXT("Bypass reject reason is stable"),
			Bypassed->RejectReason.Contains(TEXT("DirectedCutIncomplete")));
	}
	const FABTSM73DAGFailureFrontierCandidate* Protected =
		FindFrontierCandidate(
			Analysis,
			EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet,
			{2, 3});
	TestNotNull(TEXT("Complete interface cut exists"), Protected);
	if (Protected != nullptr)
	{
		TestEqual(TEXT("Complete interface cut has no bypass"),
			Protected->BypassSupportEdgeCount, 0);
	}

	FABTSM73StructureData WithDeadSideBranch = Diamond;
	const int32 DeadSideNode = AddFrontierTestNode(
		WithDeadSideBranch,
		INDEX_NONE,
		FVector(-45.0f, 40.0f, 90.0f),
		FVector(20.0f, 20.0f, 20.0f),
		EABTSM73BrickSemanticRole::Connector,
		false);
	AddFrontierTestEdge(WithDeadSideBranch, 2, DeadSideNode);
	AddFrontierTestEdge(WithDeadSideBranch, DeadSideNode, 4);
	FABTSM73DAGFailureFrontierAnalysis DeadSideAnalysis;
	Error.Reset();
	TestTrue(TEXT("Baseline-reachable incidental side branch is analyzable"),
		Analyzer.Analyze(
			Settings,
			Profiles,
			WithDeadSideBranch,
			DeadSideAnalysis,
			Error));
	const FABTSM73DAGFailureFrontierCandidate* LeftInterface =
		FindFrontierCandidate(
			DeadSideAnalysis,
			EABTSM73DAGFailureCandidateKind::SupportInterfaceCutSet,
			{2});
	TestNotNull(TEXT("Left physical interface remains a candidate"),
		LeftInterface);
	if (LeftInterface != nullptr)
	{
		TestEqual(
			TEXT("An outside edge whose source lost Ground is not a second bypass"),
			LeftInterface->BypassSupportEdgeCount,
			1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3BudgetAndDisabledRegressionTest,
	"ABTS.M73DAG3.BudgetAndDisabledRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3BudgetAndDisabledRegressionTest::RunTest(const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73StructureData Chain = MakeFrontierChain();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierSettings Disabled;
	FABTSM73DAGFailureFrontierAnalysis DisabledAnalysis;
	FString Error;
	TestTrue(TEXT("Disabled DAG3-A is an accepted no-op"),
		Analyzer.Analyze(Disabled, Profiles, Chain, DisabledAnalysis, Error));
	TestFalse(TEXT("Disabled result records that analysis did not run"),
		DisabledAnalysis.bEnabled);
	TestFalse(TEXT("Disabled analysis cannot report an accepted frontier"),
		DisabledAnalysis.bAccepted);
	TestTrue(TEXT("Disabled analysis emits no candidates"),
		DisabledAnalysis.Candidates.IsEmpty());

	FABTSM73DAGFailureFrontierSettings Budgeted =
		MakePermissiveFrontierSettings();
	Budgeted.MaxCandidateCount = 1;
	FABTSM73DAGFailureFrontierAnalysis BudgetedAnalysis;
	Error.Reset();
	TestFalse(TEXT("Candidate budget rejects instead of truncating"),
		Analyzer.Analyze(Budgeted, Profiles, Chain, BudgetedAnalysis, Error));
	TestTrue(TEXT("Candidate budget reject reason is explicit"),
		Error.StartsWith(TEXT("DAG3CandidateBudgetExceeded")));

	FABTSM73DAGFailureFrontierSettings InvalidHardCap =
		MakePermissiveFrontierSettings();
	InvalidHardCap.MaxCutSetSize = 5;
	FABTSM73DAGFailureFrontierAnalysis InvalidHardCapAnalysis;
	Error.Reset();
	TestFalse(TEXT("Programmatic settings cannot exceed the reflected hard cap"),
		Analyzer.Analyze(
			InvalidHardCap,
			Profiles,
			Chain,
			InvalidHardCapAnalysis,
			Error));
	TestEqual(TEXT("Invalid hard cap reports a settings reject"),
		Error, FString(TEXT("DAG3SettingsInvalid")));

	FABTSM73DAGFailureFrontierSettings Enabled =
		MakePermissiveFrontierSettings();
	FABTSM73DAGFailureFrontierAnalysis MissingProfileAnalysis;
	Error.Reset();
	TestFalse(TEXT("Enabled analysis fails closed without a real material profile"),
		Analyzer.Analyze(
			Enabled,
			TConstArrayView<FABTSM7MaterialProfile>(),
			Chain,
			MissingProfileAnalysis,
			Error));
	TestTrue(TEXT("Missing material profile reject is explicit"),
		Error.StartsWith(TEXT("DAG3MaterialProfileMissing")));

	FABTSM73StructureData WithOrphan = Chain;
	AddFrontierTestNode(
		WithOrphan,
		INDEX_NONE,
		FVector(300.0f, 0.0f, 60.0f),
		FVector(20.0f, 20.0f, 20.0f),
		EABTSM73BrickSemanticRole::Connector,
		false);
	FABTSM73DAGFailureFrontierAnalysis OrphanAnalysis;
	Error.Reset();
	TestFalse(TEXT("Enabled analysis rejects a pre-existing unsupported island"),
		Analyzer.Analyze(
			Enabled,
			Profiles,
			WithOrphan,
			OrphanAnalysis,
			Error));
	TestTrue(TEXT("Unsupported island reports a baseline Ground-path reject"),
		Error.StartsWith(TEXT("DAG3BaselineNoGroundPath")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3ProductionPresetDiscoveryTest,
	"ABTS.M73DAG3.ProductionPresetDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3ProductionPresetDiscoveryTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		EABTSM3TaskType TaskType;
		EABTSM7BuildingMaterial Material;
		int32 Seed;
		int32 ExpectedBrickCount;
		uint32 ExpectedTopologyHash;
	};
	const FCase Cases[] = {
		{EABTSM3TaskType::Workshop, EABTSM7BuildingMaterial::Wood,
			1034266606, 13, 2796521057u},
		{EABTSM3TaskType::TargetBuilding, EABTSM7BuildingMaterial::Stone,
			1034264727, 17, 1424001057u},
		{EABTSM3TaskType::FurnaceRuins, EABTSM7BuildingMaterial::Iron,
			1034267999, 13, 2796521057u}
	};

	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGBuildingPipeline Pipeline;
	for (const FCase& TestCase : Cases)
	{
		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				TestCase.TaskType,
				TestCase.Material);
		TestFalse(TEXT("DAG3-A remains disabled in production profiles"),
			Profile.DAGFailureFrontierSettings.bEnableAnalysis);
		Profile.GenerationSettings.BuildingSeed = TestCase.Seed;
		Profile.DAGGenerationSettings.BuildingSeed = TestCase.Seed;
		FABTSM73StructureData Data;
		FString Error;
		TestTrue(
			FString::Printf(TEXT("Production profile builds before analysis: %s"), *Error),
			Pipeline.Build(
				Profile.DAGGenerationSettings,
				Profile.DAGLayoutSettings,
				Profile.GenerationSettings,
				Data,
				Error));
		TestEqual(TEXT("DAG3-A does not mutate production brick count"),
			Data.Bricks.Num(), TestCase.ExpectedBrickCount);
		TestEqual(TEXT("DAG3-A does not replace the DAG2.3 topology identity"),
			Data.DAGTopologyHash, TestCase.ExpectedTopologyHash);

		FABTSM73DAGFailureFrontierSettings AnalysisSettings;
		AnalysisSettings.bEnableAnalysis = true;
		const uint32 TopologyHashBeforeAnalysis = Data.DAGTopologyHash;
		FABTSM73DAGFailureFrontierAnalysis Analysis;
		Error.Reset();
		TestTrue(
			FString::Printf(TEXT("Production preset has a static internal frontier: %s"), *Error),
			Analyzer.Analyze(AnalysisSettings, Profiles, Data, Analysis, Error));
		if (!Analysis.bAccepted) continue;
		TestTrue(TEXT("Selected frontier uses a non-zero identity"),
			Analysis.SelectedFrontierHash != 0);
		TestEqual(TEXT("Read-only analysis preserves the topology identity"),
			Data.DAGTopologyHash, TopologyHashBeforeAnalysis);
		TestTrue(TEXT("Selected candidate index is valid"),
			Analysis.Candidates.IsValidIndex(Analysis.SelectedCandidateIndex));
		if (!Analysis.Candidates.IsValidIndex(Analysis.SelectedCandidateIndex)) continue;
		const FABTSM73DAGFailureFrontierCandidate& Selected =
			Analysis.Candidates[Analysis.SelectedCandidateIndex];
		TestTrue(TEXT("Selected production frontier satisfies the lower height gate"),
			Selected.NormalizedHeight >= AnalysisSettings.MinNormalizedHeight);
		TestTrue(TEXT("Selected production frontier satisfies the upper height gate"),
			Selected.NormalizedHeight <= AnalysisSettings.MaxNormalizedHeight);
		TestTrue(TEXT("Selected production frontier satisfies the lower mass gate"),
			Selected.MainBodyAffectedMassRatio
			>= AnalysisSettings.MinMainBodyAffectedMassRatio);
		TestTrue(TEXT("Selected production frontier satisfies the upper mass gate"),
			Selected.MainBodyAffectedMassRatio
			<= AnalysisSettings.MaxMainBodyAffectedMassRatio);
		TestTrue(TEXT("Selected production frontier satisfies the body-span gate"),
			Selected.AffectedHeightSpanNormalized
			>= AnalysisSettings.MinAffectedHeightSpanNormalized);
		TestTrue(TEXT("Selected production frontier affects multiple Macro nodes"),
			Selected.AffectedMacroNodeIds.Num()
			>= AnalysisSettings.MinAffectedMacroNodeCount);
		TestEqual(TEXT("Selected production frontier has no support bypass"),
			Selected.BypassSupportEdgeCount, 0);

		FABTSM73DAGFailureFrontierAnalysis RepeatedAnalysis;
		Error.Reset();
		TestTrue(TEXT("Repeated production frontier analysis succeeds"),
			Analyzer.Analyze(
				AnalysisSettings,
				Profiles,
				Data,
				RepeatedAnalysis,
				Error));
		TestEqual(TEXT("Production frontier hash repeats exactly"),
			RepeatedAnalysis.SelectedFrontierHash,
			Analysis.SelectedFrontierHash);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3RewritePatternGeometryMatrixTest,
	"ABTS.M73DAG3.Rewrite.PatternGeometryMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3RewritePatternGeometryMatrixTest::RunTest(
	const FString& Parameters)
{
	FABTSM7TaskGraphBuildingProfile BaselineProfile =
		MakeRewriteTestProfile();
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData BaselineData;
	FString Error;
	if (!Pipeline.Build(
		BaselineProfile.DAGGenerationSettings,
		BaselineProfile.DAGLayoutSettings,
		BaselineProfile.GenerationSettings,
		BaselineData,
		Error))
	{
		AddError(FString::Printf(
			TEXT("DAG3-B matrix baseline failed: %s"),
			*Error));
		return false;
	}

	TArray<FABTSM73StructureData> RealizedStructures;
	TSet<uint32> RealizedHashes;
	for (const FRewritePatternCase& TestCase : RewritePatternCases)
	{
		FABTSM73StructureData Data;
		FABTSM7TaskGraphBuildingProfile Profile;
		Error.Reset();
		if (!BuildRewritePattern(
			TestCase.Pattern,
			Data,
			Error,
			&Profile))
		{
			AddError(FString::Printf(
				TEXT("%s rejected production fixture: %s"),
				TestCase.Name,
				*Error));
			continue;
		}
		const FABTSM73DAGFailurePatternResult& Result =
			Data.DAGFailurePatternResult;
		TestTrue(
			FString::Printf(TEXT("%s rewrite is enabled"), TestCase.Name),
			Result.bEnabled);
		TestTrue(
			FString::Printf(TEXT("%s rewrite is applied"), TestCase.Name),
			Result.bApplied);
		TestEqual(
			FString::Printf(TEXT("%s preserves explicit pattern"), TestCase.Name),
			static_cast<int32>(Result.Pattern),
			static_cast<int32>(TestCase.Pattern));
		TestEqual(
			FString::Printf(TEXT("%s realizes expected motion"), TestCase.Name),
			static_cast<int32>(Result.ExpectedMotion),
			static_cast<int32>(TestCase.ExpectedMotion));
		TestTrue(
			FString::Printf(TEXT("%s has a source frontier identity"), TestCase.Name),
			Result.SourceFrontierHash != 0);
		TestTrue(
			FString::Printf(TEXT("%s has a realized geometry identity"), TestCase.Name),
			Result.RealizedPatternHash != 0);
		TestTrue(
			FString::Printf(
				TEXT("%s separates source and realized hash domains"),
				TestCase.Name),
			Result.SourceFrontierHash != Result.RealizedPatternHash);
		TestEqual(
			FString::Printf(
				TEXT("%s preserves DAG topology identity"),
				TestCase.Name),
			Data.DAGTopologyHash,
			BaselineData.DAGTopologyHash);
		TestEqual(
			FString::Printf(TEXT("%s removes one authored column"), TestCase.Name),
			Result.RemovedColumnCount,
			1);
		TestEqual(
			FString::Printf(TEXT("%s has one weak support"), TestCase.Name),
			Result.WeakNodeIds.Num(),
			1);
		TestEqual(
			FString::Printf(
				TEXT("%s has the expected remaining support count"),
				TestCase.Name),
			Result.RemainingSupportNodeIds.Num(),
			TestCase.ExpectedColumnCount - 1);
		TestTrue(
			FString::Printf(
				TEXT("%s exposes a non-zero failure direction"),
				TestCase.Name),
			!Result.ExpectedFailureDirectionLocal.IsNearlyZero());
		TestTrue(
			FString::Printf(
				TEXT("%s affects authored main-body nodes"),
				TestCase.Name),
			!Result.AffectedMainBodyNodeIds.IsEmpty());

		const FABTSM73DAGPhysicalSupportMapping* Mapping =
			FindRewriteMapping(Data, Result);
		TestNotNull(
			FString::Printf(
				TEXT("%s keeps the rewritten macro interface addressable"),
				TestCase.Name),
			Mapping);
		if (Mapping != nullptr)
		{
			TestEqual(
				FString::Printf(
					TEXT("%s realizes the expected column count"),
					TestCase.Name),
				Mapping->ColumnNodeIds.Num(),
				TestCase.ExpectedColumnCount);
			TestEqual(
				FString::Printf(
					TEXT("%s keeps column roles one-to-one"),
					TestCase.Name),
				Mapping->ColumnRoles.Num(),
				Mapping->ColumnNodeIds.Num());
			TestTrue(
				FString::Printf(
					TEXT("%s materializes its weak column role"),
					TestCase.Name),
				Mapping->ColumnRoles.Contains(TestCase.ExpectedWeakRole));
			const bool bExpectPivot = TestCase.ExpectedColumnCount == 2;
			TestEqual(
				FString::Printf(
					TEXT("%s materializes only the required pivot"),
					TestCase.Name),
				Mapping->ColumnRoles.Contains(
					EABTSM73DAGRealizedColumnRole::FailureStrongPivot),
				bExpectPivot);
		}
		if (Result.WeakNodeIds.Num() == 1)
		{
			const FABTSM73BrickNode* WeakNode =
				FindRewriteNode(Data, Result.WeakNodeIds[0]);
			TestNotNull(
				FString::Printf(TEXT("%s weak node exists"), TestCase.Name),
				WeakNode);
			if (WeakNode != nullptr)
			{
				TestEqual(
					FString::Printf(
						TEXT("%s weak role lowers to WeakSupport geometry"),
						TestCase.Name),
					static_cast<int32>(WeakNode->SemanticRole),
					static_cast<int32>(
						EABTSM73BrickSemanticRole::WeakSupport));
			}
		}
		for (const int32 PivotNodeId : Result.RemainingSupportNodeIds)
		{
			const FABTSM73BrickNode* PivotNode =
				FindRewriteNode(Data, PivotNodeId);
			TestNotNull(
				FString::Printf(TEXT("%s pivot node exists"), TestCase.Name),
				PivotNode);
			if (PivotNode != nullptr)
			{
				TestEqual(
					FString::Printf(
						TEXT("%s pivot remains ordinary Column geometry"),
						TestCase.Name),
					static_cast<int32>(PivotNode->SemanticRole),
					static_cast<int32>(EABTSM73BrickSemanticRole::Column));
			}
		}
		if (TestCase.Pattern
			== EABTSM73DAGFailurePattern::InternalOffsetSeam)
		{
			TestTrue(
				TEXT("Offset seam realizes the configured minimum shift"),
				Result.OffsetSeamShiftCM + KINDA_SMALL_NUMBER
					>= Profile.DAGFailurePatternSettings.MinOffsetSeamShiftCM);
		}
		else
		{
			TestTrue(
				FString::Printf(
					TEXT("%s does not report an offset-seam shift"),
					TestCase.Name),
				FMath::IsNearlyZero(Result.OffsetSeamShiftCM));
		}
		TestFalse(
			FString::Printf(
				TEXT("%s changes baseline physical geometry"),
				TestCase.Name),
			EqualRewriteGeometry(BaselineData, Data));
		RealizedHashes.Add(Result.RealizedPatternHash);
		RealizedStructures.Add(MoveTemp(Data));
	}

	TestEqual(
		TEXT("All explicit patterns have distinct realized identities"),
		RealizedHashes.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(RewritePatternCases)));
	if (RealizedStructures.Num() == UE_ARRAY_COUNT(RewritePatternCases))
	{
		for (int32 Left = 0; Left < RealizedStructures.Num(); ++Left)
		{
			for (int32 Right = Left + 1;
				Right < RealizedStructures.Num();
				++Right)
			{
				TestFalse(
					TEXT("Explicit patterns realize pairwise-distinct geometry"),
					EqualRewriteGeometry(
						RealizedStructures[Left],
						RealizedStructures[Right]));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3RewriteRealizedContactAndIntactStabilityTest,
	"ABTS.M73DAG3.Rewrite.RealizedContactAndIntactStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3RewriteRealizedContactAndIntactStabilityTest::RunTest(
	const FString& Parameters)
{
	FABTSM73StabilityValidator StabilityValidator;
	for (const FRewritePatternCase& TestCase : RewritePatternCases)
	{
		FABTSM73StructureData Data;
		FABTSM7TaskGraphBuildingProfile Profile;
		FString Error;
		if (!BuildRewritePattern(
			TestCase.Pattern,
			Data,
			Error,
			&Profile))
		{
			AddError(FString::Printf(
				TEXT("%s contact/stability fixture rejected: %s"),
				TestCase.Name,
				*Error));
			continue;
		}
		TestEqual(
			FString::Printf(
				TEXT("%s has every required realized contact"),
				TestCase.Name),
			Data.DAGMissingRequiredContactCount,
			0);
		TestEqual(
			FString::Printf(
				TEXT("%s has no unexpected support bypass"),
				TestCase.Name),
			Data.DAGUnexpectedBypassCount,
			0);
		TestTrue(
			FString::Printf(TEXT("%s has ground nodes"), TestCase.Name),
			!Data.GroundNodeIds.IsEmpty());
		TestTrue(
			FString::Printf(TEXT("%s has support contacts"), TestCase.Name),
			!Data.SupportEdges.IsEmpty());
		TestEqual(
			FString::Printf(
				TEXT("%s retains the production contact-area gate"),
				TestCase.Name),
			Data.DAGMinSupportContactAreaRatio,
			Profile.DAGLayoutSettings.MinSupportContactAreaRatio);
		Error.Reset();
		TestTrue(
			FString::Printf(
				TEXT("%s remains intact-stable: %s"),
				TestCase.Name,
				*Error),
			StabilityValidator.Validate(
				Profile.GenerationSettings,
				Data,
				Error));

		for (int32 NodeIndex = 0;
			NodeIndex < Data.Bricks.Num();
			++NodeIndex)
		{
			const FABTSM73BrickNode& Node = Data.Bricks[NodeIndex];
			TestEqual(
				FString::Printf(
					TEXT("%s keeps NodeId equal to storage index"),
					TestCase.Name),
				Node.NodeId,
				NodeIndex);
			TestEqual(
				FString::Printf(
					TEXT("%s keeps one authored material"),
					TestCase.Name),
				static_cast<int32>(Node.Material),
				static_cast<int32>(
					Profile.GenerationSettings.PrimaryMaterial));
			TestEqual(
				FString::Printf(
					TEXT("%s does not route a weak material"),
					TestCase.Name),
				static_cast<int32>(Node.OriginalMaterial),
				static_cast<int32>(Node.Material));
			TestFalse(
				FString::Printf(
					TEXT("%s does not create legacy weak-point flags"),
					TestCase.Name),
				Node.bWeakPoint);
		}
		TestTrue(
			FString::Printf(
				TEXT("%s creates no legacy WeakPoints"),
				TestCase.Name),
			Data.WeakPoints.IsEmpty());
		TestTrue(
			FString::Printf(
				TEXT("%s creates no legacy weakness intents"),
				TestCase.Name),
			Data.StructuralWeaknessIntents.IsEmpty());
		TestTrue(
			FString::Printf(
				TEXT("%s creates no legacy failure probes"),
				TestCase.Name),
			Data.FailureProbeResults.IsEmpty());

		for (const FABTSM73DAGPhysicalSupportMapping& Mapping
			: Data.DAGPhysicalSupportMappings)
		{
			TestEqual(
				FString::Printf(
					TEXT("%s mapping keeps role cardinality"),
					TestCase.Name),
				Mapping.ColumnRoles.Num(),
				Mapping.ColumnNodeIds.Num());
			for (const int32 ColumnNodeId : Mapping.ColumnNodeIds)
			{
				TestNotNull(
					FString::Printf(
						TEXT("%s mapping column exists"),
						TestCase.Name),
					FindRewriteNode(Data, ColumnNodeId));
				TestTrue(
					FString::Printf(
						TEXT("%s realizes support-plate to column contact"),
						TestCase.Name),
					HasRewriteSupportEdge(
						Data,
						Mapping.SupportPlateNodeId,
						ColumnNodeId));
				TestTrue(
					FString::Printf(
						TEXT("%s realizes column to load-plate contact"),
						TestCase.Name),
					HasRewriteSupportEdge(
						Data,
						ColumnNodeId,
						Mapping.LoadPlateNodeId));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3RewriteCounterfactualSemanticsTest,
	"ABTS.M73DAG3.Rewrite.CounterfactualSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3RewriteCounterfactualSemanticsTest::RunTest(
	const FString& Parameters)
{
	for (const FRewritePatternCase& TestCase : RewritePatternCases)
	{
		FABTSM73StructureData Data;
		FABTSM7TaskGraphBuildingProfile Profile;
		FString Error;
		if (!BuildRewritePattern(
			TestCase.Pattern,
			Data,
			Error,
			&Profile))
		{
			AddError(FString::Printf(
				TEXT("%s counterfactual fixture rejected: %s"),
				TestCase.Name,
				*Error));
			continue;
		}
		const FABTSM73DAGFailurePatternResult& Result =
			Data.DAGFailurePatternResult;
		TestEqual(
			FString::Printf(
				TEXT("%s realizes a complete physical frontier"),
				TestCase.Name),
			Result.BypassSupportEdgeCount,
			0);
		TestTrue(
			FString::Printf(
				TEXT("%s satisfies the intact support margin"),
				TestCase.Name),
			Result.InitialSupportMarginCM + KINDA_SMALL_NUMBER
				>= Profile.DifficultySettings.MinInitialSupportMarginCM);
		TestTrue(
			FString::Printf(
				TEXT("%s reports a finite reseat risk"),
				TestCase.Name),
			FMath::IsFinite(Result.ReseatRisk)
				&& Result.ReseatRisk >= 0.0f
				&& Result.ReseatRisk <= 1.0f);
		TestTrue(
			FString::Printf(
				TEXT("%s reports an affected main body"),
				TestCase.Name),
			!Result.AffectedMainBodyNodeIds.IsEmpty());

		TArray<int32> FullRemoved = Result.WeakNodeIds;
		FullRemoved.Append(Result.RemainingSupportNodeIds);
		TestFalse(
			FString::Printf(
				TEXT("%s full interface removal cuts the load from Ground"),
				TestCase.Name),
			HasRewriteGroundPathWithout(
				Data,
				Result.LoadPlateNodeId,
				FullRemoved));

		if (TestCase.Pattern
			== EABTSM73DAGFailurePattern::InternalSingleSupport)
		{
			TestTrue(
				TEXT("Single support has no retained pivot"),
				Result.RemainingSupportNodeIds.IsEmpty());
			TestFalse(
				TEXT("Removing the single weak support disconnects the load"),
				HasRewriteGroundPathWithout(
					Data,
					Result.LoadPlateNodeId,
					Result.WeakNodeIds));
			TestTrue(
				TEXT("Drop pattern has no post-failure tip margin"),
				FMath::IsNearlyZero(Result.PostFailureTipMarginCM));
		}
		else
		{
			TestEqual(
				FString::Printf(
					TEXT("%s retains exactly one strong pivot"),
					TestCase.Name),
				Result.RemainingSupportNodeIds.Num(),
				1);
			TestTrue(
				FString::Printf(
					TEXT("%s retains a Ground path after weak-only removal"),
					TestCase.Name),
				HasRewriteGroundPathWithout(
					Data,
					Result.LoadPlateNodeId,
					Result.WeakNodeIds));
			TestTrue(
				FString::Printf(
					TEXT("%s exceeds the post-failure tip margin"),
					TestCase.Name),
				Result.PostFailureTipMarginCM + KINDA_SMALL_NUMBER
					>= Profile.DifficultySettings.MinTipMarginCM);
			TestTrue(
				FString::Printf(
					TEXT("%s stays below the reseat-risk gate"),
					TestCase.Name),
				Result.ReseatRisk
					<= Profile.DifficultySettings.MaxReseatRisk);
		}

		TestEqual(
			FString::Printf(
				TEXT("%s reports its required motion"),
				TestCase.Name),
			static_cast<int32>(Result.ExpectedMotion),
			static_cast<int32>(TestCase.ExpectedMotion));
		if (TestCase.Pattern
			== EABTSM73DAGFailurePattern::InternalOffsetSeam)
		{
			TestTrue(
				TEXT("Offset seam realizes a physical closure shift"),
				Result.OffsetSeamShiftCM + KINDA_SMALL_NUMBER
					>= Profile.DAGFailurePatternSettings.MinOffsetSeamShiftCM);
		}
		else
		{
			TestTrue(
				FString::Printf(
					TEXT("%s has no seam shift"),
					TestCase.Name),
				FMath::IsNearlyZero(Result.OffsetSeamShiftCM));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3RewriteDeterminismAndIdentityTest,
	"ABTS.M73DAG3.Rewrite.DeterminismAndIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3RewriteDeterminismAndIdentityTest::RunTest(
	const FString& Parameters)
{
	FABTSM7TaskGraphBuildingProfile BaselineProfile =
		MakeRewriteTestProfile();
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData BaselineData;
	FString Error;
	if (!Pipeline.Build(
		BaselineProfile.DAGGenerationSettings,
		BaselineProfile.DAGLayoutSettings,
		BaselineProfile.GenerationSettings,
		BaselineData,
		Error))
	{
		AddError(FString::Printf(
			TEXT("DAG3-B determinism baseline failed: %s"),
			*Error));
		return false;
	}

	TSet<uint32> RealizedHashes;
	for (const FRewritePatternCase& TestCase : RewritePatternCases)
	{
		FABTSM73StructureData First;
		FABTSM73StructureData Second;
		FString FirstError;
		FString SecondError;
		const bool bFirstBuilt = BuildRewritePattern(
			TestCase.Pattern,
			First,
			FirstError);
		const bool bSecondBuilt = BuildRewritePattern(
			TestCase.Pattern,
			Second,
			SecondError);
		if (!bFirstBuilt || !bSecondBuilt)
		{
			AddError(FString::Printf(
				TEXT("%s deterministic replay rejected: first=%s second=%s"),
				TestCase.Name,
				*FirstError,
				*SecondError));
			continue;
		}
		TestTrue(
			FString::Printf(
				TEXT("%s repeats exact physical geometry"),
				TestCase.Name),
			EqualRewriteGeometry(First, Second));
		TestTrue(
			FString::Printf(
				TEXT("%s repeats exact result identity and metrics"),
				TestCase.Name),
			EqualRewritePatternResult(
				First.DAGFailurePatternResult,
				Second.DAGFailurePatternResult));
		TestTrue(
			FString::Printf(
				TEXT("%s repeats exact realized frontier analysis"),
				TestCase.Name),
			EqualRewriteAnalysis(
				First.DAGFailureFrontierAnalysis,
				Second.DAGFailureFrontierAnalysis));
		TestEqual(
			FString::Printf(
				TEXT("%s leaves the DAG identity unchanged"),
				TestCase.Name),
			First.DAGTopologyHash,
			BaselineData.DAGTopologyHash);
		TestTrue(
			FString::Printf(
				TEXT("%s keeps source and realized identities separate"),
				TestCase.Name),
			First.DAGFailurePatternResult.SourceFrontierHash != 0
				&& First.DAGFailurePatternResult.RealizedPatternHash != 0
				&& First.DAGFailurePatternResult.SourceFrontierHash
					!= First.DAGFailurePatternResult.RealizedPatternHash);
		for (int32 NodeIndex = 0;
			NodeIndex < First.Bricks.Num();
			++NodeIndex)
		{
			TestEqual(
				FString::Printf(
					TEXT("%s keeps deterministic contiguous NodeIds"),
					TestCase.Name),
				First.Bricks[NodeIndex].NodeId,
				NodeIndex);
		}
		RealizedHashes.Add(
			First.DAGFailurePatternResult.RealizedPatternHash);
	}
	TestEqual(
		TEXT("The three pattern identities do not alias"),
		RealizedHashes.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(RewritePatternCases)));

	FABTSM73StructureData FirstAuto;
	FABTSM73StructureData SecondAuto;
	FString FirstAutoError;
	FString SecondAutoError;
	const bool bFirstAutoBuilt = BuildRewritePattern(
		EABTSM73DAGFailurePattern::Auto,
		FirstAuto,
		FirstAutoError);
	const bool bSecondAutoBuilt = BuildRewritePattern(
		EABTSM73DAGFailurePattern::Auto,
		SecondAuto,
		SecondAutoError);
	if (!bFirstAutoBuilt || !bSecondAutoBuilt)
	{
		AddError(FString::Printf(
			TEXT("Auto deterministic replay rejected: first=%s second=%s"),
			*FirstAutoError,
			*SecondAutoError));
	}
	else
	{
		TestTrue(
			TEXT("Auto resolves and applies one concrete pattern"),
			FirstAuto.DAGFailurePatternResult.bApplied
				&& FirstAuto.DAGFailurePatternResult.Pattern
					!= EABTSM73DAGFailurePattern::Auto);
		TestTrue(
			TEXT("Auto repeats exact physical geometry"),
			EqualRewriteGeometry(FirstAuto, SecondAuto));
		TestTrue(
			TEXT("Auto repeats exact result identity and metrics"),
			EqualRewritePatternResult(
				FirstAuto.DAGFailurePatternResult,
				SecondAuto.DAGFailurePatternResult));
		TestTrue(
			TEXT("Auto repeats exact realized frontier analysis"),
			EqualRewriteAnalysis(
				FirstAuto.DAGFailureFrontierAnalysis,
				SecondAuto.DAGFailureFrontierAnalysis));
		TestTrue(
			TEXT("Auto resolves to one of the certified explicit identities"),
			RealizedHashes.Contains(
				FirstAuto.DAGFailurePatternResult.RealizedPatternHash));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3RewriteBudgetDisabledAndAtomicFailureTest,
	"ABTS.M73DAG3.Rewrite.BudgetDisabledAndAtomicFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3RewriteBudgetDisabledAndAtomicFailureTest::RunTest(
	const FString& Parameters)
{
	FABTSM7TaskGraphBuildingProfile Profile = MakeRewriteTestProfile();
	FABTSM73DAGBuildingPipeline Pipeline;
	FABTSM73StructureData BaselineData;
	FString Error;
	if (!Pipeline.Build(
		Profile.DAGGenerationSettings,
		Profile.DAGLayoutSettings,
		Profile.GenerationSettings,
		BaselineData,
		Error))
	{
		AddError(FString::Printf(
			TEXT("DAG3-B atomicity baseline failed: %s"),
			*Error));
		return false;
	}
	TestFalse(
		TEXT("Production profile keeps DAG3-A disabled"),
		Profile.DAGFailureFrontierSettings.bEnableAnalysis);
	TestFalse(
		TEXT("Production profile keeps DAG3-B disabled"),
		Profile.DAGFailurePatternSettings.bEnableGeometryRewrite);

	FABTSM73StructureData DisabledData;
	Error.Reset();
	TestTrue(
		FString::Printf(
			TEXT("Disabled DAG3-B takes the baseline path: %s"),
			*Error),
		BuildRewriteProfile(Profile, DisabledData, Error));
	TestTrue(
		TEXT("Disabled DAG3-B is an exact physical no-op"),
		EqualRewriteGeometry(BaselineData, DisabledData));
	TestFalse(
		TEXT("Disabled DAG3-B does not mark a transaction enabled"),
		DisabledData.DAGFailurePatternResult.bEnabled);
	TestFalse(
		TEXT("Disabled DAG3-B does not mark a transaction applied"),
		DisabledData.DAGFailurePatternResult.bApplied);
	TestEqual(
		TEXT("Disabled DAG3-B has no realized identity"),
		DisabledData.DAGFailurePatternResult.RealizedPatternHash,
		0u);

	FABTSM7TaskGraphBuildingProfile MissingFrontierProfile = Profile;
	MissingFrontierProfile.DAGFailurePatternSettings.bEnableGeometryRewrite =
		true;
	MissingFrontierProfile.DAGFailurePatternSettings.Pattern =
		EABTSM73DAGFailurePattern::InternalSingleSupport;
	FABTSM73StructureData MissingFrontierData;
	Error.Reset();
	TestFalse(
		TEXT("Enabled rewrite rejects a disabled frontier"),
		BuildRewriteProfile(
			MissingFrontierProfile,
			MissingFrontierData,
			Error));
	TestEqual(
		TEXT("Disabled-frontier rejection is exact"),
		Error,
		FString(TEXT("DAG3BRequiresAcceptedFrontier")));
	TestTrue(
		TEXT("Disabled-frontier rejection preserves baseline geometry"),
		EqualRewriteGeometry(BaselineData, MissingFrontierData));
	TestTrue(
		TEXT("Disabled-frontier rejection records enabled intent"),
		MissingFrontierData.DAGFailurePatternResult.bEnabled);
	TestFalse(
		TEXT("Disabled-frontier rejection applies nothing"),
		MissingFrontierData.DAGFailurePatternResult.bApplied);

	FABTSM7TaskGraphBuildingProfile InvalidBudgetProfile = Profile;
	InvalidBudgetProfile.DAGFailureFrontierSettings.bEnableAnalysis = true;
	InvalidBudgetProfile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
	InvalidBudgetProfile.DAGFailurePatternSettings.Pattern =
		EABTSM73DAGFailurePattern::InternalSingleSupport;
	InvalidBudgetProfile.DAGFailurePatternSettings.MaxRewriteAttemptCount = 0;
	FABTSM73StructureData InvalidBudgetData;
	Error.Reset();
	TestFalse(
		TEXT("Zero rewrite budget is rejected"),
		BuildRewriteProfile(
			InvalidBudgetProfile,
			InvalidBudgetData,
			Error));
	TestEqual(
		TEXT("Zero rewrite budget rejection is exact"),
		Error,
		FString(TEXT("DAG3BRewriteAttemptBudgetInvalid")));
	TestTrue(
		TEXT("Zero rewrite budget preserves baseline geometry"),
		EqualRewriteGeometry(BaselineData, InvalidBudgetData));
	TestFalse(
		TEXT("Zero rewrite budget never applies a pattern"),
		InvalidBudgetData.DAGFailurePatternResult.bApplied);
	TestEqual(
		TEXT("Zero rewrite budget has no realized identity"),
		InvalidBudgetData.DAGFailurePatternResult.RealizedPatternHash,
		0u);

	FABTSM7TaskGraphBuildingProfile ExhaustedBudgetProfile = Profile;
	ExhaustedBudgetProfile.DAGFailureFrontierSettings.bEnableAnalysis = true;
	ExhaustedBudgetProfile.DAGFailurePatternSettings.bEnableGeometryRewrite =
		true;
	ExhaustedBudgetProfile.DAGFailurePatternSettings.Pattern =
		EABTSM73DAGFailurePattern::Auto;
	ExhaustedBudgetProfile.DAGFailurePatternSettings.MaxRewriteAttemptCount = 1;
	ExhaustedBudgetProfile.DifficultySettings.MinInitialSupportMarginCM =
		1000000.0f;
	FABTSM73StructureData ExhaustedBudgetData;
	Error.Reset();
	TestFalse(
		TEXT("One-attempt budget rejects an intentionally impossible window"),
		BuildRewriteProfile(
			ExhaustedBudgetProfile,
			ExhaustedBudgetData,
			Error));
	TestEqual(
		TEXT("Attempt exhaustion reports its exact bound"),
		Error,
		FString(
			TEXT("DAG3BNoAcceptedPattern:"
				"DAG3BRewriteAttemptBudgetExceeded:1:1")));
	TestTrue(
		TEXT("Attempt exhaustion preserves baseline geometry atomically"),
		EqualRewriteGeometry(BaselineData, ExhaustedBudgetData));
	TestTrue(
		TEXT("Attempt exhaustion records enabled intent"),
		ExhaustedBudgetData.DAGFailurePatternResult.bEnabled);
	TestFalse(
		TEXT("Attempt exhaustion commits no rewrite"),
		ExhaustedBudgetData.DAGFailurePatternResult.bApplied);
	TestEqual(
		TEXT("Attempt exhaustion records the consumed attempt"),
		ExhaustedBudgetData.DAGFailurePatternResult.RewriteAttemptCount,
		1);
	TestEqual(
		TEXT("Attempt exhaustion has no realized identity"),
		ExhaustedBudgetData.DAGFailurePatternResult.RealizedPatternHash,
		0u);
	return true;
}

#endif
