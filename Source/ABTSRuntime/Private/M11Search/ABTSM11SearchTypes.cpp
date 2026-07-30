// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11SearchTypes.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace ABTS::M11Search::TypesPrivate
{
	using namespace M11Core;

	bool Reject(std::string* OutFailure, const char* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool IsFiniteVector(const Vec3d& Value)
	{
		return std::isfinite(Value.X)
			&& std::isfinite(Value.Y)
			&& std::isfinite(Value.Z);
	}

	class CanonicalHash final
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

		void AddInt32(const std::int32_t Value)
		{
			AddUInt32(static_cast<std::uint32_t>(Value));
		}

		void AddUInt32(const std::uint32_t Value)
		{
			for (std::int32_t ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				AddByte(static_cast<std::uint8_t>(
					Value >> (ByteIndex * 8)));
			}
		}

		void AddUInt64(const std::uint64_t Value)
		{
			for (std::int32_t ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				AddByte(static_cast<std::uint8_t>(
					Value >> (ByteIndex * 8)));
			}
		}

		void AddDouble(const double Value)
		{
			const double CanonicalValue = Value == 0.0 ? 0.0 : Value;
			AddUInt64(std::bit_cast<std::uint64_t>(CanonicalValue));
		}

		void AddFloat(const float Value)
		{
			const float CanonicalValue = Value == 0.0f ? 0.0f : Value;
			AddUInt32(std::bit_cast<std::uint32_t>(CanonicalValue));
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
		std::uint64_t Hash = 1469598103934665603ull;
	};

	void AddInput(CanonicalHash& Hash, const LaunchInput& Input)
	{
		Hash.AddDouble(Input.YawDegrees);
		Hash.AddDouble(Input.PitchDegrees);
		Hash.AddDouble(Input.Power);
	}

	void AddLaunch(CanonicalHash& Hash, const LaunchModel& Launch)
	{
		Hash.AddInt32(Launch.Version);
		Hash.AddVector(Launch.PouchLocalPositionCM);
		Hash.AddDouble(Launch.MinimumYawDegrees);
		Hash.AddDouble(Launch.MaximumYawDegrees);
		Hash.AddDouble(Launch.MinimumPitchDegrees);
		Hash.AddDouble(Launch.MaximumPitchDegrees);
		Hash.AddDouble(Launch.MinimumPower);
		Hash.AddDouble(Launch.MaximumPower);
		Hash.AddDouble(Launch.MinimumLaunchSpeedCMPerSec);
		Hash.AddDouble(Launch.MaximumLaunchSpeedCMPerSec);
		Hash.AddDouble(Launch.MaximumSimulationTimeSeconds);
	}

	void AddBody(CanonicalHash& Hash, const GravityBodySpec& Body)
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
		Hash.AddByte(static_cast<std::uint8_t>(
			Body.AllowedPassSideValue));
		Hash.AddDouble(Body.MinimumEnergyChangeCM2PerSec2);
		Hash.AddDouble(Body.MaximumEnergyChangeCM2PerSec2);
		Hash.AddColor(Body.DebugColor);
	}

	void AddTarget(CanonicalHash& Hash, const TargetSpec& Target)
	{
		Hash.AddInt32(Target.TargetId);
		Hash.AddVector(Target.CenterCM);
		Hash.AddDouble(Target.HitRadiusCM);
		Hash.AddDouble(Target.GeometricContactRadiusCM);
		Hash.AddBool(Target.UseSeparateGeometricContactCenter);
		Hash.AddVector(Target.GeometricContactCenterCM);
		Hash.AddInt32(Target.RequiredQualifiedAssistCount);
		Hash.AddDouble(Target.MinimumQualifyingCorridorQuality);
		Hash.AddDouble(Target.MinimumQualifyingEnergyGainCM2PerSec2);
		Hash.AddBool(Target.RequireAllowedPassSide);
		Hash.AddVector(Target.PresentationForward);
	}

	void AddSolver(CanonicalHash& Hash, const SolverConfig& Solver)
	{
		Hash.AddInt32(Solver.SolverVersion);
		Hash.AddInt32(Solver.HashSchemaVersion);
		Hash.AddDouble(Solver.FixedTimeStepSeconds);
		Hash.AddDouble(Solver.MaximumSimulationTimeSeconds);
		Hash.AddInt32(Solver.MaximumStepCount);
		Hash.AddInt32(Solver.MaximumSubdivisionDepth);
		Hash.AddInt32(Solver.MaximumCoastStepExpansionDepth);
		Hash.AddDouble(Solver.AssistStepRadiusFraction);
		Hash.AddDouble(Solver.CollisionStepRadiusFraction);
		Hash.AddDouble(Solver.GravityTimescaleFraction);
		Hash.AddDouble(Solver.PositionErrorLimitCM);
		Hash.AddInt32(Solver.RootBisectionIterations);
		Hash.AddDouble(Solver.RootAlphaTolerance);
		Hash.AddDouble(Solver.BPlaneBasisMinimumLength);
		Hash.AddDouble(Solver.MinimumVInfinityCMPerSec);
		Hash.AddDouble(Solver.MaximumNaturalDeflectionErrorRadians);
		Hash.AddDouble(Solver.EnergyQualityPower);
		Hash.AddDouble(Solver.EnergyRootEpsilonCM2PerSec2);
		Hash.AddDouble(Solver.ExitEnergyResidualToleranceCM2PerSec2);
		Hash.AddInt32(Solver.EnergyShootingIterationCount);
		Hash.AddDouble(Solver.NaturalCloneMaximumTimeSeconds);
		Hash.AddInt32(Solver.NaturalCloneMaximumStepCount);
		Hash.AddByte(Solver.EnabledAssistMask);
	}

	void AddContract(
		CanonicalHash& Hash,
		const CandidateSearchContract& Contract)
	{
		Hash.AddInt32(Contract.ContractVersion);
		Hash.AddInt32(Contract.AlgorithmVersion);
		Hash.AddUInt64(Contract.SearchSeed);
		Hash.AddInt32(Contract.LocalTimeSampleCount);
		Hash.AddInt32(Contract.LocalImpactSampleCount);
		Hash.AddInt32(Contract.LocalRadialSampleCount);
		Hash.AddInt32(Contract.LocalMomentumDirectionSampleCount);
		Hash.AddInt32(Contract.TargetTimeSampleCount);
		Hash.AddInt32(Contract.RobustPreselectionWidth);
		Hash.AddInt32(Contract.MinimumRobustSurvivorCount);
		Hash.AddDouble(Contract.MaximumTotalFlightTimeSeconds);
		Hash.AddDouble(Contract.MaximumCoastSeconds);
		Hash.AddDouble(Contract.MinimumInfluenceDurationSeconds);
		Hash.AddDouble(Contract.MaximumInfluenceDurationSeconds);
		Hash.AddDouble(Contract.MinimumDeflectionRadians);
		Hash.AddDouble(Contract.MinimumEnergyGainCM2PerSec2);
		Hash.AddDouble(Contract.MinimumCorridorQuality);
		Hash.AddDouble(Contract.MinimumLayoutTurnRadians);
		Hash.AddDouble(Contract.MinimumLateralTurnAxisProjection);
		Hash.AddDouble(Contract.MinimumBodyClearanceCM);
		Hash.AddDouble(Contract.LowPowerProbe);
		Hash.AddDouble(Contract.RobustYawStepDegrees);
		Hash.AddDouble(Contract.RobustPitchStepDegrees);
		Hash.AddDouble(Contract.RobustPowerStep);
		Hash.AddUInt64(Contract.MonteCarloSeed);
		Hash.AddInt32(Contract.MonteCarloSampleCount);
		Hash.AddUInt64(Contract.ScreenAimSeed);
		Hash.AddInt32(Contract.ScreenAimSampleCount);
		Hash.AddDouble(Contract.MinimumPrefixRetentionRatio);
		Hash.AddDouble(Contract.MaximumPrefixRetentionRatio);
		Hash.AddDouble(
			Contract.FullScoreMinimumPrefixRetentionRatio);
		Hash.AddDouble(
			Contract.FullScoreMaximumPrefixRetentionRatio);
		Hash.AddInt32(Contract.ConditionalProbeSamplesPerSet);
		Hash.AddDouble(Contract.ConditionalYawHalfExtentDegrees);
		Hash.AddDouble(Contract.ConditionalPitchHalfExtentDegrees);
		Hash.AddDouble(Contract.ConditionalPowerHalfExtent);
		Hash.AddInt32(Contract.MinimumHullEvidenceCount);
		Hash.AddDouble(Contract.MinimumHullAreaSquareDegrees);
		Hash.AddDouble(Contract.MinimumHullYawSpanDegrees);
		Hash.AddDouble(Contract.MinimumHullPitchSpanDegrees);
		Hash.AddDouble(Contract.FirstEncounterMinimumSeconds);
		Hash.AddDouble(Contract.FirstEncounterMaximumSeconds);
		Hash.AddDouble(Contract.InterEncounterCoastMinimumSeconds);
		Hash.AddDouble(Contract.InterEncounterCoastMaximumSeconds);
		Hash.AddDouble(Contract.TargetCoastMinimumSeconds);
		Hash.AddDouble(Contract.TargetCoastMaximumSeconds);
		Hash.AddDouble(Contract.MinimumTargetHitRadiusCM);
		Hash.AddDouble(Contract.MaximumTargetHitRadiusCM);
		Hash.AddDouble(Contract.TargetCoverageMarginCM);
		Hash.AddDouble(Contract.MinimumInfluenceRadiusCM);
		Hash.AddDouble(Contract.MaximumInfluenceRadiusCM);
		Hash.AddDouble(Contract.MinimumVirtualMomentumSpeedCMPerSec);
		Hash.AddDouble(Contract.MaximumVirtualMomentumSpeedCMPerSec);
		Hash.AddDouble(Contract.MinimumGravityScale);
		Hash.AddDouble(Contract.MaximumGravityScale);
		Hash.AddInt32(Contract.MinimumAlternatingLateralTurnCount);
		Hash.AddInt32(Contract.RequestedCandidateCount);
		Hash.AddDouble(Contract.MinimumDiversityDistanceCM);
	}

	std::uint64_t FoldScenarioHash(const std::uint64_t Value)
	{
		std::uint32_t Folded = static_cast<std::uint32_t>(Value)
			^ static_cast<std::uint32_t>(Value >> 32);
		if (Folded == 0)
		{
			Folded = 0x11b21001u;
		}
		return Folded;
	}
}

