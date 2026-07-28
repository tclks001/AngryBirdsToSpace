// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolverInternal.h"

namespace ABTSM11GravityAssist
{
	bool IsFiniteVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	FVector3d LerpVector(const FVector3d& A, const FVector3d& B, const double Alpha)
	{
		return A + (B - A) * Alpha;
	}

	FState LerpState(const FState& A, const FState& B, const double Alpha)
	{
		FState Result;
		Result.TimeSeconds = FMath::Lerp(A.TimeSeconds, B.TimeSeconds, Alpha);
		Result.PositionCM = LerpVector(A.PositionCM, B.PositionCM, Alpha);
		Result.VelocityCMPerSec = LerpVector(A.VelocityCMPerSec, B.VelocityCMPerSec, Alpha);
		return Result;
	}

	namespace
	{
		double SmoothStep5(const double Value)
		{
			const double X = FMath::Clamp(Value, 0.0, 1.0);
			return X * X * X * (X * (X * 6.0 - 15.0) + 10.0);
		}

		double InfluenceWeight(
			const FABTSM11GravityBodySpec& Body,
			const double DistanceCM)
		{
			if (DistanceCM >= Body.InfluenceRadiusCM)
			{
				return 0.0;
			}
			if (Body.InfluenceBlendWidthCM <= UE_DOUBLE_SMALL_NUMBER)
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
				(Body.InfluenceRadiusCM - DistanceCM) / Body.InfluenceBlendWidthCM);
		}

		FVector3d BodyAcceleration(
			const FABTSM11GravityBodySpec& Body,
			const FVector3d& PositionCM,
			const double Weight)
		{
			const FVector3d DeltaCM = Body.CenterCM - PositionCM;
			const double DistanceCM = DeltaCM.Length();
			if (DistanceCM <= UE_DOUBLE_SMALL_NUMBER || Weight <= 0.0)
			{
				return FVector3d::ZeroVector;
			}
			const double SafeDistanceCM =
				FMath::Max(DistanceCM, Body.MinimumEvaluationRadiusCM);
			return DeltaCM * (
				Weight * Body.GravitationalParameterCM3PerSec2
				/ (SafeDistanceCM * SafeDistanceCM * SafeDistanceCM));
		}

