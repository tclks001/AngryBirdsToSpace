// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGLayoutSolver.h"

#include "Building/ABTSM73DAGLoadSupportSolver.h"
#include "Building/ABTSM73DAG5BSemanticEnvelope.h"
#include "Building/ABTSM73DAG5Types.h"
#include "Building/ABTSM73DAGSupportGeometry.h"
#include "Building/ABTSM73DAGTypes.h"

namespace
{
	FBox2D PlateBoundsXY(const FABTSM73DAGMacroLayout& Layout)
	{
		const FVector2D Half(Layout.PlateDimensionsCM.X * 0.5f, Layout.PlateDimensionsCM.Y * 0.5f);
		return FBox2D(FVector2D(Layout.PlateCenter.X, Layout.PlateCenter.Y) - Half,
			FVector2D(Layout.PlateCenter.X, Layout.PlateCenter.Y) + Half);
	}

	bool IntersectBoxes2D(const FBox2D& A, const FBox2D& B, FBox2D& OutIntersection)
	{
		if (!A.bIsValid || !B.bIsValid) return false;
		const FVector2D Min(FMath::Max(A.Min.X, B.Min.X), FMath::Max(A.Min.Y, B.Min.Y));
		const FVector2D Max(FMath::Min(A.Max.X, B.Max.X), FMath::Min(A.Max.Y, B.Max.Y));
		if (Max.X <= Min.X || Max.Y <= Min.Y) return false;
		OutIntersection = FBox2D(Min, Max);
		return true;
	}

	const FABTSM73DAGMacroLayout* FindLayout(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate([MacroNodeId](const FABTSM73DAGMacroLayout& Candidate)
		{
			return Candidate.MacroNodeId == MacroNodeId;
		});
	}

	bool IsGroundMacro(const FABTSM73DAGGenerationResult& Graph, const int32 MacroNodeId)
	{
		return Graph.GroundNodeIds.Contains(MacroNodeId);
	}

	bool CanFitSupportPattern(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern, const float ColumnWidthCM)
	{
		TArray<FVector2D> Centers;
		return FABTSM73DAGSupportGeometry::MakeColumnCenters(
			Region,
			Settings,
			Pattern,
			ColumnWidthCM,
			Centers);
	}

