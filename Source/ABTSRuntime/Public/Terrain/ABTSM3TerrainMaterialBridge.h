// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ABTSM3TerrainMaterialBridge.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture;
class UTexture2D;
struct FABTSM2Cell;
struct FABTSM3CellState;
struct FABTSM3CellEdgeState;
struct FABTSM3MonthlyCandidatePresentation;
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
		const FLinearColor& RoadColor,
		float TrailVisualHalfWidthCM,
		float MainRoadVisualHalfWidthCM,
		const FLinearColor& RiverColor,
		float StreamVisualHalfWidthCM,
		float ShallowRiverVisualHalfWidthCM,
		float DeepRiverVisualHalfWidthCM,
		const TArray<FABTSM2Cell>& Cells,
		const TArray<FABTSM3CellState>& CellStates,
		const TArray<FABTSM3CellEdgeState>& EdgeStates,
		const FABTSM3TerrainVisualField& VisualField,
		/** Preview identity is diagnostic-only; ground colors never consume monthly variants. */
		const FABTSM3MonthlyCandidatePresentation*
			MonthlyPresentation = nullptr);

	bool IsTerrainBasePaletteApplied() const
	{
		return bTerrainBasePaletteApplied;
	}

	int32 GetTerrainBasePaletteCellCount() const
	{
		return TerrainBasePaletteCellCount;
	}

	/**
	 * Updates the T3-A1 parameters on the existing terrain MID. Integration may
	 * call this repeatedly when the global runtime style switch changes.
	 * Missing material parameters fail soft and leave the pre-T3 surface active.
	 */
	bool ApplyStylizedSurfaceParameters(bool bStyleEnabled);

	bool IsStylizedSurfaceContractAvailable() const
	{
		return bStylizedSurfaceContractAvailable;
	}

	bool TryGetScalarParameterValue(
		const FName& ParameterName,
		float& OutValue) const;
	bool TryGetVectorParameterValue(
		const FName& ParameterName,
		FLinearColor& OutValue) const;
	bool TryGetTextureParameterValue(
		const FName& ParameterName,
		UTexture*& OutValue) const;
	const UMaterialInstanceDynamic* GetTerrainMIDForDiagnostics() const
	{
		return TerrainMID;
	}

private:
	static UTexture2D* CreateFloatTexture(UObject* Outer, int32 Width, int32 Height, const TArray<FLinearColor>& Pixels, const TCHAR* Name);

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellDirectionLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CellVisualLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BoundarySegmentLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RoadSegmentLUT;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RiverSegmentLUT;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TerrainMID;

	bool bStylizedSurfaceContractAvailable = false;
	bool bLastStyleEnabled = false;
	bool bHasAppliedStyleState = false;
	bool bTerrainBasePaletteApplied = false;
	int32 TerrainBasePaletteCellCount = 0;
};
