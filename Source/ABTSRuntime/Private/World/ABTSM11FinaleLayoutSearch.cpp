// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleLayoutSearch.h"

#include "Algo/Sort.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace ABTSM11FinaleSearchPrivate
{
	bool Reject(
		FString* OutFailure,
		FABTSM11FinaleSearchReport* Report,
		const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		if (Report != nullptr)
		{
			Report->Failure = Reason;
		}
		return false;
	}

	void RefreshPresetIdentity(FABTSM11FinaleLayoutPreset& Preset)
	{
		Preset.PresetSourceHash = 0;
		Preset.PresetHash = 0;
		Preset.ScanContractHash = 0;
		Preset.CertificationHash = 0;
		Preset.NominalTrajectoryHash = 0;
		Preset.PhysicalPlaybackTrajectoryHash = 0;
		Preset.CertifiedBundleHash = 0;
		Preset.CanonicalScenario.ScenarioHash = 1;
		Preset.PresetSourceHash =
			FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset);
		Preset.PresetHash =
			FABTSM11FinaleLayoutHash::ComputePresetHash(Preset);
		Preset.CanonicalScenario.ScenarioHash =
			FABTSM11FinaleLayoutHash::FoldScenarioHash(Preset.PresetHash);
		Preset.ScanContractHash =
			FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset);
	}

	bool SolveNominal(
		const FABTSM11FinaleLayoutPreset& Preset,
		FABTSM11TrajectoryResult& OutResult,
		FString* OutFailure = nullptr)
	{
		FABTSM11TrajectoryRequest Request;
		if (!Preset.BuildRequest(
			Preset.NominalInput, 0x7u, Request, OutFailure))
		{
			return false;
		}
		return FABTSM11GravityAssistSolver::Solve(
			Request, OutResult, OutFailure);
	}

	const FABTSM11TrajectoryPoint* FindPointAtOrAfter(
		const FABTSM11TrajectoryResult& Result,
		const double TimeSeconds)
	{
		return Result.Points.FindByPredicate(
			[TimeSeconds](const FABTSM11TrajectoryPoint& Point)
			{
				return Point.TimeSeconds >= TimeSeconds;
			});
	}

	double ComputeLowPowerApoapsisUpper(
		const FABTSM11FinaleLayoutPreset& Preset,
		const double Power)
	{
		FABTSM11FinaleLaunchInput Input = Preset.NominalInput;
		Input.Power = Power;
		const FABTSM11GravityBodySpec& Primary =
			Preset.CanonicalScenario.GetPrimary();
		const double LaunchRadiusCM =
			(Preset.LaunchModel.PouchLocalPositionCM - Primary.CenterCM).Length();
		const double SpeedCMPerSec =
			Preset.LaunchModel.MapSpeedCMPerSec(Input);
		const double Energy =
			0.5 * FMath::Square(SpeedCMPerSec)
			- Primary.GravitationalParameterCM3PerSec2 / LaunchRadiusCM;
		return Energy < 0.0
			? -Primary.GravitationalParameterCM3PerSec2 / Energy
			: TNumericLimits<double>::Max();
	}

	bool BuildImpactBasis(
		const FABTSM11GravityBodySpec& Body,
		const FABTSM11TrajectoryPoint& Point,
		FVector3d& OutT,
		FVector3d& OutR)
	{
		const FVector3d V = Point.VelocityCMPerSec.GetSafeNormal();
		OutT = FVector3d::CrossProduct(
			Body.BPlaneReferenceNormal.GetSafeNormal(), V).GetSafeNormal();
		if (OutT.IsNearlyZero())
		{
			OutT = FVector3d::CrossProduct(
				Body.BPlaneFallbackAxis.GetSafeNormal(), V).GetSafeNormal();
		}
		OutR = FVector3d::CrossProduct(V, OutT).GetSafeNormal();
		return !V.IsNearlyZero() && !OutT.IsNearlyZero() && !OutR.IsNearlyZero();
	}

	bool GeometryIsLegal(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11FinaleSearchConfig& Config,
		const int32 AssistIndex)
	{
		const FABTSM11GravityBodySpec& Primary =
			Preset.CanonicalScenario.GetPrimary();
		const FABTSM11GravityBodySpec& Candidate =
			Preset.CanonicalScenario.GetAssist(AssistIndex);
		const double PrimaryDistance =
			(Candidate.CenterCM - Primary.CenterCM).Length();
		if (PrimaryDistance
				<= Primary.CollisionRadiusCM + Candidate.InfluenceRadiusCM
			|| PrimaryDistance + Candidate.InfluenceRadiusCM
				>= Primary.MaximumSimulationRadiusCM)
		{
			return false;
		}
		for (int32 OtherIndex = 1; OtherIndex < AssistIndex; ++OtherIndex)
		{
			const FABTSM11GravityBodySpec& Other =
				Preset.CanonicalScenario.GetAssist(OtherIndex);
			const double CenterDistance =
				(Candidate.CenterCM - Other.CenterCM).Length();
			const double MinimumCenterDistance =
				OtherIndex == AssistIndex - 1
				? AssistIndex == 2
					? Config.Assist12MinimumCenterDistanceCM
					: Config.Assist23MinimumCenterDistanceCM
				: Candidate.InfluenceRadiusCM + Other.InfluenceRadiusCM;
			if (CenterDistance <= FMath::Max(
				MinimumCenterDistance,
				Candidate.InfluenceRadiusCM + Other.InfluenceRadiusCM))
			{
				return false;
			}
		}
		return true;
	}

	EABTSM11AllowedPassSide InferAllowedSide(
		const FABTSM11TrajectoryEvent& Exit)
	{
		if (FMath::Abs(Exit.BPlaneTCM) >= FMath::Abs(Exit.BPlaneRCM))
		{
			return Exit.BPlaneTCM >= 0.0
				? EABTSM11AllowedPassSide::PositiveT
				: EABTSM11AllowedPassSide::NegativeT;
		}
		return Exit.BPlaneRCM >= 0.0
			? EABTSM11AllowedPassSide::PositiveR
			: EABTSM11AllowedPassSide::NegativeR;
	}

	struct FCandidate
	{
		FABTSM11FinaleLayoutPreset Preset;
		FABTSM11TrajectoryResult Result;
		double ExitTimeSeconds = 0.0;
		double CorridorQuality = 0.0;
		double EnergyGain = 0.0;
		double ClosestClearanceCM = 0.0;
		int32 RobustSurvivorCount = 0;
		double RobustMinimumCorridorQuality = 0.0;
		double RobustMinimumEnergyGain = 0.0;
		double RobustMinimumSideMarginCM = 0.0;
		double TargetBundleRadiusCM = TNumericLimits<double>::Max();
		bool bNominalRobustSurvivor = false;
		uint32 DeterministicTieBreakKey = 0;
		int32 CandidateIndex = 0;
	};

	uint32 MakeCandidateTieBreakKey(
		const int32 SearchSeed,
		const int32 AssistIndex,
		const int32 CandidateIndex)
	{
		uint32 Value = static_cast<uint32>(SearchSeed);
		Value ^= static_cast<uint32>(AssistIndex) * 0x9e3779b9u;
		Value ^= static_cast<uint32>(CandidateIndex) * 0x85ebca6bu;
		Value ^= Value >> 16;
		Value *= 0x7feb352du;
		Value ^= Value >> 15;
		Value *= 0x846ca68bu;
		Value ^= Value >> 16;
		return Value;
	}

	bool NominalCandidateLess(const FCandidate& A, const FCandidate& B)
	{
		if (A.EnergyGain != B.EnergyGain)
		{
			return A.EnergyGain > B.EnergyGain;
		}
		if (A.CorridorQuality != B.CorridorQuality)
		{
			return A.CorridorQuality > B.CorridorQuality;
		}
		if (A.ClosestClearanceCM != B.ClosestClearanceCM)
		{
			return A.ClosestClearanceCM > B.ClosestClearanceCM;
		}
		if (A.ExitTimeSeconds != B.ExitTimeSeconds)
		{
			return A.ExitTimeSeconds < B.ExitTimeSeconds;
		}
		if (A.DeterministicTieBreakKey != B.DeterministicTieBreakKey)
		{
			return A.DeterministicTieBreakKey < B.DeterministicTieBreakKey;
		}
		return A.CandidateIndex < B.CandidateIndex;
	}

	bool CandidateLess(const FCandidate& A, const FCandidate& B)
	{
		if (A.RobustSurvivorCount != B.RobustSurvivorCount)
		{
			return A.RobustSurvivorCount > B.RobustSurvivorCount;
		}
		if (A.RobustMinimumEnergyGain != B.RobustMinimumEnergyGain)
		{
			return A.RobustMinimumEnergyGain > B.RobustMinimumEnergyGain;
		}
		if (A.RobustMinimumCorridorQuality
			!= B.RobustMinimumCorridorQuality)
		{
			return A.RobustMinimumCorridorQuality
				> B.RobustMinimumCorridorQuality;
		}
		if (A.RobustMinimumSideMarginCM != B.RobustMinimumSideMarginCM)
		{
			return A.RobustMinimumSideMarginCM
				> B.RobustMinimumSideMarginCM;
		}
		if (A.TargetBundleRadiusCM != B.TargetBundleRadiusCM)
		{
			return A.TargetBundleRadiusCM < B.TargetBundleRadiusCM;
		}
		return NominalCandidateLess(A, B);
	}

	double ComputeAllowedSideMarginCM(
		const FABTSM11GravityBodySpec& Body,
		const FABTSM11TrajectoryEvent& Exit)
	{
		switch (Body.AllowedPassSide)
		{
		case EABTSM11AllowedPassSide::PositiveT:
			return Exit.BPlaneTCM;
		case EABTSM11AllowedPassSide::NegativeT:
			return -Exit.BPlaneTCM;
		case EABTSM11AllowedPassSide::PositiveR:
			return Exit.BPlaneRCM;
		case EABTSM11AllowedPassSide::NegativeR:
			return -Exit.BPlaneRCM;
		case EABTSM11AllowedPassSide::Any:
			return TNumericLimits<double>::Max();
		default:
			return -TNumericLimits<double>::Max();
		}
	}

	bool AccumulateRobustResult(
		const FABTSM11FinaleSearchConfig& Config,
		const int32 AssistIndex,
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		FCandidate& Candidate,
		TArray<FVector3d>& TargetBundlePoints)
	{
		if (Result.CompletedAssistCount < AssistIndex)
		{
			return false;
		}

		double ResultMinimumQuality = TNumericLimits<double>::Max();
		double ResultMinimumEnergy = TNumericLimits<double>::Max();
		double ResultMinimumSideMargin = TNumericLimits<double>::Max();
		const FABTSM11TrajectoryEvent* Exit = nullptr;
		for (int32 CheckAssistIndex = 1;
			CheckAssistIndex <= AssistIndex;
			++CheckAssistIndex)
		{
			Exit = Result.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistExit,
				CheckAssistIndex);
			if (Exit == nullptr)
			{
				return false;
			}
			const FABTSM11GravityBodySpec& Body =
				Preset.CanonicalScenario.GetAssist(CheckAssistIndex);
			const double SideMarginCM =
				ComputeAllowedSideMarginCM(Body, *Exit);
			if (Exit->CorridorQuality
					< Config.RobustMinimumCorridorQuality
				|| Exit->AppliedEnergyChangeCM2PerSec2
					< Config.RobustMinimumEnergyGainCM2PerSec2
				|| SideMarginCM <= 0.0)
			{
				return false;
			}
			ResultMinimumQuality = FMath::Min(
				ResultMinimumQuality, Exit->CorridorQuality);
			ResultMinimumEnergy = FMath::Min(
				ResultMinimumEnergy,
				Exit->AppliedEnergyChangeCM2PerSec2);
			ResultMinimumSideMargin = FMath::Min(
				ResultMinimumSideMargin, SideMarginCM);
		}
		if (Exit == nullptr)
		{
			return false;
		}

		++Candidate.RobustSurvivorCount;
		Candidate.RobustMinimumCorridorQuality = FMath::Min(
			Candidate.RobustMinimumCorridorQuality,
			ResultMinimumQuality);
		Candidate.RobustMinimumEnergyGain = FMath::Min(
			Candidate.RobustMinimumEnergyGain,
			ResultMinimumEnergy);
		Candidate.RobustMinimumSideMarginCM = FMath::Min(
			Candidate.RobustMinimumSideMarginCM,
			ResultMinimumSideMargin);

		if (AssistIndex == FABTSM11GravityScenario::AssistCount)
		{
			const FABTSM11TrajectoryPoint* TargetPoint =
				FindPointAtOrAfter(
					Result,
					Exit->TimeSeconds
						+ Config.TargetFlightTimeSeconds);
			if (TargetPoint != nullptr)
			{
				TargetBundlePoints.Add(TargetPoint->PositionCM);
			}
		}
		return true;
	}

	void EvaluateRobustness(
		const FABTSM11FinaleSearchConfig& Config,
		const int32 AssistIndex,
		FCandidate& Candidate,
		FABTSM11FinaleSearchReport& Report)
	{
		Candidate.RobustSurvivorCount = 0;
		Candidate.RobustMinimumCorridorQuality =
			TNumericLimits<double>::Max();
		Candidate.RobustMinimumEnergyGain =
			TNumericLimits<double>::Max();
		Candidate.RobustMinimumSideMarginCM =
			TNumericLimits<double>::Max();
		TArray<FVector3d> TargetBundlePoints;
		Candidate.bNominalRobustSurvivor = AccumulateRobustResult(
			Config,
			AssistIndex,
			Candidate.Preset,
			Candidate.Result,
			Candidate,
			TargetBundlePoints);

		TStaticArray<FABTSM11FinaleLaunchInput, 6> Neighbors;
		for (FABTSM11FinaleLaunchInput& Input : Neighbors)
		{
			Input = Candidate.Preset.NominalInput;
		}
		Neighbors[0].YawDegrees -=
			Candidate.Preset.ScanContract.FinalYawPrecisionDegrees;
		Neighbors[1].YawDegrees +=
			Candidate.Preset.ScanContract.FinalYawPrecisionDegrees;
		Neighbors[2].PitchDegrees -=
			Candidate.Preset.ScanContract.FinalPitchPrecisionDegrees;
		Neighbors[3].PitchDegrees +=
			Candidate.Preset.ScanContract.FinalPitchPrecisionDegrees;
		Neighbors[4].Power -=
			Candidate.Preset.ScanContract.FinalPowerPrecision;
		Neighbors[5].Power +=
			Candidate.Preset.ScanContract.FinalPowerPrecision;

		for (const FABTSM11FinaleLaunchInput& Input : Neighbors)
		{
			if (!Candidate.Preset.LaunchModel.Contains(Input))
			{
				continue;
			}
			FABTSM11TrajectoryRequest Request;
			FABTSM11TrajectoryResult Result;
			FString Failure;
			++Report.CandidateSolveCount;
			if (!Candidate.Preset.BuildRequest(
					Input, 0x7u, Request, &Failure)
				|| !FABTSM11GravityAssistSolver::Solve(
					Request, Result, &Failure))
			{
				continue;
			}
			AccumulateRobustResult(
				Config,
				AssistIndex,
				Candidate.Preset,
				Result,
				Candidate,
				TargetBundlePoints);
		}

		if (Candidate.RobustSurvivorCount <= 0)
		{
			Candidate.RobustMinimumCorridorQuality = 0.0;
			Candidate.RobustMinimumEnergyGain = 0.0;
			Candidate.RobustMinimumSideMarginCM = 0.0;
		}

		if (AssistIndex == FABTSM11GravityScenario::AssistCount
			&& !TargetBundlePoints.IsEmpty())
		{
			FVector3d CenterCM = FVector3d::ZeroVector;
			for (const FVector3d& PointCM : TargetBundlePoints)
			{
				CenterCM += PointCM;
			}
			CenterCM /= TargetBundlePoints.Num();
			double RadiusCM = 0.0;
			for (const FVector3d& PointCM : TargetBundlePoints)
			{
				RadiusCM = FMath::Max(
					RadiusCM, (PointCM - CenterCM).Length());
			}
			Candidate.TargetBundleRadiusCM = RadiusCM;
			Candidate.Preset.CanonicalScenario.Target.CenterCM =
				CenterCM;
			const FABTSM11TrajectoryEvent* Exit =
				Candidate.Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::AssistExit,
					AssistIndex);
			const FABTSM11TrajectoryPoint* NominalTargetPoint =
				Exit != nullptr
					? FindPointAtOrAfter(
						Candidate.Result,
						Exit->TimeSeconds
							+ Config.TargetFlightTimeSeconds)
					: nullptr;
			if (NominalTargetPoint != nullptr)
			{
				Candidate.Preset.CanonicalScenario.Target
					.PresentationForward =
						-NominalTargetPoint->VelocityCMPerSec
							.GetSafeNormal();
			}
			RefreshPresetIdentity(Candidate.Preset);
		}
	}

	bool PlaceNextAssist(
		const FABTSM11FinaleSearchConfig& Config,
		const int32 AssistIndex,
		const FABTSM11FinaleLayoutPreset& InputPreset,
		const FABTSM11TrajectoryResult& InputArc,
		FABTSM11FinaleLayoutPreset& OutPreset,
		FABTSM11TrajectoryResult& OutResult,
		FABTSM11FinaleSearchReport& Report)
	{
		const FABTSM11TrajectoryEvent* PreviousExit = AssistIndex > 1
			? InputArc.FindAssistEvent(
				EABTSM11TrajectoryEventType::AssistExit,
				AssistIndex - 1)
			: nullptr;
		const double ArcStartTime = PreviousExit != nullptr
			? PreviousExit->TimeSeconds + Config.LaterLegMinimumTimeSeconds
			: Config.FirstAssistMinimumTimeSeconds;
		const double ArcEndTime = PreviousExit != nullptr
			? PreviousExit->TimeSeconds + Config.LaterLegMaximumTimeSeconds
			: Config.FirstAssistMaximumTimeSeconds;
		const double LowPowerApoapsis = AssistIndex == 1
			? ComputeLowPowerApoapsisUpper(
				InputPreset, Config.LowPowerGate)
			: 0.0;
		TArray<FCandidate> Candidates;
		int32 CandidateIndex = 0;
		for (double SampleTime = ArcStartTime;
			SampleTime <= ArcEndTime + UE_DOUBLE_SMALL_NUMBER;
			SampleTime += Config.ArcSampleIntervalSeconds)
		{
			const FABTSM11TrajectoryPoint* Point =
				FindPointAtOrAfter(InputArc, SampleTime);
			if (Point == nullptr)
			{
				break;
			}
			const FABTSM11GravityBodySpec& SeedBody =
				InputPreset.CanonicalScenario.GetAssist(AssistIndex);
			FVector3d T;
			FVector3d R;
			if (!BuildImpactBasis(SeedBody, *Point, T, R))
			{
				continue;
			}
			for (const double ImpactFraction : Config.ImpactFractions)
			{
				for (const double RadialFraction :
					Config.RadialImpactFractions)
				{
					for (const double Sign : {-1.0, 1.0})
					{
						++CandidateIndex;
						FABTSM11FinaleLayoutPreset Candidate = InputPreset;
						FABTSM11GravityBodySpec& Body =
							Candidate.CanonicalScenario.Bodies[AssistIndex];
						const FVector3d OffsetCM =
							T * (
								Sign
								* ImpactFraction
								* Body.InfluenceRadiusCM)
							+ R * (
								RadialFraction
								* Body.InfluenceRadiusCM);
						Body.CenterCM = Point->PositionCM + OffsetCM;
						Body.VirtualOrbitalVelocityCMPerSec =
							OffsetCM.GetSafeNormal()
							* Config.VirtualMomentumSpeedCMPerSec;
						Body.AllowedPassSide =
							EABTSM11AllowedPassSide::Any;
						Body.BPlaneTargetTCM = 0.0;
						Body.BPlaneTargetRCM = 0.0;
						Body.BPlaneSigmaTCM =
							Body.InfluenceRadiusCM * 0.45;
						Body.BPlaneSigmaRCM =
							Body.InfluenceRadiusCM * 0.45;
						if (AssistIndex == 1)
						{
							const double PrimaryDistance =
								(Body.CenterCM
									- Candidate.CanonicalScenario
										.GetPrimary().CenterCM).Length();
							if (PrimaryDistance
									- Body.InfluenceRadiusCM
								<= LowPowerApoapsis
									+ Config.LowPowerClearanceCM)
							{
								++Report.RejectedGeometryCount;
								continue;
							}
						}
						if (!GeometryIsLegal(
							Candidate, Config, AssistIndex))
						{
							++Report.RejectedGeometryCount;
							continue;
						}
						RefreshPresetIdentity(Candidate);
						FABTSM11TrajectoryResult Result;
						++Report.CandidateSolveCount;
						if (!SolveNominal(Candidate, Result))
						{
							++Report.RejectedEncounterCount;
							continue;
						}
						const FABTSM11TrajectoryEvent* Exit =
							Result.FindAssistEvent(
								EABTSM11TrajectoryEventType::AssistExit,
								AssistIndex);
						if (Exit == nullptr
							|| Result.CompletedAssistCount < AssistIndex
							|| Exit->AppliedEnergyChangeCM2PerSec2 <= 0.0)
						{
							++Report.RejectedEncounterCount;
							continue;
						}

						// Freeze the natural B-plane coordinates and the approved
						// pass side, then replay once with the production corridor.
						Body.BPlaneTargetTCM = Exit->BPlaneTCM;
						Body.BPlaneTargetRCM = Exit->BPlaneRCM;
						Body.BPlaneSigmaTCM =
							Body.InfluenceRadiusCM * 0.42;
						Body.BPlaneSigmaRCM =
							Body.InfluenceRadiusCM * 0.42;
						Body.AllowedPassSide = InferAllowedSide(*Exit);
						RefreshPresetIdentity(Candidate);
						++Report.CandidateSolveCount;
						if (!SolveNominal(Candidate, Result))
						{
							++Report.RejectedEncounterCount;
							continue;
						}
						Exit = Result.FindAssistEvent(
							EABTSM11TrajectoryEventType::AssistExit,
							AssistIndex);
						if (Exit == nullptr
							|| Result.CompletedAssistCount < AssistIndex
							|| Exit->CorridorQuality <= 0.0
							|| Exit->AppliedEnergyChangeCM2PerSec2 <= 0.0)
						{
							++Report.RejectedEncounterCount;
							continue;
						}
						FCandidate& Scored =
							Candidates.AddDefaulted_GetRef();
						Scored.Preset = MoveTemp(Candidate);
						Scored.Result = MoveTemp(Result);
						Scored.ExitTimeSeconds = Exit->TimeSeconds;
						Scored.CorridorQuality = Exit->CorridorQuality;
						Scored.EnergyGain =
							Exit->AppliedEnergyChangeCM2PerSec2;
						Scored.ClosestClearanceCM =
							Exit->ClosestDistanceCM
							- Body.CollisionRadiusCM;
						Scored.DeterministicTieBreakKey =
							MakeCandidateTieBreakKey(
								Config.SearchSeed,
								AssistIndex,
								CandidateIndex);
						Scored.CandidateIndex = CandidateIndex;
					}
				}
			}
		}
		if (Candidates.IsEmpty())
		{
			return false;
		}
		Algo::Sort(Candidates, NominalCandidateLess);
		if (Candidates.Num() > Config.RobustPreselectionWidth)
		{
			Candidates.SetNum(Config.RobustPreselectionWidth);
		}
		for (FCandidate& Candidate : Candidates)
		{
			EvaluateRobustness(
				Config, AssistIndex, Candidate, Report);
		}
		const int32 PreRobustFilterCount = Candidates.Num();
		Candidates.RemoveAll(
			[&Config](const FCandidate& Candidate)
			{
				return !Candidate.bNominalRobustSurvivor
					|| Candidate.RobustSurvivorCount
					< Config.MinimumRobustSurvivorCount;
			});
		Report.RejectedEncounterCount +=
			PreRobustFilterCount - Candidates.Num();
		if (Candidates.IsEmpty())
		{
			return false;
		}
		Algo::Sort(Candidates, CandidateLess);
		if (Candidates.Num() > Config.BeamWidth)
		{
			Candidates.SetNum(Config.BeamWidth);
		}
		// The first candidate is deterministic under the lexicographic score.
		OutPreset = MoveTemp(Candidates[0].Preset);
		OutResult = MoveTemp(Candidates[0].Result);
		Report.CompletedAssistCount = AssistIndex;
		Report.AssistExitTimes[AssistIndex - 1] =
			Candidates[0].ExitTimeSeconds;
		return true;
	}
}

