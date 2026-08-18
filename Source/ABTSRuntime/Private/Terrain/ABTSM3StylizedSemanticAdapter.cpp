// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3StylizedSemanticAdapter.h"

#include "Calibration/ABTSCalibrationTargetProxy.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM3StylizedSemanticAdapterPrivate
{
FABTSM3StylizedSemanticBinding MakeActorBinding(
	const UObject& SemanticAuthority,
	const AActor& Actor,
	const EABTSM3StylizedSemanticSource Source)
{
	FABTSM3StylizedSemanticBinding Binding;
	Binding.SemanticAuthority = &SemanticAuthority;
	Binding.Actor = &Actor;
	Binding.ObjectClass = EABTSStylizedObjectClass::SatelliteTarget;
	Binding.Source = Source;
	Binding.Granularity = EABTSM3StylizedSemanticGranularity::Actor;
	Binding.RepresentedInstanceCount = 1;
	return Binding;
}

FABTSM3StylizedSemanticBinding MakeComponentBinding(
	const AABTSM3Planet& Planet,
	const UPrimitiveComponent& Component,
	const EABTSStylizedObjectClass ObjectClass,
	const EABTSM3StylizedSemanticSource Source,
	const EABTSM3StylizedSemanticGranularity Granularity,
	const int32 RepresentedInstanceCount)
{
	FABTSM3StylizedSemanticBinding Binding;
	Binding.SemanticAuthority = &Planet;
	Binding.Actor = &Planet;
	Binding.Component = &Component;
	Binding.ObjectClass = ObjectClass;
	Binding.Source = Source;
	Binding.Granularity = Granularity;
	Binding.RepresentedInstanceCount = RepresentedInstanceCount;
	return Binding;
}
}

bool FABTSM3StylizedSemanticBinding::IsValid() const
{
	if (SemanticAuthority == nullptr
		|| Actor == nullptr
		|| ObjectClass == EABTSStylizedObjectClass::None
		|| !FABTSStylizedRenderingContract::IsObjectClassValid(ObjectClass)
		|| Source == EABTSM3StylizedSemanticSource::None
		|| Granularity == EABTSM3StylizedSemanticGranularity::None
		|| RepresentedInstanceCount < 0)
	{
		return false;
	}

	switch (Granularity)
	{
	case EABTSM3StylizedSemanticGranularity::Actor:
		return Component == nullptr && RepresentedInstanceCount == 1;
	case EABTSM3StylizedSemanticGranularity::Component:
		return Component != nullptr
			&& Component->GetOwner() == Actor
			&& RepresentedInstanceCount == 1;
	case EABTSM3StylizedSemanticGranularity::ComponentBatch:
		return Component != nullptr
			&& Component->GetOwner() == Actor;
	default:
		return false;
	}
}

bool FABTSM3StylizedSemanticAdapter::TryResolveActor(
	const UObject& SemanticAuthority,
	const AActor& Actor,
	FABTSM3StylizedSemanticBinding& OutBinding)
{
	OutBinding = FABTSM3StylizedSemanticBinding();
	if (const AABTSM9Satellite* Satellite =
		Cast<AABTSM9Satellite>(&SemanticAuthority))
	{
		if (Satellite == &Actor)
		{
			OutBinding =
				ABTSM3StylizedSemanticAdapterPrivate::MakeActorBinding(
					*Satellite,
					Actor,
					EABTSM3StylizedSemanticSource::PracticeSatelliteActor);
			return OutBinding.IsValid();
		}
		return false;
	}

	const AABTSM3MonthlySatellitePracticeRuntime* Runtime =
		Cast<AABTSM3MonthlySatellitePracticeRuntime>(&SemanticAuthority);
	if (Runtime == nullptr)
	{
		return false;
	}
	if (Runtime->GetRuntimeSatellite() == &Actor)
	{
		OutBinding =
			ABTSM3StylizedSemanticAdapterPrivate::MakeActorBinding(
				*Runtime,
				Actor,
				EABTSM3StylizedSemanticSource::PracticeSatelliteActor);
		return OutBinding.IsValid();
	}
	if (Runtime->GetRuntimeE5Target() == &Actor)
	{
		OutBinding =
			ABTSM3StylizedSemanticAdapterPrivate::MakeActorBinding(
				*Runtime,
				Actor,
				EABTSM3StylizedSemanticSource::PracticeBacksideE5Actor);
		return OutBinding.IsValid();
	}
	return false;
}