bool ABTS::M11Search::LaunchInput::IsFinite() const
{
	return std::isfinite(YawDegrees)
		&& std::isfinite(PitchDegrees)
		&& std::isfinite(Power);
}

bool ABTS::M11Search::LaunchModel::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if (Version != 1)
	{
		return Reject(OutFailure, "UnsupportedLaunchModelVersion");
	}
	if (!IsFiniteVector(PouchLocalPositionCM)
		|| !std::isfinite(MinimumYawDegrees)
		|| !std::isfinite(MaximumYawDegrees)
		|| !std::isfinite(MinimumPitchDegrees)
		|| !std::isfinite(MaximumPitchDegrees)
		|| !std::isfinite(MinimumPower)
		|| !std::isfinite(MaximumPower)
		|| MinimumYawDegrees >= MaximumYawDegrees
		|| MinimumPitchDegrees >= MaximumPitchDegrees
		|| MinimumPower >= MaximumPower)
	{
		return Reject(OutFailure, "InvalidLaunchInputDomain");
	}
	if (!std::isfinite(MinimumLaunchSpeedCMPerSec)
		|| !std::isfinite(MaximumLaunchSpeedCMPerSec)
		|| !std::isfinite(MaximumSimulationTimeSeconds)
		|| MinimumLaunchSpeedCMPerSec <= 0.0
		|| MinimumLaunchSpeedCMPerSec >= MaximumLaunchSpeedCMPerSec
		|| MaximumSimulationTimeSeconds <= 0.0)
	{
		return Reject(OutFailure, "InvalidLaunchSpeedOrTime");
	}
	return true;
}