bool FABTSM11FinaleSearchConfig::IsValid(FString* OutFailure) const
{
	if (SearchConfigVersion != 1
		|| SearchSeed <= 0
		|| BeamWidth <= 0
		|| BeamWidth > 128
		|| RobustPreselectionWidth < BeamWidth
		|| RobustPreselectionWidth > 512
		|| MinimumRobustSurvivorCount < 2
		|| MinimumRobustSurvivorCount > 7
		|| !FMath::IsFinite(FirstAssistMinimumTimeSeconds)
		|| !FMath::IsFinite(FirstAssistMaximumTimeSeconds)
		|| !FMath::IsFinite(LaterLegMinimumTimeSeconds)
		|| !FMath::IsFinite(LaterLegMaximumTimeSeconds)
		|| !FMath::IsFinite(ArcSampleIntervalSeconds)
		|| FirstAssistMinimumTimeSeconds < 0.0
		|| FirstAssistMinimumTimeSeconds >= FirstAssistMaximumTimeSeconds
		|| LaterLegMinimumTimeSeconds < 0.0
		|| LaterLegMinimumTimeSeconds >= LaterLegMaximumTimeSeconds
		|| ArcSampleIntervalSeconds <= 0.0)
	{
		return ABTSM11FinaleSearchPrivate::Reject(
			OutFailure, nullptr, TEXT("InvalidSearchGrid"));
	}
	if (ImpactFractions.IsEmpty()
		|| RadialImpactFractions.IsEmpty()
		|| !FMath::IsFinite(VirtualMomentumSpeedCMPerSec)
		|| !FMath::IsFinite(TargetFlightTimeSeconds)
		|| !FMath::IsFinite(LowPowerGate)
		|| !FMath::IsFinite(LowPowerClearanceCM)
		|| !FMath::IsFinite(Assist12MinimumCenterDistanceCM)
		|| !FMath::IsFinite(Assist23MinimumCenterDistanceCM)
		|| !FMath::IsFinite(RobustMinimumCorridorQuality)
		|| !FMath::IsFinite(RobustMinimumEnergyGainCM2PerSec2)
		|| VirtualMomentumSpeedCMPerSec <= 0.0
		|| TargetFlightTimeSeconds <= 0.0
		|| LowPowerGate <= 0.0
		|| LowPowerGate >= 1.0
		|| LowPowerClearanceCM < 0.0
		|| Assist12MinimumCenterDistanceCM <= 0.0
		|| Assist23MinimumCenterDistanceCM
			<= Assist12MinimumCenterDistanceCM
		|| RobustMinimumCorridorQuality <= 0.0
		|| RobustMinimumCorridorQuality > 1.0
		|| RobustMinimumEnergyGainCM2PerSec2 <= 0.0)
	{
		return ABTSM11FinaleSearchPrivate::Reject(
			OutFailure, nullptr, TEXT("InvalidSearchPolicy"));
	}
	for (const double Value : ImpactFractions)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.0 || Value >= 1.0)
		{
			return ABTSM11FinaleSearchPrivate::Reject(
				OutFailure, nullptr, TEXT("InvalidImpactFraction"));
		}
	}
	for (const double Value : RadialImpactFractions)
	{
		if (!FMath::IsFinite(Value) || FMath::Abs(Value) >= 1.0)
		{
			return ABTSM11FinaleSearchPrivate::Reject(
				OutFailure, nullptr, TEXT("InvalidRadialImpactFraction"));
		}
	}
	return true;
}

