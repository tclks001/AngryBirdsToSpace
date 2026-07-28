// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Algo/Reverse.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGContactGraphBuilder.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
#include "Building/ABTSM73DAGFailureFrontierTypes.h"
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

#endif
