// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Party/ABTSBirdTypes.h"
#include "ABTSBirdParty.generated.h"

class AABTSBirdPartySettings;
class AABTSM2Planet;
class AABTSM25BirdCharacter;

struct FABTSBirdPathSample
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float DistanceFromPreviousCM = 0.0f;
};

struct FABTSBirdJumpEvent
{
	int32 Serial = INDEX_NONE;
	int32 PathGeneration = 0;
	FVector TakeoffLocation = FVector::ZeroVector;
	float TakeoffRadiusCM = 0.0f;
	float MaxHeightAboveTakeoffCM = 0.0f;
	float AgeSeconds = 0.0f;
	bool bLanded = false;
};

struct FABTSBirdPartyRuntime
{
	TWeakObjectPtr<AABTSM25BirdCharacter> Bird;
	EABTSBirdId BirdId = EABTSBirdId::Red;
	TArray<FABTSBirdPathSample> Path;
	TArray<FABTSBirdJumpEvent> JumpEvents;
	bool bFollowing = false;
	bool bWasGrounded = false;
	int32 LastConsumedJumpSerial = INDEX_NONE;
	float SevereDetachSeconds = 0.0f;
	float PreviousTargetDistanceCM = TNumericLimits<float>::Max();
};

/** Runtime owner of the fixed four-bird party, its queue topology and breadcrumb paths. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSBirdParty : public AActor
{
	GENERATED_BODY()

public:
	AABTSBirdParty();

	virtual void Tick(float DeltaSeconds) override;

	bool InitializeParty(AABTSM25BirdCharacter* InitialLeader);
	bool InitializePlanarParty(AABTSM25BirdCharacter* InitialLeader, const FVector& InPlaneOrigin, const FVector& InPlaneUp);
	bool IsPlanarParty() const { return bPlanarMode; }
	FVector GetSurfaceUpAt(const FVector& WorldLocation) const;
	const FVector& GetPlanarOrigin() const { return PlanarOrigin; }

	UFUNCTION(BlueprintCallable, Category = "ABTS|M4|Party")
	bool SwitchControlledBird(EABTSBirdId NewBirdId);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M4|Party")
	bool CycleControlledBird();

	UFUNCTION(BlueprintPure, Category = "ABTS|M4|Party")
	EABTSBirdId GetControlledBirdId() const { return ControlledBirdId; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M4|Party")
	AABTSM25BirdCharacter* GetControlledBird() const;

	const FABTSBirdPresentationConfig* GetPresentation(EABTSBirdId BirdId) const;
	AABTSBirdPartySettings* GetResolvedSettings() const { return Settings.Get(); }
	bool IsPartyReady() const { return bPartyReady; }
	int32 GetMemberCount() const { return PartyMembers.Num(); }
	const TArray<TObjectPtr<AABTSM25BirdCharacter>>& GetPartyMembers() const { return PartyMembers; }
	void SetSlingshotMode(bool bEnabled) { bSlingshotMode = bEnabled; }

private:
	void BuildResolvedPresentation();
	bool SpawnFollowers(AABTSM25BirdCharacter& InitialLeader);
	void RebuildQueue(EABTSBirdId NewLeaderId);
	void ResetFollowingRuntimeState(bool bSeedPaths);
	void RecordPathsAndJumpEvents(float DeltaSeconds);
	void UpdateFollowers(float DeltaSeconds);
	void UpdateFollower(FABTSBirdPartyRuntime& Follower, FABTSBirdPartyRuntime& Predecessor, float DeltaSeconds);
	void RecordPathSample(FABTSBirdPartyRuntime& Runtime);
	bool FindTargetBehind(const FABTSBirdPartyRuntime& Predecessor, float DistanceBehindCM, FABTSBirdPathSample& OutTarget) const;
	void TryPropagateJump(FABTSBirdPartyRuntime& Follower, const FABTSBirdPartyRuntime& Predecessor, const FABTSBirdPathSample& Target);
	void RecoverFollower(FABTSBirdPartyRuntime& Follower, const FABTSBirdPathSample& SafeTarget);
	FABTSBirdPartyRuntime* FindRuntime(EABTSBirdId BirdId);
	const FABTSBirdPartyRuntime* FindRuntime(EABTSBirdId BirdId) const;
	AABTSM2Planet* FindPlanet();
	float GetSurfaceDistanceCM(const FVector& A, const FVector& B) const;

	UPROPERTY(EditAnywhere, Category = "ABTS|M4|Spawning")
	TSubclassOf<AABTSM25BirdCharacter> FollowerBirdClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M4|Party", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<AABTSM25BirdCharacter>> PartyMembers;

	TWeakObjectPtr<AABTSBirdPartySettings> Settings;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	TArray<FABTSBirdPresentationConfig> ResolvedPresentation;
	TArray<FABTSBirdPartyRuntime> RuntimeByFixedId;
	TArray<EABTSBirdId> QueueOrder;
	EABTSBirdId ControlledBirdId = EABTSBirdId::Red;
	int32 PathGeneration = 1;
	int32 NextJumpSerial = 1;
	/** One short post-switch barrier prevents the old queue from emitting commands in the handoff frame. */
	float FollowerUpdatePauseRemainingSeconds = 0.0f;
	/** Cached editor experiment mode. A mode transition must discard breadcrumb-derived runtime state. */
	bool bUsingDirectControlledBirdFollowExperiment = false;
	bool bFollowModeInitialized = false;
	bool bPartyReady = false;
	bool bSlingshotMode = false;
	bool bPlanarMode = false;
	FVector PlanarOrigin = FVector::ZeroVector;
	FVector PlanarUp = FVector::UpVector;
};
