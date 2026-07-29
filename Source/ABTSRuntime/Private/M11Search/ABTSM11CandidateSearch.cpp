// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11CandidateSearch.h"

#include "M11Core/ABTSM11CoreSolver.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace ABTS::M11Search::SearchPrivate
{
	using namespace M11Core;

	constexpr double Pi = 3.14159265358979323846264338327950288;

	bool Reject(
		std::string* OutFailure,
		CandidateRecord* Candidate,
		const EvaluationStatus Status,
		const char* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		if (Candidate != nullptr)
		{
			Candidate->Status = Status;
			Candidate->Rejection = Reason;
		}
		return false;
	}

	double Halton(std::uint64_t Index, const std::uint32_t Base)
	{
		double Result = 0.0;
		double Fraction = 1.0;
		while (Index > 0)
		{
			Fraction /= static_cast<double>(Base);
			Result += Fraction
				* static_cast<double>(Index % Base);
			Index /= Base;
		}
		return Result;
	}

	double SampleRange(
		const std::uint64_t SampleIndex,
		const std::uint32_t Base,
		const double Minimum,
		const double Maximum)
	{
		return M11Core::Lerp(
			Minimum,
			Maximum,
			Halton(SampleIndex, Base));
	}

	std::uint32_t FoldScenarioHash(const std::uint64_t Value)
	{
		std::uint32_t Result = static_cast<std::uint32_t>(Value)
			^ static_cast<std::uint32_t>(Value >> 32);
		if (Result == 0)
		{
			Result = 0x11b21001u;
		}
		return Result;
	}

	struct WorkParameters
	{
		double FirstEncounterSeconds = 9.0;
		std::array<double, 2> InterEncounterCoastSeconds{6.0, 6.0};
		std::array<double, GravityScenario::AssistCount> InfluenceRadiusCM{};
		std::array<double, GravityScenario::AssistCount> GravityScale{};
		std::array<double, GravityScenario::AssistCount> VirtualSpeedCMPerSec{};
		std::array<double, GravityScenario::AssistCount> ImpactFraction{};
		std::array<double, GravityScenario::AssistCount> RadialFraction{};
		double TargetRadiusCM = 4000.0;
	};

	WorkParameters MakeWorkParameters(
		const CandidateSearchContract& Contract,
		const std::uint64_t GlobalWorkIndex)
	{
		const std::uint64_t SampleIndex =
			GlobalWorkIndex + 1ull + Contract.SearchSeed % 104729ull;
		WorkParameters Result;
		Result.FirstEncounterSeconds = SampleRange(
			SampleIndex,
			2,
			Contract.FirstEncounterMinimumSeconds,
			Contract.FirstEncounterMaximumSeconds);
		Result.InterEncounterCoastSeconds[0] = SampleRange(
			SampleIndex,
			3,
			Contract.InterEncounterCoastMinimumSeconds,
			Contract.InterEncounterCoastMaximumSeconds);
		Result.InterEncounterCoastSeconds[1] = SampleRange(
			SampleIndex,
			5,
			Contract.InterEncounterCoastMinimumSeconds,
			Contract.InterEncounterCoastMaximumSeconds);
		constexpr std::array<std::uint32_t, 3> RadiusBases{11, 13, 17};
		constexpr std::array<std::uint32_t, 3> GravityBases{19, 23, 29};
		constexpr std::array<std::uint32_t, 3> MomentumBases{31, 37, 41};
		constexpr std::array<std::uint32_t, 3> ImpactBases{43, 47, 53};
		constexpr std::array<std::uint32_t, 3> RadialBases{59, 61, 67};
		for (std::size_t Index = 0; Index < Result.InfluenceRadiusCM.size();
			++Index)
		{
			const double AssistBias =
				static_cast<double>(Index) * 0.08;
			Result.InfluenceRadiusCM[Index] = std::clamp(
				SampleRange(
					SampleIndex,
					RadiusBases[Index],
					Contract.MinimumInfluenceRadiusCM,
					Contract.MaximumInfluenceRadiusCM)
					* (1.0 + AssistBias),
				Contract.MinimumInfluenceRadiusCM,
				Contract.MaximumInfluenceRadiusCM);
			Result.GravityScale[Index] = SampleRange(
				SampleIndex,
				GravityBases[Index],
				Contract.MinimumGravityScale,
				Contract.MaximumGravityScale);
			Result.VirtualSpeedCMPerSec[Index] = SampleRange(
				SampleIndex,
				MomentumBases[Index],
				Contract.MinimumVirtualMomentumSpeedCMPerSec,
				Contract.MaximumVirtualMomentumSpeedCMPerSec);
			Result.ImpactFraction[Index] = SampleRange(
				SampleIndex,
				ImpactBases[Index],
				0.24,
				0.58);
			Result.RadialFraction[Index] = SampleRange(
				SampleIndex,
				RadialBases[Index],
				-0.22,
				0.22);
		}
		Result.TargetRadiusCM =
			SampleRange(
				SampleIndex,
				71,
				Contract.MinimumTargetHitRadiusCM,
				Contract.MaximumTargetHitRadiusCM * 0.75);
		return Result;
	}

	GravityBodySpec MakePrimary()
	{
		GravityBodySpec Body;
		Body.BodyId = 1100;
		Body.Role = GravityRole::Primary;
		Body.CenterCM = Vec3d(0.0, 0.0, -10000.0);
		Body.GravitationalParameterCM3PerSec2 = 5.665e9;
		Body.MinimumEvaluationRadiusCM = 1000.0;
		Body.VisualRadiusCM = 10000.0;
		Body.CollisionRadiusCM = 10000.0;
		Body.MaximumSimulationRadiusCM = 300000.0;
		Body.DebugColor = Color4f{0.1f, 0.35f, 0.7f, 1.0f};
		return Body;
	}

	GravityBodySpec MakeAssist(
		const std::int32_t AssistIndex,
		const Vec3d& CenterCM,
		const WorkParameters& Parameters)
	{
		const std::size_t Index =
			static_cast<std::size_t>(AssistIndex - 1);
		constexpr std::array<double, 3> BaseMu{
			8.0e9, 1.4e10, 2.4e10};
		GravityBodySpec Body;
		Body.BodyId = 1100 + AssistIndex;
		Body.Role = static_cast<GravityRole>(AssistIndex);
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 =
			BaseMu[Index] * Parameters.GravityScale[Index];
		Body.MinimumEvaluationRadiusCM = 450.0;
		Body.VisualRadiusCM = 1300.0 + AssistIndex * 180.0;
		Body.CollisionRadiusCM = 800.0;
		Body.InfluenceRadiusCM = Parameters.InfluenceRadiusCM[Index];
		Body.InfluenceBlendWidthCM = Body.InfluenceRadiusCM * 0.10;
		Body.AssistReferenceRadiusCM =
			Body.InfluenceRadiusCM - Body.InfluenceBlendWidthCM;
		Body.VirtualOrbitalVelocityCMPerSec = Vec3d();
		Body.BPlaneReferenceNormal = Vec3d(0.0, 0.0, 1.0);
		Body.BPlaneFallbackAxis = Vec3d(0.0, 1.0, 0.0);
		Body.BPlaneSigmaTCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneSigmaRCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneOuterChiSquared = 4.0;
		Body.AllowedPassSideValue = AllowedPassSide::Any;
		Body.MinimumEnergyChangeCM2PerSec2 = -8.0e6;
		Body.MaximumEnergyChangeCM2PerSec2 = 8.0e6;
		Body.DebugColor = AssistIndex == 1
			? Color4f{0.8f, 0.15f, 0.08f, 1.0f}
			: AssistIndex == 2
				? Color4f{0.75f, 0.55f, 0.12f, 1.0f}
				: Color4f{0.55f, 0.35f, 0.75f, 1.0f};
		return Body;
	}

	CandidateLayout MakeSeedLayout(
		const CandidateSearchContract& Contract,
		const WorkParameters& Parameters)
	{
		CandidateLayout Layout;
		Layout.LayoutVersion = 2;
		Layout.Launch.MaximumSimulationTimeSeconds =
			Contract.MaximumTotalFlightTimeSeconds;
		Layout.NominalInput = LaunchInput{0.0, 30.0, 1.0};
		Layout.Solver = SolverConfig::MakeV2();
		Layout.Solver.FixedTimeStepSeconds = 1.0 / 120.0;
		Layout.Solver.MaximumSimulationTimeSeconds =
			Contract.MaximumTotalFlightTimeSeconds;
		Layout.Solver.MaximumStepCount = 800000;
		Layout.Solver.MaximumSubdivisionDepth = 6;
		Layout.Solver.MaximumCoastStepExpansionDepth = 6;
		Layout.Solver.NaturalCloneMaximumTimeSeconds =
			Contract.MaximumTotalFlightTimeSeconds;
		Layout.Solver.NaturalCloneMaximumStepCount = 800000;
		Layout.Solver.MaximumNaturalDeflectionErrorRadians = 0.65;
		Layout.Solver.EnabledAssistMask = 0x7u;

		Layout.Scenario.LayoutVersion = 2;
		Layout.Scenario.ScenarioHash = 0x11b21001u;
		Layout.Scenario.Bodies[0] = MakePrimary();
		Layout.Scenario.Bodies[1] = MakeAssist(
			1, Vec3d(135000.0, 50000.0, 35000.0), Parameters);
		Layout.Scenario.Bodies[2] = MakeAssist(
			2, Vec3d(-110000.0, 120000.0, 65000.0), Parameters);
		Layout.Scenario.Bodies[3] = MakeAssist(
			3, Vec3d(90000.0, -135000.0, 85000.0), Parameters);

		TargetSpec& Target = Layout.Scenario.Target;
		Target.TargetId = 1199;
		Target.CenterCM = Vec3d(145000.0, -90000.0, 70000.0);
		Target.HitRadiusCM = Parameters.TargetRadiusCM;
		Target.GeometricContactRadiusCM = 800.0;
		Target.UseSeparateGeometricContactCenter = false;
		Target.GeometricContactCenterCM = Target.CenterCM;
		Target.RequiredQualifiedAssistCount = 0;
		Target.MinimumQualifyingCorridorQuality = 0.0;
		Target.MinimumQualifyingEnergyGainCM2PerSec2 = 0.0;
		Target.RequireAllowedPassSide = false;
		Target.PresentationForward = Vec3d(-1.0, 0.0, 0.0);
		return Layout;
	}

	void RefreshIdentity(
		CandidateLayout& Layout,
		const CandidateSearchContract& Contract)
	{
		Layout.Scenario.ScenarioHash = 1u;
		const std::uint64_t SourceHash =
			ComputeCandidateSourceHash(Layout, Contract);
		Layout.Scenario.ScenarioHash = FoldScenarioHash(SourceHash);
	}

	bool BuildAndSolve(
		const CandidateLayout& Layout,
		const LaunchInput& Input,
		const std::uint8_t EnabledAssistMask,
		TrajectoryRequest& OutRequest,
		TrajectoryResult& OutResult,
		std::int32_t& InOutSolveCount,
		std::string* OutFailure = nullptr)
	{
		if (!Layout.BuildRequest(
				Input,
				EnabledAssistMask,
				OutRequest,
				OutFailure))
		{
			return false;
		}
		++InOutSolveCount;
		return GravityAssistSolver::Solve(
			OutRequest, OutResult, OutFailure);
	}

	const TrajectoryPoint* FindPointAtOrAfter(
		const TrajectoryResult& Result,
		const double TimeSeconds)
	{
		const auto Iterator = std::lower_bound(
			Result.Points.begin(),
			Result.Points.end(),
			TimeSeconds,
			[](const TrajectoryPoint& Point, const double Time)
			{
				return Point.TimeSeconds < Time;
			});
		return Iterator != Result.Points.end() ? &*Iterator : nullptr;
	}

	double AngleBetween(const Vec3d& Left, const Vec3d& Right)
	{
		const Vec3d A = Left.GetSafeNormal();
		const Vec3d B = Right.GetSafeNormal();
		if (A.IsNearlyZero() || B.IsNearlyZero())
		{
			return 0.0;
		}
		return std::acos(std::clamp(
			Vec3d::DotProduct(A, B), -1.0, 1.0));
	}

	double ComputePartialLayoutTurn(
		const CandidateLayout& Layout,
		const std::int32_t LastAssistIndex)
	{
		if (LastAssistIndex < 2)
		{
			return Pi;
		}
		std::array<Vec3d, 4> Points{
			Layout.Launch.PouchLocalPositionCM,
			Layout.Scenario.GetAssist(1).CenterCM,
			Layout.Scenario.GetAssist(2).CenterCM,
			Layout.Scenario.GetAssist(3).CenterCM};
		double MinimumTurn = Pi;
		for (std::int32_t Index = 1;
			Index < LastAssistIndex;
			++Index)
		{
			MinimumTurn = std::min(
				MinimumTurn,
				AngleBetween(
					Points[static_cast<std::size_t>(Index)]
						- Points[static_cast<std::size_t>(Index - 1)],
					Points[static_cast<std::size_t>(Index + 1)]
						- Points[static_cast<std::size_t>(Index)]));
		}
		return MinimumTurn;
	}

	bool BuildImpactBasis(
		const GravityBodySpec& Body,
		const TrajectoryPoint& Point,
		Vec3d& OutT,
		Vec3d& OutR)
	{
		const Vec3d V = Point.VelocityCMPerSec.GetSafeNormal();
		OutT = Vec3d::CrossProduct(
			Body.BPlaneReferenceNormal.GetSafeNormal(), V).GetSafeNormal();
		if (OutT.IsNearlyZero())
		{
			OutT = Vec3d::CrossProduct(
				Body.BPlaneFallbackAxis.GetSafeNormal(), V).GetSafeNormal();
		}
		OutR = Vec3d::CrossProduct(V, OutT).GetSafeNormal();
		return !V.IsNearlyZero() && !OutT.IsNearlyZero() && !OutR.IsNearlyZero();
	}

	AllowedPassSide InferAllowedSide(const TrajectoryEvent& Exit)
	{
		if (std::abs(Exit.BPlaneTCM) >= std::abs(Exit.BPlaneRCM))
		{
			return Exit.BPlaneTCM >= 0.0
				? AllowedPassSide::PositiveT
				: AllowedPassSide::NegativeT;
		}
		return Exit.BPlaneRCM >= 0.0
			? AllowedPassSide::PositiveR
			: AllowedPassSide::NegativeR;
	}

	double ComputeAllowedSideMargin(
		const GravityBodySpec& Body,
		const TrajectoryEvent& Exit)
	{
		switch (Body.AllowedPassSideValue)
		{
		case AllowedPassSide::PositiveT:
			return Exit.BPlaneTCM;
		case AllowedPassSide::NegativeT:
			return -Exit.BPlaneTCM;
		case AllowedPassSide::PositiveR:
			return Exit.BPlaneRCM;
		case AllowedPassSide::NegativeR:
			return -Exit.BPlaneRCM;
		case AllowedPassSide::Any:
			return std::numeric_limits<double>::max();
		default:
			return -std::numeric_limits<double>::max();
		}
	}

	bool GeometryIsLegal(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const std::int32_t AssistIndex)
	{
		const GravityBodySpec& Primary = Layout.Scenario.GetPrimary();
		const GravityBodySpec& Candidate =
			Layout.Scenario.GetAssist(AssistIndex);
		const double PrimaryDistance =
			(Candidate.CenterCM - Primary.CenterCM).Length();
		if (PrimaryDistance <= Primary.CollisionRadiusCM
				+ Candidate.InfluenceRadiusCM
				+ Contract.MinimumBodyClearanceCM
			|| PrimaryDistance + Candidate.InfluenceRadiusCM
				>= Primary.MaximumSimulationRadiusCM)
		{
			return false;
		}
		for (std::int32_t OtherIndex = 1;
			OtherIndex < AssistIndex;
			++OtherIndex)
		{
			const GravityBodySpec& Other =
				Layout.Scenario.GetAssist(OtherIndex);
			if ((Candidate.CenterCM - Other.CenterCM).Length()
				<= Candidate.InfluenceRadiusCM
					+ Other.InfluenceRadiusCM
					+ Contract.MinimumBodyClearanceCM)
			{
				return false;
			}
		}
		return true;
	}

	struct StageCandidate
	{
		CandidateLayout Layout;
		TrajectoryResult Result;
		double ExitTimeSeconds = 0.0;
		double CorridorQuality = 0.0;
		double EnergyGain = 0.0;
		double DeflectionRadians = 0.0;
		double ClearanceCM = 0.0;
		double SideMarginCM = 0.0;
		double PartialLayoutTurnRadians = Pi;
		double ForwardLayoutTurnRadians = Pi;
		double ProjectedLayoutTurnRadians = Pi;
		std::int32_t RobustSurvivorCount = 0;
		std::uint64_t TieBreak = 0;
	};

	std::vector<LaunchInput> MakeRobustInputs(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract)
	{
		std::vector<LaunchInput> Inputs;
		Inputs.reserve(7);
		Inputs.push_back(Layout.NominalInput);
		for (const std::array<double, 3>& Delta :
			std::array<std::array<double, 3>, 6>{
				std::array<double, 3>{
					-Contract.RobustYawStepDegrees, 0.0, 0.0},
				std::array<double, 3>{
					Contract.RobustYawStepDegrees, 0.0, 0.0},
				std::array<double, 3>{
					0.0, -Contract.RobustPitchStepDegrees, 0.0},
				std::array<double, 3>{
					0.0, Contract.RobustPitchStepDegrees, 0.0},
				std::array<double, 3>{
					0.0, 0.0, -Contract.RobustPowerStep},
				std::array<double, 3>{
					0.0, 0.0, Contract.RobustPowerStep}})
		{
			LaunchInput Input = Layout.NominalInput;
			Input.YawDegrees += Delta[0];
			Input.PitchDegrees += Delta[1];
			Input.Power += Delta[2];
			if (Layout.Launch.Contains(Input))
			{
				Inputs.push_back(Input);
			}
		}
		return Inputs;
	}

	bool ResultPassesAssistPrefix(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const TrajectoryResult& Result,
		const std::int32_t AssistIndex,
		double* OutMinimumQuality = nullptr,
		double* OutMinimumEnergy = nullptr,
		double* OutMinimumSide = nullptr)
	{
		if (Result.CompletedAssistCount < AssistIndex)
		{
			return false;
		}
		double MinimumQuality = std::numeric_limits<double>::max();
		double MinimumEnergy = std::numeric_limits<double>::max();
		double MinimumSide = std::numeric_limits<double>::max();
		for (std::int32_t CheckIndex = 1;
			CheckIndex <= AssistIndex;
			++CheckIndex)
		{
			const TrajectoryEvent* Enter = Result.FindAssistEvent(
				TrajectoryEventType::AssistEnter, CheckIndex);
			const TrajectoryEvent* Exit = Result.FindAssistEvent(
				TrajectoryEventType::AssistExit, CheckIndex);
			if (Enter == nullptr || Exit == nullptr)
			{
				return false;
			}
			const GravityBodySpec& Body =
				Layout.Scenario.GetAssist(CheckIndex);
			const double Side = ComputeAllowedSideMargin(Body, *Exit);
			const double Duration =
				Exit->TimeSeconds - Enter->TimeSeconds;
			const double Deflection = AngleBetween(
				Enter->VelocityCMPerSec,
				Exit->VelocityCMPerSec);
			const double Clearance =
				Exit->ClosestDistanceCM - Body.CollisionRadiusCM;
			if (Exit->CorridorQuality < Contract.MinimumCorridorQuality
				|| Exit->AppliedEnergyChangeCM2PerSec2
					< Contract.MinimumEnergyGainCM2PerSec2
				|| Side <= 0.0
				|| Duration < Contract.MinimumInfluenceDurationSeconds
				|| Duration > Contract.MaximumInfluenceDurationSeconds
				|| Deflection < Contract.MinimumDeflectionRadians
				|| Clearance < Contract.MinimumBodyClearanceCM)
			{
				return false;
			}
			MinimumQuality = std::min(
				MinimumQuality, Exit->CorridorQuality);
			MinimumEnergy = std::min(
				MinimumEnergy,
				Exit->AppliedEnergyChangeCM2PerSec2);
			MinimumSide = std::min(MinimumSide, Side);
		}
		if (OutMinimumQuality != nullptr)
		{
			*OutMinimumQuality = MinimumQuality;
		}
		if (OutMinimumEnergy != nullptr)
		{
			*OutMinimumEnergy = MinimumEnergy;
		}
		if (OutMinimumSide != nullptr)
		{
			*OutMinimumSide = MinimumSide;
		}
		return true;
	}

	void EvaluateStageRobustness(
		const CandidateSearchContract& Contract,
		const std::int32_t AssistIndex,
		StageCandidate& Candidate,
		std::int32_t& InOutSolveCount)
	{
		Candidate.RobustSurvivorCount = 0;
		for (const LaunchInput& Input :
			MakeRobustInputs(Candidate.Layout, Contract))
		{
			TrajectoryRequest Request;
			TrajectoryResult Result;
			if (BuildAndSolve(
					Candidate.Layout,
					Input,
					0x7u,
					Request,
					Result,
					InOutSolveCount)
				&& ResultPassesAssistPrefix(
					Candidate.Layout,
					Contract,
					Result,
					AssistIndex))
			{
				++Candidate.RobustSurvivorCount;
			}
		}
	}

	bool StageRanksBefore(
		const StageCandidate& Left,
		const StageCandidate& Right)
	{
		if (Left.RobustSurvivorCount != Right.RobustSurvivorCount)
		{
			return Left.RobustSurvivorCount > Right.RobustSurvivorCount;
		}
		if (Left.ProjectedLayoutTurnRadians
			!= Right.ProjectedLayoutTurnRadians)
		{
			return Left.ProjectedLayoutTurnRadians
				> Right.ProjectedLayoutTurnRadians;
		}
		if (Left.PartialLayoutTurnRadians
			!= Right.PartialLayoutTurnRadians)
		{
			return Left.PartialLayoutTurnRadians
				> Right.PartialLayoutTurnRadians;
		}
		if (Left.DeflectionRadians != Right.DeflectionRadians)
		{
			return Left.DeflectionRadians > Right.DeflectionRadians;
		}
		if (Left.EnergyGain != Right.EnergyGain)
		{
			return Left.EnergyGain > Right.EnergyGain;
		}
		if (Left.CorridorQuality != Right.CorridorQuality)
		{
			return Left.CorridorQuality > Right.CorridorQuality;
		}
		if (Left.ClearanceCM != Right.ClearanceCM)
		{
			return Left.ClearanceCM > Right.ClearanceCM;
		}
		if (Left.SideMarginCM != Right.SideMarginCM)
		{
			return Left.SideMarginCM > Right.SideMarginCM;
		}
		if (Left.ExitTimeSeconds != Right.ExitTimeSeconds)
		{
			return Left.ExitTimeSeconds < Right.ExitTimeSeconds;
		}
		return Left.TieBreak < Right.TieBreak;
	}

	bool PlaceAssist(
		const CandidateSearchContract& Contract,
		const WorkParameters& Parameters,
		const std::uint64_t GlobalWorkIndex,
		const std::int32_t AssistIndex,
		const CandidateLayout& InputLayout,
		const TrajectoryResult& InputArc,
		CandidateLayout& OutLayout,
		TrajectoryResult& OutResult,
		std::int32_t& InOutSolveCount,
		std::string& OutDiagnostic)
	{
		const TrajectoryEvent* PreviousExit = AssistIndex > 1
			? InputArc.FindAssistEvent(
				TrajectoryEventType::AssistExit,
				AssistIndex - 1)
			: nullptr;
		const double CenterTime = PreviousExit != nullptr
			? PreviousExit->TimeSeconds
				+ Parameters.InterEncounterCoastSeconds[
					static_cast<std::size_t>(AssistIndex - 2)]
			: Parameters.FirstEncounterSeconds;
		std::vector<StageCandidate> Candidates;
		std::uint64_t CandidateOrdinal = 0;
		std::int32_t GeometryRejected = 0;
		std::int32_t RawSolveRejected = 0;
		std::int32_t RawEncounterRejected = 0;
		std::int32_t ReplaySolveRejected = 0;
		std::int32_t PrefixRejected = 0;
		std::int32_t DeflectionRejected = 0;
		double MaximumRejectedDeflection = 0.0;

		for (std::int32_t TimeIndex = 0;
			TimeIndex < Contract.LocalTimeSampleCount;
			++TimeIndex)
		{
			const double TimeAlpha = Contract.LocalTimeSampleCount == 1
				? 0.0
				: static_cast<double>(TimeIndex)
					/ static_cast<double>(
						Contract.LocalTimeSampleCount - 1)
					- 0.5;
			const double SampleTime = CenterTime + TimeAlpha * 2.5;
			const TrajectoryPoint* Point =
				FindPointAtOrAfter(InputArc, SampleTime);
			if (Point == nullptr)
			{
				continue;
			}
			const GravityBodySpec& SeedBody =
				InputLayout.Scenario.GetAssist(AssistIndex);
			Vec3d T;
			Vec3d R;
			if (!BuildImpactBasis(SeedBody, *Point, T, R))
			{
				continue;
			}

			for (std::int32_t ImpactIndex = 0;
				ImpactIndex < Contract.LocalImpactSampleCount;
				++ImpactIndex)
			{
				const double ImpactAlpha =
					Contract.LocalImpactSampleCount == 1
					? 0.0
					: static_cast<double>(ImpactIndex)
						/ static_cast<double>(
							Contract.LocalImpactSampleCount - 1)
						- 0.5;
				const double ImpactFraction = std::clamp(
					Parameters.ImpactFraction[
						static_cast<std::size_t>(AssistIndex - 1)]
						+ ImpactAlpha * 0.18,
					0.12,
					0.72);
				for (std::int32_t RadialIndex = 0;
					RadialIndex < Contract.LocalRadialSampleCount;
					++RadialIndex)
				{
					const double RadialAlpha =
						Contract.LocalRadialSampleCount == 1
						? 0.0
						: static_cast<double>(RadialIndex)
							/ static_cast<double>(
								Contract.LocalRadialSampleCount - 1)
							- 0.5;
					const double RadialFraction = std::clamp(
						Parameters.RadialFraction[
							static_cast<std::size_t>(AssistIndex - 1)]
							+ RadialAlpha * 0.24,
						-0.55,
						0.55);
					for (const double Sign : {-1.0, 1.0})
					{
						for (std::int32_t MomentumIndex = 0;
							MomentumIndex
								< Contract.LocalMomentumDirectionSampleCount;
							++MomentumIndex)
						{
							++CandidateOrdinal;
							CandidateLayout Layout = InputLayout;
							GravityBodySpec& Body =
								Layout.Scenario.Bodies[
									static_cast<std::size_t>(AssistIndex)];
							const Vec3d OffsetCM =
								T * (Sign * ImpactFraction
									* Body.InfluenceRadiusCM)
								+ R * (RadialFraction
									* Body.InfluenceRadiusCM);
							Body.CenterCM = Point->PositionCM + OffsetCM;
							const double MomentumAlpha =
								Contract.LocalMomentumDirectionSampleCount == 1
								? 0.0
								: static_cast<double>(MomentumIndex)
									/ static_cast<double>(
										Contract.LocalMomentumDirectionSampleCount
											- 1)
									- 0.5;
							const double MomentumAngle = MomentumAlpha * 0.9;
							const Vec3d OffsetDirection =
								OffsetCM.GetSafeNormal();
							Vec3d MomentumFanAxis = Vec3d::CrossProduct(
								Point->VelocityCMPerSec.GetSafeNormal(),
								OffsetDirection).GetSafeNormal();
							if (MomentumFanAxis.IsNearlyZero())
							{
								MomentumFanAxis = R;
							}
							const Vec3d MomentumDirection =
								(OffsetDirection * std::cos(MomentumAngle)
									+ MomentumFanAxis
										* std::sin(MomentumAngle))
									.GetSafeNormal();
							Body.VirtualOrbitalVelocityCMPerSec =
								MomentumDirection
								* Parameters.VirtualSpeedCMPerSec[
									static_cast<std::size_t>(
										AssistIndex - 1)];
							Body.AllowedPassSideValue =
								AllowedPassSide::Any;
							Body.BPlaneTargetTCM = 0.0;
							Body.BPlaneTargetRCM = 0.0;
							Body.BPlaneSigmaTCM =
								Body.InfluenceRadiusCM * 0.46;
							Body.BPlaneSigmaRCM =
								Body.InfluenceRadiusCM * 0.46;
							if (!GeometryIsLegal(
								Layout, Contract, AssistIndex))
							{
								++GeometryRejected;
								continue;
							}
							RefreshIdentity(Layout, Contract);
							TrajectoryRequest Request;
							TrajectoryResult Result;
							if (!BuildAndSolve(
									Layout,
									Layout.NominalInput,
									0x7u,
									Request,
									Result,
									InOutSolveCount))
							{
								++RawSolveRejected;
								continue;
							}
							const TrajectoryEvent* Exit =
								Result.FindAssistEvent(
									TrajectoryEventType::AssistExit,
									AssistIndex);
							const TrajectoryEvent* Enter =
								Result.FindAssistEvent(
									TrajectoryEventType::AssistEnter,
									AssistIndex);
							if (Exit == nullptr
								|| Enter == nullptr
								|| Result.CompletedAssistCount < AssistIndex
								|| Exit->AppliedEnergyChangeCM2PerSec2 <= 0.0)
							{
								++RawEncounterRejected;
								continue;
							}

							Body.BPlaneTargetTCM = Exit->BPlaneTCM;
							Body.BPlaneTargetRCM = Exit->BPlaneRCM;
							Body.BPlaneSigmaTCM =
								Body.InfluenceRadiusCM * 0.38;
							Body.BPlaneSigmaRCM =
								Body.InfluenceRadiusCM * 0.38;
							Body.AllowedPassSideValue =
								InferAllowedSide(*Exit);
							RefreshIdentity(Layout, Contract);
							if (!BuildAndSolve(
									Layout,
									Layout.NominalInput,
									0x7u,
									Request,
									Result,
									InOutSolveCount))
							{
								++ReplaySolveRejected;
								continue;
							}
							Exit = Result.FindAssistEvent(
								TrajectoryEventType::AssistExit,
								AssistIndex);
							Enter = Result.FindAssistEvent(
								TrajectoryEventType::AssistEnter,
								AssistIndex);
							if (Exit == nullptr || Enter == nullptr
								|| !ResultPassesAssistPrefix(
									Layout,
									Contract,
									Result,
									AssistIndex))
							{
								++PrefixRejected;
								continue;
							}
							const double Deflection = AngleBetween(
								Enter->VelocityCMPerSec,
								Exit->VelocityCMPerSec);
							if (Deflection
								< Contract.MinimumDeflectionRadians)
							{
								MaximumRejectedDeflection = std::max(
									MaximumRejectedDeflection, Deflection);
								++DeflectionRejected;
								continue;
							}

							StageCandidate Candidate;
							Candidate.Layout = std::move(Layout);
							Candidate.Result = std::move(Result);
							Candidate.ExitTimeSeconds = Exit->TimeSeconds;
							Candidate.CorridorQuality = Exit->CorridorQuality;
							Candidate.EnergyGain =
								Exit->AppliedEnergyChangeCM2PerSec2;
							Candidate.DeflectionRadians = Deflection;
							Candidate.ClearanceCM =
								Exit->ClosestDistanceCM
								- Body.CollisionRadiusCM;
							Candidate.SideMarginCM =
								ComputeAllowedSideMargin(Body, *Exit);
							Candidate.PartialLayoutTurnRadians =
								ComputePartialLayoutTurn(
									Candidate.Layout, AssistIndex);
							const Vec3d PreviousCenter =
								AssistIndex == 1
								? Candidate.Layout.Launch
									.PouchLocalPositionCM
								: Candidate.Layout.Scenario.GetAssist(
									AssistIndex - 1).CenterCM;
							Candidate.ForwardLayoutTurnRadians =
								AngleBetween(
									Candidate.Layout.Scenario.GetAssist(
										AssistIndex).CenterCM
										- PreviousCenter,
									Exit->VelocityCMPerSec);
							Candidate.ProjectedLayoutTurnRadians =
								std::min(
									Candidate.PartialLayoutTurnRadians,
									Candidate.ForwardLayoutTurnRadians);
							Candidate.TieBreak =
								(GlobalWorkIndex ^ CandidateOrdinal)
								* 0x9e3779b97f4a7c15ull
								+ static_cast<std::uint64_t>(AssistIndex);
							Candidates.push_back(std::move(Candidate));
						}
					}
				}
			}
		}
		if (Candidates.empty())
		{
			OutDiagnostic =
				"StageEmpty:G=" + std::to_string(GeometryRejected)
				+ ":RS=" + std::to_string(RawSolveRejected)
				+ ":RE=" + std::to_string(RawEncounterRejected)
				+ ":PS=" + std::to_string(ReplaySolveRejected)
				+ ":P=" + std::to_string(PrefixRejected)
				+ ":D=" + std::to_string(DeflectionRejected)
				+ ":MaxD=" + std::to_string(
					MaximumRejectedDeflection);
			return false;
		}
		std::sort(
			Candidates.begin(), Candidates.end(), StageRanksBefore);
		if (Candidates.size()
			> static_cast<std::size_t>(Contract.RobustPreselectionWidth))
		{
			Candidates.resize(
				static_cast<std::size_t>(
					Contract.RobustPreselectionWidth));
		}
		for (StageCandidate& Candidate : Candidates)
		{
			EvaluateStageRobustness(
				Contract,
				AssistIndex,
				Candidate,
				InOutSolveCount);
		}
		Candidates.erase(
			std::remove_if(
				Candidates.begin(),
				Candidates.end(),
				[&Contract](const StageCandidate& Candidate)
				{
					return Candidate.RobustSurvivorCount
						< Contract.MinimumRobustSurvivorCount;
				}),
			Candidates.end());
		if (Candidates.empty())
		{
			OutDiagnostic = "StageRobustnessRejected";
			return false;
		}
		std::sort(
			Candidates.begin(), Candidates.end(), StageRanksBefore);
		OutLayout = std::move(Candidates.front().Layout);
		OutResult = std::move(Candidates.front().Result);
		OutDiagnostic.clear();
		return true;
	}

	double MinimumDistanceToTarget(
		const CandidateLayout& Layout,
		const TrajectoryResult& Result)
	{
		double MinimumDistance = std::numeric_limits<double>::max();
		for (const TrajectoryPoint& Point : Result.Points)
		{
			MinimumDistance = std::min(
				MinimumDistance,
				(Point.PositionCM - Layout.Scenario.Target.CenterCM).Length());
		}
		return MinimumDistance;
	}

	std::array<double, GravityScenario::AssistCount>
	ComputeLayoutTurns(const CandidateLayout& Layout)
	{
		std::array<Vec3d, 5> Points{
			Layout.Launch.PouchLocalPositionCM,
			Layout.Scenario.GetAssist(1).CenterCM,
			Layout.Scenario.GetAssist(2).CenterCM,
			Layout.Scenario.GetAssist(3).CenterCM,
			Layout.Scenario.Target.CenterCM};
		std::array<double, GravityScenario::AssistCount> Turns{};
		for (std::size_t Index = 1; Index + 1 < Points.size(); ++Index)
		{
			Turns[Index - 1] = AngleBetween(
				Points[Index] - Points[Index - 1],
				Points[Index + 1] - Points[Index]);
		}
		return Turns;
	}

	double ComputeMinimumLayoutTurn(const CandidateLayout& Layout)
	{
		const std::array<double, GravityScenario::AssistCount> Turns =
			ComputeLayoutTurns(Layout);
		return *std::min_element(Turns.begin(), Turns.end());
	}

	bool BuildTarget(
		const CandidateSearchContract& Contract,
		const WorkParameters& Parameters,
		CandidateLayout& InOutLayout,
		TrajectoryResult& InOutArc,
		std::int32_t& InOutSolveCount,
		std::string& OutDiagnostic)
	{
		struct RobustArc
		{
			LaunchInput Input;
			TrajectoryResult Result;
		};
		struct TargetPoint
		{
			Vec3d PositionCM;
			Vec3d VelocityCMPerSec;
			bool IsNominal = false;
		};

		CandidateLayout ArcLayout = InOutLayout;
		TargetSpec& NeutralTarget = ArcLayout.Scenario.Target;
		NeutralTarget.CenterCM = Vec3d(0.0, 0.0, 270000.0);
		NeutralTarget.HitRadiusCM = 100.0;
		NeutralTarget.GeometricContactRadiusCM = 0.0;
		NeutralTarget.UseSeparateGeometricContactCenter = true;
		NeutralTarget.GeometricContactCenterCM = NeutralTarget.CenterCM;
		NeutralTarget.RequiredQualifiedAssistCount = 3;
		NeutralTarget.MinimumQualifyingCorridorQuality =
			Contract.MinimumCorridorQuality;
		NeutralTarget.MinimumQualifyingEnergyGainCM2PerSec2 =
			Contract.MinimumEnergyGainCM2PerSec2;
		NeutralTarget.RequireAllowedPassSide = true;
		NeutralTarget.PresentationForward = Vec3d(-1.0, 0.0, 0.0);
		RefreshIdentity(ArcLayout, Contract);

		std::vector<RobustArc> RobustArcs;
		for (const LaunchInput& Input :
			MakeRobustInputs(ArcLayout, Contract))
		{
			TrajectoryRequest Request;
			TrajectoryResult Result;
			if (BuildAndSolve(
					ArcLayout,
					Input,
					0x7u,
					Request,
					Result,
					InOutSolveCount)
				&& ResultPassesAssistPrefix(
					ArcLayout, Contract, Result, 3))
			{
				RobustArc Arc;
				Arc.Input = Input;
				Arc.Result = std::move(Result);
				RobustArcs.push_back(std::move(Arc));
			}
		}
		if (RobustArcs.size()
			< static_cast<std::size_t>(
				Contract.MinimumRobustSurvivorCount))
		{
			OutDiagnostic = "TargetRobustPrefixInsufficient:"
				+ std::to_string(RobustArcs.size());
			return false;
		}

		bool FoundTarget = false;
		double BestTurn = -1.0;
		std::int32_t BestCoveredCount = -1;
		double BestRadiusCM = std::numeric_limits<double>::max();
		std::uint64_t CandidateOrdinal = 0;
		std::uint64_t BestOrdinal = 0;
		CandidateLayout BestLayout;
		TrajectoryResult BestResult;
		std::int32_t PointSetRejected = 0;
		std::int32_t RadiusRejected = 0;
		std::int32_t GeometryRejected = 0;
		std::int32_t ReplayRejected = 0;

		const std::int32_t TimeSampleCount =
			Contract.TargetTimeSampleCount;
		for (std::int32_t TimeIndex = 0;
			TimeIndex < TimeSampleCount;
			++TimeIndex)
		{
			const double TimeAlpha = TimeSampleCount == 1
				? 0.5
				: static_cast<double>(TimeIndex)
					/ static_cast<double>(TimeSampleCount - 1);
			const double CoastSeconds = M11Core::Lerp(
				Contract.TargetCoastMinimumSeconds,
				Contract.TargetCoastMaximumSeconds,
				TimeAlpha);
			std::vector<TargetPoint> Points;
			std::size_t NominalPointIndex =
				std::numeric_limits<std::size_t>::max();
			for (const RobustArc& Arc : RobustArcs)
			{
				const TrajectoryEvent* Exit3 =
					Arc.Result.FindAssistEvent(
						TrajectoryEventType::AssistExit, 3);
				if (Exit3 == nullptr)
				{
					continue;
				}
				const TrajectoryPoint* Point = FindPointAtOrAfter(
					Arc.Result,
					Exit3->TimeSeconds + CoastSeconds);
				if (Point == nullptr)
				{
					continue;
				}
				TargetPoint TargetSample;
				TargetSample.PositionCM = Point->PositionCM;
				TargetSample.VelocityCMPerSec =
					Point->VelocityCMPerSec;
				TargetSample.IsNominal =
					Arc.Input.YawDegrees
							== InOutLayout.NominalInput.YawDegrees
						&& Arc.Input.PitchDegrees
							== InOutLayout.NominalInput.PitchDegrees
						&& Arc.Input.Power
							== InOutLayout.NominalInput.Power;
				if (TargetSample.IsNominal)
				{
					NominalPointIndex = Points.size();
				}
				Points.push_back(TargetSample);
			}
			if (Points.size()
					< static_cast<std::size_t>(
						Contract.MinimumRobustSurvivorCount)
				|| NominalPointIndex
					== std::numeric_limits<std::size_t>::max())
			{
				++PointSetRejected;
				continue;
			}

			std::vector<Vec3d> Centers;
			Centers.reserve(2 + Points.size() * 2);
			const Vec3d NominalPosition =
				Points[NominalPointIndex].PositionCM;
			Centers.push_back(NominalPosition);
			Vec3d Mean;
			for (const TargetPoint& Point : Points)
			{
				Mean += Point.PositionCM;
			}
			Mean /= static_cast<double>(Points.size());
			Centers.push_back(Mean);
			for (const TargetPoint& Point : Points)
			{
				Centers.push_back(Point.PositionCM);
				if (!Point.IsNominal)
				{
					Centers.push_back(
						(NominalPosition + Point.PositionCM) * 0.5);
				}
			}
			for (std::size_t A = 0; A < Points.size(); ++A)
			{
				if (A == NominalPointIndex)
				{
					continue;
				}
				for (std::size_t B = A + 1; B < Points.size(); ++B)
				{
					if (B == NominalPointIndex)
					{
						continue;
					}
					for (std::size_t C = B + 1;
						C < Points.size();
						++C)
					{
						if (C == NominalPointIndex)
						{
							continue;
						}
						Centers.push_back(
							(NominalPosition
								+ Points[A].PositionCM
								+ Points[B].PositionCM
								+ Points[C].PositionCM)
							* 0.25);
					}
				}
			}

			for (const Vec3d& Center : Centers)
			{
				++CandidateOrdinal;
				std::vector<double> Distances;
				Distances.reserve(Points.size());
				for (const TargetPoint& Point : Points)
				{
					Distances.push_back(
						(Point.PositionCM - Center).Length());
				}
				std::sort(Distances.begin(), Distances.end());
				const std::size_t RequiredIndex =
					static_cast<std::size_t>(
						Contract.MinimumRobustSurvivorCount - 1);
				const double RequiredRadiusCM = std::max(
					{
						Contract.MinimumTargetHitRadiusCM,
						Parameters.TargetRadiusCM,
						Distances[RequiredIndex]
							+ Contract.TargetCoverageMarginCM,
						(NominalPosition - Center).Length()
							+ Contract.TargetCoverageMarginCM});
				if (RequiredRadiusCM
					> Contract.MaximumTargetHitRadiusCM)
				{
					++RadiusRejected;
					continue;
				}
				const double RadiusCM = std::clamp(
					RequiredRadiusCM,
					Contract.MinimumTargetHitRadiusCM,
					Contract.MaximumTargetHitRadiusCM);
				std::int32_t CoveredCount = 0;
				for (const TargetPoint& Point : Points)
				{
					if ((Point.PositionCM - Center).Length()
						<= RadiusCM)
					{
						++CoveredCount;
					}
				}

				bool TargetGeometryIsLegal = true;
				for (std::int32_t AssistIndex = 1;
					AssistIndex <= GravityScenario::AssistCount;
					++AssistIndex)
				{
					const GravityBodySpec& Body =
						InOutLayout.Scenario.GetAssist(AssistIndex);
					if ((Center - Body.CenterCM).Length()
						<= Body.CollisionRadiusCM + RadiusCM
							+ Contract.MinimumBodyClearanceCM)
					{
						TargetGeometryIsLegal = false;
						break;
					}
				}
				if (!TargetGeometryIsLegal)
				{
					++GeometryRejected;
					continue;
				}

				CandidateLayout Candidate = InOutLayout;
				TargetSpec& Target = Candidate.Scenario.Target;
				Target.CenterCM = Center;
				Target.HitRadiusCM = RadiusCM;
				Target.GeometricContactRadiusCM = 800.0;
				Target.UseSeparateGeometricContactCenter = false;
				Target.GeometricContactCenterCM = Center;
				Target.RequiredQualifiedAssistCount = 3;
				Target.MinimumQualifyingCorridorQuality =
					Contract.MinimumCorridorQuality;
				Target.MinimumQualifyingEnergyGainCM2PerSec2 =
					Contract.MinimumEnergyGainCM2PerSec2;
				Target.RequireAllowedPassSide = true;
				Target.PresentationForward =
					-Points[NominalPointIndex]
						.VelocityCMPerSec.GetSafeNormal(
							M11Core::SmallNumber,
							Vec3d(-1.0, 0.0, 0.0));
				RefreshIdentity(Candidate, Contract);

				TrajectoryRequest Request;
				TrajectoryResult Result;
				if (!BuildAndSolve(
						Candidate,
						Candidate.NominalInput,
						0x7u,
						Request,
						Result,
						InOutSolveCount)
					|| !Result.DidHitTarget()
					|| Result.CompletedAssistCount != 3)
				{
					++ReplayRejected;
					continue;
				}
				const double Turn =
					ComputeMinimumLayoutTurn(Candidate);
				const bool RanksBefore =
					!FoundTarget
					|| Turn > BestTurn
					|| (Turn == BestTurn
						&& CoveredCount > BestCoveredCount)
					|| (Turn == BestTurn
						&& CoveredCount == BestCoveredCount
						&& RadiusCM < BestRadiusCM)
					|| (Turn == BestTurn
						&& CoveredCount == BestCoveredCount
						&& RadiusCM == BestRadiusCM
						&& CandidateOrdinal < BestOrdinal);
				if (RanksBefore)
				{
					FoundTarget = true;
					BestTurn = Turn;
					BestCoveredCount = CoveredCount;
					BestRadiusCM = RadiusCM;
					BestOrdinal = CandidateOrdinal;
					BestLayout = std::move(Candidate);
					BestResult = std::move(Result);
				}
			}
		}

		if (!FoundTarget)
		{
			OutDiagnostic =
				"TargetCandidateEmpty:PointSets="
				+ std::to_string(PointSetRejected)
				+ ":Radius=" + std::to_string(RadiusRejected)
				+ ":Geometry=" + std::to_string(GeometryRejected)
				+ ":Replay=" + std::to_string(ReplayRejected);
			return false;
		}
		InOutLayout = std::move(BestLayout);
		InOutArc = std::move(BestResult);
		OutDiagnostic.clear();
		return true;
	}

	bool PopulateMetrics(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const TrajectoryResult& Result,
		CandidateMetrics& OutMetrics,
		std::string* OutFailure)
	{
		TrajectoryPacingDiagnostics Pacing;
		std::string Failure;
		if (!Result.BuildPacingDiagnostics(Pacing, &Failure))
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = "PacingDiagnosticsFailed:" + Failure;
			}
			return false;
		}
		OutMetrics.TotalFlightTimeSeconds = Pacing.TotalFlightTimeSeconds;
		OutMetrics.FinalCoastSeconds = Pacing.FinalCoastSeconds;
		OutMetrics.MaximumCoastSeconds = Pacing.MaximumCoastSeconds;
		OutMetrics.TotalInfluenceDurationSeconds =
			Pacing.TotalInfluenceDurationSeconds;
		OutMetrics.MinimumLayoutTurnRadians =
			ComputeMinimumLayoutTurn(Layout);
		OutMetrics.LayoutTurnsRadians = ComputeLayoutTurns(Layout);
		OutMetrics.MinimumTargetDistanceCM =
			MinimumDistanceToTarget(Layout, Result);
		for (std::int32_t AssistIndex = 1;
			AssistIndex <= GravityScenario::AssistCount;
			++AssistIndex)
		{
			const std::size_t Index =
				static_cast<std::size_t>(AssistIndex - 1);
			const AssistPhaseDiagnostics& Phase = Pacing.Assists[Index];
			const TrajectoryEvent* Exit = Result.FindAssistEvent(
				TrajectoryEventType::AssistExit, AssistIndex);
			if (!Phase.Complete || Exit == nullptr)
			{
				if (OutFailure != nullptr)
				{
					*OutFailure = "Assist"
						+ std::to_string(AssistIndex)
						+ "PacingIncomplete";
				}
				return false;
			}
			AssistMetrics& Metrics = OutMetrics.Assists[Index];
			Metrics.EnterTimeSeconds = Phase.EnterTimeSeconds;
			Metrics.ClosestTimeSeconds = Phase.ClosestTimeSeconds;
			Metrics.ExitTimeSeconds = Phase.ExitTimeSeconds;
			Metrics.CoastBeforeEnterSeconds =
				Phase.CoastBeforeEnterSeconds;
			Metrics.InfluenceDurationSeconds =
				Phase.InfluenceDurationSeconds;
			Metrics.ActualDeflectionRadians =
				Phase.ActualDeflectionRadians;
			Metrics.NaturalDeflectionRadians =
				Phase.NaturalDeflectionRadians;
			Metrics.EntrySpeedCMPerSec = Phase.EntrySpeedCMPerSec;
			Metrics.ExitSpeedCMPerSec = Phase.ExitSpeedCMPerSec;
			Metrics.CorridorQuality = Exit->CorridorQuality;
			Metrics.AppliedEnergyGainCM2PerSec2 =
				Exit->AppliedEnergyChangeCM2PerSec2;
			Metrics.CollisionClearanceCM =
				Exit->ClosestDistanceCM
				- Layout.Scenario.GetAssist(AssistIndex)
					.CollisionRadiusCM;
			const auto RejectAssistMetric =
				[OutFailure, AssistIndex](const char* Suffix)
				{
					if (OutFailure != nullptr)
					{
						*OutFailure = "Assist"
							+ std::to_string(AssistIndex) + Suffix;
					}
					return false;
				};
			if (Metrics.InfluenceDurationSeconds
				< Contract.MinimumInfluenceDurationSeconds)
			{
				return RejectAssistMetric("DurationBelowMinimum");
			}
			if (Metrics.InfluenceDurationSeconds
				> Contract.MaximumInfluenceDurationSeconds)
			{
				return RejectAssistMetric("DurationAboveMaximum");
			}
			if (Metrics.ActualDeflectionRadians
				< Contract.MinimumDeflectionRadians)
			{
				return RejectAssistMetric("DeflectionBelowMinimum");
			}
			if (Metrics.AppliedEnergyGainCM2PerSec2
				< Contract.MinimumEnergyGainCM2PerSec2)
			{
				return RejectAssistMetric("EnergyBelowMinimum");
			}
			if (Metrics.CorridorQuality
				< Contract.MinimumCorridorQuality)
			{
				return RejectAssistMetric("CorridorQualityBelowMinimum");
			}
			if (Metrics.CollisionClearanceCM
				< Contract.MinimumBodyClearanceCM)
			{
				return RejectAssistMetric("ClearanceBelowMinimum");
			}
		}
		if (OutMetrics.TotalFlightTimeSeconds
			> Contract.MaximumTotalFlightTimeSeconds)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = "TotalFlightTimeAboveMaximum";
			}
			return false;
		}
		if (OutMetrics.MaximumCoastSeconds
			> Contract.MaximumCoastSeconds)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = "CoastAboveMaximum";
			}
			return false;
		}
		if (OutMetrics.MinimumLayoutTurnRadians
			< Contract.MinimumLayoutTurnRadians)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = "LayoutTurnBelowMinimum";
			}
			return false;
		}
		if (OutFailure != nullptr)
		{
			OutFailure->clear();
		}
		return true;
	}

	std::uint64_t ComputeRequestHash(
		const CandidateRecord& Candidate,
		const TrajectoryRequest& Request)
	{
		std::uint64_t Hash =
			Candidate.CandidateSourceHash ^ 0xcbf29ce484222325ull;
		const auto Add64 = [&Hash](const std::uint64_t Value)
		{
			for (std::int32_t ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				Hash ^= static_cast<std::uint8_t>(
					Value >> (ByteIndex * 8));
				Hash *= 1099511628211ull;
			}
		};
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialPositionCM.X));
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialPositionCM.Y));
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialPositionCM.Z));
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialVelocityCMPerSec.X));
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialVelocityCMPerSec.Y));
		Add64(std::bit_cast<std::uint64_t>(
			Request.InitialVelocityCMPerSec.Z));
		Add64(Request.Config.EnabledAssistMask);
		return Hash;
	}

	double CandidateDiversityDistance(
		const CandidateRecord& Left,
		const CandidateRecord& Right)
	{
		double SumSquared = 0.0;
		for (std::int32_t AssistIndex = 1;
			AssistIndex <= GravityScenario::AssistCount;
			++AssistIndex)
		{
			SumSquared += (
				Left.Layout.Scenario.GetAssist(AssistIndex).CenterCM
				- Right.Layout.Scenario.GetAssist(AssistIndex).CenterCM)
				.SquaredLength();
		}
		SumSquared += (
			Left.Layout.Scenario.Target.CenterCM
			- Right.Layout.Scenario.Target.CenterCM).SquaredLength();
		return std::sqrt(SumSquared / 4.0);
	}
}

