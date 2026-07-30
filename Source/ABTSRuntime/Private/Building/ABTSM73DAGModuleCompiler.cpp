// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGModuleCompiler.h"

#include "Building/ABTSM73DAGContactGraphBuilder.h"
#include "Building/ABTSM73DAG5BSemanticEnvelope.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/Crc.h"

namespace
{
	const FABTSM73DAGMacroLayout* FindMacroLayoutForCompilation(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate([MacroNodeId](const FABTSM73DAGMacroLayout& Candidate)
		{
			return Candidate.MacroNodeId == MacroNodeId;
		});
	}

	int32 CountSupportEdges(const TArray<FABTSM73DAGSelectedSupport>& Supports, const int32 MacroNodeId)
	{
		int32 Count = 0;
		for (const FABTSM73DAGSelectedSupport& Edge : Supports)
		{
			if (Edge.SupportMacroNodeId == MacroNodeId || Edge.LoadMacroNodeId == MacroNodeId) ++Count;
		}
		return Count;
	}

	uint32 SemanticCellIdentityHash(
		const FIntVector& Coordinate,
		const EABTSM73DAG5BSemanticCell Semantic)
	{
		uint32 Hash = HashCombineFast(
			0x5b61u,
			static_cast<uint32>(Coordinate.X + 0x100));
		Hash = HashCombineFast(
			Hash,
			static_cast<uint32>(Coordinate.Y + 0x200));
		Hash = HashCombineFast(
			Hash,
			static_cast<uint32>(Coordinate.Z + 0x400));
		return HashCombineFast(
			Hash,
			static_cast<uint32>(Semantic));
	}

	bool BoxesOverlapWithVolume(const FBox& A, const FBox& B)
	{
		if (!A.IsValid || !B.IsValid) return false;
		const FVector Min(
			FMath::Max(A.Min.X, B.Min.X),
			FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Max(A.Min.Z, B.Min.Z));
		const FVector Max(
			FMath::Min(A.Max.X, B.Max.X),
			FMath::Min(A.Max.Y, B.Max.Y),
			FMath::Min(A.Max.Z, B.Max.Z));
		return Max.X > Min.X + KINDA_SMALL_NUMBER
			&& Max.Y > Min.Y + KINDA_SMALL_NUMBER
			&& Max.Z > Min.Z + KINDA_SMALL_NUMBER;
	}

}

void FABTSM73DAGModuleCompiler::AddBrick(
	FABTSM73StructureData& Data,
	const int32 MacroNodeId,
	const FVector& Center,
	const FVector& Dimensions,
	const EABTSM73BrickSemanticRole Role,
	const int32 StructuralLevel,
	const EABTSM7BuildingMaterial Material)
{
	FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
	Node.NodeId = Data.Bricks.Num() - 1;
	Node.MacroNodeId = MacroNodeId;
	Node.LocalCenter = Center;
	Node.DimensionsCM = Dimensions;
	Node.Material = Material;
	Node.OriginalMaterial = Material;
	Node.SemanticRole = Role;
	Node.StoreyIndex = StructuralLevel;
}

