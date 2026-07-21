// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainMaterialBridge.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "Engine/Texture2D.h"

namespace
{
	constexpr int32 BoundarySlotsPerCell = 6;
	constexpr int32 BoundaryTexelsPerSlot = 2;
}

UTexture2D* UABTSM3TerrainMaterialBridge::CreateFloatTexture(
	UObject* Outer,
	const int32 Width,
	const int32 Height,
	const TArray<FLinearColor>& Pixels,
	const TCHAR* Name)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height) return nullptr;
	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_A32B32G32R32F, Name);
	if (Texture == nullptr) return nullptr;
	Texture->Rename(nullptr, Outer);
	Texture->Filter = TF_Nearest;
	Texture->SRGB = false;
	Texture->NeverStream = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (PlatformData == nullptr || PlatformData->Mips.IsEmpty()) return nullptr;
	float* Destination = static_cast<float*>(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	if (Destination == nullptr) return nullptr;
	for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
	{
		Destination[PixelIndex * 4 + 0] = Pixels[PixelIndex].R;
		Destination[PixelIndex * 4 + 1] = Pixels[PixelIndex].G;
		Destination[PixelIndex * 4 + 2] = Pixels[PixelIndex].B;
		Destination[PixelIndex * 4 + 3] = Pixels[PixelIndex].A;
	}
	PlatformData->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

bool UABTSM3TerrainMaterialBridge::Initialize(
	UProceduralMeshComponent* Surface,
	UMaterialInterface* SourceMaterial,
	const FVector& PlanetCenterWorld,
	const float PlanetRadiusCM,
	const float BlendWidthCM,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const FABTSM3TerrainVisualField& VisualField)
{
	if (Surface == nullptr || SourceMaterial == nullptr || Cells.IsEmpty() || Cells.Num() != CellStates.Num()) return false;
	TArray<FLinearColor> DirectionPixels;
	TArray<FLinearColor> VisualPixels;
	DirectionPixels.Reserve(Cells.Num());
	VisualPixels.Reserve(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FVector Direction = Cells[CellId].UnitCenter;
		DirectionPixels.Emplace(Direction.X, Direction.Y, Direction.Z, 1.0f);
		const FLinearColor Color = VisualField.GetDebugTerrainColor(Direction);
		VisualPixels.Emplace(Color.R, Color.G, Color.B, CellStates[CellId].LogicalHeight01);
	}

	const int32 BoundaryTextureWidth = BoundarySlotsPerCell * BoundaryTexelsPerSlot;
	TArray<FLinearColor> BoundaryPixels;
	BoundaryPixels.Init(FLinearColor(0, 0, 0, -1), BoundaryTextureWidth * Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const TArray<FABTSM3BoundarySegment>& Segments = VisualField.GetBoundarySegments(CellId);
		for (int32 Slot = 0; Slot < FMath::Min(BoundarySlotsPerCell, Segments.Num()); ++Slot)
		{
			const FABTSM3BoundarySegment& Segment = Segments[Slot];
			const int32 PixelIndex = CellId * BoundaryTextureWidth + Slot * BoundaryTexelsPerSlot;
			BoundaryPixels[PixelIndex + 0] = FLinearColor(Segment.StartUnit.X, Segment.StartUnit.Y, Segment.StartUnit.Z, 1.0f);
			BoundaryPixels[PixelIndex + 1] = FLinearColor(Segment.EndUnit.X, Segment.EndUnit.Y, Segment.EndUnit.Z, static_cast<float>(Segment.OtherCellId));
		}
	}

	CellDirectionLUT = CreateFloatTexture(this, Cells.Num(), 1, DirectionPixels, TEXT("ABTS_M3_CellDirectionLUT"));
	CellVisualLUT = CreateFloatTexture(this, Cells.Num(), 1, VisualPixels, TEXT("ABTS_M3_CellVisualLUT"));
	BoundarySegmentLUT = CreateFloatTexture(this, BoundaryTextureWidth, Cells.Num(), BoundaryPixels, TEXT("ABTS_M3_BoundarySegmentLUT"));
	if (!CellDirectionLUT || !CellVisualLUT || !BoundarySegmentLUT) return false;

	TerrainMID = UMaterialInstanceDynamic::Create(SourceMaterial, Surface);
	if (TerrainMID == nullptr) return false;
	TerrainMID->SetTextureParameterValue(TEXT("M3_CellDirectionLUT"), CellDirectionLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_CellVisualLUT"), CellVisualLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_BoundarySegmentLUT"), BoundarySegmentLUT);
	TerrainMID->SetVectorParameterValue(TEXT("M3_PlanetCenter"), FLinearColor(PlanetCenterWorld));
	TerrainMID->SetScalarParameterValue(TEXT("M3_CellCount"), Cells.Num());
	TerrainMID->SetScalarParameterValue(TEXT("M3_BoundarySlots"), BoundarySlotsPerCell);
	TerrainMID->SetScalarParameterValue(TEXT("M3_PlanetRadiusCM"), PlanetRadiusCM);
	TerrainMID->SetScalarParameterValue(TEXT("M3_BlendWidthCM"), BlendWidthCM);
	Surface->SetMaterial(0, TerrainMID);
	return true;
}

