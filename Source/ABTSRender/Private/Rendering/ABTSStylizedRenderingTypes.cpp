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
	constexpr uint8 CloudComposite = 8;
}

bool FABTSStylizedViewPolicy::IsValid() const
{
	return Profile >= EABTSStylizedRenderProfile::GroundDay
		&& Profile <= EABTSStylizedRenderProfile::FinaleSpace
		&& EnvironmentProfile >= EABTSStylizedRenderProfile::GroundDay
		&& EnvironmentProfile <= EABTSStylizedRenderProfile::FinaleSpace
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

uint8 FABTSStylizedRenderingContract::
	ResolveCloudCompositeStencilValueForRenderer()
{
	return ABTSStylizedStencilAllocation::CloudComposite;
}

bool FABTSStylizedRenderingContract::
	IsCloudCompositeStencilValueForRenderer(const uint8 StencilValue)
{
	return StencilValue == ResolveCloudCompositeStencilValueForRenderer();
}

bool FABTSStylizedRenderingContract::
	ShouldSuppressInternalOutlineBetweenStencilValues(
		uint8 CenterStencilValue,
		uint8 SampleStencilValue)
{
	return CenterStencilValue == SampleStencilValue
		&& IsCloudCompositeStencilValueForRenderer(CenterStencilValue);
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
		Policy.EnvironmentProfile = EABTSStylizedRenderProfile::GroundDay;
		Policy.bAllowSelectiveStencil = false;
		Policy.bUseWorldLighting = true;
		break;
	case EABTSStylizedViewClass::SatelliteLandingPreview:
		// Surface exposure/tone deliberately remain GroundDay-equivalent so the
		// same lunar patch matches an equivalent gameplay camera. Deep space is an
		// independent empty-background replacement, not a second lighting profile.
		Policy.Profile = EABTSStylizedRenderProfile::GroundDay;
		Policy.EnvironmentProfile =
			EABTSStylizedRenderProfile::SatelliteGuide;
		Policy.bAllowSelectiveStencil = true;
		Policy.bUseWorldLighting = true;
		Policy.bReplaceEnvironmentBackground = true;
		break;
	case EABTSStylizedViewClass::FinaleRemotePreview:
	case EABTSStylizedViewClass::FinaleCinematicCapture:
		Policy.Profile = EABTSStylizedRenderProfile::FinaleSpace;
		Policy.EnvironmentProfile = EABTSStylizedRenderProfile::FinaleSpace;
		Policy.bAllowSelectiveStencil = true;
		break;
	case EABTSStylizedViewClass::MainWorld:
	default:
		Policy.Profile =
			MainWorldProfile >= EABTSStylizedRenderProfile::GroundDay
				&& MainWorldProfile <= EABTSStylizedRenderProfile::FinaleSpace
			? MainWorldProfile
			: EABTSStylizedRenderProfile::GroundDay;
		Policy.EnvironmentProfile = Policy.Profile;
		Policy.bAllowSelectiveStencil = true;
		break;
	}
	return Policy;
}

EABTSStylizedRenderProfile
FABTSStylizedRenderingContract::ResolveMainWorldProfile(
	const bool bFinaleActive,
	const EABTSStylizedRenderProfile ConfiguredProfile)
{
	if (bFinaleActive)
	{
		return EABTSStylizedRenderProfile::FinaleSpace;
	}
	return ConfiguredProfile >= EABTSStylizedRenderProfile::GroundDay
		&& ConfiguredProfile <= EABTSStylizedRenderProfile::FinaleSpace
		? ConfiguredProfile
		: EABTSStylizedRenderProfile::GroundDay;
}

bool FABTSStylizedRenderingContract::IsViewClassImplemented(
	EABTSStylizedViewClass ViewClass)
{
	return IsViewClassValid(ViewClass);
}
