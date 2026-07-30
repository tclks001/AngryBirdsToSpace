// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Core/ABTSM11CoreConformance.h"

#include "M11Core/ABTSM11CoreSolver.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace ABTS::M11Core::Testing::ConformanceDetail
{
	class StableHash64
	{
	public:
		void AddByte(const std::uint8_t Value)
		{
			Hash ^= Value;
			Hash *= 1099511628211ull;
		}

		void AddBool(const bool Value)
		{
			AddByte(Value ? 1u : 0u);
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

		void AddFloat(float Value)
		{
			if (Value == 0.0f)
			{
				Value = 0.0f;
			}
			AddUInt32(std::bit_cast<std::uint32_t>(Value));
		}

		void AddVector(const Vec3d& Value)
		{
			AddDouble(Value.X);
			AddDouble(Value.Y);
			AddDouble(Value.Z);
		}

		void AddColor(const Color4f& Value)
		{
			AddFloat(Value.R);
			AddFloat(Value.G);
			AddFloat(Value.B);
			AddFloat(Value.A);
		}

		void AddString(const std::string& Value)
		{
			AddUInt64(static_cast<std::uint64_t>(Value.size()));
			for (const unsigned char Character : Value)
			{
				AddByte(Character);
			}
		}

		[[nodiscard]] std::uint64_t Get() const
		{
			return Hash;
		}

	private:
		std::uint64_t Hash = 14695981039346656037ull;
	};

	[[nodiscard]] bool ExactDouble(const double A, const double B)
	{
		return std::bit_cast<std::uint64_t>(A)
			== std::bit_cast<std::uint64_t>(B);
	}

	[[nodiscard]] bool ExactFloat(const float A, const float B)
	{
		return std::bit_cast<std::uint32_t>(A)
			== std::bit_cast<std::uint32_t>(B);
	}

	[[nodiscard]] bool ExactVector(const Vec3d& A, const Vec3d& B)
	{
		return ExactDouble(A.X, B.X)
			&& ExactDouble(A.Y, B.Y)
			&& ExactDouble(A.Z, B.Z);
	}

	[[nodiscard]] bool ExactColor(const Color4f& A, const Color4f& B)
	{
		return ExactFloat(A.R, B.R)
			&& ExactFloat(A.G, B.G)
			&& ExactFloat(A.B, B.B)
			&& ExactFloat(A.A, B.A);
	}

	[[nodiscard]] bool ExactBody(
		const GravityBodySpec& A,
		const GravityBodySpec& B)
	{
		return A.BodyId == B.BodyId
			&& A.Role == B.Role
			&& ExactVector(A.CenterCM, B.CenterCM)
			&& ExactDouble(
				A.GravitationalParameterCM3PerSec2,
				B.GravitationalParameterCM3PerSec2)
			&& ExactDouble(
				A.MinimumEvaluationRadiusCM,
				B.MinimumEvaluationRadiusCM)
			&& ExactDouble(A.VisualRadiusCM, B.VisualRadiusCM)
			&& ExactDouble(A.CollisionRadiusCM, B.CollisionRadiusCM)
			&& ExactDouble(
				A.MaximumSimulationRadiusCM,
				B.MaximumSimulationRadiusCM)
			&& ExactDouble(A.InfluenceRadiusCM, B.InfluenceRadiusCM)
			&& ExactDouble(
				A.AssistReferenceRadiusCM,
				B.AssistReferenceRadiusCM)
			&& ExactDouble(
				A.InfluenceBlendWidthCM,
				B.InfluenceBlendWidthCM)
			&& ExactVector(
				A.VirtualOrbitalVelocityCMPerSec,
				B.VirtualOrbitalVelocityCMPerSec)
			&& ExactVector(
				A.BPlaneReferenceNormal,
				B.BPlaneReferenceNormal)
			&& ExactVector(
				A.BPlaneFallbackAxis,
				B.BPlaneFallbackAxis)
			&& ExactDouble(A.BPlaneTargetTCM, B.BPlaneTargetTCM)
			&& ExactDouble(A.BPlaneTargetRCM, B.BPlaneTargetRCM)
			&& ExactDouble(A.BPlaneSigmaTCM, B.BPlaneSigmaTCM)
			&& ExactDouble(A.BPlaneSigmaRCM, B.BPlaneSigmaRCM)
			&& ExactDouble(
				A.BPlaneOuterChiSquared,
				B.BPlaneOuterChiSquared)
			&& A.AllowedPassSideValue == B.AllowedPassSideValue
			&& ExactDouble(
				A.MinimumEnergyChangeCM2PerSec2,
				B.MinimumEnergyChangeCM2PerSec2)
			&& ExactDouble(
				A.MaximumEnergyChangeCM2PerSec2,
				B.MaximumEnergyChangeCM2PerSec2)
			&& ExactColor(A.DebugColor, B.DebugColor);
	}

	[[nodiscard]] bool ExactTarget(
		const TargetSpec& A,
		const TargetSpec& B)
	{
		return A.TargetId == B.TargetId
			&& ExactVector(A.CenterCM, B.CenterCM)
			&& ExactDouble(A.HitRadiusCM, B.HitRadiusCM)
			&& ExactDouble(
				A.GeometricContactRadiusCM,
				B.GeometricContactRadiusCM)
			&& A.UseSeparateGeometricContactCenter
				== B.UseSeparateGeometricContactCenter
			&& ExactVector(
				A.GeometricContactCenterCM,
				B.GeometricContactCenterCM)
			&& A.RequiredQualifiedAssistCount
				== B.RequiredQualifiedAssistCount
			&& ExactDouble(
				A.MinimumQualifyingCorridorQuality,
				B.MinimumQualifyingCorridorQuality)
			&& ExactDouble(
				A.MinimumQualifyingEnergyGainCM2PerSec2,
				B.MinimumQualifyingEnergyGainCM2PerSec2)
			&& A.RequireAllowedPassSide == B.RequireAllowedPassSide
			&& ExactVector(
				A.PresentationForward,
				B.PresentationForward);
	}

	[[nodiscard]] bool ExactConfig(
		const SolverConfig& A,
		const SolverConfig& B)
	{
		return A.SolverVersion == B.SolverVersion
			&& A.HashSchemaVersion == B.HashSchemaVersion
			&& ExactDouble(
				A.FixedTimeStepSeconds,
				B.FixedTimeStepSeconds)
			&& ExactDouble(
				A.MaximumSimulationTimeSeconds,
				B.MaximumSimulationTimeSeconds)
			&& A.MaximumStepCount == B.MaximumStepCount
			&& A.MaximumSubdivisionDepth
				== B.MaximumSubdivisionDepth
			&& A.MaximumCoastStepExpansionDepth
				== B.MaximumCoastStepExpansionDepth
			&& ExactDouble(
				A.AssistStepRadiusFraction,
				B.AssistStepRadiusFraction)
			&& ExactDouble(
				A.CollisionStepRadiusFraction,
				B.CollisionStepRadiusFraction)
			&& ExactDouble(
				A.GravityTimescaleFraction,
				B.GravityTimescaleFraction)
			&& ExactDouble(
				A.PositionErrorLimitCM,
				B.PositionErrorLimitCM)
			&& A.RootBisectionIterations
				== B.RootBisectionIterations
			&& ExactDouble(
				A.RootAlphaTolerance,
				B.RootAlphaTolerance)
			&& ExactDouble(
				A.BPlaneBasisMinimumLength,
				B.BPlaneBasisMinimumLength)
			&& ExactDouble(
				A.MinimumVInfinityCMPerSec,
				B.MinimumVInfinityCMPerSec)
			&& ExactDouble(
				A.MaximumNaturalDeflectionErrorRadians,
				B.MaximumNaturalDeflectionErrorRadians)
			&& ExactDouble(
				A.EnergyQualityPower,
				B.EnergyQualityPower)
			&& ExactDouble(
				A.EnergyRootEpsilonCM2PerSec2,
				B.EnergyRootEpsilonCM2PerSec2)
			&& ExactDouble(
				A.ExitEnergyResidualToleranceCM2PerSec2,
				B.ExitEnergyResidualToleranceCM2PerSec2)
			&& A.EnergyShootingIterationCount
				== B.EnergyShootingIterationCount
			&& ExactDouble(
				A.NaturalCloneMaximumTimeSeconds,
				B.NaturalCloneMaximumTimeSeconds)
			&& A.NaturalCloneMaximumStepCount
				== B.NaturalCloneMaximumStepCount
			&& A.EnabledAssistMask == B.EnabledAssistMask;
	}

	[[nodiscard]] GravityBodySpec MakePrimary(
		const Vec3d& CenterCM,
		const double Mu,
		const double MaximumRadiusCM)
	{
		GravityBodySpec Body;
		Body.BodyId = 100;
		Body.Role = GravityRole::Primary;
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 = Mu;
		Body.MinimumEvaluationRadiusCM = 50.0;
		Body.VisualRadiusCM = 100.0;
		Body.CollisionRadiusCM = 50.0;
		Body.MaximumSimulationRadiusCM = MaximumRadiusCM;
		return Body;
	}

	[[nodiscard]] GravityBodySpec MakeAssist(
		const std::int32_t AssistIndex,
		const Vec3d& CenterCM,
		const double Mu,
		const double CollisionRadiusCM,
		const double ReferenceRadiusCM,
		const double InfluenceRadiusCM)
	{
		GravityBodySpec Body;
		Body.BodyId = 100 + AssistIndex;
		Body.Role = static_cast<GravityRole>(AssistIndex);
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 = Mu;
		Body.MinimumEvaluationRadiusCM =
			Min(50.0, CollisionRadiusCM);
		Body.VisualRadiusCM = CollisionRadiusCM * 1.5;
		Body.CollisionRadiusCM = CollisionRadiusCM;
		Body.InfluenceRadiusCM = InfluenceRadiusCM;
		Body.AssistReferenceRadiusCM = ReferenceRadiusCM;
		Body.InfluenceBlendWidthCM =
			Min(
				500.0,
				(InfluenceRadiusCM - CollisionRadiusCM) * 0.2);
		Body.BPlaneReferenceNormal = Vec3d(0.0, 0.0, 1.0);
		Body.BPlaneFallbackAxis = Vec3d(0.0, 1.0, 0.0);
		Body.BPlaneTargetTCM = 0.0;
		Body.BPlaneTargetRCM = 0.0;
		Body.BPlaneSigmaTCM = 1.0e9;
		Body.BPlaneSigmaRCM = 1.0e9;
		Body.BPlaneOuterChiSquared = 4.0;
		Body.MinimumEnergyChangeCM2PerSec2 = -20000.0;
		Body.MaximumEnergyChangeCM2PerSec2 = 20000.0;
		return Body;
	}

	[[nodiscard]] GravityScenario MakeNaturalFlybyScenario()
	{
		GravityScenario Scenario;
		Scenario.LayoutVersion = 1;
		Scenario.ScenarioHash = 0x11a001u;
		Scenario.Bodies[0] =
			MakePrimary(Vec3d(0.0, -1.0e9, 0.0), 1.0, 2.0e9);
		Scenario.Bodies[1] = MakeAssist(
			1, Vec3d(), 1.0e7, 200.0, 4500.0, 5000.0);
		Scenario.Bodies[2] = MakeAssist(
			2,
			Vec3d(1.0e8, 0.0, 0.0),
			1.0,
			100.0,
			800.0,
			1000.0);
		Scenario.Bodies[3] = MakeAssist(
			3,
			Vec3d(2.0e8, 0.0, 0.0),
			1.0,
			100.0,
			800.0,
			1000.0);
		Scenario.Target.TargetId = 200;
		Scenario.Target.CenterCM = Vec3d(3.0e8, 0.0, 0.0);
		Scenario.Target.HitRadiusCM = 100.0;
		return Scenario;
	}

	[[nodiscard]] TrajectoryRequest MakeNaturalFlybyRequest(
		const Vec3d& VirtualVelocity)
	{
		TrajectoryRequest Request;
		Request.Scenario = MakeNaturalFlybyScenario();
		Request.Scenario.Bodies[1].VirtualOrbitalVelocityCMPerSec =
			VirtualVelocity;
		Request.Config.FixedTimeStepSeconds = 1.0 / 120.0;
		Request.Config.MaximumSimulationTimeSeconds = 60.0;
		Request.Config.NaturalCloneMaximumTimeSeconds = 60.0;
		Request.InitialPositionCM = Vec3d(-6000.0, 1200.0, 0.0);
		Request.InitialVelocityCMPerSec = Vec3d(250.0, 0.0, 0.0);
		return Request;
	}

	[[nodiscard]] GravityBodySpec MakeProductionPrimary()
	{
		GravityBodySpec Body;
		Body.BodyId = 1100;
		Body.Role = GravityRole::Primary;
		Body.CenterCM = Vec3d(0.0, 0.0, -10000.0);
		Body.GravitationalParameterCM3PerSec2 = 5.665e9;
		Body.MinimumEvaluationRadiusCM = 1000.0;
		Body.VisualRadiusCM = 10000.0;
		Body.CollisionRadiusCM = 10000.0;
		Body.MaximumSimulationRadiusCM = 650000.0;
		Body.DebugColor = Color4f{0.1f, 0.35f, 0.7f, 1.0f};
		return Body;
	}

	[[nodiscard]] GravityBodySpec MakeProductionAssist(
		const std::int32_t AssistIndex,
		const Vec3d& CenterCM)
	{
		GravityBodySpec Body;
		Body.BodyId = 1100 + AssistIndex;
		Body.Role = static_cast<GravityRole>(AssistIndex);
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 =
			AssistIndex == 1
				? 8.0e7
				: AssistIndex == 2 ? 1.0e8 : 1.3e8;
		Body.MinimumEvaluationRadiusCM = 500.0;
		Body.VisualRadiusCM = 1300.0 + AssistIndex * 150.0;
		Body.CollisionRadiusCM = 800.0;
		Body.InfluenceRadiusCM =
			AssistIndex == 1
				? 15000.0
				: AssistIndex == 2 ? 22000.0 : 30000.0;
		Body.InfluenceBlendWidthCM = Body.InfluenceRadiusCM * 0.10;
		Body.AssistReferenceRadiusCM =
			Body.InfluenceRadiusCM - Body.InfluenceBlendWidthCM;
		Body.BPlaneReferenceNormal = Vec3d(0.0, 0.0, 1.0);
		Body.BPlaneFallbackAxis = Vec3d(0.0, 1.0, 0.0);
		Body.BPlaneSigmaTCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneSigmaRCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneOuterChiSquared = 4.0;
		Body.MinimumEnergyChangeCM2PerSec2 = -250000.0;
		Body.MaximumEnergyChangeCM2PerSec2 = 250000.0;
		Body.DebugColor =
			AssistIndex == 1
				? Color4f{0.8f, 0.15f, 0.08f, 1.0f}
				: AssistIndex == 2
					? Color4f{0.75f, 0.55f, 0.12f, 1.0f}
					: Color4f{0.55f, 0.35f, 0.75f, 1.0f};
		return Body;
	}

	[[nodiscard]] TrajectoryRequest MakeThreeAssistRequest(
		const std::uint8_t EnabledAssistMask)
	{
		TrajectoryRequest Request;
		Request.Scenario.LayoutVersion = 1;
		// A corpus-local identity keeps this fixture independent from the
		// M11-B preset hashing implementation.
		Request.Scenario.ScenarioHash = 0x11a210c1u;
		Request.Scenario.Bodies[0] = MakeProductionPrimary();

		Request.Scenario.Bodies[1] = MakeProductionAssist(
			1,
			Vec3d(
				97219.225601219383,
				-5700.0,
				-14094.37599498272));
		Request.Scenario.Bodies[1].VirtualOrbitalVelocityCMPerSec =
			Vec3d(0.0, -650.0, 0.0);
		Request.Scenario.Bodies[1].BPlaneTargetTCM =
			277.83495339392886;
		Request.Scenario.Bodies[1].BPlaneTargetRCM =
			-6103.2950472502735;
		Request.Scenario.Bodies[1].AllowedPassSideValue =
			AllowedPassSide::NegativeR;

		Request.Scenario.Bodies[2] = MakeProductionAssist(
			2,
			Vec3d(
				138324.92597291688,
				-26497.02451798931,
				-37845.625613579061));
		Request.Scenario.Bodies[2].VirtualOrbitalVelocityCMPerSec =
			Vec3d(
				-333.29802808429707,
				-513.19881990954673,
				-219.17891257730014);
		Request.Scenario.Bodies[2].BPlaneTargetTCM =
			3486.4912461183612;
		Request.Scenario.Bodies[2].BPlaneTargetRCM =
			-8620.1106023168049;
		Request.Scenario.Bodies[2].AllowedPassSideValue =
			AllowedPassSide::NegativeR;

		Request.Scenario.Bodies[3] = MakeProductionAssist(
			3,
			Vec3d(
				190659.21928569756,
				-61253.272725944465,
				-64968.511899881327));
		Request.Scenario.Bodies[3].VirtualOrbitalVelocityCMPerSec =
			Vec3d(
				-1353.2802614607244,
				-2220.052371891436,
				0.0);
		Request.Scenario.Bodies[3].BPlaneTargetTCM =
			78.081048156920389;
		Request.Scenario.Bodies[3].BPlaneTargetRCM =
			-11664.386295301727;
		Request.Scenario.Bodies[3].AllowedPassSideValue =
			AllowedPassSide::NegativeR;

		TargetSpec& Target = Request.Scenario.Target;
		Target.TargetId = 1199;
		Target.CenterCM = Vec3d(
			233103.20024977488,
			-78974.321891263491,
			-87227.804625011457);
		Target.HitRadiusCM = 16000.0;
		Target.GeometricContactRadiusCM = 800.0;
		Target.UseSeparateGeometricContactCenter = true;
		Target.GeometricContactCenterCM = Vec3d(
			278058.940003354,
			-112576.146689672,
			-114647.405393587);
		Target.RequiredQualifiedAssistCount =
			GravityScenario::AssistCount;
		Target.MinimumQualifyingCorridorQuality = 0.95;
		Target.MinimumQualifyingEnergyGainCM2PerSec2 = 20000.0;
		Target.RequireAllowedPassSide = true;
		Target.PresentationForward =
			(Request.Scenario.Bodies[3].CenterCM
				- Target.GeometricContactCenterCM)
				.GetSafeNormal();

		Request.Config = SolverConfig::MakeV2();
		Request.Config.FixedTimeStepSeconds = 1.0 / 120.0;
		Request.Config.MaximumSimulationTimeSeconds = 700.0;
		Request.Config.MaximumStepCount = 2000000;
		Request.Config.NaturalCloneMaximumTimeSeconds = 240.0;
		Request.Config.EnabledAssistMask =
			EnabledAssistMask & 0x7u;

		constexpr double NominalPitchDegrees = 30.0;
		constexpr double NominalPower = 0.975;
		const double PitchRadians =
			NominalPitchDegrees
			* std::numbers::pi_v<double>
			/ 180.0;
		const Vec3d Direction(
			std::cos(PitchRadians),
			0.0,
			std::sin(PitchRadians));
		const double SpeedCMPerSec =
			Lerp(400.0, 1050.0, NominalPower);
		Request.InitialPositionCM = Vec3d(0.0, 0.0, 180.0);
		Request.InitialVelocityCMPerSec =
			Direction.GetSafeNormal() * SpeedCMPerSec;
		Request.InitialExpectedAssistIndex = 1;
		return Request;
	}

	[[nodiscard]] TrajectoryRequest MakeSweptRequest(
		const bool HitBody)
	{
		TrajectoryRequest Request;
		Request.Scenario = MakeNaturalFlybyScenario();
		Request.Scenario.ScenarioHash =
			HitBody ? 0x11a210b5u : 0x11a210b4u;
		Request.Scenario.Bodies[1] = MakeAssist(
			1,
			HitBody ? Vec3d() : Vec3d(1.0e8, 0.0, 0.0),
			1.0,
			10.0,
			50.0,
			60.0);
		Request.Scenario.Bodies[2].CenterCM =
			Vec3d(1.1e8, 0.0, 0.0);
		Request.Scenario.Bodies[3].CenterCM =
			Vec3d(1.2e8, 0.0, 0.0);
		Request.Scenario.Target.CenterCM =
			HitBody
				? Vec3d(1.5e8, 0.0, 0.0)
				: Vec3d(0.0, 9.5, 0.0);
		Request.Scenario.Target.HitRadiusCM = 10.0;
		Request.Config = SolverConfig::MakeV2();
		Request.Config.FixedTimeStepSeconds = 1.0;
		Request.Config.MaximumSimulationTimeSeconds = 1.0;
		Request.Config.MaximumSubdivisionDepth = 0;
		Request.Config.MaximumCoastStepExpansionDepth = 0;
		Request.Config.AssistStepRadiusFraction = 100.0;
		Request.Config.CollisionStepRadiusFraction = 100.0;
		Request.InitialPositionCM = Vec3d(-100.0, 0.0, 0.0);
		Request.InitialVelocityCMPerSec = Vec3d(200.0, 0.0, 0.0);
		return Request;
	}

	[[nodiscard]] TrajectoryRequest MakeWrongOrderRequest()
	{
		TrajectoryRequest Request = MakeSweptRequest(false);
		Request.Scenario.ScenarioHash = 0x11a210b6u;
		Request.Scenario.Bodies[1].CenterCM =
			Vec3d(1.0e8, 0.0, 0.0);
		Request.Scenario.Bodies[2] = MakeAssist(
			2,
			Vec3d(),
			1.0,
			10.0,
			50.0,
			60.0);
		Request.Scenario.Bodies[3].CenterCM =
			Vec3d(1.2e8, 0.0, 0.0);
		Request.Scenario.Target.CenterCM =
			Vec3d(80.0, 30.0, 0.0);
		Request.InitialPositionCM = Vec3d(-100.0, 30.0, 0.0);
		return Request;
	}

	[[nodiscard]] TrajectoryRequest MakeLateTimeoutRequest()
	{
		TrajectoryRequest Request =
			MakeNaturalFlybyRequest(Vec3d());
		Request.Scenario.ScenarioHash = 0x11a210bau;
		Request.InitialPositionCM = Vec3d(-5100.0, 0.0, 0.0);
		Request.Config.MaximumSimulationTimeSeconds = 5.0;
		return Request;
	}

	[[nodiscard]] TrajectoryRequest MakeMacroFallbackRequest()
	{
		TrajectoryRequest Request;
		Request.Scenario.LayoutVersion = 2;
		Request.Scenario.ScenarioHash = 0x11a210bbu;
		Request.Scenario.Bodies[0] = MakePrimary(
			Vec3d(0.0, -1000.0, 0.0),
			8.0e8,
			1.0e7);
		Request.Scenario.Bodies[1] = MakeAssist(
			1, Vec3d(1.0e6, 0.0, 0.0),
			1.0, 50.0, 800.0, 1000.0);
		Request.Scenario.Bodies[2] = MakeAssist(
			2, Vec3d(1.1e6, 0.0, 0.0),
			1.0, 50.0, 800.0, 1000.0);
		Request.Scenario.Bodies[3] = MakeAssist(
			3, Vec3d(1.2e6, 0.0, 0.0),
			1.0, 50.0, 800.0, 1000.0);
		Request.Config = SolverConfig::MakeV2();
		Request.Config.FixedTimeStepSeconds = 0.125;
		Request.Config.MaximumSimulationTimeSeconds = 1.0;
		Request.Config.MaximumSubdivisionDepth = 8;
		Request.Config.MaximumCoastStepExpansionDepth = 3;
		Request.Config.AssistStepRadiusFraction = 100.0;
		Request.Config.CollisionStepRadiusFraction = 100.0;
		Request.Config.GravityTimescaleFraction = 100.0;
		Request.Config.PositionErrorLimitCM = 1000.0;
		Request.InitialPositionCM = Vec3d(-100.0, 0.0, 0.0);
		Request.InitialVelocityCMPerSec = Vec3d(200.0, 0.0, 0.0);

		const GravityBodySpec& Primary =
			Request.Scenario.GetPrimary();
		const Vec3d PrimaryDeltaCM =
			Primary.CenterCM - Request.InitialPositionCM;
		const double PrimaryDistanceCM = PrimaryDeltaCM.Length();
		const Vec3d InitialAccelerationCMPerSec2 =
			PrimaryDeltaCM * (
				Primary.GravitationalParameterCM3PerSec2
				/ Pow(PrimaryDistanceCM, 3.0));
		const Vec3d CurvedMidpointCM =
			Request.InitialPositionCM
			+ 0.5 * Request.InitialVelocityCMPerSec
			+ 0.125 * InitialAccelerationCMPerSec2;
		Request.Scenario.Target.TargetId = 200;
		Request.Scenario.Target.CenterCM = CurvedMidpointCM;
		Request.Scenario.Target.HitRadiusCM = 5.0;
		return Request;
	}

	void AddBodyIdentity(
		StableHash64& Hash,
		const GravityBodySpec& Body)
	{
		Hash.AddInt32(Body.BodyId);
		Hash.AddByte(static_cast<std::uint8_t>(Body.Role));
		Hash.AddVector(Body.CenterCM);
		Hash.AddDouble(Body.GravitationalParameterCM3PerSec2);
		Hash.AddDouble(Body.MinimumEvaluationRadiusCM);
		Hash.AddDouble(Body.VisualRadiusCM);
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
			static_cast<std::uint8_t>(
				Body.AllowedPassSideValue));
		Hash.AddDouble(Body.MinimumEnergyChangeCM2PerSec2);
		Hash.AddDouble(Body.MaximumEnergyChangeCM2PerSec2);
		Hash.AddColor(Body.DebugColor);
	}

	void AddTargetIdentity(
		StableHash64& Hash,
		const TargetSpec& Target)
	{
		Hash.AddInt32(Target.TargetId);
		Hash.AddVector(Target.CenterCM);
		Hash.AddDouble(Target.HitRadiusCM);
		Hash.AddDouble(Target.GeometricContactRadiusCM);
		Hash.AddBool(Target.UseSeparateGeometricContactCenter);
		Hash.AddVector(Target.GeometricContactCenterCM);
		Hash.AddInt32(Target.RequiredQualifiedAssistCount);
		Hash.AddDouble(Target.MinimumQualifyingCorridorQuality);
		Hash.AddDouble(
			Target.MinimumQualifyingEnergyGainCM2PerSec2);
		Hash.AddBool(Target.RequireAllowedPassSide);
		Hash.AddVector(Target.PresentationForward);
	}

	void AddConfigIdentity(
		StableHash64& Hash,
		const SolverConfig& Config)
	{
		Hash.AddInt32(Config.SolverVersion);
		Hash.AddInt32(Config.HashSchemaVersion);
		Hash.AddDouble(Config.FixedTimeStepSeconds);
		Hash.AddDouble(Config.MaximumSimulationTimeSeconds);
		Hash.AddInt32(Config.MaximumStepCount);
		Hash.AddInt32(Config.MaximumSubdivisionDepth);
		Hash.AddInt32(Config.MaximumCoastStepExpansionDepth);
		Hash.AddDouble(Config.AssistStepRadiusFraction);
		Hash.AddDouble(Config.CollisionStepRadiusFraction);
		Hash.AddDouble(Config.GravityTimescaleFraction);
		Hash.AddDouble(Config.PositionErrorLimitCM);
		Hash.AddInt32(Config.RootBisectionIterations);
		Hash.AddDouble(Config.RootAlphaTolerance);
		Hash.AddDouble(Config.BPlaneBasisMinimumLength);
		Hash.AddDouble(Config.MinimumVInfinityCMPerSec);
		Hash.AddDouble(
			Config.MaximumNaturalDeflectionErrorRadians);
		Hash.AddDouble(Config.EnergyQualityPower);
		Hash.AddDouble(Config.EnergyRootEpsilonCM2PerSec2);
		Hash.AddDouble(
			Config.ExitEnergyResidualToleranceCM2PerSec2);
		Hash.AddInt32(Config.EnergyShootingIterationCount);
		Hash.AddDouble(Config.NaturalCloneMaximumTimeSeconds);
		Hash.AddInt32(Config.NaturalCloneMaximumStepCount);
		Hash.AddByte(Config.EnabledAssistMask);
	}

	[[nodiscard]] std::uint64_t HashRequestIdentity(
		const TrajectoryRequest& Request)
	{
		StableHash64 Hash;
		// Domain tag prevents an input identity from being confused with a
		// result hash that happens to consume the same leading bytes.
		Hash.AddString("ABTS.M11Core.RequestIdentity.v1");
		Hash.AddInt32(Request.Scenario.LayoutVersion);
		Hash.AddUInt32(Request.Scenario.ScenarioHash);
		for (const GravityBodySpec& Body : Request.Scenario.Bodies)
		{
			AddBodyIdentity(Hash, Body);
		}
		AddTargetIdentity(Hash, Request.Scenario.Target);
		AddConfigIdentity(Hash, Request.Config);
		Hash.AddVector(Request.InitialPositionCM);
		Hash.AddVector(Request.InitialVelocityCMPerSec);
		Hash.AddDouble(Request.InitialTimeSeconds);
		Hash.AddInt32(Request.InitialExpectedAssistIndex);
		return Hash.Get();
	}

	[[nodiscard]] bool SolveRepeatedly(
		const TrajectoryRequest& Request,
		TrajectoryResult& OutFirst,
		std::string& OutFailure)
	{
		TrajectoryResult Second;
		if (!GravityAssistSolver::Solve(
				Request,
				OutFirst,
				&OutFailure)
			|| !GravityAssistSolver::Solve(
				Request,
				Second,
				&OutFailure))
		{
			return false;
		}
		return ResultsExactlyEqual(OutFirst, Second);
	}

	[[nodiscard]] std::uint64_t BuildAggregateHash(
		const std::vector<CorpusCaseReport>& Cases)
	{
		StableHash64 Hash;
		Hash.AddString("ABTS.M11Core.ConformanceCorpus.v1");
		Hash.AddUInt64(static_cast<std::uint64_t>(Cases.size()));
		for (const CorpusCaseReport& Case : Cases)
		{
			Hash.AddUInt32(static_cast<std::uint32_t>(Case.Id));
			Hash.AddString(Case.Name);
			Hash.AddUInt64(Case.RequestIdentity);
			Hash.AddUInt64(Case.ResultHash);
			Hash.AddByte(
				static_cast<std::uint8_t>(Case.Termination));
			Hash.AddInt32(Case.PointCount);
			Hash.AddInt32(Case.EventCount);
			Hash.AddInt32(Case.CompletedAssistCount);
		}
		return Hash.Get();
	}

	bool Reject(
		ConformanceReport& OutReport,
		std::string* OutFailure,
		const char* Reason)
	{
		OutReport.Diagnostic = Reason;
		if (OutFailure != nullptr)
		{
			*OutFailure = OutReport.Diagnostic;
		}
		return false;
	}
}