	bool ResolveSupportPattern(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		EABTSM73DAGSupportPattern& OutPattern, float& OutColumnWidthCM)
	{
		auto TryAdaptivePattern = [&](const EABTSM73DAGSupportPattern Pattern)
		{
			// DAG-2.3 distributes one load plate's required contact area across a
			// whole support group. At candidate stage only retain a physically
			// placeable local interface; the joint solver resolves its final width.
			const float Width = Settings.MinAdaptiveColumnWidthCM;
			if (!CanFitSupportPattern(Region, Settings, Pattern, Width)) return false;
			OutPattern = Pattern;
			OutColumnWidthCM = Width;
			return true;
		};
		if (Settings.bEnableAdaptiveGeometry)
		{
			if (TryAdaptivePattern(Settings.SupportPattern)) return true;
			if (Settings.bAllowAdaptiveColumnCount)
			{
				if (TryAdaptivePattern(EABTSM73DAGSupportPattern::FourColumnFootprint)) return true;
				if (TryAdaptivePattern(EABTSM73DAGSupportPattern::ThreeColumnTripod)) return true;
				if (TryAdaptivePattern(EABTSM73DAGSupportPattern::TwoColumnLine)) return true;
			}
		}
		if (CanFitSupportPattern(Region, Settings, Settings.SupportPattern, Settings.ColumnWidthCM))
		{
			OutPattern = Settings.SupportPattern;
			OutColumnWidthCM = Settings.ColumnWidthCM;
			return true;
		}
		if (Settings.bAllowNarrowSupportFallback
			&& Settings.SupportPattern != EABTSM73DAGSupportPattern::TwoColumnLine
			&& CanFitSupportPattern(Region, Settings, EABTSM73DAGSupportPattern::TwoColumnLine, Settings.ColumnWidthCM))
		{
			OutPattern = EABTSM73DAGSupportPattern::TwoColumnLine;
			OutColumnWidthCM = Settings.ColumnWidthCM;
			return true;
		}
		if (Settings.bAllowNarrowSupportFallback && Settings.bAllowAdaptiveColumnWidth)
		{
			const FVector2D Size = Region.GetSize();
			const float SingleAdaptiveWidth = FMath::Min3(Settings.ColumnWidthCM,
				static_cast<float>(Size.X) - Settings.ColumnClearanceCM * 2.0f,
				static_cast<float>(Size.Y) - Settings.ColumnClearanceCM * 2.0f);
			if (SingleAdaptiveWidth >= Settings.MinAdaptiveColumnWidthCM
				&& CanFitSupportPattern(Region, Settings, EABTSM73DAGSupportPattern::SingleColumnInterface,
					SingleAdaptiveWidth))
			{
				OutPattern = EABTSM73DAGSupportPattern::SingleColumnInterface;
				OutColumnWidthCM = SingleAdaptiveWidth;
				return true;
			}
			const float WidthByShortAxis = FMath::Min(Size.X, Size.Y) - Settings.ColumnClearanceCM * 2.0f;
			const float WidthByLongAxis = (FMath::Max(Size.X, Size.Y) - Settings.ColumnClearanceCM * 3.0f) * 0.5f;
			const float AdaptiveWidth = FMath::Min3(Settings.ColumnWidthCM, WidthByShortAxis, WidthByLongAxis);
			if (AdaptiveWidth >= Settings.MinAdaptiveColumnWidthCM
				&& CanFitSupportPattern(Region, Settings, EABTSM73DAGSupportPattern::TwoColumnLine, AdaptiveWidth))
			{
				OutPattern = EABTSM73DAGSupportPattern::TwoColumnLine;
				OutColumnWidthCM = AdaptiveWidth;
				return true;
			}
		}
		return false;
	}

	void GatherAssociativeLayoutChildren(const int32 NodeId, const EABTSM73DAGOperator Operator,
		const EABTSM73DAGParallelPolicy ParallelPolicy, const FABTSM73DAGGenerationResult& Graph,
		TArray<int32>& OutChildren)
	{
		if (!Graph.ExpressionNodes.IsValidIndex(NodeId)) return;
		const FABTSM73DAGExpressionNode& Node = Graph.ExpressionNodes[NodeId];
		const bool bSameOperator = Node.Operator == Operator
			&& (Operator != EABTSM73DAGOperator::Parallel || Node.ParallelPolicy == ParallelPolicy);
		if (!bSameOperator)
		{
			OutChildren.Add(NodeId);
			return;
		}
		for (const int32 ChildId : Node.ChildNodeIds)
		{
			const FABTSM73DAGExpressionNode& Child = Graph.ExpressionNodes[ChildId];
			const bool bFlattenChild = Child.Operator == Operator
				&& (Operator != EABTSM73DAGOperator::Parallel || Child.ParallelPolicy == ParallelPolicy);
			if (bFlattenChild) GatherAssociativeLayoutChildren(ChildId, Operator, ParallelPolicy, Graph, OutChildren);
			else OutChildren.Add(ChildId);
		}
	}
}

