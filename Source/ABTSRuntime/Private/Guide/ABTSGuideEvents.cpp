// Copyright Epic Games, Inc. All Rights Reserved.

#include "Guide/ABTSGuideEvents.h"

#include "Guide/ABTSGuideWorldSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Party/ABTSBirdTypes.h"
#include "Slingshot/ABTSSlingshotTypes.h"

const FName FABTSGuideEventIds::WorldReady(TEXT("Guide.World.Ready"));
const FName FABTSGuideEventIds::ControlledBirdChanged(TEXT("Guide.Party.ControlledBirdChanged"));
const FName FABTSGuideEventIds::ItemAcquired(TEXT("Guide.Inventory.ItemAcquired"));
const FName FABTSGuideEventIds::HeldItemChanged(TEXT("Guide.Inventory.HeldItemChanged"));
const FName FABTSGuideEventIds::StakeInstalled(TEXT("Guide.Slingshot.StakeInstalled"));
const FName FABTSGuideEventIds::CordEndpointSelected(TEXT("Guide.Slingshot.CordEndpointSelected"));
const FName FABTSGuideEventIds::SlingshotAssembled(TEXT("Guide.Slingshot.Assembled"));
const FName FABTSGuideEventIds::SlingshotEntryRejected(TEXT("Guide.Slingshot.EntryRejected"));
const FName FABTSGuideEventIds::SlingshotReady(TEXT("Guide.Slingshot.Ready"));
const FName FABTSGuideEventIds::SlingshotPulling(TEXT("Guide.Slingshot.Pulling"));
const FName FABTSGuideEventIds::SlingshotPowerChanged(TEXT("Guide.Slingshot.PowerChanged"));
const FName FABTSGuideEventIds::SlingshotLaunched(TEXT("Guide.Slingshot.Launched"));
const FName FABTSGuideEventIds::SlingshotCompleted(TEXT("Guide.Slingshot.Completed"));
const FName FABTSGuideEventIds::ScoutRevealed(TEXT("Guide.Scout.Revealed"));
const FName FABTSGuideEventIds::BuildingMaterialRecovered(TEXT("Guide.Building.MaterialRecovered"));
const FName FABTSGuideEventIds::BridgeBuilt(TEXT("Guide.Bridge.Built"));
const FName FABTSGuideEventIds::BuildingTargetReady(TEXT("Guide.Building.TargetReady"));
const FName FABTSGuideEventIds::BuildingImpactAccepted(TEXT("Guide.Building.ImpactAccepted"));
const FName FABTSGuideEventIds::SatellitePracticeReady(TEXT("Guide.Satellite.PracticeReady"));
const FName FABTSGuideEventIds::SatelliteAssistPreviewed(TEXT("Guide.Satellite.AssistPreviewed"));
const FName FABTSGuideEventIds::FinaleReady(TEXT("Guide.Finale.Ready"));
const FName FABTSGuideEventIds::FinaleAiming(TEXT("Guide.Finale.Aiming"));
const FName FABTSGuideEventIds::FinalePrefixStable(TEXT("Guide.Finale.PrefixStable"));
const FName FABTSGuideEventIds::FinaleLaunched(TEXT("Guide.Finale.Launched"));

const FName FABTSGuideSubjects::None(NAME_None);
const FName FABTSGuideSubjects::Red(TEXT("Red"));
const FName FABTSGuideSubjects::Blue(TEXT("Blue"));
const FName FABTSGuideSubjects::Yellow(TEXT("Yellow"));
const FName FABTSGuideSubjects::Black(TEXT("Black"));
const FName FABTSGuideSubjects::Branch(TEXT("Branch"));
const FName FABTSGuideSubjects::PlantFiber(TEXT("PlantFiber"));
const FName FABTSGuideSubjects::Twig(TEXT("Twig"));
const FName FABTSGuideSubjects::Simple(TEXT("Simple"));
const FName FABTSGuideSubjects::Reinforced(TEXT("Reinforced"));
const FName FABTSGuideSubjects::Space(TEXT("Space"));
const FName FABTSGuideSubjects::Flow(TEXT("Flow"));
const FName FABTSGuideSubjects::Barrier(TEXT("Barrier"));

FName FABTSGuideSubjects::FromBird(const EABTSBirdId BirdId)
{
	switch (BirdId)
	{
	case EABTSBirdId::Blue: return Blue;
	case EABTSBirdId::Yellow: return Yellow;
	case EABTSBirdId::Black: return Black;
	default: return Red;
	}
}

FName FABTSGuideSubjects::FromItem(const EABTSItemId ItemId)
{
	switch (ItemId)
	{
	case EABTSItemId::Branch: return Branch;
	case EABTSItemId::PlantFiber: return PlantFiber;
	case EABTSItemId::SimpleStake:
	case EABTSItemId::SimpleCord: return Simple;
	case EABTSItemId::ReinforcedStake:
	case EABTSItemId::ReinforcedCord: return Reinforced;
	case EABTSItemId::SpaceStake:
	case EABTSItemId::SpaceCord: return Space;
	default: return FName(*ABTSGetItemFallbackLabel(ItemId));
	}
}

FName FABTSGuideSubjects::FromSlingshotTier(const EABTSSlingshotTier Tier)
{
	switch (Tier)
	{
	case EABTSSlingshotTier::Twig: return Twig;
	case EABTSSlingshotTier::Reinforced: return Reinforced;
	case EABTSSlingshotTier::Space: return Space;
	default: return Simple;
	}
}

bool FABTSGuideEventBus::Publish(
	const UObject* WorldContextObject,
	const FName EventId,
	const FName SubjectId,
	AActor* AnchorActor,
	const int32 PrimaryValue,
	const int32 SecondaryValue)
{
	if (WorldContextObject == nullptr || EventId.IsNone()) return false;
	UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr) return false;
	UABTSGuideWorldSubsystem* Subsystem = World->GetSubsystem<UABTSGuideWorldSubsystem>();
	if (Subsystem == nullptr) return false;
	FABTSGuideEventPayload Payload;
	Payload.SubjectId = SubjectId;
	Payload.PrimaryValue = PrimaryValue;
	Payload.SecondaryValue = SecondaryValue;
	Payload.AnchorActor = AnchorActor;
	if (AnchorActor != nullptr)
	{
		Payload.WorldLocation = AnchorActor->GetActorLocation();
		Payload.bHasWorldLocation = true;
	}
	Subsystem->PublishEvent(EventId, Payload);
	return true;
}

bool FABTSGuideEventBus::PublishAtLocation(
	const UObject* WorldContextObject,
	const FName EventId,
	const FName SubjectId,
	const FVector& WorldLocation,
	const int32 PrimaryValue,
	const int32 SecondaryValue)
{
	if (WorldContextObject == nullptr || EventId.IsNone()) return false;
	UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr) return false;
	UABTSGuideWorldSubsystem* Subsystem = World->GetSubsystem<UABTSGuideWorldSubsystem>();
	if (Subsystem == nullptr) return false;
	FABTSGuideEventPayload Payload;
	Payload.SubjectId = SubjectId;
	Payload.PrimaryValue = PrimaryValue;
	Payload.SecondaryValue = SecondaryValue;
	Payload.WorldLocation = WorldLocation;
	Payload.bHasWorldLocation = true;
	Subsystem->PublishEvent(EventId, Payload);
	return true;
}
