// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamCPreviewTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSM73BeamCPreviewActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/** Editor-only Beam-C Load DAG and static proxy preview. */
UCLASS(BlueprintType, Blueprintable,
	meta = (DisplayName = "M7.3 Beam-C Load DAG Preview"))
class ABTSRUNTIME_API AABTSM73BeamCPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73BeamCPreviewActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(CallInEditor, Category = "ABTS|M7.3-Beam-C")
	void RegeneratePreview();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-C",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-C",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralMeshComponent> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-C")
	FABTSM73BeamCPreviewSettings PreviewSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-C|Last Result")
	FABTSM73BeamCPreviewSummary LastPreviewSummary;

	/** Draw thin resultant-to-bearing segments. Disabled by default for readability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-C|Presentation")
	bool bShowLoadPaths = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-C|Presentation")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

	/** Ground, low, medium-low, medium, high, limit, and load-path colors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-C|Presentation")
	TArray<FLinearColor> UtilizationColors;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
};