ABTS::M11Core::TrajectoryRequest
ABTS::M11Core::Testing::MakeV1GoldenRequest()
{
	return ConformanceDetail::MakeNaturalFlybyRequest(
		Vec3d(0.0, -50.0, 0.0));
}

ABTS::M11Core::TrajectoryRequest
ABTS::M11Core::Testing::MakeV2StrongAssistRequest()
{
	TrajectoryRequest Request =
		ConformanceDetail::MakeNaturalFlybyRequest(
			Vec3d(0.0, -2500.0, 0.0));
	Request.Config = SolverConfig::MakeV2();
	Request.Config.MaximumSimulationTimeSeconds = 160.0;
	Request.Config.NaturalCloneMaximumTimeSeconds = 160.0;
	Request.Scenario.ScenarioHash = 0x11a021u;
	Request.Scenario.Bodies[1] = ConformanceDetail::MakeAssist(
		1,
		Vec3d(),
		1.0e8,
		200.0,
		11000.0,
		12000.0);
	Request.Scenario.Bodies[1].VirtualOrbitalVelocityCMPerSec =
		Vec3d(0.0, -2500.0, 0.0);
	Request.Scenario.Bodies[1].MinimumEnergyChangeCM2PerSec2 = -2.0e6;
	Request.Scenario.Bodies[1].MaximumEnergyChangeCM2PerSec2 = 2.0e6;
	Request.InitialPositionCM = Vec3d(-15000.0, 2500.0, 0.0);
	Request.InitialVelocityCMPerSec = Vec3d(300.0, 0.0, 0.0);
	return Request;
}