bool FABTSM73DAGModuleCompiler::Compile(
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73DAGSpatialLayout& Layout,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	OutError.Reset();
	if (!Graph.bAccepted || !Layout.bAccepted)
	{
		OutError = TEXT("DAGCompileInputRejected");
		return false;
	}
	TMap<int32, int32> PlateByMacro;
	for (const FABTSM73DAGMacroLayout& MacroLayout : Layout.MacroLayouts)
	{
		const EABTSM73BrickSemanticRole Role = CountSupportEdges(Layout.SelectedSupports, MacroLayout.MacroNodeId) > 1
			? EABTSM73BrickSemanticRole::Carrier : EABTSM73BrickSemanticRole::Deck;
		AddBrick(OutData, MacroLayout.MacroNodeId, MacroLayout.PlateCenter, MacroLayout.PlateDimensionsCM,
			Role, MacroLayout.StructuralLevel, BuildingSettings.PrimaryMaterial);
		PlateByMacro.Add(MacroLayout.MacroNodeId, OutData.Bricks.Last().NodeId);
	}

	for (const FABTSM73DAGSelectedSupport& Support : Layout.SelectedSupports)
	{
		const int32* SupportPlateId = PlateByMacro.Find(Support.SupportMacroNodeId);
		const int32* LoadPlateId = PlateByMacro.Find(Support.LoadMacroNodeId);
		const FABTSM73DAGMacroLayout* SupportLayout = FindMacroLayoutForCompilation(Layout, Support.SupportMacroNodeId);
		const FABTSM73DAGMacroLayout* LoadLayout = FindMacroLayoutForCompilation(Layout, Support.LoadMacroNodeId);
		if (SupportPlateId == nullptr || LoadPlateId == nullptr || SupportLayout == nullptr || LoadLayout == nullptr)
		{
			OutError = TEXT("DAGCompilePlateMappingMissing");
			return false;
		}
		const float BottomZ = SupportLayout->PlateCenter.Z + SupportLayout->PlateDimensionsCM.Z * 0.5f;
		const float TopZ = LoadLayout->PlateCenter.Z - LoadLayout->PlateDimensionsCM.Z * 0.5f;
		const float Height = TopZ - BottomZ;
		if (Height < LayoutSettings.MinColumnHeightCM)
		{
			OutError = FString::Printf(TEXT("DAGColumnTooShort:%d:%d:%.2f"), Support.SupportMacroNodeId, Support.LoadMacroNodeId, Height);
			return false;
		}
		const float RealizedColumnWidthCM = Support.RealizedColumnWidthCM > 0.0f
			? Support.RealizedColumnWidthCM : LayoutSettings.ColumnWidthCM;
		if (Support.RealizedColumnCenters.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("DAGColumnCentersMissing:%d:%d"),
				Support.SupportMacroNodeId,
				Support.LoadMacroNodeId);
			return false;
		}
		if (!Support.RealizedColumnRoles.IsEmpty()
			&& Support.RealizedColumnRoles.Num() != Support.RealizedColumnCenters.Num())
		{
			OutError = FString::Printf(
				TEXT("DAGColumnRoleCountMismatch:%d:%d"),
				Support.SupportMacroNodeId,
				Support.LoadMacroNodeId);
			return false;
		}
		FABTSM73DAGPhysicalSupportMapping& Mapping = OutData.DAGPhysicalSupportMappings.AddDefaulted_GetRef();
		Mapping.SupportMacroNodeId = Support.SupportMacroNodeId;
		Mapping.LoadMacroNodeId = Support.LoadMacroNodeId;
		Mapping.SupportPlateNodeId = *SupportPlateId;
		Mapping.LoadPlateNodeId = *LoadPlateId;
		Mapping.SupportPattern = Support.SupportPattern;
		Mapping.RealizedColumnWidthCM = RealizedColumnWidthCM;
		for (int32 CenterIndex = 0;
			CenterIndex < Support.RealizedColumnCenters.Num();
			++CenterIndex)
		{
			const FVector2D& CenterXY = Support.RealizedColumnCenters[CenterIndex];
			const EABTSM73DAGRealizedColumnRole ColumnRole =
				Support.RealizedColumnRoles.IsValidIndex(CenterIndex)
				? Support.RealizedColumnRoles[CenterIndex]
				: EABTSM73DAGRealizedColumnRole::Ordinary;
			const EABTSM73BrickSemanticRole BrickRole =
				ColumnRole == EABTSM73DAGRealizedColumnRole::FailureWeak
					|| ColumnRole == EABTSM73DAGRealizedColumnRole::FailureSeamKey
				? EABTSM73BrickSemanticRole::WeakSupport
				: EABTSM73BrickSemanticRole::Column;
			AddBrick(OutData, INDEX_NONE, FVector(CenterXY.X, CenterXY.Y, (BottomZ + TopZ) * 0.5f),
				FVector(RealizedColumnWidthCM, RealizedColumnWidthCM, Height),
				BrickRole, LoadLayout->StructuralLevel, BuildingSettings.PrimaryMaterial);
			Mapping.ColumnNodeIds.Add(OutData.Bricks.Last().NodeId);
			Mapping.ColumnRoles.Add(ColumnRole);
		}
	}
	OutData.DAGMacroNodeCount = Graph.MacroNodes.Num();
	OutData.DAGSelectedSupportCount = Layout.SelectedSupports.Num();
	OutData.DAGMinSupportContactAreaRatio = LayoutSettings.MinSupportContactAreaRatio;
	OutData.DAGTopologyHash = Graph.CanonicalTopologyHash;
	FABTSM73DAGContactGraphBuilder ContactBuilder;
	if (!ContactBuilder.RebuildAndAudit(LayoutSettings, OutData, OutError)) return false;
	return true;
}