bool FABTSM3StylizedSemanticAdapter::TryResolveComponent(
	const UObject& SemanticAuthority,
	const UPrimitiveComponent& Component,
	FABTSM3StylizedSemanticBinding& OutBinding)
{
	OutBinding = FABTSM3StylizedSemanticBinding();
	const AABTSM3Planet* Planet = Cast<AABTSM3Planet>(&SemanticAuthority);
	if (Planet == nullptr)
	{
		return false;
	}

	if (Planet->ContinuousSurface == &Component)
	{
		OutBinding =
			ABTSM3StylizedSemanticAdapterPrivate::MakeComponentBinding(
				*Planet,
				Component,
				EABTSStylizedObjectClass::WorldSurface,
				EABTSM3StylizedSemanticSource::PrimaryContinuousSurface,
				EABTSM3StylizedSemanticGranularity::Component,
				1);
	}
	else if (Planet->ForestHISM == &Component)
	{
		OutBinding =
			ABTSM3StylizedSemanticAdapterPrivate::MakeComponentBinding(
				*Planet,
				Component,
				EABTSStylizedObjectClass::BackgroundProp,
				EABTSM3StylizedSemanticSource::ForestHISMBatch,
				EABTSM3StylizedSemanticGranularity::ComponentBatch,
				Planet->ForestHISM->GetInstanceCount());
	}
	else if (Planet->RockHISM == &Component)
	{
		OutBinding =
			ABTSM3StylizedSemanticAdapterPrivate::MakeComponentBinding(
				*Planet,
				Component,
				EABTSStylizedObjectClass::BackgroundProp,
				EABTSM3StylizedSemanticSource::RockHISMBatch,
				EABTSM3StylizedSemanticGranularity::ComponentBatch,
				Planet->RockHISM->GetInstanceCount());
	}
	return OutBinding.IsValid();
}

void FABTSM3StylizedSemanticAdapter::GatherPrimaryPlanetSemantics(
	const AABTSM3Planet& Planet,
	TArray<FABTSM3StylizedSemanticBinding>& OutBindings)
{
	OutBindings.Reset();
	const UPrimitiveComponent* const Components[] =
	{
		Planet.ContinuousSurface,
		Planet.ForestHISM,
		Planet.RockHISM
	};
	for (const UPrimitiveComponent* Component : Components)
	{
		FABTSM3StylizedSemanticBinding Binding;
		if (Component != nullptr
			&& TryResolveComponent(Planet, *Component, Binding))
		{
			OutBindings.Add(Binding);
		}
	}
}

void FABTSM3StylizedSemanticAdapter::GatherSatelliteSemantics(
	const AABTSM9Satellite& Satellite,
	TArray<FABTSM3StylizedSemanticBinding>& OutBindings)
{
	OutBindings.Reset();
	FABTSM3StylizedSemanticBinding Binding;
	if (TryResolveActor(Satellite, Satellite, Binding))
	{
		OutBindings.Add(Binding);
	}
}

void FABTSM3StylizedSemanticAdapter::GatherMonthlyPracticeSemantics(
	const AABTSM3MonthlySatellitePracticeRuntime& Runtime,
	TArray<FABTSM3StylizedSemanticBinding>& OutBindings)
{
	OutBindings.Reset();
	const AActor* const Actors[] =
	{
		Runtime.GetRuntimeSatellite(),
		Runtime.GetRuntimeE5Target()
	};
	for (const AActor* Actor : Actors)
	{
		FABTSM3StylizedSemanticBinding Binding;
		if (Actor != nullptr && TryResolveActor(Runtime, *Actor, Binding))
		{
			OutBindings.Add(Binding);
		}
	}
}

bool FABTSM3StylizedSemanticAdapter::TryResolveMonthlySatellitePreviewElement(
	const FABTSM3MonthlySatellitePreviewResult& Result,
	const FABTSM3MonthlySatellitePreviewCandidate& Candidate,
	const EABTSM3StylizedSatellitePreviewElement Element,
	EABTSStylizedObjectClass& OutObjectClass)
{
	OutObjectClass = EABTSStylizedObjectClass::None;
	const FABTSM3MonthlySatellitePreviewCandidate* CanonicalCandidate =
		FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
			Result,
			Candidate.SourceRouteCandidateId);
	if (!Result.bPreviewResultValid
		|| Result.bMonthlyWorldAccepted
		|| Result.RejectReason
			!= EABTSM3MonthlySatellitePreviewRejectReason::None
		|| Result.ResultHash == 0
		|| static_cast<uint64>(Result.ResultHash)
			!= FABTSM3MonthlySatellitePreviewBuilder::ComputeResultHash(Result)
		|| CanonicalCandidate == nullptr
		|| CanonicalCandidate->CandidateHash != Candidate.CandidateHash
		|| Candidate.CandidateHash == 0
		|| static_cast<uint64>(Candidate.CandidateHash)
			!= FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(
				Candidate))
	{
		return false;
	}

	switch (Element)
	{
	case EABTSM3StylizedSatellitePreviewElement::SatelliteSurface:
		break;
	case EABTSM3StylizedSatellitePreviewElement::BacksideE5Target:
		if (!Candidate.bE5OnSatelliteBackside
			&& !Candidate.bE1OperatorLandingClusterPlacement)
		{
			return false;
		}
		break;
	default:
		return false;
	}
	OutObjectClass = EABTSStylizedObjectClass::SatelliteTarget;
	return true;
}