bool ABTS::M11Search::CandidateSearch::EvaluateWorkItem(
	const CandidateSearchContract& Contract,
	const std::uint64_t GlobalWorkIndex,
	CandidateRecord& OutCandidate,
	std::string* OutFailure)
{
	using namespace SearchPrivate;
	OutCandidate = CandidateRecord();
	OutCandidate.GlobalWorkIndex = GlobalWorkIndex;
	std::string Failure;
	if (!Contract.IsValid(&Failure))
	{
		return Reject(
			OutFailure,
			&OutCandidate,
			EvaluationStatus::InvalidContract,
			Failure.c_str());
	}

	const WorkParameters Parameters =
		MakeWorkParameters(Contract, GlobalWorkIndex);
	CandidateLayout Working = MakeSeedLayout(Contract, Parameters);
	RefreshIdentity(Working, Contract);
	TrajectoryRequest Request;
	TrajectoryResult Arc;
	if (!BuildAndSolve(
			Working,
			Working.NominalInput,
			0x7u,
			Request,
			Arc,
			OutCandidate.SolverInvocationCount,
			&Failure))
	{
		OutCandidate.Layout = Working;
		OutCandidate.CandidateSourceHash =
			ComputeCandidateSourceHash(Working, Contract);
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::InitialArcFailed,
			"InitialArcFailed");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	for (std::int32_t AssistIndex = 1;
		AssistIndex <= M11Core::GravityScenario::AssistCount;
		++AssistIndex)
	{
		CandidateLayout NextLayout;
		TrajectoryResult NextArc;
		std::string StageDiagnostic;
		if (!PlaceAssist(
			Contract,
			Parameters,
			GlobalWorkIndex,
			AssistIndex,
			Working,
			Arc,
			NextLayout,
			NextArc,
			OutCandidate.SolverInvocationCount,
			StageDiagnostic))
		{
			OutCandidate.Layout = Working;
			OutCandidate.CandidateSourceHash =
				ComputeCandidateSourceHash(Working, Contract);
			const EvaluationStatus Status = AssistIndex == 1
				? EvaluationStatus::Assist1ConstructionFailed
				: AssistIndex == 2
					? EvaluationStatus::Assist2ConstructionFailed
					: EvaluationStatus::Assist3ConstructionFailed;
			Reject(
				nullptr,
				&OutCandidate,
				Status,
				StageDiagnostic.empty()
					? ToString(Status)
					: StageDiagnostic.c_str());
			OutCandidate.ScoreHash =
				ComputeCandidateScoreHash(OutCandidate);
			return true;
		}
		Working = std::move(NextLayout);
		Arc = std::move(NextArc);
	}

	std::string TargetDiagnostic;
	if (!BuildTarget(
		Contract,
		Parameters,
		Working,
		Arc,
		OutCandidate.SolverInvocationCount,
		TargetDiagnostic))
	{
		OutCandidate.Layout = Working;
		OutCandidate.CandidateSourceHash =
			ComputeCandidateSourceHash(Working, Contract);
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::TargetConstructionFailed,
			TargetDiagnostic.empty()
				? "TargetConstructionFailed"
				: TargetDiagnostic.c_str());
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	OutCandidate.Layout = Working;
	OutCandidate.CandidateSourceHash =
		ComputeCandidateSourceHash(Working, Contract);
	if (!Working.BuildRequest(
			Working.NominalInput, 0x7u, Request, &Failure))
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::InternalError,
			"FinalRequestBuildFailed");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}
	OutCandidate.NominalRequestHash =
		ComputeRequestHash(OutCandidate, Request);
	OutCandidate.NominalResultHash = Arc.ValidationHash;
	if (!Arc.DidHitTarget() || Arc.CompletedAssistCount != 3)
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::NominalRejected,
			"NominalDidNotCompleteF4");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}
	if (!PopulateMetrics(
		Working, Contract, Arc, OutCandidate.Metrics, &Failure))
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::PacingRejected,
			Failure.empty()
				? "PacingOrGeometryGateRejected"
				: Failure.c_str());
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	LaunchInput LowPowerInput = Working.NominalInput;
	LowPowerInput.Power = Contract.LowPowerProbe;
	TrajectoryResult LowPowerResult;
	if (!BuildAndSolve(
			Working,
			LowPowerInput,
			0x7u,
			Request,
			LowPowerResult,
			OutCandidate.SolverInvocationCount,
			&Failure))
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::InternalError,
			"LowPowerProbeSolveFailed");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}
	OutCandidate.Metrics.LowPowerCompletedAssistCount =
		LowPowerResult.CompletedAssistCount;
	if (LowPowerResult.CompletedAssistCount >= 3
		|| LowPowerResult.DidHitTarget())
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::LowPowerGateRejected,
			"LowPowerCompletedFullAssistChain");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	OutCandidate.Metrics.RobustSurvivorCount = 0;
	for (const LaunchInput& Input :
		MakeRobustInputs(Working, Contract))
	{
		TrajectoryResult Result;
		if (BuildAndSolve(
				Working,
				Input,
				0x7u,
				Request,
				Result,
				OutCandidate.SolverInvocationCount,
				&Failure)
			&& Result.DidHitTarget()
			&& ResultPassesAssistPrefix(
				Working, Contract, Result, 3))
		{
			++OutCandidate.Metrics.RobustSurvivorCount;
		}
	}
	if (OutCandidate.Metrics.RobustSurvivorCount
		< Contract.MinimumRobustSurvivorCount)
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::RobustnessRejected,
			"NominalNeighborhoodTooNarrow");
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	for (std::size_t AblationIndex = 0;
		AblationIndex < OutCandidate.Metrics.AblationMasks.size();
		++AblationIndex)
	{
		TrajectoryResult Result;
		const std::uint8_t Mask =
			OutCandidate.Metrics.AblationMasks[AblationIndex];
		if (!BuildAndSolve(
			Working,
			Working.NominalInput,
			Mask,
			Request,
			Result,
			OutCandidate.SolverInvocationCount,
			&Failure))
		{
			Reject(
				nullptr,
				&OutCandidate,
				EvaluationStatus::InternalError,
				"AblationSolveFailed");
			OutCandidate.ScoreHash =
				ComputeCandidateScoreHash(OutCandidate);
			return true;
		}
		OutCandidate.Metrics.AblationHitTarget[AblationIndex] =
			Result.DidHitTarget();
		OutCandidate.Metrics.AblationResultHashes[AblationIndex] =
			Result.ValidationHash;
		if (Result.DidHitTarget())
		{
			Reject(
				nullptr,
				&OutCandidate,
				EvaluationStatus::AblationRejected,
				"AblatedLayoutStillHitsTarget");
			OutCandidate.ScoreHash =
				ComputeCandidateScoreHash(OutCandidate);
			return true;
		}
	}

	OutCandidate.Status = EvaluationStatus::Accepted;
	OutCandidate.Rejection.clear();
	OutCandidate.ScoreHash =
		ComputeCandidateScoreHash(OutCandidate);
	return true;
}

