// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace
{
	FABTSM11GravityBodySpec MakePrimary(
		const FVector3d& CenterCM,
		const double Mu,
		const double MaximumRadiusCM)
	{
		FABTSM11GravityBodySpec Body;
		Body.BodyId = 100;
		Body.Role = EABTSM110FinaleGravityRole::Primary;
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 = Mu;
		Body.MinimumEvaluationRadiusCM = 50.0;
		Body.VisualRadiusCM = 100.0;
		Body.CollisionRadiusCM = 50.0;
		Body.MaximumSimulationRadiusCM = MaximumRadiusCM;
		return Body;
	}

	FABTSM11GravityBodySpec MakeAssist(
		const int32 AssistIndex,
		const FVector3d& CenterCM,
		const double Mu,
		const double CollisionRadiusCM,
		const double ReferenceRadiusCM,
		const double InfluenceRadiusCM)
	{
		FABTSM11GravityBodySpec Body;
		Body.BodyId = 100 + AssistIndex;
		Body.Role = static_cast<EABTSM110FinaleGravityRole>(AssistIndex);
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 = Mu;
		Body.MinimumEvaluationRadiusCM = FMath::Min(50.0, CollisionRadiusCM);
		Body.VisualRadiusCM = CollisionRadiusCM * 1.5;
		Body.CollisionRadiusCM = CollisionRadiusCM;
		Body.InfluenceRadiusCM = InfluenceRadiusCM;
		Body.AssistReferenceRadiusCM = ReferenceRadiusCM;
		Body.InfluenceBlendWidthCM =
			FMath::Min(500.0, (InfluenceRadiusCM - CollisionRadiusCM) * 0.2);
		Body.BPlaneReferenceNormal = FVector3d(0.0, 0.0, 1.0);
		Body.BPlaneFallbackAxis = FVector3d(0.0, 1.0, 0.0);
		Body.BPlaneTargetTCM = 0.0;
		Body.BPlaneTargetRCM = 0.0;
		Body.BPlaneSigmaTCM = 1.0e9;
		Body.BPlaneSigmaRCM = 1.0e9;
		Body.BPlaneOuterChiSquared = 4.0;
		Body.MinimumEnergyChangeCM2PerSec2 = -20000.0;
		Body.MaximumEnergyChangeCM2PerSec2 = 20000.0;
		return Body;
	}

	FABTSM11GravityScenario MakeNaturalFlybyScenario()
	{
		FABTSM11GravityScenario Scenario;
		Scenario.LayoutVersion = 1;
		Scenario.ScenarioHash = 0x11a001u;
		Scenario.Bodies[0] = MakePrimary(FVector3d(0.0, -1.0e9, 0.0), 1.0, 2.0e9);
		Scenario.Bodies[1] = MakeAssist(1, FVector3d::ZeroVector, 1.0e7, 200.0, 4500.0, 5000.0);
		Scenario.Bodies[2] = MakeAssist(2, FVector3d(1.0e8, 0.0, 0.0), 1.0, 100.0, 800.0, 1000.0);
		Scenario.Bodies[3] = MakeAssist(3, FVector3d(2.0e8, 0.0, 0.0), 1.0, 100.0, 800.0, 1000.0);
		Scenario.Target.TargetId = 200;
		Scenario.Target.CenterCM = FVector3d(3.0e8, 0.0, 0.0);
		Scenario.Target.HitRadiusCM = 100.0;
		return Scenario;
	}

	FABTSM11TrajectoryRequest MakeNaturalFlybyRequest(const FVector3d& VirtualVelocity)
	{
		FABTSM11TrajectoryRequest Request;
		Request.Scenario = MakeNaturalFlybyScenario();
		Request.Scenario.Bodies[1].VirtualOrbitalVelocityCMPerSec = VirtualVelocity;
		Request.Config.FixedTimeStepSeconds = 1.0 / 120.0;
		Request.Config.MaximumSimulationTimeSeconds = 60.0;
		Request.Config.NaturalCloneMaximumTimeSeconds = 60.0;
		Request.InitialPositionCM = FVector3d(-6000.0, 1200.0, 0.0);
		Request.InitialVelocityCMPerSec = FVector3d(250.0, 0.0, 0.0);
		return Request;
	}

	void MoveNaturalFlybyToAssist(
		FABTSM11TrajectoryRequest& Request,
		const int32 AssistIndex)
	{
		check(AssistIndex >= 1 && AssistIndex <= FABTSM11GravityScenario::AssistCount);
		const FVector3d VirtualVelocity =
			Request.Scenario.Bodies[1].VirtualOrbitalVelocityCMPerSec;
		for (int32 Index = 1; Index <= FABTSM11GravityScenario::AssistCount; ++Index)
		{
			Request.Scenario.Bodies[Index] = MakeAssist(
				Index,
				FVector3d(static_cast<double>(Index) * 1.0e8, 0.0, 0.0),
				1.0,
				100.0,
				800.0,
				1000.0);
		}
		Request.Scenario.Bodies[AssistIndex] = MakeAssist(
			AssistIndex,
			FVector3d::ZeroVector,
			1.0e7,
			200.0,
			4500.0,
			5000.0);
		Request.Scenario.Bodies[AssistIndex].VirtualOrbitalVelocityCMPerSec =
			VirtualVelocity;
		Request.Scenario.ScenarioHash = 0x11a010u + static_cast<uint32>(AssistIndex);
		Request.InitialExpectedAssistIndex = AssistIndex;
	}

	FABTSM11TrajectoryRequest MakeCentralOrbitRequest(
		const double StepSeconds,
		const double DurationSeconds)
	{
		FABTSM11TrajectoryRequest Request;
		Request.Scenario.LayoutVersion = 1;
		Request.Scenario.ScenarioHash = 0x11a002u;
		Request.Scenario.Bodies[0] =
			MakePrimary(FVector3d::ZeroVector, 1.0e8, 1.0e7);
		Request.Scenario.Bodies[1] =
			MakeAssist(1, FVector3d(2.0e6, 0.0, 0.0), 1.0, 100.0, 800.0, 1000.0);
		Request.Scenario.Bodies[2] =
			MakeAssist(2, FVector3d(2.1e6, 0.0, 0.0), 1.0, 100.0, 800.0, 1000.0);
		Request.Scenario.Bodies[3] =
			MakeAssist(3, FVector3d(2.2e6, 0.0, 0.0), 1.0, 100.0, 800.0, 1000.0);
		Request.Scenario.Target.TargetId = 200;
		Request.Scenario.Target.CenterCM = FVector3d(5.0e6, 0.0, 0.0);
		Request.Scenario.Target.HitRadiusCM = 100.0;
		Request.Config.FixedTimeStepSeconds = StepSeconds;
		Request.Config.MaximumSimulationTimeSeconds = DurationSeconds;
		Request.Config.MaximumSubdivisionDepth = 0;
		Request.Config.AssistStepRadiusFraction = 1.0;
		Request.Config.CollisionStepRadiusFraction = 2.0;
		Request.InitialPositionCM = FVector3d(10000.0, 0.0, 0.0);
		Request.InitialVelocityCMPerSec = FVector3d(0.0, 100.0, 0.0);
		return Request;
	}

	FABTSM11TrajectoryRequest MakeSweptRequest(const bool bHitBody)
	{
		FABTSM11TrajectoryRequest Request;
		Request.Scenario = MakeNaturalFlybyScenario();
		Request.Scenario.ScenarioHash = bHitBody ? 0x11a004u : 0x11a003u;
		Request.Scenario.Bodies[1] =
			MakeAssist(1, bHitBody ? FVector3d::ZeroVector : FVector3d(1.0e8, 0.0, 0.0),
				1.0, 10.0, 50.0, 60.0);
		Request.Scenario.Bodies[2].CenterCM = FVector3d(1.1e8, 0.0, 0.0);
		Request.Scenario.Bodies[3].CenterCM = FVector3d(1.2e8, 0.0, 0.0);
		Request.Scenario.Target.CenterCM =
			bHitBody ? FVector3d(1.5e8, 0.0, 0.0) : FVector3d::ZeroVector;
		Request.Scenario.Target.HitRadiusCM = 10.0;
		Request.Config.FixedTimeStepSeconds = 1.0;
		Request.Config.MaximumSimulationTimeSeconds = 1.0;
		Request.Config.MaximumSubdivisionDepth = 0;
		Request.Config.AssistStepRadiusFraction = 100.0;
		Request.Config.CollisionStepRadiusFraction = 100.0;
		Request.InitialPositionCM = FVector3d(-100.0, 0.0, 0.0);
		Request.InitialVelocityCMPerSec = FVector3d(200.0, 0.0, 0.0);
		return Request;
	}

	bool ResultsExactlyEqual(
		const FABTSM11TrajectoryResult& A,
		const FABTSM11TrajectoryResult& B)
	{
		if (A.ValidationHash != B.ValidationHash
			|| A.Termination != B.Termination
			|| A.CompletedAssistCount != B.CompletedAssistCount
			|| A.Points.Num() != B.Points.Num()
			|| A.Events.Num() != B.Events.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Points.Num(); ++Index)
		{
			const FABTSM11TrajectoryPoint& PA = A.Points[Index];
			const FABTSM11TrajectoryPoint& PB = B.Points[Index];
			if (PA.TimeSeconds != PB.TimeSeconds
				|| PA.PositionCM != PB.PositionCM
				|| PA.VelocityCMPerSec != PB.VelocityCMPerSec
				|| PA.PrimarySpecificEnergyCM2PerSec2
					!= PB.PrimarySpecificEnergyCM2PerSec2)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Events.Num(); ++Index)
		{
			const FABTSM11TrajectoryEvent& EA = A.Events[Index];
			const FABTSM11TrajectoryEvent& EB = B.Events[Index];
			if (EA.Type != EB.Type
				|| EA.BodyId != EB.BodyId
				|| EA.AssistIndex != EB.AssistIndex
				|| EA.TimeSeconds != EB.TimeSeconds
				|| EA.PositionCM != EB.PositionCM
				|| EA.VelocityCMPerSec != EB.VelocityCMPerSec
				|| EA.AppliedEnergyChangeCM2PerSec2
					!= EB.AppliedEnergyChangeCM2PerSec2)
			{
				return false;
			}
		}
		return true;
	}

	double MaximumRelativeEnergyDrift(const FABTSM11TrajectoryResult& Result)
	{
		if (Result.Points.IsEmpty())
		{
			return TNumericLimits<double>::Max();
		}
		const double InitialEnergy = Result.Points[0].PrimarySpecificEnergyCM2PerSec2;
		double MaximumDrift = 0.0;
		for (const FABTSM11TrajectoryPoint& Point : Result.Points)
		{
			MaximumDrift = FMath::Max(
				MaximumDrift,
				FMath::Abs(Point.PrimarySpecificEnergyCM2PerSec2 - InitialEnergy)
					/ FMath::Max(FMath::Abs(InitialEnergy), UE_DOUBLE_SMALL_NUMBER));
		}
		return MaximumDrift;
	}

	double FindOutboundRadiusCrossingTime(
		const FABTSM11TrajectoryResult& Result,
		const FVector3d& CenterCM,
		const double RadiusCM,
		const double AfterTimeSeconds)
	{
		for (const FABTSM11TrajectoryPoint& Point : Result.Points)
		{
			const FVector3d RelativePositionCM = Point.PositionCM - CenterCM;
			if (Point.TimeSeconds > AfterTimeSeconds
				&& RelativePositionCM.Length() >= RadiusCM - 0.1
				&& FVector3d::DotProduct(
					RelativePositionCM, Point.VelocityCMPerSec) > 0.0)
			{
				return Point.TimeSeconds;
			}
		}
		return TNumericLimits<double>::Max();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ADataContractTest,
	"ABTS.M11A.DataContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ADataContractTest::RunTest(const FString& Parameters)
{
	FABTSM11TrajectoryRequest Request = MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	FString Failure;
	TestTrue(TEXT("The four-body pure-data request is valid"), Request.IsValid(&Failure));
	TestEqual(TEXT("M11-A remains a fixed primary plus three-assist contract"),
		FABTSM11GravityScenario::BodyCount, 4);
	TestEqual(TEXT("M11-A exposes exactly three ordered assists"),
		FABTSM11GravityScenario::AssistCount, 3);
	TestTrue(TEXT("Assist ablation mask enables assist 1 by default"),
		Request.Config.IsGameplayAssistEnabled(1));

	FABTSM11TrajectoryRequest UnsupportedVersion = Request;
	UnsupportedVersion.Config.SolverVersion = 2;
	TestFalse(TEXT("An unimplemented solver version is rejected"),
		UnsupportedVersion.IsValid(&Failure));
	UnsupportedVersion = Request;
	UnsupportedVersion.Config.HashSchemaVersion = 2;
	TestFalse(TEXT("An unimplemented hash schema is rejected"),
		UnsupportedVersion.IsValid(&Failure));

	FABTSM11TrajectoryRequest FadeShellReference = Request;
	FadeShellReference.Scenario.Bodies[1].AssistReferenceRadiusCM = 4501.0;
	TestFalse(TEXT("The reference sphere cannot enter the influence fade shell"),
		FadeShellReference.Scenario.IsValid(&Failure));

	FABTSM11TrajectoryRequest InitialInsideInfluence = Request;
	InitialInsideInfluence.InitialPositionCM = FVector3d(-4750.0, 0.0, 0.0);
	TestFalse(TEXT("Initial state inside an assist influence sphere is rejected"),
		InitialInsideInfluence.IsValid(&Failure));

	FABTSM11TrajectoryRequest InvalidPassSide = Request;
	InvalidPassSide.Scenario.Bodies[1].AllowedPassSide =
		static_cast<EABTSM11AllowedPassSide>(255);
	TestFalse(TEXT("An unknown pass-side enum value is rejected"),
		InvalidPassSide.IsValid(&Failure));

	FABTSM11TrajectoryRequest InvalidRootTolerance = Request;
	InvalidRootTolerance.Config.RootAlphaTolerance = 1.01;
	TestFalse(TEXT("Root alpha tolerance remains a normalized fraction"),
		InvalidRootTolerance.IsValid(&Failure));

	FABTSM11TrajectoryRequest InitialOutsidePrimary = Request;
	InitialOutsidePrimary.InitialPositionCM =
		Request.Scenario.GetPrimary().CenterCM
		+ FVector3d(
			Request.Scenario.GetPrimary().MaximumSimulationRadiusCM,
			0.0,
			0.0);
	TestFalse(TEXT("Initial state outside the primary simulation domain is rejected"),
		InitialOutsidePrimary.IsValid(&Failure));

	Request.Scenario.Bodies[2].CenterCM = Request.Scenario.Bodies[1].CenterCM;
	TestFalse(TEXT("Overlapping influence spheres are rejected"),
		Request.Scenario.IsValid(&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ADeterminismTest,
	"ABTS.M11A.DeterminismAndEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ADeterminismTest::RunTest(const FString& Parameters)
{
	const FABTSM11TrajectoryRequest Request =
		MakeNaturalFlybyRequest(FVector3d(0.0, -50.0, 0.0));
	FABTSM11TrajectoryResult First;
	FABTSM11TrajectoryResult Second;
	FString Failure;
	TestTrue(TEXT("First deterministic solve accepts the request"),
		FABTSM11GravityAssistSolver::Solve(Request, First, &Failure));
	TestTrue(TEXT("Second deterministic solve accepts the request"),
		FABTSM11GravityAssistSolver::Solve(Request, Second, &Failure));
	TestTrue(TEXT("Point, event, termination and hash streams are bit-identical"),
		ResultsExactlyEqual(First, Second));
	TestTrue(TEXT("Validation hash is non-zero"), First.ValidationHash != 0);
	AddInfo(FString::Printf(
		TEXT("M11-A HashSchema1 fixture: 0x%016llx"),
		static_cast<unsigned long long>(First.ValidationHash)));
	TestEqual(TEXT("HashSchema1 golden fixture remains frozen"),
		First.ValidationHash, 0xd78e8f7153cca7f1ull);

	FABTSM11TrajectoryRequest HashSensitiveRequest = Request;
	++HashSensitiveRequest.Scenario.ScenarioHash;
	FABTSM11TrajectoryResult HashSensitiveResult;
	TestTrue(TEXT("Scenario-hash sensitivity solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			HashSensitiveRequest, HashSensitiveResult, &Failure));
	TestNotEqual(TEXT("Changing the immutable scenario identity changes the result hash"),
		HashSensitiveResult.ValidationHash, First.ValidationHash);

	FABTSM11TrajectoryRequest NegativeZeroRequest = Request;
	NegativeZeroRequest.InitialVelocityCMPerSec.Z = -0.0;
	FABTSM11TrajectoryResult NegativeZeroResult;
	TestTrue(TEXT("Negative-zero canonicalization solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			NegativeZeroRequest, NegativeZeroResult, &Failure));
	TestTrue(TEXT("Negative and positive zero produce the same result and hash"),
		ResultsExactlyEqual(First, NegativeZeroResult));

	const FABTSM11TrajectoryEvent* Enter =
		First.FindAssistEvent(EABTSM11TrajectoryEventType::AssistEnter, 1);
	const FABTSM11TrajectoryEvent* Closest =
		First.FindAssistEvent(EABTSM11TrajectoryEventType::ClosestApproach, 1);
	const FABTSM11TrajectoryEvent* Exit =
		First.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	TestNotNull(TEXT("Natural encounter emits AssistEnter"), Enter);
	TestNotNull(TEXT("Natural encounter emits ClosestApproach"), Closest);
	TestNotNull(TEXT("Natural encounter emits AssistExit"), Exit);
	if (Enter != nullptr && Closest != nullptr && Exit != nullptr)
	{
		TestTrue(TEXT("Encounter events are temporally ordered"),
			Enter->TimeSeconds < Closest->TimeSeconds
				&& Closest->TimeSeconds < Exit->TimeSeconds);
	}

	FABTSM11TrajectoryRequest ContinuationRequest =
		MakeCentralOrbitRequest(0.5, 1.0);
	ContinuationRequest.InitialExpectedAssistIndex = 3;
	FABTSM11TrajectoryResult ContinuationResult;
	TestTrue(TEXT("Continuation-prefix solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			ContinuationRequest, ContinuationResult, &Failure));
	TestEqual(TEXT("CompletedAssistCount includes the supplied completed prefix"),
		ContinuationResult.CompletedAssistCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ACentralBindingConvergenceTest,
	"ABTS.M11A.CentralBindingAndConvergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ACentralBindingConvergenceTest::RunTest(const FString& Parameters)
{
	const double DurationSeconds = 20.0;
	const FABTSM11TrajectoryRequest CoarseRequest =
		MakeCentralOrbitRequest(0.5, DurationSeconds);
	const FABTSM11TrajectoryRequest FineRequest =
		MakeCentralOrbitRequest(0.25, DurationSeconds);
	FABTSM11TrajectoryResult Coarse;
	FABTSM11TrajectoryResult Fine;
	TestTrue(TEXT("Coarse central orbit solve runs"),
		FABTSM11GravityAssistSolver::Solve(CoarseRequest, Coarse));
	TestTrue(TEXT("Fine central orbit solve runs"),
		FABTSM11GravityAssistSolver::Solve(FineRequest, Fine));
	TestEqual(TEXT("Sub-escape orbit ends as center-bound"),
		Coarse.Termination, EABTSM11TrajectoryTermination::SolarCaptured);
	TestEqual(TEXT("Step-halved orbit preserves termination topology"),
		Fine.Termination, Coarse.Termination);

	const double AngularRate = 100.0 / 10000.0;
	const FVector3d AnalyticPosition(
		10000.0 * FMath::Cos(AngularRate * DurationSeconds),
		10000.0 * FMath::Sin(AngularRate * DurationSeconds),
		0.0);
	const double CoarseError = (Coarse.Points.Last().PositionCM - AnalyticPosition).Length();
	const double FineError = (Fine.Points.Last().PositionCM - AnalyticPosition).Length();
	const double CoarseEnergyDrift = MaximumRelativeEnergyDrift(Coarse);
	const double FineEnergyDrift = MaximumRelativeEnergyDrift(Fine);
	AddInfo(FString::Printf(
		TEXT("M11-A convergence: PosError coarse=%.9f fine=%.9f EnergyDrift coarse=%.3e fine=%.3e"),
		CoarseError, FineError, CoarseEnergyDrift, FineEnergyDrift));
	TestTrue(TEXT("Halving the step materially reduces endpoint error"),
		FineError < CoarseError * 0.6);
	TestTrue(TEXT("Conservative energy drift stays below the approved fixture threshold"),
		CoarseEnergyDrift < 1.0e-5);
	TestTrue(TEXT("Halving the step does not increase conservative energy drift"),
		FineEnergyDrift <= CoarseEnergyDrift + 1.0e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ANaturalDeflectionTest,
	"ABTS.M11A.NaturalDeflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ANaturalDeflectionTest::RunTest(const FString& Parameters)
{
	const FABTSM11TrajectoryRequest Request =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	FABTSM11TrajectoryResult Result;
	TestTrue(TEXT("Natural-only flyby solve runs"),
		FABTSM11GravityAssistSolver::Solve(Request, Result));
	const FABTSM11TrajectoryEvent* Enter =
		Result.FindAssistEvent(EABTSM11TrajectoryEventType::AssistEnter, 1);
	const FABTSM11TrajectoryEvent* Exit =
		Result.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	TestNotNull(TEXT("Natural-only flyby enters the influence sphere"), Enter);
	TestNotNull(TEXT("Natural-only flyby exits the influence sphere"), Exit);
	if (Enter != nullptr && Exit != nullptr)
	{
		AddInfo(FString::Printf(
			TEXT("M11-A natural flyby: Deflection=%.9f Ideal=%.9f Error=%.3e Closest=%.3f cm EntrySpeed=%.6f ExitSpeed=%.6f AppliedEnergy=%.9f"),
			Exit->NaturalDeflectionRadians,
			Exit->IdealDeflectionRadians,
			FMath::Abs(
				Exit->NaturalDeflectionRadians - Exit->IdealDeflectionRadians),
			Exit->ClosestDistanceCM,
			Exit->EntrySpeedCMPerSec,
			Exit->ExitSpeedCMPerSec,
			Exit->AppliedEnergyChangeCM2PerSec2));
		TestTrue(TEXT("Fixed assist gravity creates a visible natural turn"),
			Exit->NaturalDeflectionRadians > 0.05);
		TestTrue(TEXT("Osculating asymptotes match the ideal hyperbolic turn"),
			FMath::Abs(
				Exit->NaturalDeflectionRadians - Exit->IdealDeflectionRadians)
				< 5.0e-4);
		TestTrue(TEXT("The flyby remains outside the analytic collision sphere"),
			Exit->ClosestDistanceCM > Request.Scenario.Bodies[1].CollisionRadiusCM);
		TestTrue(TEXT("Zero virtual orbital velocity gives exactly zero gameplay energy"),
			Exit->RequestedEnergyChangeCM2PerSec2 == 0.0
				&& Exit->AppliedEnergyChangeCM2PerSec2 == 0.0);
		const FABTSM11GravityBodySpec& Assist = Request.Scenario.Bodies[1];
		TestTrue(TEXT("AssistEnter is rooted on the influence sphere"),
			FMath::Abs(
				(Enter->PositionCM - Assist.CenterCM).Length()
					- Assist.InfluenceRadiusCM) < 0.1);
		TestTrue(TEXT("AssistExit is rooted on the influence sphere"),
			FMath::Abs(
				(Exit->PositionCM - Assist.CenterCM).Length()
					- Assist.InfluenceRadiusCM) < 0.1);
		const double InitialEnergy =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				Request.Scenario.GetPrimary(),
				Request.InitialPositionCM,
				Request.InitialVelocityCMPerSec);
		const double ExitEnergy =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				Request.Scenario.GetPrimary(),
				Exit->PositionCM,
				Exit->VelocityCMPerSec);
		const double RelativeNaturalEnergyResidual =
			FMath::Abs(ExitEnergy - InitialEnergy)
			/ FMath::Max(FMath::Abs(InitialEnergy), 1.0);
		AddInfo(FString::Printf(
			TEXT("M11-A full influence-shell energy residual: %.3e"),
			RelativeNaturalEnergyResidual));
		TestTrue(TEXT("A fixed assist pays back the complete outbound influence shell"),
			RelativeNaturalEnergyResidual < 2.0e-5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11AVirtualMomentumAblationTest,
	"ABTS.M11A.VirtualMomentumAndAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11AVirtualMomentumAblationTest::RunTest(const FString& Parameters)
{
	FABTSM11TrajectoryRequest PositiveRequest =
		MakeNaturalFlybyRequest(FVector3d(0.0, -50.0, 0.0));
	FABTSM11TrajectoryRequest NegativeRequest =
		MakeNaturalFlybyRequest(FVector3d(0.0, 50.0, 0.0));
	FABTSM11TrajectoryRequest AblatedRequest = PositiveRequest;
	AblatedRequest.Config.EnabledAssistMask &= ~0x1u;
	FABTSM11TrajectoryRequest WrongSideRequest = PositiveRequest;
	WrongSideRequest.Scenario.Bodies[1].AllowedPassSide =
		EABTSM11AllowedPassSide::PositiveR;
	FABTSM11TrajectoryRequest PartialCorridorRequest = PositiveRequest;
	PartialCorridorRequest.Scenario.Bodies[1].BPlaneSigmaRCM = 900.0;
	FABTSM11TrajectoryRequest OutsideCorridorRequest = PositiveRequest;
	OutsideCorridorRequest.Scenario.Bodies[1].BPlaneSigmaRCM = 400.0;

	FABTSM11TrajectoryResult Positive;
	FABTSM11TrajectoryResult Negative;
	FABTSM11TrajectoryResult Ablated;
	FABTSM11TrajectoryResult WrongSide;
	FABTSM11TrajectoryResult PartialCorridor;
	FABTSM11TrajectoryResult OutsideCorridor;
	TestTrue(TEXT("Positive virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(PositiveRequest, Positive));
	TestTrue(TEXT("Negative virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(NegativeRequest, Negative));
	TestTrue(TEXT("Ablated virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(AblatedRequest, Ablated));
	TestTrue(TEXT("Wrong-side virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(WrongSideRequest, WrongSide));
	TestTrue(TEXT("Partial-corridor virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			PartialCorridorRequest, PartialCorridor));
	TestTrue(TEXT("Outside-corridor virtual-momentum solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			OutsideCorridorRequest, OutsideCorridor));

	const FABTSM11TrajectoryEvent* PositiveExit =
		Positive.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* NegativeExit =
		Negative.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* AblatedExit =
		Ablated.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* WrongSideExit =
		WrongSide.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* PartialCorridorExit =
		PartialCorridor.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* OutsideCorridorExit =
		OutsideCorridor.FindAssistEvent(EABTSM11TrajectoryEventType::AssistExit, 1);
	TestNotNull(TEXT("Positive flyby exits"), PositiveExit);
	TestNotNull(TEXT("Reverse virtual momentum flyby exits"), NegativeExit);
	TestNotNull(TEXT("Ablated flyby still naturally exits"), AblatedExit);
	TestNotNull(TEXT("Wrong-side flyby still naturally exits"), WrongSideExit);
	TestNotNull(TEXT("Partial-corridor flyby exits"), PartialCorridorExit);
	TestNotNull(TEXT("Outside-corridor flyby exits"), OutsideCorridorExit);
	if (PositiveExit != nullptr
		&& NegativeExit != nullptr
		&& AblatedExit != nullptr
		&& WrongSideExit != nullptr
		&& PartialCorridorExit != nullptr
		&& OutsideCorridorExit != nullptr)
	{
		AddInfo(FString::Printf(
			TEXT("M11-A exchange: Positive=%.3f Negative=%.3f Ablated=%.3f"),
			PositiveExit->AppliedEnergyChangeCM2PerSec2,
			NegativeExit->AppliedEnergyChangeCM2PerSec2,
			AblatedExit->AppliedEnergyChangeCM2PerSec2));
		TestTrue(TEXT("Approved virtual velocity produces positive net energy"),
			PositiveExit->AppliedEnergyChangeCM2PerSec2 > 100.0);
		TestTrue(TEXT("Reversed virtual velocity produces negative net energy"),
			NegativeExit->AppliedEnergyChangeCM2PerSec2 < -100.0);
		TestTrue(TEXT("Positive fixture does not saturate its energy limit"),
			FMath::Abs(PositiveExit->RawEnergyChangeCM2PerSec2)
				< PositiveRequest.Scenario.Bodies[1].MaximumEnergyChangeCM2PerSec2
					- 1000.0);
		const double PositiveEnergyTolerance =
			1.0e-8 * FMath::Max(
				1.0, FMath::Abs(PositiveExit->RequestedEnergyChangeCM2PerSec2));
		TestTrue(TEXT("Inner-corridor request preserves raw virtual energy"),
			FMath::Abs(
				PositiveExit->RequestedEnergyChangeCM2PerSec2
					- PositiveExit->RawEnergyChangeCM2PerSec2)
				<= PositiveEnergyTolerance);
		TestTrue(TEXT("The normalized kick applies the complete requested energy"),
			FMath::Abs(
				PositiveExit->AppliedEnergyChangeCM2PerSec2
					- PositiveExit->RequestedEnergyChangeCM2PerSec2)
				<= PositiveEnergyTolerance);
		const double InitialPrimaryEnergy =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				PositiveRequest.Scenario.GetPrimary(),
				PositiveRequest.InitialPositionCM,
				PositiveRequest.InitialVelocityCMPerSec);
		const double PositiveExitPrimaryEnergy =
			FABTSM11GravityAssistSolver::ComputePrimarySpecificEnergy(
				PositiveRequest.Scenario.GetPrimary(),
				PositiveExit->PositionCM,
				PositiveExit->VelocityCMPerSec);
		TestTrue(TEXT("The full influence-shell energy step equals the applied budget"),
			FMath::Abs(
				PositiveExitPrimaryEnergy - InitialPrimaryEnergy
					- PositiveExit->AppliedEnergyChangeCM2PerSec2) < 0.1);
		TestTrue(TEXT("Assist mask removes gameplay energy without removing natural gravity"),
			AblatedExit->AppliedEnergyChangeCM2PerSec2 == 0.0
				&& FMath::IsNearlyEqual(
					AblatedExit->NaturalDeflectionRadians,
					PositiveExit->NaturalDeflectionRadians,
					1.0e-12));
		const FABTSM11TrajectoryEvent* PositiveClosest =
			Positive.FindAssistEvent(
				EABTSM11TrajectoryEventType::ClosestApproach, 1);
		TestNotNull(TEXT("Positive fixture records closest approach"), PositiveClosest);
		if (PositiveClosest != nullptr)
		{
			double FirstVelocityDivergenceTime = TNumericLimits<double>::Max();
			double FirstVelocityDivergenceRadius = TNumericLimits<double>::Max();
			double QuarterKernelEnergy = 0.0;
			double HalfKernelEnergy = 0.0;
			double ThreeQuarterKernelEnergy = 0.0;
			bool bSampledQuarterKernel = false;
			bool bSampledHalfKernel = false;
			bool bSampledThreeQuarterKernel = false;
			bool bPreClosestStreamsIdentical = true;
			const int32 SharedPointCount =
				FMath::Min(Positive.Points.Num(), Ablated.Points.Num());
			for (int32 PointIndex = 0; PointIndex < SharedPointCount; ++PointIndex)
			{
				const FABTSM11TrajectoryPoint& AssistedPoint =
					Positive.Points[PointIndex];
				const FABTSM11TrajectoryPoint& NaturalPoint =
					Ablated.Points[PointIndex];
				if (AssistedPoint.TimeSeconds
					<= PositiveClosest->TimeSeconds + 1.0e-10)
				{
					bPreClosestStreamsIdentical &=
						AssistedPoint.TimeSeconds == NaturalPoint.TimeSeconds
						&& AssistedPoint.PositionCM == NaturalPoint.PositionCM
						&& AssistedPoint.VelocityCMPerSec
							== NaturalPoint.VelocityCMPerSec;
					continue;
				}
				if (FirstVelocityDivergenceTime == TNumericLimits<double>::Max()
					&& AssistedPoint.TimeSeconds == NaturalPoint.TimeSeconds
					&& (AssistedPoint.VelocityCMPerSec
						- NaturalPoint.VelocityCMPerSec).Length() > 1.0e-9)
				{
					FirstVelocityDivergenceTime = AssistedPoint.TimeSeconds;
					FirstVelocityDivergenceRadius =
						(AssistedPoint.PositionCM
							- PositiveRequest.Scenario.Bodies[1].CenterCM).Length();
				}
				if (AssistedPoint.TimeSeconds == NaturalPoint.TimeSeconds)
				{
					const double AssistedRadiusCM =
						(AssistedPoint.PositionCM
							- PositiveRequest.Scenario.Bodies[1].CenterCM).Length();
					const double KernelProgress = FMath::Clamp(
						(AssistedRadiusCM - PositiveClosest->ClosestDistanceCM)
							/ (PositiveRequest.Scenario.Bodies[1]
								.AssistReferenceRadiusCM
								- PositiveClosest->ClosestDistanceCM),
						0.0,
						1.0);
					const double KineticEnergyDifference =
						0.5 * (
							AssistedPoint.VelocityCMPerSec.SquaredLength()
							- NaturalPoint.VelocityCMPerSec.SquaredLength());
					if (!bSampledQuarterKernel && KernelProgress >= 0.25)
					{
						QuarterKernelEnergy = KineticEnergyDifference;
						bSampledQuarterKernel = true;
					}
					if (!bSampledHalfKernel && KernelProgress >= 0.5)
					{
						HalfKernelEnergy = KineticEnergyDifference;
						bSampledHalfKernel = true;
					}
					if (!bSampledThreeQuarterKernel && KernelProgress >= 0.75)
					{
						ThreeQuarterKernelEnergy = KineticEnergyDifference;
						bSampledThreeQuarterKernel = true;
					}
				}
			}
			TestTrue(TEXT("Gameplay energy leaves the inbound natural arc untouched"),
				bPreClosestStreamsIdentical);
			TestTrue(TEXT("The outbound kernel starts applying energy before reference exit"),
				FMath::IsFinite(FirstVelocityDivergenceTime)
					&& FirstVelocityDivergenceTime
						< TNumericLimits<double>::Max()
					&& FirstVelocityDivergenceTime > PositiveClosest->TimeSeconds
					&& FirstVelocityDivergenceRadius
						< PositiveRequest.Scenario.Bodies[1]
							.AssistReferenceRadiusCM);
			AddInfo(FString::Printf(
				TEXT("M11-A outbound kernel samples: q25=%.3f q50=%.3f q75=%.3f final=%.3f"),
				QuarterKernelEnergy,
				HalfKernelEnergy,
				ThreeQuarterKernelEnergy,
				PositiveExit->AppliedEnergyChangeCM2PerSec2));
			TestTrue(TEXT("The outbound energy budget is distributed over the kernel"),
				bSampledQuarterKernel
					&& bSampledHalfKernel
					&& bSampledThreeQuarterKernel
					&& QuarterKernelEnergy
						> PositiveExit->AppliedEnergyChangeCM2PerSec2 * 0.001
					&& QuarterKernelEnergy < HalfKernelEnergy
					&& HalfKernelEnergy < ThreeQuarterKernelEnergy
					&& QuarterKernelEnergy
						< PositiveExit->AppliedEnergyChangeCM2PerSec2 * 0.5
					&& ThreeQuarterKernelEnergy
						> PositiveExit->AppliedEnergyChangeCM2PerSec2 * 0.5
					&& ThreeQuarterKernelEnergy
						< PositiveExit->AppliedEnergyChangeCM2PerSec2 * 1.2);
		}
		const double VInfinityCMPerSec = FMath::Sqrt(
			FMath::Square(PositiveExit->EntrySpeedCMPerSec)
			- 2.0 * PositiveRequest.Scenario.Bodies[1]
				.GravitationalParameterCM3PerSec2
				/ PositiveRequest.Scenario.Bodies[1].AssistReferenceRadiusCM);
		const double ExpectedBPlaneMagnitudeCM = FVector3d::CrossProduct(
			PositiveRequest.InitialPositionCM
				- PositiveRequest.Scenario.Bodies[1].CenterCM,
			PositiveRequest.InitialVelocityCMPerSec).Length() / VInfinityCMPerSec;
		const double ObservedBPlaneMagnitudeCM = FMath::Sqrt(
			FMath::Square(PositiveExit->BPlaneTCM)
			+ FMath::Square(PositiveExit->BPlaneRCM));
		TestTrue(TEXT("B-plane magnitude matches h over v-infinity"),
			FMath::Abs(
				ObservedBPlaneMagnitudeCM - ExpectedBPlaneMagnitudeCM)
				/ ExpectedBPlaneMagnitudeCM < 2.0e-4);
		TestTrue(TEXT("Fixture passes on the negative-R side"),
			PositiveExit->BPlaneRCM < 0.0);
		TestTrue(TEXT("A disallowed side cannot receive positive assist"),
			WrongSideExit->RawEnergyChangeCM2PerSec2 > 0.0
				&& WrongSideExit->CorridorQuality == 1.0
				&& WrongSideExit->RequestedEnergyChangeCM2PerSec2 == 0.0
				&& WrongSideExit->AppliedEnergyChangeCM2PerSec2 == 0.0);
		TestTrue(TEXT("The annular corridor produces a partial quality"),
			PartialCorridorExit->CorridorQuality > 0.0
				&& PartialCorridorExit->CorridorQuality < 1.0);
		const double ExpectedPartialEnergy =
			PartialCorridorExit->RawEnergyChangeCM2PerSec2
			* FMath::Pow(
				PartialCorridorExit->CorridorQuality,
				PartialCorridorRequest.Config.EnergyQualityPower);
		TestTrue(TEXT("Partial quality deterministically scales requested energy"),
			FMath::IsNearlyEqual(
				PartialCorridorExit->RequestedEnergyChangeCM2PerSec2,
				ExpectedPartialEnergy,
				1.0e-6));
		TestTrue(TEXT("Partial requested energy is fully applied"),
			FMath::IsNearlyEqual(
				PartialCorridorExit->AppliedEnergyChangeCM2PerSec2,
				PartialCorridorExit->RequestedEnergyChangeCM2PerSec2,
				1.0e-6));
		TestTrue(TEXT("The outer corridor rejects gameplay energy"),
			OutsideCorridorExit->BPlaneChiSquared
					>= OutsideCorridorRequest.Scenario.Bodies[1]
						.BPlaneOuterChiSquared
				&& OutsideCorridorExit->CorridorQuality == 0.0
				&& OutsideCorridorExit->RequestedEnergyChangeCM2PerSec2 == 0.0
				&& OutsideCorridorExit->AppliedEnergyChangeCM2PerSec2 == 0.0);

		const double NaturalReferenceExitTime = FindOutboundRadiusCrossingTime(
			Ablated,
			PositiveRequest.Scenario.Bodies[1].CenterCM,
			PositiveRequest.Scenario.Bodies[1].AssistReferenceRadiusCM,
			PositiveClosest != nullptr ? PositiveClosest->TimeSeconds : 0.0);
		const double NegativeReferenceExitTime = FindOutboundRadiusCrossingTime(
			Negative,
			NegativeRequest.Scenario.Bodies[1].CenterCM,
			NegativeRequest.Scenario.Bodies[1].AssistReferenceRadiusCM,
			PositiveClosest != nullptr ? PositiveClosest->TimeSeconds : 0.0);
		TestTrue(TEXT("Negative gameplay energy delays reference-sphere exit"),
			NaturalReferenceExitTime < TNumericLimits<double>::Max()
				&& NegativeReferenceExitTime < TNumericLimits<double>::Max()
				&& NegativeReferenceExitTime > NaturalReferenceExitTime);
		if (NegativeReferenceExitTime > NaturalReferenceExitTime
			&& NegativeReferenceExitTime < TNumericLimits<double>::Max())
		{
			FABTSM11TrajectoryRequest CalibrationHorizonRequest = NegativeRequest;
			CalibrationHorizonRequest.Scenario.ScenarioHash = 0x11a00bu;
			CalibrationHorizonRequest.Config.MaximumSimulationTimeSeconds =
				0.5 * (NaturalReferenceExitTime + NegativeReferenceExitTime);
			FABTSM11TrajectoryResult CalibrationHorizonResult;
			TestTrue(TEXT("Calibration-horizon solve runs"),
				FABTSM11GravityAssistSolver::Solve(
					CalibrationHorizonRequest, CalibrationHorizonResult));
			TestEqual(
				TEXT("Request horizon wins when corrected calibration would exit later"),
				CalibrationHorizonResult.Termination,
				EABTSM11TrajectoryTermination::Timeout);
			TestTrue(TEXT("Calibration cannot predict points beyond the request horizon"),
				!CalibrationHorizonResult.Points.IsEmpty()
					&& FMath::IsNearlyEqual(
						CalibrationHorizonResult.Points.Last().TimeSeconds,
						CalibrationHorizonRequest.Config
							.MaximumSimulationTimeSeconds,
						1.0e-9));
		}
	}

	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		FABTSM11TrajectoryRequest EnabledRequest =
			MakeNaturalFlybyRequest(FVector3d(0.0, -50.0, 0.0));
		MoveNaturalFlybyToAssist(EnabledRequest, AssistIndex);
		FABTSM11TrajectoryRequest DisabledRequest = EnabledRequest;
		DisabledRequest.Config.EnabledAssistMask &=
			~static_cast<uint8>(1u << (AssistIndex - 1));
		FABTSM11TrajectoryResult EnabledResult;
		FABTSM11TrajectoryResult DisabledResult;
		TestTrue(
			FString::Printf(TEXT("Assist %d enabled solve runs"), AssistIndex),
			FABTSM11GravityAssistSolver::Solve(EnabledRequest, EnabledResult));
		TestTrue(
			FString::Printf(TEXT("Assist %d ablated solve runs"), AssistIndex),
			FABTSM11GravityAssistSolver::Solve(DisabledRequest, DisabledResult));
		const FABTSM11TrajectoryEvent* EnabledExit =
			EnabledResult.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistExit, AssistIndex);
		const FABTSM11TrajectoryEvent* DisabledExit =
			DisabledResult.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistExit, AssistIndex);
		TestNotNull(
			FString::Printf(TEXT("Assist %d enabled flyby exits"), AssistIndex),
			EnabledExit);
		TestNotNull(
			FString::Printf(TEXT("Assist %d ablated flyby still exits"), AssistIndex),
			DisabledExit);
		if (EnabledExit != nullptr && DisabledExit != nullptr)
		{
			TestTrue(
				FString::Printf(TEXT("Assist %d independently exchanges energy"), AssistIndex),
				EnabledExit->AppliedEnergyChangeCM2PerSec2 > 100.0);
			TestTrue(
				FString::Printf(TEXT("Assist %d mask bit independently ablates energy"), AssistIndex),
				DisabledExit->AppliedEnergyChangeCM2PerSec2 == 0.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ATargetQualificationTest,
	"ABTS.M11A.TargetQualification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ATargetQualificationTest::RunTest(const FString& Parameters)
{
	const FABTSM11TrajectoryRequest DefaultRequest = MakeSweptRequest(false);
	TestEqual(TEXT("Generic targets remain backward-compatible by default"),
		DefaultRequest.Scenario.Target.RequiredQualifiedAssistCount, 0);

	FABTSM11TrajectoryResult DefaultResult;
	TestTrue(TEXT("Default target-qualification request solves"),
		FABTSM11GravityAssistSolver::Solve(DefaultRequest, DefaultResult));
	TestEqual(TEXT("A default target can be hit without a completed assist"),
		DefaultResult.Termination,
		EABTSM11TrajectoryTermination::TargetHit);
	TestTrue(TEXT("Default TargetHit is also a geometric contact"),
		DefaultResult.DidContactTarget());
	TestEqual(TEXT("Default TargetHit records one geometric contact"),
		DefaultResult.TargetContactCount, 1);

	FABTSM11TrajectoryRequest GatedRequest = DefaultRequest;
	GatedRequest.Scenario.ScenarioHash = 0x11a00du;
	GatedRequest.Scenario.Target.RequiredQualifiedAssistCount = 1;
	GatedRequest.Scenario.Target.HitRadiusCM = 100.0;
	GatedRequest.Scenario.Target.GeometricContactRadiusCM = 10.0;
	GatedRequest.InitialPositionCM = FVector3d(-200.0, 0.0, 0.0);
	GatedRequest.Config.MaximumSimulationTimeSeconds = 2.0;
	FABTSM11TrajectoryResult GatedResult;
	TestTrue(TEXT("Qualified-assist-gated target request solves"),
		FABTSM11GravityAssistSolver::Solve(GatedRequest, GatedResult));
	TestEqual(TEXT("An unqualified target crossing is not a success"),
		GatedResult.Termination,
		EABTSM11TrajectoryTermination::Timeout);
	TestNull(TEXT("No TargetHit event is emitted before the required assist"),
		GatedResult.FindFirstEvent(EABTSM11TrajectoryEventType::TargetHit));
	TestNotNull(TEXT("Unqualified swept crossing emits TargetContact"),
		GatedResult.FindFirstEvent(EABTSM11TrajectoryEventType::TargetContact));
	TestTrue(TEXT("Unqualified swept crossing is geometrically observable"),
		GatedResult.DidContactTarget());
	TestEqual(TEXT("Unqualified swept crossing is counted once"),
		GatedResult.TargetContactCount, 1);

	FABTSM11TrajectoryRequest QualifiedRequest = GatedRequest;
	QualifiedRequest.Scenario.ScenarioHash = 0x11a00eu;
	QualifiedRequest.InitialExpectedAssistIndex = 2;
	FABTSM11TrajectoryResult QualifiedResult;
	TestTrue(TEXT("Pre-qualified target request solves"),
		FABTSM11GravityAssistSolver::Solve(
			QualifiedRequest, QualifiedResult));
	TestEqual(TEXT("Qualification activates the larger success envelope"),
		QualifiedResult.Termination,
		EABTSM11TrajectoryTermination::TargetHit);
	TestNotNull(TEXT("Qualified success envelope emits TargetHit"),
		QualifiedResult.FindFirstEvent(EABTSM11TrajectoryEventType::TargetHit));
	TestNull(TEXT("Qualified success needs no physical-contact event"),
		QualifiedResult.FindFirstEvent(
			EABTSM11TrajectoryEventType::TargetContact));
	TestEqual(TEXT("Outer success envelope does not fake physical contact"),
		QualifiedResult.TargetContactCount, 0);

	FABTSM11TrajectoryRequest NoObservationRequest = GatedRequest;
	NoObservationRequest.Scenario.ScenarioHash = 0x11a00fu;
	NoObservationRequest.Scenario.Target.CenterCM =
		FVector3d(10000.0, 0.0, 0.0);
	NoObservationRequest.Scenario.Target.HitRadiusCM = 10.0;
	NoObservationRequest.Scenario.Target.GeometricContactRadiusCM = 0.0;
	FABTSM11TrajectoryRequest ObservationRequest = NoObservationRequest;
	ObservationRequest.Scenario.ScenarioHash = 0x11a011u;
	ObservationRequest.Scenario.Target.GeometricContactRadiusCM = 10.0;
	ObservationRequest.Scenario.Target
		.bUseSeparateGeometricContactCenter = true;
	ObservationRequest.Scenario.Target.GeometricContactCenterCM =
		FVector3d::ZeroVector;
	FABTSM11TrajectoryResult NoObservationResult;
	FABTSM11TrajectoryResult ObservationResult;
	TestTrue(TEXT("No-observation comparison request solves"),
		FABTSM11GravityAssistSolver::Solve(
			NoObservationRequest, NoObservationResult));
	TestTrue(TEXT("Separated observation-only target request solves"),
		FABTSM11GravityAssistSolver::Solve(
			ObservationRequest, ObservationResult));
	TestNotNull(TEXT("Separated physical center emits TargetContact"),
		ObservationResult.FindFirstEvent(
			EABTSM11TrajectoryEventType::TargetContact));
	TestEqual(TEXT("Observation-only contact preserves termination"),
		ObservationResult.Termination,
		NoObservationResult.Termination);
	if (!NoObservationResult.Points.IsEmpty()
		&& !ObservationResult.Points.IsEmpty())
	{
		const FABTSM11TrajectoryPoint& WithoutObservation =
			NoObservationResult.Points.Last();
		const FABTSM11TrajectoryPoint& WithObservation =
			ObservationResult.Points.Last();
		TestEqual(TEXT("Observation-only contact preserves end time"),
			WithObservation.TimeSeconds,
			WithoutObservation.TimeSeconds);
		TestTrue(TEXT("Observation-only contact preserves end position"),
			WithObservation.PositionCM.Equals(
				WithoutObservation.PositionCM, 1.0e-12));
		TestTrue(TEXT("Observation-only contact preserves end velocity"),
			WithObservation.VelocityCMPerSec.Equals(
				WithoutObservation.VelocityCMPerSec, 1.0e-12));
	}

	FABTSM11TrajectoryRequest OtherGeometryRequest =
		NoObservationRequest;
	OtherGeometryRequest.Scenario.ScenarioHash =
		ObservationRequest.Scenario.ScenarioHash;
	OtherGeometryRequest.Scenario.Target.GeometricContactRadiusCM = 10.0;
	OtherGeometryRequest.Scenario.Target
		.bUseSeparateGeometricContactCenter = true;
	OtherGeometryRequest.Scenario.Target.GeometricContactCenterCM =
		FVector3d(5000.0, 5000.0, 0.0);
	FABTSM11TrajectoryResult OtherGeometryResult;
	TestTrue(TEXT("Geometry-only hash comparison request solves"),
		FABTSM11GravityAssistSolver::Solve(
			OtherGeometryRequest, OtherGeometryResult));
	TestTrue(TEXT("Separated geometric center changes result identity"),
		OtherGeometryResult.ValidationHash
			!= NoObservationResult.ValidationHash);
	TestTrue(TEXT("Two separated geometric centers change result identity"),
		OtherGeometryResult.ValidationHash
			!= ObservationResult.ValidationHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11ASweptEventsTest,
	"ABTS.M11A.SweptAnalyticEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11ASweptEventsTest::RunTest(const FString& Parameters)
{
	const FABTSM11TrajectoryRequest TargetRequest = MakeSweptRequest(false);
	const FABTSM11TrajectoryRequest BodyRequest = MakeSweptRequest(true);
	FABTSM11TrajectoryRequest WrongOrderRequest = MakeSweptRequest(false);
	WrongOrderRequest.Scenario.ScenarioHash = 0x11a005u;
	WrongOrderRequest.Scenario.Bodies[1].CenterCM = FVector3d(1.0e8, 0.0, 0.0);
	WrongOrderRequest.Scenario.Bodies[2] =
		MakeAssist(2, FVector3d::ZeroVector, 1.0, 10.0, 50.0, 60.0);
	WrongOrderRequest.Scenario.Bodies[3].CenterCM = FVector3d(1.2e8, 0.0, 0.0);
	WrongOrderRequest.Scenario.Target.CenterCM = FVector3d(80.0, 30.0, 0.0);
	WrongOrderRequest.InitialPositionCM = FVector3d(-100.0, 30.0, 0.0);
	FABTSM11TrajectoryRequest SameRootBodyTargetRequest = MakeSweptRequest(false);
	SameRootBodyTargetRequest.Scenario.ScenarioHash = 0x11a006u;
	SameRootBodyTargetRequest.Scenario.Bodies[0].CenterCM = FVector3d::ZeroVector;
	SameRootBodyTargetRequest.Scenario.Bodies[0].MinimumEvaluationRadiusCM = 10.0;
	SameRootBodyTargetRequest.Scenario.Bodies[0].VisualRadiusCM = 10.0;
	SameRootBodyTargetRequest.Scenario.Bodies[0].CollisionRadiusCM = 10.0;
	SameRootBodyTargetRequest.Scenario.Bodies[0].MaximumSimulationRadiusCM = 1.0e9;
	SameRootBodyTargetRequest.Scenario.Target.CenterCM = FVector3d::ZeroVector;
	SameRootBodyTargetRequest.Scenario.Target.HitRadiusCM = 10.0;
	FABTSM11TrajectoryRequest SameRootTargetOrderRequest = WrongOrderRequest;
	SameRootTargetOrderRequest.Scenario.ScenarioHash = 0x11a007u;
	SameRootTargetOrderRequest.Scenario.Target.CenterCM = FVector3d::ZeroVector;
	SameRootTargetOrderRequest.Scenario.Target.HitRadiusCM = 60.0;
	FABTSM11TrajectoryRequest TargetBeforePredictedBodyRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	TargetBeforePredictedBodyRequest.Scenario.ScenarioHash = 0x11a008u;
	TargetBeforePredictedBodyRequest.InitialPositionCM =
		FVector3d(-6000.0, 0.0, 0.0);
	TargetBeforePredictedBodyRequest.Scenario.Target.CenterCM =
		FVector3d(-3000.0, 0.0, 0.0);
	TargetBeforePredictedBodyRequest.Scenario.Target.HitRadiusCM = 100.0;
	FABTSM11TrajectoryRequest HorizonBeforePredictedBodyRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	HorizonBeforePredictedBodyRequest.Scenario.ScenarioHash = 0x11a009u;
	HorizonBeforePredictedBodyRequest.InitialPositionCM =
		FVector3d(-5100.0, 0.0, 0.0);
	HorizonBeforePredictedBodyRequest.Config.MaximumSimulationTimeSeconds = 5.0;
	FABTSM11TrajectoryRequest DeferredBodyRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	DeferredBodyRequest.Scenario.ScenarioHash = 0x11a00cu;
	DeferredBodyRequest.InitialPositionCM = FVector3d(-6000.0, 0.0, 0.0);
	const FABTSM11TrajectoryRequest NaturalBaselineRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	FABTSM11TrajectoryResult NaturalBaselineResult;
	TestTrue(TEXT("Natural baseline for the post-closest horizon runs"),
		FABTSM11GravityAssistSolver::Solve(
			NaturalBaselineRequest, NaturalBaselineResult));
	const FABTSM11TrajectoryEvent* BaselineClosest =
		NaturalBaselineResult.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach, 1);
	FABTSM11TrajectoryRequest HorizonAfterClosestRequest = NaturalBaselineRequest;
	HorizonAfterClosestRequest.Scenario.ScenarioHash = 0x11a00au;
	if (BaselineClosest != nullptr)
	{
		HorizonAfterClosestRequest.Config.MaximumSimulationTimeSeconds =
			BaselineClosest->TimeSeconds + 0.5;
	}
	FABTSM11TrajectoryResult TargetResult;
	FABTSM11TrajectoryResult BodyResult;
	FABTSM11TrajectoryResult WrongOrderResult;
	FABTSM11TrajectoryResult SameRootBodyTargetResult;
	FABTSM11TrajectoryResult SameRootTargetOrderResult;
	FABTSM11TrajectoryResult TargetBeforePredictedBodyResult;
	FABTSM11TrajectoryResult HorizonBeforePredictedBodyResult;
	FABTSM11TrajectoryResult DeferredBodyResult;
	FABTSM11TrajectoryResult HorizonAfterClosestResult;
	TestTrue(TEXT("One-step target sweep runs"),
		FABTSM11GravityAssistSolver::Solve(TargetRequest, TargetResult));
	TestTrue(TEXT("One-step body sweep runs"),
		FABTSM11GravityAssistSolver::Solve(BodyRequest, BodyResult));
	TestTrue(TEXT("One-step wrong-order influence sweep runs"),
		FABTSM11GravityAssistSolver::Solve(WrongOrderRequest, WrongOrderResult));
	TestTrue(TEXT("Same-root body/target sweep runs"),
		FABTSM11GravityAssistSolver::Solve(
			SameRootBodyTargetRequest, SameRootBodyTargetResult));
	TestTrue(TEXT("Same-root target/wrong-order sweep runs"),
		FABTSM11GravityAssistSolver::Solve(
			SameRootTargetOrderRequest, SameRootTargetOrderResult));
	TestTrue(TEXT("Target-before-predicted-body solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			TargetBeforePredictedBodyRequest,
			TargetBeforePredictedBodyResult));
	TestTrue(TEXT("Horizon-before-predicted-body solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			HorizonBeforePredictedBodyRequest,
			HorizonBeforePredictedBodyResult));
	TestTrue(TEXT("Deferred predicted-body collision solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			DeferredBodyRequest,
			DeferredBodyResult));
	TestNotNull(TEXT("Natural baseline reaches closest approach"), BaselineClosest);
	TestTrue(TEXT("Post-closest horizon solve runs"),
		FABTSM11GravityAssistSolver::Solve(
			HorizonAfterClosestRequest,
			HorizonAfterClosestResult));
	TestEqual(TEXT("A segment with both endpoints outside still hits the UFO"),
		TargetResult.Termination, EABTSM11TrajectoryTermination::TargetHit);
	TestEqual(TEXT("A segment with both endpoints outside still hits the planet"),
		BodyResult.Termination, EABTSM11TrajectoryTermination::BodyCollision);
	TestEqual(TEXT("Earlier assist-2 entry beats a later target root and reports WrongOrder"),
		WrongOrderResult.Termination, EABTSM11TrajectoryTermination::WrongOrder);
	TestEqual(TEXT("Body collision wins an exactly shared target root"),
		SameRootBodyTargetResult.Termination,
		EABTSM11TrajectoryTermination::BodyCollision);
	TestEqual(TEXT("Target hit wins an exactly shared wrong-order entry root"),
		SameRootTargetOrderResult.Termination,
		EABTSM11TrajectoryTermination::TargetHit);
	TestEqual(TEXT("An inbound target root beats a later predicted planet impact"),
		TargetBeforePredictedBodyResult.Termination,
		EABTSM11TrajectoryTermination::TargetHit);
	TestEqual(TEXT("The requested horizon beats a later predicted planet impact"),
		HorizonBeforePredictedBodyResult.Termination,
		EABTSM11TrajectoryTermination::Timeout);
	TestEqual(TEXT("A deferred predicted collision is emitted by the live solver"),
		DeferredBodyResult.Termination,
		EABTSM11TrajectoryTermination::BodyCollision);
	const FABTSM11TrajectoryEvent* DeferredBodyHit =
		DeferredBodyResult.FindFirstEvent(
			EABTSM11TrajectoryEventType::BodyCollision);
	TestNotNull(TEXT("Deferred live BodyCollision event exists"), DeferredBodyHit);
	if (DeferredBodyHit != nullptr)
	{
		TestEqual(TEXT("Deferred live collision identifies assist 1"),
			DeferredBodyHit->AssistIndex, 1);
		TestTrue(TEXT("Deferred live collision is rooted on the analytic body"),
			FMath::Abs(
				(DeferredBodyHit->PositionCM
					- DeferredBodyRequest.Scenario.Bodies[1].CenterCM).Length()
					- DeferredBodyRequest.Scenario.Bodies[1].CollisionRadiusCM)
				< 0.1);
	}
	TestTrue(TEXT("Predicted hard events cannot emit points beyond the horizon"),
		!HorizonBeforePredictedBodyResult.Points.IsEmpty()
			&& FMath::IsNearlyEqual(
				HorizonBeforePredictedBodyResult.Points.Last().TimeSeconds,
				HorizonBeforePredictedBodyRequest.Config
					.MaximumSimulationTimeSeconds,
				1.0e-9));
	TestEqual(TEXT("A post-closest request horizon beats later clone calibration"),
		HorizonAfterClosestResult.Termination,
		EABTSM11TrajectoryTermination::Timeout);
	TestNotNull(TEXT("The live solve reaches closest approach before that horizon"),
		HorizonAfterClosestResult.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach, 1));
	TestTrue(TEXT("Post-closest timeout is rooted on the requested horizon"),
		!HorizonAfterClosestResult.Points.IsEmpty()
			&& FMath::IsNearlyEqual(
				HorizonAfterClosestResult.Points.Last().TimeSeconds,
				HorizonAfterClosestRequest.Config.MaximumSimulationTimeSeconds,
				1.0e-9));
	const FABTSM11TrajectoryEvent* TargetHit =
		TargetResult.FindFirstEvent(EABTSM11TrajectoryEventType::TargetHit);
	const FABTSM11TrajectoryEvent* BodyHit =
		BodyResult.FindFirstEvent(EABTSM11TrajectoryEventType::BodyCollision);
	TestNotNull(TEXT("TargetHit event exists"), TargetHit);
	TestNotNull(TEXT("BodyCollision event exists"), BodyHit);
	if (TargetHit != nullptr && BodyHit != nullptr)
	{
		TestTrue(TEXT("Swept target root is near the analytic 0.45 second crossing"),
			FMath::IsNearlyEqual(TargetHit->TimeSeconds, 0.45, 1.0e-5));
		TestTrue(TEXT("Swept body root is near the analytic 0.45 second crossing"),
			FMath::IsNearlyEqual(BodyHit->TimeSeconds, 0.45, 1.0e-5));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11AStableFailureTest,
	"ABTS.M11A.StableFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11AStableFailureTest::RunTest(const FString& Parameters)
{
	FABTSM11TrajectoryRequest Request =
		MakeNaturalFlybyRequest(FVector3d(0.0, -50.0, 0.0));
	Request.Scenario.Bodies[1].BPlaneReferenceNormal = FVector3d(1.0, 0.0, 0.0);
	Request.Scenario.Bodies[1].BPlaneFallbackAxis = FVector3d(1.0, 0.0, 0.0);
	Request.Config.BPlaneBasisMinimumLength = 0.02;

	FABTSM11TrajectoryResult First;
	FABTSM11TrajectoryResult Second;
	TestTrue(TEXT("Degenerate B-plane is a solved, stable failure"),
		FABTSM11GravityAssistSolver::Solve(Request, First));
	TestTrue(TEXT("Degenerate B-plane repeats"),
		FABTSM11GravityAssistSolver::Solve(Request, Second));
	TestEqual(TEXT("Degenerate basis has a dedicated termination"),
		First.Termination, EABTSM11TrajectoryTermination::AssistInvalidBPlaneBasis);
	TestTrue(TEXT("Stable failure result and hash repeat exactly"),
		ResultsExactlyEqual(First, Second));

	FABTSM11TrajectoryRequest InvalidHyperbolaRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	InvalidHyperbolaRequest.Config.MinimumVInfinityCMPerSec = 1000.0;
	FABTSM11TrajectoryResult InvalidHyperbolaFirst;
	FABTSM11TrajectoryResult InvalidHyperbolaSecond;
	TestTrue(TEXT("Low v-infinity is a solved, stable failure"),
		FABTSM11GravityAssistSolver::Solve(
			InvalidHyperbolaRequest, InvalidHyperbolaFirst));
	TestTrue(TEXT("Low v-infinity failure repeats"),
		FABTSM11GravityAssistSolver::Solve(
			InvalidHyperbolaRequest, InvalidHyperbolaSecond));
	TestEqual(TEXT("Low v-infinity has a dedicated termination"),
		InvalidHyperbolaFirst.Termination,
		EABTSM11TrajectoryTermination::AssistInvalidHyperbola);
	TestTrue(TEXT("Invalid-hyperbola result and hash repeat exactly"),
		ResultsExactlyEqual(InvalidHyperbolaFirst, InvalidHyperbolaSecond));

	FABTSM11TrajectoryRequest SubdivisionLimitRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	SubdivisionLimitRequest.Config.FixedTimeStepSeconds = 1.0;
	SubdivisionLimitRequest.Config.MaximumSubdivisionDepth = 0;
	SubdivisionLimitRequest.Config.CollisionStepRadiusFraction = 0.01;
	FABTSM11TrajectoryResult SubdivisionLimitFirst;
	FABTSM11TrajectoryResult SubdivisionLimitSecond;
	TestTrue(TEXT("Subdivision exhaustion is a solved numerical failure"),
		FABTSM11GravityAssistSolver::Solve(
			SubdivisionLimitRequest, SubdivisionLimitFirst));
	TestTrue(TEXT("Subdivision exhaustion repeats"),
		FABTSM11GravityAssistSolver::Solve(
			SubdivisionLimitRequest, SubdivisionLimitSecond));
	TestEqual(TEXT("Subdivision exhaustion cannot degrade to a physical timeout"),
		SubdivisionLimitFirst.Termination,
		EABTSM11TrajectoryTermination::AssistSolveFailed);
	TestTrue(TEXT("Subdivision failure remains at the initial state"),
		SubdivisionLimitFirst.Events.Num() == 1
			&& SubdivisionLimitFirst.Events[0].Type
				== EABTSM11TrajectoryEventType::AssistSolveFailed
			&& SubdivisionLimitFirst.Events[0].TimeSeconds
				== SubdivisionLimitRequest.InitialTimeSeconds
			&& SubdivisionLimitFirst.Events[0].PositionCM
				== SubdivisionLimitRequest.InitialPositionCM);
	TestTrue(TEXT("Subdivision-limit failure and hash repeat exactly"),
		ResultsExactlyEqual(SubdivisionLimitFirst, SubdivisionLimitSecond));

	FABTSM11TrajectoryRequest ShallowGrazeRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	ShallowGrazeRequest.InitialPositionCM = FVector3d(-6000.0, 4900.0, 0.0);
	FABTSM11TrajectoryResult ShallowGrazeFirst;
	FABTSM11TrajectoryResult ShallowGrazeSecond;
	TestTrue(TEXT("Shallow influence graze is a solved failure"),
		FABTSM11GravityAssistSolver::Solve(
			ShallowGrazeRequest, ShallowGrazeFirst));
	TestTrue(TEXT("Shallow influence graze repeats"),
		FABTSM11GravityAssistSolver::Solve(
			ShallowGrazeRequest, ShallowGrazeSecond));
	TestEqual(TEXT("Missing the reference sphere cannot complete an assist"),
		ShallowGrazeFirst.Termination,
		EABTSM11TrajectoryTermination::AssistSolveFailed);
	TestEqual(TEXT("Shallow influence graze completes no assists"),
		ShallowGrazeFirst.CompletedAssistCount, 0);
	TestNull(TEXT("Shallow influence graze emits no AssistExit"),
		ShallowGrazeFirst.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit, 1));
	TestEqual(TEXT("Shallow influence graze has a stable diagnostic"),
		ShallowGrazeFirst.Diagnostic, FString(TEXT("ReferenceSphereMissed")));
	TestTrue(TEXT("Shallow-graze failure and hash repeat exactly"),
		ResultsExactlyEqual(ShallowGrazeFirst, ShallowGrazeSecond));

	FABTSM11TrajectoryRequest CloneBudgetRequest =
		MakeNaturalFlybyRequest(FVector3d::ZeroVector);
	CloneBudgetRequest.Config.NaturalCloneMaximumStepCount = 1;
	FABTSM11TrajectoryResult CloneBudgetFirst;
	FABTSM11TrajectoryResult CloneBudgetSecond;
	TestTrue(TEXT("Natural-clone budget exhaustion is a solved failure"),
		FABTSM11GravityAssistSolver::Solve(
			CloneBudgetRequest, CloneBudgetFirst));
	TestTrue(TEXT("Natural-clone budget exhaustion repeats"),
		FABTSM11GravityAssistSolver::Solve(
			CloneBudgetRequest, CloneBudgetSecond));
	TestEqual(TEXT("Numerical clone exhaustion is not misreported as capture"),
		CloneBudgetFirst.Termination,
		EABTSM11TrajectoryTermination::AssistSolveFailed);
	TestTrue(TEXT("Clone-budget failure and hash repeat exactly"),
		ResultsExactlyEqual(CloneBudgetFirst, CloneBudgetSecond));
	return true;
}

#endif