bool ABTS::M11Search::LaunchModel::Contains(
	const LaunchInput& Input) const
{
	return Input.IsFinite()
		&& Input.YawDegrees >= MinimumYawDegrees
		&& Input.YawDegrees <= MaximumYawDegrees
		&& Input.PitchDegrees >= MinimumPitchDegrees
		&& Input.PitchDegrees <= MaximumPitchDegrees
		&& Input.Power >= MinimumPower
		&& Input.Power <= MaximumPower;
}

ABTS::M11Core::Vec3d ABTS::M11Search::LaunchModel::MapDirection(
	const LaunchInput& Input) const
{
	constexpr double DegreesToRadians =
		3.14159265358979323846264338327950288 / 180.0;
	const double YawRadians = Input.YawDegrees * DegreesToRadians;
	const double PitchRadians = Input.PitchDegrees * DegreesToRadians;
	const double CosPitch = std::cos(PitchRadians);
	return M11Core::Vec3d(
		CosPitch * std::cos(YawRadians),
		CosPitch * std::sin(YawRadians),
		std::sin(PitchRadians)).GetSafeNormal();
}

double ABTS::M11Search::LaunchModel::MapSpeedCMPerSec(
	const LaunchInput& Input) const
{
	const double Alpha = std::clamp(
		(Input.Power - MinimumPower) / (MaximumPower - MinimumPower),
		0.0,
		1.0);
	return M11Core::Lerp(
		MinimumLaunchSpeedCMPerSec,
		MaximumLaunchSpeedCMPerSec,
		Alpha);
}