FABTSM11FinaleSearchReport::FABTSM11FinaleSearchReport()
{
	for (double& Time : AssistExitTimes)
	{
		Time = 0.0;
	}
}

bool FABTSM11FinaleLayoutSearch::BuildConstructiveSeed(
	const FABTSM11FinaleLayoutPreset& SeedPreset,
	const FABTSM11FinaleSearchConfig& Config,
	FABTSM11FinaleLayoutPreset& OutPreset,
	FABTSM11FinaleSearchReport& OutReport,
	FString* OutFailure)
{
	using namespace ABTSM11FinaleSearchPrivate;
	OutPreset = FABTSM11FinaleLayoutPreset();
	OutReport = FABTSM11FinaleSearchReport();
	OutReport.NominalInput = SeedPreset.NominalInput;
	FString ValidationFailure;
	if (!Config.IsValid(&ValidationFailure)
		|| !SeedPreset.IsValid(&ValidationFailure))
	{
		return Reject(OutFailure, &OutReport, *ValidationFailure);
	}

	FABTSM11FinaleLayoutPreset Working = SeedPreset;
	FABTSM11TrajectoryResult Arc;
	if (!SolveNominal(Working, Arc, &ValidationFailure))
	{
		return Reject(OutFailure, &OutReport, TEXT("InitialArcSolveFailed"));
	}

	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		FABTSM11FinaleLayoutPreset NextPreset;
		FABTSM11TrajectoryResult NextArc;
		if (!PlaceNextAssist(
			Config,
			AssistIndex,
			Working,
			Arc,
			NextPreset,
			NextArc,
			OutReport))
		{
			return Reject(
				OutFailure,
				&OutReport,
				*FString::Printf(
					TEXT("NoAssistCandidate%d"), AssistIndex));
		}
		Working = MoveTemp(NextPreset);
		Arc = MoveTemp(NextArc);
	}

	const FABTSM11TrajectoryEvent* Exit3 = Arc.FindAssistEvent(
		EABTSM11TrajectoryEventType::AssistExit, 3);
	if (Exit3 == nullptr)
	{
		return Reject(OutFailure, &OutReport, TEXT("MissingAssist3Exit"));
	}
	if ((Working.CanonicalScenario.Target.CenterCM
			- Working.CanonicalScenario.GetAssist(3).CenterCM).Length()
		<= Working.CanonicalScenario.Target.HitRadiusCM)
	{
		return Reject(
			OutFailure, &OutReport, TEXT("InvalidTargetBundleCenter"));
	}
	RefreshPresetIdentity(Working);
	const uint64 FrozenPresetSourceHash = Working.PresetSourceHash;
	const uint64 FrozenPresetHash = Working.PresetHash;
	const uint32 FrozenScenarioHash =
		Working.CanonicalScenario.ScenarioHash;
	FABTSM11TrajectoryResult FinalResult;
	++OutReport.CandidateSolveCount;
	if (!SolveNominal(Working, FinalResult, &ValidationFailure))
	{
		return Reject(OutFailure, &OutReport, TEXT("TargetReplayFailed"));
	}
	const FABTSM11PrefixClassification Classification =
		FABTSM11PrefixClassifier::Classify(Working, FinalResult, 0x7u);
	if (!FinalResult.DidHitTarget()
		|| Classification.HighestPrefixLevel != 4)
	{
		return Reject(
			OutFailure, &OutReport, TEXT("ConstructiveTargetMiss"));
	}

	Working.NominalTrajectoryHash = FinalResult.ValidationHash;
	if (FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Working)
			!= FrozenPresetSourceHash
		|| FABTSM11FinaleLayoutHash::ComputePresetHash(Working)
			!= FrozenPresetHash
		|| FABTSM11FinaleLayoutHash::FoldScenarioHash(FrozenPresetHash)
			!= FrozenScenarioHash)
	{
		return Reject(
			OutFailure, &OutReport, TEXT("FinalScenarioIdentityDrift"));
	}

	OutReport.NominalTrajectoryHash = FinalResult.ValidationHash;
	OutReport.SearchOutputHash = Working.PresetHash;
	OutPreset = MoveTemp(Working);
	return true;
}
