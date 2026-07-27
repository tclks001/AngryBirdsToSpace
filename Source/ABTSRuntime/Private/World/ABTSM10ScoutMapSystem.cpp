// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM10ScoutMapSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"

AABTSM10ScoutMapSystem::AABTSM10ScoutMapSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	// M6 publishes/revokes its trajectory snapshot in PostPhysics. Sampling it
	// afterwards prevents a one-frame stale landing preview on release.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void AABTSM10ScoutMapSystem::Configure(const FABTSM10ScoutMapSettings& InSettings)
{
	Settings = InSettings;
}

void AABTSM10ScoutMapSystem::BeginPlay()
{
	Super::BeginPlay();
	ResolveDependencies();
}

void AABTSM10ScoutMapSystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SlingshotSystem.IsValid())
	{
		SlingshotSystem->OnLaunchCompleted().RemoveAll(this);
		if (bBoundToSlingshot)
		{
			RemoveTickPrerequisiteActor(SlingshotSystem.Get());
		}
	}
	bBoundToSlingshot = false;
	if (LandingPreviewCamera)
	{
		// The preview is a runtime child spawned by this system; destroy it here
		// instead of leaving an orphan if the system itself is removed in-game.
		LandingPreviewCamera->DeactivatePreview();
		LandingPreviewCamera->Destroy();
		LandingPreviewCamera = nullptr;
	}
	ClearOrbitalOverview(false);
	Super::EndPlay(EndPlayReason);
}

bool AABTSM10ScoutMapSystem::CopyCurrentTrajectoryPreview(
	FABTSM6TrajectoryPreview& OutPreview) const
{
	return bScoutMapRevealed
		&& SlingshotSystem.IsValid()
		&& SlingshotSystem->CopyCurrentTrajectoryPreview(OutPreview);
}

bool AABTSM10ScoutMapSystem::TryGetQualifiedReinforcedLandingPreview(
	FABTSM6TrajectoryPreview& OutPreview) const
{
	FVector2D LandingMapPosition;
	return CopyCurrentTrajectoryPreview(OutPreview)
		&& OutPreview.SlingshotTier == EABTSSlingshotTier::Reinforced
		&& OutPreview.bHasPrimarySurfaceLanding
		&& ProjectWorldLocation(OutPreview.PrimarySurfaceLandingWorld, LandingMapPosition);
}

bool AABTSM10ScoutMapSystem::IsLandingPreviewActive() const
{
	return LandingPreviewCamera != nullptr && LandingPreviewCamera->IsPreviewActive();
}

UTextureRenderTarget2D* AABTSM10ScoutMapSystem::GetLandingPreviewRenderTarget() const
{
	return LandingPreviewCamera ? LandingPreviewCamera->GetRenderTarget() : nullptr;
}

void AABTSM10ScoutMapSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBoundToSlingshot || !SlingshotSystem.IsValid() || !Planet.IsValid())
	{
		DependencyResolveAccumulatorSeconds += DeltaSeconds;
		if (DependencyResolveAccumulatorSeconds >= 0.5f)
		{
			DependencyResolveAccumulatorSeconds = 0.0f;
			ResolveDependencies();
		}
	}
	else
	{
		DependencyResolveAccumulatorSeconds = 0.0f;
	}
	UpdateLandingPreview(DeltaSeconds);
	UpdateOrbitalOverview();
	if (!bScoutMapRevealed) return;

	EnvironmentRefreshAccumulatorSeconds += DeltaSeconds;
	const float RefreshInterval = FMath::Clamp(Settings.EnvironmentRefreshIntervalSeconds, 0.02f, 2.0f);
	if (EnvironmentRefreshAccumulatorSeconds >= RefreshInterval)
	{
		EnvironmentRefreshAccumulatorSeconds = FMath::Fmod(EnvironmentRefreshAccumulatorSeconds, RefreshInterval);
		RefreshEnvironmentMarkers();
	}
}

