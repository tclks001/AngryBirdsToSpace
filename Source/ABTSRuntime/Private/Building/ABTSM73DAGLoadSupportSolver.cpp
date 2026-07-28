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
}

bool FABTSM73DAGLoadSupportSolver::Solve(const FABTSM73DAGGenerationResult& Graph,
	const FABTSM73DAGLayoutSettings& Settings,
	const TMap<int32, TArray<FABTSM73DAGSelectedSupport>>& CandidatesByLoad,
	FABTSM73DAGSpatialLayout& InOutLayout, FString& OutError) const
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
		const int32 MaxCount = FMath::Min(Settings.MaxLogicalSupportsPerLoad, JointCandidates.Num());
		const int32 CombinationCount = 1 << JointCandidates.Num();
		for (int32 Mask = 1; Mask < CombinationCount; ++Mask)
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
					if (!FABTSM73DAGSupportGeometry::MakeColumnCenters(
						Support.FeasibleColumnRegion,
						Settings,
						Support.SupportPattern,
						RealizedWidth,
						Support.RealizedColumnCenters))
					{
						if (!TryResolveNarrowerPattern(Support, Settings, RealizedWidth))
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
