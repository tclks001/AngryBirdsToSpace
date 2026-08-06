// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonT2C1CaptureTypes.h"

#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace ABTSToonT2C1CaptureTypesPrivate
{
	bool RejectT2C1Config(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	void HashBytes(uint64& Hash, const void* Data, const SIZE_T Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (SIZE_T Index = 0; Index < Size; ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	void HashQuantizedVector(uint64& Hash, const FVector& Value)
	{
		const int64 Quantized[] = {
			FMath::RoundToInt64(Value.X * 10.0),
			FMath::RoundToInt64(Value.Y * 10.0),
			FMath::RoundToInt64(Value.Z * 10.0)};
		HashBytes(Hash, Quantized, sizeof(Quantized));
	}
}

bool FABTSToonT2C1CaptureConfig::Parse(
	const TCHAR* CommandLine,
	FABTSToonT2C1CaptureConfig& OutConfig,
	FString* OutFailure)
{
	using namespace ABTSToonT2C1CaptureTypesPrivate;
	OutConfig = FABTSToonT2C1CaptureConfig();
	if (CommandLine == nullptr)
	{
		return RejectT2C1Config(OutFailure, TEXT("Command line is null."));
	}
	FString Suite;
	FParse::Value(CommandLine, TEXT("ABTSVisualCaptureSuite="), Suite);
	OutConfig.bEnabled = FParse::Param(
		CommandLine,
		TEXT("ABTSToonT2C1Capture"))
		|| Suite.Equals(TEXT("ToonT2C1"), ESearchCase::IgnoreCase);
	if (!OutConfig.bEnabled)
	{
		return true;
	}

	FString SliceText;
	if (FParse::Value(CommandLine, TEXT("ABTSToonT2C1Slice="), SliceText))
	{
		if (SliceText.Equals(TEXT("LandingPreviews"), ESearchCase::IgnoreCase))
		{
			OutConfig.Slice = EABTSToonT2C1CaptureSlice::LandingPreviews;
		}
		else if (SliceText.Equals(TEXT("FinaleRemotePreview"), ESearchCase::IgnoreCase))
		{
			OutConfig.Slice = EABTSToonT2C1CaptureSlice::FinaleRemotePreview;
		}
		else
		{
			return RejectT2C1Config(
				OutFailure,
				TEXT("ABTSToonT2C1Slice must be LandingPreviews or FinaleRemotePreview."));
		}
	}
	int32 Stylized = OutConfig.bStylized ? 1 : 0;
	if (FParse::Value(CommandLine, TEXT("ABTSToonT2C1Stylized="), Stylized))
	{
		if (Stylized != 0 && Stylized != 1)
		{
			return RejectT2C1Config(
				OutFailure,
				TEXT("ABTSToonT2C1Stylized must be 0 or 1."));
		}
		OutConfig.bStylized = Stylized != 0;
	}
	OutConfig.bExitWhenComplete = FParse::Param(
		CommandLine,
		TEXT("ABTSToonT2C1ExitWhenDone"));
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1ExpectedSeed="),
		OutConfig.ExpectedWorldSeed);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1ScreenPercentage="),
		OutConfig.ScreenPercentage);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1WarmupFrames="),
		OutConfig.WarmupFrames);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1TimeoutSeconds="),
		OutConfig.TimeoutSeconds);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1Output="),
		OutConfig.OutputDirectory);
	FParse::Value(
		CommandLine,
		TEXT("ABTSToonT2C1BuildId="),
		OutConfig.BuildIdentity);
	if (!OutConfig.OutputDirectory.IsEmpty())
	{
		OutConfig.OutputDirectory = FPaths::ConvertRelativePathToFull(
			OutConfig.OutputDirectory);
		FPaths::NormalizeDirectoryName(OutConfig.OutputDirectory);
	}
	return OutConfig.IsValid(OutFailure);
}