void AABTSM10ScoutMapSystem::EnsureLandingPreviewCamera()
{
	if (LandingPreviewCamera != nullptr || GetWorld() == nullptr) return;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	LandingPreviewCamera = GetWorld()->SpawnActor<AABTSM101LandingPreviewCamera>(
		AABTSM101LandingPreviewCamera::StaticClass(), FTransform::Identity, SpawnParameters);
	if (LandingPreviewCamera)
	{
		LandingPreviewCamera->Configure(Settings);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M10.1][LandingPreview] Camera spawned=%s"),
			*LandingPreviewCamera->GetName());
	}
}

void AABTSM10ScoutMapSystem::UpdateLandingPreview(const float DeltaSeconds)
{
	if (!Settings.bShowReinforcedLandingPreview || !bScoutMapRevealed || !Planet.IsValid())
	{
		if (LandingPreviewCamera) LandingPreviewCamera->DeactivatePreview();
		return;
	}

	FABTSM6TrajectoryPreview Preview;
	if (!TryGetQualifiedReinforcedLandingPreview(Preview))
	{
		if (LandingPreviewCamera) LandingPreviewCamera->DeactivatePreview();
		return;
	}

	EnsureLandingPreviewCamera();
	if (LandingPreviewCamera)
	{
		LandingPreviewCamera->UpdatePreview(Preview, *Planet.Get(), DeltaSeconds);
	}
}

bool AABTSM10ScoutMapSystem::ResolveDependencies()
{
	if (!Planet.IsValid())
	{
		for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
		{
			if (It->IsPlanetReady())
			{
				Planet = *It;
				break;
			}
		}
	}

	if (!SlingshotSystem.IsValid())
	{
		// Weak-pointer invalidation means the prior actor is already gone; do not
		// pass a null prerequisite through the actor tick API.
		bBoundToSlingshot = false;
		for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
		{
			SlingshotSystem = *It;
			break;
		}
	}
	if (SlingshotSystem.IsValid() && !bBoundToSlingshot)
	{
		SlingshotSystem->OnLaunchCompleted().AddUObject(this, &AABTSM10ScoutMapSystem::HandleLaunchCompleted);
		if (PrimaryActorTick.IsTickFunctionEnabled() && SlingshotSystem->PrimaryActorTick.IsTickFunctionEnabled())
		{
			AddTickPrerequisiteActor(SlingshotSystem.Get());
		}
		bBoundToSlingshot = true;
	}
	return Planet.IsValid() && bBoundToSlingshot;
}

void AABTSM10ScoutMapSystem::HandleLaunchCompleted(
	const EABTSBirdId BirdId,
	const FVector& LandingLocation)
{
	if (BirdId != EABTSBirdId::Blue) return;
	if (!RevealAtLanding(LandingLocation))
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M10][Reveal] Failed: primary planet or terrain texture was unavailable."));
	}
}

bool AABTSM10ScoutMapSystem::RevealAtLanding(const FVector& LandingLocation)
{
	if (!Planet.IsValid() && !ResolveDependencies()) return false;
	const FVector CenterOffset = LandingLocation - Planet->GetPlanetCenterWorld();
	if (CenterOffset.IsNearlyZero()) return false;

	const FVector CandidateCenterUnit = CenterOffset.GetSafeNormal();
	FVector CandidateEastUnit;
	FVector CandidateNorthUnit;
	BuildFixedMapFrame(CandidateCenterUnit, CandidateEastUnit, CandidateNorthUnit);
	const float PlanetRadiusCM = FMath::Max(1.0f, Planet->GetPlanetRadiusCM());
	const float RatioRadiusCM = PlanetRadiusCM * FMath::Clamp(Settings.ScoutRadiusPrimaryRatio, 0.01f, 3.0f);
	float CandidateScoutRadiusCM = Settings.ScoutRadiusOverrideCM > 0.0f
		? Settings.ScoutRadiusOverrideCM
		: RatioRadiusCM;
	// A single azimuthal disc cannot represent the antipode without ambiguity.
	CandidateScoutRadiusCM = FMath::Clamp(CandidateScoutRadiusCM, 10.0f, PlanetRadiusCM * (PI - 0.01f));

	UTexture2D* CandidateTexture = BuildTerrainTexture(
		CandidateCenterUnit, CandidateEastUnit, CandidateNorthUnit, CandidateScoutRadiusCM);
	if (CandidateTexture == nullptr) return false;
	// Commit the complete snapshot atomically. A failed repeated scout therefore
	// leaves the previous map frame, texture and markers mutually consistent.
	RevealCenterUnit = CandidateCenterUnit;
	MapEastUnit = CandidateEastUnit;
	MapNorthUnit = CandidateNorthUnit;
	ResolvedScoutRadiusCM = CandidateScoutRadiusCM;
	TerrainTexture = CandidateTexture;
	bScoutMapRevealed = true;
	EnvironmentRefreshAccumulatorSeconds = 0.0f;
	RefreshEnvironmentMarkers();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M10][Reveal] Bird=Blue Landing=(%.1f,%.1f,%.1f) Radius=%.1f Ratio=%.3f Resolution=%d Markers=%d"),
		LandingLocation.X, LandingLocation.Y, LandingLocation.Z,
		ResolvedScoutRadiusCM, ResolvedScoutRadiusCM / PlanetRadiusCM,
		FMath::Clamp(Settings.TerrainTextureResolution, 64, 512), EnvironmentMarkers.Num());
	return true;
}

