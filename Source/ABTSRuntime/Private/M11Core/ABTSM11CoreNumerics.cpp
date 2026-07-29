// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreInternal.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace ABTS::M11Core::Internal
{
	bool IsFiniteVector(const Vec3d& Value)
	{
		return IsFinite(Value.X)
			&& IsFinite(Value.Y)
			&& IsFinite(Value.Z);
	}

	Vec3d LerpVector(
		const Vec3d& A,
		const Vec3d& B,
		const double Alpha)
	{
		return A + (B - A) * Alpha;
	}

	State LerpState(
		const State& A,
		const State& B,
		const double Alpha)
	{
		State Result;
		Result.TimeSeconds = Lerp(
			A.TimeSeconds,
			B.TimeSeconds,
			Alpha);
		Result.PositionCM =
			LerpVector(A.PositionCM, B.PositionCM, Alpha);
		Result.VelocityCMPerSec = LerpVector(
			A.VelocityCMPerSec,
			B.VelocityCMPerSec,
			Alpha);
		return Result;
	}
}

namespace ABTS::M11Core::NumericsDetail
{
	using namespace Internal;

	[[nodiscard]] double SmoothStep5(const double Value)
	{
		const double X = Clamp(Value, 0.0, 1.0);
		return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
	}

	[[nodiscard]] double InfluenceWeight(
		const GravityBodySpec& Body,
		const double DistanceCM)
	{
		if (DistanceCM >= Body.InfluenceRadiusCM)
		{
			return 0.0;
		}
		if (Body.InfluenceBlendWidthCM <= DoubleSmallNumber)
		{
			return 1.0;
		}
		const double BlendStartCM =
			Body.InfluenceRadiusCM - Body.InfluenceBlendWidthCM;
		if (DistanceCM <= BlendStartCM)
		{
			return 1.0;
		}
		return SmoothStep5(
			(Body.InfluenceRadiusCM - DistanceCM)
				/ Body.InfluenceBlendWidthCM);
	}

	[[nodiscard]] Vec3d BodyAcceleration(
		const GravityBodySpec& Body,
		const Vec3d& PositionCM,
		const double Weight)
	{
		const Vec3d DeltaCM = Body.CenterCM - PositionCM;
		const double DistanceCM = DeltaCM.Length();
		if (DistanceCM <= DoubleSmallNumber || Weight <= 0.0)
		{
			return Vec3d();
		}
		const double SafeDistanceCM =
			Max(DistanceCM, Body.MinimumEvaluationRadiusCM);
		return DeltaCM * (
			Weight * Body.GravitationalParameterCM3PerSec2
			/ (SafeDistanceCM
				* SafeDistanceCM
				* SafeDistanceCM));
	}

	[[nodiscard]] Vec3d ComputeAcceleration(
		const TrajectoryRequest& Request,
		const std::int32_t ExpectedAssistIndex,
		const Vec3d& PositionCM)
	{
		Vec3d Result = BodyAcceleration(
			Request.Scenario.GetPrimary(),
			PositionCM,
			1.0);
		if (ExpectedAssistIndex >= 1
			&& ExpectedAssistIndex <= GravityScenario::AssistCount)
		{
			const GravityBodySpec& Assist =
				Request.Scenario.GetAssist(ExpectedAssistIndex);
			const double DistanceCM =
				(PositionCM - Assist.CenterCM).Length();
			Result += BodyAcceleration(
				Assist,
				PositionCM,
				InfluenceWeight(Assist, DistanceCM));
		}
		return Result;
	}
}

ABTS::M11Core::Internal::State
ABTS::M11Core::Internal::ConservativeStep(
	const TrajectoryRequest& Request,
	const std::int32_t ExpectedAssistIndex,
	const State& CurrentState,
	const double DeltaSeconds)
{
	const Vec3d Acceleration0 =
		NumericsDetail::ComputeAcceleration(
			Request,
			ExpectedAssistIndex,
			CurrentState.PositionCM);
	State Result;
	Result.TimeSeconds = CurrentState.TimeSeconds + DeltaSeconds;
	Result.PositionCM =
		CurrentState.PositionCM
		+ CurrentState.VelocityCMPerSec * DeltaSeconds
		+ Acceleration0
			* (0.5 * DeltaSeconds * DeltaSeconds);
	const Vec3d Acceleration1 =
		NumericsDetail::ComputeAcceleration(
			Request,
			ExpectedAssistIndex,
			Result.PositionCM);
	Result.VelocityCMPerSec =
		CurrentState.VelocityCMPerSec
		+ (Acceleration0 + Acceleration1)
			* (0.5 * DeltaSeconds);
	return Result;
}

