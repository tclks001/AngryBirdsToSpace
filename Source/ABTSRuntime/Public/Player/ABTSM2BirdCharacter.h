// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/ABTSM1BirdCharacter.h"
#include "ABTSM2BirdCharacter.generated.h"

class UABTSM2SphericalSurfaceComponent;

/** M2 character: tangent input, pole-safe camera basis, and radial down alignment without physical gravity. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM2BirdCharacter : public AABTSM1BirdCharacter
{
	GENERATED_BODY()

public:
	AABTSM2BirdCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	void MoveOnSphereForward(float Value);
	void MoveOnSphereRight(float Value);
	void TurnOnSphere(float Value);
	void LookOnSphere(float Value);
	UABTSM2SphericalSurfaceComponent* GetSphericalSurface() const { return SphericalSurface; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M2", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UABTSM2SphericalSurfaceComponent> SphericalSurface;
};
