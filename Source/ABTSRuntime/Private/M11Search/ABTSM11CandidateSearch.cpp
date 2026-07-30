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

	Vec3d ComputePresentationNormal(const CandidateLayout& Layout);

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

	std::array<std::int8_t, GravityScenario::AssistCount>
		MakePreferredPassSidePattern(
			const CandidateSearchContract& Contract,
			const std::uint64_t GlobalWorkIndex)
	{
		const std::uint64_t SampleIndex =
			GlobalWorkIndex + 1ull + Contract.SearchSeed % 104729ull;
		const std::int8_t FirstSign =
			Halton(SampleIndex, 73) < 0.5 ? -1 : 1;
		return {
			FirstSign,
			static_cast<std::int8_t>(-FirstSign),
			FirstSign};
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
		std::array<std::int8_t, GravityScenario::AssistCount>
			PreferredPassSideSigns{-1, 1, -1};
		double TargetRadiusCM = 4000.0;
	};

	WorkParameters MakeWorkParameters(
		const CandidateSearchContract& Contract,
		const std::uint64_t GlobalWorkIndex)
	{
		const std::uint64_t SampleIndex =
			GlobalWorkIndex + 1ull + Contract.SearchSeed % 104729ull;
		WorkParameters Result;
		Result.PreferredPassSideSigns =
			MakePreferredPassSidePattern(Contract, GlobalWorkIndex);
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

	struct LateralTurnMeasurement
	{
		double SignedRadians = 0.0;
		double AxisProjection = 0.0;
	};

	LateralTurnMeasurement MeasureLateralTurn(
		const Vec3d& EntryVelocity,
		const Vec3d& ExitVelocity,
		const Vec3d& PresentationNormal,
		const double MinimumAxisProjection)
	{
		const Vec3d EntryDirection = EntryVelocity.GetSafeNormal();
		const Vec3d ExitDirection = ExitVelocity.GetSafeNormal();
		const Vec3d TurnAxis = Vec3d::CrossProduct(
			EntryDirection, ExitDirection);
		const double SignedProjection = Vec3d::DotProduct(
			TurnAxis, PresentationNormal);
		const double TurnAxisLength = TurnAxis.Length();
		const double ActualDeflectionRadians = AngleBetween(
			EntryDirection, ExitDirection);
		LateralTurnMeasurement Measurement;
		Measurement.AxisProjection =
			TurnAxisLength > DoubleSmallNumber
				? std::clamp(
					std::abs(SignedProjection) / TurnAxisLength,
					0.0,
					1.0)
				: 0.0;
		if (Measurement.AxisProjection >= MinimumAxisProjection)
		{
			Measurement.SignedRadians =
				SignedProjection < 0.0
					? -ActualDeflectionRadians
					: ActualDeflectionRadians;
		}
		return Measurement;
	}

	PartialAlternationMetrics MeasurePartialAlternation(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const TrajectoryResult& Result,
		const std::int32_t LastAssistIndex)
	{
		PartialAlternationMetrics Metrics;
		const std::int32_t CompletedLimit = std::clamp(
			std::min(LastAssistIndex, Result.CompletedAssistCount),
			0,
			GravityScenario::AssistCount);
		const Vec3d PresentationNormal =
			ComputePresentationNormal(Layout);
		for (std::int32_t AssistIndex = 1;
			AssistIndex <= CompletedLimit;
			++AssistIndex)
		{
			const TrajectoryEvent* Enter = Result.FindAssistEvent(
				TrajectoryEventType::AssistEnter, AssistIndex);
			const TrajectoryEvent* Exit = Result.FindAssistEvent(
				TrajectoryEventType::AssistExit, AssistIndex);
			if (Enter == nullptr || Exit == nullptr)
			{
				break;
			}
			const std::size_t Index =
				static_cast<std::size_t>(AssistIndex - 1);
			Metrics.SignedLateralTurnRadians[Index] =
				MeasureLateralTurn(
					Enter->VelocityCMPerSec,
					Exit->VelocityCMPerSec,
					PresentationNormal,
					Contract.MinimumLateralTurnAxisProjection)
					.SignedRadians;
			++Metrics.CompletedAssistCount;
			if (Index > 0
				&& Metrics.SignedLateralTurnRadians[Index - 1]
					* Metrics.SignedLateralTurnRadians[Index]
					< 0.0)
			{
				++Metrics.PartialAlternationCount;
			}
		}
		return Metrics;
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

	bool MatchesPreferredPassSide(
		const AllowedPassSide Side,
		const std::int8_t PreferredSign)
	{
		const bool Positive =
			Side == AllowedPassSide::PositiveT
			|| Side == AllowedPassSide::PositiveR;
		const bool Negative =
			Side == AllowedPassSide::NegativeT
			|| Side == AllowedPassSide::NegativeR;
		return PreferredSign > 0 ? Positive : Negative;
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
		std::array<double, GravityScenario::AssistCount>
			SignedLateralTurnRadians{};
		std::int32_t PartialAlternationCount = 0;
		std::int32_t RobustSurvivorCount = 0;
		bool PassesProjectedLayoutTurnGate = false;
		bool MatchesPreferredSide = false;
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
		const Vec3d PresentationNormal =
			ComputePresentationNormal(Layout);
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
			const LateralTurnMeasurement LateralTurn =
				MeasureLateralTurn(
					Enter->VelocityCMPerSec,
					Exit->VelocityCMPerSec,
					PresentationNormal,
					Contract.MinimumLateralTurnAxisProjection);
			const double Clearance =
				Exit->ClosestDistanceCM - Body.CollisionRadiusCM;
			if (Exit->CorridorQuality < Contract.MinimumCorridorQuality
				|| Exit->AppliedEnergyChangeCM2PerSec2
					< Contract.MinimumEnergyGainCM2PerSec2
				|| Side <= 0.0
				|| Duration < Contract.MinimumInfluenceDurationSeconds
				|| Duration > Contract.MaximumInfluenceDurationSeconds
				|| Deflection < Contract.MinimumDeflectionRadians
				|| LateralTurn.AxisProjection
					< Contract.MinimumLateralTurnAxisProjection
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
		if (Left.PassesProjectedLayoutTurnGate
			!= Right.PassesProjectedLayoutTurnGate)
		{
			return Left.PassesProjectedLayoutTurnGate;
		}
		if (Left.PartialAlternationCount
			!= Right.PartialAlternationCount)
		{
			return Left.PartialAlternationCount
				> Right.PartialAlternationCount;
		}
		if (Left.MatchesPreferredSide != Right.MatchesPreferredSide)
		{
			return Left.MatchesPreferredSide;
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
		std::int32_t LowPowerSolveRejected = 0;
		std::int32_t LowPowerReachedAssistRejected = 0;
		std::string FirstLowPowerSolveFailure;
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

							if (AssistIndex == 1)
							{
								LaunchInput LowPowerInput =
									Layout.NominalInput;
								LowPowerInput.Power =
									Contract.LowPowerProbe;
								TrajectoryRequest LowPowerRequest;
								TrajectoryResult LowPowerResult;
								std::string LowPowerFailure;
								if (!BuildAndSolve(
										Layout,
										LowPowerInput,
										0x7u,
										LowPowerRequest,
										LowPowerResult,
										InOutSolveCount,
										&LowPowerFailure))
								{
									++LowPowerSolveRejected;
									if (FirstLowPowerSolveFailure.empty())
									{
										FirstLowPowerSolveFailure =
											LowPowerFailure.empty()
												? "Unspecified"
												: LowPowerFailure;
									}
									continue;
								}
								const bool bQualifiedLowPowerAssist1 =
									ResultPassesAssistPrefix(
										Layout,
										Contract,
										LowPowerResult,
										1);
								if (CandidateSearch::
										ShouldRejectLowPowerResult(
											LowPowerResult,
											bQualifiedLowPowerAssist1))
								{
									++LowPowerReachedAssistRejected;
									continue;
								}
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
							Candidate.PassesProjectedLayoutTurnGate =
								Candidate.ProjectedLayoutTurnRadians
									>= Contract.MinimumLayoutTurnRadians;
							const PartialAlternationMetrics
								PartialAlternation =
									MeasurePartialAlternation(
										Candidate.Layout,
										Contract,
										Candidate.Result,
										AssistIndex);
							Candidate.SignedLateralTurnRadians =
								PartialAlternation
									.SignedLateralTurnRadians;
							Candidate.PartialAlternationCount =
								PartialAlternation
									.PartialAlternationCount;
							Candidate.MatchesPreferredSide =
								MatchesPreferredPassSide(
									Candidate.Layout.Scenario.GetAssist(
										AssistIndex).AllowedPassSideValue,
									Parameters.PreferredPassSideSigns[
										static_cast<std::size_t>(
											AssistIndex - 1)]);
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
				+ ":LPS=" + std::to_string(LowPowerSolveRejected)
				+ ":LPR="
					+ std::to_string(LowPowerReachedAssistRejected)
				+ ":MaxD=" + std::to_string(
					MaximumRejectedDeflection);
			if (!FirstLowPowerSolveFailure.empty())
			{
				OutDiagnostic += ":LPF="
					+ FirstLowPowerSolveFailure;
			}
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
			OutDiagnostic =
				"StageRobustnessRejected:LPS="
				+ std::to_string(LowPowerSolveRejected)
				+ ":LPR="
				+ std::to_string(LowPowerReachedAssistRejected);
			if (!FirstLowPowerSolveFailure.empty())
			{
				OutDiagnostic += ":LPF="
					+ FirstLowPowerSolveFailure;
			}
			return false;
		}
		if (AssistIndex >= 2)
		{
			const auto BestAlternation = std::max_element(
				Candidates.begin(),
				Candidates.end(),
				[](const StageCandidate& Left,
					const StageCandidate& Right)
				{
					return Left.PartialAlternationCount
						< Right.PartialAlternationCount;
				});
			const std::int32_t MaximumPartialAlternationCount =
				BestAlternation->PartialAlternationCount;
			Candidates.erase(
				std::remove_if(
					Candidates.begin(),
					Candidates.end(),
					[MaximumPartialAlternationCount](
						const StageCandidate& Candidate)
					{
						return Candidate.PartialAlternationCount
							< MaximumPartialAlternationCount;
					}),
				Candidates.end());
		}
		const bool HasPreferredSideCandidate =
			std::any_of(
				Candidates.begin(),
				Candidates.end(),
				[](const StageCandidate& Candidate)
				{
					return Candidate.MatchesPreferredSide;
				});
		if (HasPreferredSideCandidate)
		{
			Candidates.erase(
				std::remove_if(
					Candidates.begin(),
					Candidates.end(),
					[](const StageCandidate& Candidate)
					{
						return !Candidate.MatchesPreferredSide;
					}),
				Candidates.end());
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

	std::array<bool, 4> ClassifyInputSets(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const TrajectoryResult& Result)
	{
		std::array<bool, 4> Membership{};
		Membership[0] = ResultPassesAssistPrefix(
			Layout, Contract, Result, 1);
		Membership[1] = Membership[0]
			&& ResultPassesAssistPrefix(Layout, Contract, Result, 2);
		Membership[2] = Membership[1]
			&& ResultPassesAssistPrefix(Layout, Contract, Result, 3);
		Membership[3] = Membership[2] && Result.DidHitTarget();
		return Membership;
	}

	bool SameInput(const LaunchInput& Left, const LaunchInput& Right)
	{
		return Left.YawDegrees == Right.YawDegrees
			&& Left.PitchDegrees == Right.PitchDegrees
			&& Left.Power == Right.Power;
	}

	double InputDistanceSquared(
		const CandidateLayout& Layout,
		const LaunchInput& Input)
	{
		const double YawRange = Layout.Launch.MaximumYawDegrees
			- Layout.Launch.MinimumYawDegrees;
		const double PitchRange = Layout.Launch.MaximumPitchDegrees
			- Layout.Launch.MinimumPitchDegrees;
		const double PowerRange = Layout.Launch.MaximumPower
			- Layout.Launch.MinimumPower;
		const double DY = (Input.YawDegrees
			- Layout.NominalInput.YawDegrees) / YawRange;
		const double DP = (Input.PitchDegrees
			- Layout.NominalInput.PitchDegrees) / PitchRange;
		const double DW = (Input.Power
			- Layout.NominalInput.Power) / PowerRange;
		return DY * DY + DP * DP + DW * DW;
	}

	double Cross2D(
		const YawPitchPoint& Origin,
		const YawPitchPoint& A,
		const YawPitchPoint& B)
	{
		return (A.YawDegrees - Origin.YawDegrees)
				* (B.PitchDegrees - Origin.PitchDegrees)
			- (A.PitchDegrees - Origin.PitchDegrees)
				* (B.YawDegrees - Origin.YawDegrees);
	}

	std::vector<YawPitchPoint> BuildConvexHull(
		std::vector<YawPitchPoint> Points)
	{
		std::sort(
			Points.begin(),
			Points.end(),
			[](const YawPitchPoint& Left, const YawPitchPoint& Right)
			{
				return Left.YawDegrees < Right.YawDegrees
					|| (Left.YawDegrees == Right.YawDegrees
						&& Left.PitchDegrees < Right.PitchDegrees);
			});
		Points.erase(
			std::unique(
				Points.begin(),
				Points.end(),
				[](const YawPitchPoint& Left, const YawPitchPoint& Right)
				{
					return Left.YawDegrees == Right.YawDegrees
						&& Left.PitchDegrees == Right.PitchDegrees;
				}),
			Points.end());
		if (Points.size() <= 2)
		{
			return Points;
		}
		std::vector<YawPitchPoint> Hull;
		Hull.reserve(Points.size() * 2);
		for (const YawPitchPoint& Point : Points)
		{
			while (Hull.size() >= 2
				&& Cross2D(
					Hull[Hull.size() - 2],
					Hull.back(),
					Point) <= 0.0)
			{
				Hull.pop_back();
			}
			Hull.push_back(Point);
		}
		const std::size_t LowerSize = Hull.size();
		for (auto Iterator = Points.rbegin() + 1;
			Iterator != Points.rend();
			++Iterator)
		{
			while (Hull.size() > LowerSize
				&& Cross2D(
					Hull[Hull.size() - 2],
					Hull.back(),
					*Iterator) <= 0.0)
			{
				Hull.pop_back();
			}
			Hull.push_back(*Iterator);
		}
		if (!Hull.empty())
		{
			Hull.pop_back();
		}
		return Hull;
	}

	double ConvexHullArea(const std::vector<YawPitchPoint>& Hull)
	{
		if (Hull.size() < 3)
		{
			return 0.0;
		}
		double TwiceArea = 0.0;
		for (std::size_t Index = 0; Index < Hull.size(); ++Index)
		{
			const YawPitchPoint& A = Hull[Index];
			const YawPitchPoint& B = Hull[(Index + 1) % Hull.size()];
			TwiceArea += A.YawDegrees * B.PitchDegrees
				- A.PitchDegrees * B.YawDegrees;
		}
		return std::abs(TwiceArea) * 0.5;
	}

	bool HullContains(
		const std::vector<YawPitchPoint>& Hull,
		const YawPitchPoint& Point)
	{
		if (Hull.size() < 3)
		{
			return false;
		}
		constexpr double Tolerance = 1.0e-12;
		for (std::size_t Index = 0; Index < Hull.size(); ++Index)
		{
			if (Cross2D(
				Hull[Index],
				Hull[(Index + 1) % Hull.size()],
				Point) < -Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	void PopulateHullMetrics(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		const std::vector<YawPitchPoint>& Evidence,
		InputSetMetrics& OutSet)
	{
		OutSet.ScreenAimHullEvidencePointCount =
			static_cast<std::int32_t>(Evidence.size());
		OutSet.ScreenAimHullYawPitch = BuildConvexHull(Evidence);
		OutSet.ScreenAimHullAreaSquareDegrees =
			ConvexHullArea(OutSet.ScreenAimHullYawPitch);
		if (!Evidence.empty())
		{
			double MinimumYaw = Evidence.front().YawDegrees;
			double MaximumYaw = MinimumYaw;
			double MinimumPitch = Evidence.front().PitchDegrees;
			double MaximumPitch = MinimumPitch;
			for (const YawPitchPoint& Point : Evidence)
			{
				MinimumYaw = std::min(MinimumYaw, Point.YawDegrees);
				MaximumYaw = std::max(MaximumYaw, Point.YawDegrees);
				MinimumPitch = std::min(
					MinimumPitch, Point.PitchDegrees);
				MaximumPitch = std::max(
					MaximumPitch, Point.PitchDegrees);
			}
			OutSet.ScreenAimHullYawSpanDegrees =
				MaximumYaw - MinimumYaw;
			OutSet.ScreenAimHullPitchSpanDegrees =
				MaximumPitch - MinimumPitch;
		}
		const double LaunchArea =
			(Layout.Launch.MaximumYawDegrees
				- Layout.Launch.MinimumYawDegrees)
			* (Layout.Launch.MaximumPitchDegrees
				- Layout.Launch.MinimumPitchDegrees);
		OutSet.ScreenAimHullNormalizedArea = LaunchArea > 0.0
			? OutSet.ScreenAimHullAreaSquareDegrees / LaunchArea
			: 0.0;
		const double BoundingArea = OutSet.ScreenAimHullYawSpanDegrees
			* OutSet.ScreenAimHullPitchSpanDegrees;
		OutSet.ScreenAimHullCompactness = BoundingArea > 0.0
			? std::clamp(
				OutSet.ScreenAimHullAreaSquareDegrees / BoundingArea,
				0.0,
				1.0)
			: 0.0;
		OutSet.ScreenAimHullContainsNominal = HullContains(
			OutSet.ScreenAimHullYawPitch,
			YawPitchPoint{
				Layout.NominalInput.YawDegrees,
				Layout.NominalInput.PitchDegrees});
		OutSet.ScreenAimHullCompliant =
			OutSet.ScreenAimHullEvidencePointCount
				>= Contract.MinimumHullEvidenceCount
			&& OutSet.ScreenAimHullYawPitch.size() >= 3
			&& OutSet.ScreenAimHullAreaSquareDegrees
				>= Contract.MinimumHullAreaSquareDegrees
			&& OutSet.ScreenAimHullYawSpanDegrees
				>= Contract.MinimumHullYawSpanDegrees
			&& OutSet.ScreenAimHullPitchSpanDegrees
				>= Contract.MinimumHullPitchSpanDegrees;
	}

	double Ratio(
		const std::int32_t Numerator,
		const std::int32_t Denominator)
	{
		return Denominator > 0
			? static_cast<double>(Numerator)
				/ static_cast<double>(Denominator)
			: 0.0;
	}

	double PrefixRetentionScore(
		const CandidateSearchContract& Contract,
		const double RatioValue)
	{
		if (RatioValue < Contract.MinimumPrefixRetentionRatio
			|| RatioValue > Contract.MaximumPrefixRetentionRatio)
		{
			return 0.0;
		}
		if (RatioValue < Contract.FullScoreMinimumPrefixRetentionRatio)
		{
			return (RatioValue - Contract.MinimumPrefixRetentionRatio)
				/ (Contract.FullScoreMinimumPrefixRetentionRatio
					- Contract.MinimumPrefixRetentionRatio);
		}
		if (RatioValue > Contract.FullScoreMaximumPrefixRetentionRatio)
		{
			return (Contract.MaximumPrefixRetentionRatio - RatioValue)
				/ (Contract.MaximumPrefixRetentionRatio
					- Contract.FullScoreMaximumPrefixRetentionRatio);
		}
		return 1.0;
	}

	bool AnalyzeInputDomain(
		const CandidateLayout& Layout,
		const CandidateSearchContract& Contract,
		CandidateMetrics& OutMetrics,
		std::int32_t& InOutSolveCount,
		std::string* OutFailure)
	{
		std::array<std::vector<LaunchInput>, 4>
			FullDomainMemberInputs;
		std::array<std::vector<YawPitchPoint>, 4>
			ScreenAimHullEvidence;
		OutMetrics.ScreenAimSampleCount =
			Contract.ScreenAimSampleCount;

		const auto RecordSolveFailure =
			[OutFailure](
				const char* Domain,
				const std::int32_t SetIndex,
				const std::int32_t SampleIndex,
				const std::string& Detail,
				std::int32_t& InOutFailureCount)
			{
				++InOutFailureCount;
				if (OutFailure != nullptr)
				{
					*OutFailure = Domain;
					if (SetIndex >= 0)
					{
						*OutFailure += "[S"
							+ std::to_string(SetIndex + 1) + "]";
					}
					*OutFailure += "[Sample="
						+ std::to_string(SampleIndex) + "]";
					*OutFailure += ":"
						+ (Detail.empty()
							? "UnspecifiedBuildAndSolveFailure"
							: Detail);
				}
				return false;
			};

		const std::uint64_t ScreenAimOffset =
			Contract.ScreenAimSeed % 1000003ull;
		for (std::int32_t Index = 0;
			Index < Contract.ScreenAimSampleCount;
			++Index)
		{
			const std::uint64_t SampleIndex =
				ScreenAimOffset
				+ static_cast<std::uint64_t>(Index) + 1ull;
			const LaunchInput Input{
				M11Core::Lerp(
					Layout.Launch.MinimumYawDegrees,
					Layout.Launch.MaximumYawDegrees,
					Halton(SampleIndex, 2)),
				M11Core::Lerp(
					Layout.Launch.MinimumPitchDegrees,
					Layout.Launch.MaximumPitchDegrees,
					Halton(SampleIndex, 3)),
				Layout.NominalInput.Power};
			TrajectoryRequest Request;
			TrajectoryResult Result;
			std::string SolveFailure;
			if (!BuildAndSolve(
				Layout,
				Input,
				0x7u,
				Request,
				Result,
				InOutSolveCount,
				&SolveFailure))
			{
				return RecordSolveFailure(
					"ScreenAimBuildAndSolveFailed",
					-1,
					Index,
					SolveFailure,
					OutMetrics.ScreenAimSolveFailureCount);
			}
			const std::array<bool, 4> Membership =
				ClassifyInputSets(Layout, Contract, Result);
			for (std::size_t SetIndex = 0;
				SetIndex < Membership.size();
				++SetIndex)
			{
				if (Membership[SetIndex])
				{
					++OutMetrics.InputSets[SetIndex].ScreenAimCount;
					ScreenAimHullEvidence[SetIndex].push_back(
						YawPitchPoint{
							Input.YawDegrees,
							Input.PitchDegrees});
				}
			}
		}

		for (std::size_t SetIndex = 0;
			SetIndex < OutMetrics.InputSets.size();
			++SetIndex)
		{
			InputSetMetrics& Set = OutMetrics.InputSets[SetIndex];
			const std::int32_t ParentScreenAimCount = SetIndex == 0
				? Contract.ScreenAimSampleCount
				: OutMetrics.InputSets[SetIndex - 1].ScreenAimCount;
			Set.ScreenAimRetentionRatio =
				Ratio(Set.ScreenAimCount, ParentScreenAimCount);
			Set.ScreenAimRetentionCompliant = SetIndex < 3
				&& Set.ScreenAimRetentionRatio
					>= Contract.MinimumPrefixRetentionRatio
				&& Set.ScreenAimRetentionRatio
					<= Contract.MaximumPrefixRetentionRatio;
		}

		// The gameplay-sized prefix ratios are the first hard input-domain
		// gate. Hull shape is intentionally not considered until they pass.
		for (std::size_t SetIndex = 0; SetIndex < 3; ++SetIndex)
		{
			const InputSetMetrics& Set = OutMetrics.InputSets[SetIndex];
			if (!Set.ScreenAimRetentionCompliant)
			{
				if (OutFailure != nullptr)
				{
					*OutFailure = "S"
						+ std::to_string(SetIndex + 1)
						+ "ScreenAimRetentionOutsideRange:"
						+ std::to_string(Set.ScreenAimRetentionRatio);
				}
				return false;
			}
		}

		for (std::size_t SetIndex = 0;
			SetIndex < OutMetrics.InputSets.size();
			++SetIndex)
		{
			PopulateHullMetrics(
				Layout,
				Contract,
				ScreenAimHullEvidence[SetIndex],
				OutMetrics.InputSets[SetIndex]);
		}

		for (std::size_t SetIndex = 0; SetIndex < 3; ++SetIndex)
		{
			if (!OutMetrics.InputSets[SetIndex]
					.ScreenAimHullCompliant)
			{
				if (OutFailure != nullptr)
				{
					*OutFailure = "S"
						+ std::to_string(SetIndex + 1)
						+ "ScreenAimHullDegenerate";
				}
				return false;
			}
		}

		OutMetrics.FullDomainSampleCount =
			Contract.MonteCarloSampleCount;
		const std::uint64_t FullDomainOffset =
			Contract.MonteCarloSeed % 1000003ull;
		for (std::int32_t Index = 0;
			Index < Contract.MonteCarloSampleCount;
			++Index)
		{
			const std::uint64_t SampleIndex =
				FullDomainOffset
				+ static_cast<std::uint64_t>(Index) + 1ull;
			const LaunchInput Input{
				M11Core::Lerp(
					Layout.Launch.MinimumYawDegrees,
					Layout.Launch.MaximumYawDegrees,
					Halton(SampleIndex, 2)),
				M11Core::Lerp(
					Layout.Launch.MinimumPitchDegrees,
					Layout.Launch.MaximumPitchDegrees,
					Halton(SampleIndex, 3)),
				M11Core::Lerp(
					Layout.Launch.MinimumPower,
					Layout.Launch.MaximumPower,
					Halton(SampleIndex, 5))};
			TrajectoryRequest Request;
			TrajectoryResult Result;
			std::string SolveFailure;
			if (!BuildAndSolve(
				Layout,
				Input,
				0x7u,
				Request,
				Result,
				InOutSolveCount,
				&SolveFailure))
			{
				return RecordSolveFailure(
					"FullDomainBuildAndSolveFailed",
					-1,
					Index,
					SolveFailure,
					OutMetrics.FullDomainSolveFailureCount);
			}
			const std::array<bool, 4> Membership =
				ClassifyInputSets(Layout, Contract, Result);
			for (std::size_t SetIndex = 0;
				SetIndex < Membership.size();
				++SetIndex)
			{
				if (Membership[SetIndex])
				{
					++OutMetrics.InputSets[SetIndex].FullDomainCount;
					FullDomainMemberInputs[SetIndex].push_back(Input);
				}
			}
		}

		for (std::size_t SetIndex = 0;
			SetIndex < OutMetrics.InputSets.size();
			++SetIndex)
		{
			const std::int32_t ParentFullDomainCount = SetIndex == 0
				? Contract.MonteCarloSampleCount
				: OutMetrics.InputSets[SetIndex - 1].FullDomainCount;
			OutMetrics.InputSets[SetIndex].FullDomainRetentionRatio =
				Ratio(
					OutMetrics.InputSets[SetIndex].FullDomainCount,
					ParentFullDomainCount);
		}

		for (std::size_t SetIndex = 0;
			SetIndex < OutMetrics.InputSets.size();
			++SetIndex)
		{
			InputSetMetrics& Set = OutMetrics.InputSets[SetIndex];
			Set.ConditionalProbeCount =
				Contract.ConditionalProbeSamplesPerSet;
			std::vector<LaunchInput> Seeds;
			if (SetIndex == 0)
			{
				Seeds.push_back(Layout.NominalInput);
			}
			else
			{
				Seeds = FullDomainMemberInputs[SetIndex - 1];
				std::sort(
					Seeds.begin(),
					Seeds.end(),
					[&Layout](
						const LaunchInput& Left,
						const LaunchInput& Right)
					{
						const double LeftDistance =
							InputDistanceSquared(Layout, Left);
						const double RightDistance =
							InputDistanceSquared(Layout, Right);
						if (LeftDistance != RightDistance)
						{
							return LeftDistance < RightDistance;
						}
						if (Left.YawDegrees != Right.YawDegrees)
						{
							return Left.YawDegrees < Right.YawDegrees;
						}
						if (Left.PitchDegrees != Right.PitchDegrees)
						{
							return Left.PitchDegrees
								< Right.PitchDegrees;
						}
						return Left.Power < Right.Power;
					});
				Seeds.erase(
					std::unique(Seeds.begin(), Seeds.end(), SameInput),
					Seeds.end());
				if (Seeds.size() > 16)
				{
					Seeds.resize(16);
				}
			}
			if (Seeds.empty())
			{
				Seeds.push_back(Layout.NominalInput);
			}
			const double Scale =
				1.0 / static_cast<double>(SetIndex + 1);
			const std::uint64_t ConditionalOffset =
				FullDomainOffset
				+ 100003ull
					* static_cast<std::uint64_t>(SetIndex + 1);
			for (std::int32_t ProbeIndex = 0;
				ProbeIndex < Contract.ConditionalProbeSamplesPerSet;
				++ProbeIndex)
			{
				const LaunchInput& Seed = Seeds[
					static_cast<std::size_t>(ProbeIndex)
						% Seeds.size()];
				const std::uint64_t SampleIndex =
					ConditionalOffset
					+ static_cast<std::uint64_t>(ProbeIndex) + 1ull;
				const auto SignedHalton =
					[SampleIndex](const std::uint32_t Base)
					{
						return 2.0 * Halton(SampleIndex, Base) - 1.0;
					};
				LaunchInput Input{
					std::clamp(
						Seed.YawDegrees
							+ SignedHalton(7)
								* Contract
									.ConditionalYawHalfExtentDegrees
								* Scale,
						Layout.Launch.MinimumYawDegrees,
						Layout.Launch.MaximumYawDegrees),
					std::clamp(
						Seed.PitchDegrees
							+ SignedHalton(11)
								* Contract
									.ConditionalPitchHalfExtentDegrees
								* Scale,
						Layout.Launch.MinimumPitchDegrees,
						Layout.Launch.MaximumPitchDegrees),
					std::clamp(
						Seed.Power
							+ SignedHalton(13)
								* Contract.ConditionalPowerHalfExtent
								* Scale,
						Layout.Launch.MinimumPower,
						Layout.Launch.MaximumPower)};
				TrajectoryRequest Request;
				TrajectoryResult Result;
				std::string SolveFailure;
				if (!BuildAndSolve(
					Layout,
					Input,
					0x7u,
					Request,
					Result,
					InOutSolveCount,
					&SolveFailure))
				{
					return RecordSolveFailure(
						"ConditionalBuildAndSolveFailed",
						static_cast<std::int32_t>(SetIndex),
						ProbeIndex,
						SolveFailure,
						OutMetrics.ConditionalSolveFailureCount);
				}
				const std::array<bool, 4> Membership =
					ClassifyInputSets(Layout, Contract, Result);
				const bool ParentMember =
					SetIndex == 0 || Membership[SetIndex - 1];
				if (ParentMember)
				{
					++Set.ConditionalParentCount;
				}
				if (ParentMember && Membership[SetIndex])
				{
					++Set.ConditionalMemberCount;
				}
			}
			Set.ConditionalRetentionRatio = Ratio(
				Set.ConditionalMemberCount,
				Set.ConditionalParentCount);
		}

		double RetentionScore = 0.0;
		for (std::size_t SetIndex = 0; SetIndex < 3; ++SetIndex)
		{
			const InputSetMetrics& Set =
				OutMetrics.InputSets[SetIndex];
			RetentionScore += PrefixRetentionScore(
				Contract, Set.ScreenAimRetentionRatio);
		}
		OutMetrics.PrefixRetentionScore = RetentionScore / 3.0;

		double HullScore = 0.0;
		for (std::size_t SetIndex = 0; SetIndex < 3; ++SetIndex)
		{
			const InputSetMetrics& Set =
				OutMetrics.InputSets[SetIndex];
			const double AreaScore = std::clamp(
				Set.ScreenAimHullAreaSquareDegrees / 0.05,
				0.0,
				1.0);
			const double YawScore = std::clamp(
				Set.ScreenAimHullYawSpanDegrees / 0.25,
				0.0,
				1.0);
			const double PitchScore = std::clamp(
				Set.ScreenAimHullPitchSpanDegrees / 0.25,
				0.0,
				1.0);
			HullScore += 0.30 * AreaScore
				+ 0.20 * YawScore
				+ 0.20 * PitchScore
				+ 0.15 * Set.ScreenAimHullCompactness
				+ 0.15
					* (Set.ScreenAimHullContainsNominal ? 1.0 : 0.0);
		}
		OutMetrics.PrefixHullScore = HullScore / 3.0;
		if (OutFailure != nullptr)
		{
			OutFailure->clear();
		}
		return true;
	}

	Vec3d ComputePresentationNormal(const CandidateLayout& Layout)
	{
		const Vec3d First =
			Layout.Scenario.GetAssist(1).CenterCM
			- Layout.Launch.PouchLocalPositionCM;
		const Vec3d Second =
			Layout.Scenario.GetAssist(2).CenterCM
			- Layout.Scenario.GetAssist(1).CenterCM;
		Vec3d Normal = Vec3d::CrossProduct(First, Second).GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			const Vec3d Third =
				Layout.Scenario.GetAssist(3).CenterCM
				- Layout.Scenario.GetAssist(2).CenterCM;
			Normal =
				Vec3d::CrossProduct(Second, Third).GetSafeNormal();
		}
		if (Normal.IsNearlyZero())
		{
			Normal = Vec3d(0.0, 0.0, 1.0);
		}
		if (Vec3d::DotProduct(Normal, Vec3d(0.0, 0.0, 1.0)) < 0.0)
		{
			Normal = -Normal;
		}
		return Normal;
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
		const Vec3d PresentationNormal =
			ComputePresentationNormal(Layout);
		OutMetrics.MinimumReadableDeflectionRadians =
			std::numeric_limits<double>::max();
		for (std::int32_t AssistIndex = 1;
			AssistIndex <= GravityScenario::AssistCount;
			++AssistIndex)
		{
			const std::size_t Index =
				static_cast<std::size_t>(AssistIndex - 1);
			const AssistPhaseDiagnostics& Phase = Pacing.Assists[Index];
			const TrajectoryEvent* Enter = Result.FindAssistEvent(
				TrajectoryEventType::AssistEnter, AssistIndex);
			const TrajectoryEvent* Exit = Result.FindAssistEvent(
				TrajectoryEventType::AssistExit, AssistIndex);
			if (!Phase.Complete || Enter == nullptr || Exit == nullptr)
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
			const LateralTurnMeasurement LateralTurn =
				MeasureLateralTurn(
					Phase.EntrySpeedCMPerSec > 0.0
						? Enter->VelocityCMPerSec
						: Vec3d(),
					Exit->VelocityCMPerSec,
					PresentationNormal,
					Contract.MinimumLateralTurnAxisProjection);
			Metrics.LateralTurnAxisProjection =
				LateralTurn.AxisProjection;
			Metrics.SignedLateralTurnRadians =
				LateralTurn.SignedRadians;
			OutMetrics.MinimumReadableDeflectionRadians = std::min(
				OutMetrics.MinimumReadableDeflectionRadians,
				Metrics.ActualDeflectionRadians
					* Metrics.LateralTurnAxisProjection);
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
			if (Metrics.LateralTurnAxisProjection
				< Contract.MinimumLateralTurnAxisProjection)
			{
				return RejectAssistMetric(
					"LateralTurnAxisProjectionBelowMinimum");
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
		OutMetrics.AlternatingLateralTurnCount = 0;
		for (std::size_t Index = 1;
			Index < OutMetrics.Assists.size();
			++Index)
		{
			if (OutMetrics.Assists[Index - 1].SignedLateralTurnRadians
					* OutMetrics.Assists[Index].SignedLateralTurnRadians
				< 0.0)
			{
				++OutMetrics.AlternatingLateralTurnCount;
			}
		}
		if (OutMetrics.AlternatingLateralTurnCount
			< Contract.MinimumAlternatingLateralTurnCount)
		{
			if (OutFailure != nullptr)
			{
				*OutFailure = "AlternatingLateralTurnCountBelowMinimum";
			}
			return false;
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

std::array<std::int8_t, 3>
ABTS::M11Search::CandidateSearch::BuildPreferredPassSidePattern(
	const CandidateSearchContract& Contract,
	const std::uint64_t GlobalWorkIndex)
{
	return SearchPrivate::MakePreferredPassSidePattern(
		Contract, GlobalWorkIndex);
}

bool ABTS::M11Search::CandidateSearch::ShouldRejectLowPowerResult(
	const M11Core::TrajectoryResult& Result,
	const bool bQualifiedAssist1)
{
	return bQualifiedAssist1 || Result.DidHitTarget();
}

ABTS::M11Search::PartialAlternationMetrics
ABTS::M11Search::CandidateSearch::MeasurePartialAlternation(
	const CandidateLayout& Layout,
	const CandidateSearchContract& Contract,
	const M11Core::TrajectoryResult& Result,
	const std::int32_t LastAssistIndex)
{
	return SearchPrivate::MeasurePartialAlternation(
		Layout, Contract, Result, LastAssistIndex);
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
	const bool bQualifiedLowPowerAssist1 =
		ResultPassesAssistPrefix(
			Working,
			Contract,
			LowPowerResult,
			1);
	if (CandidateSearch::ShouldRejectLowPowerResult(
			LowPowerResult,
			bQualifiedLowPowerAssist1))
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::LowPowerGateRejected,
			"LowPowerCompletedQualifiedAssist1PrefixOrHitTarget");
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

	if (!AnalyzeInputDomain(
			Working,
			Contract,
			OutCandidate.Metrics,
			OutCandidate.SolverInvocationCount,
			&Failure))
	{
		Reject(
			nullptr,
			&OutCandidate,
			EvaluationStatus::InputDomainDegenerate,
			Failure.empty()
				? "InputDomainPrefixSetDegenerate"
				: Failure.c_str());
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	double DeflectionScore = 0.0;
	for (const AssistMetrics& Assist : OutCandidate.Metrics.Assists)
	{
		const double DeflectionHeadroom = std::clamp(
			(Assist.ActualDeflectionRadians
				- Contract.MinimumDeflectionRadians)
				/ std::max(
					0.1,
					1.20 - Contract.MinimumDeflectionRadians),
			0.0,
			1.0);
		DeflectionScore +=
			0.65 * DeflectionHeadroom
			+ 0.35 * Assist.LateralTurnAxisProjection;
	}
	OutCandidate.Metrics.DeflectionReadabilityScore =
		DeflectionScore
		/ static_cast<double>(OutCandidate.Metrics.Assists.size());
	OutCandidate.Metrics.AlternationScore =
		static_cast<double>(
			OutCandidate.Metrics.AlternatingLateralTurnCount)
		/ static_cast<double>(
			OutCandidate.Metrics.Assists.size() - 1);
	const double FlightHeadroom = std::clamp(
		1.0
			- OutCandidate.Metrics.TotalFlightTimeSeconds
				/ Contract.MaximumTotalFlightTimeSeconds,
		0.0,
		1.0);
	const double CoastHeadroom = std::clamp(
		1.0
			- OutCandidate.Metrics.MaximumCoastSeconds
				/ Contract.MaximumCoastSeconds,
		0.0,
		1.0);
	OutCandidate.Metrics.PacingScore =
		0.5 * (FlightHeadroom + CoastHeadroom);
	OutCandidate.Metrics.SoftScore =
		30.0 * OutCandidate.Metrics.PrefixRetentionScore
		+ 20.0 * OutCandidate.Metrics.PrefixHullScore
		+ 25.0 * OutCandidate.Metrics.DeflectionReadabilityScore
		+ 15.0 * OutCandidate.Metrics.AlternationScore
		+ 10.0 * OutCandidate.Metrics.PacingScore;

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
	if (Left.Metrics.SoftScore != Right.Metrics.SoftScore)
	{
		return Left.Metrics.SoftScore > Right.Metrics.SoftScore;
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