bool ABTS::M11Core::Internal::SelectStepSeconds(
	const TrajectoryRequest& Request,
	const std::int32_t ExpectedAssistIndex,
	const State& CurrentState,
	double& OutStepSeconds)
{
	const SolverConfig& Config = Request.Config;
	double MinimumAssistRadiusCM =
		std::numeric_limits<double>::max();
	double MinimumCollisionRadiusCM =
		Request.Scenario.Target.GetGeometricContactRadiusCM();
	for (const GravityBodySpec& Body : Request.Scenario.Bodies)
	{
		MinimumCollisionRadiusCM =
			Min(MinimumCollisionRadiusCM, Body.CollisionRadiusCM);
		if (Body.IsAssist())
		{
			MinimumAssistRadiusCM =
				Min(
					MinimumAssistRadiusCM,
					Body.AssistReferenceRadiusCM);
		}
	}

	const double SpeedCMPerSec =
		CurrentState.VelocityCMPerSec.Length();
	const double AccelerationCMPerSec2 =
		NumericsDetail::ComputeAcceleration(
			Request,
			ExpectedAssistIndex,
			CurrentState.PositionCM).Length();
	bool InsideAnyAssistInfluence = false;
	for (std::int32_t AssistIndex = 1;
		AssistIndex <= GravityScenario::AssistCount;
		++AssistIndex)
	{
		const GravityBodySpec& Assist =
			Request.Scenario.GetAssist(AssistIndex);
		if ((CurrentState.PositionCM - Assist.CenterCM).SquaredLength()
			< Assist.InfluenceRadiusCM * Assist.InfluenceRadiusCM)
		{
			InsideAnyAssistInfluence = true;
			break;
		}
	}
	const std::int32_t CoastExpansionDepth =
		Config.SolverVersion >= 2 && !InsideAnyAssistInfluence
			? Config.MaximumCoastStepExpansionDepth
			: 0;
	for (std::int32_t StepPower = CoastExpansionDepth;
		StepPower >= -Config.MaximumSubdivisionDepth;
		--StepPower)
	{
		const double StepScale = StepPower >= 0
			? static_cast<double>(
				std::uint64_t{1} << StepPower)
			: 1.0 / static_cast<double>(
				std::uint64_t{1} << -StepPower);
		const double StepSeconds =
			Config.FixedTimeStepSeconds * StepScale;
		bool Accepted =
			SpeedCMPerSec * StepSeconds
				<= Config.AssistStepRadiusFraction
					* MinimumAssistRadiusCM
			&& SpeedCMPerSec * StepSeconds
				<= Config.CollisionStepRadiusFraction
					* MinimumCollisionRadiusCM
			&& 0.5 * AccelerationCMPerSec2
					* StepSeconds
					* StepSeconds
				<= Config.PositionErrorLimitCM;

		const GravityBodySpec& Primary =
			Request.Scenario.GetPrimary();
		const double PrimaryDistanceCM = Max(
			(CurrentState.PositionCM - Primary.CenterCM).Length(),
			Primary.MinimumEvaluationRadiusCM);
		const double PrimaryTimescaleSeconds = Sqrt(
			PrimaryDistanceCM
				* PrimaryDistanceCM
				* PrimaryDistanceCM
			/ Primary.GravitationalParameterCM3PerSec2);
		Accepted = Accepted
			&& StepSeconds
				<= Config.GravityTimescaleFraction
					* PrimaryTimescaleSeconds;

		if (ExpectedAssistIndex >= 1
			&& ExpectedAssistIndex <= GravityScenario::AssistCount)
		{
			const GravityBodySpec& Assist =
				Request.Scenario.GetAssist(ExpectedAssistIndex);
			const double AssistDistanceCM =
				(CurrentState.PositionCM - Assist.CenterCM).Length();
			if (AssistDistanceCM < Assist.InfluenceRadiusCM)
			{
				const double SafeAssistDistanceCM = Max(
					AssistDistanceCM,
					Assist.MinimumEvaluationRadiusCM);
				const double AssistTimescaleSeconds = Sqrt(
					SafeAssistDistanceCM
						* SafeAssistDistanceCM
						* SafeAssistDistanceCM
					/ Assist.GravitationalParameterCM3PerSec2);
				Accepted = Accepted
					&& StepSeconds
						<= Config.GravityTimescaleFraction
							* AssistTimescaleSeconds;
			}
		}
		if (Accepted)
		{
			OutStepSeconds = StepSeconds;
			return true;
		}
	}
	OutStepSeconds =
		Config.FixedTimeStepSeconds
		/ static_cast<double>(
			std::int32_t{1} << Config.MaximumSubdivisionDepth);
	return false;
}

