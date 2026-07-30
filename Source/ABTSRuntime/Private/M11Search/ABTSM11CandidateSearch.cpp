// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11CandidateSearch.h"

#include "M11Core/ABTSM11CoreSolver.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
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

	class ParticleHash final
	{
	public:
		void AddUInt64(const std::uint64_t Value)
		{
			for (std::int32_t ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				ValueHash ^= static_cast<std::uint8_t>(
					Value >> (ByteIndex * 8));
				ValueHash *= 1099511628211ull;
			}
		}

		void AddInt32(const std::int32_t Value)
		{
			AddUInt64(static_cast<std::uint64_t>(
				static_cast<std::uint32_t>(Value)));
		}

		void AddDouble(const double Value)
		{
			AddUInt64(std::bit_cast<std::uint64_t>(Value));
		}

		void AddBool(const bool Value)
		{
			AddInt32(Value ? 1 : 0);
		}

		[[nodiscard]] std::uint64_t Get() const
		{
			return ValueHash;
		}

	private:
		std::uint64_t ValueHash = 14695981039346656037ull;
	};

	std::uint64_t ComputeParticleContractHash(
		const ParticleBeamSearchContract& Contract)
	{
		ParticleHash Hash;
		Hash.AddUInt64(0x11b24001ull);
		Hash.AddInt32(Contract.ContractVersion);
		Hash.AddInt32(Contract.AlgorithmVersion);
		Hash.AddUInt64(Contract.ConstructionSeed);
		Hash.AddUInt64(Contract.ExplorationSeed);
		Hash.AddUInt64(Contract.HoldoutSeed);
		Hash.AddUInt64(ComputeCandidateSearchContractHash(
			Contract.EvaluationContract));
		Hash.AddInt32(Contract.RootParameterCount);
		Hash.AddInt32(Contract.ExplorationSampleCount);
		Hash.AddInt32(Contract.GeometryTimeSampleCount);
		Hash.AddInt32(Contract.GeometryRadiusSampleCount);
		Hash.AddInt32(Contract.GeometryImpactSampleCount);
		Hash.AddInt32(Contract.GeometryRadialSampleCount);
		Hash.AddInt32(Contract.GeometryMomentumSampleCount);
		Hash.AddInt32(Contract.NominalProposalBudget);
		Hash.AddInt32(Contract.CoarseProposalBudget);
		Hash.AddInt32(Contract.RefinementProposalBudget);
		Hash.AddInt32(Contract.CoarseParticleLimit);
		Hash.AddInt32(Contract.BeamWidth);
		Hash.AddInt32(Contract.HoldoutSampleCount);
		Hash.AddInt32(Contract.MaximumFinalAuditCandidates);
		Hash.AddInt32(Contract.RobustGuardSurvivorCount);
		Hash.AddInt32(Contract.TargetRefinementTimeSampleCount);
		Hash.AddDouble(Contract.TargetPrefixRetentionRatio);
		Hash.AddDouble(Contract.ExplorationMinimumRetentionRatio);
		Hash.AddDouble(Contract.ExplorationMaximumRetentionRatio);
		Hash.AddDouble(Contract.PreferredMinimumRetentionRatio);
		Hash.AddDouble(Contract.PreferredMaximumRetentionRatio);
		Hash.AddDouble(Contract.MinimumBeamDiversityDistanceCM);
		Hash.AddDouble(Contract.FinalTargetTurnGuardRadians);
		Hash.AddDouble(
			Contract.TargetRefinementMaximumCoastSeconds);
		Hash.AddDouble(
			Contract.ConstructionInterEncounterCoastMinimumSeconds);
		Hash.AddDouble(
			Contract.ConstructionInterEncounterCoastMaximumSeconds);
		Hash.AddDouble(Contract.IdealMinimumDeflectionRadians);
		Hash.AddDouble(Contract.IdealMaximumDeflectionRadians);
		Hash.AddDouble(Contract.IdealMinimumAxisProjection);
		Hash.AddDouble(Contract.IdealMinimumInfluenceSeconds);
		Hash.AddDouble(Contract.IdealMaximumInfluenceSeconds);
		Hash.AddDouble(Contract.IdealMaximumCoastSeconds);
		Hash.AddDouble(Contract.IdealMaximumFlightSeconds);
		return Hash.Get();
	}

	template <typename FunctionType>
	void ParallelFor(
		const std::size_t Count,
		const std::uint32_t RequestedThreads,
		FunctionType&& Function)
	{
		if (Count == 0)
		{
			return;
		}
		const std::size_t ThreadCount = std::min<std::size_t>(
			std::max<std::uint32_t>(1u, RequestedThreads),
			Count);
		if (ThreadCount == 1)
		{
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				Function(Index);
			}
			return;
		}
		std::atomic<std::size_t> Cursor{0};
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
					const std::size_t Index = Cursor.fetch_add(1);
					if (Index >= Count)
					{
						break;
					}
					Function(Index);
				}
			});
		}
		for (std::thread& Thread : Threads)
		{
			Thread.join();
		}
	}

	struct BeamParticle
	{
		LaunchInput Input;
		bool CountsTowardRatio = true;
		TrajectoryResult Result;
	};

	struct BeamNode
	{
		std::uint64_t RootIndex = 0;
		WorkParameters Parameters;
		CandidateLayout Layout;
		TrajectoryResult NominalResult;
		std::shared_ptr<const std::vector<BeamParticle>> Particles;
		std::array<ParticleBeamStageMetrics,
			GravityScenario::AssistCount> StageMetrics{};
		double ConstructionScore = 0.0;
		std::uint64_t ConstructionHash = 0;
	};

	struct BeamProposal
	{
		std::size_t ParentIndex = 0;
		std::uint64_t Ordinal = 0;
		CandidateLayout Layout;
		TrajectoryResult NominalResult;
		double PredictedRetentionRatio = 0.0;
		double GeometryScore = 0.0;
		double EvaluationScore = -std::numeric_limits<double>::max();
		ParticleBeamStageMetrics StageMetrics;
		std::int32_t PartialAlternationCount = 0;
		std::int32_t SolverInvocationCount = 0;
		std::int32_t NominalFailureCode = 0;
		bool NominalAccepted = false;
		bool ParticleAccepted = false;
	};

	struct HoldoutEvaluation
	{
		BeamNode Node;
		CandidateLayout Layout;
		TrajectoryResult NominalResult;
		std::array<InputSetMetrics, 4> InputSets{};
		std::int32_t SolverInvocationCount = 0;
		double Score = 0.0;
		bool Accepted = false;
		std::string Rejection;
	};

	std::int32_t CountRatioParticles(
		const std::vector<BeamParticle>& Particles)
	{
		return static_cast<std::int32_t>(std::count_if(
			Particles.begin(),
			Particles.end(),
			[](const BeamParticle& Particle)
			{
				return Particle.CountsTowardRatio;
			}));
	}

	void ParkConstructionBodies(CandidateLayout& Layout)
	{
		const std::array<Vec3d, 3> ParkingCenters{
			Vec3d(205000.0, 95000.0, 90000.0),
			Vec3d(-185000.0, 125000.0, 105000.0),
			Vec3d(125000.0, -195000.0, 120000.0)};
		for (std::int32_t AssistIndex = 1;
			AssistIndex <= GravityScenario::AssistCount;
			++AssistIndex)
		{
			GravityBodySpec& Body =
				Layout.Scenario.Bodies[
					static_cast<std::size_t>(AssistIndex)];
			Body.CenterCM = ParkingCenters[
				static_cast<std::size_t>(AssistIndex - 1)];
			Body.AllowedPassSideValue = AllowedPassSide::Any;
			Body.BPlaneTargetTCM = 0.0;
			Body.BPlaneTargetRCM = 0.0;
		}
		TargetSpec& Target = Layout.Scenario.Target;
		Target.CenterCM = Vec3d(0.0, 0.0, 270000.0);
		Target.GeometricContactCenterCM = Target.CenterCM;
		Target.HitRadiusCM = 100.0;
		Target.GeometricContactRadiusCM = 0.0;
		Target.UseSeparateGeometricContactCenter = true;
		Target.RequiredQualifiedAssistCount = 3;
		Target.MinimumQualifyingCorridorQuality = 0.0;
		Target.MinimumQualifyingEnergyGainCM2PerSec2 = 0.0;
		Target.RequireAllowedPassSide = true;
	}

	std::vector<LaunchInput> MakeParticleInputs(
		const CandidateLayout& Layout,
		const std::uint64_t Seed,
		const std::int32_t SampleCount,
		const bool IncludeNominal)
	{
		std::vector<LaunchInput> Inputs;
		Inputs.reserve(static_cast<std::size_t>(
			SampleCount + (IncludeNominal ? 1 : 0)));
		if (IncludeNominal)
		{
			Inputs.push_back(Layout.NominalInput);
		}
		const std::uint64_t Offset = Seed % 1000003ull;
		for (std::int32_t Index = 0; Index < SampleCount; ++Index)
		{
			const std::uint64_t SampleIndex =
				Offset + static_cast<std::uint64_t>(Index) + 1ull;
			Inputs.push_back(LaunchInput{
				M11Core::Lerp(
					Layout.Launch.MinimumYawDegrees,
					Layout.Launch.MaximumYawDegrees,
					Halton(SampleIndex, 2)),
				M11Core::Lerp(
					Layout.Launch.MinimumPitchDegrees,
					Layout.Launch.MaximumPitchDegrees,
					Halton(SampleIndex, 3)),
				Layout.NominalInput.Power});
		}
		return Inputs;
	}

	double PreferredRetentionScore(
		const ParticleBeamSearchContract& Contract,
		const double Retention)
	{
		if (Retention < Contract.ExplorationMinimumRetentionRatio
			|| Retention > Contract.ExplorationMaximumRetentionRatio)
		{
			return 0.0;
		}
		if (Retention >= Contract.PreferredMinimumRetentionRatio
			&& Retention <= Contract.PreferredMaximumRetentionRatio)
		{
			const double HalfWidth = std::max(
				0.01,
				0.5 * (Contract.PreferredMaximumRetentionRatio
					- Contract.PreferredMinimumRetentionRatio));
			return std::clamp(
				1.0
					- std::abs(
						Retention
						- Contract.TargetPrefixRetentionRatio)
						/ HalfWidth,
				0.65,
				1.0);
		}
		if (Retention < Contract.PreferredMinimumRetentionRatio)
		{
			return (Retention
					- Contract.ExplorationMinimumRetentionRatio)
				/ (Contract.PreferredMinimumRetentionRatio
					- Contract.ExplorationMinimumRetentionRatio);
		}
		return (Contract.ExplorationMaximumRetentionRatio - Retention)
			/ (Contract.ExplorationMaximumRetentionRatio
				- Contract.PreferredMaximumRetentionRatio);
	}

	double BandScore(
		const double Value,
		const double Minimum,
		const double Maximum)
	{
		if (Value >= Minimum && Value <= Maximum)
		{
			return 1.0;
		}
		const double Scale = std::max(0.01, Maximum - Minimum);
		return std::clamp(
			1.0 - std::min(
				std::abs(Value - Minimum),
				std::abs(Value - Maximum)) / Scale,
			0.0,
			1.0);
	}

	void GenerateGeometryProposals(
		const ParticleBeamSearchContract& BeamContract,
		const std::vector<BeamNode>& Parents,
		const std::int32_t AssistIndex,
		std::vector<BeamProposal>& OutProposals)
	{
		const CandidateSearchContract& Contract =
			BeamContract.EvaluationContract;
		std::uint64_t Ordinal = 0;
		for (std::size_t ParentIndex = 0;
			ParentIndex < Parents.size();
			++ParentIndex)
		{
			const BeamNode& Parent = Parents[ParentIndex];
			const TrajectoryEvent* PreviousExit = AssistIndex > 1
				? Parent.NominalResult.FindAssistEvent(
					TrajectoryEventType::AssistExit,
					AssistIndex - 1)
				: nullptr;
			const double CenterTime = PreviousExit != nullptr
				? PreviousExit->TimeSeconds
					+ Parent.Parameters.InterEncounterCoastSeconds[
						static_cast<std::size_t>(AssistIndex - 2)]
				: Parent.Parameters.FirstEncounterSeconds;
			const std::int32_t ParentRatioCount =
				CountRatioParticles(*Parent.Particles);
			if (ParentRatioCount <= 0)
			{
				continue;
			}

			for (std::int32_t TimeIndex = 0;
				TimeIndex < BeamContract.GeometryTimeSampleCount;
				++TimeIndex)
			{
				const double TimeAlpha =
					BeamContract.GeometryTimeSampleCount == 1
					? 0.0
					: static_cast<double>(TimeIndex)
						/ static_cast<double>(
							BeamContract.GeometryTimeSampleCount - 1)
						- 0.5;
				const double SampleTime =
					CenterTime + TimeAlpha * 3.0;
				const TrajectoryPoint* NominalPoint =
					FindPointAtOrAfter(
						Parent.NominalResult,
						SampleTime);
				if (NominalPoint == nullptr)
				{
					continue;
				}
				const GravityBodySpec& SeedBody =
					Parent.Layout.Scenario.GetAssist(AssistIndex);
				Vec3d T;
				Vec3d R;
				if (!BuildImpactBasis(
					SeedBody, *NominalPoint, T, R))
				{
					continue;
				}
				const std::size_t AssistArrayIndex =
					static_cast<std::size_t>(AssistIndex - 1);
				const double PreferredSign =
					static_cast<double>(
						Parent.Parameters.PreferredPassSideSigns[
							AssistArrayIndex]);

				for (std::int32_t RadiusIndex = 0;
					RadiusIndex
						< BeamContract.GeometryRadiusSampleCount;
					++RadiusIndex)
				{
					const double RadiusAlpha =
						BeamContract.GeometryRadiusSampleCount == 1
						? 0.0
						: static_cast<double>(RadiusIndex)
							/ static_cast<double>(
								BeamContract.GeometryRadiusSampleCount - 1)
							- 0.5;
					const double RadiusCM = std::clamp(
						Parent.Parameters.InfluenceRadiusCM[
							AssistArrayIndex]
							* (1.0 + RadiusAlpha * 0.50),
						Contract.MinimumInfluenceRadiusCM,
						Contract.MaximumInfluenceRadiusCM);

					for (std::int32_t ImpactIndex = 0;
						ImpactIndex
							< BeamContract.GeometryImpactSampleCount;
						++ImpactIndex)
					{
						const double ImpactAlpha =
							BeamContract.GeometryImpactSampleCount == 1
							? 0.0
							: static_cast<double>(ImpactIndex)
								/ static_cast<double>(
									BeamContract
										.GeometryImpactSampleCount - 1)
								- 0.5;
						const double ImpactFraction = std::clamp(
							Parent.Parameters.ImpactFraction[
								AssistArrayIndex]
								+ ImpactAlpha * 0.36,
							0.24,
							0.70);

						for (std::int32_t RadialIndex = 0;
							RadialIndex
								< BeamContract.GeometryRadialSampleCount;
							++RadialIndex)
						{
							const double RadialAlpha =
								BeamContract.GeometryRadialSampleCount == 1
								? 0.0
								: static_cast<double>(RadialIndex)
									/ static_cast<double>(
										BeamContract
											.GeometryRadialSampleCount - 1)
									- 0.5;
							const double RadialFraction = std::clamp(
								Parent.Parameters.RadialFraction[
									AssistArrayIndex]
									+ RadialAlpha * 0.50,
								-0.50,
								0.50);
							const Vec3d OffsetCM =
								T * (PreferredSign
									* ImpactFraction * RadiusCM)
								+ R * (RadialFraction * RadiusCM);

							for (std::int32_t MomentumIndex = 0;
								MomentumIndex
									< BeamContract
										.GeometryMomentumSampleCount;
								++MomentumIndex)
							{
								BeamProposal Proposal;
								Proposal.ParentIndex = ParentIndex;
								Proposal.Ordinal = ++Ordinal;
								Proposal.Layout = Parent.Layout;
								GravityBodySpec& Body =
									Proposal.Layout.Scenario.Bodies[
										static_cast<std::size_t>(
											AssistIndex)];
								Body.CenterCM =
									NominalPoint->PositionCM + OffsetCM;
								Body.InfluenceRadiusCM = RadiusCM;
								Body.InfluenceBlendWidthCM =
									RadiusCM * 0.10;
								Body.AssistReferenceRadiusCM =
									RadiusCM
										- Body.InfluenceBlendWidthCM;
								Body.BPlaneSigmaTCM = RadiusCM * 0.46;
								Body.BPlaneSigmaRCM = RadiusCM * 0.46;
								Body.AllowedPassSideValue =
									AllowedPassSide::Any;
								Body.BPlaneTargetTCM = 0.0;
								Body.BPlaneTargetRCM = 0.0;

								const double MomentumAlpha =
									BeamContract
										.GeometryMomentumSampleCount == 1
									? 0.0
									: static_cast<double>(MomentumIndex)
										/ static_cast<double>(
											BeamContract
												.GeometryMomentumSampleCount
												- 1)
										- 0.5;
								const std::array<double, 3>
									MomentumFanWidthsRadians{
										1.20, 1.60, 2.20};
								const double MomentumAngle =
									MomentumAlpha
										* MomentumFanWidthsRadians[
											AssistArrayIndex];
								const Vec3d OffsetDirection =
									OffsetCM.GetSafeNormal();
								Vec3d FanAxis = Vec3d::CrossProduct(
									NominalPoint->VelocityCMPerSec
										.GetSafeNormal(),
									OffsetDirection).GetSafeNormal();
								if (FanAxis.IsNearlyZero())
								{
									FanAxis = R;
								}
								Body.VirtualOrbitalVelocityCMPerSec =
									(OffsetDirection
										* std::cos(MomentumAngle)
										+ FanAxis
											* std::sin(MomentumAngle))
										.GetSafeNormal()
									* Parent.Parameters
										.VirtualSpeedCMPerSec[
											AssistArrayIndex];
								if (!GeometryIsLegal(
									Proposal.Layout,
									Contract,
									AssistIndex))
								{
									continue;
								}

								std::int32_t PredictedMembers = 0;
								for (const BeamParticle& Particle :
									*Parent.Particles)
								{
									if (!Particle.CountsTowardRatio)
									{
										continue;
									}
									const TrajectoryPoint* Point =
										FindPointAtOrAfter(
											Particle.Result,
											SampleTime);
									if (Point != nullptr
										&& (Point->PositionCM
											- Body.CenterCM).Length()
											<= Body.InfluenceRadiusCM)
									{
										++PredictedMembers;
									}
								}
								Proposal.PredictedRetentionRatio =
									Ratio(
										PredictedMembers,
										ParentRatioCount);
								// Influence-shell intersection is a broad
								// zero-solve predictor. Only a fraction of
								// those crossings later satisfy B-plane,
								// duration, energy, and readability gates,
								// so deliberately target a wider geometric
								// capture than the desired qualified 0.25.
								const std::array<double, 3>
									GeometryRetentionMultipliers{
										2.5, 2.5, 1.6};
								const double GeometryRetentionTarget =
									std::clamp(
										BeamContract
											.TargetPrefixRetentionRatio
											* GeometryRetentionMultipliers[
												AssistArrayIndex],
										0.35,
										0.75);
								const double RetentionScore =
									std::clamp(
										1.0
											- std::abs(
												Proposal
													.PredictedRetentionRatio
												- GeometryRetentionTarget)
												/ 0.35,
										0.0,
										1.0);
								const double LayoutTurn =
									AssistIndex >= 2
									? ComputePartialLayoutTurn(
										Proposal.Layout,
										AssistIndex)
									: Pi;
								const double LayoutScore = std::clamp(
									LayoutTurn
										/ std::max(
											0.01,
											Contract.MinimumLayoutTurnRadians
												* 2.0),
									0.0,
									1.0);
								Proposal.GeometryScore =
									0.85 * RetentionScore
									+ 0.15 * LayoutScore;
								OutProposals.push_back(
									std::move(Proposal));
							}
						}
					}
				}
			}
		}
	}

	bool GeometryProposalRanksBefore(
		const BeamProposal& Left,
		const BeamProposal& Right)
	{
		if (Left.GeometryScore != Right.GeometryScore)
		{
			return Left.GeometryScore > Right.GeometryScore;
		}
		if (Left.PredictedRetentionRatio
			!= Right.PredictedRetentionRatio)
		{
			return std::abs(
					Left.PredictedRetentionRatio - 0.25)
				< std::abs(
					Right.PredictedRetentionRatio - 0.25);
		}
		return Left.Ordinal < Right.Ordinal;
	}

	void SelectDiverseGeometryProposals(
		std::vector<BeamProposal>& Proposals,
		const std::int32_t AssistIndex,
		const std::size_t Limit,
		const double MinimumDistanceCM)
	{
		std::sort(
			Proposals.begin(),
			Proposals.end(),
			GeometryProposalRanksBefore);
		std::vector<BeamProposal> Selected;
		Selected.reserve(std::min(Limit, Proposals.size()));
		for (BeamProposal& Proposal : Proposals)
		{
			const Vec3d& Center =
				Proposal.Layout.Scenario.GetAssist(
					AssistIndex).CenterCM;
			const bool Diverse = std::all_of(
				Selected.begin(),
				Selected.end(),
				[&](const BeamProposal& Existing)
				{
					const Vec3d& ExistingCenter =
						Existing.Layout.Scenario.GetAssist(
							AssistIndex).CenterCM;
					return (Center - ExistingCenter).Length()
						>= MinimumDistanceCM
						|| Proposal.ParentIndex
							!= Existing.ParentIndex;
				});
			if (Diverse)
			{
				Selected.push_back(std::move(Proposal));
				if (Selected.size() >= Limit)
				{
					break;
				}
			}
		}
		Proposals = std::move(Selected);
	}

	bool EvaluateNominalProposal(
		const ParticleBeamSearchContract& BeamContract,
		const std::vector<BeamNode>& Parents,
		const std::int32_t AssistIndex,
		BeamProposal& Proposal)
	{
		const CandidateSearchContract& Contract =
			BeamContract.EvaluationContract;
		const BeamNode& Parent = Parents[Proposal.ParentIndex];
		const std::uint8_t AssistMask =
			static_cast<std::uint8_t>((1u << AssistIndex) - 1u);
		RefreshIdentity(Proposal.Layout, Contract);
		TrajectoryRequest Request;
		TrajectoryResult Result;
		if (!BuildAndSolve(
				Proposal.Layout,
				Proposal.Layout.NominalInput,
				AssistMask,
				Request,
				Result,
				Proposal.SolverInvocationCount))
		{
			Proposal.NominalFailureCode = 1;
			return false;
		}
		const TrajectoryEvent* Enter = Result.FindAssistEvent(
			TrajectoryEventType::AssistEnter, AssistIndex);
		const TrajectoryEvent* Exit = Result.FindAssistEvent(
			TrajectoryEventType::AssistExit, AssistIndex);
		if (Enter == nullptr || Exit == nullptr
			|| Result.CompletedAssistCount < AssistIndex
			|| Exit->AppliedEnergyChangeCM2PerSec2 <= 0.0)
		{
			Proposal.NominalFailureCode = 2;
			return false;
		}
		GravityBodySpec& Body =
			Proposal.Layout.Scenario.Bodies[
				static_cast<std::size_t>(AssistIndex)];
		Body.BPlaneTargetTCM = Exit->BPlaneTCM;
		Body.BPlaneTargetRCM = Exit->BPlaneRCM;
		const std::array<double, GravityScenario::AssistCount>
			StageCorridorSigmaFractions{0.42, 0.48, 0.55};
		const double CorridorSigmaFraction =
			StageCorridorSigmaFractions[
				static_cast<std::size_t>(AssistIndex - 1)];
		Body.BPlaneSigmaTCM =
			Body.InfluenceRadiusCM * CorridorSigmaFraction;
		Body.BPlaneSigmaRCM =
			Body.InfluenceRadiusCM * CorridorSigmaFraction;
		Body.AllowedPassSideValue = InferAllowedSide(*Exit);
		RefreshIdentity(Proposal.Layout, Contract);
		if (!BuildAndSolve(
				Proposal.Layout,
				Proposal.Layout.NominalInput,
				AssistMask,
				Request,
				Result,
				Proposal.SolverInvocationCount)
			|| !ResultPassesAssistPrefix(
				Proposal.Layout,
				Contract,
				Result,
				AssistIndex))
		{
			Proposal.NominalFailureCode = 3;
			return false;
		}
		Enter = Result.FindAssistEvent(
			TrajectoryEventType::AssistEnter, AssistIndex);
		Exit = Result.FindAssistEvent(
			TrajectoryEventType::AssistExit, AssistIndex);
		if (Enter == nullptr || Exit == nullptr)
		{
			Proposal.NominalFailureCode = 4;
			return false;
		}
		if (AssistIndex == 1)
		{
			LaunchInput LowPower = Proposal.Layout.NominalInput;
			LowPower.Power = Contract.LowPowerProbe;
			TrajectoryResult LowPowerResult;
			if (!BuildAndSolve(
					Proposal.Layout,
					LowPower,
					AssistMask,
					Request,
					LowPowerResult,
					Proposal.SolverInvocationCount)
				|| CandidateSearch::ShouldRejectLowPowerResult(
					LowPowerResult,
					ResultPassesAssistPrefix(
						Proposal.Layout,
						Contract,
						LowPowerResult,
						1)))
			{
				Proposal.NominalFailureCode = 5;
				return false;
			}
		}
		const PartialAlternationMetrics Alternation =
			MeasurePartialAlternation(
				Proposal.Layout,
				Contract,
				Result,
				AssistIndex);
		if (AssistIndex >= 2
			&& Alternation.PartialAlternationCount < 1)
		{
			Proposal.NominalFailureCode = 6;
			return false;
		}
		if (AssistIndex >= 2
			&& ComputePartialLayoutTurn(
				Proposal.Layout,
				AssistIndex)
				< Contract.MinimumLayoutTurnRadians)
		{
			Proposal.NominalFailureCode = 7;
			return false;
		}
		if (AssistIndex == GravityScenario::AssistCount)
		{
			const Vec3d IncomingLayoutDirection =
				Body.CenterCM
					- Proposal.Layout.Scenario.GetAssist(
						AssistIndex - 1).CenterCM;
			if (AngleBetween(
					IncomingLayoutDirection,
					Exit->VelocityCMPerSec)
				< Contract.MinimumLayoutTurnRadians
					+ BeamContract.FinalTargetTurnGuardRadians)
			{
				Proposal.NominalFailureCode = 8;
				return false;
			}
		}

		const double Deflection = AngleBetween(
			Enter->VelocityCMPerSec,
			Exit->VelocityCMPerSec);
		const LateralTurnMeasurement LateralTurn =
			MeasureLateralTurn(
				Enter->VelocityCMPerSec,
				Exit->VelocityCMPerSec,
				ComputePresentationNormal(Proposal.Layout),
				Contract.MinimumLateralTurnAxisProjection);
		const TrajectoryEvent* PreviousExit = AssistIndex > 1
			? Result.FindAssistEvent(
				TrajectoryEventType::AssistExit,
				AssistIndex - 1)
			: nullptr;
		const double CoastBeforeEnter =
			Enter->TimeSeconds
				- (PreviousExit != nullptr
					? PreviousExit->TimeSeconds
					: 0.0);
		const double InfluenceDuration =
			Exit->TimeSeconds - Enter->TimeSeconds;
		Proposal.StageMetrics.AssistIndex = AssistIndex;
		Proposal.StageMetrics.ActualDeflectionRadians = Deflection;
		Proposal.StageMetrics.SignedLateralTurnRadians =
			Alternation.SignedLateralTurnRadians[
				static_cast<std::size_t>(AssistIndex - 1)];
		Proposal.StageMetrics.InfluenceDurationSeconds =
			InfluenceDuration;
		Proposal.StageMetrics.CoastBeforeEnterSeconds =
			CoastBeforeEnter;
		Proposal.PartialAlternationCount =
			Alternation.PartialAlternationCount;
		const double DeflectionScore = BandScore(
			Deflection,
			BeamContract.IdealMinimumDeflectionRadians,
			BeamContract.IdealMaximumDeflectionRadians);
		const double AxisScore = std::clamp(
			LateralTurn.AxisProjection
				/ BeamContract.IdealMinimumAxisProjection,
			0.0,
			1.0);
		const double InfluenceScore = BandScore(
			InfluenceDuration,
			BeamContract.IdealMinimumInfluenceSeconds,
			BeamContract.IdealMaximumInfluenceSeconds);
		const double CoastScore = std::clamp(
			1.0 - CoastBeforeEnter
				/ BeamContract.IdealMaximumCoastSeconds,
			0.0,
			1.0);
		Proposal.EvaluationScore =
			0.35 * Proposal.GeometryScore
			+ 0.30 * DeflectionScore
			+ 0.15 * AxisScore
			+ 0.10 * InfluenceScore
			+ 0.10 * CoastScore;
		Proposal.NominalResult = std::move(Result);
		Proposal.NominalAccepted = true;
		return true;
	}

	std::vector<std::size_t> SelectParticleIndices(
		const std::vector<BeamParticle>& Particles,
		const std::int32_t CountedLimit)
	{
		std::vector<std::size_t> Result;
		std::vector<std::size_t> Counted;
		for (std::size_t Index = 0; Index < Particles.size(); ++Index)
		{
			if (Particles[Index].CountsTowardRatio)
			{
				Counted.push_back(Index);
			}
			else
			{
				Result.push_back(Index);
			}
		}
		const std::size_t Requested = std::min<std::size_t>(
			Counted.size(),
			static_cast<std::size_t>(
				std::max(1, CountedLimit)));
		for (std::size_t PickIndex = 0;
			PickIndex < Requested;
			++PickIndex)
		{
			const std::size_t SourceIndex = std::min(
				Counted.size() - 1,
				(PickIndex * Counted.size()) / Requested);
			Result.push_back(Counted[SourceIndex]);
		}
		return Result;
	}

	bool EvaluateProposalParticleSet(
		const ParticleBeamSearchContract& BeamContract,
		const std::vector<BeamNode>& Parents,
		const std::int32_t AssistIndex,
		const bool RequireExplorationBand,
		const std::int32_t CountedLimit,
		BeamProposal& Proposal)
	{
		const CandidateSearchContract& Contract =
			BeamContract.EvaluationContract;
		const BeamNode& Parent = Parents[Proposal.ParentIndex];
		const std::vector<std::size_t> ParticleIndices =
			SelectParticleIndices(
				*Parent.Particles,
				CountedLimit);
		const std::uint8_t AssistMask =
			static_cast<std::uint8_t>((1u << AssistIndex) - 1u);
		std::int32_t ParentCount = 0;
		std::int32_t MemberCount = 0;
		bool NominalSurvived = false;
		std::vector<YawPitchPoint> Evidence;
		for (const std::size_t ParticleIndex : ParticleIndices)
		{
			const BeamParticle& Particle =
				(*Parent.Particles)[ParticleIndex];
			TrajectoryRequest Request;
			TrajectoryResult Result;
			if (!BuildAndSolve(
				Proposal.Layout,
				Particle.Input,
				AssistMask,
				Request,
				Result,
				Proposal.SolverInvocationCount))
			{
				return false;
			}
			if (Particle.CountsTowardRatio)
			{
				++ParentCount;
			}
			const bool IsMember = ResultPassesAssistPrefix(
				Proposal.Layout,
				Contract,
				Result,
				AssistIndex);
			if (!Particle.CountsTowardRatio)
			{
				NominalSurvived |= IsMember;
			}
			else if (IsMember)
			{
				++MemberCount;
				Evidence.push_back(YawPitchPoint{
					Particle.Input.YawDegrees,
					Particle.Input.PitchDegrees});
			}
		}
		if (!NominalSurvived || ParentCount < 4)
		{
			return false;
		}
		const double Retention = Ratio(MemberCount, ParentCount);
		InputSetMetrics Hull;
		PopulateHullMetrics(
			Proposal.Layout,
			Contract,
			Evidence,
			Hull);
		Proposal.StageMetrics.ParentParticleCount = ParentCount;
		Proposal.StageMetrics.MemberParticleCount = MemberCount;
		Proposal.StageMetrics.RetentionRatio = Retention;
		Proposal.StageMetrics.HullAreaSquareDegrees =
			Hull.ScreenAimHullAreaSquareDegrees;
		Proposal.StageMetrics.HullYawSpanDegrees =
			Hull.ScreenAimHullYawSpanDegrees;
		Proposal.StageMetrics.HullPitchSpanDegrees =
			Hull.ScreenAimHullPitchSpanDegrees;
		Proposal.StageMetrics.HullCompactness =
			Hull.ScreenAimHullCompactness;
		if (RequireExplorationBand
			&& (Retention
					< BeamContract.ExplorationMinimumRetentionRatio
				|| Retention
					> BeamContract.ExplorationMaximumRetentionRatio
				|| MemberCount < 2))
		{
			return false;
		}
		const double RetentionScore =
			PreferredRetentionScore(BeamContract, Retention);
		const double HullScore =
			0.35 * std::clamp(
				Hull.ScreenAimHullAreaSquareDegrees / 0.05,
				0.0,
				1.0)
			+ 0.25 * std::clamp(
				Hull.ScreenAimHullYawSpanDegrees / 0.25,
				0.0,
				1.0)
			+ 0.25 * std::clamp(
				Hull.ScreenAimHullPitchSpanDegrees / 0.25,
				0.0,
				1.0)
			+ 0.15 * Hull.ScreenAimHullCompactness;
		const double AlternationScore = AssistIndex == 1
			? 1.0
			: std::clamp(
				static_cast<double>(
					Proposal.PartialAlternationCount)
					/ static_cast<double>(AssistIndex - 1),
				0.0,
				1.0);
		double RobustnessScore = 1.0;
		if (RequireExplorationBand
			&& AssistIndex == GravityScenario::AssistCount)
		{
			std::int32_t RobustPrefixSurvivors = 0;
			for (const LaunchInput& RobustInput :
				MakeRobustInputs(Proposal.Layout, Contract))
			{
				TrajectoryRequest Request;
				TrajectoryResult Result;
				if (BuildAndSolve(
						Proposal.Layout,
						RobustInput,
						AssistMask,
						Request,
						Result,
						Proposal.SolverInvocationCount)
					&& ResultPassesAssistPrefix(
						Proposal.Layout,
						Contract,
						Result,
						AssistIndex))
				{
					++RobustPrefixSurvivors;
				}
			}
			Proposal.StageMetrics.RobustPrefixSurvivorCount =
				RobustPrefixSurvivors;
			const std::int32_t RequiredRobustPrefixSurvivors =
				Contract.MinimumRobustSurvivorCount
					+ BeamContract.RobustGuardSurvivorCount;
			if (RobustPrefixSurvivors
				< RequiredRobustPrefixSurvivors)
			{
				return false;
			}
			RobustnessScore = std::clamp(
				static_cast<double>(RobustPrefixSurvivors)
					/ static_cast<double>(
						RequiredRobustPrefixSurvivors + 1),
				0.0,
				1.0);
		}
		Proposal.StageMetrics.StageScore =
			100.0 * (
				0.50 * RetentionScore
				+ 0.15 * HullScore
				+ 0.15 * std::clamp(
					Proposal.EvaluationScore,
					0.0,
					1.0)
				+ 0.10 * AlternationScore
				+ 0.10 * RobustnessScore);
		Proposal.EvaluationScore =
			Parent.ConstructionScore
			+ Proposal.StageMetrics.StageScore;
		Proposal.ParticleAccepted = true;
		return true;
	}

	bool EvaluatedProposalRanksBefore(
		const BeamProposal& Left,
		const BeamProposal& Right)
	{
		if (Left.ParticleAccepted != Right.ParticleAccepted)
		{
			return Left.ParticleAccepted;
		}
		if (Left.EvaluationScore != Right.EvaluationScore)
		{
			return Left.EvaluationScore > Right.EvaluationScore;
		}
		if (Left.PartialAlternationCount
			!= Right.PartialAlternationCount)
		{
			return Left.PartialAlternationCount
				> Right.PartialAlternationCount;
		}
		if (Left.StageMetrics.ActualDeflectionRadians
			!= Right.StageMetrics.ActualDeflectionRadians)
		{
			return Left.StageMetrics.ActualDeflectionRadians
				> Right.StageMetrics.ActualDeflectionRadians;
		}
		return Left.Ordinal < Right.Ordinal;
	}

	bool PopulateChildParticles(
		const ParticleBeamSearchContract& BeamContract,
		const std::vector<BeamNode>& Parents,
		const std::int32_t AssistIndex,
		BeamProposal& Proposal,
		std::shared_ptr<const std::vector<BeamParticle>>& OutParticles)
	{
		const BeamNode& Parent = Parents[Proposal.ParentIndex];
		const std::uint8_t AssistMask =
			static_cast<std::uint8_t>((1u << AssistIndex) - 1u);
		std::vector<BeamParticle> Members;
		Members.reserve(Parent.Particles->size());
		bool NominalSurvived = false;
		for (const BeamParticle& Particle : *Parent.Particles)
		{
			TrajectoryRequest Request;
			TrajectoryResult Result;
			if (!BuildAndSolve(
				Proposal.Layout,
				Particle.Input,
				AssistMask,
				Request,
				Result,
				Proposal.SolverInvocationCount))
			{
				return false;
			}
			if (!ResultPassesAssistPrefix(
				Proposal.Layout,
				BeamContract.EvaluationContract,
				Result,
				AssistIndex))
			{
				continue;
			}
			BeamParticle Member;
			Member.Input = Particle.Input;
			Member.CountsTowardRatio =
				Particle.CountsTowardRatio;
			Member.Result = std::move(Result);
			NominalSurvived |= !Member.CountsTowardRatio;
			Members.push_back(std::move(Member));
		}
		const std::int32_t CountedMembers =
			CountRatioParticles(Members);
		if (!NominalSurvived || CountedMembers < 2)
		{
			return false;
		}
		OutParticles =
			std::make_shared<const std::vector<BeamParticle>>(
				std::move(Members));
		return true;
	}

	std::uint64_t ComputeNodeConstructionHash(
		const ParticleBeamSearchContract& BeamContract,
		const BeamNode& Node,
		const std::int32_t CompletedAssistCount)
	{
		ParticleHash Hash;
		Hash.AddUInt64(ComputeParticleContractHash(BeamContract));
		Hash.AddUInt64(Node.RootIndex);
		Hash.AddUInt64(ComputeCandidateSourceHash(
			Node.Layout,
			BeamContract.EvaluationContract));
		Hash.AddDouble(Node.ConstructionScore);
		for (std::int32_t Index = 0;
			Index < CompletedAssistCount;
			++Index)
		{
			const ParticleBeamStageMetrics& Stage =
				Node.StageMetrics[static_cast<std::size_t>(Index)];
			Hash.AddInt32(Stage.AssistIndex);
			Hash.AddInt32(Stage.ParentParticleCount);
			Hash.AddInt32(Stage.MemberParticleCount);
			Hash.AddDouble(Stage.RetentionRatio);
			Hash.AddDouble(Stage.HullAreaSquareDegrees);
			Hash.AddDouble(Stage.ActualDeflectionRadians);
			Hash.AddDouble(Stage.SignedLateralTurnRadians);
			Hash.AddDouble(Stage.InfluenceDurationSeconds);
			Hash.AddDouble(Stage.CoastBeforeEnterSeconds);
			Hash.AddInt32(Stage.RobustPrefixSurvivorCount);
			Hash.AddDouble(Stage.StageScore);
		}
		return Hash.Get();
	}

	std::vector<BeamNode> BuildNextBeam(
		const ParticleBeamSearchContract& BeamContract,
		const std::vector<BeamNode>& Parents,
		const std::int32_t AssistIndex,
		std::vector<BeamProposal>& Refined,
		const std::uint32_t ThreadCount,
		ParticleBeamConstructionMetrics& InOutMetrics)
	{
		std::sort(
			Refined.begin(),
			Refined.end(),
			EvaluatedProposalRanksBefore);
		std::vector<std::size_t> SelectedIndices;
		for (std::size_t Index = 0;
			Index < Refined.size();
			++Index)
		{
			if (!Refined[Index].ParticleAccepted)
			{
				continue;
			}
			const Vec3d& Center =
				Refined[Index].Layout.Scenario.GetAssist(
					AssistIndex).CenterCM;
			const bool Diverse = std::all_of(
				SelectedIndices.begin(),
				SelectedIndices.end(),
				[&](const std::size_t ExistingIndex)
				{
					const Vec3d& ExistingCenter =
						Refined[ExistingIndex]
							.Layout.Scenario.GetAssist(
								AssistIndex).CenterCM;
					return (Center - ExistingCenter).Length()
						>= BeamContract
							.MinimumBeamDiversityDistanceCM;
				});
			if (Diverse)
			{
				SelectedIndices.push_back(Index);
				if (SelectedIndices.size()
					>= static_cast<std::size_t>(
						BeamContract.BeamWidth))
				{
					break;
				}
			}
		}
		if (SelectedIndices.empty())
		{
			return {};
		}
		std::vector<std::shared_ptr<
			const std::vector<BeamParticle>>> ChildParticles(
				SelectedIndices.size());
		std::vector<std::uint8_t> ParticleSuccess(
			SelectedIndices.size(), 0u);
		ParallelFor(
			SelectedIndices.size(),
			ThreadCount,
			[&](const std::size_t LocalIndex)
			{
				ParticleSuccess[LocalIndex] =
					PopulateChildParticles(
						BeamContract,
						Parents,
						AssistIndex,
						Refined[SelectedIndices[LocalIndex]],
						ChildParticles[LocalIndex]);
			});

		std::vector<BeamNode> Result;
		for (std::size_t LocalIndex = 0;
			LocalIndex < SelectedIndices.size();
			++LocalIndex)
		{
			BeamProposal& Proposal =
				Refined[SelectedIndices[LocalIndex]];
			InOutMetrics.RefinementParticleSolveCounts[
				static_cast<std::size_t>(AssistIndex - 1)]
				+= static_cast<std::uint64_t>(
					Proposal.SolverInvocationCount);
			if (ParticleSuccess[LocalIndex] == 0u)
			{
				continue;
			}
			const BeamNode& Parent =
				Parents[Proposal.ParentIndex];
			BeamNode Node;
			Node.RootIndex = Parent.RootIndex;
			Node.Parameters = Parent.Parameters;
			Node.Layout = std::move(Proposal.Layout);
			Node.NominalResult =
				std::move(Proposal.NominalResult);
			Node.Particles =
				std::move(ChildParticles[LocalIndex]);
			Node.StageMetrics = Parent.StageMetrics;
			Node.StageMetrics[
				static_cast<std::size_t>(AssistIndex - 1)]
				= Proposal.StageMetrics;
			Node.ConstructionScore =
				Proposal.EvaluationScore;
			Node.ConstructionHash =
				ComputeNodeConstructionHash(
					BeamContract,
					Node,
					AssistIndex);
			Result.push_back(std::move(Node));
		}
		std::sort(
			Result.begin(),
			Result.end(),
			[](const BeamNode& Left, const BeamNode& Right)
			{
				if (Left.ConstructionScore
					!= Right.ConstructionScore)
				{
					return Left.ConstructionScore
						> Right.ConstructionScore;
				}
				return Left.ConstructionHash
					< Right.ConstructionHash;
			});
		if (Result.size()
			> static_cast<std::size_t>(BeamContract.BeamWidth))
		{
			Result.resize(static_cast<std::size_t>(
				BeamContract.BeamWidth));
		}
		return Result;
	}

	bool BuildInitialBeam(
		const ParticleBeamSearchContract& BeamContract,
		const std::uint32_t ThreadCount,
		std::vector<BeamNode>& OutBeam,
		ParticleBeamConstructionMetrics& InOutMetrics,
		std::string& OutFailure)
	{
		CandidateSearchContract ParameterContract =
			BeamContract.EvaluationContract;
		ParameterContract.SearchSeed =
			BeamContract.ConstructionSeed;
		std::vector<WorkParameters> Parameters;
		Parameters.reserve(static_cast<std::size_t>(
			BeamContract.RootParameterCount));
		std::vector<CandidateLayout> RootLayouts;
		RootLayouts.reserve(Parameters.capacity());
		for (std::int32_t RootIndex = 0;
			RootIndex < BeamContract.RootParameterCount;
			++RootIndex)
		{
			WorkParameters Work = MakeWorkParameters(
				ParameterContract,
				static_cast<std::uint64_t>(RootIndex));
			const std::uint64_t CoastSampleIndex =
				BeamContract.ConstructionSeed % 104729ull
				+ static_cast<std::uint64_t>(RootIndex) + 1ull;
			Work.InterEncounterCoastSeconds[0] =
				M11Core::Lerp(
					BeamContract
						.ConstructionInterEncounterCoastMinimumSeconds,
					BeamContract
						.ConstructionInterEncounterCoastMaximumSeconds,
					Halton(CoastSampleIndex, 79));
			Work.InterEncounterCoastSeconds[1] =
				M11Core::Lerp(
					BeamContract
						.ConstructionInterEncounterCoastMinimumSeconds,
					BeamContract
						.ConstructionInterEncounterCoastMaximumSeconds,
					Halton(CoastSampleIndex, 83));
			CandidateLayout Layout = MakeSeedLayout(
				BeamContract.EvaluationContract,
				Work);
			ParkConstructionBodies(Layout);
			RefreshIdentity(
				Layout,
				BeamContract.EvaluationContract);
			Parameters.push_back(std::move(Work));
			RootLayouts.push_back(std::move(Layout));
		}
		if (RootLayouts.empty())
		{
			OutFailure = "ParticleBeamHasNoParameterRoots";
			return false;
		}
		const std::vector<LaunchInput> Inputs =
			MakeParticleInputs(
				RootLayouts.front(),
				BeamContract.ExplorationSeed,
				BeamContract.ExplorationSampleCount,
				true);
		std::vector<BeamParticle> Particles(Inputs.size());
		std::vector<std::uint8_t> Solved(Inputs.size(), 0u);
		ParallelFor(
			Inputs.size(),
			ThreadCount,
			[&](const std::size_t Index)
			{
				BeamParticle& Particle = Particles[Index];
				Particle.Input = Inputs[Index];
				Particle.CountsTowardRatio = Index != 0;
				TrajectoryRequest Request;
				std::int32_t SolveCount = 0;
				Solved[Index] = BuildAndSolve(
					RootLayouts.front(),
					Particle.Input,
					0x0u,
					Request,
					Particle.Result,
					SolveCount);
			});
		if (std::any_of(
			Solved.begin(),
			Solved.end(),
			[](const std::uint8_t Value) { return Value == 0u; }))
		{
			OutFailure = "InitialParticleSolveFailed";
			return false;
		}
		InOutMetrics.InitialParticleSolveCount +=
			static_cast<std::uint64_t>(Particles.size());
		const std::shared_ptr<const std::vector<BeamParticle>>
			SharedParticles =
				std::make_shared<const std::vector<BeamParticle>>(
					std::move(Particles));
		OutBeam.clear();
		OutBeam.reserve(RootLayouts.size());
		for (std::size_t RootIndex = 0;
			RootIndex < RootLayouts.size();
			++RootIndex)
		{
			BeamNode Node;
			Node.RootIndex =
				static_cast<std::uint64_t>(RootIndex);
			Node.Parameters = Parameters[RootIndex];
			Node.Layout = std::move(RootLayouts[RootIndex]);
			Node.NominalResult =
				SharedParticles->front().Result;
			Node.Particles = SharedParticles;
			Node.ConstructionHash =
				ComputeNodeConstructionHash(
					BeamContract, Node, 0);
			OutBeam.push_back(std::move(Node));
		}
		return true;
	}

	bool ExpandBeamStage(
		const ParticleBeamSearchContract& BeamContract,
		const std::uint32_t ThreadCount,
		const std::int32_t AssistIndex,
		const std::vector<BeamNode>& Parents,
		std::vector<BeamNode>& OutBeam,
		ParticleBeamConstructionMetrics& InOutMetrics,
		std::string& OutFailure)
	{
		std::vector<BeamProposal> Proposals;
		GenerateGeometryProposals(
			BeamContract,
			Parents,
			AssistIndex,
			Proposals);
		const std::size_t MetricIndex =
			static_cast<std::size_t>(AssistIndex - 1);
		InOutMetrics.GeometryProposalCounts[MetricIndex] +=
			static_cast<std::uint64_t>(Proposals.size());
		if (Proposals.empty())
		{
			OutFailure = "Stage"
				+ std::to_string(AssistIndex)
				+ "GeometryEmpty";
			return false;
		}
		SelectDiverseGeometryProposals(
			Proposals,
			AssistIndex,
			static_cast<std::size_t>(
				BeamContract.NominalProposalBudget),
			BeamContract.MinimumBeamDiversityDistanceCM * 0.35);
		ParallelFor(
			Proposals.size(),
			ThreadCount,
			[&](const std::size_t Index)
			{
				EvaluateNominalProposal(
					BeamContract,
					Parents,
					AssistIndex,
					Proposals[Index]);
			});
		for (BeamProposal& Proposal : Proposals)
		{
			InOutMetrics.NominalProposalSolveCounts[MetricIndex]
				+= static_cast<std::uint64_t>(
					std::max(0, Proposal.SolverInvocationCount));
			Proposal.SolverInvocationCount = 0;
		}
		std::array<std::int32_t, 9> NominalFailureCounts{};
		for (const BeamProposal& Proposal : Proposals)
		{
			if (!Proposal.NominalAccepted
				&& Proposal.NominalFailureCode >= 0
				&& Proposal.NominalFailureCode
					< static_cast<std::int32_t>(
						NominalFailureCounts.size()))
			{
				++NominalFailureCounts[
					static_cast<std::size_t>(
						Proposal.NominalFailureCode)];
			}
		}
		Proposals.erase(
			std::remove_if(
				Proposals.begin(),
				Proposals.end(),
				[](const BeamProposal& Proposal)
				{
					return !Proposal.NominalAccepted;
				}),
			Proposals.end());
		if (Proposals.empty())
		{
			OutFailure = "Stage"
				+ std::to_string(AssistIndex)
				+ "NominalEmpty:Codes="
				+ std::to_string(NominalFailureCounts[0]) + ","
				+ std::to_string(NominalFailureCounts[1]) + ","
				+ std::to_string(NominalFailureCounts[2]) + ","
				+ std::to_string(NominalFailureCounts[3]) + ","
				+ std::to_string(NominalFailureCounts[4]) + ","
				+ std::to_string(NominalFailureCounts[5]) + ","
				+ std::to_string(NominalFailureCounts[6]) + ","
				+ std::to_string(NominalFailureCounts[7]) + ","
				+ std::to_string(NominalFailureCounts[8]);
			return false;
		}
		std::sort(
			Proposals.begin(),
			Proposals.end(),
			EvaluatedProposalRanksBefore);
		if (Proposals.size()
			> static_cast<std::size_t>(
				BeamContract.CoarseProposalBudget))
		{
			Proposals.resize(static_cast<std::size_t>(
				BeamContract.CoarseProposalBudget));
		}
		ParallelFor(
			Proposals.size(),
			ThreadCount,
			[&](const std::size_t Index)
			{
				EvaluateProposalParticleSet(
					BeamContract,
					Parents,
					AssistIndex,
					false,
					BeamContract.CoarseParticleLimit,
					Proposals[Index]);
			});
		for (BeamProposal& Proposal : Proposals)
		{
			InOutMetrics.CoarseParticleSolveCounts[MetricIndex]
				+= static_cast<std::uint64_t>(
					std::max(0, Proposal.SolverInvocationCount));
			Proposal.SolverInvocationCount = 0;
		}
		Proposals.erase(
			std::remove_if(
				Proposals.begin(),
				Proposals.end(),
				[](const BeamProposal& Proposal)
				{
					return !Proposal.ParticleAccepted;
				}),
			Proposals.end());
		if (Proposals.empty())
		{
			OutFailure = "Stage"
				+ std::to_string(AssistIndex)
				+ "CoarseEmpty";
			return false;
		}
		std::sort(
			Proposals.begin(),
			Proposals.end(),
			EvaluatedProposalRanksBefore);
		if (Proposals.size()
			> static_cast<std::size_t>(
				BeamContract.RefinementProposalBudget))
		{
			Proposals.resize(static_cast<std::size_t>(
				BeamContract.RefinementProposalBudget));
		}
		for (BeamProposal& Proposal : Proposals)
		{
			Proposal.ParticleAccepted = false;
		}
		ParallelFor(
			Proposals.size(),
			ThreadCount,
			[&](const std::size_t Index)
			{
				EvaluateProposalParticleSet(
					BeamContract,
					Parents,
					AssistIndex,
					true,
					BeamContract.ExplorationSampleCount + 1,
					Proposals[Index]);
			});
		for (BeamProposal& Proposal : Proposals)
		{
			InOutMetrics.RefinementParticleSolveCounts[MetricIndex]
				+= static_cast<std::uint64_t>(
					std::max(0, Proposal.SolverInvocationCount));
			Proposal.SolverInvocationCount = 0;
		}
		double MinimumRetention =
			std::numeric_limits<double>::max();
		double MaximumRetention = 0.0;
		std::int32_t MaximumMembers = 0;
		std::int32_t MaximumRobustPrefixSurvivors = 0;
		for (const BeamProposal& Proposal : Proposals)
		{
			MinimumRetention = std::min(
				MinimumRetention,
				Proposal.StageMetrics.RetentionRatio);
			MaximumRetention = std::max(
				MaximumRetention,
				Proposal.StageMetrics.RetentionRatio);
			MaximumMembers = std::max(
				MaximumMembers,
				Proposal.StageMetrics.MemberParticleCount);
			MaximumRobustPrefixSurvivors = std::max(
				MaximumRobustPrefixSurvivors,
				Proposal.StageMetrics.RobustPrefixSurvivorCount);
		}
		Proposals.erase(
			std::remove_if(
				Proposals.begin(),
				Proposals.end(),
				[](const BeamProposal& Proposal)
				{
					return !Proposal.ParticleAccepted;
				}),
			Proposals.end());
		if (Proposals.empty())
		{
			OutFailure = "Stage"
				+ std::to_string(AssistIndex)
				+ "RefinementEmpty:Retention="
				+ std::to_string(
					MinimumRetention
						== std::numeric_limits<double>::max()
					? 0.0
					: MinimumRetention)
				+ ".." + std::to_string(MaximumRetention)
				+ ":MaxMembers="
				+ std::to_string(MaximumMembers)
				+ ":MaxRobust="
				+ std::to_string(MaximumRobustPrefixSurvivors);
			return false;
		}
		OutBeam = BuildNextBeam(
			BeamContract,
			Parents,
			AssistIndex,
			Proposals,
			ThreadCount,
			InOutMetrics);
		InOutMetrics.BeamSurvivorCounts[MetricIndex] =
			static_cast<std::int32_t>(OutBeam.size());
		if (OutBeam.empty())
		{
			OutFailure = "Stage"
				+ std::to_string(AssistIndex)
				+ "BeamEmpty";
			return false;
		}
		return true;
	}

	bool EvaluateHoldout(
		const ParticleBeamSearchContract& BeamContract,
		HoldoutEvaluation& Evaluation)
	{
		const CandidateSearchContract& Contract =
			BeamContract.EvaluationContract;
		const std::vector<LaunchInput> Inputs =
			MakeParticleInputs(
				Evaluation.Layout,
				BeamContract.HoldoutSeed,
				BeamContract.HoldoutSampleCount,
				false);
		std::array<std::vector<YawPitchPoint>, 4> Evidence;
		for (const LaunchInput& Input : Inputs)
		{
			TrajectoryRequest Request;
			TrajectoryResult Result;
			if (!BuildAndSolve(
				Evaluation.Layout,
				Input,
				0x7u,
				Request,
				Result,
				Evaluation.SolverInvocationCount))
			{
				Evaluation.Rejection =
					"HoldoutBuildAndSolveFailed";
				return false;
			}
			const std::array<bool, 4> Membership =
				ClassifyInputSets(
					Evaluation.Layout,
					Contract,
					Result);
			for (std::size_t SetIndex = 0;
				SetIndex < Membership.size();
				++SetIndex)
			{
				if (Membership[SetIndex])
				{
					++Evaluation.InputSets[SetIndex]
						.ScreenAimCount;
					Evidence[SetIndex].push_back(
						YawPitchPoint{
							Input.YawDegrees,
							Input.PitchDegrees});
				}
			}
		}
		double RetentionScore = 0.0;
		double HullScore = 0.0;
		for (std::size_t SetIndex = 0;
			SetIndex < Evaluation.InputSets.size();
			++SetIndex)
		{
			InputSetMetrics& Set =
				Evaluation.InputSets[SetIndex];
			const std::int32_t ParentCount = SetIndex == 0
				? BeamContract.HoldoutSampleCount
				: Evaluation.InputSets[SetIndex - 1]
					.ScreenAimCount;
			Set.ScreenAimRetentionRatio =
				Ratio(Set.ScreenAimCount, ParentCount);
			Set.ScreenAimRetentionCompliant = SetIndex < 3
				&& Set.ScreenAimRetentionRatio
					>= Contract.MinimumPrefixRetentionRatio
				&& Set.ScreenAimRetentionRatio
					<= Contract.MaximumPrefixRetentionRatio;
			PopulateHullMetrics(
				Evaluation.Layout,
				Contract,
				Evidence[SetIndex],
				Set);
			if (SetIndex < 3)
			{
				if (Set.ScreenAimRetentionRatio
					< Contract.MinimumPrefixRetentionRatio)
				{
					Evaluation.Rejection = "HoldoutS"
						+ std::to_string(SetIndex + 1)
						+ "RetentionLow";
					return false;
				}
				if (Set.ScreenAimRetentionRatio
					> Contract.MaximumPrefixRetentionRatio)
				{
					Evaluation.Rejection = "HoldoutS"
						+ std::to_string(SetIndex + 1)
						+ "RetentionHigh";
					return false;
				}
				if (!Set.ScreenAimHullCompliant)
				{
					Evaluation.Rejection = "HoldoutS"
						+ std::to_string(SetIndex + 1)
						+ "HullDegenerate";
					return false;
				}
				RetentionScore += PreferredRetentionScore(
					BeamContract,
					Set.ScreenAimRetentionRatio);
				HullScore +=
					0.50 * std::clamp(
						Set.ScreenAimHullAreaSquareDegrees / 0.05,
						0.0,
						1.0)
					+ 0.25 * std::clamp(
						Set.ScreenAimHullYawSpanDegrees / 0.25,
						0.0,
						1.0)
					+ 0.25 * std::clamp(
						Set.ScreenAimHullPitchSpanDegrees / 0.25,
						0.0,
						1.0);
			}
		}
		Evaluation.Score =
			Evaluation.Node.ConstructionScore
			+ 25.0 * (RetentionScore / 3.0)
			+ 10.0 * (HullScore / 3.0);
		Evaluation.Accepted = true;
		return true;
	}

	bool RefineParticleTarget(
		const ParticleBeamSearchContract& BeamContract,
		const TrajectoryResult& TargetlessNominalArc,
		CandidateLayout& InOutLayout,
		TrajectoryResult& OutNominalResult,
		std::int32_t& InOutSolveCount,
		std::string& OutDiagnostic)
	{
		const CandidateSearchContract& Contract =
			BeamContract.EvaluationContract;
		const TrajectoryEvent* Exit3 =
			TargetlessNominalArc.FindAssistEvent(
				TrajectoryEventType::AssistExit,
				GravityScenario::AssistCount);
		if (Exit3 == nullptr)
		{
			OutDiagnostic = "ParticleTargetMissingAssist3Exit";
			return false;
		}

		struct TargetCenterProposal
		{
			Vec3d CenterCM;
			Vec3d VelocityCMPerSec;
			std::uint64_t Ordinal = 0;
		};
		std::vector<TargetCenterProposal> Centers;
		Centers.push_back(TargetCenterProposal{
			InOutLayout.Scenario.Target.CenterCM,
			-InOutLayout.Scenario.Target.PresentationForward,
			0});
		for (std::int32_t TimeIndex = 0;
			TimeIndex < BeamContract.TargetRefinementTimeSampleCount;
			++TimeIndex)
		{
			const double TimeAlpha =
				BeamContract.TargetRefinementTimeSampleCount == 1
				? 0.5
				: static_cast<double>(TimeIndex)
					/ static_cast<double>(
						BeamContract
							.TargetRefinementTimeSampleCount - 1);
			const double CoastSeconds = M11Core::Lerp(
				Contract.TargetCoastMinimumSeconds,
				BeamContract
					.TargetRefinementMaximumCoastSeconds,
				TimeAlpha);
			const TrajectoryPoint* Point = FindPointAtOrAfter(
				TargetlessNominalArc,
				Exit3->TimeSeconds + CoastSeconds);
			if (Point != nullptr)
			{
				Centers.push_back(TargetCenterProposal{
					Point->PositionCM,
					Point->VelocityCMPerSec,
					static_cast<std::uint64_t>(TimeIndex + 1)});
			}
		}

		const double BaseRadius = InOutLayout.Scenario.Target.HitRadiusCM;
		const std::array<double, 3> RadiusCandidates{
			BaseRadius,
			std::min(
				Contract.MaximumTargetHitRadiusCM,
				BaseRadius + 3.0 * Contract.TargetCoverageMarginCM),
			Contract.MaximumTargetHitRadiusCM};
		const std::int32_t RequiredRobustSurvivors =
			Contract.MinimumRobustSurvivorCount;
		bool Found = false;
		double BestTurn = -1.0;
		std::int32_t BestRobustSurvivors = -1;
		double BestRadius = std::numeric_limits<double>::max();
		std::uint64_t BestOrdinal =
			std::numeric_limits<std::uint64_t>::max();
		double MaximumObservedTurn = 0.0;
		std::int32_t MaximumObservedRobustSurvivors = 0;
		CandidateLayout BestLayout;
		TrajectoryResult BestNominalResult;

		for (const TargetCenterProposal& Center : Centers)
		{
			for (const double RadiusCM : RadiusCandidates)
			{
				CandidateLayout Candidate = InOutLayout;
				TargetSpec& Target = Candidate.Scenario.Target;
				Target.CenterCM = Center.CenterCM;
				Target.HitRadiusCM = RadiusCM;
				Target.GeometricContactCenterCM = Center.CenterCM;
				Target.PresentationForward =
					-Center.VelocityCMPerSec.GetSafeNormal(
						M11Core::SmallNumber,
						Vec3d(1.0, 0.0, 0.0));
				bool GeometryLegal = true;
				for (std::int32_t AssistIndex = 1;
					AssistIndex <= GravityScenario::AssistCount;
					++AssistIndex)
				{
					const GravityBodySpec& Assist =
						Candidate.Scenario.GetAssist(AssistIndex);
					if ((Target.CenterCM - Assist.CenterCM).Length()
						<= Assist.CollisionRadiusCM
							+ Target.HitRadiusCM
							+ Contract.MinimumBodyClearanceCM)
					{
						GeometryLegal = false;
						break;
					}
				}
				if (!GeometryLegal)
				{
					continue;
				}
				const double LayoutTurn =
					ComputeMinimumLayoutTurn(Candidate);
				MaximumObservedTurn = std::max(
					MaximumObservedTurn,
					LayoutTurn);
				if (LayoutTurn < Contract.MinimumLayoutTurnRadians)
				{
					continue;
				}
				RefreshIdentity(Candidate, Contract);
				std::int32_t RobustSurvivors = 0;
				bool NominalHit = false;
				TrajectoryResult NominalResult;
				for (const LaunchInput& Input :
					MakeRobustInputs(Candidate, Contract))
				{
					TrajectoryRequest Request;
					TrajectoryResult Result;
					if (!BuildAndSolve(
							Candidate,
							Input,
							0x7u,
							Request,
							Result,
							InOutSolveCount))
					{
						continue;
					}
					const bool QualifiedHit =
						Result.DidHitTarget()
						&& ResultPassesAssistPrefix(
							Candidate,
							Contract,
							Result,
							GravityScenario::AssistCount);
					if (QualifiedHit)
					{
						++RobustSurvivors;
					}
					if (SameInput(Input, Candidate.NominalInput))
					{
						NominalHit = QualifiedHit;
						NominalResult = std::move(Result);
					}
				}
				if (!NominalHit
					|| RobustSurvivors < RequiredRobustSurvivors)
				{
					MaximumObservedRobustSurvivors = std::max(
						MaximumObservedRobustSurvivors,
						RobustSurvivors);
					continue;
				}
				MaximumObservedRobustSurvivors = std::max(
					MaximumObservedRobustSurvivors,
					RobustSurvivors);
				const bool RanksBefore =
					!Found
					|| LayoutTurn > BestTurn
					|| (LayoutTurn == BestTurn
						&& RobustSurvivors > BestRobustSurvivors)
					|| (LayoutTurn == BestTurn
						&& RobustSurvivors == BestRobustSurvivors
						&& RadiusCM < BestRadius)
					|| (LayoutTurn == BestTurn
						&& RobustSurvivors == BestRobustSurvivors
						&& RadiusCM == BestRadius
						&& Center.Ordinal < BestOrdinal);
				if (RanksBefore)
				{
					Found = true;
					BestTurn = LayoutTurn;
					BestRobustSurvivors = RobustSurvivors;
					BestRadius = RadiusCM;
					BestOrdinal = Center.Ordinal;
					BestLayout = std::move(Candidate);
					BestNominalResult = std::move(NominalResult);
				}
			}
		}
		if (!Found)
		{
			OutDiagnostic = "ParticleTargetRefinementEmpty:MaxTurn="
				+ std::to_string(MaximumObservedTurn)
				+ ":MaxRobust="
				+ std::to_string(
					MaximumObservedRobustSurvivors);
			return false;
		}
		InOutLayout = std::move(BestLayout);
		OutNominalResult = std::move(BestNominalResult);
		OutDiagnostic.clear();
		return true;
	}

	bool FinalizeParticleCandidate(
		const CandidateSearchContract& Contract,
		const std::uint64_t GlobalIndex,
		const CandidateLayout& Layout,
		const TrajectoryResult& NominalArc,
		CandidateRecord& OutCandidate)
	{
		OutCandidate = CandidateRecord();
		OutCandidate.GlobalWorkIndex = GlobalIndex;
		OutCandidate.Layout = Layout;
		OutCandidate.CandidateSourceHash =
			ComputeCandidateSourceHash(Layout, Contract);
		const auto RejectFinal =
			[&OutCandidate](
				const EvaluationStatus Status,
				const std::string& Reason)
			{
				OutCandidate.Status = Status;
				OutCandidate.Rejection = Reason;
				OutCandidate.ScoreHash =
					ComputeCandidateScoreHash(OutCandidate);
				return true;
			};

		std::string Failure;
		TrajectoryRequest Request;
		if (!Layout.BuildRequest(
				Layout.NominalInput,
				0x7u,
				Request,
				&Failure))
		{
			return RejectFinal(
				EvaluationStatus::InternalError,
				"FinalRequestBuildFailed");
		}
		OutCandidate.NominalRequestHash =
			ComputeRequestHash(OutCandidate, Request);
		OutCandidate.NominalResultHash =
			NominalArc.ValidationHash;
		if (!NominalArc.DidHitTarget()
			|| NominalArc.CompletedAssistCount != 3)
		{
			return RejectFinal(
				EvaluationStatus::NominalRejected,
				"NominalDidNotCompleteF4");
		}
		if (!PopulateMetrics(
			Layout,
			Contract,
			NominalArc,
			OutCandidate.Metrics,
			&Failure))
		{
			return RejectFinal(
				EvaluationStatus::PacingRejected,
				Failure.empty()
					? "PacingOrGeometryGateRejected"
					: Failure);
		}

		LaunchInput LowPowerInput = Layout.NominalInput;
		LowPowerInput.Power = Contract.LowPowerProbe;
		TrajectoryResult LowPowerResult;
		if (!BuildAndSolve(
			Layout,
			LowPowerInput,
			0x7u,
			Request,
			LowPowerResult,
			OutCandidate.SolverInvocationCount,
			&Failure))
		{
			return RejectFinal(
				EvaluationStatus::InternalError,
				"LowPowerProbeSolveFailed");
		}
		OutCandidate.Metrics.LowPowerCompletedAssistCount =
			LowPowerResult.CompletedAssistCount;
		const bool QualifiedLowPowerAssist1 =
			ResultPassesAssistPrefix(
				Layout,
				Contract,
				LowPowerResult,
				1);
		if (CandidateSearch::ShouldRejectLowPowerResult(
			LowPowerResult,
			QualifiedLowPowerAssist1))
		{
			return RejectFinal(
				EvaluationStatus::LowPowerGateRejected,
				"LowPowerCompletedQualifiedAssist1PrefixOrHitTarget");
		}

		for (const LaunchInput& Input :
			MakeRobustInputs(Layout, Contract))
		{
			TrajectoryResult Result;
			if (BuildAndSolve(
					Layout,
					Input,
					0x7u,
					Request,
					Result,
					OutCandidate.SolverInvocationCount,
					&Failure)
				&& Result.DidHitTarget()
				&& ResultPassesAssistPrefix(
					Layout,
					Contract,
					Result,
					3))
			{
				++OutCandidate.Metrics.RobustSurvivorCount;
			}
		}
		if (OutCandidate.Metrics.RobustSurvivorCount
			< Contract.MinimumRobustSurvivorCount)
		{
			return RejectFinal(
				EvaluationStatus::RobustnessRejected,
				"NominalNeighborhoodTooNarrow");
		}

		for (std::size_t AblationIndex = 0;
			AblationIndex
				< OutCandidate.Metrics.AblationMasks.size();
			++AblationIndex)
		{
			TrajectoryResult Result;
			const std::uint8_t Mask =
				OutCandidate.Metrics.AblationMasks[AblationIndex];
			if (!BuildAndSolve(
				Layout,
				Layout.NominalInput,
				Mask,
				Request,
				Result,
				OutCandidate.SolverInvocationCount,
				&Failure))
			{
				return RejectFinal(
					EvaluationStatus::InternalError,
					"AblationSolveFailed");
			}
			OutCandidate.Metrics.AblationHitTarget[AblationIndex] =
				Result.DidHitTarget();
			OutCandidate.Metrics.AblationResultHashes[AblationIndex] =
				Result.ValidationHash;
			if (Result.DidHitTarget())
			{
				return RejectFinal(
					EvaluationStatus::AblationRejected,
					"AblatedLayoutStillHitsTarget");
			}
		}

		if (!AnalyzeInputDomain(
			Layout,
			Contract,
			OutCandidate.Metrics,
			OutCandidate.SolverInvocationCount,
			&Failure))
		{
			return RejectFinal(
				EvaluationStatus::InputDomainDegenerate,
				Failure.empty()
					? "InputDomainPrefixSetDegenerate"
					: Failure);
		}

		double DeflectionScore = 0.0;
		for (const AssistMetrics& Assist :
			OutCandidate.Metrics.Assists)
		{
			const double DeflectionHeadroom = std::clamp(
				(Assist.ActualDeflectionRadians
					- Contract.MinimumDeflectionRadians)
					/ std::max(
						0.1,
						1.20
							- Contract.MinimumDeflectionRadians),
				0.0,
				1.0);
			DeflectionScore +=
				0.65 * DeflectionHeadroom
				+ 0.35 * Assist.LateralTurnAxisProjection;
		}
		OutCandidate.Metrics.DeflectionReadabilityScore =
			DeflectionScore
			/ static_cast<double>(
				OutCandidate.Metrics.Assists.size());
		OutCandidate.Metrics.AlternationScore =
			static_cast<double>(
				OutCandidate.Metrics
					.AlternatingLateralTurnCount)
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
			+ 25.0
				* OutCandidate.Metrics.DeflectionReadabilityScore
			+ 15.0 * OutCandidate.Metrics.AlternationScore
			+ 10.0 * OutCandidate.Metrics.PacingScore;
		OutCandidate.Status = EvaluationStatus::Accepted;
		OutCandidate.Rejection.clear();
		OutCandidate.ScoreHash =
			ComputeCandidateScoreHash(OutCandidate);
		return true;
	}

	std::uint64_t ComputeParticleCandidateHash(
		const ParticleBeamCandidateRecord& Candidate)
	{
		ParticleHash Hash;
		Hash.AddUInt64(0x11b24002ull);
		Hash.AddUInt64(Candidate.ConstructionHash);
		Hash.AddUInt64(Candidate.Candidate.CandidateSourceHash);
		Hash.AddUInt64(Candidate.Candidate.NominalResultHash);
		Hash.AddUInt64(Candidate.Candidate.ScoreHash);
		Hash.AddDouble(Candidate.ConstructionScore);
		for (const ParticleBeamStageMetrics& Stage :
			Candidate.Stages)
		{
			Hash.AddInt32(Stage.AssistIndex);
			Hash.AddInt32(Stage.ParentParticleCount);
			Hash.AddInt32(Stage.MemberParticleCount);
			Hash.AddDouble(Stage.RetentionRatio);
			Hash.AddDouble(Stage.HullAreaSquareDegrees);
			Hash.AddDouble(Stage.HullYawSpanDegrees);
			Hash.AddDouble(Stage.HullPitchSpanDegrees);
			Hash.AddDouble(Stage.HullCompactness);
			Hash.AddDouble(Stage.ActualDeflectionRadians);
			Hash.AddDouble(Stage.SignedLateralTurnRadians);
			Hash.AddDouble(Stage.InfluenceDurationSeconds);
			Hash.AddDouble(Stage.CoastBeforeEnterSeconds);
			Hash.AddInt32(Stage.RobustPrefixSurvivorCount);
			Hash.AddDouble(Stage.StageScore);
		}
		Hash.AddInt32(Candidate.HoldoutSampleCount);
		for (const InputSetMetrics& Set :
			Candidate.HoldoutInputSets)
		{
			Hash.AddInt32(Set.FullDomainCount);
			Hash.AddDouble(Set.FullDomainRetentionRatio);
			Hash.AddInt32(Set.ScreenAimCount);
			Hash.AddDouble(Set.ScreenAimRetentionRatio);
			Hash.AddBool(Set.ScreenAimRetentionCompliant);
			Hash.AddInt32(Set.ConditionalProbeCount);
			Hash.AddInt32(Set.ConditionalParentCount);
			Hash.AddInt32(Set.ConditionalMemberCount);
			Hash.AddDouble(Set.ConditionalRetentionRatio);
			Hash.AddInt32(Set.ScreenAimHullEvidencePointCount);
			Hash.AddUInt64(static_cast<std::uint64_t>(
				Set.ScreenAimHullYawPitch.size()));
			for (const YawPitchPoint& Point :
				Set.ScreenAimHullYawPitch)
			{
				Hash.AddDouble(Point.YawDegrees);
				Hash.AddDouble(Point.PitchDegrees);
			}
			Hash.AddDouble(Set.ScreenAimHullAreaSquareDegrees);
			Hash.AddDouble(Set.ScreenAimHullYawSpanDegrees);
			Hash.AddDouble(Set.ScreenAimHullPitchSpanDegrees);
			Hash.AddDouble(Set.ScreenAimHullNormalizedArea);
			Hash.AddDouble(Set.ScreenAimHullCompactness);
			Hash.AddBool(Set.ScreenAimHullContainsNominal);
			Hash.AddBool(Set.ScreenAimHullCompliant);
		}
		return Hash.Get();
	}

	std::uint64_t ComputeParticleAggregateHash(
		const std::vector<ParticleBeamCandidateRecord>& Candidates)
	{
		ParticleHash Hash;
		Hash.AddUInt64(0x11b24003ull);
		Hash.AddUInt64(static_cast<std::uint64_t>(
			Candidates.size()));
		for (const ParticleBeamCandidateRecord& Candidate :
			Candidates)
		{
			Hash.AddUInt64(
				ComputeParticleCandidateHash(Candidate));
		}
		return Hash.Get();
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

std::uint64_t
ABTS::M11Search::ParticleBeamSearch::ComputeContractHash(
	const ParticleBeamSearchContract& Contract)
{
	return SearchPrivate::ComputeParticleContractHash(Contract);
}

bool ABTS::M11Search::ParticleBeamSearch::Run(
	const ParticleBeamSearchContract& Contract,
	const std::uint32_t ThreadCount,
	const std::uint32_t RequestedCandidateCount,
	ParticleBeamSearchResult& OutResult,
	std::string* OutFailure)
{
	using namespace SearchPrivate;
	OutResult = ParticleBeamSearchResult();
	const auto Start = std::chrono::steady_clock::now();
	std::string Failure;
	if (!Contract.IsValid(&Failure)
		|| ThreadCount == 0
		|| RequestedCandidateCount == 0
		|| RequestedCandidateCount
			> static_cast<std::uint32_t>(
				Contract.MaximumFinalAuditCandidates))
	{
		if (Failure.empty())
		{
			Failure = "InvalidParticleBeamExecutionRequest";
		}
		OutResult.Diagnostic = Failure;
		if (OutFailure != nullptr)
		{
			*OutFailure = Failure;
		}
		return false;
	}
	OutResult.ContractHash =
		ComputeParticleContractHash(Contract);
	std::vector<BeamNode> Beam;
	if (!BuildInitialBeam(
		Contract,
		ThreadCount,
		Beam,
		OutResult.Construction,
		Failure))
	{
		OutResult.Diagnostic = Failure;
		if (OutFailure != nullptr)
		{
			*OutFailure = Failure;
		}
		return false;
	}
	for (std::int32_t AssistIndex = 1;
		AssistIndex <= M11Core::GravityScenario::AssistCount;
		++AssistIndex)
	{
		std::vector<BeamNode> Next;
		if (!ExpandBeamStage(
			Contract,
			ThreadCount,
			AssistIndex,
			Beam,
			Next,
			OutResult.Construction,
			Failure))
		{
			const auto End = std::chrono::steady_clock::now();
			OutResult.WallClockSeconds =
				std::chrono::duration<double>(End - Start).count();
			OutResult.Diagnostic = Failure;
			if (OutFailure != nullptr)
			{
				OutFailure->clear();
			}
			return true;
		}
		Beam = std::move(Next);
	}

	std::vector<HoldoutEvaluation> Holdouts(Beam.size());
	ParallelFor(
		Beam.size(),
		ThreadCount,
		[&](const std::size_t Index)
		{
			HoldoutEvaluation& Evaluation = Holdouts[Index];
			Evaluation.Node = Beam[Index];
			Evaluation.Layout = Beam[Index].Layout;
			Evaluation.NominalResult =
				Beam[Index].NominalResult;
			std::string TargetDiagnostic;
			CandidateSearchContract TargetConstructionContract =
				Contract.EvaluationContract;
			TargetConstructionContract.MinimumRobustSurvivorCount +=
				Contract.RobustGuardSurvivorCount;
			TargetConstructionContract.TargetCoverageMarginCM =
				std::min(
					TargetConstructionContract.MaximumTargetHitRadiusCM
						- TargetConstructionContract
							.MinimumTargetHitRadiusCM,
					TargetConstructionContract.TargetCoverageMarginCM
						* 2.0);
			if (!BuildTarget(
				TargetConstructionContract,
				Beam[Index].Parameters,
				Evaluation.Layout,
				Evaluation.NominalResult,
				Evaluation.SolverInvocationCount,
				TargetDiagnostic))
			{
				Evaluation.Rejection =
					TargetDiagnostic.empty()
						? "TargetConstructionFailed"
						: TargetDiagnostic;
				return;
			}
			if (!RefineParticleTarget(
				Contract,
				Beam[Index].NominalResult,
				Evaluation.Layout,
				Evaluation.NominalResult,
				Evaluation.SolverInvocationCount,
				TargetDiagnostic))
			{
				Evaluation.Rejection =
					TargetDiagnostic.empty()
						? "ParticleTargetRefinementFailed"
						: TargetDiagnostic;
				return;
			}
			EvaluateHoldout(Contract, Evaluation);
		});
	for (const HoldoutEvaluation& Evaluation : Holdouts)
	{
		OutResult.Construction.HoldoutSolveCount +=
			static_cast<std::uint64_t>(
				std::max(0, Evaluation.SolverInvocationCount));
	}
	std::map<std::string, std::int32_t> HoldoutRejectionCounts;
	for (const HoldoutEvaluation& Evaluation : Holdouts)
	{
		if (!Evaluation.Accepted)
		{
			++HoldoutRejectionCounts[
				Evaluation.Rejection.empty()
				? "UnspecifiedHoldoutRejection"
				: Evaluation.Rejection];
		}
	}
	Holdouts.erase(
		std::remove_if(
			Holdouts.begin(),
			Holdouts.end(),
			[](const HoldoutEvaluation& Evaluation)
			{
				return !Evaluation.Accepted;
			}),
		Holdouts.end());
	std::sort(
		Holdouts.begin(),
		Holdouts.end(),
		[](const HoldoutEvaluation& Left,
			const HoldoutEvaluation& Right)
		{
			if (Left.Score != Right.Score)
			{
				return Left.Score > Right.Score;
			}
			return Left.Node.ConstructionHash
				< Right.Node.ConstructionHash;
		});
	if (Holdouts.size()
		> static_cast<std::size_t>(
			Contract.MaximumFinalAuditCandidates))
	{
		Holdouts.resize(static_cast<std::size_t>(
			Contract.MaximumFinalAuditCandidates));
	}
	if (Holdouts.empty())
	{
		const auto End = std::chrono::steady_clock::now();
		OutResult.WallClockSeconds =
			std::chrono::duration<double>(End - Start).count();
		std::ostringstream Diagnostic;
		Diagnostic << "CompletedInsufficientHoldoutCandidates";
		for (const auto& [Reason, Count] : HoldoutRejectionCounts)
		{
			Diagnostic << ":" << Reason << "=" << Count;
		}
		OutResult.Diagnostic = Diagnostic.str();
		if (OutFailure != nullptr)
		{
			OutFailure->clear();
		}
		return true;
	}

	OutResult.Evaluations.resize(Holdouts.size());
	ParallelFor(
		Holdouts.size(),
		ThreadCount,
		[&](const std::size_t Index)
		{
			const HoldoutEvaluation& Holdout = Holdouts[Index];
			ParticleBeamCandidateRecord& Evaluation =
				OutResult.Evaluations[Index];
			Evaluation.ConstructionHash =
				Holdout.Node.ConstructionHash;
			Evaluation.ConstructionScore = Holdout.Score;
			Evaluation.Stages = Holdout.Node.StageMetrics;
			Evaluation.HoldoutSampleCount =
				Contract.HoldoutSampleCount;
			Evaluation.HoldoutInputSets =
				Holdout.InputSets;
			FinalizeParticleCandidate(
				Contract.EvaluationContract,
				Holdout.Node.RootIndex,
				Holdout.Layout,
				Holdout.NominalResult,
				Evaluation.Candidate);
		});
	for (const ParticleBeamCandidateRecord& Evaluation :
		OutResult.Evaluations)
	{
		OutResult.Construction.FinalAuditSolveCount +=
			static_cast<std::uint64_t>(
				std::max(
					0,
					Evaluation.Candidate.SolverInvocationCount));
	}

	std::vector<std::size_t> RankedIndices;
	for (std::size_t Index = 0;
		Index < OutResult.Evaluations.size();
		++Index)
	{
		if (OutResult.Evaluations[Index].Candidate.IsAccepted())
		{
			RankedIndices.push_back(Index);
		}
	}
	std::sort(
		RankedIndices.begin(),
		RankedIndices.end(),
		[&](const std::size_t Left, const std::size_t Right)
		{
			return CandidateSearch::CandidateRanksBefore(
				OutResult.Evaluations[Left].Candidate,
				OutResult.Evaluations[Right].Candidate);
		});
	for (const std::size_t Index : RankedIndices)
	{
		const ParticleBeamCandidateRecord& Candidate =
			OutResult.Evaluations[Index];
		const bool Diverse = std::all_of(
			OutResult.TopCandidates.begin(),
			OutResult.TopCandidates.end(),
			[&](const ParticleBeamCandidateRecord& Existing)
			{
				return CandidateDiversityDistance(
					Candidate.Candidate,
					Existing.Candidate)
					>= Contract.EvaluationContract
						.MinimumDiversityDistanceCM;
			});
		if (Diverse)
		{
			OutResult.TopCandidates.push_back(Candidate);
			if (OutResult.TopCandidates.size()
				>= RequestedCandidateCount)
			{
				break;
			}
		}
	}
	OutResult.ConstructionAggregateHash =
		ComputeParticleAggregateHash(OutResult.Evaluations);
	OutResult.CandidateAggregateHash =
		ComputeParticleAggregateHash(OutResult.TopCandidates);
	const auto End = std::chrono::steady_clock::now();
	OutResult.WallClockSeconds =
		std::chrono::duration<double>(End - Start).count();
	OutResult.Diagnostic =
		OutResult.TopCandidates.size() >= RequestedCandidateCount
		? "Completed"
		: "CompletedInsufficientCandidates";
	if (OutFailure != nullptr)
	{
		OutFailure->clear();
	}
	return true;
}
