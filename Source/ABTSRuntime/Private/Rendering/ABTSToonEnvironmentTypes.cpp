// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSToonEnvironmentTypes.h"

#include "Components/DirectionalLightComponent.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Terrain/ABTSM3Planet.h"

namespace ABTSToonEnvironmentTypesPrivate
{
	constexpr uint64 FnvOffset = 14695981039346656037ull;
	constexpr uint64 FnvPrime = 1099511628211ull;

	template <typename ValueType>
	void HashValue(uint64& InOutHash, const ValueType& Value)
	{
		const uint8* Bytes = reinterpret_cast<const uint8*>(&Value);
		for (int32 Index = 0; Index < sizeof(ValueType); ++Index)
		{
			InOutHash ^= Bytes[Index];
			InOutHash *= FnvPrime;
		}
	}

	int64 Quantize(const double Value, const double Scale)
	{
		return FMath::RoundToInt64(Value * Scale);
	}

	bool Fail(FString* OutFailure, const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}
}

bool FABTSToonEnvironmentSnapshot::IsValid(const double Tolerance) const
{
	return Version == ContractVersion
		&& !PlanetCenterWorld.ContainsNaN()
		&& FMath::IsFinite(PlanetRadiusCM)
		&& PlanetRadiusCM > Tolerance
		&& !SunDirectionToSunWorld.ContainsNaN()
		&& FMath::Abs(SunDirectionToSunWorld.SizeSquared() - 1.0)
			<= FMath::Max(Tolerance, 1.0e-6)
		&& FABTSStylizedRenderingControl::IsProfileValid(Profile)
		&& WorldSeed > 0
		&& GeneratorVersion > 0
		&& GenerationAttempt >= 0
		&& bSourceWorldAccepted
		&& IdentityHash != 0;
}

double FABTSToonEnvironmentSnapshot::ComputeAltitudeCM(
	const FVector& WorldPosition) const
{
	return IsValid()
		? FVector::Distance(WorldPosition, PlanetCenterWorld) - PlanetRadiusCM
		: 0.0;
}

FVector FABTSToonEnvironmentSnapshot::ComputeRadialUp(
	const FVector& WorldPosition) const
{
	return IsValid()
		? (WorldPosition - PlanetCenterWorld).GetSafeNormal()
		: FVector::ZeroVector;
}

bool FABTSToonEnvironmentResolver::BuildSnapshot(
	const FVector& PlanetCenterWorld,
	const double PlanetRadiusCM,
	const FVector& SunDirectionToSunWorld,
	const EABTSStylizedRenderProfile Profile,
	const int32 WorldSeed,
	const int32 GeneratorVersion,
	const int32 GenerationAttempt,
	const bool bSourceWorldAccepted,
	FABTSToonEnvironmentSnapshot& OutSnapshot,
	FString* OutFailure)
{
	OutSnapshot = FABTSToonEnvironmentSnapshot();
	if (PlanetCenterWorld.ContainsNaN()
		|| !FMath::IsFinite(PlanetRadiusCM)
		|| PlanetRadiusCM <= 0.0)
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Planet center or radius is invalid."));
	}
	const FVector NormalizedSun = SunDirectionToSunWorld.GetSafeNormal();
	if (NormalizedSun.IsNearlyZero())
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Atmosphere sun direction is degenerate."));
	}
	if (!FABTSStylizedRenderingControl::IsProfileValid(Profile))
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Environment profile is outside the frozen profile domain."));
	}
	if (WorldSeed <= 0 || GeneratorVersion <= 0
		|| GenerationAttempt < 0 || !bSourceWorldAccepted)
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Generated-world identity is not accepted or complete."));
	}

	OutSnapshot.PlanetCenterWorld = PlanetCenterWorld;
	OutSnapshot.PlanetRadiusCM = PlanetRadiusCM;
	OutSnapshot.SunDirectionToSunWorld = NormalizedSun;
	OutSnapshot.Profile = Profile;
	OutSnapshot.WorldSeed = WorldSeed;
	OutSnapshot.GeneratorVersion = GeneratorVersion;
	OutSnapshot.GenerationAttempt = GenerationAttempt;
	OutSnapshot.bSourceWorldAccepted = bSourceWorldAccepted;
	OutSnapshot.IdentityHash = ComputeIdentityHash(OutSnapshot);
	if (!OutSnapshot.IsValid())
	{
		OutSnapshot = FABTSToonEnvironmentSnapshot();
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Resolved environment snapshot failed its final contract validation."));
	}
	return true;
}

