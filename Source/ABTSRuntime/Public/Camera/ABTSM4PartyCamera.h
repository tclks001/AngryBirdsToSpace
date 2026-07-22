// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "ABTSM4PartyCamera.generated.h"

class AABTSBirdParty;
class AABTSM2Planet;
class AABTSM25BirdCharacter;

/** Elevated top-down camera that smoothly tracks the currently controlled party bird. */
UCLASS()
class ABTSRUNTIME_API AABTSM4PartyCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM4PartyCamera();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void UpdateCamera(float DeltaSeconds, bool bForceInstant);
	AABTSBirdParty* FindParty();
	AABTSM2Planet* FindPlanet();

	TWeakObjectPtr<AABTSBirdParty> Party;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	TWeakObjectPtr<AABTSM25BirdCharacter> LastTargetBird;
	FVector LastValidForward = FVector::ForwardVector;
	bool bInitializedView = false;
};