bool ABTS::M11Core::Internal::SegmentSphereRoots(
	const Vec3d& StartCM,
	const Vec3d& EndCM,
	const Vec3d& CenterCM,
	const double RadiusCM,
	SphereRoots& OutRoots)
{
	const Vec3d SegmentCM = EndCM - StartCM;
	const Vec3d OffsetCM = StartCM - CenterCM;
	const double A = SegmentCM.SquaredLength();
	const double C =
		OffsetCM.SquaredLength() - RadiusCM * RadiusCM;
	OutRoots.StartsInside = C <= 0.0;
	if (A <= DoubleSmallNumber)
	{
		if (OutRoots.StartsInside)
		{
			OutRoots.EnterAlpha = 0.0;
			OutRoots.ExitAlpha = 0.0;
			return true;
		}
		return false;
	}

	const double B = 2.0 * Vec3d::DotProduct(OffsetCM, SegmentCM);
	const double Discriminant = B * B - 4.0 * A * C;
	if (Discriminant < 0.0)
	{
		return false;
	}
	const double Root = Sqrt(Max(0.0, Discriminant));
	double First = (-B - Root) / (2.0 * A);
	double Second = (-B + Root) / (2.0 * A);
	if (First > Second)
	{
		std::swap(First, Second);
	}
	if (Second < 0.0 || First > 1.0)
	{
		return false;
	}
	OutRoots.EnterAlpha =
		OutRoots.StartsInside ? 0.0 : Clamp(First, 0.0, 1.0);
	OutRoots.ExitAlpha = Clamp(Second, 0.0, 1.0);
	return true;
}

bool ABTS::M11Core::Internal::IsV2MacroStepSphereTopologyCertified(
	const TrajectoryRequest& Request,
	const State& Start,
	const State& End,
	const double FullStepSeconds)
{
	if (Request.Config.SolverVersion < 2
		|| FullStepSeconds <= DoubleSmallNumber)
	{
		return false;
	}

	const Vec3d AccelerationDisplacementCM =
		End.PositionCM - Start.PositionCM
		- Start.VelocityCMPerSec * FullStepSeconds;
	const double MaximumChordDeviationCM =
		0.25 * AccelerationDisplacementCM.Length();
	if (!IsFinite(MaximumChordDeviationCM)
		|| !IsFiniteVector(End.PositionCM))
	{
		return false;
	}

	const auto SphereTopologyIsCertified =
		[&Request, &Start, &End, MaximumChordDeviationCM](
			const Vec3d& CenterCM,
			const double RadiusCM)
		{
			if (!IsFinite(RadiusCM) || RadiusCM <= 0.0)
			{
				return true;
			}

			const Vec3d ChordCM =
				End.PositionCM - Start.PositionCM;
			const double ChordLengthSquaredCM2 =
				ChordCM.SquaredLength();
			const double StartDistanceCM =
				(Start.PositionCM - CenterCM).Length();
			const double EndDistanceCM =
				(End.PositionCM - CenterCM).Length();
			const bool StartInside = StartDistanceCM <= RadiusCM;
			const bool EndInside = EndDistanceCM <= RadiusCM;
			if (StartInside != EndInside)
			{
				return false;
			}
			const double RoundoffMarginCM = Max(
				1.0e-9,
				Request.Config.RootAlphaTolerance * (
					RadiusCM
					+ Sqrt(ChordLengthSquaredCM2)
					+ StartDistanceCM
					+ EndDistanceCM));
			const double EnvelopeCM =
				MaximumChordDeviationCM + RoundoffMarginCM;
			if (StartInside)
			{
				return Max(StartDistanceCM, EndDistanceCM)
					+ EnvelopeCM < RadiusCM;
			}

			double ClosestAlpha = 0.0;
			if (ChordLengthSquaredCM2 > DoubleSmallNumber)
			{
				ClosestAlpha = Clamp(
					Vec3d::DotProduct(
						CenterCM - Start.PositionCM,
						ChordCM)
						/ ChordLengthSquaredCM2,
					0.0,
					1.0);
			}
			const double ChordDistanceCM =
				(Start.PositionCM
					+ ChordCM * ClosestAlpha
					- CenterCM).Length();
			return ChordDistanceCM > RadiusCM + EnvelopeCM;
		};

	for (const GravityBodySpec& Body : Request.Scenario.Bodies)
	{
		if (!SphereTopologyIsCertified(
			Body.CenterCM,
			Body.CollisionRadiusCM))
		{
			return false;
		}
	}
	for (std::int32_t AssistIndex = 1;
		AssistIndex <= GravityScenario::AssistCount;
		++AssistIndex)
	{
		const GravityBodySpec& Assist =
			Request.Scenario.GetAssist(AssistIndex);
		if (!SphereTopologyIsCertified(
				Assist.CenterCM,
				Assist.InfluenceRadiusCM)
			|| !SphereTopologyIsCertified(
				Assist.CenterCM,
				Assist.AssistReferenceRadiusCM))
		{
			return false;
		}
	}
	return SphereTopologyIsCertified(
			Request.Scenario.Target.GetGeometricContactCenterCM(),
			Request.Scenario.Target.GetGeometricContactRadiusCM())
		&& SphereTopologyIsCertified(
			Request.Scenario.Target.CenterCM,
			Request.Scenario.Target.HitRadiusCM)
		&& SphereTopologyIsCertified(
			Request.Scenario.GetPrimary().CenterCM,
			Request.Scenario.GetPrimary().MaximumSimulationRadiusCM);
}