ABTS::M11Core::TrajectoryRequest
ABTS::M11Core::Testing::MakeAllFieldsRequestSentinel()
{
	TrajectoryRequest Request;
	Request.Scenario.LayoutVersion = 37;
	Request.Scenario.ScenarioHash = 0xa21f1e1du;
	for (std::size_t Index = 0;
		Index < Request.Scenario.Bodies.size();
		++Index)
	{
		const double Base = 1000.0 + static_cast<double>(Index) * 100.0;
		GravityBodySpec& Body = Request.Scenario.Bodies[Index];
		Body.BodyId = 410 + static_cast<std::int32_t>(Index);
		Body.Role = static_cast<GravityRole>(
			static_cast<std::uint8_t>(Index));
		Body.CenterCM = Vec3d(Base + 1.0, Base + 2.0, Base + 3.0);
		Body.GravitationalParameterCM3PerSec2 = Base + 4.0;
		Body.MinimumEvaluationRadiusCM = Base + 5.0;
		Body.VisualRadiusCM = Base + 6.0;
		Body.CollisionRadiusCM = Base + 7.0;
		Body.MaximumSimulationRadiusCM = Base + 8.0;
		Body.InfluenceRadiusCM = Base + 9.0;
		Body.AssistReferenceRadiusCM = Base + 10.0;
		Body.InfluenceBlendWidthCM = Base + 11.0;
		Body.VirtualOrbitalVelocityCMPerSec =
			Vec3d(Base + 12.0, Base + 13.0, Base + 14.0);
		Body.BPlaneReferenceNormal =
			Vec3d(Base + 15.0, Base + 16.0, Base + 17.0);
		Body.BPlaneFallbackAxis =
			Vec3d(Base + 18.0, Base + 19.0, Base + 20.0);
		Body.BPlaneTargetTCM = Base + 21.0;
		Body.BPlaneTargetRCM = Base + 22.0;
		Body.BPlaneSigmaTCM = Base + 23.0;
		Body.BPlaneSigmaRCM = Base + 24.0;
		Body.BPlaneOuterChiSquared = Base + 25.0;
		Body.AllowedPassSideValue = static_cast<AllowedPassSide>(
			static_cast<std::uint8_t>(Index) + 1u);
		Body.MinimumEnergyChangeCM2PerSec2 = -(Base + 26.0);
		Body.MaximumEnergyChangeCM2PerSec2 = Base + 27.0;
		Body.DebugColor = Color4f{
			0.11f + static_cast<float>(Index) * 0.1f,
			0.22f + static_cast<float>(Index) * 0.1f,
			0.33f + static_cast<float>(Index) * 0.1f,
			0.44f + static_cast<float>(Index) * 0.1f};
	}

	TargetSpec& Target = Request.Scenario.Target;
	Target.TargetId = 919;
	Target.CenterCM = Vec3d(9101.0, 9102.0, 9103.0);
	Target.HitRadiusCM = 9104.0;
	Target.GeometricContactRadiusCM = 9105.0;
	Target.UseSeparateGeometricContactCenter = true;
	Target.GeometricContactCenterCM =
		Vec3d(9106.0, 9107.0, 9108.0);
	Target.RequiredQualifiedAssistCount = 2;
	Target.MinimumQualifyingCorridorQuality = 0.625;
	Target.MinimumQualifyingEnergyGainCM2PerSec2 = 9109.0;
	Target.RequireAllowedPassSide = true;
	Target.PresentationForward = Vec3d(0.25, -0.5, 0.75);

	SolverConfig& Config = Request.Config;
	Config.SolverVersion = 2;
	Config.HashSchemaVersion = 9;
	Config.FixedTimeStepSeconds = 0.0125;
	Config.MaximumSimulationTimeSeconds = 91.25;
	Config.MaximumStepCount = 9125;
	Config.MaximumSubdivisionDepth = 5;
	Config.MaximumCoastStepExpansionDepth = 3;
	Config.AssistStepRadiusFraction = 0.125;
	Config.CollisionStepRadiusFraction = 0.375;
	Config.GravityTimescaleFraction = 0.0625;
	Config.PositionErrorLimitCM = 1.25;
	Config.RootBisectionIterations = 17;
	Config.RootAlphaTolerance = 2.5e-9;
	Config.BPlaneBasisMinimumLength = 3.5e-7;
	Config.MinimumVInfinityCMPerSec = 4.5;
	Config.MaximumNaturalDeflectionErrorRadians = 0.45;
	Config.EnergyQualityPower = 3.25;
	Config.EnergyRootEpsilonCM2PerSec2 = 5.5e-5;
	Config.ExitEnergyResidualToleranceCM2PerSec2 = 6.5;
	Config.EnergyShootingIterationCount = 7;
	Config.NaturalCloneMaximumTimeSeconds = 81.5;
	Config.NaturalCloneMaximumStepCount = 8150;
	Config.EnabledAssistMask = 0x5u;

	Request.InitialPositionCM = Vec3d(9201.0, -9202.0, 9203.0);
	Request.InitialVelocityCMPerSec =
		Vec3d(-9301.0, 9302.0, -9303.0);
	Request.InitialTimeSeconds = 94.125;
	Request.InitialExpectedAssistIndex = 3;
	return Request;
}

