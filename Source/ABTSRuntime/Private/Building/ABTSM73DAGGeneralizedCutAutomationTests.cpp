// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Algo/Reverse.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 AddGeneralizedCutNode(
		FABTSM73StructureData& Data,
		const FVector& Center,
		const FVector& Dimensions)
	{
		FABTSM73BrickNode& Node = Data.Bricks.AddDefaulted_GetRef();
		Node.NodeId = Data.Bricks.Num() - 1;
		Node.MacroNodeId = Node.NodeId;
		Node.Material = EABTSM7BuildingMaterial::Wood;
		Node.OriginalMaterial = Node.Material;
		Node.LocalCenter = Center;
		Node.DimensionsCM = Dimensions;
		Node.SemanticRole = EABTSM73BrickSemanticRole::Carrier;
		Node.bFailureFrontierMainBody = true;
		return Node.NodeId;
	}

	void AddGeneralizedCutEdge(
		FABTSM73StructureData& Data,
		const int32 LowerNodeId,
		const int32 UpperNodeId)
	{
		FABTSM73SupportEdge& Edge = Data.SupportEdges.AddDefaulted_GetRef();
		Edge.LowerNodeId = LowerNodeId;
		Edge.UpperNodeId = UpperNodeId;
		Edge.ContactAreaCM2 = 400.0f;
	}

	FABTSM73StructureData MakeRecursiveWovenFixture()
	{
		FABTSM73StructureData Data;
		const int32 Ground = AddGeneralizedCutNode(
			Data,
			FVector(0.0f, 0.0f, 10.0f),
			FVector(200.0f, 160.0f, 20.0f));
		const int32 StemLeft = AddGeneralizedCutNode(
			Data,
			FVector(-50.0f, 0.0f, 50.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 StemRight = AddGeneralizedCutNode(
			Data,
			FVector(50.0f, 0.0f, 50.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 WeaveLeft = AddGeneralizedCutNode(
			Data,
			FVector(-50.0f, 0.0f, 90.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 WeaveRight = AddGeneralizedCutNode(
			Data,
			FVector(50.0f, 0.0f, 90.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 RecursiveLeft = AddGeneralizedCutNode(
			Data,
			FVector(-50.0f, 0.0f, 130.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 RecursiveRight = AddGeneralizedCutNode(
			Data,
			FVector(50.0f, 0.0f, 130.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 Top = AddGeneralizedCutNode(
			Data,
			FVector(0.0f, 0.0f, 170.0f),
			FVector(100.0f, 80.0f, 20.0f));
		const int32 Cap = AddGeneralizedCutNode(
			Data,
			FVector(0.0f, 0.0f, 210.0f),
			FVector(100.0f, 80.0f, 20.0f));

		Data.GroundNodeIds.Add(Ground);
		AddGeneralizedCutEdge(Data, Ground, StemLeft);
		AddGeneralizedCutEdge(Data, Ground, StemRight);
		AddGeneralizedCutEdge(Data, StemLeft, WeaveLeft);
		AddGeneralizedCutEdge(Data, StemLeft, WeaveRight);
		AddGeneralizedCutEdge(Data, StemRight, WeaveLeft);
		AddGeneralizedCutEdge(Data, StemRight, WeaveRight);
		AddGeneralizedCutEdge(Data, WeaveLeft, RecursiveLeft);
		AddGeneralizedCutEdge(Data, WeaveLeft, RecursiveRight);
		AddGeneralizedCutEdge(Data, WeaveRight, RecursiveLeft);
		AddGeneralizedCutEdge(Data, WeaveRight, RecursiveRight);
		AddGeneralizedCutEdge(Data, RecursiveLeft, Top);
		AddGeneralizedCutEdge(Data, RecursiveRight, Top);
		AddGeneralizedCutEdge(Data, Top, Cap);
		Data.DAGMacroNodeCount = Data.Bricks.Num();
		Data.DAGSelectedSupportCount = Data.SupportEdges.Num();
		Data.DAGTopologyHash = 0xD3C00001u;
		return Data;
	}

	FABTSM73StructureData MakeSingleIncomingNonPhysicalEdgeFixture()
	{
		FABTSM73StructureData Data;
		const int32 Ground = AddGeneralizedCutNode(
			Data,
			FVector(0.0f, 0.0f, 10.0f),
			FVector(180.0f, 140.0f, 20.0f));
		const int32 SharedStem = AddGeneralizedCutNode(
			Data,
			FVector(0.0f, 0.0f, 50.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 TargetRoot = AddGeneralizedCutNode(
			Data,
			FVector(-45.0f, 0.0f, 90.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 SiblingRoot = AddGeneralizedCutNode(
			Data,
			FVector(45.0f, 0.0f, 90.0f),
			FVector(36.0f, 36.0f, 40.0f));
		const int32 TargetCap = AddGeneralizedCutNode(
			Data,
			FVector(-45.0f, 0.0f, 130.0f),
			FVector(70.0f, 60.0f, 20.0f));
		const int32 SiblingCap = AddGeneralizedCutNode(
			Data,
			FVector(45.0f, 0.0f, 130.0f),
			FVector(70.0f, 60.0f, 20.0f));

		Data.GroundNodeIds.Add(Ground);
		AddGeneralizedCutEdge(Data, Ground, SharedStem);
		AddGeneralizedCutEdge(Data, SharedStem, TargetRoot);
		AddGeneralizedCutEdge(Data, SharedStem, SiblingRoot);
		AddGeneralizedCutEdge(Data, TargetRoot, TargetCap);
		AddGeneralizedCutEdge(Data, SiblingRoot, SiblingCap);
		Data.DAGMacroNodeCount = Data.Bricks.Num();
		Data.DAGSelectedSupportCount = Data.SupportEdges.Num();
		Data.DAGTopologyHash = 0xD3C00002u;
		return Data;
	}

	FABTSM73DAGFailureFrontierSettings MakeGeneralizedCutSettings()
	{
		FABTSM73DAGFailureFrontierSettings Settings;
		Settings.bEnableAnalysis = true;
		Settings.bEnableGeneralizedSmallCutSearch = true;
		Settings.MaxCutSetSize = 4;
		Settings.MaxCandidateCount = 128;
		Settings.MaxFlowOperationCount = 8192;
		Settings.MinNormalizedHeight = 0.0f;
		Settings.MaxNormalizedHeight = 1.0f;
		Settings.MinMainBodyAffectedMassRatio = 0.0f;
		Settings.TargetMainBodyAffectedMassRatio = 0.45f;
		Settings.MaxMainBodyAffectedMassRatio = 1.0f;
		Settings.MinAffectedHeightSpanNormalized = 0.0f;
		Settings.MinAffectedMacroNodeCount = 1;
		return Settings;
	}

	const FABTSM73DAGFailureFrontierCandidate* FindGeneralizedCutCandidate(
		const FABTSM73DAGFailureFrontierAnalysis& Analysis,
		const EABTSM73DAGFailureCandidateKind Kind,
		TArray<int32> CandidateNodeIds)
	{
		CandidateNodeIds.Sort();
		return Analysis.Candidates.FindByPredicate(
			[Kind, &CandidateNodeIds](
				const FABTSM73DAGFailureFrontierCandidate& Candidate)
			{
				return Candidate.Kind == Kind
					&& Candidate.CandidateNodeIds == CandidateNodeIds;
			});
	}

	bool EqualGeneralizedCutCandidate(
		const FABTSM73DAGFailureFrontierCandidate& A,
		const FABTSM73DAGFailureFrontierCandidate& B)
	{
		return A.bAccepted == B.bAccepted
			&& A.bDirectedDominator == B.bDirectedDominator
			&& A.Kind == B.Kind
			&& A.CandidateNodeIds == B.CandidateNodeIds
			&& A.CandidateEdges == B.CandidateEdges
			&& A.ProtectedRootNodeIds == B.ProtectedRootNodeIds
			&& A.ExpectedAffectedNodeIds == B.ExpectedAffectedNodeIds
			&& A.AffectedMainBodyNodeIds == B.AffectedMainBodyNodeIds
			&& A.AffectedMacroNodeIds == B.AffectedMacroNodeIds
			&& A.NormalizedHeight == B.NormalizedHeight
			&& A.MainBodyAffectedMassRatio == B.MainBodyAffectedMassRatio
			&& A.AffectedHeightSpanNormalized
				== B.AffectedHeightSpanNormalized
			&& A.BypassSupportEdgeCount == B.BypassSupportEdgeCount
			&& A.FrontierHash == B.FrontierHash
			&& A.RejectReason == B.RejectReason;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CRecursiveWovenCutTest,
	"ABTS.M73DAG3.C.GeneralizedCut.RecursiveWoven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CRecursiveWovenCutTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73StructureData Data = MakeRecursiveWovenFixture();
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakeGeneralizedCutSettings();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis Analysis;
	FString Error;
	TestTrue(
		FString::Printf(
			TEXT("Recursive woven graph is analyzable: %s"),
			*Error),
		Analyzer.Analyze(Settings, Profiles, Data, Analysis, Error));

	const FABTSM73DAGFailureFrontierCandidate* SmallCut =
		FindGeneralizedCutCandidate(
			Analysis,
			EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut,
			{1, 2});
	TestNotNull(
		TEXT("Unit-capacity search finds the two physical stem bricks"),
		SmallCut);
	if (SmallCut != nullptr)
	{
		TestTrue(
			TEXT("The bounded physical node cut is complete and accepted"),
			SmallCut->bDirectedDominator && SmallCut->bAccepted);
		TestEqual(
			TEXT("The woven cut records all four stable boundary edges"),
			SmallCut->CandidateEdges.Num(),
			4);
		TestTrue(
			TEXT("The boundary exposes both first woven roots"),
			SmallCut->ProtectedRootNodeIds == TArray<int32>({3, 4}));
		TestNotEqual(
			TEXT("Generalized identity has a non-zero hash"),
			SmallCut->FrontierHash,
			0u);
	}

	const FABTSM73DAGFailureFrontierCandidate* EdgeCut =
		FindGeneralizedCutCandidate(
			Analysis,
			EABTSM73DAGFailureCandidateKind::DirectedEdgeCut,
			{7});
	TestNotNull(
		TEXT("The physical Top brick realizes the unique Top-to-Cap edge cut"),
		EdgeCut);
	if (EdgeCut != nullptr)
	{
		TestEqual(
			TEXT("The edge-cut identity contains exactly one stable edge"),
			EdgeCut->CandidateEdges.Num(),
			1);
		if (EdgeCut->CandidateEdges.Num() == 1)
		{
			TestEqual(
				TEXT("Edge-cut Lower identity is stable"),
				EdgeCut->CandidateEdges[0].LowerNodeId,
				7);
			TestEqual(
				TEXT("Edge-cut Upper identity is stable"),
				EdgeCut->CandidateEdges[0].UpperNodeId,
				8);
		}
	}

	FABTSM73DAGFailureFrontierSettings CutSizeOne = Settings;
	CutSizeOne.MaxCutSetSize = 1;
	FABTSM73DAGFailureFrontierAnalysis CutSizeOneAnalysis;
	Error.Reset();
	TestTrue(
		TEXT("A too-small cut bound skips woven cuts without truncation"),
		Analyzer.Analyze(
			CutSizeOne,
			Profiles,
			Data,
			CutSizeOneAnalysis,
			Error));
	TestNull(
		TEXT("No two-node cut leaks through a one-node hard bound"),
		FindGeneralizedCutCandidate(
			CutSizeOneAnalysis,
			EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut,
			{1, 2}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CSingleIncomingUpstreamCutTest,
	"ABTS.M73DAG3.C.GeneralizedCut.SingleIncomingUpstreamCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CSingleIncomingUpstreamCutTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73StructureData Data =
		MakeSingleIncomingNonPhysicalEdgeFixture();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;

	FABTSM73DAGFailureFrontierSettings LegacySettings =
		MakeGeneralizedCutSettings();
	LegacySettings.bEnableGeneralizedSmallCutSearch = false;
	FABTSM73DAGFailureFrontierAnalysis LegacyAnalysis;
	FString LegacyError;
	TestTrue(
		FString::Printf(
			TEXT("Legacy analysis remains valid with generalized search off: %s"),
			*LegacyError),
		Analyzer.Analyze(
			LegacySettings,
			Profiles,
			Data,
			LegacyAnalysis,
			LegacyError));
	TestNull(
		TEXT("Default-off analysis does not add a generalized small cut"),
		FindGeneralizedCutCandidate(
			LegacyAnalysis,
			EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut,
			{1}));
	const FABTSM73DAGFailureFrontierCandidate* LegacyNodeCut =
		FindGeneralizedCutCandidate(
			LegacyAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{1});
	TestNotNull(
		TEXT("Legacy shared-stem node cut remains present"),
		LegacyNodeCut);

	const FABTSM73DAGFailureFrontierSettings GeneralizedSettings =
		MakeGeneralizedCutSettings();
	FABTSM73DAGFailureFrontierAnalysis GeneralizedAnalysis;
	FString GeneralizedError;
	TestTrue(
		FString::Printf(
			TEXT("Generalized analysis accepts the single-incoming fixture: %s"),
			*GeneralizedError),
		Analyzer.Analyze(
			GeneralizedSettings,
			Profiles,
			Data,
			GeneralizedAnalysis,
			GeneralizedError));

	TestNull(
		TEXT("A branching Lower brick cannot masquerade as a direct edge cut"),
		FindGeneralizedCutCandidate(
			GeneralizedAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedEdgeCut,
			{1}));
	const FABTSM73DAGFailureFrontierCandidate* UpstreamCut =
		FindGeneralizedCutCandidate(
			GeneralizedAnalysis,
			EABTSM73DAGFailureCandidateKind::BoundedSmallNodeCut,
			{1});
	TestNotNull(
		TEXT("Min-cut still searches an indegree-one root when its direct edge is not physically realizable"),
		UpstreamCut);
	if (UpstreamCut != nullptr)
	{
		TestTrue(
			TEXT("The upstream physical cut is complete and accepted"),
			UpstreamCut->bDirectedDominator && UpstreamCut->bAccepted);
		TestTrue(
			TEXT("Removing the shared stem records both physical boundary edges"),
			UpstreamCut->CandidateEdges.Num() == 2);
		TestTrue(
			TEXT("Both branches are protected roots of the physical cut"),
			UpstreamCut->ProtectedRootNodeIds == TArray<int32>({2, 3}));
	}

	const FABTSM73DAGFailureFrontierCandidate* GeneralizedNodeCut =
		FindGeneralizedCutCandidate(
			GeneralizedAnalysis,
			EABTSM73DAGFailureCandidateKind::DirectedNodeCut,
			{1});
	TestNotNull(
		TEXT("Opt-in analysis preserves the legacy shared-stem node cut"),
		GeneralizedNodeCut);
	if (LegacyNodeCut != nullptr && GeneralizedNodeCut != nullptr)
	{
		TestEqual(
			TEXT("Generalized opt-in does not change the legacy candidate hash"),
			GeneralizedNodeCut->FrontierHash,
			LegacyNodeCut->FrontierHash);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CGeneralizedCutPermutationTest,
	"ABTS.M73DAG3.C.GeneralizedCut.PermutationDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CGeneralizedCutPermutationTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73DAGFailureFrontierSettings Settings =
		MakeGeneralizedCutSettings();
	const FABTSM73StructureData Forward = MakeRecursiveWovenFixture();
	FABTSM73StructureData Reversed = Forward;
	Algo::Reverse(Reversed.Bricks);
	Algo::Reverse(Reversed.SupportEdges);
	Algo::Reverse(Reversed.GroundNodeIds);

	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FABTSM73DAGFailureFrontierAnalysis ForwardAnalysis;
	FABTSM73DAGFailureFrontierAnalysis ReversedAnalysis;
	FString ForwardError;
	FString ReversedError;
	TestTrue(
		FString::Printf(TEXT("Forward graph succeeds: %s"), *ForwardError),
		Analyzer.Analyze(
			Settings,
			Profiles,
			Forward,
			ForwardAnalysis,
			ForwardError));
	TestTrue(
		FString::Printf(TEXT("Reversed graph succeeds: %s"), *ReversedError),
		Analyzer.Analyze(
			Settings,
			Profiles,
			Reversed,
			ReversedAnalysis,
			ReversedError));
	TestEqual(
		TEXT("Candidate count is independent of input order"),
		ForwardAnalysis.Candidates.Num(),
		ReversedAnalysis.Candidates.Num());
	TestEqual(
		TEXT("Accepted candidate count is independent of input order"),
		ForwardAnalysis.AcceptedCandidateCount,
		ReversedAnalysis.AcceptedCandidateCount);
	TestEqual(
		TEXT("Selected generalized frontier identity is deterministic"),
		ForwardAnalysis.SelectedFrontierHash,
		ReversedAnalysis.SelectedFrontierHash);
	for (int32 Index = 0;
		Index < ForwardAnalysis.Candidates.Num()
			&& ReversedAnalysis.Candidates.IsValidIndex(Index);
		++Index)
	{
		TestTrue(
			TEXT("Every node/edge candidate remains byte-semantically stable"),
			EqualGeneralizedCutCandidate(
				ForwardAnalysis.Candidates[Index],
				ReversedAnalysis.Candidates[Index]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG3CGeneralizedCutBudgetTest,
	"ABTS.M73DAG3.C.GeneralizedCut.BudgetFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG3CGeneralizedCutBudgetTest::RunTest(
	const FString& Parameters)
{
	const TArray<FABTSM7MaterialProfile> Profiles =
		FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	const FABTSM73StructureData Data = MakeRecursiveWovenFixture();
	FABTSM73DAGFailureFrontierAnalyzer Analyzer;
	FString Error;

	FABTSM73DAGFailureFrontierSettings FlowBudget =
		MakeGeneralizedCutSettings();
	FlowBudget.MaxFlowOperationCount = 64;
	FABTSM73DAGFailureFrontierAnalysis FlowBudgetAnalysis;
	TestFalse(
		TEXT("Flow work exceeding the deterministic ceiling fails closed"),
		Analyzer.Analyze(
			FlowBudget,
			Profiles,
			Data,
			FlowBudgetAnalysis,
			Error));
	TestTrue(
		TEXT("Flow ceiling failure is explicit"),
		Error.StartsWith(
			TEXT("DAG3GeneralizedCutFlowBudgetExceeded")));

	FABTSM73DAGFailureFrontierSettings CandidateBudget =
		MakeGeneralizedCutSettings();
	CandidateBudget.MaxCandidateCount = 7;
	FABTSM73DAGFailureFrontierAnalysis CandidateBudgetAnalysis;
	Error.Reset();
	TestFalse(
		TEXT("Generalized candidates cannot be silently truncated"),
		Analyzer.Analyze(
			CandidateBudget,
			Profiles,
			Data,
			CandidateBudgetAnalysis,
			Error));
	TestTrue(
		TEXT("Candidate ceiling failure is explicit"),
		Error.StartsWith(TEXT("DAG3CandidateBudgetExceeded")));
	return true;
}

#endif
