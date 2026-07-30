// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABTSCalibrationTargetProxy.generated.h"

class UMaterialInterface;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Calibration marker. Range spheres stay non-blocking; the surface-resting
 * E5 cube blocks the bird while the rig's swept segment test remains the
 * high-speed hit/event authority.
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
	/** Configures the surface-resting temporary E5 building proxy. */
	void ConfigureCube(
		FName InTargetId,
		float InHalfExtentCM,
		const FLinearColor& InColor);
	void MarkHit();

	FName GetTargetId() const { return TargetId; }
	float GetTargetRadiusCM() const { return TargetRadiusCM; }
	FVector GetTargetHalfExtentCM() const { return FVector(TargetRadiusCM); }
	bool IsCubeTarget() const { return bCubeTarget; }

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

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereStaticMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeStaticMesh;

	UPROPERTY(VisibleInstanceOnly)
	FName TargetId = NAME_None;

	UPROPERTY(VisibleInstanceOnly)
	float TargetRadiusCM = 100.0f;

	UPROPERTY(VisibleInstanceOnly)
	FLinearColor TargetColor = FLinearColor::White;

	bool bCubeTarget = false;
	bool bWasHit = false;
};