ABTS::M11Core::TrajectoryResult
ABTS::M11Core::Testing::MakeAllFieldsResultSentinel()
{
	TrajectoryResult Result;
	for (std::int32_t Index = 0; Index < 2; ++Index)
	{
		const double Base = 100.0 + static_cast<double>(Index) * 100.0;
		TrajectoryPoint Point;
		Point.TimeSeconds = Base + 1.0;
		Point.PositionCM = Vec3d(Base + 2.0, Base + 3.0, Base + 4.0);
		Point.VelocityCMPerSec =
			Vec3d(Base + 5.0, Base + 6.0, Base + 7.0);
		Point.PrimarySpecificEnergyCM2PerSec2 = -(Base + 8.0);
		Result.Points.push_back(Point);
	}

	constexpr std::uint8_t EventTypeCount =
		static_cast<std::uint8_t>(
			TrajectoryEventType::TargetContact)
		+ 1u;
	for (std::uint8_t EventValue = 0;
		EventValue < EventTypeCount;
		++EventValue)
	{
		const double Base =
			1000.0 + static_cast<double>(EventValue) * 100.0;
		TrajectoryEvent Event;
		Event.Type = static_cast<TrajectoryEventType>(EventValue);
		Event.BodyId = 510 + static_cast<std::int32_t>(EventValue);
		Event.AssistIndex =
			1 + static_cast<std::int32_t>(EventValue % 3u);
		Event.TimeSeconds = Base + 1.0;
		Event.PositionCM =
			Vec3d(Base + 2.0, Base + 3.0, Base + 4.0);
		Event.VelocityCMPerSec =
			Vec3d(Base + 5.0, Base + 6.0, Base + 7.0);
		Event.EntrySpeedCMPerSec = Base + 8.0;
		Event.ExitSpeedCMPerSec = Base + 9.0;
		Event.ClosestDistanceCM = Base + 10.0;
		Event.BPlaneTCM = -(Base + 11.0);
		Event.BPlaneRCM = Base + 12.0;
		Event.BPlaneChiSquared = Base + 13.0;
		Event.CorridorQuality = Base + 14.0;
		Event.NaturalDeflectionRadians = Base + 15.0;
		Event.IdealDeflectionRadians = Base + 16.0;
		Event.RawEnergyChangeCM2PerSec2 = -(Base + 17.0);
		Event.RequestedEnergyChangeCM2PerSec2 = Base + 18.0;
		Event.AppliedEnergyChangeCM2PerSec2 = Base + 19.0;
		Result.Events.push_back(Event);
	}
	Result.Termination = TrajectoryTermination::PlanetCaptured;
	Result.CompletedAssistCount = 3;
	Result.TargetContactCount = 4;
	Result.ValidationHash = 0xf1e1da21f00dcafeull;
	Result.Diagnostic = "all-fields-result-sentinel";
	return Result;
}

