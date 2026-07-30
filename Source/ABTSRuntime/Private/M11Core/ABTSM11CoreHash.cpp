// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreInternal.h"

#include <bit>
#include <cstdint>

namespace ABTS::M11Core::HashDetail
{
	class StableHash64
	{
	public:
		void AddByte(const std::uint8_t Value)
		{
			Hash ^= Value;
			Hash *= 1099511628211ull;
		}

		void AddUInt32(const std::uint32_t Value)
		{
			for (std::int32_t Shift = 0; Shift < 32; Shift += 8)
			{
				AddByte(static_cast<std::uint8_t>(
					(Value >> Shift) & 0xffu));
			}
		}

		void AddUInt64(const std::uint64_t Value)
		{
			for (std::int32_t Shift = 0; Shift < 64; Shift += 8)
			{
				AddByte(static_cast<std::uint8_t>(
					(Value >> Shift) & 0xffull));
			}
		}

		void AddInt32(const std::int32_t Value)
		{
			AddUInt32(static_cast<std::uint32_t>(Value));
		}

		void AddDouble(double Value)
		{
			if (Value == 0.0)
			{
				Value = 0.0;
			}
			AddUInt64(std::bit_cast<std::uint64_t>(Value));
		}

		void AddVector(const Vec3d& Value)
		{
			AddDouble(Value.X);
			AddDouble(Value.Y);
			AddDouble(Value.Z);
		}

		[[nodiscard]] std::uint64_t Get() const
		{
			return Hash;
		}

	private:
		// M11-A result hash uses the canonical FNV-1a 64-bit offset basis.
		std::uint64_t Hash = 14695981039346656037ull;
	};
}

std::uint64_t ABTS::M11Core::Internal::ComputeResultHash(
	const TrajectoryRequest& Request,
	const TrajectoryResult& Result)
{
	HashDetail::StableHash64 Hash;
	Hash.AddInt32(Request.Config.HashSchemaVersion);
	Hash.AddInt32(Request.Config.SolverVersion);
	Hash.AddDouble(Request.Config.FixedTimeStepSeconds);
	Hash.AddDouble(Request.Config.MaximumSimulationTimeSeconds);
	Hash.AddInt32(Request.Config.MaximumStepCount);
	Hash.AddInt32(Request.Config.MaximumSubdivisionDepth);
	if (Request.Config.HashSchemaVersion >= 2)
	{
		Hash.AddInt32(
			Request.Config.MaximumCoastStepExpansionDepth);
	}
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
	for (const GravityBodySpec& Body : Request.Scenario.Bodies)
	{
		Hash.AddInt32(Body.BodyId);
		Hash.AddByte(static_cast<std::uint8_t>(Body.Role));
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
		Hash.AddByte(
			static_cast<std::uint8_t>(Body.AllowedPassSideValue));
		Hash.AddDouble(Body.MinimumEnergyChangeCM2PerSec2);
		Hash.AddDouble(Body.MaximumEnergyChangeCM2PerSec2);
	}
	Hash.AddInt32(Request.Scenario.Target.TargetId);
	Hash.AddVector(Request.Scenario.Target.CenterCM);
	Hash.AddDouble(Request.Scenario.Target.HitRadiusCM);
	const bool UsesTargetExtension =
		Request.Scenario.Target.GeometricContactRadiusCM != 0.0
		|| Request.Scenario.Target.UseSeparateGeometricContactCenter
		|| Request.Scenario.Target.RequiredQualifiedAssistCount != 0
		|| Request.Scenario.Target.MinimumQualifyingCorridorQuality != 0.0
		|| Request.Scenario.Target
			.MinimumQualifyingEnergyGainCM2PerSec2 != 0.0
		|| Request.Scenario.Target.RequireAllowedPassSide;
	if (UsesTargetExtension)
	{
		Hash.AddInt32(
			Request.Scenario.Target.RequiredQualifiedAssistCount);
		Hash.AddDouble(
			Request.Scenario.Target.GeometricContactRadiusCM);
		Hash.AddByte(
			Request.Scenario.Target
				.UseSeparateGeometricContactCenter
				? 1u
				: 0u);
		Hash.AddVector(
			Request.Scenario.Target.GeometricContactCenterCM);
		Hash.AddDouble(
			Request.Scenario.Target
				.MinimumQualifyingCorridorQuality);
		Hash.AddDouble(
			Request.Scenario.Target
				.MinimumQualifyingEnergyGainCM2PerSec2);
		Hash.AddByte(
			Request.Scenario.Target.RequireAllowedPassSide ? 1u : 0u);
	}
	Hash.AddVector(Request.InitialPositionCM);
	Hash.AddVector(Request.InitialVelocityCMPerSec);
	Hash.AddDouble(Request.InitialTimeSeconds);
	Hash.AddInt32(Request.InitialExpectedAssistIndex);

	Hash.AddByte(static_cast<std::uint8_t>(Result.Termination));
	Hash.AddInt32(Result.CompletedAssistCount);
	if (UsesTargetExtension)
	{
		Hash.AddInt32(Result.TargetContactCount);
	}
	Hash.AddInt32(static_cast<std::int32_t>(Result.Points.size()));
	for (const TrajectoryPoint& Point : Result.Points)
	{
		Hash.AddDouble(Point.TimeSeconds);
		Hash.AddVector(Point.PositionCM);
		Hash.AddVector(Point.VelocityCMPerSec);
		Hash.AddDouble(Point.PrimarySpecificEnergyCM2PerSec2);
	}
	Hash.AddInt32(static_cast<std::int32_t>(Result.Events.size()));
	for (const TrajectoryEvent& Event : Result.Events)
	{
		Hash.AddByte(static_cast<std::uint8_t>(Event.Type));
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
