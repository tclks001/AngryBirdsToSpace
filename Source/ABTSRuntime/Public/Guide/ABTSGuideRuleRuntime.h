// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Guide/ABTSGuideEvents.h"

enum class EABTSGuideAnchorMode : uint8
{
	ScreenTop,
	ControlledBird,
	EventActor
};

/** Asset-free P0 pictograms. P1 may replace individual drawings with soft texture assets. */
enum class EABTSGuidePictogram : uint8
{
	None,
	CollectResources,
	InstallStakes,
	ConnectFirst,
	ConnectSecond,
	SwitchBird,
	EnterLaunch,
	PullPouch,
	AdjustPower,
	ReleaseLaunch
};

struct ABTSRUNTIME_API FABTSGuideFactKey
{
	FName EventId = NAME_None;
	FName SubjectId = NAME_None;

	bool operator==(const FABTSGuideFactKey& Other) const
	{
		return EventId == Other.EventId && SubjectId == Other.SubjectId;
	}
};

FORCEINLINE uint32 GetTypeHash(const FABTSGuideFactKey& Key)
{
	return HashCombine(GetTypeHash(Key.EventId), GetTypeHash(Key.SubjectId));
}

struct ABTSRUNTIME_API FABTSGuideFactCondition
{
	FABTSGuideFactKey Key;
	int32 MinimumCount = 1;
};

struct ABTSRUNTIME_API FABTSGuideDefinition
{
	FName GuideId = NAME_None;
	FText Title;
	FText Body;
	FText InputHint;
	TArray<FABTSGuideFactCondition> RequiredFacts;
	TArray<FABTSGuideFactCondition> CompletionFacts;
	FABTSGuideFactKey AnchorFact;
	EABTSGuideAnchorMode AnchorMode = EABTSGuideAnchorMode::ScreenTop;
	EABTSGuidePictogram Pictogram = EABTSGuidePictogram::None;
	int32 Priority = 0;
};

struct ABTSRUNTIME_API FABTSGuidePresentationSnapshot
{
	FName GuideId = NAME_None;
	FText Title;
	FText Body;
	FText InputHint;
	int32 StepNumber = 0;
	int32 TotalSteps = 0;
	EABTSGuideAnchorMode AnchorMode = EABTSGuideAnchorMode::ScreenTop;
	EABTSGuidePictogram Pictogram = EABTSGuidePictogram::None;
	TWeakObjectPtr<AActor> AnchorActor;
	FVector WorldLocation = FVector::ZeroVector;
	bool bHasWorldLocation = false;
};

/** Deterministic, non-ticking fact/rule runtime used by the world subsystem and automation tests. */
class ABTSRUNTIME_API FABTSGuideRuleRuntime
{
public:
	FABTSGuideRuleRuntime();

	void Reset();
	void PublishEvent(FName EventId, const FABTSGuideEventPayload& Payload);
	bool GetActiveGuide(FABTSGuidePresentationSnapshot& OutSnapshot) const;
	int32 GetEventCount(FName EventId, FName SubjectId = NAME_None) const;
	const TArray<FABTSGuideDefinition>& GetDefinitions() const { return Definitions; }
	const TSet<FName>& GetCompletedGuideIds() const { return CompletedGuideIds; }

private:
	void BuildP0Definitions();
	bool AreConditionsMet(const TArray<FABTSGuideFactCondition>& Conditions) const;
	void EvaluateRules();
	void ActivateGuide(int32 DefinitionIndex);

	TArray<FABTSGuideDefinition> Definitions;
	TMap<FABTSGuideFactKey, int32> FactCounts;
	TMap<FABTSGuideFactKey, FABTSGuideEventPayload> LastPayloads;
	TSet<FName> CompletedGuideIds;
	int32 ActiveDefinitionIndex = INDEX_NONE;
	FABTSGuidePresentationSnapshot ActiveSnapshot;
};
