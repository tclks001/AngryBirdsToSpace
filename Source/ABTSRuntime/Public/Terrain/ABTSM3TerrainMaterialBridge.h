// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ABTSM3TerrainMaterialBridge.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2D;
struct FABTSM2Cell;
struct FABTSM3CellState;
class FABTSM3TerrainVisualField;

/** Owns transient GPU lookup textures used by the hand-authored M3 SDF material. */
UCLASS()
class ABTSRUNTIME_API UABTSM3TerrainMaterialBridge : public UObject
{
	GENERATED_BODY()

public:
	bool Initialize(
		UProceduralMeshComponent* Surface,
		UMaterialInterface* SourceMaterial,
		const FVector& PlanetCenterWorld,
		float PlanetRadiusCM,
		float BlendWidthCM,
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		const FABTSM3TerrainVisualField& VisualField);

private:
	static UTexture2D* CreateFloatTexture(UObject* Outer, int32 Width, int32 Height, const TArray<FLinearColor>& Pixels, const TCHAR* Name);

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellDirectionLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellVisualLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BoundarySegmentLUT;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMID;
};