bool FABTSM73DAGLayoutSolver::Solve(
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	FABTSM73DAGSpatialLayout& OutLayout,
	FString& OutError,
	const FABTSM73DAGFailureRewriteIntent* RewriteIntent) const
{
	OutLayout = FABTSM73DAGSpatialLayout();
	OutError.Reset();
	if (!Graph.bAccepted || Graph.ExpressionNodes.IsEmpty() || Graph.MacroNodes.IsEmpty())
	{
		OutError = TEXT("DAGGraphNotAccepted");
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (Settings.TargetWidthCM < Settings.MinPlateExtentCM || Settings.TargetDepthCM < Settings.MinPlateExtentCM
		|| Settings.TargetHeightCM <= Settings.PlateThicknessCM || Settings.PlateFootprintRatio <= 0.0f
		|| Settings.PlateFootprintRatio > 1.0f
		|| Settings.PreferredLogicalSupportsPerLoad < 1 || Settings.MaxLogicalSupportsPerLoad < 1)
	{
		OutError = TEXT("DAGLayoutSettingsInvalid");
		OutLayout.RejectReason = OutError;
		return false;
	}

	TMap<int32, int32> MacroByExpression;
	for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes)
	{
		MacroByExpression.Add(Macro.SourceExpressionNodeId, Macro.NodeId);
	}
	const FBox RootScope(FVector(-Settings.TargetWidthCM * 0.5f, -Settings.TargetDepthCM * 0.5f, 0.0f),
		FVector(Settings.TargetWidthCM * 0.5f, Settings.TargetDepthCM * 0.5f, Settings.TargetHeightCM));
	if (!AssignExpressionScope(Graph.RootExpressionNodeId, RootScope, Graph, Settings, MacroByExpression, OutLayout, OutError))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (OutLayout.MacroLayouts.Num() != Graph.MacroNodes.Num())
	{
		OutError = FString::Printf(TEXT("DAGScopeMacroCountMismatch:%d:%d"), OutLayout.MacroLayouts.Num(), Graph.MacroNodes.Num());
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (RewriteIntent != nullptr && RewriteIntent->bEnabled)
	{
		if (RewriteIntent->SupportMacroNodeId == INDEX_NONE
			|| RewriteIntent->LoadMacroNodeId == INDEX_NONE
			|| RewriteIntent->AffectedMacroNodeIds.IsEmpty()
			|| !RewriteIntent->AffectedMacroNodeIds.Contains(RewriteIntent->LoadMacroNodeId)
			|| RewriteIntent->AffectedMacroNodeIds.Contains(RewriteIntent->SupportMacroNodeId)
			|| RewriteIntent->ExpectedFailureDirectionXY.IsNearlyZero())
		{
			OutError = TEXT("DAG3BRewriteIntentInvalid");
			OutLayout.RejectReason = OutError;
			return false;
		}
		if (RewriteIntent->Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam)
		{
			const FVector2D DeltaXY =
				RewriteIntent->ExpectedFailureDirectionXY.GetSafeNormal()
				* RewriteIntent->OffsetSeamShiftCM;
			if (DeltaXY.IsNearlyZero())
			{
				OutError = TEXT("DAG3BOffsetSeamShiftInvalid");
				OutLayout.RejectReason = OutError;
				return false;
			}
			for (FABTSM73DAGMacroLayout& MacroLayout : OutLayout.MacroLayouts)
			{
				if (!RewriteIntent->AffectedMacroNodeIds.Contains(MacroLayout.MacroNodeId)) continue;
				MacroLayout.PlateCenter.X += DeltaXY.X;
				MacroLayout.PlateCenter.Y += DeltaXY.Y;
				MacroLayout.AllowedScope.Min.X += DeltaXY.X;
				MacroLayout.AllowedScope.Min.Y += DeltaXY.Y;
				MacroLayout.AllowedScope.Max.X += DeltaXY.X;
				MacroLayout.AllowedScope.Max.Y += DeltaXY.Y;
			}
		}
	}
	if (!SelectSparseSupports(Graph, Settings, OutLayout, OutError, RewriteIntent))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (!AssignStructuralLevels(Graph, Settings, OutLayout, OutError))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	OutLayout.bAccepted = true;
	return true;
}

bool FABTSM73DAGLayoutSolver::SolveSemantic(
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	const FABTSM73DAGSpatialLayout& InitialLayout,
	const FABTSM73SemanticEnvelope& Envelope,
	FABTSM73DAGSpatialLayout& OutLayout,
	FString& OutError,
	const FABTSM73DAGFailureRewriteIntent* RewriteIntent) const
{
	OutLayout = FABTSM73DAGSpatialLayout();
	OutError.Reset();
	if (!Graph.bAccepted
		|| !Envelope.bAccepted
		|| Graph.MacroNodes.IsEmpty()
		|| Graph.SupportEdges.IsEmpty()
		|| Graph.GroundNodeIds.IsEmpty()
		|| Graph.TopLoadNodeIds.IsEmpty())
	{
		OutError = TEXT("DAG5BSemanticGraphNotAccepted");
		OutLayout.RejectReason = OutError;
		return false;
	}
	FABTSM73DAG5BSemanticEnvelopeBuilder ContractBuilder;
	if (!ContractBuilder.ValidateEnvelopeIdentity(
		Envelope,
		OutError))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (InitialLayout.MacroLayouts.Num() != Graph.MacroNodes.Num()
		|| Settings.TargetWidthCM
			< Settings.MinAdaptivePlateExtentCM
		|| Settings.TargetDepthCM
			< Settings.MinAdaptivePlateExtentCM
		|| Settings.TargetHeightCM <= Settings.PlateThicknessCM
		|| Settings.PreferredLogicalSupportsPerLoad < 1
		|| Settings.MaxLogicalSupportsPerLoad < 1)
	{
		OutError = TEXT("DAG5BSemanticLayoutSettingsInvalid");
		OutLayout.RejectReason = OutError;
		return false;
	}

	TSet<int32> MacroIds;
	for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes)
	{
		if (Macro.NodeId == INDEX_NONE || MacroIds.Contains(Macro.NodeId))
		{
			OutError = TEXT("DAG5BSemanticMacroIdentityInvalid");
			OutLayout.RejectReason = OutError;
			return false;
		}
		MacroIds.Add(Macro.NodeId);
	}
	OutLayout.MacroLayouts = InitialLayout.MacroLayouts;
	for (FABTSM73DAGMacroLayout& MacroLayout : OutLayout.MacroLayouts)
	{
		const FABTSM73DAG5BMacroConstraint* Constraint = nullptr;
		int32 ConstraintCount = 0;
		for (const FABTSM73DAG5BMacroConstraint& Candidate :
			Envelope.MacroConstraints)
		{
			if (Candidate.MacroNodeId == MacroLayout.MacroNodeId)
			{
				Constraint = &Candidate;
				++ConstraintCount;
			}
		}
		if (!MacroIds.Contains(MacroLayout.MacroNodeId)
			|| ConstraintCount != 1
			|| Constraint == nullptr
			|| !MacroLayout.AllowedScope.IsValid
			|| MacroLayout.PlateDimensionsCM.X
				< Settings.MinAdaptivePlateExtentCM
			|| MacroLayout.PlateDimensionsCM.Y
				< Settings.MinAdaptivePlateExtentCM
			|| MacroLayout.PlateDimensionsCM.Z <= 0.0f)
		{
			OutError = FString::Printf(
				TEXT("DAG5BSemanticMacroLayoutInvalid:%d"),
				MacroLayout.MacroNodeId);
			OutLayout.RejectReason = OutError;
			return false;
		}
		const FVector2D ConstraintDimensions(
			Constraint->FootprintScale.X * Settings.TargetWidthCM,
			Constraint->FootprintScale.Y * Settings.TargetDepthCM);
		if (!FVector2D(
				MacroLayout.PlateCenter.X,
				MacroLayout.PlateCenter.Y).Equals(
					Constraint->OffsetCM,
					KINDA_SMALL_NUMBER)
			|| !FVector2D(
				MacroLayout.PlateDimensionsCM.X,
				MacroLayout.PlateDimensionsCM.Y).Equals(
					ConstraintDimensions,
					KINDA_SMALL_NUMBER))
		{
			OutError = FString::Printf(
				TEXT("DAG5BSemanticMacroConstraintDrift:%d"),
				MacroLayout.MacroNodeId);
			OutLayout.RejectReason = OutError;
			return false;
		}
		// SemanticEnvelope is the stable authority. InitialLayout only carries
		// plate thickness, ground identity and the construction scope.
		MacroLayout.PlateCenter.X = Constraint->OffsetCM.X;
		MacroLayout.PlateCenter.Y = Constraint->OffsetCM.Y;
		MacroLayout.PlateDimensionsCM.X = ConstraintDimensions.X;
		MacroLayout.PlateDimensionsCM.Y = ConstraintDimensions.Y;
		MacroLayout.StructuralLevel = INDEX_NONE;
	}
	if (Envelope.MacroConstraints.Num() != OutLayout.MacroLayouts.Num()
		|| Envelope.ShapeScopes.Num() != OutLayout.MacroLayouts.Num())
	{
		OutError = TEXT("DAG5BSemanticEnvelopeMacroCardinality");
		OutLayout.RejectReason = OutError;
		return false;
	}

	if (RewriteIntent != nullptr && RewriteIntent->bEnabled)
	{
		if (RewriteIntent->SupportMacroNodeId == INDEX_NONE
			|| RewriteIntent->LoadMacroNodeId == INDEX_NONE
			|| RewriteIntent->AffectedMacroNodeIds.IsEmpty()
			|| !RewriteIntent->AffectedMacroNodeIds.Contains(
				RewriteIntent->LoadMacroNodeId)
			|| RewriteIntent->AffectedMacroNodeIds.Contains(
				RewriteIntent->SupportMacroNodeId)
			|| RewriteIntent->ExpectedFailureDirectionXY.IsNearlyZero())
		{
			OutError = TEXT("DAG3BRewriteIntentInvalid");
			OutLayout.RejectReason = OutError;
			return false;
		}
		if (RewriteIntent->Pattern
			== EABTSM73DAGFailurePattern::InternalOffsetSeam)
		{
			const FVector2D DeltaXY =
				RewriteIntent->ExpectedFailureDirectionXY.GetSafeNormal()
				* RewriteIntent->OffsetSeamShiftCM;
			if (DeltaXY.IsNearlyZero())
			{
				OutError = TEXT("DAG3BOffsetSeamShiftInvalid");
				OutLayout.RejectReason = OutError;
				return false;
			}
			for (FABTSM73DAGMacroLayout& MacroLayout :
				OutLayout.MacroLayouts)
			{
				if (!RewriteIntent->AffectedMacroNodeIds.Contains(
					MacroLayout.MacroNodeId))
				{
					continue;
				}
				MacroLayout.PlateCenter.X += DeltaXY.X;
				MacroLayout.PlateCenter.Y += DeltaXY.Y;
				MacroLayout.AllowedScope.Min.X += DeltaXY.X;
				MacroLayout.AllowedScope.Min.Y += DeltaXY.Y;
				MacroLayout.AllowedScope.Max.X += DeltaXY.X;
				MacroLayout.AllowedScope.Max.Y += DeltaXY.Y;
			}
		}
	}
	if (!SelectSparseSupports(
		Graph,
		Settings,
		OutLayout,
		OutError,
		RewriteIntent,
		&Envelope))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	if (!AssignStructuralLevels(
		Graph,
		Settings,
		OutLayout,
		OutError))
	{
		OutLayout.RejectReason = OutError;
		return false;
	}
	OutLayout.bAccepted = true;
	return true;
}

