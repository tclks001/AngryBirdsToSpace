// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ABTSM1BirdCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** Temporary third-person playable representation for M1; it deliberately owns no inventory or party logic. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM1BirdCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AABTSM1BirdCharacter();

	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable, Category = "ABTS|Bird|Presentation")
	void SetBirdVisualMesh(UStaticMesh* InMesh);

	UFUNCTION(BlueprintPure, Category = "ABTS|Bird|Presentation")
	UStaticMeshComponent* GetBirdVisual() const { return BirdVisual; }

private:
	void MoveForward(float Value);
	void MoveRight(float Value);

	/** Optional per-class model. Null falls back to the native engine sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Bird|Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMesh> BirdMeshOverride;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BirdVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
};