bool ABTS::M11Search::LaunchModel::ApplyToRequest(
	const LaunchInput& Input,
	M11Core::TrajectoryRequest& InOutRequest,
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if (!IsValid(OutFailure) || !Contains(Input))
	{
		return Reject(OutFailure, "LaunchInputOutsideSearchDomain");
	}
	InOutRequest.InitialPositionCM = PouchLocalPositionCM;
	InOutRequest.InitialVelocityCMPerSec =
		MapDirection(Input) * MapSpeedCMPerSec(Input);
	InOutRequest.InitialTimeSeconds = 0.0;
	InOutRequest.InitialExpectedAssistIndex = 1;
	InOutRequest.Config.MaximumSimulationTimeSeconds =
		MaximumSimulationTimeSeconds;
	return true;
}

bool ABTS::M11Search::CandidateLayout::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if (LayoutVersion != 2)
	{
		return Reject(OutFailure, "UnsupportedCandidateLayoutVersion");
	}
	if (!Launch.IsValid(OutFailure)
		|| !Launch.Contains(NominalInput)
		|| !Scenario.IsValid(OutFailure)
		|| !Solver.IsValid(OutFailure))
	{
		return false;
	}
	if (Solver.SolverVersion != 2
		|| Solver.HashSchemaVersion != 2
		|| Solver.MaximumSimulationTimeSeconds
			!= Launch.MaximumSimulationTimeSeconds)
	{
		return Reject(OutFailure, "CandidateRequiresSolverV2");
	}
	return true;
}

bool ABTS::M11Search::CandidateLayout::BuildRequest(
	const LaunchInput& Input,
	const std::uint8_t EnabledAssistMask,
	M11Core::TrajectoryRequest& OutRequest,
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if ((EnabledAssistMask & static_cast<std::uint8_t>(~0x07u)) != 0)
	{
		return Reject(OutFailure, "InvalidAssistMask");
	}
	if (!IsValid(OutFailure))
	{
		return false;
	}
	OutRequest = M11Core::TrajectoryRequest();
	OutRequest.Scenario = Scenario;
	OutRequest.Config = Solver;
	OutRequest.Config.EnabledAssistMask = EnabledAssistMask;
	return Launch.ApplyToRequest(Input, OutRequest, OutFailure)
		&& OutRequest.IsValid(OutFailure);
}

ABTS::M11Search::CandidateSearchContract
ABTS::M11Search::CandidateSearchContract::MakeV2_1()
{
	return CandidateSearchContract();
}

