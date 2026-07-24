// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73GroundAdapter.h"

#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "EngineUtils.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace
{
	FVector StableForward(const FVector& Candidate, const FVector& Up)
	{
		FVector Forward = FVector::VectorPlaneProject(Candidate, Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
		return Forward;
	}

	void AddUniqueSamplePoint(TArray<FVector2D>& Points, const FVector2D& Point)
	{
		if (!Points.ContainsByPredicate([&Point](const FVector2D& Existing) { return Existing.Equals(Point, 0.5f); }))
		{
			Points.Add(Point);
		}
	}
}

bool FABTSM73GroundAdapter::Resolve(
	AActor& Host,
	const EABTSM73GroundMode RequestedMode,
	const int32 RequestedAnchorCellId,
	const bool bSnapPlanarAnchorToStage,
	FABTSM73GroundContext& OutContext,
	FString& OutError) const
{
	OutContext = FABTSM73GroundContext();
	OutError.Reset();
	UWorld* World = Host.GetWorld();
	if (World == nullptr)
	{
		OutError = TEXT("NoWorld");
		return false;
	}

	AABTSM71PhysicsTestStage* Stage = nullptr;
	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM71PhysicsTestStage> It(World); It; ++It) { Stage = *It; break; }
	for (TActorIterator<AABTSM3Planet> It(World); It; ++It) { if (It->IsPlanetReady()) { Planet = *It; break; } }

	const bool bWantPlanar = RequestedMode == EABTSM73GroundMode::PlanarTestStage
		|| (RequestedMode == EABTSM73GroundMode::Auto && Stage != nullptr);
	if (bWantPlanar)
	{
		if (Stage == nullptr)
		{
			OutError = TEXT("PlanarStageMissing");
			return false;
		}
		const FVector Up = Stage->GetPlaneUp();
		const FVector PlaneOrigin = Stage->GetPlaneOrigin();
		// M7.1 is also a free placement/model test stage. By default the authored
		// Actor location defines a virtual local construction plane, so XYZ dragging
		// moves the preview, foundation and runtime structure as one object. Optional
		// snapping preserves the old floor-projection behavior for quick placement.
		const FVector Location = bSnapPlanarAnchorToStage
			? Host.GetActorLocation() - Up * FVector::DotProduct(Host.GetActorLocation() - PlaneOrigin, Up)
			: Host.GetActorLocation();
		const FVector Forward = StableForward(Host.GetActorForwardVector(), Up);
		OutContext.bValid = true;
		OutContext.bPlanar = true;
		OutContext.GravityUp = Up;
		OutContext.PlaneOrigin = PlaneOrigin;
		OutContext.AnchorTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), Location);
		OutContext.TestStage = Stage;
		return true;
	}

	if (Planet == nullptr)
	{
		OutError = TEXT("SphericalPlanetMissingOrNotReady");
		return false;
	}
	int32 AnchorCellId = RequestedAnchorCellId;
	FVector Direction = (Host.GetActorLocation() - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	if (Planet->LogicalCells.IsValidIndex(AnchorCellId)) Direction = Planet->LogicalCells[AnchorCellId].UnitCenter.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		OutError = TEXT("InvalidSphericalDirection");
		return false;
	}
	FVector SurfacePosition;
	FVector SurfaceNormal;
	float Radius = 0.0f;
	int32 ResolvedCellId = INDEX_NONE;
	if (!Planet->QuerySurface(Direction, SurfacePosition, SurfaceNormal, Radius, ResolvedCellId))
	{
		OutError = TEXT("AnchorSurfaceQueryFailed");
		return false;
	}
	AnchorCellId = Planet->LogicalCells.IsValidIndex(AnchorCellId) ? AnchorCellId : ResolvedCellId;
	const FVector Up = Direction;
	const FVector Forward = StableForward(Host.GetActorForwardVector(), Up);
	OutContext.bValid = true;
	OutContext.bPlanar = false;
	OutContext.GravityUp = Up;
	OutContext.AnchorTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), SurfacePosition);
	OutContext.Planet = Planet;
	OutContext.AnchorCellId = AnchorCellId;
	return true;
}

