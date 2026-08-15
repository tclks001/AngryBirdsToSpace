// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Guide/ABTSGuideRuleRuntime.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSGuideWorldSubsystem.generated.h"

/** Integration-owned dispatcher and per-world guide progress owner. */
UCLASS()
class ABTSRUNTIME_API UABTSGuideWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void PublishEvent(FName EventId, const FABTSGuideEventPayload& Payload);
	bool GetActiveGuide(FABTSGuidePresentationSnapshot& OutSnapshot) const;
	void ResetGuideProgress();
	int32 GetEventCount(FName EventId, FName SubjectId = NAME_None) const;
	void DumpState() const;

private:
	FABTSGuideRuleRuntime Runtime;
};