ABTS::M11Core::TrajectoryPacingDiagnostics
ABTS::M11Core::Testing::MakeAllFieldsPacingDiagnosticsSentinel()
{
	TrajectoryPacingDiagnostics Diagnostics;
	Diagnostics.DiagnosticsVersion = 11;
	Diagnostics.StartTimeSeconds = 1.25;
	Diagnostics.EndTimeSeconds = 61.5;
	Diagnostics.TotalFlightTimeSeconds = 60.25;
	Diagnostics.FirstObservedAssistIndex = 1;
	Diagnostics.LastObservedAssistIndex = 3;
	Diagnostics.ObservedAssistCount = 3;
	for (std::size_t Index = 0;
		Index < Diagnostics.Assists.size();
		++Index)
	{
		const double Base = 10.0 + static_cast<double>(Index) * 20.0;
		AssistPhaseDiagnostics& Assist = Diagnostics.Assists[Index];
		Assist.Complete = true;
		Assist.EnterTimeSeconds = Base + 1.0;
		Assist.ClosestTimeSeconds = Base + 2.0;
		Assist.ExitTimeSeconds = Base + 3.0;
		Assist.CoastBeforeEnterSeconds = Base + 4.0;
		Assist.InfluenceDurationSeconds = Base + 5.0;
		Assist.ActualDeflectionRadians = Base + 6.0;
		Assist.NaturalDeflectionRadians = Base + 7.0;
		Assist.EntrySpeedCMPerSec = Base + 8.0;
		Assist.ExitSpeedCMPerSec = Base + 9.0;
		Assist.AppliedEnergyChangeCM2PerSec2 = -(Base + 10.0);
	}
	Diagnostics.TargetHit = true;
	Diagnostics.TargetHitTimeSeconds = 58.75;
	Diagnostics.FinalCoastSeconds = 4.125;
	Diagnostics.TotalCoastSeconds = 21.25;
	Diagnostics.TotalInfluenceDurationSeconds = 15.75;
	Diagnostics.MaximumCoastSeconds = 9.5;
	Diagnostics.MaximumInfluenceDurationSeconds = 6.25;
	return Diagnostics;
}

