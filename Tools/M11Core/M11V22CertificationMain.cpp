// Copyright Epic Games, Inc. All Rights Reserved.

#include "M11Search/ABTSM11CandidateSearch.h"
#include "M11Search/ABTSM11FrozenCandidateLayouts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	namespace fs = std::filesystem;
	using ABTS::M11Search::CandidateLayout;
	using ABTS::M11Search::CandidateRecord;
	using ABTS::M11Search::CandidateSearch;
	using ABTS::M11Search::CandidateSearchContract;
	using ABTS::M11Search::FrozenCandidateIdentity;
	using ABTS::M11Search::InputEvaluation;
	using ABTS::M11Search::LaunchInput;

	struct Options
	{
		bool Merge = false;
		bool ScreenAim = false;
		std::int32_t Rank = 3;
		fs::path Output;
		fs::path InputRoot;
		std::uint32_t Threads = 1;
		std::uint32_t ShardIndex = 0;
		std::uint32_t ShardCount = 1;
		double YawStep = 2.0;
		double PitchStep = 3.0;
		double PowerStep = 0.025;
		double MinYaw = std::numeric_limits<double>::quiet_NaN();
		double MaxYaw = std::numeric_limits<double>::quiet_NaN();
		double MinPitch = std::numeric_limits<double>::quiet_NaN();
		double MaxPitch = std::numeric_limits<double>::quiet_NaN();
		double MinPower = std::numeric_limits<double>::quiet_NaN();
		double MaxPower = std::numeric_limits<double>::quiet_NaN();
		double TargetOffsetXCM = 0.0;
		double TargetOffsetYCM = 0.0;
		double TargetOffsetZCM = 0.0;
		double ConstellationDistanceCM = 0.0;
		std::array<double, 4> CelestialRadialDeltaCM{};
		double Assist3OffsetXCM = 0.0;
		double Assist3OffsetYCM = 0.0;
		double Assist3OffsetZCM = 0.0;
		double Assist3BPlaneTargetTDeltaCM = 0.0;
		double Assist3BPlaneTargetRDeltaCM = 0.0;
		double Assist3BPlaneSigmaScale = 1.0;
		double Assist3VelocityDeltaXCMPerSec = 0.0;
		double Assist3VelocityDeltaYCMPerSec = 0.0;
		double Assist3VelocityDeltaZCMPerSec = 0.0;
		double Assist2OffsetXCM = 0.0;
		double Assist2OffsetYCM = 0.0;
		double Assist2OffsetZCM = 0.0;
		double Assist2BPlaneTargetTDeltaCM = 0.0;
		double Assist2BPlaneTargetRDeltaCM = 0.0;
		double Assist2BPlaneSigmaScale = 1.0;
		double Assist2VelocityDeltaXCMPerSec = 0.0;
		double Assist2VelocityDeltaYCMPerSec = 0.0;
		double Assist2VelocityDeltaZCMPerSec = 0.0;
		double TargetHitRadiusCM =
			std::numeric_limits<double>::quiet_NaN();
		double TargetMinimumCorridorQuality =
			std::numeric_limits<double>::quiet_NaN();
		double ArrivalConeDegrees = 180.0;
		double ArrivalFaceConeDegrees = 180.0;
		std::uint32_t CheckpointEvery = 256;
		std::uint32_t ScreenAimSampleCount = 5000;
		bool Resume = false;
	};

	struct Grid
	{
		double MinYaw = -18.0;
		double MaxYaw = 18.0;
		double MinPitch = 0.0;
		double MaxPitch = 60.0;
		double MinPower = 0.0;
		double MaxPower = 1.0;
		double YawStep = 2.0;
		double PitchStep = 3.0;
		double PowerStep = 0.025;
		std::int32_t YawCount = 0;
		std::int32_t PitchCount = 0;
		std::int32_t PowerCount = 0;

		std::uint64_t Count() const
		{
			return static_cast<std::uint64_t>(YawCount)
				* static_cast<std::uint64_t>(PitchCount)
				* static_cast<std::uint64_t>(PowerCount);
		}
	};

	struct Sample
	{
		std::uint64_t GlobalIndex = 0;
		std::int32_t YawIndex = 0;
		std::int32_t PitchIndex = 0;
		std::int32_t PowerIndex = 0;
		std::uint8_t PrefixMask = 0;
		std::uint8_t Termination = 0;
		std::int32_t CompletedAssistCount = 0;
		std::int32_t TargetContactCount = 0;
		std::uint64_t ResultHash = 0;
	};

	bool ParseUnsigned(const std::string& Text, std::uint64_t& Out)
	{
		const char* Begin = Text.data();
		const char* End = Begin + Text.size();
		const auto Result = std::from_chars(Begin, End, Out);
		return Result.ec == std::errc() && Result.ptr == End;
	}

	bool ParseDouble(const std::string& Text, double& Out)
	{
		char* End = nullptr;
		Out = std::strtod(Text.c_str(), &End);
		return End == Text.c_str() + Text.size() && std::isfinite(Out);
	}

	bool ParseOptions(
		const int Argc,
		char** Argv,
		Options& Out,
		std::string& Failure)
	{
		if (Argc < 2)
		{
			Failure = "MissingCommand";
			return false;
		}
		const std::string Command = Argv[1];
		if (Command == "merge")
		{
			Out.Merge = true;
		}
		else if (Command == "screen-aim")
		{
			Out.ScreenAim = true;
		}
		else if (Command != "preflight")
		{
			Failure = "UnknownCommand";
			return false;
		}
		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Key = Argv[Index];
			if (Key == "--resume")
			{
				Out.Resume = true;
				continue;
			}
			if (Index + 1 >= Argc)
			{
				Failure = "MissingValue:" + Key;
				return false;
			}
			const std::string Value = Argv[++Index];
			std::uint64_t Unsigned = 0;
			double Number = 0.0;
			if (Key == "--output")
			{
				Out.Output = fs::path(Value);
			}
			else if (Key == "--input-root")
			{
				Out.InputRoot = fs::path(Value);
			}
			else if (Key == "--rank" && ParseUnsigned(Value, Unsigned))
			{
				Out.Rank = static_cast<std::int32_t>(Unsigned);
			}
			else if (Key == "--threads" && ParseUnsigned(Value, Unsigned))
			{
				Out.Threads = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--shard-index" && ParseUnsigned(Value, Unsigned))
			{
				Out.ShardIndex = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--shard-count" && ParseUnsigned(Value, Unsigned))
			{
				Out.ShardCount = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--checkpoint-every"
				&& ParseUnsigned(Value, Unsigned))
			{
				Out.CheckpointEvery = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--screen-aim-samples"
				&& ParseUnsigned(Value, Unsigned))
			{
				Out.ScreenAimSampleCount = static_cast<std::uint32_t>(Unsigned);
			}
			else if (Key == "--yaw-step" && ParseDouble(Value, Number))
			{
				Out.YawStep = Number;
			}
			else if (Key == "--pitch-step" && ParseDouble(Value, Number))
			{
				Out.PitchStep = Number;
			}
			else if (Key == "--power-step" && ParseDouble(Value, Number))
			{
				Out.PowerStep = Number;
			}
			else if (Key == "--min-yaw" && ParseDouble(Value, Number))
			{
				Out.MinYaw = Number;
			}
			else if (Key == "--max-yaw" && ParseDouble(Value, Number))
			{
				Out.MaxYaw = Number;
			}
			else if (Key == "--min-pitch" && ParseDouble(Value, Number))
			{
				Out.MinPitch = Number;
			}
			else if (Key == "--max-pitch" && ParseDouble(Value, Number))
			{
				Out.MaxPitch = Number;
			}
			else if (Key == "--min-power" && ParseDouble(Value, Number))
			{
				Out.MinPower = Number;
			}
			else if (Key == "--max-power" && ParseDouble(Value, Number))
			{
				Out.MaxPower = Number;
			}
			else if (Key == "--target-offset-x" && ParseDouble(Value, Number))
			{
				Out.TargetOffsetXCM = Number;
			}
			else if (Key == "--target-offset-y" && ParseDouble(Value, Number))
			{
				Out.TargetOffsetYCM = Number;
			}
			else if (Key == "--target-offset-z" && ParseDouble(Value, Number))
			{
				Out.TargetOffsetZCM = Number;
			}
			else if (Key == "--constellation-distance"
				&& ParseDouble(Value, Number))
			{
				Out.ConstellationDistanceCM = Number;
			}
			else if (Key == "--assist1-radial-delta"
				&& ParseDouble(Value, Number))
			{
				Out.CelestialRadialDeltaCM[0] = Number;
			}
			else if (Key == "--assist2-radial-delta"
				&& ParseDouble(Value, Number))
			{
				Out.CelestialRadialDeltaCM[1] = Number;
			}
			else if (Key == "--assist3-radial-delta"
				&& ParseDouble(Value, Number))
			{
				Out.CelestialRadialDeltaCM[2] = Number;
			}
			else if (Key == "--target-radial-delta"
				&& ParseDouble(Value, Number))
			{
				Out.CelestialRadialDeltaCM[3] = Number;
			}
			else if (Key == "--target-hit-radius" && ParseDouble(Value, Number))
			{
				Out.TargetHitRadiusCM = Number;
			}
			else if (Key == "--target-min-corridor-quality"
				&& ParseDouble(Value, Number))
			{
				Out.TargetMinimumCorridorQuality = Number;
			}
			else if (Key == "--assist3-offset-x" && ParseDouble(Value, Number))
			{
				Out.Assist3OffsetXCM = Number;
			}
			else if (Key == "--assist3-offset-y" && ParseDouble(Value, Number))
			{
				Out.Assist3OffsetYCM = Number;
			}
			else if (Key == "--assist3-offset-z" && ParseDouble(Value, Number))
			{
				Out.Assist3OffsetZCM = Number;
			}
			else if (Key == "--assist3-bplane-t-delta"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3BPlaneTargetTDeltaCM = Number;
			}
			else if (Key == "--assist3-bplane-r-delta"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3BPlaneTargetRDeltaCM = Number;
			}
			else if (Key == "--assist3-bplane-sigma-scale"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3BPlaneSigmaScale = Number;
			}
			else if (Key == "--assist3-velocity-delta-x"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3VelocityDeltaXCMPerSec = Number;
			}
			else if (Key == "--assist3-velocity-delta-y"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3VelocityDeltaYCMPerSec = Number;
			}
			else if (Key == "--assist3-velocity-delta-z"
				&& ParseDouble(Value, Number))
			{
				Out.Assist3VelocityDeltaZCMPerSec = Number;
			}
			else if (Key == "--assist2-offset-x" && ParseDouble(Value, Number))
			{
				Out.Assist2OffsetXCM = Number;
			}
			else if (Key == "--assist2-offset-y" && ParseDouble(Value, Number))
			{
				Out.Assist2OffsetYCM = Number;
			}
			else if (Key == "--assist2-offset-z" && ParseDouble(Value, Number))
			{
				Out.Assist2OffsetZCM = Number;
			}
			else if (Key == "--assist2-bplane-t-delta"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2BPlaneTargetTDeltaCM = Number;
			}
			else if (Key == "--assist2-bplane-r-delta"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2BPlaneTargetRDeltaCM = Number;
			}
			else if (Key == "--assist2-bplane-sigma-scale"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2BPlaneSigmaScale = Number;
			}
			else if (Key == "--assist2-velocity-delta-x"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2VelocityDeltaXCMPerSec = Number;
			}
			else if (Key == "--assist2-velocity-delta-y"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2VelocityDeltaYCMPerSec = Number;
			}
			else if (Key == "--assist2-velocity-delta-z"
				&& ParseDouble(Value, Number))
			{
				Out.Assist2VelocityDeltaZCMPerSec = Number;
			}
			else if (Key == "--arrival-cone-degrees" && ParseDouble(Value, Number))
			{
				Out.ArrivalConeDegrees = Number;
			}
			else if (Key == "--arrival-face-cone-degrees"
				&& ParseDouble(Value, Number))
			{
				Out.ArrivalFaceConeDegrees = Number;
			}
			else
			{
				Failure = "InvalidOption:" + Key;
				return false;
			}
		}
		if (Out.Output.empty() || !Out.Output.is_absolute()
			|| Out.Threads == 0 || Out.ShardCount == 0
			|| Out.ShardIndex >= Out.ShardCount
			|| Out.CheckpointEvery == 0 || Out.ScreenAimSampleCount == 0)
		{
			Failure = "InvalidExecutionContract";
			return false;
		}
		if (Out.Merge && (Out.InputRoot.empty()
			|| !Out.InputRoot.is_absolute()))
		{
			Failure = "MergeRequiresAbsoluteInputRoot";
			return false;
		}
		const double TargetOffsetSquared =
			Out.TargetOffsetXCM * Out.TargetOffsetXCM
			+ Out.TargetOffsetYCM * Out.TargetOffsetYCM
			+ Out.TargetOffsetZCM * Out.TargetOffsetZCM;
		if (!std::isfinite(TargetOffsetSquared)
			|| TargetOffsetSquared > 30000.0 * 30000.0)
		{
			Failure = "TargetOffsetOutsideDiagnosticLimit";
			return false;
		}
		if (!std::isfinite(Out.ConstellationDistanceCM)
			|| Out.ConstellationDistanceCM < 0.0
			|| Out.ConstellationDistanceCM > 120000.0)
		{
			Failure = "ConstellationDistanceOutsideDiagnosticLimit";
			return false;
		}
		for (const double RadialDeltaCM : Out.CelestialRadialDeltaCM)
		{
			if (!std::isfinite(RadialDeltaCM)
				|| RadialDeltaCM < -120000.0
				|| RadialDeltaCM > 120000.0)
			{
				Failure = "CelestialRadialDeltaOutsideDiagnosticLimit";
				return false;
			}
		}
		const double Assist3OffsetSquared =
			Out.Assist3OffsetXCM * Out.Assist3OffsetXCM
			+ Out.Assist3OffsetYCM * Out.Assist3OffsetYCM
			+ Out.Assist3OffsetZCM * Out.Assist3OffsetZCM;
		if (!std::isfinite(Assist3OffsetSquared)
			|| Assist3OffsetSquared > 10000.0 * 10000.0)
		{
			Failure = "Assist3OffsetOutsideDiagnosticLimit";
			return false;
		}
		const double BPlaneDeltaSquared =
			Out.Assist3BPlaneTargetTDeltaCM
				* Out.Assist3BPlaneTargetTDeltaCM
			+ Out.Assist3BPlaneTargetRDeltaCM
				* Out.Assist3BPlaneTargetRDeltaCM;
		if (!std::isfinite(BPlaneDeltaSquared)
			|| BPlaneDeltaSquared > 5000.0 * 5000.0
			|| Out.Assist3BPlaneSigmaScale < 0.65
			|| Out.Assist3BPlaneSigmaScale > 1.50)
		{
			Failure = "Assist3BPlaneOverrideOutsideDiagnosticLimit";
			return false;
		}
		const double VelocityDeltaSquared =
			Out.Assist3VelocityDeltaXCMPerSec
				* Out.Assist3VelocityDeltaXCMPerSec
			+ Out.Assist3VelocityDeltaYCMPerSec
				* Out.Assist3VelocityDeltaYCMPerSec
			+ Out.Assist3VelocityDeltaZCMPerSec
				* Out.Assist3VelocityDeltaZCMPerSec;
		if (!std::isfinite(VelocityDeltaSquared)
			|| VelocityDeltaSquared > 2500.0 * 2500.0)
		{
			Failure = "Assist3VelocityOverrideOutsideDiagnosticLimit";
			return false;
		}
		const double Assist2OffsetSquared =
			Out.Assist2OffsetXCM * Out.Assist2OffsetXCM
			+ Out.Assist2OffsetYCM * Out.Assist2OffsetYCM
			+ Out.Assist2OffsetZCM * Out.Assist2OffsetZCM;
		const double Assist2BPlaneDeltaSquared =
			Out.Assist2BPlaneTargetTDeltaCM
				* Out.Assist2BPlaneTargetTDeltaCM
			+ Out.Assist2BPlaneTargetRDeltaCM
				* Out.Assist2BPlaneTargetRDeltaCM;
		const double Assist2VelocityDeltaSquared =
			Out.Assist2VelocityDeltaXCMPerSec
				* Out.Assist2VelocityDeltaXCMPerSec
			+ Out.Assist2VelocityDeltaYCMPerSec
				* Out.Assist2VelocityDeltaYCMPerSec
			+ Out.Assist2VelocityDeltaZCMPerSec
				* Out.Assist2VelocityDeltaZCMPerSec;
		if (!std::isfinite(Assist2OffsetSquared)
			|| Assist2OffsetSquared > 10000.0 * 10000.0
			|| !std::isfinite(Assist2BPlaneDeltaSquared)
			|| Assist2BPlaneDeltaSquared > 5000.0 * 5000.0
			|| Out.Assist2BPlaneSigmaScale < 0.65
			|| Out.Assist2BPlaneSigmaScale > 1.50
			|| !std::isfinite(Assist2VelocityDeltaSquared)
			|| Assist2VelocityDeltaSquared > 2500.0 * 2500.0)
		{
			Failure = "Assist2OverrideOutsideDiagnosticLimit";
			return false;
		}
		if (std::isfinite(Out.TargetHitRadiusCM)
			&& (Out.TargetHitRadiusCM < 4500.0
				|| Out.TargetHitRadiusCM > 12000.0))
		{
			Failure = "TargetHitRadiusOutsideSearchContract";
			return false;
		}
		if (std::isfinite(Out.TargetMinimumCorridorQuality)
			&& (Out.TargetMinimumCorridorQuality < 0.05
				|| Out.TargetMinimumCorridorQuality > 1.0))
		{
			Failure = "TargetCorridorQualityOutsideDiagnosticLimit";
			return false;
		}
		if (!(Out.ArrivalConeDegrees > 0.0)
			|| Out.ArrivalConeDegrees > 180.0)
		{
			Failure = "ArrivalConeOutsideDiagnosticLimit";
			return false;
		}
		if (!(Out.ArrivalFaceConeDegrees > 0.0)
			|| Out.ArrivalFaceConeDegrees > 180.0)
		{
			Failure = "ArrivalFaceConeOutsideDiagnosticLimit";
			return false;
		}
		return true;
	}

	void ApplyDiagnosticOffsets(
		const Options& OptionsValue,
		CandidateLayout& Layout)
	{
		const ABTS::M11Core::Vec3d ConstellationDirection =
			(Layout.Scenario.Bodies[1].CenterCM
				- Layout.Launch.PouchLocalPositionCM).GetSafeNormal();
		const ABTS::M11Core::Vec3d ConstellationOffset =
			ConstellationDirection * OptionsValue.ConstellationDistanceCM;
		for (std::size_t BodyIndex = 1;
			BodyIndex < Layout.Scenario.Bodies.size(); ++BodyIndex)
		{
			Layout.Scenario.Bodies[BodyIndex].CenterCM += ConstellationOffset;
		}
		Layout.Scenario.Target.CenterCM += ConstellationOffset;
		Layout.Scenario.Target.GeometricContactCenterCM += ConstellationOffset;
		for (std::size_t AssistIndex = 0; AssistIndex < 3; ++AssistIndex)
		{
			ABTS::M11Core::Vec3d& Center =
				Layout.Scenario.Bodies[AssistIndex + 1].CenterCM;
			const ABTS::M11Core::Vec3d Direction =
				(Center - Layout.Launch.PouchLocalPositionCM).GetSafeNormal();
			Center += Direction
				* OptionsValue.CelestialRadialDeltaCM[AssistIndex];
		}
		const ABTS::M11Core::Vec3d TargetDirection =
			(Layout.Scenario.Target.CenterCM
				- Layout.Launch.PouchLocalPositionCM).GetSafeNormal();
		const ABTS::M11Core::Vec3d TargetRadialOffset = TargetDirection
			* OptionsValue.CelestialRadialDeltaCM[3];
		Layout.Scenario.Target.CenterCM += TargetRadialOffset;
		Layout.Scenario.Target.GeometricContactCenterCM += TargetRadialOffset;
		const ABTS::M11Core::Vec3d Offset{
			OptionsValue.TargetOffsetXCM,
			OptionsValue.TargetOffsetYCM,
			OptionsValue.TargetOffsetZCM};
		Layout.Scenario.Target.CenterCM += Offset;
		Layout.Scenario.Target.GeometricContactCenterCM += Offset;
		Layout.Scenario.Bodies[3].CenterCM += ABTS::M11Core::Vec3d{
			OptionsValue.Assist3OffsetXCM,
			OptionsValue.Assist3OffsetYCM,
			OptionsValue.Assist3OffsetZCM};
		ABTS::M11Core::GravityBodySpec& Assist3 =
			Layout.Scenario.Bodies[3];
		Assist3.BPlaneTargetTCM +=
			OptionsValue.Assist3BPlaneTargetTDeltaCM;
		Assist3.BPlaneTargetRCM +=
			OptionsValue.Assist3BPlaneTargetRDeltaCM;
		Assist3.BPlaneSigmaTCM *= OptionsValue.Assist3BPlaneSigmaScale;
		Assist3.BPlaneSigmaRCM *= OptionsValue.Assist3BPlaneSigmaScale;
		Assist3.VirtualOrbitalVelocityCMPerSec += ABTS::M11Core::Vec3d{
			OptionsValue.Assist3VelocityDeltaXCMPerSec,
			OptionsValue.Assist3VelocityDeltaYCMPerSec,
			OptionsValue.Assist3VelocityDeltaZCMPerSec};
		ABTS::M11Core::GravityBodySpec& Assist2 =
			Layout.Scenario.Bodies[2];
		Assist2.CenterCM += ABTS::M11Core::Vec3d{
			OptionsValue.Assist2OffsetXCM,
			OptionsValue.Assist2OffsetYCM,
			OptionsValue.Assist2OffsetZCM};
		Assist2.BPlaneTargetTCM +=
			OptionsValue.Assist2BPlaneTargetTDeltaCM;
		Assist2.BPlaneTargetRCM +=
			OptionsValue.Assist2BPlaneTargetRDeltaCM;
		Assist2.BPlaneSigmaTCM *= OptionsValue.Assist2BPlaneSigmaScale;
		Assist2.BPlaneSigmaRCM *= OptionsValue.Assist2BPlaneSigmaScale;
		Assist2.VirtualOrbitalVelocityCMPerSec += ABTS::M11Core::Vec3d{
			OptionsValue.Assist2VelocityDeltaXCMPerSec,
			OptionsValue.Assist2VelocityDeltaYCMPerSec,
			OptionsValue.Assist2VelocityDeltaZCMPerSec};
		if (std::isfinite(OptionsValue.TargetHitRadiusCM))
		{
			Layout.Scenario.Target.HitRadiusCM =
				OptionsValue.TargetHitRadiusCM;
		}
		if (std::isfinite(OptionsValue.TargetMinimumCorridorQuality))
		{
			Layout.Scenario.Target.MinimumQualifyingCorridorQuality =
				OptionsValue.TargetMinimumCorridorQuality;
		}
	}

	bool MakeGrid(
		const CandidateLayout& Layout,
		const Options& OptionsValue,
		Grid& Out,
		std::string& Failure)
	{
		Out.MinYaw = std::isfinite(OptionsValue.MinYaw)
			? OptionsValue.MinYaw : Layout.Launch.MinimumYawDegrees;
		Out.MaxYaw = std::isfinite(OptionsValue.MaxYaw)
			? OptionsValue.MaxYaw : Layout.Launch.MaximumYawDegrees;
		Out.MinPitch = std::isfinite(OptionsValue.MinPitch)
			? OptionsValue.MinPitch : Layout.Launch.MinimumPitchDegrees;
		Out.MaxPitch = std::isfinite(OptionsValue.MaxPitch)
			? OptionsValue.MaxPitch : Layout.Launch.MaximumPitchDegrees;
		Out.MinPower = std::isfinite(OptionsValue.MinPower)
			? OptionsValue.MinPower : Layout.Launch.MinimumPower;
		Out.MaxPower = std::isfinite(OptionsValue.MaxPower)
			? OptionsValue.MaxPower : Layout.Launch.MaximumPower;
		if (Out.MinYaw < Layout.Launch.MinimumYawDegrees
			|| Out.MaxYaw > Layout.Launch.MaximumYawDegrees
			|| Out.MinPitch < Layout.Launch.MinimumPitchDegrees
			|| Out.MaxPitch > Layout.Launch.MaximumPitchDegrees
			|| Out.MinPower < Layout.Launch.MinimumPower
			|| Out.MaxPower > Layout.Launch.MaximumPower
			|| Out.MinYaw > Out.MaxYaw
			|| Out.MinPitch > Out.MaxPitch
			|| Out.MinPower > Out.MaxPower)
		{
			Failure = "GridBoundsOutsideLaunchDomain";
			return false;
		}
		Out.YawStep = OptionsValue.YawStep;
		Out.PitchStep = OptionsValue.PitchStep;
		Out.PowerStep = OptionsValue.PowerStep;
		const auto Count = [&Failure](
			const double Minimum,
			const double Maximum,
			const double Step,
			std::int32_t& OutCount)
		{
			if (!(Step > 0.0))
			{
				Failure = "NonPositiveGridStep";
				return false;
			}
			const double Cells = (Maximum - Minimum) / Step;
			const double Rounded = std::round(Cells);
			if (std::abs(Cells - Rounded) > 1.0e-9
				|| Rounded > std::numeric_limits<std::int32_t>::max() - 1)
			{
				Failure = "GridStepDoesNotDivideClosedDomain";
				return false;
			}
			OutCount = static_cast<std::int32_t>(Rounded) + 1;
			return true;
		};
		return Count(Out.MinYaw, Out.MaxYaw, Out.YawStep, Out.YawCount)
			&& Count(
				Out.MinPitch,
				Out.MaxPitch,
				Out.PitchStep,
				Out.PitchCount)
			&& Count(
				Out.MinPower,
				Out.MaxPower,
				Out.PowerStep,
				Out.PowerCount);
	}

	void DecodeIndex(
		const Grid& GridValue,
		const std::uint64_t GlobalIndex,
		Sample& Out)
	{
		const std::uint64_t PitchPower =
			static_cast<std::uint64_t>(GridValue.PitchCount)
			* static_cast<std::uint64_t>(GridValue.PowerCount);
		Out.GlobalIndex = GlobalIndex;
		Out.YawIndex = static_cast<std::int32_t>(
			GlobalIndex / PitchPower);
		const std::uint64_t Remainder = GlobalIndex % PitchPower;
		Out.PitchIndex = static_cast<std::int32_t>(
			Remainder / static_cast<std::uint64_t>(GridValue.PowerCount));
		Out.PowerIndex = static_cast<std::int32_t>(
			Remainder % static_cast<std::uint64_t>(GridValue.PowerCount));
	}

	LaunchInput InputFor(const Grid& GridValue, const Sample& SampleValue)
	{
		return LaunchInput{
			GridValue.MinYaw + SampleValue.YawIndex * GridValue.YawStep,
			GridValue.MinPitch
				+ SampleValue.PitchIndex * GridValue.PitchStep,
			GridValue.MinPower
				+ SampleValue.PowerIndex * GridValue.PowerStep};
	}

	std::string Hex64(const std::uint64_t Value)
	{
		std::ostringstream Stream;
		Stream << "0x" << std::hex << std::setw(16)
			<< std::setfill('0') << Value;
		return Stream.str();
	}

	bool WriteSummary(
		const fs::path& Path,
		const Options& OptionsValue,
		const FrozenCandidateIdentity& Identity,
		const std::uint64_t VariantSourceHash,
		const double TargetHitRadiusCM,
		const double TargetMinimumCorridorQuality,
		const Grid& GridValue,
		const std::vector<Sample>& Samples,
		const bool Complete,
		const std::string& Failure)
	{
		std::array<std::uint64_t, 4> Counts{};
		for (const Sample& SampleValue : Samples)
		{
			for (std::size_t Level = 0; Level < Counts.size(); ++Level)
			{
				if ((SampleValue.PrefixMask & (1u << Level)) != 0)
				{
					++Counts[Level];
				}
			}
		}
		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			return false;
		}
		Stream << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v2_2.preflight.v1\",\n"
			<< "  \"complete\":" << (Complete ? "true" : "false") << ",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"candidateSourceHash\":\""
			<< Hex64(Identity.CandidateSourceHash) << "\",\n"
			<< "  \"variantSourceHash\":\""
			<< Hex64(VariantSourceHash) << "\",\n"
			<< "  \"constellationDistanceCM\":"
			<< OptionsValue.ConstellationDistanceCM << ",\n"
			<< "  \"celestialRadialDeltaCM\":["
			<< OptionsValue.CelestialRadialDeltaCM[0] << ','
			<< OptionsValue.CelestialRadialDeltaCM[1] << ','
			<< OptionsValue.CelestialRadialDeltaCM[2] << ','
			<< OptionsValue.CelestialRadialDeltaCM[3] << "],\n"
			<< "  \"targetOffsetCM\":[" << OptionsValue.TargetOffsetXCM
			<< ',' << OptionsValue.TargetOffsetYCM << ','
			<< OptionsValue.TargetOffsetZCM << "],\n"
			<< "  \"assist2OffsetCM\":[" << OptionsValue.Assist2OffsetXCM
			<< ',' << OptionsValue.Assist2OffsetYCM << ','
			<< OptionsValue.Assist2OffsetZCM << "],\n"
			<< "  \"assist2BPlaneDeltaCM\":["
			<< OptionsValue.Assist2BPlaneTargetTDeltaCM << ','
			<< OptionsValue.Assist2BPlaneTargetRDeltaCM << "],\n"
			<< "  \"assist2BPlaneSigmaScale\":"
			<< OptionsValue.Assist2BPlaneSigmaScale << ",\n"
			<< "  \"assist2VelocityDeltaCMPerSec\":["
			<< OptionsValue.Assist2VelocityDeltaXCMPerSec << ','
			<< OptionsValue.Assist2VelocityDeltaYCMPerSec << ','
			<< OptionsValue.Assist2VelocityDeltaZCMPerSec << "],\n"
			<< "  \"assist3OffsetCM\":[" << OptionsValue.Assist3OffsetXCM
			<< ',' << OptionsValue.Assist3OffsetYCM << ','
			<< OptionsValue.Assist3OffsetZCM << "],\n"
			<< "  \"assist3BPlaneDeltaCM\":["
			<< OptionsValue.Assist3BPlaneTargetTDeltaCM << ','
			<< OptionsValue.Assist3BPlaneTargetRDeltaCM << "],\n"
			<< "  \"assist3BPlaneSigmaScale\":"
			<< OptionsValue.Assist3BPlaneSigmaScale << ",\n"
			<< "  \"assist3VelocityDeltaCMPerSec\":["
			<< OptionsValue.Assist3VelocityDeltaXCMPerSec << ','
			<< OptionsValue.Assist3VelocityDeltaYCMPerSec << ','
			<< OptionsValue.Assist3VelocityDeltaZCMPerSec << "],\n"
			<< "  \"targetHitRadiusCM\":"
			<< TargetHitRadiusCM << ",\n"
			<< "  \"targetMinimumCorridorQuality\":"
			<< TargetMinimumCorridorQuality
			<< ",\n"
			<< "  \"arrivalConeDegrees\":"
			<< OptionsValue.ArrivalConeDegrees << ",\n"
			<< "  \"arrivalFaceConeDegrees\":"
			<< OptionsValue.ArrivalFaceConeDegrees << ",\n"
			<< "  \"shardIndex\":" << OptionsValue.ShardIndex << ",\n"
			<< "  \"shardCount\":" << OptionsValue.ShardCount << ",\n"
			<< "  \"grid\":{\"yawCount\":" << GridValue.YawCount
			<< ",\"pitchCount\":" << GridValue.PitchCount
			<< ",\"powerCount\":" << GridValue.PowerCount
			<< ",\"minYaw\":" << GridValue.MinYaw
			<< ",\"maxYaw\":" << GridValue.MaxYaw
			<< ",\"minPitch\":" << GridValue.MinPitch
			<< ",\"maxPitch\":" << GridValue.MaxPitch
			<< ",\"minPower\":" << GridValue.MinPower
			<< ",\"maxPower\":" << GridValue.MaxPower
			<< ",\"yawStep\":" << GridValue.YawStep
			<< ",\"pitchStep\":" << GridValue.PitchStep
			<< ",\"powerStep\":" << GridValue.PowerStep << "},\n"
			<< "  \"globalSampleCount\":" << GridValue.Count() << ",\n"
			<< "  \"completedShardSamples\":" << Samples.size() << ",\n"
			<< "  \"prefixCounts\":[" << Counts[0] << ',' << Counts[1]
			<< ',' << Counts[2] << ',' << Counts[3] << "],\n"
			<< "  \"failure\":\"" << Failure << "\"\n}\n";
		return Stream.good();
	}

	bool ReadSamples(
		const fs::path& Path,
		std::vector<Sample>& Out,
		std::string& Failure)
	{
		Out.clear();
		std::ifstream Stream(Path);
		if (!Stream)
		{
			return true;
		}
		std::string Line;
		std::getline(Stream, Line);
		while (std::getline(Stream, Line))
		{
			std::istringstream Row(Line);
			Sample Value;
			unsigned int Prefix = 0;
			unsigned int Termination = 0;
			std::string Hash;
			if (!(Row >> Value.GlobalIndex >> Value.YawIndex
				>> Value.PitchIndex >> Value.PowerIndex >> Prefix
				>> Termination >> Value.CompletedAssistCount
				>> Value.TargetContactCount >> Hash))
			{
				Failure = "MalformedSampleRow";
				return false;
			}
			Value.PrefixMask = static_cast<std::uint8_t>(Prefix);
			Value.Termination = static_cast<std::uint8_t>(Termination);
			const auto HashResult = std::from_chars(
				Hash.data() + 2,
				Hash.data() + Hash.size(),
				Value.ResultHash,
				16);
			if (Hash.size() != 18 || HashResult.ec != std::errc())
			{
				Failure = "MalformedSampleHash";
				return false;
			}
			Out.push_back(Value);
		}
		return true;
	}

	std::uint64_t AggregateSampleHash(const std::vector<Sample>& Samples)
	{
		std::uint64_t Hash = 14695981039346656037ull;
		const auto Add = [&Hash](const std::uint64_t Value)
		{
			for (std::uint32_t Byte = 0; Byte < 8; ++Byte)
			{
				Hash ^= (Value >> (Byte * 8)) & 0xffu;
				Hash *= 1099511628211ull;
			}
		};
		for (const Sample& Value : Samples)
		{
			Add(Value.GlobalIndex);
			Add(Value.PrefixMask);
			Add(Value.Termination);
			Add(Value.ResultHash);
		}
		return Hash;
	}

	struct ComponentSummary
	{
		std::array<std::uint64_t, 4> Count{};
		std::array<std::uint64_t, 4> LargestSize{};
	};

	ComponentSummary CountComponents(
		const Grid& GridValue,
		const std::vector<Sample>& Samples)
	{
		ComponentSummary Summary;
		for (std::size_t Level = 0; Level < Summary.Count.size(); ++Level)
		{
			std::vector<std::uint8_t> Visited(Samples.size(), 0);
			for (std::size_t Start = 0; Start < Samples.size(); ++Start)
			{
				if (Visited[Start] != 0
					|| (Samples[Start].PrefixMask & (1u << Level)) == 0)
				{
					continue;
				}
				++Summary.Count[Level];
				std::uint64_t ComponentSize = 0;
				std::vector<std::uint64_t> Open{Samples[Start].GlobalIndex};
				Visited[Start] = 1;
				while (!Open.empty())
				{
					const std::uint64_t Index = Open.back();
					Open.pop_back();
					++ComponentSize;
					Sample Decoded;
					DecodeIndex(GridValue, Index, Decoded);
					const std::array<std::int32_t, 6> DY{-1, 1, 0, 0, 0, 0};
					const std::array<std::int32_t, 6> DP{0, 0, -1, 1, 0, 0};
					const std::array<std::int32_t, 6> DW{0, 0, 0, 0, -1, 1};
					for (std::size_t Axis = 0; Axis < 6; ++Axis)
					{
						const std::int32_t Y = Decoded.YawIndex + DY[Axis];
						const std::int32_t P = Decoded.PitchIndex + DP[Axis];
						const std::int32_t W = Decoded.PowerIndex + DW[Axis];
						if (Y < 0 || Y >= GridValue.YawCount
							|| P < 0 || P >= GridValue.PitchCount
							|| W < 0 || W >= GridValue.PowerCount)
						{
							continue;
						}
						const std::uint64_t Neighbor =
							(static_cast<std::uint64_t>(Y)
								* GridValue.PitchCount
								+ static_cast<std::uint64_t>(P))
								* GridValue.PowerCount
							+ static_cast<std::uint64_t>(W);
						if (Visited[Neighbor] == 0
							&& (Samples[Neighbor].PrefixMask
								& (1u << Level)) != 0)
						{
							Visited[Neighbor] = 1;
							Open.push_back(Neighbor);
						}
					}
				}
				Summary.LargestSize[Level] = std::max(
					Summary.LargestSize[Level], ComponentSize);
			}
		}
		return Summary;
	}

	int RunMerge(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				OptionsValue.Rank, Layout, &Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		const CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		if (ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract)
			!= Identity.CandidateSourceHash)
		{
			std::cerr << "CandidateSourceIdentityMismatch\n";
			return 1;
		}
		ApplyDiagnosticOffsets(OptionsValue, Layout);
		const std::uint64_t VariantSourceHash =
			ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract);
		Grid GridValue;
		std::string Failure;
		if (!MakeGrid(Layout, OptionsValue, GridValue, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::vector<Sample> Samples;
		for (std::uint32_t Shard = 0; Shard < OptionsValue.ShardCount; ++Shard)
		{
			std::ostringstream Name;
			Name << "shard_" << std::setw(4) << std::setfill('0') << Shard;
			std::vector<Sample> Part;
			if (!ReadSamples(
					OptionsValue.InputRoot / Name.str() / "samples.tsv",
					Part,
					Failure)
				|| Part.empty())
			{
				std::cerr << (Failure.empty() ? "MissingShardSamples" : Failure)
					<< ':' << Shard << '\n';
				return 1;
			}
			for (const Sample& Value : Part)
			{
				if (Value.GlobalIndex % OptionsValue.ShardCount != Shard)
				{
					std::cerr << "ShardOwnershipMismatch:" << Shard << '\n';
					return 1;
				}
			}
			Samples.insert(Samples.end(), Part.begin(), Part.end());
		}
		std::sort(Samples.begin(), Samples.end(), [](const Sample& A, const Sample& B)
		{
			return A.GlobalIndex < B.GlobalIndex;
		});
		if (Samples.size() != GridValue.Count())
		{
			std::cerr << "IncompleteCoverage\n";
			return 1;
		}
		std::array<std::uint64_t, 4> PrefixCounts{};
		std::uint64_t NestingViolations = 0;
		std::array<std::int32_t, 4> MinimumPowerIndex{
			GridValue.PowerCount, GridValue.PowerCount,
			GridValue.PowerCount, GridValue.PowerCount};
		std::array<std::int32_t, 4> MaximumPowerIndex{-1, -1, -1, -1};
		for (std::uint64_t Index = 0; Index < Samples.size(); ++Index)
		{
			const Sample& Value = Samples[Index];
			if (Value.GlobalIndex != Index)
			{
				std::cerr << "DuplicateOrMissingCanonicalIndex:" << Index << '\n';
				return 1;
			}
			if ((Value.PrefixMask & 8u) != 0 && (Value.PrefixMask & 7u) != 7u
				|| (Value.PrefixMask & 4u) != 0 && (Value.PrefixMask & 3u) != 3u
				|| (Value.PrefixMask & 2u) != 0 && (Value.PrefixMask & 1u) == 0)
			{
				++NestingViolations;
			}
			for (std::size_t Level = 0; Level < 4; ++Level)
			{
				if ((Value.PrefixMask & (1u << Level)) != 0)
				{
					++PrefixCounts[Level];
					MinimumPowerIndex[Level] = std::min(
						MinimumPowerIndex[Level], Value.PowerIndex);
					MaximumPowerIndex[Level] = std::max(
						MaximumPowerIndex[Level], Value.PowerIndex);
				}
			}
		}
		const ComponentSummary Components =
			CountComponents(GridValue, Samples);
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error)
		{
			std::cerr << "OutputCreateFailed\n";
			return 1;
		}
		const fs::path SamplePath = OptionsValue.Output / "samples.tsv";
		std::ofstream SampleStream(SamplePath, std::ios::trunc);
		SampleStream << "global yaw pitch power prefix termination assists contacts hash\n";
		for (const Sample& Value : Samples)
		{
			SampleStream << Value.GlobalIndex << ' ' << Value.YawIndex << ' '
				<< Value.PitchIndex << ' ' << Value.PowerIndex << ' '
				<< static_cast<unsigned int>(Value.PrefixMask) << ' '
				<< static_cast<unsigned int>(Value.Termination) << ' '
				<< Value.CompletedAssistCount << ' ' << Value.TargetContactCount
				<< ' ' << Hex64(Value.ResultHash) << '\n';
		}
		bool NominalF4 = false;
		const auto ExactIndex = [](const double Value, const double Minimum,
			const double Step, const std::int32_t Count, std::int32_t& OutIndex)
		{
			const double Coordinate = (Value - Minimum) / Step;
			const double Rounded = std::round(Coordinate);
			if (std::abs(Coordinate - Rounded) > 1.0e-9
				|| Rounded < 0.0 || Rounded >= Count)
			{
				return false;
			}
			OutIndex = static_cast<std::int32_t>(Rounded);
			return true;
		};
		std::int32_t NominalYaw = 0;
		std::int32_t NominalPitch = 0;
		std::int32_t NominalPower = 0;
		if (ExactIndex(Layout.NominalInput.YawDegrees, GridValue.MinYaw,
				GridValue.YawStep, GridValue.YawCount, NominalYaw)
			&& ExactIndex(Layout.NominalInput.PitchDegrees, GridValue.MinPitch,
				GridValue.PitchStep, GridValue.PitchCount, NominalPitch)
			&& ExactIndex(Layout.NominalInput.Power, GridValue.MinPower,
				GridValue.PowerStep, GridValue.PowerCount, NominalPower))
		{
			const std::uint64_t NominalIndex =
				(static_cast<std::uint64_t>(NominalYaw)
					* GridValue.PitchCount
					+ static_cast<std::uint64_t>(NominalPitch))
					* GridValue.PowerCount
				+ static_cast<std::uint64_t>(NominalPower);
			NominalF4 = (Samples[NominalIndex].PrefixMask & 8u) != 0;
		}
		bool HasRepresentativeF4 = false;
		LaunchInput RepresentativeInput;
		double RepresentativeDistance =
			std::numeric_limits<double>::infinity();
		for (const Sample& Value : Samples)
		{
			if ((Value.PrefixMask & 8u) == 0)
			{
				continue;
			}
			const LaunchInput Input = InputFor(GridValue, Value);
			const double YawDelta = Input.YawDegrees
				- Layout.NominalInput.YawDegrees;
			const double PitchDelta = Input.PitchDegrees
				- Layout.NominalInput.PitchDegrees;
			const double PowerDelta = (Input.Power
				- Layout.NominalInput.Power) * 20.0;
			const double Distance = YawDelta * YawDelta
				+ PitchDelta * PitchDelta + PowerDelta * PowerDelta;
			if (Distance < RepresentativeDistance)
			{
				RepresentativeDistance = Distance;
				RepresentativeInput = Input;
				HasRepresentativeF4 = true;
			}
		}
		ABTS::M11Core::TrajectoryPacingDiagnostics RepresentativePacing;
		if (HasRepresentativeF4)
		{
			CandidateRecord Replay;
			Replay.Layout = Layout;
			Replay.Layout.NominalInput = RepresentativeInput;
			ABTS::M11Core::TrajectoryResult Result;
			std::string ReplayFailure;
			HasRepresentativeF4 = CandidateSearch::ReplayCandidate(
				Replay, 0x7u, Result, &ReplayFailure)
				&& Result.BuildPacingDiagnostics(
					RepresentativePacing, &ReplayFailure);
		}
		const bool Passed = PrefixCounts[3] > 0 && NominalF4
			&& Components.Count[3] == 1 && NestingViolations == 0;
		std::ofstream Summary(
			OptionsValue.Output / "summary.json",
			std::ios::binary | std::ios::trunc);
		Summary << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v2_2.preflight_merged.v1\",\n"
			<< "  \"passed\":" << (Passed ? "true" : "false") << ",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"candidateSourceHash\":\"" << Hex64(Identity.CandidateSourceHash)
			<< "\",\n  \"variantSourceHash\":\"" << Hex64(VariantSourceHash)
			<< "\",\n  \"constellationDistanceCM\":"
			<< OptionsValue.ConstellationDistanceCM
			<< ",\n  \"celestialRadialDeltaCM\":["
			<< OptionsValue.CelestialRadialDeltaCM[0] << ','
			<< OptionsValue.CelestialRadialDeltaCM[1] << ','
			<< OptionsValue.CelestialRadialDeltaCM[2] << ','
			<< OptionsValue.CelestialRadialDeltaCM[3] << ']'
			<< ",\n  \"targetOffsetCM\":[" << OptionsValue.TargetOffsetXCM
			<< ',' << OptionsValue.TargetOffsetYCM << ','
			<< OptionsValue.TargetOffsetZCM << "],\n"
			<< "  \"assist2OffsetCM\":["
			<< OptionsValue.Assist2OffsetXCM << ','
			<< OptionsValue.Assist2OffsetYCM << ','
			<< OptionsValue.Assist2OffsetZCM << "],\n"
			<< "  \"assist2BPlaneDeltaCM\":["
			<< OptionsValue.Assist2BPlaneTargetTDeltaCM << ','
			<< OptionsValue.Assist2BPlaneTargetRDeltaCM << "],\n"
			<< "  \"assist2BPlaneSigmaScale\":"
			<< OptionsValue.Assist2BPlaneSigmaScale << ",\n"
			<< "  \"assist2VelocityDeltaCMPerSec\":["
			<< OptionsValue.Assist2VelocityDeltaXCMPerSec << ','
			<< OptionsValue.Assist2VelocityDeltaYCMPerSec << ','
			<< OptionsValue.Assist2VelocityDeltaZCMPerSec << "],\n"
			<< "  \"assist3OffsetCM\":["
			<< OptionsValue.Assist3OffsetXCM << ','
			<< OptionsValue.Assist3OffsetYCM << ','
			<< OptionsValue.Assist3OffsetZCM << "],\n"
			<< "  \"assist3BPlaneDeltaCM\":["
			<< OptionsValue.Assist3BPlaneTargetTDeltaCM << ','
			<< OptionsValue.Assist3BPlaneTargetRDeltaCM << "],\n"
			<< "  \"assist3BPlaneSigmaScale\":"
			<< OptionsValue.Assist3BPlaneSigmaScale << ",\n"
			<< "  \"assist3VelocityDeltaCMPerSec\":["
			<< OptionsValue.Assist3VelocityDeltaXCMPerSec << ','
			<< OptionsValue.Assist3VelocityDeltaYCMPerSec << ','
			<< OptionsValue.Assist3VelocityDeltaZCMPerSec << "],\n"
			<< "  \"targetHitRadiusCM\":"
			<< Layout.Scenario.Target.HitRadiusCM << ",\n"
			<< "  \"targetMinimumCorridorQuality\":"
			<< Layout.Scenario.Target.MinimumQualifyingCorridorQuality
			<< ",\n"
			<< "  \"arrivalConeDegrees\":"
			<< OptionsValue.ArrivalConeDegrees << ",\n"
			<< "  \"arrivalFaceConeDegrees\":"
			<< OptionsValue.ArrivalFaceConeDegrees << ",\n"
			<< "  \"aggregateSampleHash\":\""
			<< Hex64(AggregateSampleHash(Samples)) << "\",\n"
			<< "  \"grid\":{\"yawCount\":" << GridValue.YawCount
			<< ",\"pitchCount\":" << GridValue.PitchCount
			<< ",\"powerCount\":" << GridValue.PowerCount
			<< ",\"minYaw\":" << GridValue.MinYaw
			<< ",\"maxYaw\":" << GridValue.MaxYaw
			<< ",\"minPitch\":" << GridValue.MinPitch
			<< ",\"maxPitch\":" << GridValue.MaxPitch
			<< ",\"minPower\":" << GridValue.MinPower
			<< ",\"maxPower\":" << GridValue.MaxPower
			<< ",\"yawStep\":" << GridValue.YawStep
			<< ",\"pitchStep\":" << GridValue.PitchStep
			<< ",\"powerStep\":" << GridValue.PowerStep << "},\n"
			<< "  \"sampleCount\":" << Samples.size() << ",\n"
			<< "  \"prefixCounts\":[" << PrefixCounts[0] << ','
			<< PrefixCounts[1] << ',' << PrefixCounts[2] << ','
			<< PrefixCounts[3] << "],\n  \"componentCounts\":["
			<< Components.Count[0] << ',' << Components.Count[1] << ','
			<< Components.Count[2] << ',' << Components.Count[3] << "],\n"
			<< "  \"largestComponentSizes\":["
			<< Components.LargestSize[0] << ','
			<< Components.LargestSize[1] << ','
			<< Components.LargestSize[2] << ','
			<< Components.LargestSize[3] << "],\n"
			<< "  \"nominalF4\":" << (NominalF4 ? "true" : "false") << ",\n"
			<< "  \"representativeF4Available\":"
			<< (HasRepresentativeF4 ? "true" : "false") << ",\n"
			<< "  \"representativeF4Input\":["
			<< RepresentativeInput.YawDegrees << ','
			<< RepresentativeInput.PitchDegrees << ','
			<< RepresentativeInput.Power << "],\n"
			<< "  \"representativeFlightTimeSeconds\":"
			<< RepresentativePacing.TotalFlightTimeSeconds << ",\n"
			<< "  \"representativeAssistDurationsSeconds\":["
			<< RepresentativePacing.Assists[0].InfluenceDurationSeconds << ','
			<< RepresentativePacing.Assists[1].InfluenceDurationSeconds << ','
			<< RepresentativePacing.Assists[2].InfluenceDurationSeconds << "],\n"
			<< "  \"representativeAssistDeflectionsRadians\":["
			<< RepresentativePacing.Assists[0].ActualDeflectionRadians << ','
			<< RepresentativePacing.Assists[1].ActualDeflectionRadians << ','
			<< RepresentativePacing.Assists[2].ActualDeflectionRadians << "],\n"
			<< "  \"minimumPowerIndices\":[" << MinimumPowerIndex[0] << ','
			<< MinimumPowerIndex[1] << ',' << MinimumPowerIndex[2] << ','
			<< MinimumPowerIndex[3] << "],\n  \"maximumPowerIndices\":["
			<< MaximumPowerIndex[0] << ',' << MaximumPowerIndex[1] << ','
			<< MaximumPowerIndex[2] << ',' << MaximumPowerIndex[3] << "],\n"
			<< "  \"nestingViolations\":" << NestingViolations << "\n}\n";
		std::cout << "[ABTS][M11-B-v2.2][Merge] Passed=" << Passed
			<< " Prefix=" << PrefixCounts[0] << ',' << PrefixCounts[1] << ','
			<< PrefixCounts[2] << ',' << PrefixCounts[3] << " Components="
			<< Components.Count[0] << ',' << Components.Count[1] << ','
			<< Components.Count[2] << ',' << Components.Count[3]
			<< " NominalF4=" << NominalF4 << '\n';
		return Passed ? 0 : 2;
	}

	int RunPreflight(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				OptionsValue.Rank,
				Layout,
				&Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		const CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		if (ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract)
			!= Identity.CandidateSourceHash)
		{
			std::cerr << "CandidateSourceIdentityMismatch\n";
			return 1;
		}
		ApplyDiagnosticOffsets(OptionsValue, Layout);
		const std::uint64_t VariantSourceHash =
			ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract);
		ABTS::M11Core::Vec3d NominalArrivalDirection;
		ABTS::M11Core::Vec3d NominalArrivalFaceNormal;
		double MinimumArrivalAlignment = -1.0;
		double MinimumArrivalFaceAlignment = -1.0;
		if (OptionsValue.ArrivalConeDegrees < 180.0
			|| OptionsValue.ArrivalFaceConeDegrees < 180.0)
		{
			InputEvaluation NominalEvaluation;
			std::string NominalFailure;
			if (!CandidateSearch::EvaluateInput(
					Layout,
					Contract,
					Layout.NominalInput,
					0x7u,
					NominalEvaluation,
					&NominalFailure)
				|| !NominalEvaluation.PrefixMembership[3]
				|| !NominalEvaluation.HasTargetHitVelocity)
			{
				std::cerr << "NominalArrivalDirectionUnavailable:"
					<< NominalFailure << '\n';
				return 1;
			}
			NominalArrivalDirection =
				NominalEvaluation.TargetHitVelocityCMPerSec.GetSafeNormal();
			NominalArrivalFaceNormal =
				(NominalEvaluation.TargetHitPositionCM
					- Layout.Scenario.Target.CenterCM).GetSafeNormal();
			MinimumArrivalAlignment = std::cos(
				OptionsValue.ArrivalConeDegrees
					* 3.14159265358979323846 / 180.0);
			MinimumArrivalFaceAlignment = std::cos(
				OptionsValue.ArrivalFaceConeDegrees
					* 3.14159265358979323846 / 180.0);
		}
		Grid GridValue;
		std::string Failure;
		if (!MakeGrid(Layout, OptionsValue, GridValue, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error)
		{
			std::cerr << "OutputCreateFailed\n";
			return 1;
		}
		const fs::path SamplePath = OptionsValue.Output / "samples.tsv";
		std::vector<Sample> Existing;
		if (OptionsValue.Resume
			&& !ReadSamples(SamplePath, Existing, Failure))
		{
			std::cerr << Failure << '\n';
			return 1;
		}
		std::uint64_t NextGlobal = OptionsValue.ShardIndex;
		if (!Existing.empty())
		{
			NextGlobal = Existing.back().GlobalIndex
				+ OptionsValue.ShardCount;
		}
		std::ofstream SamplesOut(
			SamplePath,
			OptionsValue.Resume && !Existing.empty()
				? std::ios::app
				: std::ios::trunc);
		if (!SamplesOut)
		{
			std::cerr << "SampleOutputOpenFailed\n";
			return 1;
		}
		if (Existing.empty())
		{
			SamplesOut << "global yaw pitch power prefix termination "
				"assists contacts hash\n";
		}
		std::vector<Sample> All = Existing;
		const std::uint64_t Total = GridValue.Count();
		while (NextGlobal < Total)
		{
			std::vector<std::uint64_t> Indices;
			Indices.reserve(OptionsValue.CheckpointEvery);
			for (std::uint64_t Index = NextGlobal;
				Index < Total
					&& Indices.size() < OptionsValue.CheckpointEvery;
				Index += OptionsValue.ShardCount)
			{
				Indices.push_back(Index);
			}
			std::vector<Sample> Chunk(Indices.size());
			std::vector<std::string> Failures(Indices.size());
			std::atomic<std::size_t> Cursor{0};
			std::vector<std::thread> Workers;
			const std::size_t WorkerCount = std::min<std::size_t>(
				OptionsValue.Threads,
				Indices.size());
			for (std::size_t WorkerIndex = 0;
				WorkerIndex < WorkerCount;
				++WorkerIndex)
			{
				Workers.emplace_back([&]()
				{
					while (true)
					{
						const std::size_t Local = Cursor.fetch_add(1);
						if (Local >= Indices.size())
						{
							return;
						}
						Sample Value;
						DecodeIndex(GridValue, Indices[Local], Value);
						InputEvaluation Evaluation;
						if (!CandidateSearch::EvaluateInput(
								Layout,
								Contract,
								InputFor(GridValue, Value),
								0x7u,
								Evaluation,
								&Failures[Local]))
						{
							continue;
						}
						if (Evaluation.PrefixMembership[3]
							&& !Evaluation.HasOrderedTerminalHit)
						{
							Evaluation.PrefixMembership[3] = false;
						}
						if (Evaluation.PrefixMembership[3]
							&& OptionsValue.ArrivalConeDegrees < 180.0)
						{
							const ABTS::M11Core::Vec3d ArrivalDirection =
								Evaluation.TargetHitVelocityCMPerSec
									.GetSafeNormal();
							Evaluation.PrefixMembership[3] =
								Evaluation.HasTargetHitVelocity
								&& ABTS::M11Core::Vec3d::DotProduct(
									ArrivalDirection,
									NominalArrivalDirection)
									>= MinimumArrivalAlignment;
						}
						if (Evaluation.PrefixMembership[3]
							&& OptionsValue.ArrivalFaceConeDegrees < 180.0)
						{
							const ABTS::M11Core::Vec3d ArrivalFaceNormal =
								(Evaluation.TargetHitPositionCM
									- Layout.Scenario.Target.CenterCM)
									.GetSafeNormal();
							Evaluation.PrefixMembership[3] =
								Evaluation.HasTargetHitVelocity
								&& ABTS::M11Core::Vec3d::DotProduct(
									ArrivalFaceNormal,
									NominalArrivalFaceNormal)
									>= MinimumArrivalFaceAlignment;
						}
						for (std::size_t Level = 0; Level < 4; ++Level)
						{
							if (Evaluation.PrefixMembership[Level])
							{
								Value.PrefixMask |=
									static_cast<std::uint8_t>(1u << Level);
							}
						}
						Value.Termination = static_cast<std::uint8_t>(
							Evaluation.Termination);
						Value.CompletedAssistCount =
							Evaluation.CompletedAssistCount;
						Value.TargetContactCount =
							Evaluation.TargetContactCount;
						Value.ResultHash = Evaluation.ResultHash;
						Chunk[Local] = Value;
					}
				});
			}
			for (std::thread& Worker : Workers)
			{
				Worker.join();
			}
			for (std::size_t Local = 0; Local < Chunk.size(); ++Local)
			{
				if (!Failures[Local].empty())
				{
					std::cerr << "SolveFailed:" << Indices[Local]
						<< ':' << Failures[Local] << '\n';
					return 1;
				}
				const Sample& Value = Chunk[Local];
				SamplesOut << Value.GlobalIndex << ' ' << Value.YawIndex
					<< ' ' << Value.PitchIndex << ' ' << Value.PowerIndex
					<< ' ' << static_cast<unsigned int>(Value.PrefixMask)
					<< ' ' << static_cast<unsigned int>(Value.Termination)
					<< ' ' << Value.CompletedAssistCount << ' '
					<< Value.TargetContactCount << ' '
					<< Hex64(Value.ResultHash) << '\n';
				All.push_back(Value);
			}
			SamplesOut.flush();
			NextGlobal = Indices.back() + OptionsValue.ShardCount;
			WriteSummary(
				OptionsValue.Output / "summary.json",
				OptionsValue,
				Identity,
				VariantSourceHash,
				Layout.Scenario.Target.HitRadiusCM,
				Layout.Scenario.Target.MinimumQualifyingCorridorQuality,
				GridValue,
				All,
				NextGlobal >= Total,
				"");
			std::cout << "[ABTS][M11-B-v2.2][Preflight] Shard="
				<< OptionsValue.ShardIndex << '/' << OptionsValue.ShardCount
				<< " Completed=" << All.size() << '\n';
		}
		return 0;
	}

	double Halton(std::uint64_t Index, const std::uint32_t Base)
	{
		double Result = 0.0;
		double Fraction = 1.0;
		while (Index > 0)
		{
			Fraction /= static_cast<double>(Base);
			Result += Fraction * static_cast<double>(Index % Base);
			Index /= Base;
		}
		return Result;
	}

	int RunScreenAim(const Options& OptionsValue)
	{
		CandidateLayout Layout;
		FrozenCandidateIdentity Identity;
		if (!ABTS::M11Search::BuildFrozenV4CandidateLayout(
				OptionsValue.Rank, Layout, &Identity))
		{
			std::cerr << "CandidateRankUnavailable\n";
			return 1;
		}
		const CandidateSearchContract Contract =
			CandidateSearchContract::MakeV2_1();
		if (ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract)
			!= Identity.CandidateSourceHash)
		{
			std::cerr << "CandidateSourceIdentityMismatch\n";
			return 1;
		}
		ApplyDiagnosticOffsets(OptionsValue, Layout);
		const std::uint64_t VariantSourceHash =
			ABTS::M11Search::ComputeCandidateSourceHash(Layout, Contract);
		std::error_code Error;
		fs::create_directories(OptionsValue.Output, Error);
		if (Error)
		{
			std::cerr << "OutputCreateFailed\n";
			return 1;
		}
		std::ofstream Samples(
			OptionsValue.Output / "screen_aim_samples.tsv",
			std::ios::trunc);
		if (!Samples)
		{
			std::cerr << "SampleOutputOpenFailed\n";
			return 1;
		}
		Samples << "index yaw pitch power prefix hash\n";
		std::array<std::uint64_t, 4> Counts{};
		const std::uint64_t Offset = Contract.ScreenAimSeed % 1000003ull;
		for (std::uint32_t Index = 0;
			Index < OptionsValue.ScreenAimSampleCount;
			++Index)
		{
			const std::uint64_t SampleIndex = Offset + Index + 1ull;
			const LaunchInput Input{
				ABTS::M11Core::Lerp(
					Layout.Launch.MinimumYawDegrees,
					Layout.Launch.MaximumYawDegrees,
					Halton(SampleIndex, 2)),
				ABTS::M11Core::Lerp(
					Layout.Launch.MinimumPitchDegrees,
					Layout.Launch.MaximumPitchDegrees,
					Halton(SampleIndex, 3)),
				Layout.Launch.MaximumPower};
			InputEvaluation Evaluation;
			std::string Failure;
			if (!CandidateSearch::EvaluateInput(
					Layout, Contract, Input, 0x7u, Evaluation, &Failure))
			{
				std::cerr << "ScreenAimSolveFailed:" << Index << ':'
					<< Failure << '\n';
				return 1;
			}
			if (Evaluation.PrefixMembership[3]
				&& !Evaluation.HasOrderedTerminalHit)
			{
				Evaluation.PrefixMembership[3] = false;
			}
			std::uint8_t PrefixMask = 0;
			for (std::size_t Level = 0; Level < Counts.size(); ++Level)
			{
				if (Evaluation.PrefixMembership[Level])
				{
					PrefixMask |= static_cast<std::uint8_t>(1u << Level);
					++Counts[Level];
				}
			}
			Samples << Index << ' ' << std::setprecision(17)
				<< Input.YawDegrees << ' ' << Input.PitchDegrees << ' '
				<< Input.Power << ' '
				<< static_cast<unsigned int>(PrefixMask) << ' '
				<< Hex64(Evaluation.ResultHash) << '\n';
		}
		std::ofstream Summary(
			OptionsValue.Output / "screen_aim_summary.json",
			std::ios::binary | std::ios::trunc);
		Summary << std::setprecision(17)
			<< "{\n  \"schema\":\"abts.m11b.v2_2.screen_aim.v1\",\n"
			<< "  \"candidateRank\":" << Identity.Rank << ",\n"
			<< "  \"variantSourceHash\":\"" << Hex64(VariantSourceHash)
			<< "\",\n  \"sampleCount\":"
			<< OptionsValue.ScreenAimSampleCount << ",\n"
			<< "  \"seed\":" << Contract.ScreenAimSeed << ",\n"
			<< "  \"power\":" << Layout.Launch.MaximumPower << ",\n"
			<< "  \"yawRangeDegrees\":["
			<< Layout.Launch.MinimumYawDegrees << ','
			<< Layout.Launch.MaximumYawDegrees << "],\n"
			<< "  \"pitchRangeDegrees\":["
			<< Layout.Launch.MinimumPitchDegrees << ','
			<< Layout.Launch.MaximumPitchDegrees << "],\n"
			<< "  \"prefixCounts\":[" << Counts[0] << ',' << Counts[1]
			<< ',' << Counts[2] << ',' << Counts[3] << "]\n}\n";
		std::cout << "[ABTS][M11-B-v2.2][ScreenAim] Samples="
			<< OptionsValue.ScreenAimSampleCount << " Prefix="
			<< Counts[0] << ',' << Counts[1] << ',' << Counts[2] << ','
			<< Counts[3] << '\n';
		return 0;
	}
}

int main(const int Argc, char** Argv)
{
	Options OptionsValue;
	std::string Failure;
	if (!ParseOptions(Argc, Argv, OptionsValue, Failure))
	{
		std::cerr << Failure << '\n';
		return 1;
	}
	if (OptionsValue.Merge)
	{
		return RunMerge(OptionsValue);
	}
	if (OptionsValue.ScreenAim)
	{
		return RunScreenAim(OptionsValue);
	}
	return RunPreflight(OptionsValue);
}