		FVector3d ComputeAcceleration(
			const FABTSM11TrajectoryRequest& Request,
			const int32 ExpectedAssistIndex,
			const FVector3d& PositionCM)
		{
			FVector3d Result =
				BodyAcceleration(Request.Scenario.GetPrimary(), PositionCM, 1.0);
			if (ExpectedAssistIndex >= 1
				&& ExpectedAssistIndex <= FABTSM11GravityScenario::AssistCount)
			{
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(ExpectedAssistIndex);
				const double DistanceCM = (PositionCM - Assist.CenterCM).Length();
				Result += BodyAcceleration(
					Assist, PositionCM, InfluenceWeight(Assist, DistanceCM));
			}
			return Result;
		}
	}

	FState ConservativeStep(
		const FABTSM11TrajectoryRequest& Request,
		const int32 ExpectedAssistIndex,
		const FState& State,
		const double DeltaSeconds)
	{
		const FVector3d Acceleration0 =
			ComputeAcceleration(Request, ExpectedAssistIndex, State.PositionCM);
		FState Result;
		Result.TimeSeconds = State.TimeSeconds + DeltaSeconds;
		Result.PositionCM =
			State.PositionCM
			+ State.VelocityCMPerSec * DeltaSeconds
			+ Acceleration0 * (0.5 * DeltaSeconds * DeltaSeconds);
		const FVector3d Acceleration1 =
			ComputeAcceleration(Request, ExpectedAssistIndex, Result.PositionCM);
		Result.VelocityCMPerSec =
			State.VelocityCMPerSec + (Acceleration0 + Acceleration1) * (0.5 * DeltaSeconds);
		return Result;
	}

	bool SelectStepSeconds(
		const FABTSM11TrajectoryRequest& Request,
		const int32 ExpectedAssistIndex,
		const FState& State,
		double& OutStepSeconds)
	{
		const FABTSM11SolverConfig& Config = Request.Config;
		double MinimumAssistRadiusCM = TNumericLimits<double>::Max();
		double MinimumCollisionRadiusCM = Request.Scenario.Target.HitRadiusCM;
		for (const FABTSM11GravityBodySpec& Body : Request.Scenario.Bodies)
		{
			MinimumCollisionRadiusCM =
				FMath::Min(MinimumCollisionRadiusCM, Body.CollisionRadiusCM);
			if (Body.IsAssist())
			{
				MinimumAssistRadiusCM =
					FMath::Min(MinimumAssistRadiusCM, Body.AssistReferenceRadiusCM);
			}
		}

		const double SpeedCMPerSec = State.VelocityCMPerSec.Length();
		const double AccelerationCMPerSec2 =
			ComputeAcceleration(Request, ExpectedAssistIndex, State.PositionCM).Length();
		for (int32 Depth = 0; Depth <= Config.MaximumSubdivisionDepth; ++Depth)
		{
			const double StepSeconds =
				Config.FixedTimeStepSeconds / static_cast<double>(1 << Depth);
			bool bAccepted =
				SpeedCMPerSec * StepSeconds
					<= Config.AssistStepRadiusFraction * MinimumAssistRadiusCM
				&& SpeedCMPerSec * StepSeconds
					<= Config.CollisionStepRadiusFraction * MinimumCollisionRadiusCM
				&& 0.5 * AccelerationCMPerSec2 * StepSeconds * StepSeconds
					<= Config.PositionErrorLimitCM;

			const FABTSM11GravityBodySpec& Primary = Request.Scenario.GetPrimary();
			const double PrimaryDistanceCM =
				FMath::Max((State.PositionCM - Primary.CenterCM).Length(),
					Primary.MinimumEvaluationRadiusCM);
			const double PrimaryTimescaleSeconds = FMath::Sqrt(
				PrimaryDistanceCM * PrimaryDistanceCM * PrimaryDistanceCM
					/ Primary.GravitationalParameterCM3PerSec2);
			bAccepted = bAccepted
				&& StepSeconds <= Config.GravityTimescaleFraction * PrimaryTimescaleSeconds;

			if (ExpectedAssistIndex >= 1
				&& ExpectedAssistIndex <= FABTSM11GravityScenario::AssistCount)
			{
				const FABTSM11GravityBodySpec& Assist =
					Request.Scenario.GetAssist(ExpectedAssistIndex);
				const double AssistDistanceCM =
					(State.PositionCM - Assist.CenterCM).Length();
				if (AssistDistanceCM < Assist.InfluenceRadiusCM)
				{
					const double SafeAssistDistanceCM =
						FMath::Max(AssistDistanceCM, Assist.MinimumEvaluationRadiusCM);
					const double AssistTimescaleSeconds = FMath::Sqrt(
						SafeAssistDistanceCM * SafeAssistDistanceCM * SafeAssistDistanceCM
							/ Assist.GravitationalParameterCM3PerSec2);
					bAccepted = bAccepted
						&& StepSeconds <= Config.GravityTimescaleFraction * AssistTimescaleSeconds;
				}
			}
			if (bAccepted)
			{
				OutStepSeconds = StepSeconds;
				return true;
			}
		}
		OutStepSeconds =
			Config.FixedTimeStepSeconds
			/ static_cast<double>(1 << Config.MaximumSubdivisionDepth);
		return false;
	}

	bool SegmentSphereRoots(
		const FVector3d& StartCM,
		const FVector3d& EndCM,
		const FVector3d& CenterCM,
		const double RadiusCM,
		FSphereRoots& OutRoots)
	{
		const FVector3d SegmentCM = EndCM - StartCM;
		const FVector3d OffsetCM = StartCM - CenterCM;
		const double A = SegmentCM.SquaredLength();
		const double C = OffsetCM.SquaredLength() - RadiusCM * RadiusCM;
		OutRoots.bStartsInside = C <= 0.0;
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			if (OutRoots.bStartsInside)
			{
				OutRoots.EnterAlpha = 0.0;
				OutRoots.ExitAlpha = 0.0;
				return true;
			}
			return false;
		}

		const double B = 2.0 * FVector3d::DotProduct(OffsetCM, SegmentCM);
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant < 0.0)
		{
			return false;
		}
		const double Root = FMath::Sqrt(FMath::Max(0.0, Discriminant));
		double First = (-B - Root) / (2.0 * A);
		double Second = (-B + Root) / (2.0 * A);
		if (First > Second)
		{
			Swap(First, Second);
		}
		if (Second < 0.0 || First > 1.0)
		{
			return false;
		}
		OutRoots.EnterAlpha =
			OutRoots.bStartsInside ? 0.0 : FMath::Clamp(First, 0.0, 1.0);
		OutRoots.ExitAlpha = FMath::Clamp(Second, 0.0, 1.0);
		return true;
	}

	double FindRadialRootAlpha(
		const FState& Start,
		const FState& End,
		const FVector3d& CenterCM,
		const FABTSM11SolverConfig& Config)
	{
		auto RadialRate = [&Start, &End, &CenterCM](const double Alpha)
		{
			const FVector3d PositionCM =
				LerpVector(Start.PositionCM, End.PositionCM, Alpha);
			const FVector3d VelocityCMPerSec =
				LerpVector(Start.VelocityCMPerSec, End.VelocityCMPerSec, Alpha);
			return FVector3d::DotProduct(PositionCM - CenterCM, VelocityCMPerSec);
		};

		double Lower = 0.0;
		double Upper = 1.0;
		for (int32 Iteration = 0; Iteration < Config.RootBisectionIterations; ++Iteration)
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

	double FindRadialStepFraction(
		const FABTSM11TrajectoryRequest& Request,
		const int32 ExpectedAssistIndex,
		const FState& State,
		const double FullStepSeconds,
		const FVector3d& CenterCM,
		const FABTSM11SolverConfig& Config,
		const bool bIncreasing)
	{
		double Lower = 0.0;
		double Upper = 1.0;
		for (int32 Iteration = 0; Iteration < Config.RootBisectionIterations; ++Iteration)
		{
			const double Middle = 0.5 * (Lower + Upper);
			const FState MiddleState =
				ConservativeStep(
					Request, ExpectedAssistIndex, State, FullStepSeconds * Middle);
			const double RadialRate = FVector3d::DotProduct(
				MiddleState.PositionCM - CenterCM,
				MiddleState.VelocityCMPerSec);
			const bool bBeforeRoot =
				bIncreasing ? RadialRate <= 0.0 : RadialRate >= 0.0;
			if (bBeforeRoot)
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

	double FindSphereBoundaryStepFraction(
		const FABTSM11TrajectoryRequest& Request,
		const int32 ExpectedAssistIndex,
		const FState& State,
		const double FullStepSeconds,
		const FVector3d& CenterCM,
		const double RadiusCM,
		const bool bEntering,
		const FABTSM11SolverConfig& Config)
	{
		double Lower = 0.0;
		double Upper = 1.0;
		for (int32 Iteration = 0; Iteration < Config.RootBisectionIterations; ++Iteration)
		{
			const double Middle = 0.5 * (Lower + Upper);
			const FState MiddleState =
				ConservativeStep(
					Request, ExpectedAssistIndex, State, FullStepSeconds * Middle);
			const bool bInside =
				(MiddleState.PositionCM - CenterCM).Length() <= RadiusCM;
			const bool bBeforeBoundary = bEntering ? !bInside : bInside;
			if (bBeforeBoundary)
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
}
