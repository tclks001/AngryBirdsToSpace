// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73DAG5BShapePreviewTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSM73DAG5BShapePreviewActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/**
 * Editor-only visual prototype for DAG5-B v2.
 * It intentionally owns no physical building, collision, DAG2.3 or Chaos state.
 */
UCLASS(BlueprintType, Blueprintable,
	meta = (DisplayName = "M7.3 DAG5-B v2 Complex Silhouette Preview"))
class ABTSRUNTIME_API AABTSM73DAG5BShapePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73DAG5BShapePreviewActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(CallInEditor, Category = "ABTS|M7.3-DAG5B v2")
	void RegeneratePreview();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-DAG5B v2",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-DAG5B v2",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralMeshComponent> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2")
	FABTSM73DAG5BV2PreviewSettings PreviewSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-DAG5B v2|Last Result")
	FABTSM73DAG5BV2PreviewSummary LastPreviewSummary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2|Presentation")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2|Presentation")
	FLinearColor BoxColor =
		FLinearColor(0.08f, 0.34f, 0.90f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2|Presentation")
	FLinearColor PrismColor =
		FLinearColor(0.05f, 0.78f, 0.58f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2|Presentation")
	FLinearColor PyramidColor =
		FLinearColor(0.92f, 0.22f, 0.36f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-DAG5B v2|Presentation")
	bool bCastPreviewShadow = true;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
};