bool ABTS::M11Search::CandidateSearchContract::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if (ContractVersion != SearchContractVersion
		|| AlgorithmVersion != SearchAlgorithmVersion
		|| SearchSeed == 0
		|| LocalTimeSampleCount < 1
		|| LocalTimeSampleCount > 9
		|| LocalImpactSampleCount < 1
		|| LocalImpactSampleCount > 9
		|| LocalRadialSampleCount < 1
		|| LocalRadialSampleCount > 9
		|| LocalMomentumDirectionSampleCount < 1
		|| LocalMomentumDirectionSampleCount > 9
		|| TargetTimeSampleCount < 1
		|| TargetTimeSampleCount > 17
		|| RobustPreselectionWidth < 1
		|| RobustPreselectionWidth > 128
		|| MinimumRobustSurvivorCount < 1
		|| MinimumRobustSurvivorCount > 7
		|| MonteCarloSeed == 0
		|| MonteCarloSampleCount != 5000
		|| ScreenAimSeed == 0
		|| ScreenAimSampleCount != 5000
		|| ConditionalProbeSamplesPerSet < 0
		|| ConditionalProbeSamplesPerSet > 4096
		|| MinimumHullEvidenceCount < 3
		|| MinimumHullEvidenceCount
			> ScreenAimSampleCount
		|| MinimumAlternatingLateralTurnCount < 0
		|| MinimumAlternatingLateralTurnCount > 2
		|| RequestedCandidateCount < 1
		|| RequestedCandidateCount > 32)
	{
		return Reject(OutFailure, "InvalidSearchContractInteger");
	}
	const std::array<double, 40> Values{
		MaximumTotalFlightTimeSeconds,
		MaximumCoastSeconds,
		MinimumInfluenceDurationSeconds,
		MaximumInfluenceDurationSeconds,
		MinimumDeflectionRadians,
		MinimumEnergyGainCM2PerSec2,
		MinimumCorridorQuality,
		MinimumLayoutTurnRadians,
		MinimumLateralTurnAxisProjection,
		MinimumBodyClearanceCM,
		LowPowerProbe,
		RobustYawStepDegrees,
		RobustPitchStepDegrees,
		RobustPowerStep,
		MinimumPrefixRetentionRatio,
		MaximumPrefixRetentionRatio,
		FullScoreMinimumPrefixRetentionRatio,
		FullScoreMaximumPrefixRetentionRatio,
		ConditionalYawHalfExtentDegrees,
		ConditionalPitchHalfExtentDegrees,
		ConditionalPowerHalfExtent,
		MinimumHullAreaSquareDegrees,
		MinimumHullYawSpanDegrees,
		MinimumHullPitchSpanDegrees,
		FirstEncounterMinimumSeconds,
		FirstEncounterMaximumSeconds,
		InterEncounterCoastMinimumSeconds,
		InterEncounterCoastMaximumSeconds,
		TargetCoastMinimumSeconds,
		TargetCoastMaximumSeconds,
		MinimumTargetHitRadiusCM,
		MaximumTargetHitRadiusCM,
		TargetCoverageMarginCM,
		MinimumInfluenceRadiusCM,
		MaximumInfluenceRadiusCM,
		MinimumVirtualMomentumSpeedCMPerSec,
		MaximumVirtualMomentumSpeedCMPerSec,
		MinimumGravityScale,
		MaximumGravityScale,
		MinimumDiversityDistanceCM};
	if (!std::all_of(
			Values.begin(),
			Values.end(),
			[](const double Value) { return std::isfinite(Value); }))
	{
		return Reject(OutFailure, "NonFiniteSearchContract");
	}
	if (MaximumTotalFlightTimeSeconds <= 0.0
		|| MaximumTotalFlightTimeSeconds > 60.0
		|| MaximumCoastSeconds <= 0.0
		|| MinimumInfluenceDurationSeconds <= 0.0
		|| MinimumInfluenceDurationSeconds
			>= MaximumInfluenceDurationSeconds
		|| MinimumDeflectionRadians <= 0.0
		|| MinimumEnergyGainCM2PerSec2 <= 0.0
		|| MinimumCorridorQuality <= 0.0
		|| MinimumCorridorQuality > 1.0
		|| MinimumLayoutTurnRadians <= 0.0
		|| MinimumLateralTurnAxisProjection <= 0.0
		|| MinimumLateralTurnAxisProjection > 1.0
		|| MinimumBodyClearanceCM < 0.0
		|| LowPowerProbe <= 0.0
		|| LowPowerProbe >= 1.0
		|| RobustYawStepDegrees <= 0.0
		|| RobustPitchStepDegrees <= 0.0
		|| RobustPowerStep <= 0.0
		|| MinimumPrefixRetentionRatio <= 0.0
		|| MinimumPrefixRetentionRatio
			>= FullScoreMinimumPrefixRetentionRatio
		|| FullScoreMinimumPrefixRetentionRatio
			>= FullScoreMaximumPrefixRetentionRatio
		|| FullScoreMaximumPrefixRetentionRatio
			>= MaximumPrefixRetentionRatio
		|| MaximumPrefixRetentionRatio >= 1.0
		|| ConditionalYawHalfExtentDegrees <= 0.0
		|| ConditionalYawHalfExtentDegrees > 18.0
		|| ConditionalPitchHalfExtentDegrees <= 0.0
		|| ConditionalPitchHalfExtentDegrees > 30.0
		|| ConditionalPowerHalfExtent <= 0.0
		|| ConditionalPowerHalfExtent > 0.5
		|| MinimumHullAreaSquareDegrees <= 0.0
		|| MinimumHullYawSpanDegrees <= 0.0
		|| MinimumHullPitchSpanDegrees <= 0.0
		|| FirstEncounterMinimumSeconds <= 0.0
		|| FirstEncounterMinimumSeconds
			>= FirstEncounterMaximumSeconds
		|| InterEncounterCoastMinimumSeconds <= 0.0
		|| InterEncounterCoastMinimumSeconds
			>= InterEncounterCoastMaximumSeconds
		|| TargetCoastMinimumSeconds <= 0.0
		|| TargetCoastMinimumSeconds >= TargetCoastMaximumSeconds
		|| MinimumTargetHitRadiusCM <= 0.0
		|| MinimumTargetHitRadiusCM >= MaximumTargetHitRadiusCM
		|| TargetCoverageMarginCM < 0.0
		|| MinimumInfluenceRadiusCM <= 0.0
		|| MinimumInfluenceRadiusCM >= MaximumInfluenceRadiusCM
		|| MinimumVirtualMomentumSpeedCMPerSec <= 0.0
		|| MinimumVirtualMomentumSpeedCMPerSec
			>= MaximumVirtualMomentumSpeedCMPerSec
		|| MinimumGravityScale <= 0.0
		|| MinimumGravityScale >= MaximumGravityScale
		|| MinimumDiversityDistanceCM < 0.0)
	{
		return Reject(OutFailure, "InvalidSearchContractRange");
	}
	return true;
}

ABTS::M11Search::ParticleBeamSearchContract
ABTS::M11Search::ParticleBeamSearchContract::MakeV4()
{
	return ParticleBeamSearchContract();
}