void AABTSM10ScoutMapSystem::BuildFixedMapFrame(
	const FVector& CenterUnitDirection,
	FVector& OutEastUnit,
	FVector& OutNorthUnit) const
{
	const FVector SafeCenterUnit = CenterUnitDirection.GetSafeNormal();
	OutNorthUnit = FVector::VectorPlaneProject(FVector::UpVector, SafeCenterUnit).GetSafeNormal();
	if (OutNorthUnit.IsNearlyZero())
	{
		OutNorthUnit = FVector::VectorPlaneProject(FVector::ForwardVector, SafeCenterUnit).GetSafeNormal();
	}
	if (OutNorthUnit.IsNearlyZero())
	{
		OutNorthUnit = FVector::VectorPlaneProject(FVector::RightVector, SafeCenterUnit).GetSafeNormal();
	}
	OutEastUnit = FVector::CrossProduct(OutNorthUnit, SafeCenterUnit).GetSafeNormal();
	OutNorthUnit = FVector::CrossProduct(SafeCenterUnit, OutEastUnit).GetSafeNormal();
}

UTexture2D* AABTSM10ScoutMapSystem::BuildTerrainTexture(
	const FVector& CenterUnitDirection,
	const FVector& EastUnit,
	const FVector& NorthUnit,
	const float ScoutRadiusCM) const
{
	if (!Planet.IsValid() || ScoutRadiusCM <= 0.0f) return nullptr;
	const int32 Resolution = FMath::Clamp(Settings.TerrainTextureResolution, 64, 512);
	const float PlanetRadiusCM = FMath::Max(1.0f, Planet->GetPlanetRadiusCM());
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Resolution * Resolution);

	for (int32 PixelY = 0; PixelY < Resolution; ++PixelY)
	{
		int32 RowCellHint = 0;
		const float MapY = ((static_cast<float>(PixelY) + 0.5f) / Resolution) * 2.0f - 1.0f;
		for (int32 PixelX = 0; PixelX < Resolution; ++PixelX)
		{
			const float MapX = ((static_cast<float>(PixelX) + 0.5f) / Resolution) * 2.0f - 1.0f;
			const float NormalizedRadius = FMath::Sqrt(MapX * MapX + MapY * MapY);
			FColor& Pixel = Pixels[PixelY * Resolution + PixelX];
			if (NormalizedRadius > 1.0f)
			{
				Pixel = FColor(0, 0, 0, 0);
				continue;
			}

			FVector SampleDirection = CenterUnitDirection;
			if (NormalizedRadius > KINDA_SMALL_NUMBER)
			{
				const FVector TangentDirection =
					(EastUnit * MapX - NorthUnit * MapY).GetSafeNormal();
				const float AngularDistance = NormalizedRadius * ScoutRadiusCM / PlanetRadiusCM;
				SampleDirection = (CenterUnitDirection * FMath::Cos(AngularDistance)
					+ TangentDirection * FMath::Sin(AngularDistance)).GetSafeNormal();
			}
			FLinearColor TerrainColor;
			int32 SampleCellId = RowCellHint;
			if (Planet->QueryScoutMapTerrainColor(SampleDirection, TerrainColor, RowCellHint, &SampleCellId))
			{
				RowCellHint = SampleCellId;
				Pixel = TerrainColor.ToFColorSRGB();
			}
			else
			{
				Pixel = FColor(28, 28, 32, 255);
			}
			Pixel.A = 255;
		}
	}

	// NAME_None guarantees a unique transient object when Blue scouts repeatedly.
	UTexture2D* NewTerrainTexture = UTexture2D::CreateTransient(
		Resolution, Resolution, PF_B8G8R8A8, NAME_None);
	if (NewTerrainTexture == nullptr || NewTerrainTexture->GetPlatformData() == nullptr
		|| NewTerrainTexture->GetPlatformData()->Mips.IsEmpty()) return nullptr;
	NewTerrainTexture->SRGB = true;
	NewTerrainTexture->Filter = TF_Bilinear;
	NewTerrainTexture->AddressX = TA_Clamp;
	NewTerrainTexture->AddressY = TA_Clamp;
	NewTerrainTexture->NeverStream = true;
	FTexture2DMipMap& Mip = NewTerrainTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	NewTerrainTexture->UpdateResource();
	return NewTerrainTexture;
}

