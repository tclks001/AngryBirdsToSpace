// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSStylizedRenderProfile.h"

class UWorld;

/**
 * Read-only spherical environment identity shared by the T4 lighting, fog,
 * cloud and sky consumers.  It never owns or mutates gameplay actors.
 */
struct ABTSRUNTIME_API FABTSToonEnvironmentSnapshot
{
	static constexpr int32 ContractVersion = 1;

	int32 Version = ContractVersion;
	FVector PlanetCenterWorld = FVector::ZeroVector;
	double PlanetRadiusCM = 0.0;
	/** Unit vector from the planet toward the atmosphere sun. */
	FVector SunDirectionToSunWorld = FVector::ZeroVector;
	EABTSStylizedRenderProfile Profile =
		EABTSStylizedRenderProfile::GroundDay;
	int32 WorldSeed = 0;
	int32 GeneratorVersion = 0;
	int32 GenerationAttempt = INDEX_NONE;
	bool bSourceWorldAccepted = false;
	uint64 IdentityHash = 0;

	bool IsValid(double Tolerance = 1.0e-4) const;
	double ComputeAltitudeCM(const FVector& WorldPosition) const;
	FVector ComputeRadialUp(const FVector& WorldPosition) const;
};

/** Pure builder plus the single Integration-owned runtime resolver. */
class ABTSRUNTIME_API FABTSToonEnvironmentResolver
{
public:
	static bool BuildSnapshot(
		const FVector& PlanetCenterWorld,
		double PlanetRadiusCM,
		const FVector& SunDirectionToSunWorld,
		EABTSStylizedRenderProfile Profile,
		int32 WorldSeed,
		int32 GeneratorVersion,
		int32 GenerationAttempt,
		bool bSourceWorldAccepted,
		FABTSToonEnvironmentSnapshot& OutSnapshot,
		FString* OutFailure = nullptr);

	/**
	 * Resolves exactly one accepted M3 planet and exactly one enabled
	 * Atmosphere Sun Light at index zero. Ambiguous or incomplete worlds fail
	 * closed so later T4 stages cannot silently choose a different frame.
	 */
	static bool ResolveWorldSnapshot(
		UWorld& World,
		EABTSStylizedRenderProfile Profile,
		FABTSToonEnvironmentSnapshot& OutSnapshot,
		FString* OutFailure = nullptr);

	static uint64 ComputeIdentityHash(
		const FABTSToonEnvironmentSnapshot& Snapshot);
};