double ABTS::M11Core::Internal::FindRadialRootAlpha(
	const State& Start,
	const State& End,
	const Vec3d& CenterCM,
	const SolverConfig& Config)
{
	const auto RadialRate =
		[&Start, &End, &CenterCM](const double Alpha)
		{
			const Vec3d PositionCM =
				LerpVector(Start.PositionCM, End.PositionCM, Alpha);
			const Vec3d VelocityCMPerSec = LerpVector(
				Start.VelocityCMPerSec,
				End.VelocityCMPerSec,
				Alpha);
			return Vec3d::DotProduct(
				PositionCM - CenterCM,
				VelocityCMPerSec);
		};

	double Lower = 0.0;
	double Upper = 1.0;
	for (std::int32_t Iteration = 0;
		Iteration < Config.RootBisectionIterations;
		++Iteration)
	{
		const double Middle = 0.5 * (Lower + Upper);
		if (RadialRate(Middle) <= 0.0)
		{
			Lower = Middle;
		}
		else
		{
			Upper = Middle;
		}
		if (Upper - Lower <= Config.RootAlphaTolerance)
		{
			break;
		}
	}
	return 0.5 * (Lower + Upper);
}

double ABTS::M11Core::Internal::FindRadialStepFraction(
	const TrajectoryRequest& Request,
	const std::int32_t ExpectedAssistIndex,
	const State& CurrentState,
	const double FullStepSeconds,
	const Vec3d& CenterCM,
	const SolverConfig& Config,
	const bool Increasing)
{
	double Lower = 0.0;
	double Upper = 1.0;
	for (std::int32_t Iteration = 0;
		Iteration < Config.RootBisectionIterations;
		++Iteration)
	{
		const double Middle = 0.5 * (Lower + Upper);
		const State MiddleState = ConservativeStep(
			Request,
			ExpectedAssistIndex,
			CurrentState,
			FullStepSeconds * Middle);
		const double RadialRate = Vec3d::DotProduct(
			MiddleState.PositionCM - CenterCM,
			MiddleState.VelocityCMPerSec);
		const bool BeforeRoot =
			Increasing ? RadialRate <= 0.0 : RadialRate >= 0.0;
		if (BeforeRoot)
		{
			Lower = Middle;
		}
		else
		{
			Upper = Middle;
		}
		if (Upper - Lower <= Config.RootAlphaTolerance)
		{
			break;
		}
	}
	return 0.5 * (Lower + Upper);
}

double ABTS::M11Core::Internal::FindSphereBoundaryStepFraction(
	const TrajectoryRequest& Request,
	const std::int32_t ExpectedAssistIndex,
	const State& CurrentState,
	const double FullStepSeconds,
	const Vec3d& CenterCM,
	const double RadiusCM,
	const bool Entering,
	const SolverConfig& Config)
{
	double Lower = 0.0;
	double Upper = 1.0;
	for (std::int32_t Iteration = 0;
		Iteration < Config.RootBisectionIterations;
		++Iteration)
	{
		const double Middle = 0.5 * (Lower + Upper);
		const State MiddleState = ConservativeStep(
			Request,
			ExpectedAssistIndex,
			CurrentState,
			FullStepSeconds * Middle);
		const bool Inside =
			(MiddleState.PositionCM - CenterCM).Length()
				<= RadiusCM;
		const bool BeforeBoundary = Entering ? !Inside : Inside;
		if (BeforeBoundary)
		{
			Lower = Middle;
		}
		else
		{
			Upper = Middle;
		}
		if (Upper - Lower <= Config.RootAlphaTolerance)
		{
			break;
		}
	}
	return 0.5 * (Lower + Upper);
}
