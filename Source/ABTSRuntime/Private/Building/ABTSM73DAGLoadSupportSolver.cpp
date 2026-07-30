// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGLoadSupportSolver.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGSupportGeometry.h"
#include "Building/ABTSM73DAGTypes.h"

namespace
{
	struct FLoadState
	{
		float Mass = 0.0f;
		FVector2D FirstMoment = FVector2D::ZeroVector;
	};

	const FABTSM73DAGMacroLayout* FindLoadSupportLayout(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate([MacroNodeId](const FABTSM73DAGMacroLayout& Candidate)
		{
			return Candidate.MacroNodeId == MacroNodeId;
		});
	}

	int32 PatternColumnCount(const EABTSM73DAGSupportPattern Pattern)
	{
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface: return 1;
		case EABTSM73DAGSupportPattern::TwoColumnLine: return 2;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod: return 3;
		case EABTSM73DAGSupportPattern::FourColumnFootprint: return 4;
		default: return 0;
		}
	}

	bool TryResolveNarrowerPattern(
		FABTSM73DAGSelectedSupport& Support,
		const FABTSM73DAGLayoutSettings& Settings,
		const float ColumnWidthCM)
	{
		if (!Settings.bAllowNarrowSupportFallback) return false;

		if (Support.SupportPattern != EABTSM73DAGSupportPattern::TwoColumnLine
			&& Support.SupportPattern != EABTSM73DAGSupportPattern::SingleColumnInterface)
		{
			TArray<FVector2D> Centers;
			if (FABTSM73DAGSupportGeometry::MakeColumnCenters(
				Support.FeasibleColumnRegion,
				Settings,
				EABTSM73DAGSupportPattern::TwoColumnLine,
				ColumnWidthCM,
				Centers))
			{
				Support.SupportPattern = EABTSM73DAGSupportPattern::TwoColumnLine;
				Support.RealizedColumnCenters = MoveTemp(Centers);
				return true;
			}
		}

		if (Settings.bAllowAdaptiveColumnWidth
			&& Support.SupportPattern != EABTSM73DAGSupportPattern::SingleColumnInterface)
		{
			TArray<FVector2D> Centers;
			if (FABTSM73DAGSupportGeometry::MakeColumnCenters(
				Support.FeasibleColumnRegion,
				Settings,
				EABTSM73DAGSupportPattern::SingleColumnInterface,
				ColumnWidthCM,
				Centers))
			{
				Support.SupportPattern = EABTSM73DAGSupportPattern::SingleColumnInterface;
				Support.RealizedColumnCenters = MoveTemp(Centers);
				return true;
			}
		}
		return false;
	}

	float Cross2D(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - Origin.X) * (B.Y - Origin.Y) - (A.Y - Origin.Y) * (B.X - Origin.X);
	}

	TArray<FVector2D> BuildHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B) { return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y; });
		TArray<FVector2D> Hull;
		for (const FVector2D& Point : Points)
		{
			while (Hull.Num() >= 2 && Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Point) <= KINDA_SMALL_NUMBER) Hull.Pop();
			Hull.Add(Point);
		}
		const int32 LowerCount = Hull.Num();
		for (int32 Index = Points.Num() - 2; Index >= 0; --Index)
		{
			while (Hull.Num() > LowerCount && Cross2D(Hull[Hull.Num() - 2], Hull.Last(), Points[Index]) <= KINDA_SMALL_NUMBER) Hull.Pop();
			Hull.Add(Points[Index]);
		}
		if (!Hull.IsEmpty()) Hull.Pop();
		return Hull;
	}

	bool ContainsPoint(const FVector2D& Point, const TArray<FVector2D>& Hull)
	{
		if (Hull.Num() < 3) return false;
		bool bPositive = false;
		bool bNegative = false;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const float Side = Cross2D(Hull[Index], Hull[(Index + 1) % Hull.Num()], Point);
			bPositive |= Side > KINDA_SMALL_NUMBER;
			bNegative |= Side < -KINDA_SMALL_NUMBER;
			if (bPositive && bNegative) return false;
		}
		return true;
	}

	float HullMargin(const FVector2D& Point, const TArray<FVector2D>& Hull)
	{
		float Margin = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D A = Hull[Index];
			const FVector2D B = Hull[(Index + 1) % Hull.Num()];
			Margin = FMath::Min(Margin, FMath::Abs(Cross2D(A, B, Point)) / FMath::Max(KINDA_SMALL_NUMBER, FVector2D::Distance(A, B)));
		}
		return Margin;
	}

	void AddSquareCorners(const FVector2D& Center, const float Width, TArray<FVector2D>& OutCorners)
	{
		const float Half = Width * 0.5f;
		OutCorners.Add(Center + FVector2D(-Half, -Half));
		OutCorners.Add(Center + FVector2D(Half, -Half));
		OutCorners.Add(Center + FVector2D(-Half, Half));
		OutCorners.Add(Center + FVector2D(Half, Half));
	}

	bool MakeResultantCenteredColumnCenters(
		const FBox2D& Region,
		const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern,
		const float ColumnWidthCM,
		const FVector2D& Resultant,
		TArray<FVector2D>& OutCenters)
	{
		if (!Region.bIsValid)
		{
			return false;
		}

		const float RequiredSeparation =
			ColumnWidthCM + Settings.ColumnClearanceCM;
		TArray<FVector2D> LocalOffsets;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface:
			LocalOffsets.Add(FVector2D::ZeroVector);
			break;
		case EABTSM73DAGSupportPattern::TwoColumnLine:
			if (Region.GetSize().X >= Region.GetSize().Y)
			{
				LocalOffsets.Add(FVector2D(
					-RequiredSeparation * 0.5f,
					0.0f));
				LocalOffsets.Add(FVector2D(
					RequiredSeparation * 0.5f,
					0.0f));
			}
			else
			{
				LocalOffsets.Add(FVector2D(
					0.0f,
					-RequiredSeparation * 0.5f));
				LocalOffsets.Add(FVector2D(
					0.0f,
					RequiredSeparation * 0.5f));
			}
			break;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
			if (Region.GetSize().X >= Region.GetSize().Y)
			{
				LocalOffsets.Add(FVector2D(
					-RequiredSeparation * 0.5f,
					-RequiredSeparation / 3.0f));
				LocalOffsets.Add(FVector2D(
					RequiredSeparation * 0.5f,
					-RequiredSeparation / 3.0f));
				LocalOffsets.Add(FVector2D(
					0.0f,
					RequiredSeparation * 2.0f / 3.0f));
			}
			else
			{
				LocalOffsets.Add(FVector2D(
					-RequiredSeparation / 3.0f,
					-RequiredSeparation * 0.5f));
				LocalOffsets.Add(FVector2D(
					-RequiredSeparation / 3.0f,
					RequiredSeparation * 0.5f));
				LocalOffsets.Add(FVector2D(
					RequiredSeparation * 2.0f / 3.0f,
					0.0f));
			}
			break;
		case EABTSM73DAGSupportPattern::FourColumnFootprint:
			LocalOffsets.Add(FVector2D(
				-RequiredSeparation * 0.5f,
				-RequiredSeparation * 0.5f));
			LocalOffsets.Add(FVector2D(
				RequiredSeparation * 0.5f,
				-RequiredSeparation * 0.5f));
			LocalOffsets.Add(FVector2D(
				-RequiredSeparation * 0.5f,
				RequiredSeparation * 0.5f));
			LocalOffsets.Add(FVector2D(
				RequiredSeparation * 0.5f,
				RequiredSeparation * 0.5f));
			break;
		default:
			return false;
		}

		FVector2D MinOffset(TNumericLimits<float>::Max());
		FVector2D MaxOffset(-TNumericLimits<float>::Max());
		for (const FVector2D& Offset : LocalOffsets)
		{
			MinOffset.X = FMath::Min(MinOffset.X, Offset.X);
			MinOffset.Y = FMath::Min(MinOffset.Y, Offset.Y);
			MaxOffset.X = FMath::Max(MaxOffset.X, Offset.X);
			MaxOffset.Y = FMath::Max(MaxOffset.Y, Offset.Y);
		}

		const float SafeInset =
			ColumnWidthCM * 0.5f + Settings.ColumnClearanceCM;
		const FVector2D AnchorMin =
			Region.Min + FVector2D(SafeInset) - MinOffset;
		const FVector2D AnchorMax =
			Region.Max - FVector2D(SafeInset) - MaxOffset;
		if (AnchorMax.X + KINDA_SMALL_NUMBER < AnchorMin.X
			|| AnchorMax.Y + KINDA_SMALL_NUMBER < AnchorMin.Y)
		{
			return false;
		}

		const FVector2D Anchor(
			FMath::Clamp(Resultant.X, AnchorMin.X, AnchorMax.X),
			FMath::Clamp(Resultant.Y, AnchorMin.Y, AnchorMax.Y));
		OutCenters.Reset(LocalOffsets.Num());
		for (const FVector2D& Offset : LocalOffsets)
		{
			OutCenters.Add(Anchor + Offset);
		}
		return true;
	}

	bool ComputeMinimumSingleColumnRemovalMargin(
		const FVector2D& Resultant,
		const TArray<FABTSM73DAGSelectedSupport>& Supports,
		float& OutMargin)
	{
		int32 TotalColumnCount = 0;
		for (const FABTSM73DAGSelectedSupport& Support : Supports)
		{
			TotalColumnCount += Support.RealizedColumnCenters.Num();
		}
		if (TotalColumnCount <= 1)
		{
			return false;
		}

		OutMargin = TNumericLimits<float>::Max();
		for (int32 RemovedSupportIndex = 0;
			RemovedSupportIndex < Supports.Num();
			++RemovedSupportIndex)
		{
			const FABTSM73DAGSelectedSupport& RemovedSupport =
				Supports[RemovedSupportIndex];
			for (int32 RemovedColumnIndex = 0;
				RemovedColumnIndex
					< RemovedSupport.RealizedColumnCenters.Num();
				++RemovedColumnIndex)
			{
				TArray<FVector2D> RemainingCorners;
				for (int32 SupportIndex = 0;
					SupportIndex < Supports.Num();
					++SupportIndex)
				{
					const FABTSM73DAGSelectedSupport& Support =
						Supports[SupportIndex];
					for (int32 ColumnIndex = 0;
						ColumnIndex
							< Support.RealizedColumnCenters.Num();
						++ColumnIndex)
					{
						if (SupportIndex == RemovedSupportIndex
							&& ColumnIndex == RemovedColumnIndex)
						{
							continue;
						}
						AddSquareCorners(
							Support.RealizedColumnCenters[ColumnIndex],
							Support.RealizedColumnWidthCM,
							RemainingCorners);
					}
				}
				const TArray<FVector2D> RemainingHull =
					BuildHull(MoveTemp(RemainingCorners));
				if (!ContainsPoint(Resultant, RemainingHull))
				{
					return false;
				}
				OutMargin = FMath::Min(
					OutMargin,
					HullMargin(Resultant, RemainingHull));
			}
		}
		return FMath::IsFinite(OutMargin);
	}

	bool HasColumnClearanceAcrossSupports(
		const TArray<FABTSM73DAGSelectedSupport>& Supports,
		const float ColumnClearanceCM)
	{
		TArray<FVector2D> Centers;
		TArray<float> Widths;
		for (const FABTSM73DAGSelectedSupport& Support : Supports)
		{
			for (const FVector2D& Center
				: Support.RealizedColumnCenters)
			{
				Centers.Add(Center);
				Widths.Add(Support.RealizedColumnWidthCM);
			}
		}
		for (int32 A = 0; A < Centers.Num(); ++A)
		{
			for (int32 B = A + 1; B < Centers.Num(); ++B)
			{
				const FVector2D Delta =
					(Centers[A] - Centers[B]).GetAbs();
				const float RequiredAxisSeparation =
					(Widths[A] + Widths[B]) * 0.5f
					+ ColumnClearanceCM;
				if (Delta.X + KINDA_SMALL_NUMBER
						< RequiredAxisSeparation
					&& Delta.Y + KINDA_SMALL_NUMBER
						< RequiredAxisSeparation)
				{
					return false;
				}
			}
		}
		return true;
	}

	bool MakeFailureRewriteSupport(
		const FABTSM73DAGFailureRewriteIntent& Intent,
		const FABTSM73DAGLayoutSettings& Settings,
		const FABTSM73DAGMacroLayout& Load,
		const FVector2D& Resultant,
		const FABTSM73DAGSelectedSupport& Candidate,
		FABTSM73DAGSelectedSupport& OutSupport,
		FString& OutError)
	{
		OutSupport = Candidate;
		OutSupport.RealizedColumnCenters.Reset();
		OutSupport.RealizedColumnRoles.Reset();
		const bool bSingle =
			Intent.Pattern == EABTSM73DAGFailurePattern::InternalSingleSupport;
		const int32 ColumnCount = bSingle ? 1 : 2;
		const FVector2D LoadCenter(Load.PlateCenter.X, Load.PlateCenter.Y);
		const FVector2D Direction = Intent.ExpectedFailureDirectionXY.GetSafeNormal();
		const bool bAlongX = FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y);
		const float RequiredAreaWidth = FMath::Sqrt(
			Intent.ContactAreaSafetyFactor
			* Settings.MinSupportContactAreaRatio
			* Load.PlateDimensionsCM.X
			* Load.PlateDimensionsCM.Y
			/ static_cast<float>(ColumnCount));
		const float CrossDelta = bAlongX
			? FMath::Abs(LoadCenter.Y - Resultant.Y)
			: FMath::Abs(LoadCenter.X - Resultant.X);
		const float SingleDelta = FMath::Max(
			FMath::Abs(LoadCenter.X - Resultant.X),
			FMath::Abs(LoadCenter.Y - Resultant.Y));
		const float RequiredContainmentWidth = bSingle
			? SingleDelta + Intent.MinInitialSupportMarginCM * 2.0f
			: CrossDelta + Intent.MinInitialSupportMarginCM * 2.0f;
		const float Width = FMath::Max3(
			Settings.MinAdaptiveColumnWidthCM,
			RequiredAreaWidth,
			RequiredContainmentWidth);
		if (!FMath::IsFinite(Width)
			|| Width > Settings.MaxAdaptiveColumnWidthCM + KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("DAG3BColumnWidthUnavailable:%.3f"), Width);
			return false;
		}

		const float Half = Width * 0.5f;
		const float Clearance = Settings.ColumnClearanceCM;
		const FVector2D CenterMin =
			Candidate.FeasibleColumnRegion.Min + FVector2D(Half + Clearance);
		const FVector2D CenterMax =
			Candidate.FeasibleColumnRegion.Max - FVector2D(Half + Clearance);
		if (CenterMax.X < CenterMin.X || CenterMax.Y < CenterMin.Y)
		{
			OutError = TEXT("DAG3BRewriteInterfaceTooNarrow");
			return false;
		}

		OutSupport.RealizedColumnWidthCM = Width;
		if (bSingle)
		{
			OutSupport.SupportPattern = EABTSM73DAGSupportPattern::SingleColumnInterface;
			const FVector2D DesiredCenter = (Resultant + LoadCenter) * 0.5f;
			const FVector2D Center(
				FMath::Clamp(DesiredCenter.X, CenterMin.X, CenterMax.X),
				FMath::Clamp(DesiredCenter.Y, CenterMin.Y, CenterMax.Y));
			const float ResultantMargin = FMath::Min(
				Half - FMath::Abs(Resultant.X - Center.X),
				Half - FMath::Abs(Resultant.Y - Center.Y));
			if (ResultantMargin + KINDA_SMALL_NUMBER < Intent.MinInitialSupportMarginCM
				|| FMath::Abs(LoadCenter.X - Center.X) > Half + KINDA_SMALL_NUMBER
				|| FMath::Abs(LoadCenter.Y - Center.Y) > Half + KINDA_SMALL_NUMBER)
			{
				OutError = TEXT("DAG3BSingleSupportContainmentFailed");
				return false;
			}
			OutSupport.RealizedColumnCenters.Add(Center);
			OutSupport.RealizedColumnRoles.Add(EABTSM73DAGRealizedColumnRole::FailureWeak);
			return true;
		}

		OutSupport.SupportPattern = EABTSM73DAGSupportPattern::TwoColumnLine;
		const float Separation = Width
			+ Intent.MinPostFailureTipMarginCM * 2.0f
			+ 2.0f;
		const float AxisCenterMin = bAlongX ? CenterMin.X : CenterMin.Y;
		const float AxisCenterMax = bAlongX ? CenterMax.X : CenterMax.Y;
		const float ResultantAxis = bAlongX ? Resultant.X : Resultant.Y;
		const float ResultantCross = bAlongX ? Resultant.Y : Resultant.X;
		const float LoadCross = bAlongX ? LoadCenter.Y : LoadCenter.X;
		const FVector2D Axis = bAlongX
			? FVector2D(FMath::Sign(Direction.X), 0.0f)
			: FVector2D(0.0f, FMath::Sign(Direction.Y));
		const float AxisSign = bAlongX ? Axis.X : Axis.Y;
		const float WeakAxisMin = AxisSign > 0.0f
			? AxisCenterMin + Separation
			: AxisCenterMin;
		const float WeakAxisMax = AxisSign > 0.0f
			? AxisCenterMax
			: AxisCenterMax - Separation;
		if (WeakAxisMax < WeakAxisMin)
		{
			OutError = TEXT("DAG3BDualSupportSpanUnavailable");
			return false;
		}
		const float WeakAxis = FMath::Clamp(
			ResultantAxis,
			WeakAxisMin,
			WeakAxisMax);
		const float SharedCross = FMath::Clamp(
			(ResultantCross + LoadCross) * 0.5f,
			bAlongX ? CenterMin.Y : CenterMin.X,
			bAlongX ? CenterMax.Y : CenterMax.X);
		const FVector2D WeakCenter = bAlongX
			? FVector2D(WeakAxis, SharedCross)
			: FVector2D(SharedCross, WeakAxis);
		const FVector2D PivotCenter =
			WeakCenter - Axis * Separation;
		TArray<FVector2D> FullCorners;
		AddSquareCorners(WeakCenter, Width, FullCorners);
		AddSquareCorners(PivotCenter, Width, FullCorners);
		const TArray<FVector2D> FullHull = BuildHull(FullCorners);
		const float WeakControlMargin = FMath::Min(
			Half - FMath::Abs(Resultant.X - WeakCenter.X),
			Half - FMath::Abs(Resultant.Y - WeakCenter.Y));
		if (!ContainsPoint(Resultant, FullHull)
			|| !ContainsPoint(LoadCenter, FullHull)
			|| HullMargin(Resultant, FullHull) + KINDA_SMALL_NUMBER
				< Intent.MinInitialSupportMarginCM
			|| WeakControlMargin + KINDA_SMALL_NUMBER
				< Intent.MinInitialSupportMarginCM)
		{
			OutError = FString::Printf(
				TEXT("DAG3BDualSupportContainmentFailed:WeakMargin=%.3f"),
				WeakControlMargin);
			return false;
		}
		const float PostFailureTipMargin =
			FVector2D::DotProduct(Resultant - PivotCenter, Axis) - Half;
		if (PostFailureTipMargin + KINDA_SMALL_NUMBER
			< Intent.MinPostFailureTipMarginCM)
		{
			OutError = FString::Printf(
				TEXT("DAG3BDualSupportTipMarginTooSmall:%.3f"),
				PostFailureTipMargin);
			return false;
		}
		OutSupport.RealizedColumnCenters.Add(WeakCenter);
		OutSupport.RealizedColumnRoles.Add(
			Intent.Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam
				? EABTSM73DAGRealizedColumnRole::FailureSeamKey
				: EABTSM73DAGRealizedColumnRole::FailureWeak);
		OutSupport.RealizedColumnCenters.Add(PivotCenter);
		OutSupport.RealizedColumnRoles.Add(
			EABTSM73DAGRealizedColumnRole::FailureStrongPivot);
		return true;
	}
}

