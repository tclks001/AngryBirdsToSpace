// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainMaterialBridge.h"

#include "ABTSRuntime.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "ProceduralMeshComponent.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "Terrain/ABTSM3RiverVisualBuilder.h"
#include "Terrain/ABTSM3TerrainFeatureVisualBuilder.h"
#include "Engine/Texture2D.h"

namespace ABTSM3TerrainMaterialBridgePrivate
{
	constexpr int32 BoundarySlotsPerCell = 32;
	constexpr int32 BoundaryTexelsPerSlot = 2;
	constexpr int32 RoadSlotsPerCell = 16;
	constexpr int32 RoadTexelsPerSlot = 2;
	constexpr int32 RiverSlotsPerCell = 24;
	constexpr int32 RiverTexelsPerSlot = 2;

	bool HasScalarParameter(
		const UMaterialInterface& Material,
		const FName& ParameterName)
	{
		float Value = 0.0f;
		return Material.GetScalarParameterValue(
			FHashedMaterialParameterInfo(ParameterName),
			Value);
	}

	bool HasVectorParameter(
		const UMaterialInterface& Material,
		const FName& ParameterName)
	{
		FLinearColor Value = FLinearColor::Black;
		return Material.GetVectorParameterValue(
			FHashedMaterialParameterInfo(ParameterName),
			Value);
	}

