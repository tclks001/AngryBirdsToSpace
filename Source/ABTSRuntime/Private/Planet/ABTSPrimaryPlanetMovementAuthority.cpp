// Copyright Epic Games, Inc. All Rights Reserved.

#include "Planet/ABTSPrimaryPlanetMovementAuthority.h"

#include "EngineUtils.h"
#include "Planet/ABTSM2Planet.h"
#include "World/ABTSM9Satellite.h"

bool ABTSPrimaryPlanetMovementAuthority::IsPrimaryCandidate(
	const AABTSM2Planet* Planet)
{
	return IsValid(Planet)
		&& !Planet->IsActorBeingDestroyed()
		&& Planet->IsPlanetReady()
		&& !Planet->IsA<AABTSM9Satellite>();
}

AABTSM2Planet* ABTSPrimaryPlanetMovementAuthority::Resolve(
	const UWorld* World,
	TWeakObjectPtr<AABTSM2Planet>& InOutCachedPrimary)
{
	if (IsPrimaryCandidate(InOutCachedPrimary.Get()))
	{
		return InOutCachedPrimary.Get();
	}
	InOutCachedPrimary.Reset();
	if (World == nullptr)
	{
		return nullptr;
	}

	AABTSM2Planet* Best = nullptr;
	for (TActorIterator<AABTSM2Planet> It(World); It; ++It)
	{
		AABTSM2Planet* Candidate = *It;
		if (!IsPrimaryCandidate(Candidate))
		{
			continue;
		}
		if (Best == nullptr
			|| Candidate->GetPlanetRadiusCM() > Best->GetPlanetRadiusCM()
			|| (FMath::IsNearlyEqual(
				Candidate->GetPlanetRadiusCM(),
				Best->GetPlanetRadiusCM())
				&& Candidate->GetPathName() < Best->GetPathName()))
		{
			Best = Candidate;
		}
	}
	InOutCachedPrimary = Best;
	return Best;
}
