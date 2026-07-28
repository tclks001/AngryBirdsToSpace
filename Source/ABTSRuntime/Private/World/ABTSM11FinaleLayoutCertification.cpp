// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleLayoutCertification.h"

#include "Async/ParallelFor.h"
#include "World/ABTSM11GravityAssistSolver.h"

#include <atomic>

namespace ABTSM11FinaleCertificationPrivate
{
	bool IsAllowedSide(
		const FABTSM11GravityBodySpec& Body,
		const FABTSM11TrajectoryEvent& Exit)
	{
		switch (Body.AllowedPassSide)
		{
		case EABTSM11AllowedPassSide::Any:
			return true;
		case EABTSM11AllowedPassSide::PositiveT:
			return Exit.BPlaneTCM > 0.0;
		case EABTSM11AllowedPassSide::NegativeT:
			return Exit.BPlaneTCM < 0.0;
		case EABTSM11AllowedPassSide::PositiveR:
			return Exit.BPlaneRCM > 0.0;
		case EABTSM11AllowedPassSide::NegativeR:
			return Exit.BPlaneRCM < 0.0;
		default:
			return false;
		}
	}

	bool IsValidAssist(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		const uint8 EnabledAssistMask,
		const int32 AssistIndex,
		FABTSM11PrefixClassification& Classification)
	{
		const FABTSM11TrajectoryEvent* Enter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter, AssistIndex);
		const FABTSM11TrajectoryEvent* Closest = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach, AssistIndex);
		const FABTSM11TrajectoryEvent* Exit = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit, AssistIndex);
		if (Enter == nullptr || Closest == nullptr || Exit == nullptr)
		{
			return false;
		}

		const int32 ArrayIndex = AssistIndex - 1;
		Classification.CorridorQuality[ArrayIndex] = Exit->CorridorQuality;
		Classification.AppliedEnergyGain[ArrayIndex] =
			Exit->AppliedEnergyChangeCM2PerSec2;
		const FABTSM11GravityBodySpec& Body =
			Preset.CanonicalScenario.GetAssist(AssistIndex);
		return (EnabledAssistMask & (1u << ArrayIndex)) != 0
			&& Enter->TimeSeconds < Closest->TimeSeconds
			&& Closest->TimeSeconds < Exit->TimeSeconds
			&& IsAllowedSide(Body, *Exit)
			&& Exit->CorridorQuality
				>= Preset.MinimumCertifiedCorridorQuality[ArrayIndex]
			&& Exit->AppliedEnergyChangeCM2PerSec2
				>= Preset.MinimumCertifiedEnergyGainCM2PerSec2[ArrayIndex];
	}

	bool HasLaterAssistEnter(
		const FABTSM11TrajectoryResult& Result,
		const int32 LaterAssistIndex,
		const double AfterTimeSeconds)
	{
		const FABTSM11TrajectoryEvent* Enter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			LaterAssistIndex);
		return Enter != nullptr && Enter->TimeSeconds > AfterTimeSeconds;
	}

	bool SweepsTargetApproachAfter(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		const double AfterTimeSeconds)
	{
		for (int32 PointIndex = 1; PointIndex < Result.Points.Num(); ++PointIndex)
		{
			const FABTSM11TrajectoryPoint& Start = Result.Points[PointIndex - 1];
			const FABTSM11TrajectoryPoint& End = Result.Points[PointIndex];
			if (End.TimeSeconds <= AfterTimeSeconds)
			{
				continue;
			}
			double HitAlpha = 0.0;
			if (FABTSM11GravityAssistSolver::SweptSphereFirstHit(
				Start.PositionCM,
				End.PositionCM,
				Preset.CanonicalScenario.Target.CenterCM,
				Preset.TargetApproachRadiusCM,
				HitAlpha))
			{
				const double HitTime = FMath::Lerp(
					Start.TimeSeconds,
					End.TimeSeconds,
					HitAlpha);
				if (HitTime > AfterTimeSeconds)
				{
					return true;
				}
			}
		}
		return false;
	}

	bool ExceedsOrbitLimit(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result)
	{
		if (Result.Points.Num() < 2)
		{
			return false;
		}
		const FVector3d CenterCM =
			Preset.CanonicalScenario.GetPrimary().CenterCM;
		double AccumulatedAngle = 0.0;
		FVector3d Previous =
			(Result.Points[0].PositionCM - CenterCM).GetSafeNormal();
		for (int32 PointIndex = 1; PointIndex < Result.Points.Num(); ++PointIndex)
		{
			const FVector3d Current =
				(Result.Points[PointIndex].PositionCM - CenterCM).GetSafeNormal();
			if (!Previous.IsNearlyZero() && !Current.IsNearlyZero())
			{
				AccumulatedAngle += FMath::Acos(FMath::Clamp(
					FVector3d::DotProduct(Previous, Current), -1.0, 1.0));
			}
			Previous = Current;
		}
		return AccumulatedAngle
			> 2.0 * UE_PI
				* Preset.ScanContract.MaximumCompletePrimaryOrbits;
	}

	int32 FlattenIndex(
		const int32 YawIndex,
		const int32 PitchIndex,
		const int32 PowerIndex,
		const int32 YawCount,
		const int32 PitchCount)
	{
		return (PowerIndex * PitchCount + PitchIndex) * YawCount + YawIndex;
	}

	void UnflattenIndex(
		const int32 FlatIndex,
		const int32 YawCount,
		const int32 PitchCount,
		int32& OutYawIndex,
		int32& OutPitchIndex,
		int32& OutPowerIndex)
	{
		OutYawIndex = FlatIndex % YawCount;
		const int32 PitchPowerIndex = FlatIndex / YawCount;
		OutPitchIndex = PitchPowerIndex % PitchCount;
		OutPowerIndex = PitchPowerIndex / PitchCount;
	}

	bool CanPruneAsUnreachable(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11FinaleLaunchInput& Input)
	{
		const FABTSM11GravityBodySpec& Primary =
			Preset.CanonicalScenario.GetPrimary();
		const double LaunchRadiusCM =
			(Preset.LaunchModel.PouchLocalPositionCM - Primary.CenterCM).Length();
		const double SpeedCMPerSec =
			Preset.LaunchModel.MapSpeedCMPerSec(Input);
		const double SpecificEnergy =
			0.5 * FMath::Square(SpeedCMPerSec)
			- Primary.GravitationalParameterCM3PerSec2 / LaunchRadiusCM;
		if (SpecificEnergy >= 0.0)
		{
			return false;
		}
		const double MaximumReachRadiusCM =
			-Primary.GravitationalParameterCM3PerSec2 / SpecificEnergy;
		double NearestRequiredRadiusCM = TNumericLimits<double>::Max();
		for (int32 AssistIndex = 1;
			AssistIndex <= FABTSM11GravityScenario::AssistCount;
			++AssistIndex)
		{
			const FABTSM11GravityBodySpec& Assist =
				Preset.CanonicalScenario.GetAssist(AssistIndex);
			NearestRequiredRadiusCM = FMath::Min(
				NearestRequiredRadiusCM,
				(Assist.CenterCM - Primary.CenterCM).Length()
					- Assist.InfluenceRadiusCM);
		}
		NearestRequiredRadiusCM = FMath::Min(
			NearestRequiredRadiusCM,
			(Preset.CanonicalScenario.Target.CenterCM - Primary.CenterCM).Length()
				- Preset.CanonicalScenario.Target.HitRadiusCM);
		NearestRequiredRadiusCM = FMath::Min(
			NearestRequiredRadiusCM,
			(Preset.CanonicalScenario.Target.GetGeometricContactCenterCM()
				- Primary.CenterCM).Length()
				- Preset.CanonicalScenario.Target
					.GetGeometricContactRadiusCM());
		return MaximumReachRadiusCM < NearestRequiredRadiusCM;
	}

	bool InputToIndex(
		const FABTSM11InputGrid& Grid,
		const FABTSM11FinaleLaunchInput& Input,
		int32& OutYaw,
		int32& OutPitch,
		int32& OutPower)
	{
		OutYaw = FMath::RoundToInt(
			(Input.YawDegrees - Grid.Minimum.YawDegrees)
				/ Grid.YawStepDegrees);
		OutPitch = FMath::RoundToInt(
			(Input.PitchDegrees - Grid.Minimum.PitchDegrees)
				/ Grid.PitchStepDegrees);
		OutPower = FMath::RoundToInt(
			(Input.Power - Grid.Minimum.Power) / Grid.PowerStep);
		const FABTSM11FinaleLaunchInput Rebuilt =
			Grid.GetInput(OutYaw, OutPitch, OutPower);
		return FMath::IsNearlyEqual(
				Input.YawDegrees, Rebuilt.YawDegrees, 1.0e-9)
			&& FMath::IsNearlyEqual(
				Input.PitchDegrees, Rebuilt.PitchDegrees, 1.0e-9)
			&& FMath::IsNearlyEqual(Input.Power, Rebuilt.Power, 1.0e-9);
	}

	bool BoxIsInsidePrefix(
		TConstArrayView<FABTSM11CertificationSample> Samples,
		const int32 YawCount,
		const int32 PitchCount,
		const int32 PrefixLevel,
		const int32 MinYaw,
		const int32 MaxYaw,
		const int32 MinPitch,
		const int32 MaxPitch,
		const int32 MinPower,
		const int32 MaxPower)
	{
		for (int32 PowerIndex = MinPower;
			PowerIndex <= MaxPower;
			++PowerIndex)
		{
			for (int32 PitchIndex = MinPitch;
				PitchIndex <= MaxPitch;
				++PitchIndex)
			{
				for (int32 YawIndex = MinYaw;
					YawIndex <= MaxYaw;
					++YawIndex)
				{
					const int32 Index = FlattenIndex(
						YawIndex,
						PitchIndex,
						PowerIndex,
						YawCount,
						PitchCount);
					if (!Samples.IsValidIndex(Index)
						|| Samples[Index].HighestPrefixLevel < PrefixLevel)
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	struct FAimSliceCandidate
	{
		bool bFound = false;
		FABTSM11PrefixTrustRegion Region;
		double YawWidthPixels = 0.0;
		double PitchWidthPixels = 0.0;
	};

	double GetYawWidthPixels(
		const FABTSM11FinaleLayoutPreset& Preset,
		const double WidthDegrees)
	{
		const double DomainWidth =
			Preset.LaunchModel.MaximumYawDegrees
			- Preset.LaunchModel.MinimumYawDegrees;
		return WidthDegrees / DomainWidth
			* static_cast<double>(
				Preset.ScanContract.ReferenceResolutionX)
			/ Preset.ScanContract.ReferenceDPIScale;
	}

	double GetPitchWidthPixels(
		const FABTSM11FinaleLayoutPreset& Preset,
		const double WidthDegrees)
	{
		const double DomainWidth =
			Preset.LaunchModel.MaximumPitchDegrees
			- Preset.LaunchModel.MinimumPitchDegrees;
		return WidthDegrees / DomainWidth
			* static_cast<double>(
				Preset.ScanContract.ReferenceResolutionY)
			/ Preset.ScanContract.ReferenceDPIScale;
	}

	bool AimSliceMeetsPlayablePolicy(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FAimSliceCandidate& Candidate)
	{
		if (!Candidate.bFound)
		{
			return false;
		}
		const double YawWidth =
			Candidate.Region.Maximum.YawDegrees
			- Candidate.Region.Minimum.YawDegrees;
		const double PitchWidth =
			Candidate.Region.Maximum.PitchDegrees
			- Candidate.Region.Minimum.PitchDegrees;
		constexpr double Epsilon = 1.0e-12;
		return YawWidth + Epsilon
				>= Preset.ScanContract.MinimumF4YawWidthDegrees
			&& PitchWidth + Epsilon
				>= Preset.ScanContract.MinimumF4PitchWidthDegrees
			&& Candidate.YawWidthPixels + Epsilon
				>= Preset.ScanContract.MinimumScreenTrustWidthPixels
			&& Candidate.PitchWidthPixels + Epsilon
				>= Preset.ScanContract.MinimumScreenTrustWidthPixels;
	}

	bool AimSlicesHavePlayableOverlap(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FAimSliceCandidate& A,
		const FAimSliceCandidate& B)
	{
		const double YawOverlap =
			FMath::Min(
				A.Region.Maximum.YawDegrees,
				B.Region.Maximum.YawDegrees)
			- FMath::Max(
				A.Region.Minimum.YawDegrees,
				B.Region.Minimum.YawDegrees);
		const double PitchOverlap =
			FMath::Min(
				A.Region.Maximum.PitchDegrees,
				B.Region.Maximum.PitchDegrees)
			- FMath::Max(
				A.Region.Minimum.PitchDegrees,
				B.Region.Minimum.PitchDegrees);
		constexpr double Epsilon = 1.0e-12;
		return YawOverlap + Epsilon
				>= Preset.ScanContract.FinalYawPrecisionDegrees
			&& PitchOverlap + Epsilon
				>= Preset.ScanContract.FinalPitchPrecisionDegrees;
	}

	FAimSliceCandidate FindBestBalancedAimSlice(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11LayoutCertificationReport& Report,
		TConstArrayView<int32> Labels,
		const int32 ComponentLabel,
		const int32 PrefixLevel,
		const int32 PowerIndex,
		const int32 ComponentMinYaw,
		const int32 ComponentMaxYaw,
		const int32 ComponentMinPitch,
		const int32 ComponentMaxPitch,
		const bool bMustContainInput,
		const int32 RequiredYaw,
		const int32 RequiredPitch)
	{
		FAimSliceCandidate Best;
		Best.Region.PrefixLevel = PrefixLevel;
		const int32 IntegralYawCount = Report.YawCount + 1;
		TArray<int32> Integral;
		Integral.Init(
			0,
			IntegralYawCount * (Report.PitchCount + 1));
		auto IntegralIndex = [IntegralYawCount](
			const int32 Pitch,
			const int32 Yaw)
		{
			return Pitch * IntegralYawCount + Yaw;
		};
		for (int32 PitchIndex = 0;
			PitchIndex < Report.PitchCount;
			++PitchIndex)
		{
			for (int32 YawIndex = 0;
				YawIndex < Report.YawCount;
				++YawIndex)
			{
				const int32 SampleIndex = FlattenIndex(
					YawIndex,
					PitchIndex,
					PowerIndex,
					Report.YawCount,
					Report.PitchCount);
				const int32 Cell =
					Labels.IsValidIndex(SampleIndex)
					&& Labels[SampleIndex] == ComponentLabel
					? 1
					: 0;
				Integral[IntegralIndex(PitchIndex + 1, YawIndex + 1)] =
					Cell
					+ Integral[IntegralIndex(PitchIndex, YawIndex + 1)]
					+ Integral[IntegralIndex(PitchIndex + 1, YawIndex)]
					- Integral[IntegralIndex(PitchIndex, YawIndex)];
			}
		}

		double BestBalancePixels = -1.0;
		int32 BestArea = -1;
		const int32 LastMinimumPitch = bMustContainInput
			? RequiredPitch
			: ComponentMaxPitch;
		const int32 LastMinimumYaw = bMustContainInput
			? RequiredYaw
			: ComponentMaxYaw;
		for (int32 MinPitch = ComponentMinPitch;
			MinPitch <= LastMinimumPitch;
			++MinPitch)
		{
			const int32 FirstMaximumPitch = bMustContainInput
				? RequiredPitch
				: MinPitch;
			for (int32 MaxPitch = FirstMaximumPitch;
				MaxPitch <= ComponentMaxPitch;
				++MaxPitch)
			{
				for (int32 MinYaw = ComponentMinYaw;
					MinYaw <= LastMinimumYaw;
					++MinYaw)
				{
					const int32 FirstMaximumYaw = bMustContainInput
						? RequiredYaw
						: MinYaw;
					for (int32 MaxYaw = FirstMaximumYaw;
						MaxYaw <= ComponentMaxYaw;
						++MaxYaw)
					{
						const int32 Occupied =
							Integral[IntegralIndex(
								MaxPitch + 1,
								MaxYaw + 1)]
							- Integral[IntegralIndex(
								MinPitch,
								MaxYaw + 1)]
							- Integral[IntegralIndex(
								MaxPitch + 1,
								MinYaw)]
							+ Integral[IntegralIndex(
								MinPitch,
								MinYaw)];
						const int32 Area =
							(MaxPitch - MinPitch + 1)
							* (MaxYaw - MinYaw + 1);
						if (Occupied != Area)
						{
							continue;
						}
						const double YawWidth =
							(MaxYaw - MinYaw)
							* Report.Grid.YawStepDegrees;
						const double PitchWidth =
							(MaxPitch - MinPitch)
							* Report.Grid.PitchStepDegrees;
						const double YawPixels =
							GetYawWidthPixels(Preset, YawWidth);
						const double PitchPixels =
							GetPitchWidthPixels(Preset, PitchWidth);
						const double BalancePixels =
							FMath::Min(YawPixels, PitchPixels);
						if (BalancePixels
								> BestBalancePixels + 1.0e-12
							|| (FMath::IsNearlyEqual(
									BalancePixels,
									BestBalancePixels,
									1.0e-12)
								&& Area > BestArea))
						{
							Best.bFound = true;
							BestBalancePixels = BalancePixels;
							BestArea = Area;
							Best.YawWidthPixels = YawPixels;
							Best.PitchWidthPixels = PitchPixels;
							Best.Region.Minimum =
								Report.Grid.GetInput(
									MinYaw,
									MinPitch,
									PowerIndex);
							Best.Region.Maximum =
								Report.Grid.GetInput(
									MaxYaw,
									MaxPitch,
									PowerIndex);
						}
					}
				}
			}
		}
		return Best;
	}

	void AnalyzePrefix(
		const FABTSM11FinaleLayoutPreset& Preset,
		const int32 PrefixLevel,
		FABTSM11LayoutCertificationReport& Report)
	{
		FABTSM11PrefixComponentSummary& Summary =
			Report.Prefixes[PrefixLevel - 1];
		Summary.PrefixLevel = PrefixLevel;
		Summary.SampleCount = Report.PrefixSampleCounts[PrefixLevel];
		TArray<int32> Labels;
		Summary.ComponentCount =
			FABTSM11FinaleLayoutCertification::CountComponents6(
				Report.Samples,
				Report.YawCount,
				Report.PitchCount,
				Report.PowerCount,
				PrefixLevel,
				&Labels);
		if (Summary.SampleCount == 0)
		{
			return;
		}

		int32 NominalYaw = 0;
		int32 NominalPitch = 0;
		int32 NominalPower = 0;
		if (!InputToIndex(
			Report.Grid,
			Preset.NominalInput,
			NominalYaw,
			NominalPitch,
			NominalPower))
		{
			return;
		}
		const int32 NominalIndex = FlattenIndex(
			NominalYaw,
			NominalPitch,
			NominalPower,
			Report.YawCount,
			Report.PitchCount);
		if (!Labels.IsValidIndex(NominalIndex) || Labels[NominalIndex] < 0)
		{
			return;
		}
		const int32 NominalLabel = Labels[NominalIndex];
		int32 MinYaw = MAX_int32;
		int32 MaxYaw = MIN_int32;
		int32 MinPitch = MAX_int32;
		int32 MaxPitch = MIN_int32;
		int32 MinPower = MAX_int32;
		int32 MaxPower = MIN_int32;
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			if (Labels[Index] != NominalLabel)
			{
				continue;
			}
			int32 YawIndex = 0;
			int32 PitchIndex = 0;
			int32 PowerIndex = 0;
			UnflattenIndex(
				Index,
				Report.YawCount,
				Report.PitchCount,
				YawIndex,
				PitchIndex,
				PowerIndex);
			MinYaw = FMath::Min(MinYaw, YawIndex);
			MaxYaw = FMath::Max(MaxYaw, YawIndex);
			MinPitch = FMath::Min(MinPitch, PitchIndex);
			MaxPitch = FMath::Max(MaxPitch, PitchIndex);
			MinPower = FMath::Min(MinPower, PowerIndex);
			MaxPower = FMath::Max(MaxPower, PowerIndex);
			++Summary.NominalComponentSampleCount;
		}
		if (Summary.NominalComponentSampleCount <= 0)
		{
			return;
		}

		Summary.Minimum = Report.Grid.GetInput(
			MinYaw, MinPitch, MinPower);
		Summary.Maximum = Report.Grid.GetInput(
			MaxYaw, MaxPitch, MaxPower);

		// Build a conservative solid box around the nominal grid point. Grow
		// one face at a time and prefer the largest resulting volume. This
		// balances the three axes without assuming that a diagonal success tube
		// can expand all six faces simultaneously.
		int32 TrustMinYaw = NominalYaw;
		int32 TrustMaxYaw = NominalYaw;
		int32 TrustMinPitch = NominalPitch;
		int32 TrustMaxPitch = NominalPitch;
		int32 TrustMinPower = NominalPower;
		int32 TrustMaxPower = NominalPower;
		for (;;)
		{
			int32 BestMinYaw = TrustMinYaw;
			int32 BestMaxYaw = TrustMaxYaw;
			int32 BestMinPitch = TrustMinPitch;
			int32 BestMaxPitch = TrustMaxPitch;
			int32 BestMinPower = TrustMinPower;
			int32 BestMaxPower = TrustMaxPower;
			int64 BestVolume = 0;
			for (int32 Face = 0; Face < 6; ++Face)
			{
				int32 CandidateMinYaw = TrustMinYaw;
				int32 CandidateMaxYaw = TrustMaxYaw;
				int32 CandidateMinPitch = TrustMinPitch;
				int32 CandidateMaxPitch = TrustMaxPitch;
				int32 CandidateMinPower = TrustMinPower;
				int32 CandidateMaxPower = TrustMaxPower;
				switch (Face)
				{
				case 0:
					CandidateMinYaw = FMath::Max(MinYaw, TrustMinYaw - 1);
					break;
				case 1:
					CandidateMaxYaw = FMath::Min(MaxYaw, TrustMaxYaw + 1);
					break;
				case 2:
					CandidateMinPitch =
						FMath::Max(MinPitch, TrustMinPitch - 1);
					break;
				case 3:
					CandidateMaxPitch =
						FMath::Min(MaxPitch, TrustMaxPitch + 1);
					break;
				case 4:
					CandidateMinPower =
						FMath::Max(MinPower, TrustMinPower - 1);
					break;
				default:
					CandidateMaxPower =
						FMath::Min(MaxPower, TrustMaxPower + 1);
					break;
				}
				if (CandidateMinYaw == TrustMinYaw
					&& CandidateMaxYaw == TrustMaxYaw
					&& CandidateMinPitch == TrustMinPitch
					&& CandidateMaxPitch == TrustMaxPitch
					&& CandidateMinPower == TrustMinPower
					&& CandidateMaxPower == TrustMaxPower)
				{
					continue;
				}
				if (!BoxIsInsidePrefix(
					Report.Samples,
					Report.YawCount,
					Report.PitchCount,
					PrefixLevel,
					CandidateMinYaw,
					CandidateMaxYaw,
					CandidateMinPitch,
					CandidateMaxPitch,
					CandidateMinPower,
					CandidateMaxPower))
				{
					continue;
				}
				const int64 Volume =
					static_cast<int64>(
						CandidateMaxYaw - CandidateMinYaw + 1)
					* (CandidateMaxPitch - CandidateMinPitch + 1)
					* (CandidateMaxPower - CandidateMinPower + 1);
				if (Volume > BestVolume)
				{
					BestVolume = Volume;
					BestMinYaw = CandidateMinYaw;
					BestMaxYaw = CandidateMaxYaw;
					BestMinPitch = CandidateMinPitch;
					BestMaxPitch = CandidateMaxPitch;
					BestMinPower = CandidateMinPower;
					BestMaxPower = CandidateMaxPower;
				}
			}
			if (BestVolume == 0)
			{
				break;
			}
			TrustMinYaw = BestMinYaw;
			TrustMaxYaw = BestMaxYaw;
			TrustMinPitch = BestMinPitch;
			TrustMaxPitch = BestMaxPitch;
			TrustMinPower = BestMinPower;
			TrustMaxPower = BestMaxPower;
		}
		const int32 ErosionCells =
			FMath::Max(0, FMath::CeilToInt(
				Preset.ScanContract.TrustErosionCells));
		if (TrustMaxYaw - TrustMinYaw + 1
			> 2 * ErosionCells + 1)
		{
			TrustMinYaw += ErosionCells;
			TrustMaxYaw -= ErosionCells;
		}
		if (TrustMaxPitch - TrustMinPitch + 1
			> 2 * ErosionCells + 1)
		{
			TrustMinPitch += ErosionCells;
			TrustMaxPitch -= ErosionCells;
		}
		if (TrustMaxPower - TrustMinPower + 1
			> 2 * ErosionCells + 1)
		{
			TrustMinPower += ErosionCells;
			TrustMaxPower -= ErosionCells;
		}
		Summary.TrustRegion.PrefixLevel = PrefixLevel;
		Summary.TrustRegion.Minimum = Report.Grid.GetInput(
			TrustMinYaw,
			TrustMinPitch,
			TrustMinPower);
		Summary.TrustRegion.Maximum = Report.Grid.GetInput(
			TrustMaxYaw,
			TrustMaxPitch,
			TrustMaxPower);

		TArray<FAimSliceCandidate> AimSlices;
		AimSlices.SetNum(Report.PowerCount);
		for (int32 PowerIndex = MinPower;
			PowerIndex <= MaxPower;
			++PowerIndex)
		{
			AimSlices[PowerIndex] = FindBestBalancedAimSlice(
				Preset,
				Report,
				Labels,
				NominalLabel,
				PrefixLevel,
				PowerIndex,
				MinYaw,
				MaxYaw,
				MinPitch,
				MaxPitch,
				false,
				NominalYaw,
				NominalPitch);
		}
		AimSlices[NominalPower] = FindBestBalancedAimSlice(
			Preset,
			Report,
			Labels,
			NominalLabel,
			PrefixLevel,
			NominalPower,
			MinYaw,
			MaxYaw,
			MinPitch,
			MaxPitch,
			true,
			NominalYaw,
			NominalPitch);
		const FAimSliceCandidate& NominalAim =
			AimSlices[NominalPower];
		Summary.PlayableAimRegion = NominalAim.Region;
		Summary.PlayableAimYawWidthPixels =
			NominalAim.YawWidthPixels;
		Summary.PlayableAimPitchWidthPixels =
			NominalAim.PitchWidthPixels;
		if (AimSliceMeetsPlayablePolicy(Preset, NominalAim))
		{
			int32 PlayableMinimumPowerIndex = NominalPower;
			int32 PlayableMaximumPowerIndex = NominalPower;
			while (PlayableMinimumPowerIndex > MinPower)
			{
				const int32 CandidateIndex =
					PlayableMinimumPowerIndex - 1;
				if (!AimSliceMeetsPlayablePolicy(
						Preset,
						AimSlices[CandidateIndex])
					|| !AimSlicesHavePlayableOverlap(
						Preset,
						AimSlices[PlayableMinimumPowerIndex],
						AimSlices[CandidateIndex]))
				{
					break;
				}
				PlayableMinimumPowerIndex = CandidateIndex;
			}
			while (PlayableMaximumPowerIndex < MaxPower)
			{
				const int32 CandidateIndex =
					PlayableMaximumPowerIndex + 1;
				if (!AimSliceMeetsPlayablePolicy(
						Preset,
						AimSlices[CandidateIndex])
					|| !AimSlicesHavePlayableOverlap(
						Preset,
						AimSlices[PlayableMaximumPowerIndex],
						AimSlices[CandidateIndex]))
				{
					break;
				}
				PlayableMaximumPowerIndex = CandidateIndex;
			}
			Summary.PlayablePowerSliceCount =
				PlayableMaximumPowerIndex
				- PlayableMinimumPowerIndex
				+ 1;
			Summary.PlayablePowerMinimum =
				Report.Grid.GetInput(
					NominalYaw,
					NominalPitch,
					PlayableMinimumPowerIndex).Power;
			Summary.PlayablePowerMaximum =
				Report.Grid.GetInput(
					NominalYaw,
					NominalPitch,
					PlayableMaximumPowerIndex).Power;
		}
	}

	FABTSM11InputGrid MakeHalfCellGrid(
		const FABTSM11FinaleLayoutPreset& Preset)
	{
		FABTSM11InputGrid Grid;
		Grid.Minimum = FABTSM11FinaleLaunchInput{
			Preset.LaunchModel.MinimumYawDegrees
				+ 0.5 * Preset.ScanContract.YawStepDegrees,
			Preset.LaunchModel.MinimumPitchDegrees
				+ 0.5 * Preset.ScanContract.PitchStepDegrees,
			Preset.LaunchModel.MinimumPower
				+ 0.5 * Preset.ScanContract.PowerStep};
		Grid.Maximum = FABTSM11FinaleLaunchInput{
			Preset.LaunchModel.MaximumYawDegrees
				- 0.5 * Preset.ScanContract.YawStepDegrees,
			Preset.LaunchModel.MaximumPitchDegrees
				- 0.5 * Preset.ScanContract.PitchStepDegrees,
			Preset.LaunchModel.MaximumPower
				- 0.5 * Preset.ScanContract.PowerStep};
		Grid.YawStepDegrees = Preset.ScanContract.YawStepDegrees;
		Grid.PitchStepDegrees = Preset.ScanContract.PitchStepDegrees;
		Grid.PowerStep = Preset.ScanContract.PowerStep;
		return Grid;
	}

	void AccumulateDiscoveryBounds(
		const FABTSM11LayoutCertificationReport& Report,
		bool& bInOutFound,
		FABTSM11FinaleLaunchInput& InOutMinimum,
		FABTSM11FinaleLaunchInput& InOutMaximum)
	{
		for (int32 FlatIndex = 0;
			FlatIndex < Report.Samples.Num();
			++FlatIndex)
		{
			const FABTSM11CertificationSample& Sample =
				Report.Samples[FlatIndex];
			if (Sample.HighestPrefixLevel < 3
				&& !Sample.bBypassTargetHit
				&& Sample.Termination
					!= EABTSM11TrajectoryTermination::TargetHit)
			{
				continue;
			}
			int32 YawIndex = 0;
			int32 PitchIndex = 0;
			int32 PowerIndex = 0;
			UnflattenIndex(
				FlatIndex,
				Report.YawCount,
				Report.PitchCount,
				YawIndex,
				PitchIndex,
				PowerIndex);
			const FABTSM11FinaleLaunchInput Input =
				Report.Grid.GetInput(
					YawIndex, PitchIndex, PowerIndex);
			if (!bInOutFound)
			{
				InOutMinimum = Input;
				InOutMaximum = Input;
				bInOutFound = true;
				continue;
			}
			InOutMinimum.YawDegrees = FMath::Min(
				InOutMinimum.YawDegrees, Input.YawDegrees);
			InOutMinimum.PitchDegrees = FMath::Min(
				InOutMinimum.PitchDegrees, Input.PitchDegrees);
			InOutMinimum.Power = FMath::Min(
				InOutMinimum.Power, Input.Power);
			InOutMaximum.YawDegrees = FMath::Max(
				InOutMaximum.YawDegrees, Input.YawDegrees);
			InOutMaximum.PitchDegrees = FMath::Max(
				InOutMaximum.PitchDegrees, Input.PitchDegrees);
			InOutMaximum.Power = FMath::Max(
				InOutMaximum.Power, Input.Power);
		}
	}

	double QuantizeDown(
		const double Value,
		const double DomainMinimum,
		const double Step)
	{
		return DomainMinimum
			+ FMath::FloorToDouble(
				(Value - DomainMinimum) / Step + 1.0e-9)
				* Step;
	}

	double QuantizeUp(
		const double Value,
		const double DomainMinimum,
		const double Step)
	{
		return DomainMinimum
			+ FMath::CeilToDouble(
				(Value - DomainMinimum) / Step - 1.0e-9)
				* Step;
	}

	bool BuildInitialRefinementGrid(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11LayoutCertificationReport& Base,
		const FABTSM11LayoutCertificationReport& HalfCell,
		FABTSM11InputGrid& OutGrid)
	{
		bool bFound = false;
		FABTSM11FinaleLaunchInput Minimum = Preset.NominalInput;
		FABTSM11FinaleLaunchInput Maximum = Preset.NominalInput;
		AccumulateDiscoveryBounds(Base, bFound, Minimum, Maximum);
		AccumulateDiscoveryBounds(HalfCell, bFound, Minimum, Maximum);
		if (!bFound)
		{
			return false;
		}
		const double Halo =
			static_cast<double>(
				Preset.ScanContract.RefinementHaloCoarseCells);
		Minimum.YawDegrees -=
			Halo * Preset.ScanContract.YawStepDegrees;
		Maximum.YawDegrees +=
			Halo * Preset.ScanContract.YawStepDegrees;
		Minimum.PitchDegrees -=
			Halo * Preset.ScanContract.PitchStepDegrees;
		Maximum.PitchDegrees +=
			Halo * Preset.ScanContract.PitchStepDegrees;
		Minimum.Power -= Halo * Preset.ScanContract.PowerStep;
		Maximum.Power += Halo * Preset.ScanContract.PowerStep;

		OutGrid.Minimum = FABTSM11FinaleLaunchInput{
			FMath::Clamp(
				QuantizeDown(
					Minimum.YawDegrees,
					Preset.LaunchModel.MinimumYawDegrees,
					Preset.ScanContract.FinalYawPrecisionDegrees),
				Preset.LaunchModel.MinimumYawDegrees,
				Preset.LaunchModel.MaximumYawDegrees),
			FMath::Clamp(
				QuantizeDown(
					Minimum.PitchDegrees,
					Preset.LaunchModel.MinimumPitchDegrees,
					Preset.ScanContract.FinalPitchPrecisionDegrees),
				Preset.LaunchModel.MinimumPitchDegrees,
				Preset.LaunchModel.MaximumPitchDegrees),
			FMath::Clamp(
				QuantizeDown(
					Minimum.Power,
					Preset.LaunchModel.MinimumPower,
					Preset.ScanContract.FinalPowerPrecision),
				Preset.LaunchModel.MinimumPower,
				Preset.LaunchModel.MaximumPower)};
		OutGrid.Maximum = FABTSM11FinaleLaunchInput{
			FMath::Clamp(
				QuantizeUp(
					Maximum.YawDegrees,
					Preset.LaunchModel.MinimumYawDegrees,
					Preset.ScanContract.FinalYawPrecisionDegrees),
				Preset.LaunchModel.MinimumYawDegrees,
				Preset.LaunchModel.MaximumYawDegrees),
			FMath::Clamp(
				QuantizeUp(
					Maximum.PitchDegrees,
					Preset.LaunchModel.MinimumPitchDegrees,
					Preset.ScanContract.FinalPitchPrecisionDegrees),
				Preset.LaunchModel.MinimumPitchDegrees,
				Preset.LaunchModel.MaximumPitchDegrees),
			FMath::Clamp(
				QuantizeUp(
					Maximum.Power,
					Preset.LaunchModel.MinimumPower,
					Preset.ScanContract.FinalPowerPrecision),
				Preset.LaunchModel.MinimumPower,
				Preset.LaunchModel.MaximumPower)};
		OutGrid.YawStepDegrees =
			Preset.ScanContract.FinalYawPrecisionDegrees;
		OutGrid.PitchStepDegrees =
			Preset.ScanContract.FinalPitchPrecisionDegrees;
		OutGrid.PowerStep = Preset.ScanContract.FinalPowerPrecision;
		return true;
	}

	bool ExpandForOpenBoundary(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11LayoutCertificationReport& Report,
		FABTSM11InputGrid& InOutGrid)
	{
		bool bExpandMinimumYaw = false;
		bool bExpandMaximumYaw = false;
		bool bExpandMinimumPitch = false;
		bool bExpandMaximumPitch = false;
		bool bExpandMinimumPower = false;
		bool bExpandMaximumPower = false;
		for (int32 FlatIndex = 0;
			FlatIndex < Report.Samples.Num();
			++FlatIndex)
		{
			const FABTSM11CertificationSample& Sample =
				Report.Samples[FlatIndex];
			if (Sample.HighestPrefixLevel < 3
				&& !Sample.bBypassTargetHit
				&& Sample.Termination
					!= EABTSM11TrajectoryTermination::TargetHit)
			{
				continue;
			}
			int32 YawIndex = 0;
			int32 PitchIndex = 0;
			int32 PowerIndex = 0;
			UnflattenIndex(
				FlatIndex,
				Report.YawCount,
				Report.PitchCount,
				YawIndex,
				PitchIndex,
				PowerIndex);
			bExpandMinimumYaw |=
				YawIndex == 0
				&& InOutGrid.Minimum.YawDegrees
					> Preset.LaunchModel.MinimumYawDegrees;
			bExpandMaximumYaw |=
				YawIndex == Report.YawCount - 1
				&& InOutGrid.Maximum.YawDegrees
					< Preset.LaunchModel.MaximumYawDegrees;
			bExpandMinimumPitch |=
				PitchIndex == 0
				&& InOutGrid.Minimum.PitchDegrees
					> Preset.LaunchModel.MinimumPitchDegrees;
			bExpandMaximumPitch |=
				PitchIndex == Report.PitchCount - 1
				&& InOutGrid.Maximum.PitchDegrees
					< Preset.LaunchModel.MaximumPitchDegrees;
			bExpandMinimumPower |=
				PowerIndex == 0
				&& InOutGrid.Minimum.Power
					> Preset.LaunchModel.MinimumPower;
			bExpandMaximumPower |=
				PowerIndex == Report.PowerCount - 1
				&& InOutGrid.Maximum.Power
					< Preset.LaunchModel.MaximumPower;
		}
		if (!(bExpandMinimumYaw
			|| bExpandMaximumYaw
			|| bExpandMinimumPitch
			|| bExpandMaximumPitch
			|| bExpandMinimumPower
			|| bExpandMaximumPower))
		{
			return false;
		}
		const double Halo =
			static_cast<double>(
				Preset.ScanContract.RefinementHaloCoarseCells);
		if (bExpandMinimumYaw)
		{
			InOutGrid.Minimum.YawDegrees = FMath::Max(
				Preset.LaunchModel.MinimumYawDegrees,
				InOutGrid.Minimum.YawDegrees
					- Halo * Preset.ScanContract.YawStepDegrees);
		}
		if (bExpandMaximumYaw)
		{
			InOutGrid.Maximum.YawDegrees = FMath::Min(
				Preset.LaunchModel.MaximumYawDegrees,
				InOutGrid.Maximum.YawDegrees
					+ Halo * Preset.ScanContract.YawStepDegrees);
		}
		if (bExpandMinimumPitch)
		{
			InOutGrid.Minimum.PitchDegrees = FMath::Max(
				Preset.LaunchModel.MinimumPitchDegrees,
				InOutGrid.Minimum.PitchDegrees
					- Halo * Preset.ScanContract.PitchStepDegrees);
		}
		if (bExpandMaximumPitch)
		{
			InOutGrid.Maximum.PitchDegrees = FMath::Min(
				Preset.LaunchModel.MaximumPitchDegrees,
				InOutGrid.Maximum.PitchDegrees
					+ Halo * Preset.ScanContract.PitchStepDegrees);
		}
		if (bExpandMinimumPower)
		{
			InOutGrid.Minimum.Power = FMath::Max(
				Preset.LaunchModel.MinimumPower,
				InOutGrid.Minimum.Power
					- Halo * Preset.ScanContract.PowerStep);
		}
		if (bExpandMaximumPower)
		{
			InOutGrid.Maximum.Power = FMath::Min(
				Preset.LaunchModel.MaximumPower,
				InOutGrid.Maximum.Power
					+ Halo * Preset.ScanContract.PowerStep);
		}
		return true;
	}
}

FABTSM11PrefixClassification FABTSM11PrefixClassifier::Classify(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& Result,
	const uint8 EnabledAssistMask)
{
	using namespace ABTSM11FinaleCertificationPrivate;
	FABTSM11PrefixClassification Classification;
	for (double& Quality : Classification.CorridorQuality)
	{
		Quality = 0.0;
	}
	for (double& Energy : Classification.AppliedEnergyGain)
	{
		Energy = 0.0;
	}
	Classification.bWrongOrder =
		Result.Termination == EABTSM11TrajectoryTermination::WrongOrder
		|| Result.FindFirstEvent(EABTSM11TrajectoryEventType::WrongOrder)
			!= nullptr;
	Classification.bExceededOrbitLimit = ExceedsOrbitLimit(Preset, Result);

	for (const FABTSM11TrajectoryPoint& Point : Result.Points)
	{
		Classification.MinimumTargetDistanceCM = FMath::Min(
			Classification.MinimumTargetDistanceCM,
			(Point.PositionCM - Preset.CanonicalScenario.Target.CenterCM).Length());
	}

	TStaticArray<bool, FABTSM11GravityScenario::AssistCount> Valid;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		Valid[AssistIndex - 1] = IsValidAssist(
			Preset,
			Result,
			EnabledAssistMask,
			AssistIndex,
			Classification);
		if (Valid[AssistIndex - 1])
		{
			Classification.ValidAssistMask |=
				1u << (AssistIndex - 1);
		}
	}

	const FABTSM11TrajectoryEvent* Exit1 = Result.FindAssistEvent(
		EABTSM11TrajectoryEventType::AssistExit, 1);
	const FABTSM11TrajectoryEvent* Exit2 = Result.FindAssistEvent(
		EABTSM11TrajectoryEventType::AssistExit, 2);
	const FABTSM11TrajectoryEvent* Exit3 = Result.FindAssistEvent(
		EABTSM11TrajectoryEventType::AssistExit, 3);
	const FABTSM11TrajectoryEvent* TargetHit =
		Result.FindFirstEvent(EABTSM11TrajectoryEventType::TargetHit);
	Classification.bGeometricTargetContact =
		Result.DidContactTarget();

	if (!Classification.bWrongOrder
		&& !Classification.bExceededOrbitLimit
		&& Valid[0]
		&& Exit1 != nullptr
		&& HasLaterAssistEnter(Result, 2, Exit1->TimeSeconds))
	{
		Classification.HighestPrefixLevel = 1;
	}
	if (Classification.HighestPrefixLevel >= 1
		&& Valid[1]
		&& Exit2 != nullptr
		&& HasLaterAssistEnter(Result, 3, Exit2->TimeSeconds))
	{
		Classification.HighestPrefixLevel = 2;
	}
	if (Classification.HighestPrefixLevel >= 2
		&& Valid[2]
		&& Exit3 != nullptr
		&& SweepsTargetApproachAfter(Preset, Result, Exit3->TimeSeconds))
	{
		Classification.HighestPrefixLevel = 3;
		Classification.bEnteredTargetApproach = true;
	}
	if (Classification.HighestPrefixLevel >= 3
		&& Result.CompletedAssistCount == 3
		&& Result.DidHitTarget()
		&& TargetHit != nullptr
		&& Exit3 != nullptr
		&& TargetHit->TimeSeconds > Exit3->TimeSeconds)
	{
		Classification.HighestPrefixLevel = 4;
	}
	Classification.bBypassTargetHit =
		Classification.bGeometricTargetContact
		&& Classification.HighestPrefixLevel < 4;
	return Classification;
}

int32 FABTSM11FinaleLayoutCertification::CountComponents6(
	const TConstArrayView<FABTSM11CertificationSample> Samples,
	const int32 YawCount,
	const int32 PitchCount,
	const int32 PowerCount,
	const int32 PrefixLevel,
	TArray<int32>* OutLabels)
{
	using namespace ABTSM11FinaleCertificationPrivate;
	const int32 ExpectedCount = YawCount * PitchCount * PowerCount;
	if (YawCount <= 0
		|| PitchCount <= 0
		|| PowerCount <= 0
		|| PrefixLevel < 1
		|| PrefixLevel > 4
		|| Samples.Num() != ExpectedCount)
	{
		if (OutLabels != nullptr)
		{
			OutLabels->Reset();
		}
		return 0;
	}

	TArray<int32> Labels;
	Labels.Init(-1, ExpectedCount);
	TArray<int32> Queue;
	Queue.Reserve(ExpectedCount);
	int32 ComponentCount = 0;
	for (int32 SeedIndex = 0; SeedIndex < ExpectedCount; ++SeedIndex)
	{
		if (Labels[SeedIndex] >= 0
			|| Samples[SeedIndex].HighestPrefixLevel < PrefixLevel)
		{
			continue;
		}
		const int32 Label = ComponentCount++;
		Labels[SeedIndex] = Label;
		Queue.Reset();
		Queue.Add(SeedIndex);
		for (int32 ReadIndex = 0; ReadIndex < Queue.Num(); ++ReadIndex)
		{
			int32 YawIndex = 0;
			int32 PitchIndex = 0;
			int32 PowerIndex = 0;
			UnflattenIndex(
				Queue[ReadIndex],
				YawCount,
				PitchCount,
				YawIndex,
				PitchIndex,
				PowerIndex);
			const int32 DeltaYaw[6] = {-1, 1, 0, 0, 0, 0};
			const int32 DeltaPitch[6] = {0, 0, -1, 1, 0, 0};
			const int32 DeltaPower[6] = {0, 0, 0, 0, -1, 1};
			for (int32 Neighbor = 0; Neighbor < 6; ++Neighbor)
			{
				const int32 NYaw = YawIndex + DeltaYaw[Neighbor];
				const int32 NPitch = PitchIndex + DeltaPitch[Neighbor];
				const int32 NPower = PowerIndex + DeltaPower[Neighbor];
				if (NYaw < 0 || NYaw >= YawCount
					|| NPitch < 0 || NPitch >= PitchCount
					|| NPower < 0 || NPower >= PowerCount)
				{
					continue;
				}
				const int32 NeighborIndex = FlattenIndex(
					NYaw, NPitch, NPower, YawCount, PitchCount);
				if (Labels[NeighborIndex] < 0
					&& Samples[NeighborIndex].HighestPrefixLevel
						>= PrefixLevel)
				{
					Labels[NeighborIndex] = Label;
					Queue.Add(NeighborIndex);
				}
			}
		}
	}
	if (OutLabels != nullptr)
	{
		*OutLabels = MoveTemp(Labels);
	}
	return ComponentCount;
}

bool FABTSM11FinaleLayoutCertification::ScanGrid(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11InputGrid& Grid,
	const uint8 EnabledAssistMask,
	FABTSM11LayoutCertificationReport& OutReport,
	FString* OutFailure)
{
	using namespace ABTSM11FinaleCertificationPrivate;
	OutReport.ReportVersion = 3;
	OutReport.YawCount = 0;
	OutReport.PitchCount = 0;
	OutReport.PowerCount = 0;
	OutReport.TotalSampleCount = 0;
	OutReport.SolverInvocationCount = 0;
	OutReport.InvalidRequestCount = 0;
	OutReport.TargetContactCount = 0;
	OutReport.TargetHitCount = 0;
	OutReport.BypassTargetHitCount = 0;
	for (int32& Count : OutReport.PrefixSampleCounts)
	{
		Count = 0;
	}
	for (int32 Index = 0; Index < OutReport.Prefixes.Num(); ++Index)
	{
		OutReport.Prefixes[Index] = FABTSM11PrefixComponentSummary();
		OutReport.Prefixes[Index].PrefixLevel = Index + 1;
	}
	OutReport.Samples.Reset();
	OutReport.ReportHash = 0;
	OutReport.bPassed = false;
	OutReport.Failure.Reset();
	OutReport.EnabledAssistMask = EnabledAssistMask & 0x7u;
	OutReport.PresetHash = Preset.PresetHash;
	OutReport.ScenarioHash = Preset.CanonicalScenario.ScenarioHash;
	OutReport.ScanContractHash = Preset.ScanContractHash;
	OutReport.Grid = Grid;
	FString ValidationFailure;
	if (!Preset.IsValid(&ValidationFailure)
		|| !Grid.IsValid(Preset.LaunchModel, &ValidationFailure))
	{
		OutReport.Failure = ValidationFailure;
		OutReport.bPassed = false;
		OutReport.ReportHash =
			FABTSM11FinaleLayoutHash::ComputeReportHash(OutReport);
		if (OutFailure != nullptr)
		{
			*OutFailure = OutReport.Failure;
		}
		return false;
	}

	OutReport.YawCount = Grid.GetYawCount();
	OutReport.PitchCount = Grid.GetPitchCount();
	OutReport.PowerCount = Grid.GetPowerCount();
	OutReport.TotalSampleCount = Grid.GetSampleCount();
	OutReport.Samples.SetNum(OutReport.TotalSampleCount);

	std::atomic<int32> SolverInvocationCount = 0;
	std::atomic<int32> InvalidRequestCount = 0;
	ParallelFor(
		OutReport.TotalSampleCount,
		[&Preset, &OutReport, &SolverInvocationCount, &InvalidRequestCount](
			const int32 FlatIndex)
		{
			int32 YawIndex = 0;
			int32 PitchIndex = 0;
			int32 PowerIndex = 0;
			UnflattenIndex(
				FlatIndex,
				OutReport.YawCount,
				OutReport.PitchCount,
				YawIndex,
				PitchIndex,
				PowerIndex);
			const FABTSM11FinaleLaunchInput Input =
				OutReport.Grid.GetInput(
					YawIndex,
					PitchIndex,
					PowerIndex);
			FABTSM11CertificationSample& Sample =
				OutReport.Samples[FlatIndex];
			if (CanPruneAsUnreachable(Preset, Input))
			{
				Sample.Termination =
					EABTSM11TrajectoryTermination::SolarCaptured;
				return;
			}

			FABTSM11TrajectoryRequest Request;
			FString BuildFailure;
			if (!Preset.BuildRequest(
				Input,
				OutReport.EnabledAssistMask,
				Request,
				&BuildFailure))
			{
				++InvalidRequestCount;
				Sample.Termination =
					EABTSM11TrajectoryTermination::InvalidInput;
				return;
			}
			++SolverInvocationCount;
			FABTSM11TrajectoryResult Result;
			if (!FABTSM11GravityAssistSolver::Solve(Request, Result))
			{
				++InvalidRequestCount;
				Sample.Termination =
					EABTSM11TrajectoryTermination::InvalidInput;
				return;
			}
			const FABTSM11PrefixClassification Classification =
				FABTSM11PrefixClassifier::Classify(
					Preset, Result, OutReport.EnabledAssistMask);
			Sample.HighestPrefixLevel =
				Classification.HighestPrefixLevel;
			Sample.ValidAssistMask = Classification.ValidAssistMask;
			Sample.Termination = Result.Termination;
			Sample.bTargetContact =
				Classification.bGeometricTargetContact;
			Sample.bBypassTargetHit =
				Classification.bBypassTargetHit;
			Sample.bWrongOrder = Classification.bWrongOrder;
			Sample.bExceededOrbitLimit =
				Classification.bExceededOrbitLimit;
			Sample.TrajectoryHash = Result.ValidationHash;
		});

	OutReport.SolverInvocationCount = SolverInvocationCount.load();
	OutReport.InvalidRequestCount = InvalidRequestCount.load();
	for (const FABTSM11CertificationSample& Sample : OutReport.Samples)
	{
		if (Sample.bTargetContact)
		{
			++OutReport.TargetContactCount;
		}
		if (Sample.Termination == EABTSM11TrajectoryTermination::TargetHit)
		{
			++OutReport.TargetHitCount;
		}
		if (Sample.bBypassTargetHit)
		{
			++OutReport.BypassTargetHitCount;
		}
		for (int32 PrefixLevel = 1; PrefixLevel <= 4; ++PrefixLevel)
		{
			if (Sample.HighestPrefixLevel >= PrefixLevel)
			{
				++OutReport.PrefixSampleCounts[PrefixLevel];
			}
		}
	}
	for (int32 PrefixLevel = 1; PrefixLevel <= 4; ++PrefixLevel)
	{
		AnalyzePrefix(Preset, PrefixLevel, OutReport);
	}
	for (int32 PrefixIndex = 1; PrefixIndex >= 0; --PrefixIndex)
	{
		FABTSM11PrefixTrustRegion& Shallower =
			OutReport.Prefixes[PrefixIndex].TrustRegion;
		const FABTSM11PrefixTrustRegion& Deeper =
			OutReport.Prefixes[PrefixIndex + 1].TrustRegion;
		if (!Shallower.Contains(Deeper))
		{
			const int32 PrefixLevel = Shallower.PrefixLevel;
			const double CaptureMarginCells =
				Shallower.CaptureMarginCells;
			const double ReleaseMarginCells =
				Shallower.ReleaseMarginCells;
			Shallower = Deeper;
			Shallower.PrefixLevel = PrefixLevel;
			Shallower.CaptureMarginCells = CaptureMarginCells;
			Shallower.ReleaseMarginCells = ReleaseMarginCells;
			Shallower.RegionHash = 0;
		}
	}

	if (OutReport.InvalidRequestCount != 0)
	{
		OutReport.Failure = TEXT("InvalidRequestsDuringScan");
	}
	else if (OutReport.EnabledAssistMask == 0x7u)
	{
		if (OutReport.Prefixes[0].ComponentCount != 1
			|| OutReport.Prefixes[1].ComponentCount != 1
			|| OutReport.Prefixes[2].ComponentCount != 1
			|| OutReport.Prefixes[3].ComponentCount != 1)
		{
			OutReport.Failure = TEXT("PrefixComponentCount");
		}
		else if (OutReport.BypassTargetHitCount != 0
			|| OutReport.TargetHitCount
				!= OutReport.PrefixSampleCounts[4])
		{
			OutReport.Failure = TEXT("BypassTargetHit");
		}
		else if (OutReport.PrefixSampleCounts[1]
				<= OutReport.PrefixSampleCounts[2]
			|| OutReport.PrefixSampleCounts[2]
				<= OutReport.PrefixSampleCounts[3]
			|| OutReport.PrefixSampleCounts[3]
				<= OutReport.PrefixSampleCounts[4])
		{
			OutReport.Failure = TEXT("PrefixSetsNotProperlyNested");
		}
		else
		{
			const FABTSM11PrefixComponentSummary& F4 =
				OutReport.Prefixes[3];
			const double YawWidth =
				F4.PlayableAimRegion.Maximum.YawDegrees
				- F4.PlayableAimRegion.Minimum.YawDegrees;
			const double PitchWidth =
				F4.PlayableAimRegion.Maximum.PitchDegrees
				- F4.PlayableAimRegion.Minimum.PitchDegrees;
			if (YawWidth < Preset.ScanContract.MinimumF4YawWidthDegrees
				|| PitchWidth
					< Preset.ScanContract.MinimumF4PitchWidthDegrees
				|| F4.PlayableAimYawWidthPixels
					< Preset.ScanContract.MinimumScreenTrustWidthPixels
				|| F4.PlayableAimPitchWidthPixels
					< Preset.ScanContract.MinimumScreenTrustWidthPixels)
			{
				OutReport.Failure = TEXT("F4BelowPlayableWidth");
			}
			else if (F4.PlayablePowerSliceCount
				< Preset.ScanContract.MinimumPlayableF4PowerSliceCount)
			{
				OutReport.Failure =
					TEXT("F4InsufficientConsecutivePowerSlices");
			}
			else if (Preset.LaunchModel.MaximumPower
					- Preset.NominalInput.Power
				> Preset.ScanContract
					.MaximumLockedPowerDeficitFromFullPower)
			{
				OutReport.Failure = TEXT("NominalPowerNotNearMaximum");
			}
			else if (OutReport.Prefixes[0].Minimum.Power
					< Preset.ScanContract.MinimumF1OnsetPower
				|| OutReport.Prefixes[0].Minimum.Power
					> Preset.ScanContract.MaximumF1OnsetPower)
			{
				OutReport.Failure = TEXT("F1PowerOnsetOutsidePolicy");
			}
			else if (!OutReport.Prefixes[0].TrustRegion.Contains(
					OutReport.Prefixes[1].TrustRegion)
				|| !OutReport.Prefixes[1].TrustRegion.Contains(
					OutReport.Prefixes[2].TrustRegion))
			{
				OutReport.Failure = TEXT("TrustRegionsNotNested");
			}
		}
	}
	else if (OutReport.TargetHitCount != 0)
	{
		OutReport.Failure = TEXT("AblatedTargetHit");
	}
	OutReport.bPassed = OutReport.Failure.IsEmpty();
	OutReport.ReportHash =
		FABTSM11FinaleLayoutHash::ComputeReportHash(OutReport);
	if (!OutReport.bPassed && OutFailure != nullptr)
	{
		*OutFailure = OutReport.Failure;
	}
	return true;
}

bool FABTSM11FinaleLayoutCertification::ScanRegularGrid(
	const FABTSM11FinaleLayoutPreset& Preset,
	const uint8 EnabledAssistMask,
	FABTSM11LayoutCertificationReport& OutReport,
	FString* OutFailure)
{
	return ScanGrid(
		Preset,
		FABTSM11InputGrid::MakeFullDomain(
			Preset.LaunchModel, Preset.ScanContract),
		EnabledAssistMask,
		OutReport,
		OutFailure);
}

bool FABTSM11FinaleLayoutCertification::Certify(
	const FABTSM11FinaleLayoutPreset& Preset,
	FABTSM11CertificationSuiteReport& OutSuite,
	FString* OutFailure)
{
	using namespace ABTSM11FinaleCertificationPrivate;
	OutSuite = FABTSM11CertificationSuiteReport();
	FString ValidationFailure;
	if (!Preset.IsValid(&ValidationFailure))
	{
		OutSuite.Failure = ValidationFailure;
		OutSuite.bPassed = false;
		OutSuite.SuiteHash =
			FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
				OutSuite);
		if (OutFailure != nullptr)
		{
			*OutFailure = OutSuite.Failure;
		}
		return false;
	}
	const FABTSM11InputGrid HalfCellGrid = MakeHalfCellGrid(Preset);
	if (!ScanRegularGrid(Preset, 0x7u, OutSuite.Baseline)
		|| !ScanGrid(
			Preset,
			HalfCellGrid,
			0x7u,
			OutSuite.HalfCellBaseline))
	{
		OutSuite.Failure = TEXT("DiscoveryScanExecutionFailed");
		OutSuite.bPassed = false;
		OutSuite.SuiteHash =
			FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
				OutSuite);
		if (OutFailure != nullptr)
		{
			*OutFailure = OutSuite.Failure;
		}
		return false;
	}
	OutSuite.DiscoverySampleCount =
		OutSuite.Baseline.TotalSampleCount
		+ OutSuite.HalfCellBaseline.TotalSampleCount;
	OutSuite.TotalSolverInvocationCount =
		OutSuite.Baseline.SolverInvocationCount
		+ OutSuite.HalfCellBaseline.SolverInvocationCount;
	if (OutSuite.Baseline.InvalidRequestCount != 0
		|| OutSuite.HalfCellBaseline.InvalidRequestCount != 0)
	{
		OutSuite.Failure = TEXT("InvalidDiscoveryRequest");
	}
	else if (OutSuite.Baseline.PrefixSampleCounts[4] == 0
		|| OutSuite.HalfCellBaseline.PrefixSampleCounts[4] == 0)
	{
		OutSuite.Failure = TEXT("F4MissingFromDiscoveryPhase");
	}
	else if (OutSuite.Baseline.TargetContactCount != 0
		|| OutSuite.HalfCellBaseline.TargetContactCount != 0
		|| OutSuite.Baseline.BypassTargetHitCount != 0
		|| OutSuite.HalfCellBaseline.BypassTargetHitCount != 0
		|| OutSuite.Baseline.TargetHitCount
			!= OutSuite.Baseline.PrefixSampleCounts[4]
		|| OutSuite.HalfCellBaseline.TargetHitCount
			!= OutSuite.HalfCellBaseline.PrefixSampleCounts[4])
	{
		OutSuite.Failure = TEXT("DiscoveryTargetBypass");
	}

	FABTSM11InputGrid RefinementGrid;
	if (OutSuite.Failure.IsEmpty()
		&& !BuildInitialRefinementGrid(
			Preset,
			OutSuite.Baseline,
			OutSuite.HalfCellBaseline,
			RefinementGrid))
	{
		OutSuite.Failure = TEXT("NoRefinementCandidate");
	}
	if (OutSuite.Failure.IsEmpty())
	{
		OutSuite.bDiscoveryCoverageComplete = true;
		for (int32 Iteration = 0;
			Iteration < Preset.ScanContract.MaximumRefinementIterations;
			++Iteration)
		{
			if (RefinementGrid.GetSampleCount()
				> Preset.ScanContract.MaximumRefinementSampleCount)
			{
				OutSuite.Failure = TEXT("RefinementSampleBudgetExceeded");
				break;
			}
			if (!ScanGrid(
				Preset,
				RefinementGrid,
				0x7u,
				OutSuite.RefinedBaseline))
			{
				OutSuite.Failure = TEXT("RefinementScanExecutionFailed");
				break;
			}
			++OutSuite.RefinementIterationCount;
			OutSuite.RefinementSampleCount +=
				OutSuite.RefinedBaseline.TotalSampleCount;
			OutSuite.TotalSolverInvocationCount +=
				OutSuite.RefinedBaseline.SolverInvocationCount;
			if (!ExpandForOpenBoundary(
				Preset,
				OutSuite.RefinedBaseline,
				RefinementGrid))
			{
				OutSuite.bClosureConverged = true;
				break;
			}
		}
		if (!OutSuite.bClosureConverged
			&& OutSuite.Failure.IsEmpty())
		{
			OutSuite.Failure = TEXT("RefinementClosureDidNotConverge");
		}
	}
	if (OutSuite.Failure.IsEmpty()
		&& !OutSuite.RefinedBaseline.bPassed)
	{
		OutSuite.Failure = OutSuite.RefinedBaseline.Failure;
	}

	for (int32 Index = 0; Index < OutSuite.Ablations.Num(); ++Index)
	{
		if (!ScanRegularGrid(
			Preset,
			OutSuite.AblationMasks[Index],
			OutSuite.Ablations[Index])
			|| !ScanGrid(
				Preset,
				HalfCellGrid,
				OutSuite.AblationMasks[Index],
				OutSuite.HalfCellAblations[Index])
			|| !ScanGrid(
				Preset,
				RefinementGrid,
				OutSuite.AblationMasks[Index],
				OutSuite.RefinedAblations[Index]))
		{
			OutSuite.Failure = TEXT("AblationScanExecutionFailed");
			OutSuite.bPassed = false;
			OutSuite.SuiteHash =
				FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
					OutSuite);
			if (OutFailure != nullptr)
			{
				*OutFailure = OutSuite.Failure;
			}
			return false;
		}
		OutSuite.TotalSolverInvocationCount +=
			OutSuite.Ablations[Index].SolverInvocationCount
			+ OutSuite.HalfCellAblations[Index].SolverInvocationCount
			+ OutSuite.RefinedAblations[Index].SolverInvocationCount;
		if (OutSuite.Ablations[Index].InvalidRequestCount != 0
			|| OutSuite.HalfCellAblations[Index].InvalidRequestCount != 0
			|| OutSuite.RefinedAblations[Index].InvalidRequestCount != 0
			|| OutSuite.Ablations[Index].TargetHitCount != 0
			|| OutSuite.HalfCellAblations[Index].TargetHitCount != 0
			|| OutSuite.RefinedAblations[Index].TargetHitCount != 0
			|| OutSuite.Ablations[Index].TargetContactCount != 0
			|| OutSuite.HalfCellAblations[Index].TargetContactCount != 0
			|| OutSuite.RefinedAblations[Index].TargetContactCount != 0
			|| OutSuite.Ablations[Index].BypassTargetHitCount != 0
			|| OutSuite.HalfCellAblations[Index].BypassTargetHitCount != 0
			|| OutSuite.RefinedAblations[Index].BypassTargetHitCount != 0)
		{
			if (OutSuite.Failure.IsEmpty())
			{
				OutSuite.Failure = TEXT("AblationCertificationFailed");
			}
		}
	}

	OutSuite.bPassed = OutSuite.Failure.IsEmpty()
		&& OutSuite.bDiscoveryCoverageComplete
		&& OutSuite.bClosureConverged;
	OutSuite.SuiteHash =
		FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(OutSuite);
	if (!OutSuite.bPassed && OutFailure != nullptr)
	{
		*OutFailure = OutSuite.Failure;
	}
	return true;
}