bool FABTSM73DAGLayoutSolver::AssignExpressionScope(
	const int32 ExpressionNodeId,
	const FBox& Scope,
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	const TMap<int32, int32>& MacroByExpression,
	FABTSM73DAGSpatialLayout& InOutLayout,
	FString& OutError) const
{
	if (!Graph.ExpressionNodes.IsValidIndex(ExpressionNodeId) || !Scope.IsValid)
	{
		OutError = TEXT("DAGScopeExpressionInvalid");
		return false;
	}
	const FABTSM73DAGExpressionNode& Expression = Graph.ExpressionNodes[ExpressionNodeId];
	const FVector ScopeSize = Scope.GetSize();
	if (Expression.Operator == EABTSM73DAGOperator::Atom)
	{
		const int32* MacroNodeId = MacroByExpression.Find(ExpressionNodeId);
		if (MacroNodeId == nullptr)
		{
			OutError = FString::Printf(TEXT("DAGMacroForExpressionMissing:%d"), ExpressionNodeId);
			return false;
		}
		const float RequiredPlateExtent = Settings.bEnableAdaptiveGeometry
			? Settings.MinAdaptivePlateExtentCM : Settings.MinPlateExtentCM;
		if (ScopeSize.X < RequiredPlateExtent || ScopeSize.Y < RequiredPlateExtent)
		{
			OutError = FString::Printf(TEXT("DAGScopeTooSmall:%d:%.1f:%.1f:%.1f"), *MacroNodeId, ScopeSize.X, ScopeSize.Y, ScopeSize.Z);
			return false;
		}
		FABTSM73DAGMacroLayout& Layout = InOutLayout.MacroLayouts.AddDefaulted_GetRef();
		Layout.MacroNodeId = *MacroNodeId;
		Layout.AllowedScope = Scope;
		Layout.bGroundTerminal = IsGroundMacro(Graph, *MacroNodeId);
		const float NarrowExtentRatio = FMath::Clamp(
			FMath::Min(ScopeSize.X, ScopeSize.Y) / FMath::Max(1.0f, Settings.MinPlateExtentCM), 0.0f, 1.0f);
		const float RealizedPlateThickness = Settings.bEnableAdaptiveGeometry
			? FMath::Lerp(Settings.MinAdaptivePlateThicknessCM, Settings.PlateThicknessCM, NarrowExtentRatio)
			: Settings.PlateThicknessCM;
		Layout.PlateDimensionsCM = FVector(ScopeSize.X * Settings.PlateFootprintRatio,
			ScopeSize.Y * Settings.PlateFootprintRatio, RealizedPlateThickness);
		// Recursive expression scopes own only the XY footprint. Z is solved later
		// from the compiled support DAG, so grammar depth cannot create short or
		// same-height physical columns.
		Layout.PlateCenter = FVector(Scope.GetCenter().X, Scope.GetCenter().Y, 0.0f);
		Layout.StructuralLevel = INDEX_NONE;
		return true;
	}
	if (Expression.ChildNodeIds.Num() < 2)
	{
		OutError = FString::Printf(TEXT("DAGScopeOperatorArity:%d"), ExpressionNodeId);
		return false;
	}

	TArray<int32> LayoutChildren;
	GatherAssociativeLayoutChildren(ExpressionNodeId, Expression.Operator, Expression.ParallelPolicy, Graph, LayoutChildren);
	const int32 ChildCount = LayoutChildren.Num();
	if (ChildCount < 2)
	{
		OutError = FString::Printf(TEXT("DAGScopeFlattenedOperatorArity:%d"), ExpressionNodeId);
		return false;
	}
	if (Expression.Operator == EABTSM73DAGOperator::Series)
	{
		// A Series operator describes support order, not another subdivision of
		// the height budget. All children retain the same XY construction scope.
		for (const int32 ChildId : LayoutChildren)
		{
			if (!AssignExpressionScope(ChildId, Scope, Graph, Settings, MacroByExpression, InOutLayout, OutError)) return false;
		}
		return true;
	}

	const bool bSplitX = !Settings.bAlternateParallelAxes || (Expression.ExpansionDepth % 2 == 0);
	const float AxisLength = bSplitX ? ScopeSize.X : ScopeSize.Y;
	const float AvailableLength = AxisLength - Settings.ParallelGapCM * static_cast<float>(ChildCount - 1);
	const float ChildLength = AvailableLength / static_cast<float>(ChildCount);
	const float RequiredChildExtent = Settings.bEnableAdaptiveGeometry
		? Settings.MinAdaptivePlateExtentCM : Settings.MinPlateExtentCM;
	if (ChildLength < RequiredChildExtent)
	{
		OutError = FString::Printf(TEXT("DAGParallelScopeTooNarrow:%d:%.1f"), ExpressionNodeId, ChildLength);
		return false;
	}
	float Cursor = bSplitX ? Scope.Min.X : Scope.Min.Y;
	for (const int32 ChildId : LayoutChildren)
	{
		FBox ChildScope = Scope;
		if (bSplitX)
		{
			ChildScope.Min.X = Cursor;
			ChildScope.Max.X = Cursor + ChildLength;
		}
		else
		{
			ChildScope.Min.Y = Cursor;
			ChildScope.Max.Y = Cursor + ChildLength;
		}
		if (!AssignExpressionScope(ChildId, ChildScope, Graph, Settings, MacroByExpression, InOutLayout, OutError)) return false;
		Cursor += ChildLength + Settings.ParallelGapCM;
	}
	return true;
}

