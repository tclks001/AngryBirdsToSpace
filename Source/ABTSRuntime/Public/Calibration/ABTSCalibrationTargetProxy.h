// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABTSCalibrationTargetProxy.generated.h"

class UMaterialInterface;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Non-blocking calibration marker. Runtime hit authority remains the rig's
 * swept segment test, so high-speed birds cannot tunnel through this proxy.
 */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSCalibrationTargetProxy : public AActor
{
	GENERATED_BODY()

public:
	AABTSCalibrationTargetProxy();
	virtual void BeginPlay() override;

	void Configure(
		FName InTargetId,
		float InRadiusCM,
		const FLinearColor& InColor);
	void MarkHit();

	FName GetTargetId() const { return TargetId; }
	float GetTargetRadiusCM() const { return TargetRadiusCM; }

private:
	void RefreshPresentation();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> QuerySphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(VisibleInstanceOnly)
	FName TargetId = NAME_None;

	UPROPERTY(VisibleInstanceOnly)
	float TargetRadiusCM = 100.0f;

	UPROPERTY(VisibleInstanceOnly)
	FLinearColor TargetColor = FLinearColor::White;

	bool bWasHit = false;
};
