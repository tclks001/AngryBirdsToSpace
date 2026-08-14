// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/ABTSCraftingTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSCraftingStation.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** M5 station query/click contract. M5.1 replaces placement and presentation. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSCraftingStation : public AActor
{
	GENERATED_BODY()

public:
	AABTSCraftingStation();

	EABTSCraftingStationType GetStationType() const { return StationType; }
	/** Used by the M5 runtime spawn owner before the station begins gameplay. */
	void SetStationType(EABTSCraftingStationType InStationType);
	void SetCellId(int32 InCellId) { CellId = InCellId; }
	int32 GetCellId() const { return CellId; }
	float GetUseRangeCM() const { return UseRangeCM; }
	bool IsWithinUseRange(const FVector& WorldLocation) const;
	virtual void NotifyActorOnClicked(FKey ButtonPressed) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M5|Station")
	TObjectPtr<UStaticMeshComponent> Visual;

	/** Hard asset references resolved on the class default object during construction. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> WorkbenchMeshAsset;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WorkbenchMaterialAsset;

	UPROPERTY()
	TObjectPtr<UStaticMesh> FurnaceMeshAsset;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FurnaceMaterialAsset;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5|Station")
	EABTSCraftingStationType StationType = EABTSCraftingStationType::Workbench;

	UPROPERTY(EditAnywhere, Category = "ABTS|M5|Station", meta = (ClampMin = "50.0", UIMax = "1500.0"))
	float UseRangeCM = 500.0f;

	int32 CellId = INDEX_NONE;
};
