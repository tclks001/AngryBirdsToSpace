// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11GravityAssistSolverInternal.h"

namespace ABTSM11GravityAssist
{
	namespace
	{
		class FStableHash64
		{
		public:
			void AddByte(const uint8 Value)
			{
				Hash ^= Value;
				Hash *= 1099511628211ull;
			}

			void AddUInt32(const uint32 Value)
			{
				for (int32 Shift = 0; Shift < 32; Shift += 8)
				{
					AddByte(static_cast<uint8>((Value >> Shift) & 0xffu));
				}
			}

			void AddUInt64(const uint64 Value)
			{
				for (int32 Shift = 0; Shift < 64; Shift += 8)
				{
					AddByte(static_cast<uint8>((Value >> Shift) & 0xffull));
				}
			}

			void AddInt32(const int32 Value)
			{
				AddUInt32(static_cast<uint32>(Value));
			}

			void AddDouble(double Value)
			{
				if (Value == 0.0)
				{
					Value = 0.0; // Canonicalize negative zero.
				}
				uint64 Bits = 0;
				FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
				AddUInt64(Bits);
			}

			void AddVector(const FVector3d& Value)
			{
				AddDouble(Value.X);
				AddDouble(Value.Y);
				AddDouble(Value.Z);
			}

			uint64 Get() const { return Hash; }

		private:
			uint64 Hash = 14695981039346656037ull;
		};
	}

	uint64 ComputeResultHash(
		const FABTSM11TrajectoryRequest& Request,
		const FABTSM11TrajectoryResult& Result)
	{
		FStableHash64 Hash;
		Hash.AddInt32(Request.Config.HashSchemaVersion);
		Hash.AddInt32(Request.Config.SolverVersion);
		Hash.AddDouble(Request.Config.FixedTimeStepSeconds);
		Hash.AddDouble(Request.Config.MaximumSimulationTimeSeconds);
		Hash.AddInt32(Request.Config.MaximumStepCount);
		Hash.AddInt32(Request.Config.MaximumSubdivisionDepth);
		Hash.AddDouble(Request.Config.AssistStepRadiusFraction);
		Hash.AddDouble(Request.Config.CollisionStepRadiusFraction);
		Hash.AddDouble(Request.Config.GravityTimescaleFraction);
		Hash.AddDouble(Request.Config.PositionErrorLimitCM);
		Hash.AddInt32(Request.Config.RootBisectionIterations);
		Hash.AddDouble(Request.Config.RootAlphaTolerance);
		Hash.AddDouble(Request.Config.BPlaneBasisMinimumLength);
		Hash.AddDouble(Request.Config.MinimumVInfinityCMPerSec);
		Hash.AddDouble(Request.Config.MaximumNaturalDeflectionErrorRadians);
		Hash.AddDouble(Request.Config.EnergyQualityPower);
		Hash.AddDouble(Request.Config.EnergyRootEpsilonCM2PerSec2);
		Hash.AddDouble(Request.Config.ExitEnergyResidualToleranceCM2PerSec2);
		Hash.AddInt32(Request.Config.EnergyShootingIterationCount);
		Hash.AddDouble(Request.Config.NaturalCloneMaximumTimeSeconds);
		Hash.AddInt32(Request.Config.NaturalCloneMaximumStepCount);
		Hash.AddByte(Request.Config.EnabledAssistMask);

		Hash.AddInt32(Request.Scenario.LayoutVersion);
		Hash.AddUInt32(Request.Scenario.ScenarioHash);
		for (const FABTSM11GravityBodySpec& Body : Request.Scenario.Bodies)
		{
			Hash.AddInt32(Body.BodyId);
			Hash.AddByte(static_cast<uint8>(Body.Role));
			Hash.AddVector(Body.CenterCM);
			Hash.AddDouble(Body.GravitationalParameterCM3PerSec2);
			Hash.AddDouble(Body.MinimumEvaluationRadiusCM);
			Hash.AddDouble(Body.CollisionRadiusCM);
			Hash.AddDouble(Body.MaximumSimulationRadiusCM);
			Hash.AddDouble(Body.InfluenceRadiusCM);
			Hash.AddDouble(Body.AssistReferenceRadiusCM);
			Hash.AddDouble(Body.InfluenceBlendWidthCM);
			Hash.AddVector(Body.VirtualOrbitalVelocityCMPerSec);
			Hash.AddVector(Body.BPlaneReferenceNormal);
			Hash.AddVector(Body.BPlaneFallbackAxis);
			Hash.AddDouble(Body.BPlaneTargetTCM);
			Hash.AddDouble(Body.BPlaneTargetRCM);
			Hash.AddDouble(Body.BPlaneSigmaTCM);
			Hash.AddDouble(Body.BPlaneSigmaRCM);
			Hash.AddDouble(Body.BPlaneOuterChiSquared);
			Hash.AddByte(static_cast<uint8>(Body.AllowedPassSide));
			Hash.AddDouble(Body.MinimumEnergyChangeCM2PerSec2);
			Hash.AddDouble(Body.MaximumEnergyChangeCM2PerSec2);
		}
		Hash.AddInt32(Request.Scenario.Target.TargetId);
		Hash.AddVector(Request.Scenario.Target.CenterCM);
		Hash.AddDouble(Request.Scenario.Target.HitRadiusCM);
		Hash.AddVector(Request.InitialPositionCM);
		Hash.AddVector(Request.InitialVelocityCMPerSec);
		Hash.AddDouble(Request.InitialTimeSeconds);
		Hash.AddInt32(Request.InitialExpectedAssistIndex);

		Hash.AddByte(static_cast<uint8>(Result.Termination));
		Hash.AddInt32(Result.CompletedAssistCount);
		Hash.AddInt32(Result.Points.Num());
		for (const FABTSM11TrajectoryPoint& Point : Result.Points)
		{
			Hash.AddDouble(Point.TimeSeconds);
			Hash.AddVector(Point.PositionCM);
			Hash.AddVector(Point.VelocityCMPerSec);
			Hash.AddDouble(Point.PrimarySpecificEnergyCM2PerSec2);
		}
		Hash.AddInt32(Result.Events.Num());
		for (const FABTSM11TrajectoryEvent& Event : Result.Events)
		{
			Hash.AddByte(static_cast<uint8>(Event.Type));
			Hash.AddInt32(Event.BodyId);
			Hash.AddInt32(Event.AssistIndex);
			Hash.AddDouble(Event.TimeSeconds);
			Hash.AddVector(Event.PositionCM);
			Hash.AddVector(Event.VelocityCMPerSec);
			Hash.AddDouble(Event.EntrySpeedCMPerSec);
			Hash.AddDouble(Event.ExitSpeedCMPerSec);
			Hash.AddDouble(Event.ClosestDistanceCM);
			Hash.AddDouble(Event.BPlaneTCM);
			Hash.AddDouble(Event.BPlaneRCM);
			Hash.AddDouble(Event.BPlaneChiSquared);
			Hash.AddDouble(Event.CorridorQuality);
			Hash.AddDouble(Event.NaturalDeflectionRadians);
			Hash.AddDouble(Event.IdealDeflectionRadians);
			Hash.AddDouble(Event.RawEnergyChangeCM2PerSec2);
			Hash.AddDouble(Event.RequestedEnergyChangeCM2PerSec2);
			Hash.AddDouble(Event.AppliedEnergyChangeCM2PerSec2);
		}
		return Hash.Get();
	}
}