bool ABTS::M11Search::ParticleBeamSearchContract::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	std::string EvaluationFailure;
	if (ContractVersion != ParticleBeamContractVersion
		|| AlgorithmVersion != ParticleBeamAlgorithmVersion
		|| ConstructionSeed == 0
		|| ExplorationSeed == 0
		|| HoldoutSeed == 0
		|| ExplorationSeed == HoldoutSeed
		|| !EvaluationContract.IsValid(&EvaluationFailure)
		|| RootParameterCount < 2
		|| RootParameterCount > 128
		|| ExplorationSampleCount < 32
		|| ExplorationSampleCount > 4096
		|| GeometryTimeSampleCount < 1
		|| GeometryTimeSampleCount > 9
		|| GeometryRadiusSampleCount < 1
		|| GeometryRadiusSampleCount > 9
		|| GeometryImpactSampleCount < 1
		|| GeometryImpactSampleCount > 9
		|| GeometryRadialSampleCount < 1
		|| GeometryRadialSampleCount > 9
		|| GeometryMomentumSampleCount < 1
		|| GeometryMomentumSampleCount > 9
		|| NominalProposalBudget < 1
		|| NominalProposalBudget > 512
		|| CoarseProposalBudget < 1
		|| CoarseProposalBudget > NominalProposalBudget
		|| RefinementProposalBudget < 1
		|| RefinementProposalBudget > CoarseProposalBudget
		|| CoarseParticleLimit < 4
		|| CoarseParticleLimit > ExplorationSampleCount + 1
		|| BeamWidth < 1
		|| BeamWidth > RefinementProposalBudget
		|| HoldoutSampleCount < 32
		|| HoldoutSampleCount > 4096
		|| MaximumFinalAuditCandidates < 1
		|| MaximumFinalAuditCandidates > BeamWidth
		|| RobustGuardSurvivorCount < 0
		|| RobustGuardSurvivorCount > 2
		|| EvaluationContract.MinimumRobustSurvivorCount
				+ RobustGuardSurvivorCount
			> 6
		|| TargetRefinementTimeSampleCount < 2
		|| TargetRefinementTimeSampleCount > 17)
	{
		return Reject(
			OutFailure,
			EvaluationFailure.empty()
				? "InvalidParticleBeamContractIntegerOrIdentity"
				: ("InvalidParticleBeamEvaluationContract:"
					+ EvaluationFailure).c_str());
	}
	const std::array<double, 18> Values{
		TargetPrefixRetentionRatio,
		ExplorationMinimumRetentionRatio,
		ExplorationMaximumRetentionRatio,
		PreferredMinimumRetentionRatio,
		PreferredMaximumRetentionRatio,
		MinimumBeamDiversityDistanceCM,
		FinalTargetTurnGuardRadians,
		TargetRefinementMaximumCoastSeconds,
		ConstructionInterEncounterCoastMinimumSeconds,
		ConstructionInterEncounterCoastMaximumSeconds,
		IdealMinimumDeflectionRadians,
		IdealMaximumDeflectionRadians,
		IdealMinimumAxisProjection,
		IdealMinimumInfluenceSeconds,
		IdealMaximumInfluenceSeconds,
		IdealMaximumCoastSeconds,
		IdealMaximumFlightSeconds,
		EvaluationContract.MaximumTotalFlightTimeSeconds};
	if (!std::all_of(
			Values.begin(),
			Values.end(),
			[](const double Value) { return std::isfinite(Value); }))
	{
		return Reject(OutFailure, "NonFiniteParticleBeamContract");
	}
	if (ExplorationMinimumRetentionRatio <= 0.0
		|| ExplorationMinimumRetentionRatio
			>= PreferredMinimumRetentionRatio
		|| PreferredMinimumRetentionRatio
			> TargetPrefixRetentionRatio
		|| TargetPrefixRetentionRatio
			> PreferredMaximumRetentionRatio
		|| PreferredMaximumRetentionRatio
			>= ExplorationMaximumRetentionRatio
		|| ExplorationMaximumRetentionRatio >= 1.0
		|| MinimumBeamDiversityDistanceCM < 0.0
		|| FinalTargetTurnGuardRadians < 0.0
		|| FinalTargetTurnGuardRadians > 0.5
		|| TargetRefinementMaximumCoastSeconds
			< EvaluationContract.TargetCoastMaximumSeconds
		|| TargetRefinementMaximumCoastSeconds
			> EvaluationContract.MaximumCoastSeconds
		|| ConstructionInterEncounterCoastMinimumSeconds <= 0.0
		|| ConstructionInterEncounterCoastMinimumSeconds
			>= ConstructionInterEncounterCoastMaximumSeconds
		|| ConstructionInterEncounterCoastMaximumSeconds
			> EvaluationContract.MaximumCoastSeconds
		|| IdealMinimumDeflectionRadians
			< EvaluationContract.MinimumDeflectionRadians
		|| IdealMinimumDeflectionRadians
			>= IdealMaximumDeflectionRadians
		|| IdealMinimumAxisProjection
			< EvaluationContract.MinimumLateralTurnAxisProjection
		|| IdealMinimumAxisProjection > 1.0
		|| IdealMinimumInfluenceSeconds
			< EvaluationContract.MinimumInfluenceDurationSeconds
		|| IdealMinimumInfluenceSeconds
			>= IdealMaximumInfluenceSeconds
		|| IdealMaximumInfluenceSeconds
			> EvaluationContract.MaximumInfluenceDurationSeconds
		|| IdealMaximumCoastSeconds <= 0.0
		|| IdealMaximumCoastSeconds
			> EvaluationContract.MaximumCoastSeconds
		|| IdealMaximumFlightSeconds <= 0.0
		|| IdealMaximumFlightSeconds
			> EvaluationContract.MaximumTotalFlightTimeSeconds)
	{
		return Reject(OutFailure, "InvalidParticleBeamContractRange");
	}
	return true;
}