bool FABTSM73DAGLayoutSolver::AssignStructuralLevels(
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	FABTSM73DAGSpatialLayout& InOutLayout,
	FString& OutError) const
{
	TMap<int32, int32> LevelByMacro;
	for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes) LevelByMacro.Add(Macro.NodeId, 0);

	// Support -> Load is a DAG. Repeated relaxation computes its longest-path
	// rank without relying on expression recursion depth or node numbering.
	for (int32 Pass = 0; Pass < Graph.MacroNodes.Num(); ++Pass)
	{
		bool bChanged = false;
		for (const FABTSM73DAGSelectedSupport& Edge : InOutLayout.SelectedSupports)
		{
			const int32* SupportLevel = LevelByMacro.Find(Edge.SupportMacroNodeId);
			int32* LoadLevel = LevelByMacro.Find(Edge.LoadMacroNodeId);
			if (SupportLevel == nullptr || LoadLevel == nullptr)
			{
				OutError = TEXT("DAGStructuralLevelNodeMissing");
				return false;
			}
			const int32 RequiredLevel = *SupportLevel + 1;
			if (*LoadLevel < RequiredLevel) { *LoadLevel = RequiredLevel; bChanged = true; }
		}
		if (!bChanged) break;
		if (Pass == Graph.MacroNodes.Num() - 1)
		{
			OutError = TEXT("DAGStructuralLevelCycle");
			return false;
		}
	}

	int32 MaxLevel = 0;
	for (const TPair<int32, int32>& Pair : LevelByMacro) MaxLevel = FMath::Max(MaxLevel, Pair.Value);
	const float RequiredPitch = Settings.PlateThicknessCM + Settings.MinColumnHeightCM;
	const float TargetPitch = MaxLevel > 0
		? (Settings.TargetHeightCM - Settings.PlateThicknessCM) / static_cast<float>(MaxLevel)
		: RequiredPitch;
	const float LevelPitch = FMath::Max(RequiredPitch, TargetPitch);
	for (FABTSM73DAGMacroLayout& Layout : InOutLayout.MacroLayouts)
	{
		const int32* Level = LevelByMacro.Find(Layout.MacroNodeId);
		if (Level == nullptr) { OutError = TEXT("DAGStructuralLevelLayoutMissing"); return false; }
		Layout.StructuralLevel = *Level;
		Layout.PlateCenter.Z = Layout.PlateDimensionsCM.Z * 0.5f + LevelPitch * static_cast<float>(*Level);
		Layout.AllowedScope.Min.Z = Layout.PlateCenter.Z - Settings.PlateThicknessCM * 0.5f;
		Layout.AllowedScope.Max.Z = Layout.PlateCenter.Z + Settings.PlateThicknessCM * 0.5f;
	}
	return true;
}