bool ABTS::M11Search::CandidateSearch::CandidateRanksBefore(
	const CandidateRecord& Left,
	const CandidateRecord& Right)
{
	if (Left.IsAccepted() != Right.IsAccepted())
	{
		return Left.IsAccepted();
	}
	if (Left.Metrics.RobustSurvivorCount
		!= Right.Metrics.RobustSurvivorCount)
	{
		return Left.Metrics.RobustSurvivorCount
			> Right.Metrics.RobustSurvivorCount;
	}
	double LeftMinimumDeflection = std::numeric_limits<double>::max();
	double RightMinimumDeflection = std::numeric_limits<double>::max();
	double LeftMinimumEnergy = std::numeric_limits<double>::max();
	double RightMinimumEnergy = std::numeric_limits<double>::max();
	for (std::size_t Index = 0;
		Index < Left.Metrics.Assists.size();
		++Index)
	{
		LeftMinimumDeflection = std::min(
			LeftMinimumDeflection,
			Left.Metrics.Assists[Index].ActualDeflectionRadians);
		RightMinimumDeflection = std::min(
			RightMinimumDeflection,
			Right.Metrics.Assists[Index].ActualDeflectionRadians);
		LeftMinimumEnergy = std::min(
			LeftMinimumEnergy,
			Left.Metrics.Assists[Index].AppliedEnergyGainCM2PerSec2);
		RightMinimumEnergy = std::min(
			RightMinimumEnergy,
			Right.Metrics.Assists[Index].AppliedEnergyGainCM2PerSec2);
	}
	if (LeftMinimumDeflection != RightMinimumDeflection)
	{
		return LeftMinimumDeflection > RightMinimumDeflection;
	}
	if (LeftMinimumEnergy != RightMinimumEnergy)
	{
		return LeftMinimumEnergy > RightMinimumEnergy;
	}
	if (Left.Metrics.MinimumLayoutTurnRadians
		!= Right.Metrics.MinimumLayoutTurnRadians)
	{
		return Left.Metrics.MinimumLayoutTurnRadians
			> Right.Metrics.MinimumLayoutTurnRadians;
	}
	if (Left.Metrics.MaximumCoastSeconds
		!= Right.Metrics.MaximumCoastSeconds)
	{
		return Left.Metrics.MaximumCoastSeconds
			< Right.Metrics.MaximumCoastSeconds;
	}
	if (Left.Metrics.TotalFlightTimeSeconds
		!= Right.Metrics.TotalFlightTimeSeconds)
	{
		return Left.Metrics.TotalFlightTimeSeconds
			< Right.Metrics.TotalFlightTimeSeconds;
	}
	if (Left.CandidateSourceHash != Right.CandidateSourceHash)
	{
		return Left.CandidateSourceHash < Right.CandidateSourceHash;
	}
	return Left.GlobalWorkIndex < Right.GlobalWorkIndex;
}