	bool HasStylizedSurfaceParameters(const UMaterialInterface& Material)
	{
		return HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetStyleEnabledParameterName())
			&& HasVectorParameter(
				Material,
				FABTSStylizedMaterialContract::GetBaseColorTintParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetRoughnessFloorParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetRoughnessScaleParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetSpecularScaleParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetMetallicScaleParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetRimStrengthParameterName())
			&& HasScalarParameter(
				Material,
				FABTSStylizedMaterialContract::GetRimPowerParameterName());
	}
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
	#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TMGS_NoMipmaps;
	#endif
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
	const FLinearColor& RoadColor,
	const float TrailVisualHalfWidthCM,
	const float MainRoadVisualHalfWidthCM,
	const FLinearColor& RiverColor,
	const float StreamVisualHalfWidthCM,
	const float ShallowRiverVisualHalfWidthCM,
	const float DeepRiverVisualHalfWidthCM,
	const TArray<FABTSM2Cell>& Cells,
	const TArray<FABTSM3CellState>& CellStates,
	const TArray<FABTSM3CellEdgeState>& EdgeStates,
	const FABTSM3TerrainVisualField& VisualField,
	const FABTSM3MonthlyCandidatePresentation*
		MonthlyPresentation)
{
	using namespace ABTSM3TerrainMaterialBridgePrivate;

	if (Surface == nullptr || SourceMaterial == nullptr || Cells.IsEmpty() || Cells.Num() != CellStates.Num()) return false;
	bTerrainBasePaletteApplied = false;
	TerrainBasePaletteCellCount = 0;
	TArray<FLinearColor> DirectionPixels;
	TArray<FLinearColor> VisualPixels;
	DirectionPixels.Reserve(Cells.Num());
	VisualPixels.Reserve(Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		const FVector Direction = Cells[CellId].UnitCenter;
		DirectionPixels.Emplace(Direction.X, Direction.Y, Direction.Z, 1.0f);
		const FLinearColor Color =
			VisualField.GetCellBaseLandColor(CellId);
		// Road and river masks are independent segment SDFs. Cell flags remain
		// gameplay caches and must not fill whole Voronoi cells in the material.
		VisualPixels.Emplace(Color.R, Color.G, Color.B, 0.0f);
	}
	bTerrainBasePaletteApplied = true;
	TerrainBasePaletteCellCount = VisualPixels.Num();

	TArray<FABTSM3RiverVisualSegment> RoadSegments;
	FABTSM3RiverVisualBuilder::BuildRoadSegments(Cells, EdgeStates, TrailVisualHalfWidthCM, MainRoadVisualHalfWidthCM, RoadSegments);
	TArray<TArray<int32>> RoadSegmentIndicesByCell;
	int32 DroppedRoadReferences = 0;
	FABTSM3RiverVisualBuilder::BuildLocalSegmentIndices(Cells, RoadSegments, PlanetRadiusCM, BlendWidthCM, RoadSlotsPerCell, RoadSegmentIndicesByCell, DroppedRoadReferences);
	const int32 RoadTextureWidth = RoadSlotsPerCell * RoadTexelsPerSlot;
	TArray<FLinearColor> RoadPixels;
	RoadPixels.Init(FLinearColor::Transparent, RoadTextureWidth * Cells.Num());
	for (int32 CellId = 0; CellId < RoadSegmentIndicesByCell.Num(); ++CellId)
	{
		for (int32 Slot = 0; Slot < RoadSegmentIndicesByCell[CellId].Num(); ++Slot)
		{
			const FABTSM3RiverVisualSegment& Segment = RoadSegments[RoadSegmentIndicesByCell[CellId][Slot]];
			const int32 PixelIndex = CellId * RoadTextureWidth + Slot * RoadTexelsPerSlot;
			RoadPixels[PixelIndex] = FLinearColor(Segment.StartUnit.X, Segment.StartUnit.Y, Segment.StartUnit.Z, Segment.HalfWidthCM);
			RoadPixels[PixelIndex + 1] = FLinearColor(Segment.EndUnit.X, Segment.EndUnit.Y, Segment.EndUnit.Z, static_cast<float>(Segment.TransportType));
		}
	}

	TArray<FABTSM3RiverVisualSegment> RiverSegments;
	FABTSM3RiverVisualBuilder::BuildSegments(Cells, EdgeStates, StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM, RiverSegments);
	int32 FlowCenterlineSegments = 0;
	int32 BarrierDualSegments = 0;
	int32 SmoothedBarrierSegments = 0;
	for (const FABTSM3RiverVisualSegment& Segment : RiverSegments)
	{
		SmoothedBarrierSegments += Segment.bBarrierCenterlineProjected ? 1 : 0;
	}
	for (const FABTSM3CellEdgeState& Edge : EdgeStates)
	{
		if (Edge.Water == EABTSM3WaterEdgeType::None) continue;
		if (Edge.DownstreamCellId != INDEX_NONE && !Edge.bBlocksOnFoot) ++FlowCenterlineSegments;
		else ++BarrierDualSegments;
	}
	TArray<TArray<int32>> RiverSegmentIndicesByCell;
	int32 DroppedRiverReferences = 0;
	FABTSM3RiverVisualBuilder::BuildLocalSegmentIndices(Cells, RiverSegments, PlanetRadiusCM, BlendWidthCM, RiverSlotsPerCell, RiverSegmentIndicesByCell, DroppedRiverReferences);
	const int32 RiverTextureWidth = RiverSlotsPerCell * RiverTexelsPerSlot;
	TArray<FLinearColor> RiverPixels;
	RiverPixels.Init(FLinearColor::Transparent, RiverTextureWidth * Cells.Num());
	for (int32 CellId = 0; CellId < RiverSegmentIndicesByCell.Num(); ++CellId)
	{
		for (int32 Slot = 0; Slot < RiverSegmentIndicesByCell[CellId].Num(); ++Slot)
		{
			const FABTSM3RiverVisualSegment& Segment = RiverSegments[RiverSegmentIndicesByCell[CellId][Slot]];
			const int32 PixelIndex = CellId * RiverTextureWidth + Slot * RiverTexelsPerSlot;
			RiverPixels[PixelIndex] = FLinearColor(Segment.StartUnit.X, Segment.StartUnit.Y, Segment.StartUnit.Z, Segment.HalfWidthCM);
			RiverPixels[PixelIndex + 1] = FLinearColor(Segment.EndUnit.X, Segment.EndUnit.Y, Segment.EndUnit.Z, static_cast<float>(Segment.WaterType));
		}
	}

	TArray<FABTSM3TerrainFeatureVisualSegment> TerrainFeatures;
	FABTSM3TerrainFeatureVisualBuilder::BuildSegments(Cells, CellStates, TerrainFeatures);
	TArray<TArray<int32>> TerrainBoundaryIndicesByCell;
	int32 PrunedTerrainReferences = 0;
	FABTSM3TerrainFeatureVisualBuilder::BuildLocalSegmentIndices(Cells, TerrainFeatures, 3, BoundarySlotsPerCell, PlanetRadiusCM, TerrainBoundaryIndicesByCell, PrunedTerrainReferences);
	const int32 BoundaryTextureWidth = BoundarySlotsPerCell * BoundaryTexelsPerSlot;
	TArray<FLinearColor> BoundaryPixels;
	BoundaryPixels.Init(FLinearColor(0, 0, 0, -1), BoundaryTextureWidth * Cells.Num());
	for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
	{
		for (int32 Slot = 0; Slot < TerrainBoundaryIndicesByCell[CellId].Num(); ++Slot)
		{
			const FABTSM3TerrainFeatureVisualSegment& Segment = TerrainFeatures[TerrainBoundaryIndicesByCell[CellId][Slot]];
			const int32 PixelIndex = CellId * BoundaryTextureWidth + Slot * BoundaryTexelsPerSlot;
			BoundaryPixels[PixelIndex + 0] = FLinearColor(Segment.StartUnit.X, Segment.StartUnit.Y, Segment.StartUnit.Z, static_cast<float>(Segment.TerrainType) + 1.0f);
			BoundaryPixels[PixelIndex + 1] = FLinearColor(Segment.EndUnit.X, Segment.EndUnit.Y, Segment.EndUnit.Z, static_cast<float>(Segment.RepresentativeCellId));
		}
	}

	CellDirectionLUT = CreateFloatTexture(this, Cells.Num(), 1, DirectionPixels, TEXT("ABTS_M3_CellDirectionLUT"));
	CellVisualLUT = CreateFloatTexture(this, Cells.Num(), 1, VisualPixels, TEXT("ABTS_M3_CellVisualLUT"));
	BoundarySegmentLUT = CreateFloatTexture(this, BoundaryTextureWidth, Cells.Num(), BoundaryPixels, TEXT("ABTS_M3_BoundarySegmentLUT"));
	RoadSegmentLUT = CreateFloatTexture(this, RoadTextureWidth, Cells.Num(), RoadPixels, TEXT("ABTS_M3_RoadSegmentLUT"));
	RiverSegmentLUT = CreateFloatTexture(this, RiverTextureWidth, Cells.Num(), RiverPixels, TEXT("ABTS_M3_RiverSegmentLUT"));
	if (!CellDirectionLUT || !CellVisualLUT || !BoundarySegmentLUT || !RoadSegmentLUT || !RiverSegmentLUT) return false;

	TerrainMID = UMaterialInstanceDynamic::Create(SourceMaterial, Surface);
	if (TerrainMID == nullptr) return false;
	bStylizedSurfaceContractAvailable =
		HasStylizedSurfaceParameters(*SourceMaterial);
	bHasAppliedStyleState = false;
	TerrainMID->SetTextureParameterValue(TEXT("M3_CellDirectionLUT"), CellDirectionLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_CellVisualLUT"), CellVisualLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_BoundarySegmentLUT"), BoundarySegmentLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_RoadSegmentLUT"), RoadSegmentLUT);
	TerrainMID->SetTextureParameterValue(TEXT("M3_RiverSegmentLUT"), RiverSegmentLUT);
	TerrainMID->SetVectorParameterValue(TEXT("M3_PlanetCenter"), FLinearColor(PlanetCenterWorld));
	TerrainMID->SetScalarParameterValue(TEXT("M3_CellCount"), Cells.Num());
	TerrainMID->SetScalarParameterValue(TEXT("M3_BoundarySlots"), BoundarySlotsPerCell);
	TerrainMID->SetScalarParameterValue(TEXT("M3_RoadSegmentCount"), RoadSlotsPerCell);
	TerrainMID->SetScalarParameterValue(TEXT("M3_PlanetRadiusCM"), PlanetRadiusCM);
	TerrainMID->SetScalarParameterValue(TEXT("M3_BlendWidthCM"), BlendWidthCM);
	TerrainMID->SetVectorParameterValue(TEXT("M3_RoadColor"), RoadColor);
	TerrainMID->SetVectorParameterValue(TEXT("M3_RiverColor"), RiverColor);
	TerrainMID->SetScalarParameterValue(TEXT("M3_RiverSegmentCount"), RiverSlotsPerCell);
	const bool bStylizedParametersApplied = ApplyStylizedSurfaceParameters(
		FABTSStylizedRenderingControl::IsEnabled());
	if (!bStylizedParametersApplied)
	{
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M3][T3-A1] SurfaceStyleUnavailable Material=%s Family=M3Surface Adoption=InPlaceStyleParameter OriginalSurfacePreserved=1 PlanetReadyBlocked=0"),
			*GetNameSafe(SourceMaterial));
	}
	Surface->SetMaterial(0, TerrainMID);
	UE_LOG(LogTemp, Log, TEXT("[ABTS][M3][RiverSDF] Segments=%d FlowCenterlines=%d BarrierDuals=%d SmoothedBarrierSegments=%d BarrierSmoothingVersion=%d TextureWidth=%d DroppedLocalRefs=%d StreamHalfWidth=%.1f ShallowHalfWidth=%.1f DeepHalfWidth=%.1f"),
		RiverSegments.Num(), FlowCenterlineSegments, BarrierDualSegments, SmoothedBarrierSegments, FABTSM3RiverVisualBuilder::BarrierSmoothingVersion, RiverTextureWidth, DroppedRiverReferences,
		StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM);
	UE_LOG(LogTemp, Log, TEXT("[ABTS][M3][LinearSDF] RoadSegments=%d TerrainFeatures=%d RoadTextureWidth=%d TerrainTextureWidth=%d DroppedRoadRefs=%d PrunedTerrainRefs=%d TerrainRings=3 TerrainSlots=32"),
		RoadSegments.Num(), TerrainFeatures.Num(), RoadTextureWidth, BoundaryTextureWidth,
		DroppedRoadReferences, PrunedTerrainReferences);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5][MaterialBasePalette] Applied=%d Cells=%d VisualBeatConsumed=0 ThemeVariantConsumed=0 PreviewAuthority=%d MonthlyAccepted=0"),
		bTerrainBasePaletteApplied ? 1 : 0,
		TerrainBasePaletteCellCount,
		MonthlyPresentation != nullptr ? 1 : 0);
	return true;
}