bool ABTS::M11Search::BatchRequest::IsValid(
	std::string* OutFailure) const
{
	using namespace TypesPrivate;
	if (GlobalWorkItemCount == 0
		|| GlobalWorkItemCount > 100000000ull
		|| ShardCount == 0
		|| ShardIndex >= ShardCount
		|| ThreadCount == 0
		|| ThreadCount > 256
		|| RequestedTopCandidateCount == 0
		|| RequestedTopCandidateCount > 64
		|| LocalBeginOffset > GlobalWorkItemCount)
	{
		return Reject(OutFailure, "InvalidBatchRequest");
	}
	return true;
}

const char* ABTS::M11Search::ToString(const EvaluationStatus Status)
{
	switch (Status)
	{
	case EvaluationStatus::Accepted:
		return "Accepted";
	case EvaluationStatus::InvalidContract:
		return "InvalidContract";
	case EvaluationStatus::InitialArcFailed:
		return "InitialArcFailed";
	case EvaluationStatus::Assist1ConstructionFailed:
		return "Assist1ConstructionFailed";
	case EvaluationStatus::Assist2ConstructionFailed:
		return "Assist2ConstructionFailed";
	case EvaluationStatus::Assist3ConstructionFailed:
		return "Assist3ConstructionFailed";
	case EvaluationStatus::TargetConstructionFailed:
		return "TargetConstructionFailed";
	case EvaluationStatus::NominalRejected:
		return "NominalRejected";
	case EvaluationStatus::PacingRejected:
		return "PacingRejected";
	case EvaluationStatus::LowPowerGateRejected:
		return "LowPowerGateRejected";
	case EvaluationStatus::RobustnessRejected:
		return "RobustnessRejected";
	case EvaluationStatus::AblationRejected:
		return "AblationRejected";
	case EvaluationStatus::InputDomainDegenerate:
		return "InputDomainDegenerate";
	case EvaluationStatus::InternalError:
		return "InternalError";
	default:
		return "Unknown";
	}
}

std::uint64_t ABTS::M11Search::ComputeCandidateSourceHash(
	const CandidateLayout& Layout,
	const CandidateSearchContract& Contract)
{
	using namespace TypesPrivate;
	CanonicalHash Hash;
	Hash.AddUInt32(0x11b21001u);
	Hash.AddInt32(CandidateManifestVersion);
	Hash.AddInt32(Layout.LayoutVersion);
	AddLaunch(Hash, Layout.Launch);
	AddInput(Hash, Layout.NominalInput);
	Hash.AddInt32(Layout.Scenario.LayoutVersion);
	for (const M11Core::GravityBodySpec& Body : Layout.Scenario.Bodies)
	{
		AddBody(Hash, Body);
	}
	AddTarget(Hash, Layout.Scenario.Target);
	AddSolver(Hash, Layout.Solver);
	AddContract(Hash, Contract);
	return Hash.Get();
}

std::uint64_t ABTS::M11Search::ComputeCandidateSearchContractHash(
	const CandidateSearchContract& Contract)
{
	using namespace TypesPrivate;
	CanonicalHash Hash;
	Hash.AddUInt32(0x11b21004u);
	AddContract(Hash, Contract);
	return Hash.Get();
}