std::uint64_t ABTS::M11Core::Testing::ComputeRequestIdentity(
	const TrajectoryRequest& Request)
{
	return ConformanceDetail::HashRequestIdentity(Request);
}

std::vector<ABTS::M11Core::Testing::CorpusCaseDefinition>
ABTS::M11Core::Testing::MakePortableConformanceCorpus()
{
	std::vector<CorpusCaseDefinition> Cases;
	Cases.reserve(PortableCorpusCaseCount);

	const auto AddCase =
		[&Cases](
			const CorpusCaseId Id,
			const char* Name,
			TrajectoryRequest Request,
			const std::uint64_t ExpectedRequestIdentity,
			const std::uint64_t ExpectedResultHash,
			const TrajectoryTermination ExpectedTermination,
			const std::int32_t ExpectedPointCount,
			const std::int32_t ExpectedEventCount,
			const std::int32_t ExpectedCompletedAssistCount)
		{
			CorpusCaseDefinition Definition;
			Definition.Id = Id;
			Definition.Name = Name;
			Definition.Request = std::move(Request);
			Definition.ExpectedRequestIdentity =
				ExpectedRequestIdentity;
			Definition.ExpectedResultHash = ExpectedResultHash;
			Definition.ExpectedTermination = ExpectedTermination;
			Definition.ExpectedPointCount = ExpectedPointCount;
			Definition.ExpectedEventCount = ExpectedEventCount;
			Definition.ExpectedCompletedAssistCount =
				ExpectedCompletedAssistCount;
			Cases.push_back(std::move(Definition));
		};

	AddCase(
		CorpusCaseId::V1GoldenNaturalFlyby,
		"v1_golden_natural_flyby",
		MakeV1GoldenRequest(),
		0xd2f7ab5b610a348bull,
		V1PortableGoldenHash,
		TrajectoryTermination::Timeout,
		7204,
		4,
		1);
	AddCase(
		CorpusCaseId::V2StrongAssist,
		"v2_strong_assist",
		MakeV2StrongAssistRequest(),
		0x9e95960b9747b036ull,
		V2PortableGoldenHash,
		TrajectoryTermination::Timeout,
		18226,
		4,
		1);
	AddCase(
		CorpusCaseId::ThreeAssistNominal,
		"three_assist_nominal",
		ConformanceDetail::MakeThreeAssistRequest(0x7u),
		0xcd2e9a7c57ac8807ull,
		0xb096906e5d2f60a5ull,
		TrajectoryTermination::TargetHit,
		34852,
		10,
		GravityScenario::AssistCount);
	AddCase(
		CorpusCaseId::TargetNearBoundary,
		"target_near_boundary",
		ConformanceDetail::MakeSweptRequest(false),
		0xff1b978c4b1edf67ull,
		0x02b3a602d9ddecb2ull,
		TrajectoryTermination::TargetHit,
		2,
		1,
		0);
	AddCase(
		CorpusCaseId::BodyCollision,
		"body_collision",
		ConformanceDetail::MakeSweptRequest(true),
		0xa2ea310eeac5fa86ull,
		0xfd9944f388b588b6ull,
		TrajectoryTermination::BodyCollision,
		4,
		2,
		0);
	AddCase(
		CorpusCaseId::WrongOrder,
		"wrong_order",
		ConformanceDetail::MakeWrongOrderRequest(),
		0x307627319f0a3213ull,
		0xe3dad9f45c01b923ull,
		TrajectoryTermination::WrongOrder,
		2,
		1,
		0);
	AddCase(
		CorpusCaseId::AblateAssist1,
		"ablate_assist_1",
		ConformanceDetail::MakeThreeAssistRequest(0x6u),
		0x8077ea66dbc95a0cull,
		0x0c47bd74ea9c7978ull,
		TrajectoryTermination::AssistSolveFailed,
		14781,
		5,
		1);
	AddCase(
		CorpusCaseId::AblateAssist2,
		"ablate_assist_2",
		ConformanceDetail::MakeThreeAssistRequest(0x5u),
		0x775097169ec442e5ull,
		0xf9ac04f1fa23d201ull,
		TrajectoryTermination::Timeout,
		37834,
		10,
		3);
	AddCase(
		CorpusCaseId::AblateAssist3,
		"ablate_assist_3",
		ConformanceDetail::MakeThreeAssistRequest(0x3u),
		0xe9a5180d5458a7fbull,
		0x992356b17f3cebc5ull,
		TrajectoryTermination::Timeout,
		36454,
		10,
		3);
	AddCase(
		CorpusCaseId::LateTimeout,
		"late_timeout",
		ConformanceDetail::MakeLateTimeoutRequest(),
		0x247a4adb7438bdfbull,
		0x4effe50172aba53aull,
		TrajectoryTermination::Timeout,
		603,
		2,
		0);
	AddCase(
		CorpusCaseId::MacroStepFallback,
		"macro_step_fallback",
		ConformanceDetail::MakeMacroFallbackRequest(),
		0x4ec23df0b63fe01full,
		0x50ff8cabd3124f34ull,
		TrajectoryTermination::TargetHit,
		4,
		1,
		0);
	return Cases;
}

bool ABTS::M11Core::Testing::RequestsExactlyEqual(
	const TrajectoryRequest& A,
	const TrajectoryRequest& B)
{
	using namespace ConformanceDetail;
	if (A.Scenario.LayoutVersion != B.Scenario.LayoutVersion
		|| A.Scenario.ScenarioHash != B.Scenario.ScenarioHash
		|| !ExactTarget(A.Scenario.Target, B.Scenario.Target)
		|| !ExactConfig(A.Config, B.Config)
		|| !ExactVector(A.InitialPositionCM, B.InitialPositionCM)
		|| !ExactVector(
			A.InitialVelocityCMPerSec,
			B.InitialVelocityCMPerSec)
		|| !ExactDouble(A.InitialTimeSeconds, B.InitialTimeSeconds)
		|| A.InitialExpectedAssistIndex != B.InitialExpectedAssistIndex)
	{
		return false;
	}
	for (std::size_t BodyIndex = 0;
		BodyIndex < A.Scenario.Bodies.size();
		++BodyIndex)
	{
		if (!ExactBody(
				A.Scenario.Bodies[BodyIndex],
				B.Scenario.Bodies[BodyIndex]))
		{
			return false;
		}
	}
	return true;
}

