// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGLoadSupportSolver.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGTypes.h"

namespace
{
	struct FLoadState
	{
		float Mass = 0.0f;
		FVector2D FirstMoment = FVector2D::ZeroVector;
	};

	const FABTSM73DAGMacroLayout* FindLayout(const FABTSM73DAGSpatialLayout& Layout, const int32 MacroNodeId)
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

	bool CanFitPattern(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern, const float Width)
	{
		if (!Region.bIsValid) return false;
		const FVector2D Size = Region.GetSize();
		const float SingleAxis = Width + Settings.ColumnClearanceCM * 2.0f;
		const float PairAxis = Width * 2.0f + Settings.ColumnClearanceCM * 3.0f;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface: return Size.X >= SingleAxis && Size.Y >= SingleAxis;
		case EABTSM73DAGSupportPattern::TwoColumnLine: return FMath::Min(Size.X, Size.Y) >= SingleAxis && FMath::Max(Size.X, Size.Y) >= PairAxis;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
		case EABTSM73DAGSupportPattern::FourColumnFootprint: return Size.X >= PairAxis && Size.Y >= PairAxis;
		default: return false;
		}
	}

	bool MakeColumnCenters(const FBox2D& Region, const FABTSM73DAGLayoutSettings& Settings,
		const EABTSM73DAGSupportPattern Pattern, const float Width, TArray<FVector2D>& OutCenters)
	{
		OutCenters.Reset();
		if (!CanFitPattern(Region, Settings, Pattern, Width)) return false;
		const float Half = Width * 0.5f + Settings.ColumnClearanceCM;
		const FVector2D SafeMin = Region.Min + FVector2D(Half, Half);
		const FVector2D SafeMax = Region.Max - FVector2D(Half, Half);
		const FVector2D Center = (SafeMin + SafeMax) * 0.5f;
		const FVector2D Span = SafeMax - SafeMin;
		const float OffsetX = Span.X * 0.5f;
		const float OffsetY = Span.Y * 0.5f;
		switch (Pattern)
		{
		case EABTSM73DAGSupportPattern::SingleColumnInterface: OutCenters.Add(Center); return true;
		case EABTSM73DAGSupportPattern::TwoColumnLine:
			if (Span.X >= Span.Y) { OutCenters.Add(Center + FVector2D(-OffsetX, 0)); OutCenters.Add(Center + FVector2D(OffsetX, 0)); }
			else { OutCenters.Add(Center + FVector2D(0, -OffsetY)); OutCenters.Add(Center + FVector2D(0, OffsetY)); }
			return true;
		case EABTSM73DAGSupportPattern::ThreeColumnTripod:
			if (Span.X >= Span.Y)
			{
				OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY)); OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY)); OutCenters.Add(Center + FVector2D(0, OffsetY));
			}
			else
			{
				OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY)); OutCenters.Add(Center + FVector2D(-OffsetX, OffsetY)); OutCenters.Add(Center + FVector2D(OffsetX, 0));
			}
			return true;
		case EABTSM73DAGSupportPattern::FourColumnFootprint:
			OutCenters.Add(Center + FVector2D(-OffsetX, -OffsetY)); OutCenters.Add(Center + FVector2D(OffsetX, -OffsetY));
			OutCenters.Add(Center + FVector2D(-OffsetX, OffsetY)); OutCenters.Add(Center + FVector2D(OffsetX, OffsetY));
			return true;
		default: return false;
		}
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
		const FABTSM73DAGMacroLayout* Load = FindLayout(InOutLayout, LoadId);
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
			const FABTSM73DAGMacroLayout* LayoutA = FindLayout(InOutLayout, A.SupportMacroNodeId);
			const FABTSM73DAGMacroLayout* LayoutB = FindLayout(InOutLayout, B.SupportMacroNodeId);
			const float DistanceA = LayoutA ? FVector2D::Distance(Resultant, FVector2D(LayoutA->PlateCenter)) : TNumericLimits<float>::Max();
			const float DistanceB = LayoutB ? FVector2D::Distance(Resultant, FVector2D(LayoutB->PlateCenter)) : TNumericLimits<float>::Max();
			return !FMath::IsNearlyEqual(DistanceA, DistanceB) ? DistanceA < DistanceB : A.SupportMacroNodeId < B.SupportMacroNodeId;
		});

		TArray<FABTSM73DAGSelectedSupport> BestSupports;
		TArray<TArray<FVector2D>> BestCenters;
		float BestMargin = -1.0f;
		const int32 MaxCount = FMath::Min(Settings.MaxLogicalSupportsPerLoad, JointCandidates.Num());
		const int32 CombinationCount = 1 << JointCandidates.Num();
		for (int32 Mask = 1; Mask < CombinationCount; ++Mask)
		{
			if (FMath::CountBits(static_cast<uint32>(Mask)) > MaxCount) continue;
			int32 TotalColumns = 0;
			for (int32 Index = 0; Index < JointCandidates.Num(); ++Index) if ((Mask & (1 << Index)) != 0) TotalColumns += PatternColumnCount(JointCandidates[Index].SupportPattern);
			// Leave deterministic headroom above the validator threshold; the
			// contact graph is rebuilt from floats and exact 4% is not robust.
			const float Width = FMath::Sqrt(1.05f * Settings.MinSupportContactAreaRatio
				* Load->PlateDimensionsCM.X * Load->PlateDimensionsCM.Y / FMath::Max(1, TotalColumns));
			if (Width > Settings.MaxAdaptiveColumnWidthCM + KINDA_SMALL_NUMBER) continue;
			const float RealizedWidth = FMath::Max(Settings.MinAdaptiveColumnWidthCM, Width);
			TArray<FABTSM73DAGSelectedSupport> TrialSupports;
			TArray<TArray<FVector2D>> TrialCenters;
			TArray<FVector2D> ContactCorners;
			bool bFeasible = true;
			for (int32 Index = 0; Index < JointCandidates.Num(); ++Index)
			{
				if ((Mask & (1 << Index)) == 0) continue;
				FABTSM73DAGSelectedSupport Support = JointCandidates[Index];
				Support.RealizedColumnWidthCM = RealizedWidth;
				TArray<FVector2D>& Centers = TrialCenters.AddDefaulted_GetRef();
				if (!MakeColumnCenters(Support.FeasibleColumnRegion, Settings, Support.SupportPattern, RealizedWidth, Centers)) { bFeasible = false; break; }
				TrialSupports.Add(Support);
				for (const FVector2D& Center : Centers)
				{
					const float Half = RealizedWidth * 0.5f;
					ContactCorners.Add(Center + FVector2D(-Half, -Half)); ContactCorners.Add(Center + FVector2D(Half, -Half));
					ContactCorners.Add(Center + FVector2D(-Half, Half)); ContactCorners.Add(Center + FVector2D(Half, Half));
				}
			}
			if (!bFeasible || ContactCorners.Num() < 4) continue;
			const TArray<FVector2D> Hull = BuildHull(MoveTemp(ContactCorners));
			if (!ContainsPoint(Resultant, Hull)) continue;
			const float Margin = HullMargin(Resultant, Hull);
			if (Margin > BestMargin + KINDA_SMALL_NUMBER)
			{
				BestMargin = Margin;
				BestSupports = MoveTemp(TrialSupports);
				BestCenters = MoveTemp(TrialCenters);
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
			const float PerColumnMass = LoadState.Mass / FMath::Max(1, BestCenters[SupportIndex].Num() * BestSupports.Num());
			for (const FVector2D& Center : BestCenters[SupportIndex])
			{
				LowerState.Mass += PerColumnMass;
				LowerState.FirstMoment += Center * PerColumnMass;
			}
		}
	}
	return !InOutLayout.SelectedSupports.IsEmpty();
}
