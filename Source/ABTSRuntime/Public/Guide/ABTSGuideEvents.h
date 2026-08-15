// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UObject;
enum class EABTSBirdId : uint8;
enum class EABTSItemId : uint8;
enum class EABTSSlingshotTier : uint8;

/** Small, stable payload shared by milestone systems that publish guide facts. */
struct ABTSRUNTIME_API FABTSGuideEventPayload
{
	FName SubjectId = NAME_None;
	int32 PrimaryValue = 0;
	int32 SecondaryValue = 0;
	TWeakObjectPtr<AActor> AnchorActor;
	FVector WorldLocation = FVector::ZeroVector;
	bool bHasWorldLocation = false;
};

/** Stable event IDs. Feature worktrees publish these; the integration dispatcher owns their meaning. */
struct ABTSRUNTIME_API FABTSGuideEventIds
{
	static const FName WorldReady;
	static const FName ControlledBirdChanged;
	static const FName ItemAcquired;
	static const FName HeldItemChanged;
	static const FName StakeInstalled;
	static const FName CordEndpointSelected;
	static const FName SlingshotAssembled;
	static const FName SlingshotEntryRejected;
	static const FName SlingshotReady;
	static const FName SlingshotPulling;
	static const FName SlingshotPowerChanged;
	static const FName SlingshotLaunched;
	static const FName SlingshotCompleted;
	static const FName ScoutRevealed;
	static const FName BuildingMaterialRecovered;
	static const FName BridgeBuilt;

	// Reserved P0 event IDs. Their producing features remain under M7/M11 ownership.
	static const FName BuildingTargetReady;
	static const FName BuildingImpactAccepted;
	static const FName SatellitePracticeReady;
	static const FName SatelliteAssistPreviewed;
	static const FName FinaleReady;
	static const FName FinaleAiming;
	static const FName FinalePrefixStable;
	static const FName FinaleLaunched;
};

/** Stable subject IDs used to specialize an event without multiplying event types. */
struct ABTSRUNTIME_API FABTSGuideSubjects
{
	static const FName None;
	static const FName Red;
	static const FName Blue;
	static const FName Yellow;
	static const FName Black;
	static const FName Branch;
	static const FName PlantFiber;
	static const FName Twig;
	static const FName Simple;
	static const FName Reinforced;
	static const FName Space;
	static const FName Flow;
	static const FName Barrier;

	static FName FromBird(EABTSBirdId BirdId);
	static FName FromItem(EABTSItemId ItemId);
	static FName FromSlingshotTier(EABTSSlingshotTier Tier);
};

/** One-call publishing facade. It has no gameplay side effects if no guide subsystem exists. */
class ABTSRUNTIME_API FABTSGuideEventBus
{
public:
	static bool Publish(
		const UObject* WorldContextObject,
		FName EventId,
		FName SubjectId = NAME_None,
		AActor* AnchorActor = nullptr,
		int32 PrimaryValue = 0,
		int32 SecondaryValue = 0);

	static bool PublishAtLocation(
		const UObject* WorldContextObject,
		FName EventId,
		FName SubjectId,
		const FVector& WorldLocation,
		int32 PrimaryValue = 0,
		int32 SecondaryValue = 0);
};