bool FABTSM73DAGModuleCompiler::CompileSemantic(
	const FABTSM73GenerationSettings& BuildingSettings,
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73DAGSpatialLayout& Layout,
	const FABTSM73SemanticEnvelope& Envelope,
	FABTSM73DAG5BResult& InOutResult,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	OutData = FABTSM73StructureData();
	if (!Envelope.bAccepted
		|| !InOutResult.bEnabled
		|| InOutResult.ShapeFamily != Envelope.ShapeFamily
		|| InOutResult.FeatureMask != Envelope.FeatureMask
		|| InOutResult.ShapeHash != Envelope.ShapeHash
		|| InOutResult.WFCHash != Envelope.WFCHash
		|| InOutResult.EnvelopeHash != Envelope.EnvelopeHash)
	{
		OutError = TEXT("DAG5BSemanticIdentityMismatch");
		InOutResult.bAccepted = false;
		InOutResult.RejectReason = OutError;
		return false;
	}
	FABTSM73SemanticEnvelope BoundEnvelope = Envelope;
	FABTSM73DAG5BSemanticEnvelopeBuilder ContractBuilder;
	if (!ContractBuilder.BindPhysicalContract(
		Layout,
		BoundEnvelope,
		OutError))
	{
		InOutResult.bAccepted = false;
		InOutResult.RejectReason = OutError;
		return false;
	}
	FABTSM73StructureData CandidateData;
	if (!Compile(
		BuildingSettings,
		Graph,
		LayoutSettings,
		Layout,
		CandidateData,
		OutError))
	{
		InOutResult.bAccepted = false;
		InOutResult.RejectReason = OutError;
		return false;
	}
	CandidateData.DAG5BSemanticBrickMappings.Reset();
	InOutResult.SemanticRegionMappingCount = 0;
	InOutResult.WFCMappedBrickCount = 0;
	for (const FABTSM73DAGPhysicalSupportMapping& Physical :
		CandidateData.DAGPhysicalSupportMappings)
	{
		const FABTSM73DAG5BSupportPortConstraint* Port =
			BoundEnvelope.SupportPorts.FindByPredicate(
				[&Physical](
					const FABTSM73DAG5BSupportPortConstraint& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== Physical.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== Physical.LoadMacroNodeId;
				});
		if (Port == nullptr || Physical.ColumnNodeIds.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("DAG5BSemanticBrickMappingMissing:%d:%d"),
				Physical.SupportMacroNodeId,
				Physical.LoadMacroNodeId);
			InOutResult.bAccepted = false;
			InOutResult.RejectReason = OutError;
			return false;
		}
		FABTSM73DAG5BSemanticBrickMapping& Mapping =
			CandidateData.DAG5BSemanticBrickMappings
				.AddDefaulted_GetRef();
		Mapping.Kind =
			EABTSM73DAG5BSemanticMappingKind::SupportPort;
		Mapping.SupportMacroNodeId = Physical.SupportMacroNodeId;
		Mapping.LoadMacroNodeId = Physical.LoadMacroNodeId;
		Mapping.SourceSemantic = Port->SourceSemantic;
		Mapping.SourceCellMin = Port->SourceCellMin;
		Mapping.SourceCellMax = Port->SourceCellMax;
		Mapping.SourceCellHash = Port->SourceCellHash;
		Mapping.BrickNodeIds = Physical.ColumnNodeIds;
		FString MappingCanonical = FString::Printf(
			TEXT("K=%d|%d>%d|T=%d|S=%d|C=%d,%d,%d..%d,%d,%d|H=%u"),
			static_cast<int32>(Mapping.Kind),
			Mapping.SupportMacroNodeId,
			Mapping.LoadMacroNodeId,
			Mapping.TargetMacroNodeId,
			static_cast<int32>(Mapping.SourceSemantic),
			Mapping.SourceCellMin.X,
			Mapping.SourceCellMin.Y,
			Mapping.SourceCellMin.Z,
			Mapping.SourceCellMax.X,
			Mapping.SourceCellMax.Y,
			Mapping.SourceCellMax.Z,
			Mapping.SourceCellHash);
		for (const int32 BrickNodeId : Mapping.BrickNodeIds)
		{
			if (!CandidateData.Bricks.IsValidIndex(BrickNodeId))
			{
				OutError = TEXT("DAG5BSemanticBrickMappingNodeInvalid");
				InOutResult.bAccepted = false;
				InOutResult.RejectReason = OutError;
				return false;
			}
			const FABTSM73BrickNode& Brick =
				CandidateData.Bricks[BrickNodeId];
			MappingCanonical += FString::Printf(
				TEXT("|B=%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
				BrickNodeId,
				Brick.LocalCenter.X,
				Brick.LocalCenter.Y,
				Brick.LocalCenter.Z,
				Brick.DimensionsCM.X,
				Brick.DimensionsCM.Y,
				Brick.DimensionsCM.Z);
		}
		Mapping.MappingHash =
			FCrc::StrCrc32(*MappingCanonical);
		++InOutResult.SemanticRegionMappingCount;
		InOutResult.WFCMappedBrickCount +=
			Mapping.BrickNodeIds.Num();
	}

	int32 ShapeMacroMappingCount = 0;
	const int32 RawCellCount =
		BoundEnvelope.GridSize.X
		* BoundEnvelope.GridSize.Y
		* BoundEnvelope.GridSize.Z;
	for (const FABTSM73DAG5BSemanticCellRecord& ContractCell :
		BoundEnvelope.Cells)
	{
		if (ContractCell.Occupancy
				!= EABTSM73DAG5BOccupancy::MustOccupy
			|| ContractCell.RequiredMacroNodeId == INDEX_NONE
			|| ContractCell.Semantic
				== EABTSM73DAG5BSemanticCell::FloorCarrier)
		{
			continue;
		}
		const FABTSM73BrickNode* TargetBrick =
			CandidateData.Bricks.FindByPredicate(
				[&ContractCell](const FABTSM73BrickNode& Brick)
				{
					return Brick.MacroNodeId
						== ContractCell.RequiredMacroNodeId;
				});
		const FABTSM73DAG5BSemanticCellRecord* SourceCell = nullptr;
		float BestDistanceSquared =
			TNumericLimits<float>::Max();
		uint32 BestSourceHash = MAX_uint32;
		if (TargetBrick != nullptr)
		{
			for (int32 CellIndex = 0;
				CellIndex < RawCellCount;
				++CellIndex)
			{
				const FABTSM73DAG5BSemanticCellRecord& Candidate =
					BoundEnvelope.Cells[CellIndex];
				const FBox TargetBounds(
					TargetBrick->LocalCenter
						- TargetBrick->DimensionsCM * 0.5f,
					TargetBrick->LocalCenter
						+ TargetBrick->DimensionsCM * 0.5f);
				if (Candidate.Semantic != ContractCell.Semantic
					|| !Candidate.bHardAnchor
					|| !BoxesOverlapWithVolume(
						Candidate.LocalBounds,
						TargetBounds))
				{
					continue;
				}
				const float DistanceSquared = FVector::DistSquared(
					Candidate.LocalBounds.GetCenter(),
					TargetBrick->LocalCenter);
				const uint32 SourceHash = SemanticCellIdentityHash(
					Candidate.Coordinate,
					Candidate.Semantic);
				if (DistanceSquared
						< BestDistanceSquared - KINDA_SMALL_NUMBER
					|| (FMath::IsNearlyEqual(
							DistanceSquared,
							BestDistanceSquared)
						&& SourceHash < BestSourceHash))
				{
					SourceCell = &Candidate;
					BestDistanceSquared = DistanceSquared;
					BestSourceHash = SourceHash;
				}
			}
		}
		if (TargetBrick == nullptr || SourceCell == nullptr)
		{
			OutError = FString::Printf(
				TEXT("DAG5BShapeMacroMappingMissing:%d:%d"),
				ContractCell.RequiredMacroNodeId,
				static_cast<int32>(ContractCell.Semantic));
			InOutResult.bAccepted = false;
			InOutResult.RejectReason = OutError;
			return false;
		}
		FABTSM73DAG5BSemanticBrickMapping& Mapping =
			CandidateData.DAG5BSemanticBrickMappings
				.AddDefaulted_GetRef();
		Mapping.Kind =
			EABTSM73DAG5BSemanticMappingKind::ShapeMacro;
		Mapping.TargetMacroNodeId =
			ContractCell.RequiredMacroNodeId;
		Mapping.SourceSemantic = SourceCell->Semantic;
		Mapping.SourceCellMin = SourceCell->Coordinate;
		Mapping.SourceCellMax = SourceCell->Coordinate;
		Mapping.SourceCellHash = BestSourceHash;
		Mapping.BrickNodeIds = {TargetBrick->NodeId};
		FString MappingCanonical = FString::Printf(
			TEXT("K=%d|%d>%d|T=%d|S=%d|C=%d,%d,%d..%d,%d,%d|H=%u"),
			static_cast<int32>(Mapping.Kind),
			Mapping.SupportMacroNodeId,
			Mapping.LoadMacroNodeId,
			Mapping.TargetMacroNodeId,
			static_cast<int32>(Mapping.SourceSemantic),
			Mapping.SourceCellMin.X,
			Mapping.SourceCellMin.Y,
			Mapping.SourceCellMin.Z,
			Mapping.SourceCellMax.X,
			Mapping.SourceCellMax.Y,
			Mapping.SourceCellMax.Z,
			Mapping.SourceCellHash);
		MappingCanonical += FString::Printf(
			TEXT("|B=%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
			TargetBrick->NodeId,
			TargetBrick->LocalCenter.X,
			TargetBrick->LocalCenter.Y,
			TargetBrick->LocalCenter.Z,
			TargetBrick->DimensionsCM.X,
			TargetBrick->DimensionsCM.Y,
			TargetBrick->DimensionsCM.Z);
		Mapping.MappingHash =
			FCrc::StrCrc32(*MappingCanonical);
		++ShapeMacroMappingCount;
		++InOutResult.SemanticRegionMappingCount;
		++InOutResult.WFCMappedBrickCount;
	}
	const int32 SupportMappingCount =
		InOutResult.SemanticRegionMappingCount
			- ShapeMacroMappingCount;
	if (SupportMappingCount
			!= CandidateData.DAGPhysicalSupportMappings.Num()
		|| ShapeMacroMappingCount < 1
		|| InOutResult.WFCMappedBrickCount < 2)
	{
		OutError = TEXT("DAG5BSemanticBrickMappingCardinality");
		InOutResult.bAccepted = false;
		InOutResult.RejectReason = OutError;
		return false;
	}
	FABTSM73DAG5BEnvelopeAuditor Auditor;
	if (!Auditor.Audit(
		BoundEnvelope,
		CandidateData,
		InOutResult.Audit,
		OutError))
	{
		InOutResult.bAccepted = false;
		InOutResult.RejectReason = OutError;
		return false;
	}
	InOutResult.bAccepted = true;
	InOutResult.RejectReason.Reset();
	InOutResult.ResultHash = FCrc::StrCrc32(
		*FString::Printf(
			TEXT("Shape=%u|WFC=%u|Envelope=%u|Topology=%u|Audit=%u")
			TEXT("|Operations=%d|Backtracks=%d|Collapsed=%d|WFCVoid=%d")
			TEXT("|Bricks=%d|Regions=%d|Mapped=%d"),
			InOutResult.ShapeHash,
			InOutResult.WFCHash,
			InOutResult.EnvelopeHash,
			Graph.CanonicalTopologyHash,
			InOutResult.Audit.AuditHash,
			InOutResult.PropagationOperationCount,
			InOutResult.BacktrackStepCount,
			InOutResult.CollapsedNonAnchorCellCount,
			InOutResult.WFCDerivedMustVoidCount,
			CandidateData.Bricks.Num(),
			InOutResult.SemanticRegionMappingCount,
			InOutResult.WFCMappedBrickCount));
	CandidateData.DAG5BSemanticEnvelope = BoundEnvelope;
	CandidateData.DAG5BResult = InOutResult;
	OutData = MoveTemp(CandidateData);
	return true;
}