std::uint64_t ABTS::M11Search::ComputeCandidateScoreHash(
	const CandidateRecord& Candidate)
{
	using namespace TypesPrivate;
	CanonicalHash Hash;
	Hash.AddUInt32(0x11b21002u);
	Hash.AddUInt64(Candidate.GlobalWorkIndex);
	Hash.AddByte(static_cast<std::uint8_t>(Candidate.Status));
	Hash.AddUInt64(Candidate.CandidateSourceHash);
	Hash.AddUInt64(Candidate.NominalRequestHash);
	Hash.AddUInt64(Candidate.NominalResultHash);
	Hash.AddInt32(Candidate.SolverInvocationCount);
	Hash.AddDouble(Candidate.Metrics.TotalFlightTimeSeconds);
	Hash.AddDouble(Candidate.Metrics.FinalCoastSeconds);
	Hash.AddDouble(Candidate.Metrics.MaximumCoastSeconds);
	Hash.AddDouble(Candidate.Metrics.TotalInfluenceDurationSeconds);
	Hash.AddDouble(Candidate.Metrics.MinimumLayoutTurnRadians);
	for (const double Turn : Candidate.Metrics.LayoutTurnsRadians)
	{
		Hash.AddDouble(Turn);
	}
	Hash.AddDouble(Candidate.Metrics.MinimumTargetDistanceCM);
	Hash.AddDouble(Candidate.Metrics.MinimumReadableDeflectionRadians);
	Hash.AddInt32(Candidate.Metrics.AlternatingLateralTurnCount);
	Hash.AddInt32(Candidate.Metrics.RobustSurvivorCount);
	Hash.AddInt32(Candidate.Metrics.LowPowerCompletedAssistCount);
	Hash.AddInt32(Candidate.Metrics.FullDomainSampleCount);
	Hash.AddInt32(Candidate.Metrics.ScreenAimSampleCount);
	Hash.AddInt32(Candidate.Metrics.FullDomainSolveFailureCount);
	Hash.AddInt32(Candidate.Metrics.ScreenAimSolveFailureCount);
	Hash.AddInt32(Candidate.Metrics.ConditionalSolveFailureCount);
	for (const InputSetMetrics& Set : Candidate.Metrics.InputSets)
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
		for (const YawPitchPoint& Point : Set.ScreenAimHullYawPitch)
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
	Hash.AddDouble(Candidate.Metrics.PrefixRetentionScore);
	Hash.AddDouble(Candidate.Metrics.PrefixHullScore);
	Hash.AddDouble(Candidate.Metrics.DeflectionReadabilityScore);
	Hash.AddDouble(Candidate.Metrics.AlternationScore);
	Hash.AddDouble(Candidate.Metrics.PacingScore);
	Hash.AddDouble(Candidate.Metrics.SoftScore);
	for (const AssistMetrics& Assist : Candidate.Metrics.Assists)
	{
		Hash.AddDouble(Assist.EnterTimeSeconds);
		Hash.AddDouble(Assist.ClosestTimeSeconds);
		Hash.AddDouble(Assist.ExitTimeSeconds);
		Hash.AddDouble(Assist.CoastBeforeEnterSeconds);
		Hash.AddDouble(Assist.InfluenceDurationSeconds);
		Hash.AddDouble(Assist.ActualDeflectionRadians);
		Hash.AddDouble(Assist.NaturalDeflectionRadians);
		Hash.AddDouble(Assist.EntrySpeedCMPerSec);
		Hash.AddDouble(Assist.ExitSpeedCMPerSec);
		Hash.AddDouble(Assist.CorridorQuality);
		Hash.AddDouble(Assist.AppliedEnergyGainCM2PerSec2);
		Hash.AddDouble(Assist.CollisionClearanceCM);
		Hash.AddDouble(Assist.SignedLateralTurnRadians);
		Hash.AddDouble(Assist.LateralTurnAxisProjection);
	}
	for (std::size_t Index = 0;
		Index < Candidate.Metrics.AblationMasks.size();
		++Index)
	{
		Hash.AddByte(Candidate.Metrics.AblationMasks[Index]);
		Hash.AddBool(Candidate.Metrics.AblationHitTarget[Index]);
		Hash.AddUInt64(Candidate.Metrics.AblationResultHashes[Index]);
	}
	Hash.AddString(Candidate.Rejection);
	return Hash.Get();
}

std::uint64_t ABTS::M11Search::ComputeEvaluationAggregateHash(
	const std::vector<CandidateRecord>& Evaluations)
{
	using namespace TypesPrivate;
	CanonicalHash Hash;
	Hash.AddUInt32(0x11b21003u);
	Hash.AddUInt64(static_cast<std::uint64_t>(Evaluations.size()));
	for (const CandidateRecord& Evaluation : Evaluations)
	{
		Hash.AddUInt64(Evaluation.GlobalWorkIndex);
		Hash.AddByte(static_cast<std::uint8_t>(Evaluation.Status));
		Hash.AddUInt64(Evaluation.CandidateSourceHash);
		Hash.AddUInt64(Evaluation.NominalResultHash);
		Hash.AddUInt64(Evaluation.ScoreHash);
	}
	return Hash.Get();
}
