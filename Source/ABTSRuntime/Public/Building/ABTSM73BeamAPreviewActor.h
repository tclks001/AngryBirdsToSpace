// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSM73BeamAPreviewActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;

/** Editor-only Beam-A structural IR preview. Owns no physics or Load DAG. */
UCLASS(BlueprintType, Blueprintable,
	meta = (DisplayName = "M7.3 Beam-A Structural IR Preview"))
class ABTSRUNTIME_API AABTSM73BeamAPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73BeamAPreviewActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(CallInEditor, Category = "ABTS|M7.3-Beam-A")
	void RegeneratePreview();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-A",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-A",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralMeshComponent> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A")
	FABTSM73BeamAPreviewSettings PreviewSettings;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-A|Last Result")
	FABTSM73BeamAPreviewSummary LastPreviewSummary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	TObjectPtr<UMaterialInterface> PreviewMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation",
		meta = (ClampMin = "2.0", ClampMax = "150.0", Units = "cm"))
	float JointSizeCM = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	bool bShowJoints = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	FLinearColor XMemberColor = FLinearColor(0.90f, 0.18f, 0.10f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	FLinearColor YMemberColor = FLinearColor(0.12f, 0.75f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	FLinearColor ZMemberColor = FLinearColor(0.08f, 0.34f, 0.95f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-A|Presentation")
	FLinearColor JointColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.0f);

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
};
