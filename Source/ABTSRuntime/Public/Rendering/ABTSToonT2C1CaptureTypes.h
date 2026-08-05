// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Slingshot/ABTSM6Types.h"

enum class EABTSToonT2C1CaptureSlice : uint8
{
	LandingPreviews = 0,
	FinaleRemotePreview
};

/** Explicit, testable command-line contract for one no-M7 T2-C1 slice. */
struct ABTSRUNTIME_API FABTSToonT2C1CaptureConfig
{
	static constexpr int32 ContractVersion = 1;

	bool bEnabled = false;
	EABTSToonT2C1CaptureSlice Slice =
		EABTSToonT2C1CaptureSlice::LandingPreviews;
	bool bStylized = true;
	bool bExitWhenComplete = false;
	int32 ExpectedWorldSeed = 312503;
	int32 ScreenPercentage = 100;
	int32 WarmupFrames = 8;
	double TimeoutSeconds = 180.0;
	FString OutputDirectory;
	FString BuildIdentity;

	static bool Parse(
		const TCHAR* CommandLine,
		FABTSToonT2C1CaptureConfig& OutConfig,
		FString* OutFailure = nullptr);
	bool IsValid(FString* OutFailure = nullptr) const;
};

/** Pure-data deterministic fixtures consumed by the production preview camera. */
class ABTSRUNTIME_API FABTSToonT2C1PreviewFixtureBuilder
{
public:
	static bool BuildGroundLandingPreview(
		const FVector& LandingWorld,
		const FVector& LandingUp,
		const FVector& TangentForward,
		FABTSM6TrajectoryPreview& OutPreview);

	static bool BuildSatelliteE5Preview(
		const FVector& SatelliteCenterWorld,
		double SatelliteRadiusCM,
		const FVector& E5World,
		const FVector& E5HalfExtentCM,
		const FVector& TangentForward,
		FABTSM6TrajectoryPreview& OutPreview);

	static uint64 ComputeFixtureHash(
		const FABTSM6TrajectoryPreview& Preview);
	static const TCHAR* LexToString(EABTSToonT2C1CaptureSlice Slice);
};