bool AABTSM10ScoutMapSystem::ProjectWorldLocation(
	const FVector& WorldLocation,
	FVector2D& OutNormalizedMapPosition) const
{
	OutNormalizedMapPosition = FVector2D::ZeroVector;
	if (!bScoutMapRevealed || !Planet.IsValid() || ResolvedScoutRadiusCM <= 0.0f) return false;
	const FVector Direction = (WorldLocation - Planet->GetPlanetCenterWorld()).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	const float CenterDot = FMath::Clamp(FVector::DotProduct(RevealCenterUnit, Direction), -1.0f, 1.0f);
	const float AngularDistance = FMath::Acos(CenterDot);
	const float ArcDistanceCM = AngularDistance * Planet->GetPlanetRadiusCM();
	if (ArcDistanceCM > ResolvedScoutRadiusCM) return false;
	if (AngularDistance <= KINDA_SMALL_NUMBER) return true;

	const FVector TangentDirection = (Direction - RevealCenterUnit * CenterDot).GetSafeNormal();
	const float NormalizedRadius = ArcDistanceCM / ResolvedScoutRadiusCM;
	OutNormalizedMapPosition.X = FVector::DotProduct(TangentDirection, MapEastUnit) * NormalizedRadius;
	OutNormalizedMapPosition.Y = -FVector::DotProduct(TangentDirection, MapNorthUnit) * NormalizedRadius;
	return OutNormalizedMapPosition.SizeSquared() <= 1.0001f;
}

void AABTSM10ScoutMapSystem::AppendMarker(
	const FVector& WorldLocation,
	const EABTSM10ScoutMarkerType Type)
{
	if (EnvironmentMarkers.Num() >= FMath::Max(64, Settings.MaximumEnvironmentMarkerCount)) return;
	FVector2D Position;
	if (!ProjectWorldLocation(WorldLocation, Position)) return;
	FABTSM10ScoutMapMarker& Marker = EnvironmentMarkers.AddDefaulted_GetRef();
	Marker.Type = Type;
	Marker.NormalizedMapPosition = Position;
}

void AABTSM10ScoutMapSystem::AppendHISMMarkers(
	UHierarchicalInstancedStaticMeshComponent* HISM,
	const EABTSM10ScoutMarkerType Type)
{
	if (HISM == nullptr || !Planet.IsValid() || ResolvedScoutRadiusCM <= 0.0f) return;
	const float PlanetRadiusCM = FMath::Max(1.0f, Planet->GetPlanetRadiusCM());
	const float AngularRadius = FMath::Clamp(ResolvedScoutRadiusCM / PlanetRadiusCM, 0.0f, PI);
	const float ChordRadiusCM = 2.0f * PlanetRadiusCM * FMath::Sin(AngularRadius * 0.5f);
	const float QueryRadiusCM = ChordRadiusCM + FMath::Max(0.0f, Settings.EnvironmentBroadphasePaddingCM);
	const FVector QueryCenter = Planet->GetPlanetCenterWorld() + RevealCenterUnit * PlanetRadiusCM;
	TArray<int32> CandidateIndices = HISM->GetInstancesOverlappingSphere(QueryCenter, QueryRadiusCM, true);
	CandidateIndices.Sort();
	for (const int32 InstanceIndex : CandidateIndices)
	{
		if (EnvironmentMarkers.Num() >= FMath::Max(64, Settings.MaximumEnvironmentMarkerCount)) break;
		FTransform WorldTransform;
		if (HISM->GetInstanceTransform(InstanceIndex, WorldTransform, true))
		{
			AppendMarker(WorldTransform.GetLocation(), Type);
		}
	}
}