bool FABTSM73GroundAdapter::QueryGround(
	const FABTSM73GroundContext& Context,
	const FVector2D& LocalXY,
	FABTSM73GroundSample& OutSample)
{
	OutSample = FABTSM73GroundSample();
	OutSample.LocalXY = LocalXY;
	const FVector LocalPoint(LocalXY.X, LocalXY.Y, 0.0f);
	const FVector PlanePoint = Context.AnchorTransform.TransformPositionNoScale(LocalPoint);
	if (Context.bPlanar)
	{
		OutSample.WorldPosition = PlanePoint;
		OutSample.WorldNormal = Context.GravityUp;
		OutSample.LocalHeightCM = 0.0f;
		return true;
	}
	AABTSM3Planet* Planet = Context.Planet.Get();
	if (Planet == nullptr) return false;
	const FVector Direction = (PlanePoint - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	float Radius = 0.0f;
	if (!Planet->QuerySurface(Direction, OutSample.WorldPosition, OutSample.WorldNormal, Radius, OutSample.CellId)) return false;
	OutSample.LocalHeightCM = FVector::DotProduct(OutSample.WorldPosition - Context.AnchorTransform.GetLocation(), Context.GravityUp);
	const TArray<FABTSM3CellState>& States = Planet->GetGeneratedCellStates();
	OutSample.bBuildable = States.IsValidIndex(OutSample.CellId) && States[OutSample.CellId].bBuildable && !States[OutSample.CellId].bWater;
	return true;
}

bool FABTSM73GroundAdapter::AnalyzeFootprint(
	const FABTSM73GenerationSettings& Settings,
	const FABTSM73GroundContext& Context,
	FABTSM73StructureData& InOutData,
	FString& OutError) const
{
	OutError.Reset();
	if (!Context.bValid)
	{
		OutError = TEXT("InvalidGroundContext");
		return false;
	}
	const FVector2D Extent = InOutData.FootprintHalfExtent + FVector2D(FMath::Max(0.0f, Settings.FoundationMarginCM));
	const float Spacing = FMath::Max(20.0f, Settings.FootprintSampleSpacingCM);
	TArray<FVector2D> SamplePoints;
	for (float X = -Extent.X; X <= Extent.X + 0.5f; X += Spacing)
	{
		for (float Y = -Extent.Y; Y <= Extent.Y + 0.5f; Y += Spacing)
		{
			AddUniqueSamplePoint(SamplePoints, FVector2D(FMath::Min(X, Extent.X), FMath::Min(Y, Extent.Y)));
		}
	}
	const double AxisX[3] = {-Extent.X, 0.0, Extent.X};
	const double AxisY[3] = {-Extent.Y, 0.0, Extent.Y};
	for (const double X : AxisX)
	{
		for (const double Y : AxisY) AddUniqueSamplePoint(SamplePoints, FVector2D(X, Y));
	}
	for (const FVector2D& Support : InOutData.GroundSupportPoints) AddUniqueSamplePoint(SamplePoints, Support);

	float MinHeight = BIG_NUMBER;
	float MaxHeight = -BIG_NUMBER;
	float MaxSlope = 0.0f;
	TSet<int32> CoveredCells;
	InOutData.GroundSamples.Reset();
	for (const FVector2D& Point : SamplePoints)
	{
		FABTSM73GroundSample Sample;
		if (!QueryGround(Context, Point, Sample))
		{
			OutError = TEXT("FootprintSurfaceQueryFailed");
			return false;
		}
		if (!Context.bPlanar && !Sample.bBuildable)
		{
			OutError = FString::Printf(TEXT("FootprintCellNotBuildable:%d"), Sample.CellId);
			return false;
		}
		MinHeight = FMath::Min(MinHeight, Sample.LocalHeightCM);
		MaxHeight = FMath::Max(MaxHeight, Sample.LocalHeightCM);
		MaxSlope = FMath::Max(MaxSlope, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Sample.WorldNormal.GetSafeNormal(), Context.GravityUp), -1.0f, 1.0f))));
		if (Sample.CellId != INDEX_NONE) CoveredCells.Add(Sample.CellId);
		InOutData.GroundSamples.Add(Sample);
	}

	InOutData.TerrainDeltaCM = FMath::Max(0.0f, MaxHeight - MinHeight);
	InOutData.MaxSlopeDegrees = MaxSlope;
	if (!Context.bPlanar)
	{
		const AABTSM3Planet* Planet = Context.Planet.Get();
		const float R = Planet ? FMath::Max(1.0f, Planet->GetPlanetRadiusCM()) : 1.0f;
		const float Rho = Extent.Size();
		InOutData.CurvatureDropCM = R - FMath::Sqrt(FMath::Max(0.0f, R * R - FMath::Square(FMath::Min(Rho, R))));
		const float AngularSpan = FMath::RadiansToDegrees(2.0f * FMath::Atan2(Rho, R));
		if (AngularSpan > Settings.MaxSinglePlatformAngularSpanDegrees)
		{
			OutError = FString::Printf(TEXT("AngularSpanTooLarge:%.2f"), AngularSpan);
			return false;
		}
	}
	if (InOutData.TerrainDeltaCM > Settings.MaxTerrainDeltaCM)
	{
		OutError = FString::Printf(TEXT("TerrainDeltaTooLarge:%.2f"), InOutData.TerrainDeltaCM);
		return false;
	}
	if (MaxSlope > Settings.MaxBuildingPadSlopeDegrees)
	{
		OutError = FString::Printf(TEXT("SlopeTooLarge:%.2f"), MaxSlope);
		return false;
	}

	InOutData.FoundationCapBottomCM = MaxHeight + FMath::Max(0.0f, Settings.FoundationTopClearanceCM);
	InOutData.FoundationCapTopCM = InOutData.FoundationCapBottomCM + FMath::Max(10.0f, Settings.FoundationCapThicknessCM);
	InOutData.FoundationFeet.Reset();
	TArray<FVector2D> FootPoints = InOutData.GroundSupportPoints;
	for (const FVector2D& Corner : {
		FVector2D(-Extent.X, -Extent.Y), FVector2D(-Extent.X, Extent.Y),
		FVector2D(Extent.X, -Extent.Y), FVector2D(Extent.X, Extent.Y)}) AddUniqueSamplePoint(FootPoints, Corner);
	for (const FVector2D& FootPoint : FootPoints)
	{
		FABTSM73GroundSample Sample;
		if (!QueryGround(Context, FootPoint, Sample)) continue;
		FABTSM73FoundationFoot& Foot = InOutData.FoundationFeet.AddDefaulted_GetRef();
		Foot.LocalXY = FootPoint;
		Foot.GroundHeightCM = Sample.LocalHeightCM;
		Foot.BottomHeightCM = Sample.LocalHeightCM - FMath::Max(0.0f, Settings.FoundationEmbedDepthCM);
		Foot.TopHeightCM = InOutData.FoundationCapBottomCM;
		InOutData.MaxFoundationDepthCM = FMath::Max(InOutData.MaxFoundationDepthCM, Foot.TopHeightCM - Foot.BottomHeightCM);
	}
	if (InOutData.MaxFoundationDepthCM > Settings.MaxFoundationDepthCM)
	{
		OutError = FString::Printf(TEXT("FoundationDepthTooLarge:%.2f"), InOutData.MaxFoundationDepthCM);
		return false;
	}
	return true;
}