bool FABTSM73DAGLoadSupportSolver::Solve(const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	const TMap<int32, TArray<FABTSM73DAGSelectedSupport>>& CandidatesByLoad,
	FABTSM73DAGSpatialLayout& InOutLayout, FString& OutError,
	const FABTSM73DAGFailureRewriteIntent* RewriteIntent) const
{
	TMap<int32, int32> LevelByMacro;
	for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes) LevelByMacro.Add(Macro.NodeId, 0);
	for (int32 Pass = 0; Pass < Graph.MacroNodes.Num(); ++Pass)
	{
		bool bChanged = false;
		for (const FABTSM73DAGSupportEdge& Edge : Graph.SupportEdges)
		{
			int32& LoadLevel = LevelByMacro.FindChecked(Edge.LoadNodeId);
			const int32 Required = LevelByMacro.FindRef(Edge.SupportNodeId) + 1;
			if (LoadLevel < Required) { LoadLevel = Required; bChanged = true; }
		}
		if (!bChanged) break;
	}

	TMap<int32, FLoadState> States;
	for (const FABTSM73DAGMacroLayout& Layout : InOutLayout.MacroLayouts)
	{
		const float Mass = FMath::Max(1.0f, Layout.PlateDimensionsCM.X * Layout.PlateDimensionsCM.Y * Layout.PlateDimensionsCM.Z);
		FLoadState& State = States.FindOrAdd(Layout.MacroNodeId);
		State.Mass = Mass;
		State.FirstMoment = FVector2D(Layout.PlateCenter) * Mass;
	}
	TArray<int32> LoadOrder;
	for (const FABTSM73DAGMacroLayout& Layout : InOutLayout.MacroLayouts) if (!Layout.bGroundTerminal) LoadOrder.Add(Layout.MacroNodeId);
	LoadOrder.Sort([&LevelByMacro](const int32 A, const int32 B) { return LevelByMacro.FindRef(A) > LevelByMacro.FindRef(B); });

	for (const int32 LoadId : LoadOrder)
	{
		const FABTSM73DAGMacroLayout* Load = FindLoadSupportLayout(InOutLayout, LoadId);
		const TArray<FABTSM73DAGSelectedSupport>* Candidates = CandidatesByLoad.Find(LoadId);
		if (Load == nullptr || Candidates == nullptr || Candidates->IsEmpty()) { OutError = FString::Printf(TEXT("DAGNoLoadSupportCandidates:%d"), LoadId); return false; }
		const FLoadState& LoadState = States.FindChecked(LoadId);
		const FVector2D Resultant = LoadState.FirstMoment / FMath::Max(1.0f, LoadState.Mass);
		// A Parallel refinement may leave the left and right feasible interfaces at
		// different provisional ranks. They are still a valid joint support group
		// when their physical column footprints jointly contain the resultant.
		TArray<FABTSM73DAGSelectedSupport> JointCandidates = *Candidates;
		JointCandidates.Sort([&Resultant, &InOutLayout](const FABTSM73DAGSelectedSupport& A, const FABTSM73DAGSelectedSupport& B)
		{
			const FABTSM73DAGMacroLayout* LayoutA = FindLoadSupportLayout(InOutLayout, A.SupportMacroNodeId);
			const FABTSM73DAGMacroLayout* LayoutB = FindLoadSupportLayout(InOutLayout, B.SupportMacroNodeId);
			const float DistanceA = LayoutA ? FVector2D::Distance(Resultant, FVector2D(LayoutA->PlateCenter)) : TNumericLimits<float>::Max();
			const float DistanceB = LayoutB ? FVector2D::Distance(Resultant, FVector2D(LayoutB->PlateCenter)) : TNumericLimits<float>::Max();
			return !FMath::IsNearlyEqual(DistanceA, DistanceB) ? DistanceA < DistanceB : A.SupportMacroNodeId < B.SupportMacroNodeId;
		});

		TArray<FABTSM73DAGSelectedSupport> BestSupports;
		float BestMargin = -1.0f;
		const bool bRewriteLoad = RewriteIntent != nullptr
			&& RewriteIntent->bEnabled
			&& RewriteIntent->LoadMacroNodeId == LoadId;
		const bool bRequireOrdinaryColumnRedundancy =
			RewriteIntent != nullptr
			&& RewriteIntent->bEnabled
			&& RewriteIntent->Pattern
				== EABTSM73DAGFailurePattern::InternalSingleSupport
			&& !bRewriteLoad;
		// DAG-4 showed that Single's intended one-column W was not unique:
		// removing one column from a wide ordinary tripod could drop 76% of
		// the main body. Dual/Seam already retain P as their mandatory
		// structural control, and their ordinary response is certified by the
		// dynamic comparison. Keep this stronger N-1 repair scoped to Single
		// so it does not erase the authored Tip/Seam response.
		if (bRewriteLoad)
		{
			const FABTSM73DAGSelectedSupport* RewriteCandidate =
				JointCandidates.FindByPredicate([RewriteIntent](
					const FABTSM73DAGSelectedSupport& Candidate)
				{
					return Candidate.SupportMacroNodeId
						== RewriteIntent->SupportMacroNodeId;
				});
			FABTSM73DAGSelectedSupport RewrittenSupport;
			if (RewriteCandidate == nullptr
				|| !MakeFailureRewriteSupport(
					*RewriteIntent,
					Settings,
					*Load,
					Resultant,
					RewriteCandidate != nullptr ? *RewriteCandidate
						: FABTSM73DAGSelectedSupport(),
					RewrittenSupport,
					OutError))
			{
				if (OutError.IsEmpty()) OutError = TEXT("DAG3BRewriteCandidateMissing");
				return false;
			}
			BestSupports.Add(MoveTemp(RewrittenSupport));
		}
		const int32 MaxCount = FMath::Min(Settings.MaxLogicalSupportsPerLoad, JointCandidates.Num());
		const int32 CombinationCount = 1 << JointCandidates.Num();
		for (int32 Mask = 1; !bRewriteLoad && Mask < CombinationCount; ++Mask)
		{
			if (FMath::CountBits(static_cast<uint32>(Mask)) > MaxCount) continue;
			TArray<FABTSM73DAGSelectedSupport> TrialSupports;
			for (int32 Index = 0; Index < JointCandidates.Num(); ++Index)
			{
				if ((Mask & (1 << Index)) == 0) continue;
				TrialSupports.Add(JointCandidates[Index]);
			}

			TArray<FVector2D> ContactCorners;
			bool bResolvedGeometry = false;
			// Each support can reduce at most twice:
			// Four/Tripod -> TwoColumn -> SingleColumn. A reduction widens every
			// remaining column, so different supports may fail on later passes.
			const int32 MaxResolvePasses = TrialSupports.Num() * 2 + 1;
			for (int32 ResolvePass = 0; ResolvePass < MaxResolvePasses; ++ResolvePass)
			{
				int32 TotalColumns = 0;
				for (const FABTSM73DAGSelectedSupport& Support : TrialSupports)
				{
					TotalColumns += PatternColumnCount(Support.SupportPattern);
				}
				// Leave deterministic headroom above the validator threshold;
				// the contact graph is rebuilt from floats and the exact configured
				// contact-area ratio is not robust.
				const float Width = FMath::Sqrt(1.05f * Settings.MinSupportContactAreaRatio
					* Load->PlateDimensionsCM.X * Load->PlateDimensionsCM.Y / FMath::Max(1, TotalColumns));
				if (Width > Settings.MaxAdaptiveColumnWidthCM + KINDA_SMALL_NUMBER) break;
				const float RealizedWidth = FMath::Max(Settings.MinAdaptiveColumnWidthCM, Width);
				ContactCorners.Reset();
				bool bPatternChanged = false;
				bool bPassFeasible = true;
				for (FABTSM73DAGSelectedSupport& Support : TrialSupports)
				{
					Support.RealizedColumnWidthCM = RealizedWidth;
					const bool bMadeCenters =
						bRequireOrdinaryColumnRedundancy
						? MakeResultantCenteredColumnCenters(
								Support.FeasibleColumnRegion,
								Settings,
								Support.SupportPattern,
								RealizedWidth,
								Resultant,
								Support.RealizedColumnCenters)
						: FABTSM73DAGSupportGeometry::
							MakeColumnCenters(
								Support.FeasibleColumnRegion,
								Settings,
								Support.SupportPattern,
								RealizedWidth,
								Support.RealizedColumnCenters);
					if (!bMadeCenters)
					{
						if (!TryResolveNarrowerPattern(Support, Settings, RealizedWidth))
						{
							bPassFeasible = false;
							break;
						}
						if (bRequireOrdinaryColumnRedundancy
							&& !MakeResultantCenteredColumnCenters(
								Support.FeasibleColumnRegion,
								Settings,
								Support.SupportPattern,
								RealizedWidth,
								Resultant,
								Support.RealizedColumnCenters))
						{
							bPassFeasible = false;
							break;
						}
						bPatternChanged = true;
					}
					for (const FVector2D& Center : Support.RealizedColumnCenters)
					{
						const float Half = RealizedWidth * 0.5f;
						ContactCorners.Add(Center + FVector2D(-Half, -Half));
						ContactCorners.Add(Center + FVector2D(Half, -Half));
						ContactCorners.Add(Center + FVector2D(-Half, Half));
						ContactCorners.Add(Center + FVector2D(Half, Half));
					}
				}
				if (!bPassFeasible) break;
				if (bPatternChanged) continue;
				bResolvedGeometry = true;
				break;
			}
			if (!bResolvedGeometry || ContactCorners.Num() < 4) continue;
			const TArray<FVector2D> Hull = BuildHull(MoveTemp(ContactCorners));
			if (!ContainsPoint(Resultant, Hull)) continue;
			const float Margin = HullMargin(Resultant, Hull);
			if (bRequireOrdinaryColumnRedundancy)
			{
				float SingleRemovalMargin = 0.0f;
				if (!HasColumnClearanceAcrossSupports(
					TrialSupports,
					Settings.ColumnClearanceCM)
					|| !ComputeMinimumSingleColumnRemovalMargin(
					Resultant,
					TrialSupports,
					SingleRemovalMargin)
					|| SingleRemovalMargin
						+ KINDA_SMALL_NUMBER
							< RewriteIntent->MinInitialSupportMarginCM)
				{
					continue;
				}
			}
			if (Margin > BestMargin + KINDA_SMALL_NUMBER)
			{
				BestMargin = Margin;
				BestSupports = MoveTemp(TrialSupports);
			}
		}
		if (BestSupports.IsEmpty())
		{
			for (const FABTSM73DAGSelectedSupport& Candidate : *Candidates)
			{
				UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M7.3-DAG2.3][JointReject] Load=%d Resultant=(%.1f,%.1f) Support=%d Level=%d Region=(%.1f,%.1f)-(%.1f,%.1f) Pattern=%d"),
					LoadId, Resultant.X, Resultant.Y, Candidate.SupportMacroNodeId,
					LevelByMacro.FindRef(Candidate.SupportMacroNodeId), Candidate.FeasibleColumnRegion.Min.X,
					Candidate.FeasibleColumnRegion.Min.Y, Candidate.FeasibleColumnRegion.Max.X,
					Candidate.FeasibleColumnRegion.Max.Y, static_cast<int32>(Candidate.SupportPattern));
			}
			OutError = FString::Printf(TEXT("DAGNoJointSupportHull:%d:Load=%.1f,%.1f Candidates=%d"), LoadId, Resultant.X, Resultant.Y, JointCandidates.Num());
			return false;
		}
		for (int32 SupportIndex = 0; SupportIndex < BestSupports.Num(); ++SupportIndex)
		{
			const FABTSM73DAGSelectedSupport& Support = BestSupports[SupportIndex];
			InOutLayout.SelectedSupports.Add(Support);
			FLoadState& LowerState = States.FindOrAdd(Support.SupportMacroNodeId);
			if (bRewriteLoad)
			{
				// DAG3-B owns exactly one rewritten logical interface. Its contact
				// hull may realize the same resultant through non-central pressure,
				// so preserve the complete accumulated force and first moment
				// rather than replacing them with an equal-per-column proxy.
				LowerState.Mass += LoadState.Mass;
				LowerState.FirstMoment += LoadState.FirstMoment;
				continue;
			}
			const float PerColumnMass = LoadState.Mass
				/ FMath::Max(1, Support.RealizedColumnCenters.Num() * BestSupports.Num());
			for (const FVector2D& Center : Support.RealizedColumnCenters)
			{
				LowerState.Mass += PerColumnMass;
				LowerState.FirstMoment += Center * PerColumnMass;
			}
		}
	}
	return !InOutLayout.SelectedSupports.IsEmpty();
}