bool FABTSToonT2C1CaptureConfig::IsValid(FString* OutFailure) const
{
	using namespace ABTSToonT2C1CaptureTypesPrivate;
	if (!bEnabled)
	{
		return true;
	}
	if (ExpectedWorldSeed == 0)
	{
		return RejectT2C1Config(OutFailure, TEXT("Expected seed must be non-zero."));
	}
	if (ScreenPercentage != 50
		&& ScreenPercentage != 75
		&& ScreenPercentage != 100)
	{
		return RejectT2C1Config(
			OutFailure,
			TEXT("Screen percentage must be 50, 75, or 100."));
	}
	if (WarmupFrames < 1 || WarmupFrames > 600)
	{
		return RejectT2C1Config(OutFailure, TEXT("Warmup frames must be in [1, 600]."));
	}
	if (!FMath::IsFinite(TimeoutSeconds)
		|| TimeoutSeconds < 10.0
		|| TimeoutSeconds > 1800.0)
	{
		return RejectT2C1Config(OutFailure, TEXT("Timeout must be in [10, 1800] seconds."));
	}
	if (OutputDirectory.IsEmpty() || FPaths::IsRelative(OutputDirectory))
	{
		return RejectT2C1Config(OutFailure, TEXT("ABTSToonT2C1Output must be an absolute path."));
	}
	if (BuildIdentity.IsEmpty())
	{
		return RejectT2C1Config(OutFailure, TEXT("ABTSToonT2C1BuildId is required."));
	}
	if (Slice == EABTSToonT2C1CaptureSlice::FinaleRemotePreview
		&& bExitWhenComplete)
	{
		return RejectT2C1Config(
			OutFailure,
			TEXT("FinaleRemotePreview must leave process exit ownership to the M11 recorder."));
	}
	return true;
}

bool FABTSToonT2C1PreviewFixtureBuilder::BuildGroundLandingPreview(
	const FVector& LandingWorld,
	const FVector& LandingUp,
	const FVector& TangentForward,
	FABTSM6TrajectoryPreview& OutPreview)
{
	OutPreview = FABTSM6TrajectoryPreview();
	const FVector Up = LandingUp.GetSafeNormal();
	const FVector Tangent = FVector::VectorPlaneProject(
		TangentForward,
		Up).GetSafeNormal();
	if (LandingWorld.ContainsNaN() || Up.IsNearlyZero() || Tangent.IsNearlyZero())
	{
		return false;
	}
	const FVector Velocity = (Tangent * 0.72 - Up * 0.69).GetSafeNormal() * 3300.0;
	OutPreview.SlingshotTier = EABTSSlingshotTier::Reinforced;
	OutPreview.bHasPrimarySurfaceLanding = true;
	OutPreview.PrimarySurfaceLandingWorld = LandingWorld;
	OutPreview.PrimarySurfaceLandingVelocity = Velocity;
	OutPreview.TerminalType = EABTSM6TrajectoryTerminalType::PrimarySurface;
	OutPreview.TerminalWorldLocation = LandingWorld;
	OutPreview.TerminalWorldVelocity = Velocity;
	for (int32 Index = 11; Index >= 0; --Index)
	{
		const double Distance = static_cast<double>(Index) * 115.0;
		const double NormalizedBacktrack =
			static_cast<double>(Index) / 11.0;
		const double GravityArcLift =
			FMath::Square(NormalizedBacktrack) * 520.0;
		OutPreview.WorldPoints.Add(
			LandingWorld
			- Velocity.GetSafeNormal() * Distance
			+ Up * GravityArcLift);
	}
	OutPreview.InitialWorldLocation = OutPreview.WorldPoints[0];
	OutPreview.InitialWorldVelocity = Velocity;
	OutPreview.PredictedPathLengthCM = 11.0f * 115.0f;
	return true;
}

