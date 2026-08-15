// Copyright Epic Games, Inc. All Rights Reserved.

#include "Guide/ABTSGuideWorldSubsystem.h"

#include "ABTSRuntime.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarGuideEnabled(
		TEXT("abts.Guide.Enabled"),
		1,
		TEXT("Show and schedule the integration-owned guide overlay (0/1)."));

	template <typename CallbackType>
	void ForEachGameGuideSubsystem(CallbackType&& Callback)
	{
		if (GEngine == nullptr) return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World == nullptr || !World->IsGameWorld()) continue;
			if (UABTSGuideWorldSubsystem* Subsystem = World->GetSubsystem<UABTSGuideWorldSubsystem>())
			{
				Callback(*Subsystem);
			}
		}
	}

	void DumpGuideState()
	{
		ForEachGameGuideSubsystem([](const UABTSGuideWorldSubsystem& Subsystem) { Subsystem.DumpState(); });
	}

	void ResetGuideState()
	{
		ForEachGameGuideSubsystem([](UABTSGuideWorldSubsystem& Subsystem) { Subsystem.ResetGuideProgress(); });
	}

	FAutoConsoleCommand DumpGuideCommand(
		TEXT("abts.Guide.Dump"),
		TEXT("Print the active guide and P0 fact counts."),
		FConsoleCommandDelegate::CreateStatic(&DumpGuideState));

	FAutoConsoleCommand ResetGuideCommand(
		TEXT("abts.Guide.Reset"),
		TEXT("Reset guide facts and completion state for active game worlds."),
		FConsoleCommandDelegate::CreateStatic(&ResetGuideState));
}

void UABTSGuideWorldSubsystem::PublishEvent(const FName EventId, const FABTSGuideEventPayload& Payload)
{
	if (EventId.IsNone()) return;
	Runtime.PublishEvent(EventId, Payload);
	UE_LOG(LogABTSRuntime, Verbose,
		TEXT("[ABTS][Guide][Event] Id=%s Subject=%s Primary=%d Secondary=%d Anchor=%s"),
		*EventId.ToString(), *Payload.SubjectId.ToString(), Payload.PrimaryValue,
		Payload.SecondaryValue, *GetNameSafe(Payload.AnchorActor.Get()));
}

bool UABTSGuideWorldSubsystem::GetActiveGuide(FABTSGuidePresentationSnapshot& OutSnapshot) const
{
	if (CVarGuideEnabled.GetValueOnGameThread() == 0 || !Runtime.GetActiveGuide(OutSnapshot)) return false;
	UWorld* World = GetWorld();
	if (World == nullptr) return false;
	for (TActorIterator<AABTSM6SlingshotSystem> It(World); It; ++It)
	{
		const EABTSM6LaunchState State = It->GetLaunchState();
		if (State == EABTSM6LaunchState::Flying
			|| State == EABTSM6LaunchState::Settling
			|| State == EABTSM6LaunchState::Returning)
		{
			return false;
		}
	}
	if (OutSnapshot.AnchorMode == EABTSGuideAnchorMode::ControlledBird)
	{
		for (TActorIterator<AABTSBirdParty> It(World); It; ++It)
		{
			OutSnapshot.AnchorActor = It->GetControlledBird();
			if (OutSnapshot.AnchorActor.IsValid())
			{
				OutSnapshot.WorldLocation = OutSnapshot.AnchorActor->GetActorLocation();
				OutSnapshot.bHasWorldLocation = true;
			}
			break;
		}
	}
	return true;
}

void UABTSGuideWorldSubsystem::ResetGuideProgress()
{
	Runtime.Reset();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Guide][Reset] World=%s"), *GetNameSafe(GetWorld()));
}

int32 UABTSGuideWorldSubsystem::GetEventCount(const FName EventId, const FName SubjectId) const
{
	return Runtime.GetEventCount(EventId, SubjectId);
}

void UABTSGuideWorldSubsystem::DumpState() const
{
	FABTSGuidePresentationSnapshot Snapshot;
	const bool bHasActive = Runtime.GetActiveGuide(Snapshot);
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][Guide][Dump] World=%s Enabled=%d Active=%s Step=%d/%d Completed=%d Definitions=%d"),
		*GetNameSafe(GetWorld()), CVarGuideEnabled.GetValueOnGameThread(),
		bHasActive ? *Snapshot.GuideId.ToString() : TEXT("None"),
		Snapshot.StepNumber, Snapshot.TotalSteps,
		Runtime.GetCompletedGuideIds().Num(), Runtime.GetDefinitions().Num());
}