bool FABTSToonEnvironmentResolver::ResolveWorldSnapshot(
	UWorld& World,
	const EABTSStylizedRenderProfile Profile,
	FABTSToonEnvironmentSnapshot& OutSnapshot,
	FString* OutFailure)
{
	OutSnapshot = FABTSToonEnvironmentSnapshot();
	TArray<AABTSM3Planet*> Planets;
	for (TActorIterator<AABTSM3Planet> It(&World); It; ++It)
	{
		if (IsValid(*It))
		{
			Planets.Add(*It);
		}
	}
	if (Planets.Num() != 1 || !Planets[0]->IsM3PresentationReady())
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			FString::Printf(
				TEXT("Expected one ready M3 presentation planet; found %d."),
				Planets.Num()));
	}

	FABTSFinaleWorldContract WorldContract;
	if (!Planets[0]->TryExportFinaleWorldContract(WorldContract)
		|| !WorldContract.IsUsable()
		|| !FMath::IsNearlyEqual(
			WorldContract.PrimaryRadiusCM,
			static_cast<double>(Planets[0]->GetPlanetRadiusCM()),
			1.0e-3))
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			TEXT("Accepted M3 world identity or primary radius is unavailable."));
	}

	TArray<UDirectionalLightComponent*> AtmosphereSuns;
	for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
	{
		UDirectionalLightComponent* Component =
			It->FindComponentByClass<UDirectionalLightComponent>();
		if (IsValid(Component)
			&& Component->IsVisible()
			&& Component->IsUsedAsAtmosphereSunLight()
			&& Component->GetAtmosphereSunLightIndex() == 0)
		{
			AtmosphereSuns.Add(Component);
		}
	}
	if (AtmosphereSuns.Num() != 1)
	{
		return ABTSToonEnvironmentTypesPrivate::Fail(
			OutFailure,
			FString::Printf(
				TEXT("Expected one visible Atmosphere Sun Light at index 0; found %d."),
				AtmosphereSuns.Num()));
	}

	// ULightComponent::GetDirection is the direction in which rays travel.
	// The environment contract stores the inverse: planet-to-sun direction.
	const FVector DirectionToSun = -AtmosphereSuns[0]->GetDirection();
	return BuildSnapshot(
		Planets[0]->GetPlanetCenterWorld(),
		WorldContract.PrimaryRadiusCM,
		DirectionToSun,
		Profile,
		WorldContract.Identity.WorldSeed,
		WorldContract.Identity.GeneratorVersion,
		WorldContract.Identity.GenerationAttempt,
		WorldContract.Identity.bSourceWorldAccepted,
		OutSnapshot,
		OutFailure);
}

uint64 FABTSToonEnvironmentResolver::ComputeIdentityHash(
	const FABTSToonEnvironmentSnapshot& Snapshot)
{
	using namespace ABTSToonEnvironmentTypesPrivate;
	uint64 Hash = FnvOffset;
	HashValue(Hash, Snapshot.Version);
	const int64 Values[] = {
		Quantize(Snapshot.PlanetCenterWorld.X, 10.0),
		Quantize(Snapshot.PlanetCenterWorld.Y, 10.0),
		Quantize(Snapshot.PlanetCenterWorld.Z, 10.0),
		Quantize(Snapshot.PlanetRadiusCM, 10.0),
		Quantize(Snapshot.SunDirectionToSunWorld.X, 1000000.0),
		Quantize(Snapshot.SunDirectionToSunWorld.Y, 1000000.0),
		Quantize(Snapshot.SunDirectionToSunWorld.Z, 1000000.0)
	};
	for (const int64 Value : Values)
	{
		HashValue(Hash, Value);
	}
	const uint8 Profile = static_cast<uint8>(Snapshot.Profile);
	HashValue(Hash, Profile);
	HashValue(Hash, Snapshot.WorldSeed);
	HashValue(Hash, Snapshot.GeneratorVersion);
	HashValue(Hash, Snapshot.GenerationAttempt);
	HashValue(Hash, Snapshot.bSourceWorldAccepted);
	return Hash != 0 ? Hash : 1;
}