bool FABTSM73DAGLayoutSolver::SelectSparseSupports(
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	FABTSM73DAGSpatialLayout& InOutLayout,
	FString& OutError,
	const FABTSM73DAGFailureRewriteIntent* RewriteIntent,
	const FABTSM73SemanticEnvelope* SemanticEnvelope) const
{
	TMap<int32, TArray<FABTSM73DAGSelectedSupport>> CandidatesByLoad;
	for (const FABTSM73DAGSupportEdge& Edge : Graph.SupportEdges)
	{
		if (RewriteIntent != nullptr && RewriteIntent->bEnabled)
		{
			if (Edge.LoadNodeId == RewriteIntent->LoadMacroNodeId
				&& Edge.SupportNodeId != RewriteIntent->SupportMacroNodeId)
			{
				continue;
			}
			if (Edge.LoadNodeId != RewriteIntent->LoadMacroNodeId
				&& RewriteIntent->AffectedMacroNodeIds.Contains(Edge.LoadNodeId)
				&& !RewriteIntent->AffectedMacroNodeIds.Contains(Edge.SupportNodeId))
			{
				continue;
			}
		}
		const FABTSM73DAGMacroLayout* Support = FindLayout(InOutLayout, Edge.SupportNodeId);
		const FABTSM73DAGMacroLayout* Load = FindLayout(InOutLayout, Edge.LoadNodeId);
		if (Support == nullptr || Load == nullptr)
		{
			OutError = TEXT("DAGSupportLayoutMissing");
			return false;
		}
		FBox2D Intersection(EForceInit::ForceInit);
		if (!IntersectBoxes2D(PlateBoundsXY(*Support), PlateBoundsXY(*Load), Intersection))
		{
			++InOutLayout.RejectedCandidateEdgeCount;
			continue;
		}
		if (SemanticEnvelope != nullptr)
		{
			const FABTSM73DAG5BSupportPortConstraint* Port =
				SemanticEnvelope->SupportPorts.FindByPredicate(
					[&Edge](
						const FABTSM73DAG5BSupportPortConstraint&
							Candidate)
					{
						return Candidate.SupportMacroNodeId
								== Edge.SupportNodeId
							&& Candidate.LoadMacroNodeId
								== Edge.LoadNodeId;
					});
			if (Port == nullptr)
			{
				OutError = FString::Printf(
					TEXT("DAG5BSupportPortMissing:%d:%d"),
					Edge.SupportNodeId,
					Edge.LoadNodeId);
				return false;
			}
			FABTSM73DAG5BSemanticEnvelopeBuilder ContractBuilder;
			if (!ContractBuilder.ValidateSupportPortProvenance(
				*SemanticEnvelope,
				*Port,
				OutError))
			{
				return false;
			}
			FBox2D PortIntersection(EForceInit::ForceInit);
			if (!IntersectBoxes2D(
				Intersection,
				Port->AllowedColumnRegion,
				PortIntersection))
			{
				OutError = FString::Printf(
					TEXT("DAG5BSupportPortNoOverlap:%d:%d"),
					Edge.SupportNodeId,
					Edge.LoadNodeId);
				return false;
			}
			Intersection = PortIntersection;
		}
		EABTSM73DAGSupportPattern ResolvedPattern = Settings.SupportPattern;
		float ResolvedColumnWidthCM = Settings.ColumnWidthCM;
		const bool bRewriteInterface = RewriteIntent != nullptr
			&& RewriteIntent->bEnabled
			&& Edge.SupportNodeId == RewriteIntent->SupportMacroNodeId
			&& Edge.LoadNodeId == RewriteIntent->LoadMacroNodeId;
		if (bRewriteInterface)
		{
			ResolvedPattern =
				RewriteIntent->Pattern == EABTSM73DAGFailurePattern::InternalSingleSupport
				? EABTSM73DAGSupportPattern::SingleColumnInterface
				: EABTSM73DAGSupportPattern::TwoColumnLine;
			ResolvedColumnWidthCM = Settings.MinAdaptiveColumnWidthCM;
		}
		else if (!ResolveSupportPattern(Intersection, Settings, ResolvedPattern, ResolvedColumnWidthCM))
		{
			++InOutLayout.RejectedCandidateEdgeCount;
			continue;
		}
		FABTSM73DAGSelectedSupport& Candidate = CandidatesByLoad.FindOrAdd(Edge.LoadNodeId).AddDefaulted_GetRef();
		Candidate.SupportMacroNodeId = Edge.SupportNodeId;
		Candidate.LoadMacroNodeId = Edge.LoadNodeId;
		Candidate.FeasibleColumnRegion = Intersection;
		Candidate.SupportPattern = ResolvedPattern;
		Candidate.RealizedColumnWidthCM = ResolvedColumnWidthCM;
		Candidate.Cost = FVector2D::Distance(FVector2D(Support->PlateCenter.X, Support->PlateCenter.Y),
			FVector2D(Load->PlateCenter.X, Load->PlateCenter.Y));
	}

	FABTSM73DAGLoadSupportSolver LoadSupportSolver;
	return LoadSupportSolver.Solve(Graph, Settings, CandidatesByLoad, InOutLayout, OutError, RewriteIntent);
}