void AABTSM10ScoutMapSystem::RefreshEnvironmentMarkers()
{
	EnvironmentMarkers.Reset();
	if (!bScoutMapRevealed || !Planet.IsValid()) return;
	EnvironmentMarkers.Reserve(FMath::Clamp(Settings.MaximumEnvironmentMarkerCount, 64, 4096));

	// Buildings are scarce and gameplay-critical, so reserve their markers before
	// dense foliage can consume the configured marker budget.
	for (TActorIterator<AABTSM73StableBuildingActor> It(GetWorld()); It; ++It)
	{
		FVector LiveCentroid;
		int32 LiveModuleCount = 0;
		if (It->QueryScoutMapMarkerLocation(Planet.Get(), LiveCentroid, LiveModuleCount))
		{
			AppendMarker(LiveCentroid, EABTSM10ScoutMarkerType::Building);
		}
	}

	// Startup Chaos normally removes every HISM instance and permanently replaces
	// it with a frozen proxy. During later launches the world may contain a mix of
	// both sources, so query M6's live weak-pointer registry every refresh. Dynamic
	// proxies take budget priority over static HISM markers.
	if (SlingshotSystem.IsValid())
	{
		SlingshotSystem->GatherLiveDestructibleProxies(ProxyRefreshScratch);
		for (AABTSM6DestructibleProxy* Proxy : ProxyRefreshScratch)
		{
			if (EnvironmentMarkers.Num() >= FMath::Max(64, Settings.MaximumEnvironmentMarkerCount)) break;
			if (Proxy == nullptr || Proxy->IsActorBeingDestroyed()) continue;
			const EABTSM10ScoutMarkerType Type = Proxy->GetImpactMaterial() == EABTSM6ImpactMaterial::Wood
				? EABTSM10ScoutMarkerType::Tree
				: EABTSM10ScoutMarkerType::Stone;
			if (Proxy->GetImpactMaterial() != EABTSM6ImpactMaterial::Wood
				&& Proxy->GetImpactMaterial() != EABTSM6ImpactMaterial::Stone) continue;
			const UStaticMeshComponent* Mesh = Proxy->GetMeshComponent();
			const FVector ProxyLocation = Mesh ? Mesh->GetComponentLocation() : Proxy->GetActorLocation();
			if (IsInsideEnvironmentBroadphase(ProxyLocation)) AppendMarker(ProxyLocation, Type);
		}
	}

	AppendHISMMarkers(Planet->RockHISM, EABTSM10ScoutMarkerType::Stone);
	AppendHISMMarkers(Planet->ForestHISM, EABTSM10ScoutMarkerType::Tree);
}

bool AABTSM10ScoutMapSystem::IsInsideEnvironmentBroadphase(const FVector& WorldLocation) const
{
	if (!Planet.IsValid() || ResolvedScoutRadiusCM <= 0.0f) return false;
	const float PlanetRadiusCM = FMath::Max(1.0f, Planet->GetPlanetRadiusCM());
	const float AngularRadius = FMath::Clamp(ResolvedScoutRadiusCM / PlanetRadiusCM, 0.0f, PI);
	const float ChordRadiusCM = 2.0f * PlanetRadiusCM * FMath::Sin(AngularRadius * 0.5f);
	const float QueryRadiusCM = ChordRadiusCM + FMath::Max(0.0f, Settings.EnvironmentBroadphasePaddingCM);
	const FVector QueryCenter = Planet->GetPlanetCenterWorld() + RevealCenterUnit * PlanetRadiusCM;
	return FVector::DistSquared(WorldLocation, QueryCenter) <= FMath::Square(QueryRadiusCM);
}
