// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingTypes.h"

namespace ABTSStylizedStencilAllocation
{
	constexpr uint8 PlayerBird = 1;
	constexpr uint8 Slingshot = 2;
	constexpr uint8 BuildingBody = 3;
	constexpr uint8 BuildingWeakPoint = 4;
	constexpr uint8 SatelliteTarget = 5;
	constexpr uint8 FinalePlanet = 6;
	constexpr uint8 FinaleUFO = 7;
}

bool FABTSStylizedViewPolicy::IsValid() const
{
	return Profile >= EABTSStylizedRenderProfile::GroundDay
		&& Profile <= EABTSStylizedRenderProfile::FinaleSpace
		&& (bApplyTone || bApplyOutline);
}

bool FABTSStylizedRenderingContract::IsObjectClassValid(
	EABTSStylizedObjectClass ObjectClass)
{
	return ObjectClass >= EABTSStylizedObjectClass::None
		&& ObjectClass <= EABTSStylizedObjectClass::FinaleUFO;
}

bool FABTSStylizedRenderingContract::IsViewClassValid(
	EABTSStylizedViewClass ViewClass)
{
	return ViewClass >= EABTSStylizedViewClass::MainWorld
		&& ViewClass <= EABTSStylizedViewClass::FinaleCinematicCapture;
}

bool FABTSStylizedRenderingContract::RequiresSelectiveStencil(
	EABTSStylizedObjectClass ObjectClass)
{
	return ResolveStencilValueForRenderer(ObjectClass) != 0;
}

uint8 FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
	EABTSStylizedObjectClass ObjectClass)
{
	using namespace ABTSStylizedStencilAllocation;
	switch (ObjectClass)
	{
	case EABTSStylizedObjectClass::PlayerBird:
		return PlayerBird;
	case EABTSStylizedObjectClass::Slingshot:
		return Slingshot;
	case EABTSStylizedObjectClass::BuildingBody:
		return BuildingBody;
	case EABTSStylizedObjectClass::BuildingWeakPoint:
		return BuildingWeakPoint;
	case EABTSStylizedObjectClass::SatelliteTarget:
		return SatelliteTarget;
	case EABTSStylizedObjectClass::FinalePlanet:
		return FinalePlanet;
	case EABTSStylizedObjectClass::FinaleUFO:
		return FinaleUFO;
	case EABTSStylizedObjectClass::None:
	case EABTSStylizedObjectClass::WorldSurface:
	case EABTSStylizedObjectClass::BackgroundProp:
	default:
		return 0;
	}
}

FABTSStylizedViewPolicy FABTSStylizedRenderingContract::ResolveViewPolicy(
	EABTSStylizedViewClass ViewClass,
	EABTSStylizedRenderProfile MainWorldProfile)
{
	FABTSStylizedViewPolicy Policy;
	switch (ViewClass)
	{
	case EABTSStylizedViewClass::GroundLandingPreview:
		Policy.Profile = EABTSStylizedRenderProfile::GroundDay;
		Policy.bAllowSelectiveStencil = false;
		break;
	case EABTSStylizedViewClass::SatelliteLandingPreview:
		Policy.Profile = EABTSStylizedRenderProfile::SatelliteGuide;
		// The lunar landing preview intentionally captures BaseColor so the
		// far side remains a readable navigation instrument.  Preserve that
		// lighting-independent palette and only add its thin outline layer.
		Policy.bApplyTone = false;
		Policy.bAllowSelectiveStencil = true;
		break;
	case EABTSStylizedViewClass::FinaleRemotePreview:
	case EABTSStylizedViewClass::FinaleCinematicCapture:
		Policy.Profile = EABTSStylizedRenderProfile::FinaleSpace;
		Policy.bAllowSelectiveStencil = true;
		break;
	case EABTSStylizedViewClass::MainWorld:
	default:
		Policy.Profile =
			MainWorldProfile >= EABTSStylizedRenderProfile::GroundDay
				&& MainWorldProfile <= EABTSStylizedRenderProfile::FinaleSpace
			? MainWorldProfile
			: EABTSStylizedRenderProfile::GroundDay;
		Policy.bAllowSelectiveStencil = true;
		break;
	}
	return Policy;
}

bool FABTSStylizedRenderingContract::IsViewClassImplemented(
	EABTSStylizedViewClass ViewClass)
{
	return IsViewClassValid(ViewClass);
}
