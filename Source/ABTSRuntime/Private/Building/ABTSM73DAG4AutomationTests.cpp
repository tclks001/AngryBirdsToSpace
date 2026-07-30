// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Algo/Reverse.h"
#include "Building/ABTSM73DAG4ResponseEvaluator.h"
#include "Building/ABTSM73DAG4SettledContactValidator.h"
#include "Building/ABTSM73DAG4TrialPlanner.h"
#include "Game/ABTSM7GameMode.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FABTSM73DAG4ValidationSettings MakeDAG4TestSettings()
	{
		FABTSM73DAG4ValidationSettings Settings;
		Settings.bEnableSettledChaosValidation = true;
		Settings.ContactGapToleranceCM = 3.0f;
		Settings.ContactPenetrationToleranceCM = 3.0f;
		Settings.MinContactPatchAreaCM2 = 4.0f;
		Settings.MinRequiredContactAreaRetention = 0.35f;
		Settings.NonWeakProbeCount = 3;
		Settings.MaxOrdinaryPredictedAffectedMassRatio = 0.25f;
		Settings.TrialDurationSeconds = 1.0f;
		Settings.TrialWarmupSeconds = 0.1f;
		Settings.SignificantDisplacementCM = 20.0f;
		Settings.SignificantRotationDegrees = 6.0f;
		Settings.MinWeakAffectedMassRatio = 0.20f;
		Settings.MaxWeakAffectedMassRatio = 0.80f;
		Settings.MinPredictedAffectedRealizationRatio = 0.50f;
		Settings.MaxOrdinaryAffectedMassRatio = 0.25f;
		Settings.MinWeakResponseAdvantage = 1.50f;
		Settings.MinWeakAbsoluteAffectedMassAdvantage = 0.10f;
		Settings.MinFailureDirectionAlignment = 0.15f;
		Settings.MinWeakResponseScore = 0.20f;
		Settings.MaxSettledBodyCount = 64;
		Settings.MaxContactPairQueryCount = 4096;
		Settings.MaxTrialCount = 4;
		Settings.MaxTrialTickCount = 120;
		Settings.MaxTotalValidationSeconds = 5.0f;
		Settings.MaxContactEventCount = 64;
		return Settings;
	}

	FABTSM73DAG4SettledNode MakeSettledNode(
		const int32 NodeId,
		const FVector& Location,
		const FVector& Dimensions,
		const FQuat& Rotation = FQuat::Identity,
		const bool bMainBody = true)
	{
		FABTSM73DAG4SettledNode Node;
		Node.NodeId = NodeId;
		Node.MacroNodeId = NodeId;
		Node.Material = EABTSM7BuildingMaterial::Wood;
		Node.DimensionsCM = Dimensions;
		Node.LocalTransform = FTransform(
			Rotation.GetNormalized(),
			Location,
			FVector::OneVector);
		Node.Mass =
			static_cast<double>(Dimensions.X)
			* Dimensions.Y
			* Dimensions.Z;
		Node.bMainBody = bMainBody;
		return Node;
	}

	void AddSupportEdge(
		TArray<FABTSM73SupportEdge>& Edges,
		const int32 LowerNodeId,
		const int32 UpperNodeId,
		const float AreaCM2 = 400.0f)
	{
		FABTSM73SupportEdge& Edge = Edges.AddDefaulted_GetRef();
		Edge.LowerNodeId = LowerNodeId;
		Edge.UpperNodeId = UpperNodeId;
		Edge.ContactAreaCM2 = AreaCM2;
	}

	FABTSM73DAG4SettledContact MakeSettledContact(
		const FABTSM73SupportEdge& Edge)
	{
		FABTSM73DAG4SettledContact Contact;
		Contact.LowerNodeId = Edge.LowerNodeId;
		Contact.UpperNodeId = Edge.UpperNodeId;
		Contact.ContactAreaCM2 = Edge.ContactAreaCM2;
		Contact.SignedGapCM = 0.0f;
		return Contact;
	}

	FABTSM73DAG4SettledContactInput MakeSettledDualFixture()
	{
		const FQuat FoundationYaw(
			FVector::UpVector,
			FMath::DegreesToRadians(1.0f));
		const FQuat PivotYaw(
			FVector::UpVector,
			FMath::DegreesToRadians(-1.5f));
		const FQuat LoadPitch(
			FVector::RightVector,
			FMath::DegreesToRadians(0.2f));
		const FQuat PayloadYaw(
			FVector::UpVector,
			FMath::DegreesToRadians(2.0f));

		FABTSM73DAG4SettledContactInput Input;
		Input.Nodes =
		{
			MakeSettledNode(
				0,
				FVector(0.0f, 0.0f, 10.2f),
				FVector(220.0f, 160.0f, 20.0f),
				FoundationYaw),
			MakeSettledNode(
				1,
				FVector(-35.0f, 0.0f, 40.3f),
				FVector(30.0f, 40.0f, 40.0f)),
			MakeSettledNode(
				2,
				FVector(35.0f, 0.0f, 40.3f),
				FVector(30.0f, 40.0f, 40.0f),
				PivotYaw),
			MakeSettledNode(
				3,
				FVector(-10.0f, 0.0f, 70.4f),
				FVector(160.0f, 120.0f, 20.0f),
				LoadPitch),
			MakeSettledNode(
				4,
				FVector(-25.0f, 0.0f, 90.4f),
				FVector(80.0f, 80.0f, 20.0f),
				PayloadYaw)
		};
		AddSupportEdge(Input.BaselineAllowedContacts, 0, 1);
		AddSupportEdge(Input.BaselineAllowedContacts, 0, 2);
		AddSupportEdge(Input.BaselineAllowedContacts, 1, 3);
		AddSupportEdge(Input.BaselineAllowedContacts, 2, 3);
		AddSupportEdge(Input.BaselineAllowedContacts, 3, 4);
		Input.RequiredContacts = Input.BaselineAllowedContacts;
		Input.BaselineGroundNodeIds = {0};
		Input.WeakNodeIds = {1};
		Input.RemainingSupportNodeIds = {2};
		Input.ExpectedAffectedNodeIds = {3, 4};
		Input.ExpectedAffectedMainBodyNodeIds = {3, 4};
		Input.LoadPlateNodeId = 3;
		Input.RealizedPatternHash = 0xD4A60001u;
		Input.Pattern =
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport;
		Input.ExpectedMotion = EABTSM73DAGFailureMotion::Tip;
		Input.ExpectedFailureDirectionLocal = -FVector::ForwardVector;
		Input.MinInitialSupportMarginCM = 2.0f;
		Input.MinPostFailureTipMarginCM = 8.0f;
		Input.MaxReseatRisk = 0.35f;
		return Input;
	}

	FABTSM73DAG4TrialPlanningInput MakePlannerFixture()
	{
		const FABTSM73DAG4SettledContactInput Settled =
			MakeSettledDualFixture();
		FABTSM73DAG4TrialPlanningInput Input;
		Input.Nodes = Settled.Nodes;
		for (const FABTSM73SupportEdge& Edge
			: Settled.BaselineAllowedContacts)
		{
			Input.Contacts.Add(MakeSettledContact(Edge));
		}
		Input.GroundNodeIds = Settled.BaselineGroundNodeIds;
		Input.WeakNodeIds = Settled.WeakNodeIds;
		Input.RemainingSupportNodeIds =
			Settled.RemainingSupportNodeIds;
		Input.ExpectedAffectedMainBodyNodeIds =
			Settled.ExpectedAffectedMainBodyNodeIds;
		Input.AttackDirectionLocal = FVector::RightVector;
		Input.ProjectileRadiusCM = 1.0f;
		Input.AttackApproachDistanceCM = 500.0f;
		return Input;
	}

	bool ArePlansEqual(
		const TConstArrayView<FABTSM73DAG4TrialPlan> A,
		const TConstArrayView<FABTSM73DAG4TrialPlan> B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Kind != B[Index].Kind
				|| A[Index].ProbeIndex != B[Index].ProbeIndex
				|| A[Index].RemovedNodeIds
					!= B[Index].RemovedNodeIds
				|| A[Index].PredictedAffectedMainBodyNodeIds
					!= B[Index].PredictedAffectedMainBodyNodeIds
				|| !FMath::IsNearlyEqual(
					A[Index].PredictedAffectedMainBodyMassRatio,
					B[Index].PredictedAffectedMainBodyMassRatio))
			{
				return false;
			}
		}
		return true;
	}

	FABTSM73DAG4NodeOutcome MakeStationaryOutcome(
		const int32 NodeId)
	{
		FABTSM73DAG4NodeOutcome Outcome;
		Outcome.NodeId = NodeId;
		return Outcome;
	}

	FABTSM73DAG4NodeOutcome MakeFallingOutcome(
		const int32 NodeId)
	{
		FABTSM73DAG4NodeOutcome Outcome;
		Outcome.NodeId = NodeId;
		Outcome.FinalDisplacementLocal =
			FVector(0.0f, 0.0f, -40.0f);
		Outcome.FinalRotationDegrees = 8.0f;
		Outcome.MaxDisplacementCM = 40.0f;
		Outcome.MaxRotationDegrees = 8.0f;
		Outcome.MaxDropDistanceCM = 40.0f;
		return Outcome;
	}

	FABTSM73DAG4TrialEvaluationInput MakeWeakEvaluationFixture()
	{
		const FABTSM73DAG4TrialPlanningInput Planning =
			MakePlannerFixture();
		FABTSM73DAG4TrialEvaluationInput Input;
		Input.Nodes = Planning.Nodes;
		Input.SettledContacts = Planning.Contacts;
		Input.Plan.Kind = EABTSM73DAG4TrialKind::WeakPoint;
		Input.Plan.ProbeIndex = 0;
		Input.Plan.RemovedNodeIds = {1};
		Input.Plan.PredictedAffectedMainBodyNodeIds = {3, 4};
		double TotalMainBodyMass = 0.0;
		double PredictedAffectedMainBodyMass = 0.0;
		for (const FABTSM73DAG4SettledNode& Node : Input.Nodes)
		{
			if (!Node.bMainBody)
			{
				continue;
			}
			TotalMainBodyMass += Node.Mass;
			if (Input.Plan.PredictedAffectedMainBodyNodeIds.Contains(
				Node.NodeId))
			{
				PredictedAffectedMainBodyMass += Node.Mass;
			}
		}
		Input.Plan.PredictedAffectedMainBodyMassRatio =
			static_cast<float>(
				PredictedAffectedMainBodyMass
				/ TotalMainBodyMass);
		Input.Outcomes =
		{
			MakeStationaryOutcome(0),
			MakeStationaryOutcome(2),
			MakeFallingOutcome(3),
			MakeFallingOutcome(4)
		};
		// The first two entries are the same external pair in reverse order.
		// The evaluator must canonicalize it and count it only once.
		Input.SecondaryContactNodePairs =
		{
			3, INDEX_NONE,
			INDEX_NONE, 3,
			4, 2
		};
		Input.ExpectedMotion = EABTSM73DAGFailureMotion::Drop;
		Input.ExpectedFailureDirectionLocal = FVector::ZeroVector;
		Input.DurationSeconds = 1.0f;
		Input.TickCount = 30;
		return Input;
	}

	FABTSM73DAG4TrialMetrics MakeOrdinaryMetrics(
		const int32 ProbeIndex,
		const int32 RemovedNodeId)
	{
		FABTSM73DAG4TrialMetrics Metrics;
		Metrics.Kind = EABTSM73DAG4TrialKind::Ordinary;
		Metrics.ProbeIndex = ProbeIndex;
		Metrics.RemovedNodeIds = {RemovedNodeId};
		Metrics.bCompleted = true;
		Metrics.DurationSeconds = 1.0f;
		Metrics.TickCount = 30;
		Metrics.PredictedAffectedMainBodyMassRatio = 0.05f;
		Metrics.AffectedMainBodyMassRatio = 0.05f;
		Metrics.PredictedAffectedRealizationRatio = 0.0f;
		Metrics.MaxDisplacementCM = 5.0f;
		Metrics.MaxRotationDegrees = 1.0f;
		Metrics.MaxDropDistanceCM = 5.0f;
		Metrics.MaxExpectedDirectionSlideCM = 5.0f;
		Metrics.PropagationDepth = 1;
		Metrics.DirectionAlignment = 0.0f;
		Metrics.SecondaryContactCount = 0;
		Metrics.ResponseScore = 0.05f;
		return Metrics;
	}

	TArray<FABTSM73DAG4TrialMetrics> MakeComparisonFixture(
		const FABTSM73DAG4TrialMetrics& WeakMetrics)
	{
		return
		{
			WeakMetrics,
			MakeOrdinaryMetrics(0, 2),
			MakeOrdinaryMetrics(1, 3),
			MakeOrdinaryMetrics(2, 4)
		};
	}

	FABTSM73DAG4ValidationResult MakeSettledAcceptedResult()
	{
		FABTSM73DAG4ValidationResult Result;
		Result.bEnabled = true;
		Result.bSettledContactAccepted = true;
		Result.BaselineContactHash = 0x1234;
		Result.SettledContactHash = 0x5678;
		return Result;
	}

	FABTSM7TaskGraphBuildingProfile MakeExplicitDAG4Profile()
	{
		FABTSM7TaskGraphBuildingProfile Profile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				EABTSM3TaskType::TargetBuilding,
				EABTSM7BuildingMaterial::Stone);
		Profile.DAGFailureFrontierSettings.bEnableAnalysis = true;
		Profile.DAGFailureFrontierSettings
			.bEnableGeneralizedSmallCutSearch = true;
		Profile.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
		Profile.DAGFailurePlayabilitySettings
			.bEnablePlayabilityRouting = true;
		Profile.DAG4ValidationSettings
			.bEnableSettledChaosValidation = true;
		Profile.DAG4ValidationSettings.NonWeakProbeCount = 5;
		Profile.DAG4ValidationSettings.MaxTrialCount = 6;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG4SettledContactAutomationTest,
	"ABTS.M73DAG4.SettledContact.RebuildAndFrontierAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG4SettledContactAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM73DAG4ValidationSettings Settings =
		MakeDAG4TestSettings();
	const FABTSM73DAG4SettledContactInput Fixture =
		MakeSettledDualFixture();
	FABTSM73DAG4SettledContactValidator Validator;
	FABTSM73DAG4SettledContactResult Result;
	FString Error;

	TestTrue(
		FString::Printf(
			TEXT("Small settled translation/OBB rotation remains valid: %s"),
			*Error),
		Validator.RebuildAndValidate(
			Settings,
			Fixture,
			Result,
			Error));
	TestTrue(TEXT("Settled contact hash is non-zero"),
		Result.SettledContactHash != 0);
	TestTrue(TEXT("Baseline contact hash is non-zero"),
		Result.BaselineContactHash != 0);
	TestEqual(TEXT("All five generated contacts are rebuilt"),
		Result.Contacts.Num(), 5);
	TestTrue(TEXT("Foundation is the only ground root"),
		Result.GroundNodeIds == TArray<int32>({0}));
	TestTrue(TEXT("Initial full support margin remains certified"),
		Result.InitialSupportMarginCM
			>= Fixture.MinInitialSupportMarginCM);
	TestTrue(TEXT("Remaining pivot produces the intended tip margin"),
		Result.PostFailureTipMarginCM
			>= Fixture.MinPostFailureTipMarginCM);
	TestTrue(TEXT("Remaining pivot prevents vertical reseat"),
		Result.ReseatRisk <= Fixture.MaxReseatRisk);
	TestTrue(TEXT("No settled Frontier bypass is present"),
		Result.FrontierBypassNodeIds.IsEmpty());

	FABTSM73DAG4SettledContactInput Reversed = Fixture;
	Algo::Reverse(Reversed.Nodes);
	Algo::Reverse(Reversed.BaselineAllowedContacts);
	Algo::Reverse(Reversed.RequiredContacts);
	Algo::Reverse(Reversed.ExpectedAffectedNodeIds);
	Algo::Reverse(Reversed.ExpectedAffectedMainBodyNodeIds);
	FABTSM73DAG4SettledContactResult ReversedResult;
	Error.Reset();
	TestTrue(
		FString::Printf(
			TEXT("Input order does not change settled acceptance: %s"),
			*Error),
		Validator.RebuildAndValidate(
			Settings,
			Reversed,
			ReversedResult,
			Error));
	TestEqual(TEXT("Baseline hash is canonical"),
		ReversedResult.BaselineContactHash,
		Result.BaselineContactHash);
	TestEqual(TEXT("Settled hash is canonical"),
		ReversedResult.SettledContactHash,
		Result.SettledContactHash);

	FABTSM73DAG4SettledContactInput MicroSettled = Fixture;
	MicroSettled.Nodes[4].LocalTransform.AddToTranslation(
		FVector(0.002f, -0.002f, 0.001f));
	FABTSM73DAG4SettledContactResult MicroSettledResult;
	Error.Reset();
	TestTrue(
		TEXT("Sub-centimeter Chaos settling keeps the topology valid"),
		Validator.RebuildAndValidate(
			Settings,
			MicroSettled,
			MicroSettledResult,
			Error));
	TestEqual(
		TEXT("Settled topology hash ignores sub-centimeter jitter"),
		MicroSettledResult.SettledContactHash,
		Result.SettledContactHash);

	FABTSM73DAG4SettledContactInput HelperWeighted = Fixture;
	HelperWeighted.Nodes.Add(MakeSettledNode(
		5,
		FVector(-200.0f, 0.0f, 110.4f),
		FVector(400.0f, 40.0f, 20.0f),
		FQuat::Identity,
		false));
	AddSupportEdge(
		HelperWeighted.BaselineAllowedContacts,
		4,
		5);
	AddSupportEdge(
		HelperWeighted.RequiredContacts,
		4,
		5);
	HelperWeighted.ExpectedAffectedNodeIds.Add(5);
	FABTSM73DAG4SettledContactResult HelperRejected;
	Error.Reset();
	TestFalse(
		TEXT("Non-main-body helper mass participates in mechanical COM"),
		Validator.RebuildAndValidate(
			Settings,
			HelperWeighted,
			HelperRejected,
			Error));
	TestTrue(
		TEXT("Helper-weighted COM rejection is explicit"),
		Error.StartsWith(
			TEXT("DAG4SettledInitialSupportMarginTooSmall")));

	FABTSM73DAG4SettledContactInput RequiredMissing = Fixture;
	RequiredMissing.Nodes[4].LocalTransform.AddToTranslation(
		FVector(0.0f, 0.0f, 8.0f));
	FABTSM73DAG4SettledContactResult Rejected;
	Rejected.Contacts.AddDefaulted();
	Rejected.SettledContactHash = 99;
	Error.Reset();
	TestFalse(TEXT("A missing Required settled contact rejects"),
		Validator.RebuildAndValidate(
			Settings,
			RequiredMissing,
			Rejected,
			Error));
	TestTrue(TEXT("Required rejection reason is explicit"),
		Error.StartsWith(TEXT("DAG4SettledRequiredContactMissing")));
	TestTrue(TEXT("Required rejection clears partial contacts"),
		Rejected.Contacts.IsEmpty());
	TestEqual(TEXT("Required rejection clears stale hash"),
		Rejected.SettledContactHash, 0u);

	FABTSM73DAG4SettledContactInput Bypass = Fixture;
	Bypass.Nodes.Add(MakeSettledNode(
		5,
		FVector(0.0f, 0.0f, 40.3f),
		FVector(20.0f, 20.0f, 40.0f),
		FQuat::Identity,
		false));
	Error.Reset();
	TestFalse(TEXT("A new settled path around W and P rejects"),
		Validator.RebuildAndValidate(
			Settings,
			Bypass,
			Rejected,
			Error));
	TestTrue(TEXT("New Frontier bypass reason is explicit"),
		Error.StartsWith(TEXT("DAG4SettledFrontierBypass")));
	TestTrue(TEXT("Bypass rejection remains atomic"),
		Rejected.Contacts.IsEmpty()
			&& Rejected.FrontierBypassNodeIds.IsEmpty()
			&& Rejected.PairQueryCount == 0);

	FABTSM73DAG4SettledContactInput DuplicateIdentity = Fixture;
	DuplicateIdentity.Nodes.Last().NodeId = 3;
	Error.Reset();
	TestFalse(TEXT("Duplicate settled identity rejects"),
		Validator.RebuildAndValidate(
			Settings,
			DuplicateIdentity,
			Rejected,
			Error));
	TestTrue(TEXT("Identity rejection reason is explicit"),
		Error.StartsWith(TEXT("DAG4SettledNodeInvalid")));
	TestTrue(TEXT("Identity rejection publishes no partial result"),
		Rejected.Contacts.IsEmpty()
			&& Rejected.BaselineContactHash == 0
			&& Rejected.SettledContactHash == 0);

	FABTSM73DAG4ValidationSettings BodyBudget = Settings;
	BodyBudget.MaxSettledBodyCount = 4;
	Rejected.Contacts.AddDefaulted();
	Rejected.SettledContactHash = 123;
	Error.Reset();
	TestFalse(TEXT("Settled body budget is hard"),
		Validator.RebuildAndValidate(
			BodyBudget,
			Fixture,
			Rejected,
			Error));
	TestTrue(TEXT("Body budget reason is explicit"),
		Error.StartsWith(TEXT("DAG4SettledBodyBudgetInvalid")));
	TestTrue(TEXT("Body budget failure clears stale result"),
		Rejected.Contacts.IsEmpty()
			&& Rejected.SettledContactHash == 0
			&& Rejected.PairQueryCount == 0);

	FABTSM73DAG4ValidationSettings PairBudget = Settings;
	PairBudget.MaxContactPairQueryCount = 9;
	Error.Reset();
	TestFalse(TEXT("Settled pair-query budget is hard"),
		Validator.RebuildAndValidate(
			PairBudget,
			Fixture,
			Rejected,
			Error));
	TestTrue(TEXT("Pair budget reason is explicit"),
		Error.StartsWith(TEXT("DAG4SettledPairBudgetExceeded")));
	TestTrue(TEXT("Pair budget failure is atomic"),
		Rejected.Contacts.IsEmpty()
			&& Rejected.PairQueryCount == 0);

	FABTSM73DAG4ValidationSettings Disabled = Settings;
	Disabled.bEnableSettledChaosValidation = false;
	Rejected.Contacts.AddDefaulted();
	Error = TEXT("stale");
	TestTrue(TEXT("Disabled settled validator is a no-op"),
		Validator.RebuildAndValidate(
			Disabled,
			FABTSM73DAG4SettledContactInput(),
			Rejected,
			Error));
	TestTrue(TEXT("Disabled settled validator clears stale output"),
		Rejected.Contacts.IsEmpty()
			&& Rejected.RejectReason.IsEmpty()
			&& Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG4PlannerAutomationTest,
	"ABTS.M73DAG4.Planner.OrdinaryProbeDeterminismAndBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG4PlannerAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM73DAG4ValidationSettings Settings =
		MakeDAG4TestSettings();
	const FABTSM73DAG4TrialPlanningInput Fixture =
		MakePlannerFixture();
	FABTSM73DAG4TrialPlanner Planner;
	TArray<FABTSM73DAG4TrialPlan> Plans;
	FString Error;
	TestTrue(
		FString::Printf(TEXT("Planner builds the bounded contrast set: %s"),
			*Error),
		Planner.BuildPlans(Settings, Fixture, Plans, Error));
	TestEqual(TEXT("One weak plus three ordinary trials are planned"),
		Plans.Num(), 4);
	if (Plans.Num() == 4)
	{
		TestEqual(TEXT("Weak trial is always first"),
			Plans[0].Kind, EABTSM73DAG4TrialKind::WeakPoint);
		TestTrue(TEXT("Weak trial removes W only"),
			Plans[0].RemovedNodeIds == TArray<int32>({1}));
		TestEqual(TEXT("First ordinary trial is ordinary"),
			Plans[1].Kind, EABTSM73DAG4TrialKind::Ordinary);
		TestTrue(TEXT("P is the mandatory first ordinary probe"),
			Plans[1].RemovedNodeIds == TArray<int32>({2}));
		for (int32 PlanIndex = 1; PlanIndex < Plans.Num(); ++PlanIndex)
		{
			TestEqual(
				FString::Printf(
					TEXT("Ordinary plan %d removes one node"),
					PlanIndex),
				Plans[PlanIndex].RemovedNodeIds.Num(),
				1);
		}
	}

	FABTSM73DAG4TrialPlanningInput Reversed = Fixture;
	Algo::Reverse(Reversed.Nodes);
	Algo::Reverse(Reversed.Contacts);
	Algo::Reverse(Reversed.ExpectedAffectedMainBodyNodeIds);
	TArray<FABTSM73DAG4TrialPlan> ReversedPlans;
	Error.Reset();
	TestTrue(TEXT("Reversed planner input remains valid"),
		Planner.BuildPlans(
			Settings,
			Reversed,
			ReversedPlans,
			Error));
	TestTrue(TEXT("Reversed planner input produces the same plans"),
		ArePlansEqual(Plans, ReversedPlans));

	FABTSM73DAG4ValidationSettings ShortageSettings = Settings;
	ShortageSettings.NonWeakProbeCount = 4;
	ShortageSettings.MaxTrialCount = 5;
	TArray<FABTSM73DAG4TrialPlan> RejectedPlans;
	RejectedPlans.AddDefaulted();
	Error.Reset();
	TestFalse(TEXT("Insufficient ordinary candidates fail closed"),
		Planner.BuildPlans(
			ShortageSettings,
			Fixture,
			RejectedPlans,
			Error));
	TestTrue(TEXT("Candidate shortage reason is explicit"),
		Error.StartsWith(
			TEXT("DAG4PlannerOrdinaryCandidateShortage")));
	TestTrue(TEXT("Candidate shortage publishes no partial plan"),
		RejectedPlans.IsEmpty());

	FABTSM73DAG4ValidationSettings BudgetSettings = Settings;
	BudgetSettings.MaxTrialCount = 3;
	RejectedPlans.AddDefaulted();
	Error.Reset();
	TestFalse(TEXT("Trial count budget fails before planning"),
		Planner.BuildPlans(
			BudgetSettings,
			Fixture,
			RejectedPlans,
			Error));
	TestTrue(TEXT("Trial budget reason is explicit"),
		Error.StartsWith(TEXT("DAG4PlannerTrialBudgetExceeded")));
	TestTrue(TEXT("Trial budget failure clears stale plans"),
		RejectedPlans.IsEmpty());

	FABTSM73DAG4ValidationSettings Disabled = Settings;
	Disabled.bEnableSettledChaosValidation = false;
	RejectedPlans.AddDefaulted();
	Error = TEXT("stale");
	TestTrue(TEXT("Disabled planner is a no-op"),
		Planner.BuildPlans(
			Disabled,
			FABTSM73DAG4TrialPlanningInput(),
			RejectedPlans,
			Error));
	TestTrue(TEXT("Disabled planner clears stale plan state"),
		RejectedPlans.IsEmpty() && Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG4ResponseAutomationTest,
	"ABTS.M73DAG4.Response.EvaluatorAndComparison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG4ResponseAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM73DAG4ValidationSettings Settings =
		MakeDAG4TestSettings();
	FABTSM73DAG4ResponseEvaluator Evaluator;
	const FABTSM73DAG4TrialEvaluationInput EvaluationFixture =
		MakeWeakEvaluationFixture();
	FABTSM73DAG4TrialMetrics WeakMetrics;
	FString Error;
	TestTrue(
		FString::Printf(TEXT("Weak response reduces to metrics: %s"),
			*Error),
		Evaluator.EvaluateTrial(
			Settings,
			EvaluationFixture,
			WeakMetrics,
			Error));
	TestTrue(TEXT("Weak response completes"), WeakMetrics.bCompleted);
	TestTrue(TEXT("Weak response affects a certified body fraction"),
		WeakMetrics.AffectedMainBodyMassRatio
			>= Settings.MinWeakAffectedMassRatio
			&& WeakMetrics.AffectedMainBodyMassRatio
				<= Settings.MaxWeakAffectedMassRatio);
	TestEqual(TEXT("Predicted weak body is fully realized"),
		WeakMetrics.PredictedAffectedRealizationRatio, 1.0f);
	TestEqual(TEXT("Drop direction aligns with local gravity"),
		WeakMetrics.DirectionAlignment, 1.0f);
	TestEqual(TEXT("Reversed duplicate secondary pair is deduplicated"),
		WeakMetrics.SecondaryContactCount, 2);
	TestEqual(TEXT("Propagation depth follows the settled graph"),
		WeakMetrics.PropagationDepth, 2);
	TestTrue(TEXT("Weak response score clears its floor"),
		WeakMetrics.ResponseScore >= Settings.MinWeakResponseScore);
	TestTrue(TEXT("Evaluator preserves the recomputed mass ratio"),
		FMath::IsNearlyEqual(
			WeakMetrics.PredictedAffectedMainBodyMassRatio,
			EvaluationFixture.Plan
				.PredictedAffectedMainBodyMassRatio,
			1.0e-6f));

	auto ExpectEvaluationFailure =
		[this, &Evaluator, &Settings](
			const FString& Label,
			const FABTSM73DAG4TrialEvaluationInput& Input,
			const TCHAR* ExpectedPrefix)
	{
		FABTSM73DAG4TrialMetrics Rejected;
		Rejected.bCompleted = true;
		Rejected.ResponseScore = 1.0f;
		FString LocalError;
		TestFalse(
			Label,
			Evaluator.EvaluateTrial(
				Settings,
				Input,
				Rejected,
				LocalError));
		TestTrue(
			Label + TEXT(" has an explicit reason"),
			LocalError.StartsWith(ExpectedPrefix));
		TestTrue(
			Label + TEXT(" clears stale measurements"),
			!Rejected.bCompleted
				&& Rejected.ResponseScore == 0.0f);
	};

	FABTSM73DAG4TrialEvaluationInput InvalidEvaluation =
		EvaluationFixture;
	InvalidEvaluation.Plan.RemovedNodeIds.Add(2);
	ExpectEvaluationFailure(
		TEXT("A trial cannot remove multiple nodes"),
		InvalidEvaluation,
		TEXT("DAG4ResponsePlanInvalid"));

	InvalidEvaluation = EvaluationFixture;
	InvalidEvaluation.Plan.PredictedAffectedMainBodyMassRatio =
		0.10f;
	ExpectEvaluationFailure(
		TEXT("Authored and recomputed predicted mass must agree"),
		InvalidEvaluation,
		TEXT("DAG4ResponsePlanPredictionMismatch"));

	InvalidEvaluation = EvaluationFixture;
	InvalidEvaluation.Outcomes.Add(MakeStationaryOutcome(1));
	ExpectEvaluationFailure(
		TEXT("The removed node cannot publish an outcome"),
		InvalidEvaluation,
		TEXT("DAG4ResponseOutcomeInvalid:1"));

	InvalidEvaluation = EvaluationFixture;
	InvalidEvaluation.Outcomes[2].FinalDisplacementLocal =
		FVector(0.0f, 0.0f, -41.0f);
	ExpectEvaluationFailure(
		TEXT("Final displacement cannot exceed the observed maximum"),
		InvalidEvaluation,
		TEXT("DAG4ResponseOutcomeInvalid:3"));

	InvalidEvaluation = EvaluationFixture;
	InvalidEvaluation.SecondaryContactNodePairs = {3, 2};
	ExpectEvaluationFailure(
		TEXT("A settled pair cannot masquerade as a secondary contact"),
		InvalidEvaluation,
		TEXT("DAG4ResponseSecondaryPairWasSettled"));

	const TArray<FABTSM73DAG4TrialMetrics> BaseTrials =
		MakeComparisonFixture(WeakMetrics);
	FABTSM73DAG4ValidationResult Certified =
		MakeSettledAcceptedResult();
	Error.Reset();
	TestTrue(
		FString::Printf(TEXT("Weak-vs-ordinary comparison certifies: %s"),
			*Error),
		Evaluator.CertifyComparison(
			Settings,
			EABTSM73DAGFailureMotion::Drop,
			BaseTrials,
			Certified,
			Error));
	TestTrue(TEXT("Comparison publishes final acceptance"),
		Certified.bChaosComparisonAccepted && Certified.bAccepted);
	TestEqual(TEXT("Weak trial index is retained"),
		Certified.WeakTrialIndex, 0);
	TestTrue(TEXT("Comparison hash is non-zero"),
		Certified.ValidationHash != 0);
	const int64 CanonicalValidationHash = Certified.ValidationHash;

	auto ExpectComparisonFailure =
		[this, &Evaluator, &Settings](
			const FString& Label,
			const TArray<FABTSM73DAG4TrialMetrics>& Trials,
			const EABTSM73DAGFailureMotion Motion,
			const TCHAR* ExpectedPrefix)
	{
		FABTSM73DAG4ValidationResult Rejected =
			MakeSettledAcceptedResult();
		FString LocalError;
		TestFalse(
			Label,
			Evaluator.CertifyComparison(
				Settings,
				Motion,
				Trials,
				Rejected,
				LocalError));
		TestTrue(
			Label + TEXT(" has an explicit reason"),
			LocalError.StartsWith(ExpectedPrefix));
		TestFalse(
			Label + TEXT(" does not publish acceptance"),
			Rejected.bChaosComparisonAccepted
				|| Rejected.bAccepted);
		TestEqual(
			Label + TEXT(" clears validation hash"),
			Rejected.ValidationHash,
			static_cast<int64>(0));
	};

	TArray<FABTSM73DAG4TrialMetrics> Mutated = BaseTrials;
	Mutated.Swap(0, 1);
	ExpectComparisonFailure(
		TEXT("Weak trial must remain trial zero and probe zero"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4ComparisonWeakTrialOrderInvalid"));

	Mutated = BaseTrials;
	Mutated[2].ProbeIndex = Mutated[1].ProbeIndex;
	ExpectComparisonFailure(
		TEXT("Ordinary probe indices must be unique"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4ComparisonOrdinaryProbeInvalid"));

	Mutated = BaseTrials;
	Mutated[2].RemovedNodeIds = Mutated[1].RemovedNodeIds;
	ExpectComparisonFailure(
		TEXT("Each comparison trial must remove a unique node"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4ComparisonRemovedNodeRepeated"));

	Mutated = BaseTrials;
	Mutated[1].PredictedAffectedMainBodyMassRatio = 0.30f;
	ExpectComparisonFailure(
		TEXT("Ordinary predicted-mass ceiling"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4OrdinaryPredictedMassExceeded"));

	Mutated = BaseTrials;
	Mutated[1].AffectedMainBodyMassRatio = 0.30f;
	ExpectComparisonFailure(
		TEXT("Ordinary realized-mass ceiling"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4OrdinaryAffectedMassExceeded"));

	Mutated = BaseTrials;
	Mutated[0].AffectedMainBodyMassRatio = 0.10f;
	ExpectComparisonFailure(
		TEXT("Weak affected-mass minimum"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakAffectedMassOutsideRange"));

	Mutated = BaseTrials;
	Mutated[0].AffectedMainBodyMassRatio = 0.90f;
	ExpectComparisonFailure(
		TEXT("Weak affected-mass maximum"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakAffectedMassOutsideRange"));

	Mutated = BaseTrials;
	Mutated[0].PredictedAffectedRealizationRatio = 0.40f;
	ExpectComparisonFailure(
		TEXT("Weak predicted-closure realization"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakPredictedRealizationTooLow"));

	Mutated = BaseTrials;
	Mutated[0].MaxDropDistanceCM = 19.0f;
	ExpectComparisonFailure(
		TEXT("Drop visible-displacement gate"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakDropMotionRejected"));

	Mutated = BaseTrials;
	Mutated[0].DirectionAlignment = 0.0f;
	ExpectComparisonFailure(
		TEXT("Failure-direction gate"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakDropMotionRejected"));

	Mutated = BaseTrials;
	Mutated[0].MaxRotationDegrees = 5.0f;
	ExpectComparisonFailure(
		TEXT("Tip visible-rotation gate"),
		Mutated,
		EABTSM73DAGFailureMotion::Tip,
		TEXT("DAG4WeakTipMotionRejected"));

	Mutated = BaseTrials;
	Mutated[0].MaxExpectedDirectionSlideCM = 19.0f;
	ExpectComparisonFailure(
		TEXT("SlideThenTip translation gate"),
		Mutated,
		EABTSM73DAGFailureMotion::SlideThenTip,
		TEXT("DAG4WeakSeamMotionRejected"));

	Mutated = BaseTrials;
	Mutated[0].ResponseScore = 0.10f;
	ExpectComparisonFailure(
		TEXT("Weak absolute response-score gate"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakResponseScoreTooLow"));

	Mutated = BaseTrials;
	Mutated[1].ResponseScore = 0.40f;
	ExpectComparisonFailure(
		TEXT("Weak relative response advantage"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakResponseAdvantageTooLow"));

	Mutated = BaseTrials;
	Mutated[0].AffectedMainBodyMassRatio = 0.20f;
	for (int32 TrialIndex = 1;
		TrialIndex < Mutated.Num();
		++TrialIndex)
	{
		Mutated[TrialIndex].AffectedMainBodyMassRatio = 0.15f;
	}
	ExpectComparisonFailure(
		TEXT("Weak absolute affected-mass advantage"),
		Mutated,
		EABTSM73DAGFailureMotion::Drop,
		TEXT("DAG4WeakAffectedMassAdvantageTooLow"));

	FABTSM73DAG4ValidationResult Aliased =
		MakeSettledAcceptedResult();
	Aliased.Trials = BaseTrials;
	Error.Reset();
	TestTrue(TEXT("Trials may alias the destination result"),
		Evaluator.CertifyComparison(
			Settings,
			EABTSM73DAGFailureMotion::Drop,
			Aliased.Trials,
			Aliased,
			Error));
	TestEqual(TEXT("Aliased certification preserves all trials"),
		Aliased.Trials.Num(), BaseTrials.Num());
	TestEqual(TEXT("Aliased certification has the canonical hash"),
		Aliased.ValidationHash, CanonicalValidationHash);

	Mutated = BaseTrials;
	Mutated[1].RemovedNodeIds.Reset();
	FABTSM73DAG4ValidationResult StructurallyRejected =
		MakeSettledAcceptedResult();
	StructurallyRejected.bChaosComparisonAccepted = true;
	StructurallyRejected.bAccepted = true;
	StructurallyRejected.Trials = BaseTrials;
	StructurallyRejected.WeakTrialIndex = 2;
	StructurallyRejected.WeakResponseScore = 0.9f;
	StructurallyRejected.ValidationHash = 777;
	Error.Reset();
	TestFalse(TEXT("Malformed completed trial is rejected"),
		Evaluator.CertifyComparison(
			Settings,
			EABTSM73DAGFailureMotion::Drop,
			Mutated,
			StructurallyRejected,
			Error));
	TestTrue(TEXT("Malformed trial reason is explicit"),
		Error.StartsWith(TEXT("DAG4ComparisonTrialInvalid")));
	TestTrue(TEXT("Structural input rejection clears stale trials"),
		StructurallyRejected.Trials.IsEmpty());
	TestTrue(TEXT("Structural input rejection clears rollout state"),
		!StructurallyRejected.bChaosComparisonAccepted
			&& !StructurallyRejected.bAccepted
			&& StructurallyRejected.WeakTrialIndex == INDEX_NONE
			&& StructurallyRejected.WeakResponseScore == 0.0f
			&& StructurallyRejected.ValidationHash == 0);

	InvalidEvaluation = EvaluationFixture;
	InvalidEvaluation.SecondaryContactNodePairs.Add(0);
	FABTSM73DAG4TrialMetrics StaleMetrics;
	StaleMetrics.bCompleted = true;
	StaleMetrics.ResponseScore = 1.0f;
	Error.Reset();
	TestFalse(TEXT("Malformed secondary pair array rejects"),
		Evaluator.EvaluateTrial(
			Settings,
			InvalidEvaluation,
			StaleMetrics,
			Error));
	TestEqual(TEXT("Evaluation rejection is explicit"),
		Error,
		FString(TEXT("DAG4ResponseSecondaryPairArrayInvalid")));
	TestTrue(TEXT("Evaluation rejection clears stale measurements"),
		!StaleMetrics.bCompleted
			&& StaleMetrics.ResponseScore == 0.0f);

	FABTSM73DAG4ValidationSettings Disabled = Settings;
	Disabled.bEnableSettledChaosValidation = false;
	StaleMetrics.bCompleted = true;
	StaleMetrics.ResponseScore = 1.0f;
	Error = TEXT("stale");
	TestTrue(TEXT("Disabled evaluator is a no-op"),
		Evaluator.EvaluateTrial(
			Disabled,
			FABTSM73DAG4TrialEvaluationInput(),
			StaleMetrics,
			Error));
	TestTrue(TEXT("Disabled evaluator clears stale output"),
		!StaleMetrics.bCompleted
			&& StaleMetrics.ResponseScore == 0.0f
			&& Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG4ProfileAutomationTest,
	"ABTS.M73DAG4.Profile.DisabledPrerequisitesAndAtomicFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG4ProfileAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	for (const TPair<EABTSM3TaskType, EABTSM7BuildingMaterial>& Entry
		: {
			TPair<EABTSM3TaskType, EABTSM7BuildingMaterial>(
				EABTSM3TaskType::Workshop,
				EABTSM7BuildingMaterial::Wood),
			TPair<EABTSM3TaskType, EABTSM7BuildingMaterial>(
				EABTSM3TaskType::TargetBuilding,
				EABTSM7BuildingMaterial::Stone),
			TPair<EABTSM3TaskType, EABTSM7BuildingMaterial>(
				EABTSM3TaskType::FurnaceRuins,
				EABTSM7BuildingMaterial::Iron)})
	{
		const FABTSM7TaskGraphBuildingProfile DefaultProfile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				Entry.Key,
				Entry.Value);
		TestFalse(
			TEXT("Production default leaves DAG-4 disabled"),
			DefaultProfile.DAG4ValidationSettings
				.bEnableSettledChaosValidation);
	}

	FABTSM7TaskGraphBuildingProfile Legacy;
	Legacy.TaskType = EABTSM3TaskType::TargetBuilding;
	Legacy.GenerationSettings.GenerationAlgorithm =
		EABTSM73GenerationAlgorithm::LegacyLayeredAB2;
	Legacy.GenerationSettings.PrimaryMaterial =
		EABTSM7BuildingMaterial::Stone;
	Legacy.DAGFailureFrontierSettings.bEnableAnalysis = true;
	Legacy.DAGFailureFrontierSettings
		.bEnableGeneralizedSmallCutSearch = true;
	Legacy.DAGFailurePatternSettings.bEnableGeometryRewrite = true;
	Legacy.DAGFailurePlayabilitySettings
		.bEnablePlayabilityRouting = true;
	Legacy.DAG4ValidationSettings
		.bEnableSettledChaosValidation = true;
	FABTSM7TaskGraphBuildingProfile Resolved;
	bool bMigratedLegacy = false;
	TestTrue(TEXT("Legacy profile migrates through the resolver"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::TargetBuilding,
			Legacy,
			Resolved,
			bMigratedLegacy));
	TestTrue(TEXT("Legacy migration is reported"),
		bMigratedLegacy);
	TestFalse(TEXT("Legacy migration forcibly disables DAG-4"),
		Resolved.DAG4ValidationSettings
			.bEnableSettledChaosValidation);

	auto ExpectPrerequisiteRejection =
		[this](
			const FString& Label,
			const FABTSM7TaskGraphBuildingProfile& Source)
	{
		FABTSM7TaskGraphBuildingProfile LocalResolved;
		bool bLocalMigrated = true;
		TestFalse(
			Label,
			FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
				EABTSM3TaskType::TargetBuilding,
				Source,
				LocalResolved,
				bLocalMigrated));
		TestFalse(
			Label + TEXT(" is not a legacy migration"),
			bLocalMigrated);
	};

	FABTSM7TaskGraphBuildingProfile Missing =
		MakeExplicitDAG4Profile();
	Missing.DAGFailureFrontierSettings.bEnableAnalysis = false;
	ExpectPrerequisiteRejection(
		TEXT("DAG-4 without DAG3-A analysis rejects"),
		Missing);

	Missing = MakeExplicitDAG4Profile();
	Missing.DAGFailureFrontierSettings
		.bEnableGeneralizedSmallCutSearch = false;
	ExpectPrerequisiteRejection(
		TEXT("DAG-4 without generalized cut search rejects"),
		Missing);

	Missing = MakeExplicitDAG4Profile();
	Missing.DAGFailurePatternSettings.bEnableGeometryRewrite = false;
	ExpectPrerequisiteRejection(
		TEXT("DAG-4 without DAG3-B rejects"),
		Missing);

	Missing = MakeExplicitDAG4Profile();
	Missing.DAGFailurePlayabilitySettings
		.bEnablePlayabilityRouting = false;
	ExpectPrerequisiteRejection(
		TEXT("DAG-4 without DAG3-C rejects"),
		Missing);

	const FABTSM7TaskGraphBuildingProfile Explicit =
		MakeExplicitDAG4Profile();
	bMigratedLegacy = true;
	TestTrue(TEXT("Complete explicit DAG profile resolves"),
		FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			EABTSM3TaskType::TargetBuilding,
			Explicit,
			Resolved,
			bMigratedLegacy));
	TestFalse(TEXT("Explicit DAG profile is not migrated"),
		bMigratedLegacy);
	TestTrue(TEXT("Complete explicit profile preserves DAG-4"),
		Resolved.DAG4ValidationSettings
			.bEnableSettledChaosValidation);
	TestEqual(TEXT("Explicit DAG-4 settings remain authored"),
		Resolved.DAG4ValidationSettings.NonWeakProbeCount, 5);
	return true;
}

#endif