bool ABTS::M11Core::Testing::ResultsExactlyEqual(
	const TrajectoryResult& A,
	const TrajectoryResult& B)
{
	using namespace ConformanceDetail;
	if (A.ValidationHash != B.ValidationHash
		|| A.Termination != B.Termination
		|| A.CompletedAssistCount != B.CompletedAssistCount
		|| A.TargetContactCount != B.TargetContactCount
		|| A.Diagnostic != B.Diagnostic
		|| A.Points.size() != B.Points.size()
		|| A.Events.size() != B.Events.size())
	{
		return false;
	}
	for (std::size_t Index = 0; Index < A.Points.size(); ++Index)
	{
		const TrajectoryPoint& Left = A.Points[Index];
		const TrajectoryPoint& Right = B.Points[Index];
		if (!ExactDouble(Left.TimeSeconds, Right.TimeSeconds)
			|| !ExactVector(Left.PositionCM, Right.PositionCM)
			|| !ExactVector(
				Left.VelocityCMPerSec,
				Right.VelocityCMPerSec)
			|| !ExactDouble(
				Left.PrimarySpecificEnergyCM2PerSec2,
				Right.PrimarySpecificEnergyCM2PerSec2))
		{
			return false;
		}
	}
	for (std::size_t Index = 0; Index < A.Events.size(); ++Index)
	{
		const TrajectoryEvent& Left = A.Events[Index];
		const TrajectoryEvent& Right = B.Events[Index];
		if (Left.Type != Right.Type
			|| Left.BodyId != Right.BodyId
			|| Left.AssistIndex != Right.AssistIndex
			|| !ExactDouble(Left.TimeSeconds, Right.TimeSeconds)
			|| !ExactVector(Left.PositionCM, Right.PositionCM)
			|| !ExactVector(
				Left.VelocityCMPerSec,
				Right.VelocityCMPerSec)
			|| !ExactDouble(
				Left.EntrySpeedCMPerSec,
				Right.EntrySpeedCMPerSec)
			|| !ExactDouble(
				Left.ExitSpeedCMPerSec,
				Right.ExitSpeedCMPerSec)
			|| !ExactDouble(
				Left.ClosestDistanceCM,
				Right.ClosestDistanceCM)
			|| !ExactDouble(Left.BPlaneTCM, Right.BPlaneTCM)
			|| !ExactDouble(Left.BPlaneRCM, Right.BPlaneRCM)
			|| !ExactDouble(
				Left.BPlaneChiSquared,
				Right.BPlaneChiSquared)
			|| !ExactDouble(
				Left.CorridorQuality,
				Right.CorridorQuality)
			|| !ExactDouble(
				Left.NaturalDeflectionRadians,
				Right.NaturalDeflectionRadians)
			|| !ExactDouble(
				Left.IdealDeflectionRadians,
				Right.IdealDeflectionRadians)
			|| !ExactDouble(
				Left.RawEnergyChangeCM2PerSec2,
				Right.RawEnergyChangeCM2PerSec2)
			|| !ExactDouble(
				Left.RequestedEnergyChangeCM2PerSec2,
				Right.RequestedEnergyChangeCM2PerSec2)
			|| !ExactDouble(
				Left.AppliedEnergyChangeCM2PerSec2,
				Right.AppliedEnergyChangeCM2PerSec2))
		{
			return false;
		}
	}
	return true;
}

bool ABTS::M11Core::Testing::PacingDiagnosticsExactlyEqual(
	const TrajectoryPacingDiagnostics& A,
	const TrajectoryPacingDiagnostics& B)
{
	using namespace ConformanceDetail;
	if (A.DiagnosticsVersion != B.DiagnosticsVersion
		|| !ExactDouble(A.StartTimeSeconds, B.StartTimeSeconds)
		|| !ExactDouble(A.EndTimeSeconds, B.EndTimeSeconds)
		|| !ExactDouble(
			A.TotalFlightTimeSeconds,
			B.TotalFlightTimeSeconds)
		|| A.FirstObservedAssistIndex
			!= B.FirstObservedAssistIndex
		|| A.LastObservedAssistIndex
			!= B.LastObservedAssistIndex
		|| A.ObservedAssistCount != B.ObservedAssistCount
		|| A.TargetHit != B.TargetHit
		|| !ExactDouble(
			A.TargetHitTimeSeconds,
			B.TargetHitTimeSeconds)
		|| !ExactDouble(A.FinalCoastSeconds, B.FinalCoastSeconds)
		|| !ExactDouble(A.TotalCoastSeconds, B.TotalCoastSeconds)
		|| !ExactDouble(
			A.TotalInfluenceDurationSeconds,
			B.TotalInfluenceDurationSeconds)
		|| !ExactDouble(
			A.MaximumCoastSeconds,
			B.MaximumCoastSeconds)
		|| !ExactDouble(
			A.MaximumInfluenceDurationSeconds,
			B.MaximumInfluenceDurationSeconds))
	{
		return false;
	}
	for (std::size_t Index = 0; Index < A.Assists.size(); ++Index)
	{
		const AssistPhaseDiagnostics& Left = A.Assists[Index];
		const AssistPhaseDiagnostics& Right = B.Assists[Index];
		if (Left.Complete != Right.Complete
			|| !ExactDouble(
				Left.EnterTimeSeconds,
				Right.EnterTimeSeconds)
			|| !ExactDouble(
				Left.ClosestTimeSeconds,
				Right.ClosestTimeSeconds)
			|| !ExactDouble(
				Left.ExitTimeSeconds,
				Right.ExitTimeSeconds)
			|| !ExactDouble(
				Left.CoastBeforeEnterSeconds,
				Right.CoastBeforeEnterSeconds)
			|| !ExactDouble(
				Left.InfluenceDurationSeconds,
				Right.InfluenceDurationSeconds)
			|| !ExactDouble(
				Left.ActualDeflectionRadians,
				Right.ActualDeflectionRadians)
			|| !ExactDouble(
				Left.NaturalDeflectionRadians,
				Right.NaturalDeflectionRadians)
			|| !ExactDouble(
				Left.EntrySpeedCMPerSec,
				Right.EntrySpeedCMPerSec)
			|| !ExactDouble(
				Left.ExitSpeedCMPerSec,
				Right.ExitSpeedCMPerSec)
			|| !ExactDouble(
				Left.AppliedEnergyChangeCM2PerSec2,
				Right.AppliedEnergyChangeCM2PerSec2))
		{
			return false;
		}
	}
	return true;
}

