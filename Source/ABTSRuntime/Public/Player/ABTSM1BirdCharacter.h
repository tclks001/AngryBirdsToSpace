// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ABTSM1BirdCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class USkeletalMeshComponent;

/** Temporary third-person playable representation for M1; it deliberately owns no inventory or party logic. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM1BirdCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AABTSM1BirdCharacter();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintPure, Category = "ABTS|Bird|Presentation")
	USkeletalMeshComponent* GetBirdVisual() const { return BirdVisual; }

protected:
	/** Local mesh-axis correction used by physics-driven visual frames. */
	FQuat GetBirdVisualAxisCorrection() const { return BirdVisualRelativeRotation.Quaternion(); }
	/** User-authored offset from the collision support point, expressed in the presentation frame. */
	const FVector& GetBirdVisualRelativeLocation() const { return BirdVisualRelativeLocation; }

private:
	void ApplyBirdVisualTransform();
	void MoveForward(float Value);
	void MoveRight(float Value);

	/** Purely visual offset; Chaos applies it from the sphere support point and it never moves either collision body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Bird|Presentation|Transform", meta = (AllowPrivateAccess = "true", MakeEditWidget = "true"))
	FVector BirdVisualRelativeLocation = FVector::ZeroVector;

	/** Purely visual local orientation. Adjust this when the imported mesh faces a different forward axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Bird|Presentation|Transform", meta = (AllowPrivateAccess = "true"))
	FRotator BirdVisualRelativeRotation = FRotator(0.0f, -90.0f, 0.0f);

	/** Purely visual local scale. It never changes the collision size or physical mass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|Bird|Presentation|Transform", meta = (AllowPrivateAccess = "true", ClampMin = "0.01"))
	FVector BirdVisualRelativeScale = FVector(4.0f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> BirdVisual;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;
};