bool FABTSToonT2C1PreviewFixtureBuilder::BuildSatelliteE5Preview(
	const FVector& SatelliteCenterWorld,
	const double SatelliteRadiusCM,
	const FVector& E5World,
	const FVector& E5HalfExtentCM,
	const FVector& TangentForward,
	FABTSM6TrajectoryPreview& OutPreview)
{
	OutPreview = FABTSM6TrajectoryPreview();
	const FVector Up = (E5World - SatelliteCenterWorld).GetSafeNormal();
	const FVector Tangent = FVector::VectorPlaneProject(
		TangentForward,
		Up).GetSafeNormal();
	if (SatelliteCenterWorld.ContainsNaN()
		|| E5World.ContainsNaN()
		|| E5HalfExtentCM.ContainsNaN()
		|| !FMath::IsFinite(SatelliteRadiusCM)
		|| SatelliteRadiusCM <= 0.0
		|| E5HalfExtentCM.GetMin() <= 0.0
		|| Up.IsNearlyZero()
		|| Tangent.IsNearlyZero())
	{
		return false;
	}
	// The runtime predictor reports the swept collision point on the E5 face,
	// never the actor pivot at the centre of the large proxy cube.  Keep this
	// deterministic fixture on the outward face as well; aiming at the pivot
	// places the production 1200 cm preview camera almost inside an 840 cm cube
	// and produces a meaningless full-frame magenta surface.
	const FVector TerminalWorld =
		E5World + Up * E5HalfExtentCM.GetMax();
	const FVector Velocity = (Tangent * 0.90 - Up * 0.20).GetSafeNormal() * 3300.0;
	OutPreview.SlingshotTier = EABTSSlingshotTier::Reinforced;
	OutPreview.TerminalType = EABTSM6TrajectoryTerminalType::SatelliteE5;
	OutPreview.TerminalWorldLocation = TerminalWorld;
	OutPreview.TerminalWorldVelocity = Velocity;
	OutPreview.bHasSatelliteEncounter = true;
	OutPreview.EncounterSatelliteCenterWorld = SatelliteCenterWorld;
	OutPreview.EncounterSatelliteRadiusCM = static_cast<float>(SatelliteRadiusCM);
	for (int32 Index = 15; Index >= 0; --Index)
	{
		const double Distance = static_cast<double>(Index) * 95.0;
		const double Lift = FMath::Square(static_cast<double>(Index) / 15.0) * 180.0;
		OutPreview.WorldPoints.Add(
			TerminalWorld - Velocity.GetSafeNormal() * Distance + Up * Lift);
	}
	OutPreview.InitialWorldLocation = OutPreview.WorldPoints[0];
	OutPreview.InitialWorldVelocity = Velocity;
	OutPreview.PredictedPathLengthCM = 15.0f * 95.0f;
	return true;
}

uint64 FABTSToonT2C1PreviewFixtureBuilder::ComputeFixtureHash(
	const FABTSM6TrajectoryPreview& Preview)
{
	using namespace ABTSToonT2C1CaptureTypesPrivate;
	uint64 Hash = 1469598103934665603ull;
	const uint8 Terminal = static_cast<uint8>(Preview.TerminalType);
	HashBytes(Hash, &Terminal, sizeof(Terminal));
	HashQuantizedVector(Hash, Preview.InitialWorldLocation);
	HashQuantizedVector(Hash, Preview.InitialWorldVelocity);
	HashQuantizedVector(Hash, Preview.TerminalWorldLocation);
	HashQuantizedVector(Hash, Preview.TerminalWorldVelocity);
	for (const FVector& Point : Preview.WorldPoints)
	{
		HashQuantizedVector(Hash, Point);
	}
	return Hash;
}

const TCHAR* FABTSToonT2C1PreviewFixtureBuilder::LexToString(
	const EABTSToonT2C1CaptureSlice Slice)
{
	switch (Slice)
	{
	case EABTSToonT2C1CaptureSlice::LandingPreviews:
		return TEXT("LandingPreviews");
	case EABTSToonT2C1CaptureSlice::FinaleRemotePreview:
		return TEXT("FinaleRemotePreview");
	default:
		return TEXT("Unknown");
	}
}
