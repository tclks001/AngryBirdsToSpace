// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/ABTSM10GameMode.h"
#include "ABTSM11GameMode.generated.h"

class AABTSM11FinaleSystem;
class AABTSM11FinaleInteractionSystem;
class AABTSM11FinaleCameraCaptureRunner;
class AABTSM6SlingshotCamera;
class AABTSM6SlingshotSystem;
class UWorld;

/**
 * M11 entry point. M10 remains intact; this subclass adds the M11-B layout
 * authority and M11-C interaction after the accepted World places its player.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11GameMode : public AABTSM10GameMode
{
	GENERATED_BODY()

public:
	AABTSM11GameMode();

	AABTSM11FinaleSystem* GetFinaleSystem() const
	{
		return FinaleSystem;
	}
	AABTSM11FinaleInteractionSystem* GetFinaleInteractionSystem() const
	{
		return FinaleInteractionSystem;
	}
	/**
	 * Resolves the exact camera subclass spawned by the unique M6 runtime
	 * system. M11 clones that class so both launch modes consume the same
	 * Blueprint defaults without sharing one mutable camera Actor.
	 */
	static TSubclassOf<AABTSM6SlingshotCamera>
		ResolveRuntimeSlingshotCameraClass(
			UWorld& World,
			const AABTSM6SlingshotSystem& SlingshotSystem,
			int32* OutMatchingCameraCount = nullptr);

protected:
	virtual void InitGame(
		const FString& MapName,
		const FString& Options,
		FString& ErrorMessage) override;
	virtual void OnInitialPlayerPlaced(
		ACharacter& Character,
		const FTransform& SpawnTransform,
		int32 SpawnCellId) override;

private:
	/** Enables the frozen M3R-5.2/M5.1/M11 frame on the authored production map. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11|Frozen Production Frame")
	bool bEnableMonthlyFinalePreviewIntegration = false;

	/** Exact M3 SourceRouteCandidateId; array order is never an authority. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11|Frozen Production Frame",
		meta = (EditCondition = "bEnableMonthlyFinalePreviewIntegration"))
	int32 MonthlyFinalePreviewCandidateId = INDEX_NONE;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C")
	TSubclassOf<AABTSM11FinaleInteractionSystem>
		FinaleInteractionSystemClass;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleInteractionSystem>
		FinaleInteractionSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleCameraCaptureRunner>
		FinaleCameraCaptureRunner;
};
