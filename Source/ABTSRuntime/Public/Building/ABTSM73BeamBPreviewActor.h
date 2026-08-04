// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamBPreviewTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSM73BeamBPreviewActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/** Editor-only Beam-B Motif WFC and graph-grammar preview. */
UCLASS(BlueprintType, Blueprintable,
	meta = (DisplayName = "M7.3 Beam-B Motif WFC Preview"))
class ABTSRUNTIME_API AABTSM73BeamBPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73BeamBPreviewActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(CallInEditor, Category = "ABTS|M7.3-Beam-B")
	void RegeneratePreview();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-B",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-B",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralMeshComponent> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-B")
	FABTSM73BeamBPreviewSettings PreviewSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-B|Last Result")
	FABTSM73BeamBPreviewSummary LastPreviewSummary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-B|Presentation")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-B|Presentation")
	TArray<FLinearColor> MotifColors;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
};