bool ABTS::M11Core::Testing::RunPortableConformance(
	ConformanceReport& OutReport,
	std::string* OutFailure)
{
	using namespace ConformanceDetail;
	OutReport = ConformanceReport();
	const std::vector<CorpusCaseDefinition> Definitions =
		MakePortableConformanceCorpus();
	constexpr std::uint32_t FirstCorpusCaseValue =
		static_cast<std::uint32_t>(
			CorpusCaseId::V1GoldenNaturalFlyby);
	constexpr std::uint32_t CorpusCountValue =
		static_cast<std::uint32_t>(CorpusCaseId::Count);
	static_assert(
		CorpusCountValue - FirstCorpusCaseValue
			== static_cast<std::uint32_t>(
				PortableCorpusCaseCount),
		"Portable corpus ID range and frozen case count diverged.");
	if (Definitions.size()
		!= static_cast<std::size_t>(PortableCorpusCaseCount))
	{
		return Reject(
			OutReport,
			OutFailure,
			"PortableCorpusCaseCountMismatch");
	}
	for (std::size_t CaseIndex = 0;
		CaseIndex < Definitions.size();
		++CaseIndex)
	{
		const CorpusCaseDefinition& Definition =
			Definitions[CaseIndex];
		const std::uint32_t ExpectedId =
			FirstCorpusCaseValue
			+ static_cast<std::uint32_t>(CaseIndex);
		if (static_cast<std::uint32_t>(Definition.Id) != ExpectedId)
		{
			return Reject(
				OutReport,
				OutFailure,
				"PortableCorpusCaseIdSequenceMismatch");
		}
		if (Definition.Name.empty()
			|| Definition.ExpectedRequestIdentity == 0
			|| Definition.ExpectedResultHash == 0
			|| Definition.ExpectedTermination
				== TrajectoryTermination::None
			|| Definition.ExpectedPointCount < 0
			|| Definition.ExpectedEventCount < 0
			|| Definition.ExpectedCompletedAssistCount < 0)
		{
			return Reject(
				OutReport,
				OutFailure,
				"PortableCorpusExpectationPlaceholder");
		}
	}

	std::vector<TrajectoryResult> BaselineResults;
	BaselineResults.resize(Definitions.size());
	OutReport.Cases.reserve(Definitions.size());
	bool AllRepeated = true;
	bool AllExpectations = true;
	bool CorpusIdentityIsValid = true;

	for (std::size_t CaseIndex = 0;
		CaseIndex < Definitions.size();
		++CaseIndex)
	{
		const CorpusCaseDefinition& Definition =
			Definitions[CaseIndex];
		CorpusCaseReport CaseReport;
		CaseReport.Id = Definition.Id;
		CaseReport.Name = Definition.Name;
		CaseReport.RequestIdentity =
			ComputeRequestIdentity(Definition.Request);
		bool CaseIdentityIsValid = true;

		if (CaseIndex > 0
			&& static_cast<std::uint32_t>(
				Definitions[CaseIndex - 1].Id)
				>= static_cast<std::uint32_t>(Definition.Id))
		{
			CorpusIdentityIsValid = false;
			CaseIdentityIsValid = false;
			CaseReport.Diagnostic =
				"NonCanonicalCorpusCaseOrder";
		}
		for (std::size_t PreviousIndex = 0;
			PreviousIndex < CaseIndex;
			++PreviousIndex)
		{
			const CorpusCaseDefinition& Previous =
				Definitions[PreviousIndex];
			if (Previous.Id == Definition.Id
				|| Previous.Name == Definition.Name)
			{
				CorpusIdentityIsValid = false;
				CaseIdentityIsValid = false;
				CaseReport.Diagnostic =
					"DuplicateCorpusCaseIdentity";
			}
		}

		std::string Failure;
		CaseReport.RepeatedResultMatch =
			SolveRepeatedly(
				Definition.Request,
				BaselineResults[CaseIndex],
				Failure);
		AllRepeated =
			AllRepeated && CaseReport.RepeatedResultMatch;

		const TrajectoryResult& Result =
			BaselineResults[CaseIndex];
		CaseReport.ResultHash = Result.ValidationHash;
		CaseReport.Termination = Result.Termination;
		CaseReport.PointCount =
			static_cast<std::int32_t>(Result.Points.size());
		CaseReport.EventCount =
			static_cast<std::int32_t>(Result.Events.size());
		CaseReport.CompletedAssistCount =
			Result.CompletedAssistCount;

		const bool RequestIdentityMatches =
			Definition.ExpectedRequestIdentity
				== CaseReport.RequestIdentity;
		const bool ResultHashMatches =
			Definition.ExpectedResultHash
				== CaseReport.ResultHash;
		const bool TerminationMatches =
			Definition.ExpectedTermination
				== CaseReport.Termination;
		const bool PointCountMatches =
			Definition.ExpectedPointCount
				== CaseReport.PointCount;
		const bool EventCountMatches =
			Definition.ExpectedEventCount
				== CaseReport.EventCount;
		const bool AssistCountMatches =
			Definition.ExpectedCompletedAssistCount
				== CaseReport.CompletedAssistCount;
		CaseReport.ExpectedOutcomeMatch =
			CaseIdentityIsValid
			&& RequestIdentityMatches
			&& ResultHashMatches
			&& TerminationMatches
			&& PointCountMatches
			&& EventCountMatches
			&& AssistCountMatches;
		AllExpectations =
			AllExpectations && CaseReport.ExpectedOutcomeMatch;

		if (!CaseReport.RepeatedResultMatch)
		{
			CaseReport.Diagnostic =
				Failure.empty()
					? "RepeatedSolveMismatch"
					: "RepeatedSolveFailed:" + Failure;
		}
		else if (!CaseReport.ExpectedOutcomeMatch)
		{
			CaseReport.Diagnostic =
				"FrozenCaseExpectationMismatch";
		}
		else if (CaseReport.Diagnostic.empty())
		{
			CaseReport.Diagnostic = "PendingParallelVerification";
		}
		OutReport.Cases.push_back(std::move(CaseReport));
	}

	OutReport.RepeatedResultsMatch = AllRepeated;
	OutReport.AllCaseExpectationsMatch = AllExpectations;

	std::vector<std::future<TrajectoryResult>> ParallelFutures;
	ParallelFutures.reserve(Definitions.size());
	for (const CorpusCaseDefinition& Definition : Definitions)
	{
		ParallelFutures.push_back(std::async(
			std::launch::async,
			[Request = Definition.Request]()
			{
				TrajectoryResult Result;
				std::string LocalFailure;
				const bool Solved = GravityAssistSolver::Solve(
					Request,
					Result,
					&LocalFailure);
				if (!Solved)
				{
					Result.Diagnostic =
						"ParallelSolveFailed:" + LocalFailure;
				}
				return Result;
			}));
	}

	bool AllParallel = true;
	for (std::size_t CaseIndex = 0;
		CaseIndex < Definitions.size();
		++CaseIndex)
	{
		const TrajectoryResult ParallelResult =
			ParallelFutures[CaseIndex].get();
		CorpusCaseReport& CaseReport =
			OutReport.Cases[CaseIndex];
		CaseReport.ParallelResultMatch =
			ResultsExactlyEqual(
				BaselineResults[CaseIndex],
				ParallelResult);
		AllParallel =
			AllParallel && CaseReport.ParallelResultMatch;
		CaseReport.Passed =
			CaseReport.ExpectedOutcomeMatch
			&& CaseReport.RepeatedResultMatch
			&& CaseReport.ParallelResultMatch;
		if (!CaseReport.ParallelResultMatch)
		{
			CaseReport.Diagnostic = "ParallelSolveMismatch";
		}
		else if (CaseReport.Passed)
		{
			CaseReport.Diagnostic =
				"PortableCorpusCasePassed";
		}
	}
	OutReport.ParallelResultsMatch = AllParallel;
	OutReport.CorpusAggregateHash =
		BuildAggregateHash(OutReport.Cases);

	if (OutReport.Cases.size() < 2
		|| OutReport.Cases[0].Id
			!= CorpusCaseId::V1GoldenNaturalFlyby
		|| OutReport.Cases[1].Id
			!= CorpusCaseId::V2StrongAssist)
	{
		return Reject(
			OutReport,
			OutFailure,
			"PortableLegacyGoldenCasesMissing");
	}
	const CorpusCaseReport& V1Report = OutReport.Cases[0];
	const CorpusCaseReport& V2Report = OutReport.Cases[1];
	OutReport.V1ValidationHash = V1Report.ResultHash;
	OutReport.V1PointCount = V1Report.PointCount;
	OutReport.V1EventCount = V1Report.EventCount;
	OutReport.V1Termination = V1Report.Termination;
	OutReport.V2ValidationHash = V2Report.ResultHash;
	OutReport.V2PointCount = V2Report.PointCount;
	OutReport.V2EventCount = V2Report.EventCount;
	OutReport.V2Termination = V2Report.Termination;

	if (OutReport.V1ValidationHash != V1PortableGoldenHash)
	{
		return Reject(
			OutReport,
			OutFailure,
			"V1PortableGoldenMismatch");
	}
	if (OutReport.V2ValidationHash != V2PortableGoldenHash)
	{
		return Reject(
			OutReport,
			OutFailure,
			"V2PortableGoldenMismatch");
	}

	const auto InvalidCaseFailsClosed =
		[](const TrajectoryRequest& InvalidRequest)
		{
			TrajectoryResult InvalidResult =
				MakeAllFieldsResultSentinel();
			std::string InvalidFailure;
			const bool InvalidSolved = GravityAssistSolver::Solve(
				InvalidRequest,
				InvalidResult,
				&InvalidFailure);
			return !InvalidSolved
				&& InvalidResult.Termination
					== TrajectoryTermination::InvalidInput
				&& InvalidResult.Points.empty()
				&& InvalidResult.Events.empty()
				&& InvalidResult.CompletedAssistCount == 0
				&& InvalidResult.TargetContactCount == 0
				&& InvalidResult.ValidationHash == 0
				&& !InvalidFailure.empty()
				&& InvalidResult.Diagnostic == InvalidFailure;
		};
	TrajectoryRequest NaNRequest = Definitions[0].Request;
	NaNRequest.InitialPositionCM.X =
		std::numeric_limits<double>::quiet_NaN();
	TrajectoryRequest HighMaskAliasRequest =
		Definitions[0].Request;
	HighMaskAliasRequest.Config.EnabledAssistMask = 0x87u;
	OutReport.InvalidInputFailsClosed =
		InvalidCaseFailsClosed(NaNRequest)
		&& InvalidCaseFailsClosed(HighMaskAliasRequest);
	if (!OutReport.InvalidInputFailsClosed)
	{
		return Reject(
			OutReport,
			OutFailure,
			"InvalidInputDidNotFailClosed");
	}
	if (!CorpusIdentityIsValid)
	{
		return Reject(
			OutReport,
			OutFailure,
			"CorpusIdentityMismatch");
	}
	if (!OutReport.RepeatedResultsMatch)
	{
		return Reject(
			OutReport,
			OutFailure,
			"CorpusRepeatedSolveMismatch");
	}
	if (!OutReport.ParallelResultsMatch)
	{
		return Reject(
			OutReport,
			OutFailure,
			"CorpusParallelSolveMismatch");
	}
	if (!OutReport.AllCaseExpectationsMatch)
	{
		return Reject(
			OutReport,
			OutFailure,
			"CorpusExpectationMismatch");
	}

	OutReport.Passed = true;
	OutReport.Diagnostic = "PortableCoreConformancePassed";
	if (OutFailure != nullptr)
	{
		OutFailure->clear();
	}
	return true;
}
