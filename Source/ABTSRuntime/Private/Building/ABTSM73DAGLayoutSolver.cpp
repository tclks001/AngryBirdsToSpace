// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGLayoutSolver.h"

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
		if (!Region.bIsValid) return false;
		const FVector2D Size = Region.GetSize();
		const float SingleAxis = ColumnWidthCM + Settings.ColumnClearanceCM * 2.0f;
		const float PairAxis = ColumnWidthCM * 2.0f + Settings.ColumnClearanceCM * 3.0f;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface:
			return Size.X >= SingleAxis && Size.Y >= SingleAxis;
		case EABTSM73DAGSupportPattern::TwoColumnLine:
			return FMath::Min(Size.X, Size.Y) >= SingleAxis && FMath::Max(Size.X, Size.Y) >= PairAxis;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
		case EABTSM73DAGSupportPattern::FourColumnFootprint:
			return Size.X >= PairAxis && Size.Y >= PairAxis;
		default:
			return false;
		}
	}

	bool ResolveSupportPattern(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		EABTSM73DAGSupportPattern& OutPattern, float& OutColumnWidthCM)
	{
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
	FString& OutError) const
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
	if (!SelectSparseSupports(Graph, Settings, OutLayout, OutError))
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
		if (ScopeSize.X < Settings.MinPlateExtentCM || ScopeSize.Y < Settings.MinPlateExtentCM
			|| ScopeSize.Z < Settings.PlateThicknessCM)
		{
			OutError = FString::Printf(TEXT("DAGScopeTooSmall:%d:%.1f:%.1f:%.1f"), *MacroNodeId, ScopeSize.X, ScopeSize.Y, ScopeSize.Z);
			return false;
		}
		FABTSM73DAGMacroLayout& Layout = InOutLayout.MacroLayouts.AddDefaulted_GetRef();
		Layout.MacroNodeId = *MacroNodeId;
		Layout.AllowedScope = Scope;
		Layout.bGroundTerminal = IsGroundMacro(Graph, *MacroNodeId);
		Layout.PlateDimensionsCM = FVector(ScopeSize.X * Settings.PlateFootprintRatio,
			ScopeSize.Y * Settings.PlateFootprintRatio, Settings.PlateThicknessCM);
		Layout.PlateCenter = Scope.GetCenter();
		if (Layout.bGroundTerminal) Layout.PlateCenter.Z = Scope.Min.Z + Settings.PlateThicknessCM * 0.5f;
		Layout.StructuralLevel = FMath::RoundToInt(Layout.PlateCenter.Z / FMath::Max(1.0f, Settings.PlateThicknessCM));
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
		const float AvailableHeight = ScopeSize.Z - Settings.SeriesGapCM * static_cast<float>(ChildCount - 1);
		const float ChildHeight = AvailableHeight / static_cast<float>(ChildCount);
		if (ChildHeight < Settings.PlateThicknessCM)
		{
			OutError = FString::Printf(TEXT("DAGSeriesScopeTooShort:%d:%.1f"), ExpressionNodeId, ChildHeight);
			return false;
		}
		float TopZ = Scope.Max.Z;
		for (const int32 ChildId : LayoutChildren)
		{
			const float BottomZ = TopZ - ChildHeight;
			const FBox ChildScope(FVector(Scope.Min.X, Scope.Min.Y, BottomZ), FVector(Scope.Max.X, Scope.Max.Y, TopZ));
			if (!AssignExpressionScope(ChildId, ChildScope, Graph, Settings, MacroByExpression, InOutLayout, OutError)) return false;
			TopZ = BottomZ - Settings.SeriesGapCM;
		}
		return true;
	}

	const bool bSplitX = !Settings.bAlternateParallelAxes || (Expression.ExpansionDepth % 2 == 0);
	const float AxisLength = bSplitX ? ScopeSize.X : ScopeSize.Y;
	const float AvailableLength = AxisLength - Settings.ParallelGapCM * static_cast<float>(ChildCount - 1);
	const float ChildLength = AvailableLength / static_cast<float>(ChildCount);
	if (ChildLength < Settings.MinPlateExtentCM)
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

bool FABTSM73DAGLayoutSolver::SelectSparseSupports(
	const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	FABTSM73DAGSpatialLayout& InOutLayout,
	FString& OutError) const
{
	TMap<int32, TArray<FABTSM73DAGSelectedSupport>> CandidatesByLoad;
	for (const FABTSM73DAGSupportEdge& Edge : Graph.SupportEdges)
	{
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
		EABTSM73DAGSupportPattern ResolvedPattern = Settings.SupportPattern;
		float ResolvedColumnWidthCM = Settings.ColumnWidthCM;
		if (!ResolveSupportPattern(Intersection, Settings, ResolvedPattern, ResolvedColumnWidthCM))
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

	for (const FABTSM73DAGMacroLayout& LoadLayout : InOutLayout.MacroLayouts)
	{
		if (LoadLayout.bGroundTerminal) continue;
		TArray<FABTSM73DAGSelectedSupport>* Candidates = CandidatesByLoad.Find(LoadLayout.MacroNodeId);
		if (Candidates == nullptr || Candidates->IsEmpty())
		{
			OutError = FString::Printf(TEXT("DAGNoFeasibleSupport:%d:Center=%.1f,%.1f:Size=%.1f,%.1f"),
				LoadLayout.MacroNodeId, LoadLayout.PlateCenter.X, LoadLayout.PlateCenter.Y,
				LoadLayout.PlateDimensionsCM.X, LoadLayout.PlateDimensionsCM.Y);
			return false;
		}
		Candidates->Sort([](const FABTSM73DAGSelectedSupport& A, const FABTSM73DAGSelectedSupport& B)
		{
			if (!FMath::IsNearlyEqual(A.Cost, B.Cost)) return A.Cost < B.Cost;
			return A.SupportMacroNodeId < B.SupportMacroNodeId;
		});
		const int32 SelectedCount = FMath::Min3(Settings.PreferredLogicalSupportsPerLoad,
			Settings.MaxLogicalSupportsPerLoad, Candidates->Num());
		for (int32 Index = 0; Index < SelectedCount; ++Index) InOutLayout.SelectedSupports.Add((*Candidates)[Index]);
	}
	if (InOutLayout.SelectedSupports.IsEmpty())
	{
		OutError = TEXT("DAGSparseSupportEmpty");
		return false;
	}
	return true;
}