std::vector<ABTS::M11Search::CandidateRecord>
ABTS::M11Search::CandidateSearch::SelectTopCandidates(
	const CandidateSearchContract& Contract,
	const std::vector<CandidateRecord>& Evaluations,
	const std::uint32_t RequestedCount)
{
	using namespace SearchPrivate;
	std::vector<CandidateRecord> Ranked;
	for (const CandidateRecord& Candidate : Evaluations)
	{
		if (Candidate.IsAccepted())
		{
			Ranked.push_back(Candidate);
		}
	}
	std::sort(Ranked.begin(), Ranked.end(), CandidateRanksBefore);
	std::vector<CandidateRecord> Result;
	for (const CandidateRecord& Candidate : Ranked)
	{
		bool Diverse = true;
		for (const CandidateRecord& Existing : Result)
		{
			if (CandidateDiversityDistance(Candidate, Existing)
				< Contract.MinimumDiversityDistanceCM)
			{
				Diverse = false;
				break;
			}
		}
		if (Diverse)
		{
			Result.push_back(Candidate);
			if (Result.size() >= RequestedCount)
			{
				break;
			}
		}
	}
	return Result;
}

bool ABTS::M11Search::CandidateSearch::RunBatch(
	const CandidateSearchContract& Contract,
	const BatchRequest& Request,
	BatchResult& OutResult,
	std::string* OutFailure)
{
	using namespace SearchPrivate;
	OutResult = BatchResult();
	std::string Failure;
	if (!Contract.IsValid(&Failure) || !Request.IsValid(&Failure))
	{
		OutResult.Diagnostic = Failure;
		if (OutFailure != nullptr)
		{
			*OutFailure = Failure;
		}
		return false;
	}
	std::vector<std::uint64_t> WorkIndices;
	for (std::uint64_t Index = Request.ShardIndex;
		Index < Request.GlobalWorkItemCount;
		Index += Request.ShardCount)
	{
		WorkIndices.push_back(Index);
	}
	if (Request.LocalBeginOffset >= WorkIndices.size())
	{
		WorkIndices.clear();
	}
	else if (Request.LocalBeginOffset > 0)
	{
		WorkIndices.erase(
			WorkIndices.begin(),
			WorkIndices.begin()
				+ static_cast<std::ptrdiff_t>(
					Request.LocalBeginOffset));
	}
	if (Request.LocalWorkItemLimit > 0
		&& WorkIndices.size() > Request.LocalWorkItemLimit)
	{
		WorkIndices.resize(static_cast<std::size_t>(
			Request.LocalWorkItemLimit));
	}
	OutResult.Evaluations.resize(WorkIndices.size());
	const auto Start = std::chrono::steady_clock::now();
	std::atomic<std::size_t> Cursor{0};
	const std::size_t ThreadCount = std::min<std::size_t>(
		Request.ThreadCount,
		std::max<std::size_t>(1, WorkIndices.size()));
	std::vector<std::thread> Threads;
	Threads.reserve(ThreadCount);
	for (std::size_t ThreadIndex = 0;
		ThreadIndex < ThreadCount;
		++ThreadIndex)
	{
		Threads.emplace_back([&]()
		{
			while (true)
			{
				const std::size_t LocalIndex = Cursor.fetch_add(1);
				if (LocalIndex >= WorkIndices.size())
				{
					break;
				}
				CandidateRecord& Candidate =
					OutResult.Evaluations[LocalIndex];
				std::string LocalFailure;
				if (!EvaluateWorkItem(
					Contract,
					WorkIndices[LocalIndex],
					Candidate,
					&LocalFailure))
				{
					Candidate = CandidateRecord();
					Candidate.GlobalWorkIndex =
						WorkIndices[LocalIndex];
					Candidate.Status =
						EvaluationStatus::InternalError;
					Candidate.Rejection =
						LocalFailure.empty()
						? "EvaluateWorkItemFailed"
						: LocalFailure;
					Candidate.ScoreHash =
						ComputeCandidateScoreHash(Candidate);
				}
			}
		});
	}
	for (std::thread& Thread : Threads)
	{
		Thread.join();
	}
	const auto End = std::chrono::steady_clock::now();
	OutResult.WallClockSeconds =
		std::chrono::duration<double>(End - Start).count();
	for (const CandidateRecord& Candidate : OutResult.Evaluations)
	{
		OutResult.SolverInvocationCount +=
			static_cast<std::uint64_t>(
				std::max(0, Candidate.SolverInvocationCount));
	}
	OutResult.EvaluationAggregateHash =
		ComputeEvaluationAggregateHash(OutResult.Evaluations);

	OutResult.TopCandidates = SelectTopCandidates(
		Contract,
		OutResult.Evaluations,
		Request.RequestedTopCandidateCount);
	OutResult.CandidateAggregateHash =
		ComputeEvaluationAggregateHash(OutResult.TopCandidates);
	OutResult.Diagnostic =
		OutResult.TopCandidates.empty()
		? "CompletedInsufficientCandidates"
		: "Completed";
	return true;
}

bool ABTS::M11Search::CandidateSearch::ReplayCandidate(
	const CandidateRecord& Candidate,
	const std::uint8_t EnabledAssistMask,
	M11Core::TrajectoryResult& OutResult,
	std::string* OutFailure)
{
	M11Core::TrajectoryRequest Request;
	if (!Candidate.Layout.BuildRequest(
			Candidate.Layout.NominalInput,
			EnabledAssistMask,
			Request,
			OutFailure))
	{
		return false;
	}
	return M11Core::GravityAssistSolver::Solve(
		Request, OutResult, OutFailure);
}