bool UABTSM3TerrainMaterialBridge::ApplyStylizedSurfaceParameters(
	const bool bStyleEnabled)
{
	if (TerrainMID == nullptr
		|| !bStylizedSurfaceContractAvailable
		|| FABTSStylizedMaterialContract::ResolveOwner(
			EABTSStylizedMaterialFamily::M3Surface)
			!= EABTSStylizedMaterialOwner::M3
		|| FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::M3Surface)
			!= EABTSStylizedMaterialAdoptionMode::InPlaceStyleParameter)
	{
		return false;
	}

	const FABTSStylizedSurfaceParameters Parameters =
		FABTSStylizedMaterialContract::ResolveDefaultParameters(
			EABTSStylizedMaterialFamily::M3Surface);
	if (!Parameters.IsValid())
	{
		return false;
	}

	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		bStyleEnabled ? 1.0f : 0.0f);
	TerrainMID->SetVectorParameterValue(
		FABTSStylizedMaterialContract::GetBaseColorTintParameterName(),
		Parameters.BaseColorTint);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
		Parameters.RoughnessFloor);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
		Parameters.RoughnessScale);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
		Parameters.SpecularScale);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
		Parameters.MetallicScale);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
		Parameters.RimStrength);
	TerrainMID->SetScalarParameterValue(
		FABTSStylizedMaterialContract::GetRimPowerParameterName(),
		Parameters.RimPower);

	if (!bHasAppliedStyleState || bLastStyleEnabled != bStyleEnabled)
	{
		bLastStyleEnabled = bStyleEnabled;
		bHasAppliedStyleState = true;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M3][T3-A1] SurfaceStyle=%d Family=M3Surface Adoption=InPlaceStyleParameter MID=%s RoughnessFloor=%.3f RoughnessScale=%.3f SpecularScale=%.3f MetallicScale=%.3f"),
			bStyleEnabled ? 1 : 0,
			*GetNameSafe(TerrainMID),
			Parameters.RoughnessFloor,
			Parameters.RoughnessScale,
			Parameters.SpecularScale,
			Parameters.MetallicScale);
	}
	return true;
}

bool UABTSM3TerrainMaterialBridge::TryGetScalarParameterValue(
	const FName& ParameterName,
	float& OutValue) const
{
	return TerrainMID != nullptr
		&& TerrainMID->GetScalarParameterValue(
			FHashedMaterialParameterInfo(ParameterName),
			OutValue,
			true);
}

bool UABTSM3TerrainMaterialBridge::TryGetVectorParameterValue(
	const FName& ParameterName,
	FLinearColor& OutValue) const
{
	return TerrainMID != nullptr
		&& TerrainMID->GetVectorParameterValue(
			FHashedMaterialParameterInfo(ParameterName),
			OutValue,
			true);
}

bool UABTSM3TerrainMaterialBridge::TryGetTextureParameterValue(
	const FName& ParameterName,
	UTexture*& OutValue) const
{
	return TerrainMID != nullptr
		&& TerrainMID->GetTextureParameterValue(
			FHashedMaterialParameterInfo(ParameterName),
			OutValue,
			true);
}
