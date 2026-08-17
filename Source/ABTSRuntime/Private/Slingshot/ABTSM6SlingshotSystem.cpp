// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Guide/ABTSGuideEvents.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSChaosBirdMovementComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "UI/ABTSUITheme.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"
#include "World/ABTSM9Satellite.h"

namespace
{
	constexpr float BasicShapeSphereDiameterCM = 100.0f;

	TAutoConsoleVariable<float> CVarFlightWorldTrajectoryCoreScale(
		TEXT("abts.UI.Flight.WorldTrajectory.CoreScale"), 0.62f,
		TEXT("World trajectory cyan core scale relative to M6 point size [0.25, 1]."));
	TAutoConsoleVariable<float> CVarFlightWorldTrajectoryUnderlayScale(
		TEXT("abts.UI.Flight.WorldTrajectory.UnderlayScale"), 1.24f,
		TEXT("World trajectory dark underlay scale relative to M6 point size [1, 2]."));
	TAutoConsoleVariable<float> CVarFlightWorldTrajectoryEndpointScale(
		TEXT("abts.UI.Flight.WorldTrajectory.EndpointScale"), 1.35f,
		TEXT("Predicted endpoint scale relative to M6 point size [1, 2.5]."));
	TAutoConsoleVariable<float> CVarFlightWorldTrajectoryForegroundDepthBiasCM(
		TEXT("abts.UI.Flight.WorldTrajectory.ForegroundDepthBiasCM"), 0.5f,
		TEXT("Camera-facing world-space bias for the cyan/amber foreground point [0.05, 3] cm."));

	/** Keeps pouch local +Y on the stable stake-to-stake side while local +Z follows launch. */
	FQuat MakePulledPouchRotation(const FVector& LaunchDirection, const FVector& PreferredRight)
	{
		const FVector PouchForwardZ = LaunchDirection.GetSafeNormal();
		FVector PouchSideY = FVector::VectorPlaneProject(PreferredRight, PouchForwardZ).GetSafeNormal();
		if (PouchSideY.IsNearlyZero())
		{
			const FVector FallbackAxis = FMath::Abs(PouchForwardZ.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
			PouchSideY = FVector::CrossProduct(PouchForwardZ, FallbackAxis).GetSafeNormal();
		}
		return FRotationMatrix::MakeFromYZ(PouchSideY, PouchForwardZ).ToQuat();
	}
}

FVector FABTSM6PouchClearanceGeometry::ContractAlongLaunchRay(
	const FVector& LaunchFocusWorld,
	const FVector& UnobstructedPouchWorld,
	const float RetainedDrawScale)
{
	return FMath::Lerp(
		LaunchFocusWorld,
		UnobstructedPouchWorld,
		FMath::Clamp(RetainedDrawScale, 0.0f, 1.0f));
}

AABTSM6SlingshotSystem::AABTSM6SlingshotSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	PouchVisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PouchVisualMesh"));
	PouchVisualMesh->SetupAttachment(VisualRoot);
	PouchVisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PouchVisualMesh->SetGenerateOverlapEvents(false);
	PouchVisualMesh->SetHiddenInGame(true);
	PouchVisualMesh->SetVisibility(false);
	const auto ConfigureTrajectoryInstances = [this](
		TObjectPtr<UInstancedStaticMeshComponent>& OutComponent,
		const TCHAR* ComponentName)
	{
		OutComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(ComponentName);
		OutComponent->SetupAttachment(VisualRoot);
		OutComponent->SetMobility(EComponentMobility::Movable);
		OutComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutComponent->SetGenerateOverlapEvents(false);
		OutComponent->SetCanEverAffectNavigation(false);
		OutComponent->SetCastShadow(false);
		OutComponent->bAffectDistanceFieldLighting = false;
		OutComponent->bAffectDynamicIndirectLighting = false;
		OutComponent->SetHiddenInGame(true);
		OutComponent->SetVisibility(false);
	};
	ConfigureTrajectoryInstances(TrajectoryUnderlayInstances, TEXT("FlightTrajectoryUnderlay"));
	ConfigureTrajectoryInstances(TrajectoryCoreInstances, TEXT("FlightTrajectoryCore"));
	ConfigureTrajectoryInstances(TrajectoryEndpointInstances, TEXT("FlightTrajectoryEndpoint"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		TrajectoryUnderlayInstances->SetStaticMesh(SphereMesh.Object);
		TrajectoryCoreInstances->SetStaticMesh(SphereMesh.Object);
		TrajectoryEndpointInstances->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterial.Succeeded())
	{
		TrajectoryUnderlayInstances->SetMaterial(0, BasicShapeMaterial.Object);
		TrajectoryCoreInstances->SetMaterial(0, BasicShapeMaterial.Object);
		TrajectoryEndpointInstances->SetMaterial(0, BasicShapeMaterial.Object);
	}
	ProxyClass = AABTSM6DestructibleProxy::StaticClass();
	CameraClass = AABTSM6SlingshotCamera::StaticClass();
	DebugTwigSlingshotClass = AABTSM71TwigSlingshotActor::StaticClass();
	DebugSimpleSlingshotClass = AABTSM71SimpleSlingshotActor::StaticClass();
	DebugReinforcedSlingshotClass = AABTSM71ReinforcedSlingshotActor::StaticClass();

	const auto AddBird = [this](const EABTSBirdId Id, const float Knock, const float Break, const float Retain, const float Bounce)
	{
		FABTSM6BirdImpactProfile& P = BirdImpactProfiles.AddDefaulted_GetRef();
		P.BirdId = Id; P.KnockSpeedCMPerSec = Knock; P.BreakSpeedCMPerSec = Break; P.RetainedTangentSpeed = Retain; P.Restitution = Bounce;
	};
	AddBird(EABTSBirdId::Red, 520.0f, 1050.0f, 0.62f, 0.12f);
	AddBird(EABTSBirdId::Blue, 430.0f, 880.0f, 0.68f, 0.10f);
	AddBird(EABTSBirdId::Yellow, 610.0f, 1020.0f, 0.72f, 0.08f);
	AddBird(EABTSBirdId::Black, 720.0f, 1280.0f, 0.48f, 0.05f);
	const auto AddMaterial = [this](const EABTSM6ImpactMaterial Type, const float Knock, const float Break, const float Retain, const float Bounce)
	{
		FABTSM6MaterialImpactProfile& P = MaterialImpactProfiles.AddDefaulted_GetRef();
		P.Material = Type; P.KnockThresholdMultiplier = Knock; P.BreakThresholdMultiplier = Break; P.BirdSpeedRetention = Retain; P.BirdRestitution = Bounce;
	};
	AddMaterial(EABTSM6ImpactMaterial::Terrain, 10.0f, 10.0f, 0.58f, 0.10f);
	AddMaterial(EABTSM6ImpactMaterial::Wood, 0.82f, 0.82f, 0.72f, 0.06f);
	AddMaterial(EABTSM6ImpactMaterial::Stone, 1.20f, 1.30f, 0.48f, 0.22f);
	AddMaterial(EABTSM6ImpactMaterial::Iron, 1.38f, 1.55f, 0.42f, 0.24f);
	AddMaterial(EABTSM6ImpactMaterial::Glass, 0.62f, 0.48f, 0.64f, 0.10f);
	AddMaterial(EABTSM6ImpactMaterial::Building, 1.0f, 1.0f, 0.55f, 0.12f);
	for (FABTSM6MaterialImpactProfile& Profile : MaterialImpactProfiles)
	{
		switch (Profile.Material)
		{
		case EABTSM6ImpactMaterial::Wood:
			Profile.DynamicFriction = 0.72f; Profile.StaticFriction = 0.88f; Profile.ObjectRestitution = 0.05f; Profile.DensityGPerCubicCM = 0.62f; Profile.DamageAtBreakSpeed = 105.0f; Profile.PushVelocityTransfer = 0.86f; break;
		case EABTSM6ImpactMaterial::Stone:
			Profile.DynamicFriction = 0.80f; Profile.StaticFriction = 0.95f; Profile.ObjectRestitution = 0.18f; Profile.DensityGPerCubicCM = 2.55f; Profile.DamageAtBreakSpeed = 86.0f; Profile.PushVelocityTransfer = 0.52f; break;
		case EABTSM6ImpactMaterial::Iron:
			Profile.DynamicFriction = 0.56f; Profile.StaticFriction = 0.70f; Profile.ObjectRestitution = 0.24f; Profile.DensityGPerCubicCM = 7.85f; Profile.DamageAtBreakSpeed = 68.0f; Profile.PushVelocityTransfer = 0.38f; break;
		case EABTSM6ImpactMaterial::Glass:
			Profile.DynamicFriction = 0.36f; Profile.StaticFriction = 0.46f; Profile.ObjectRestitution = 0.12f; Profile.DensityGPerCubicCM = 2.50f; Profile.DamageAtBreakSpeed = 160.0f; Profile.PushVelocityTransfer = 0.68f; break;
		default:
			Profile.DynamicFriction = 0.70f; Profile.StaticFriction = 0.82f; Profile.ObjectRestitution = 0.08f; Profile.DensityGPerCubicCM = 1.0f; break;
		}
	}
}

void AABTSM6SlingshotSystem::BeginPlay()
{
	Super::BeginPlay();
	EnsureTrajectoryVisualMaterials();
	bStartupPhysicsWarmupComplete = !bEnableStartupPhysicsWarmup;
	bStartupPhysicsWarmupFailed = false;
	bStartupPhysicsWarmupStarted = false;
	bStartupPhysicsWarmupWaitingLogged = false;
	StartupPhysicsWarmupEligibleTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, StartupPhysicsWarmupInitialDelaySeconds)
		: 0.0f;
	FABTSM6PhysicsSettleConfig StartupConfig;
	StartupConfig.LinearSpeedThresholdCMPerSec = StartupSettleLinearSpeedThresholdCMPerSec;
	StartupConfig.AngularSpeedThresholdDegPerSec = StartupSettleAngularSpeedThresholdDegPerSec;
	StartupConfig.StableHoldSeconds = StartupSettleStableHoldSeconds;
	StartupConfig.MinimumPostActivitySeconds = 0.0f;
	StartupConfig.MaximumWaitSeconds = StartupSettleDiagnosticPeriodSeconds;
	StartupConfig.SampleIntervalSeconds = 0.1f;
	StartupPhysicsSettleMonitor.Configure(StartupConfig);
	ResolveDependencies();
	const auto ApplyStaticPhysics = [this](UPrimitiveComponent* Component, const EABTSM6ImpactMaterial Material, const TCHAR* Name)
	{
		if (Component == nullptr) return;
		const FABTSM6MaterialImpactProfile& Profile = GetMaterialProfile(Material);
		UPhysicalMaterial* Physical = NewObject<UPhysicalMaterial>(this, FName(Name), RF_Transient);
		Physical->Friction = Profile.DynamicFriction; Physical->StaticFriction = Profile.StaticFriction; Physical->Restitution = Profile.ObjectRestitution; Physical->Density = Profile.DensityGPerCubicCM;
		Physical->bOverrideFrictionCombineMode = true; Physical->FrictionCombineMode = EFrictionCombineMode::Average;
		Physical->bOverrideRestitutionCombineMode = true; Physical->RestitutionCombineMode = EFrictionCombineMode::Average;
		Component->SetPhysMaterialOverride(Physical);
		RuntimeImpactPhysicalMaterials.Add(Physical);
	};
	if (Planet.IsValid())
	{
		ApplyStaticPhysics(Planet->ForestHISM, EABTSM6ImpactMaterial::Wood, TEXT("ABTSForestImpactPhysics"));
		ApplyStaticPhysics(Planet->RockHISM, EABTSM6ImpactMaterial::Stone, TEXT("ABTSRockImpactPhysics"));
	}
	if (bPlanarTestMode)
	{
		for (TActorIterator<AABTSM71TreeHISMActor> It(GetWorld()); It; ++It)
		{
			ApplyStaticPhysics(It->GetHISM(), EABTSM6ImpactMaterial::Wood, TEXT("ABTSPlanarTreeImpactPhysics"));
		}
		for (TActorIterator<AABTSM71RockHISMActor> It(GetWorld()); It; ++It)
		{
			ApplyStaticPhysics(It->GetHISM(), EABTSM6ImpactMaterial::Stone, TEXT("ABTSPlanarRockImpactPhysics"));
		}
		for (TActorIterator<AABTSM71PlaceableBrickActor> It(GetWorld()); It; ++It)
		{
			EABTSM6ImpactMaterial Material = EABTSM6ImpactMaterial::Wood;
			switch (It->GetBuildingMaterial())
			{
			case EABTSM7BuildingMaterial::Stone: Material = EABTSM6ImpactMaterial::Stone; break;
			case EABTSM7BuildingMaterial::Iron: Material = EABTSM6ImpactMaterial::Iron; break;
			case EABTSM7BuildingMaterial::Glass: Material = EABTSM6ImpactMaterial::Glass; break;
			case EABTSM7BuildingMaterial::Crystal: Material = EABTSM6ImpactMaterial::Glass; break;
			default: break;
			}
			ApplyStaticPhysics(It->GetHISM(), Material, TEXT("ABTSPlanarBrickImpactPhysics"));
		}
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SlingshotCamera = GetWorld()->SpawnActor<AABTSM6SlingshotCamera>(CameraClass, FTransform::Identity, Params);
	FABTSM6LaunchProfileCatalog ProductionCatalog =
		FABTSSlingshotSatelliteCalibrationModel::MakeFrozenLaunchProfileCatalogV0();
	if (SlingshotCamera == nullptr
		|| !SlingshotCamera->CopyAimFraming(
			ProductionCatalog.AimCameraDistanceCM,
			ProductionCatalog.AimCameraPitchDegrees,
			ProductionCatalog.AimTargetForwardDistanceCM,
			ProductionCatalog.AimTargetHeightCM)
		|| !ConfigureLaunchProfiles(ProductionCatalog))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][ProfileCatalog] Production initialization failed; normal-tier launch entry will fail closed."));
	}
	SpawnDebugSlingshots();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M6] System ready Camera=%d BirdProfiles=%d MaterialProfiles=%d LaunchProfiles=%d LaunchProfileHash=%llu Calibration=%d"),
		SlingshotCamera ? 1 : 0,
		BirdImpactProfiles.Num(),
		MaterialImpactProfiles.Num(),
		bLaunchProfileCatalogEnabled ? 1 : 0,
		bLaunchProfileCatalogEnabled ? CalibrationLaunchProfileHash : 0,
		bCalibrationModeEnabled ? 1 : 0);
}

void AABTSM6SlingshotSystem::ConfigureDebugSlingshots(const bool bEnable, const int32 InStartCellId)
{
#if UE_BUILD_SHIPPING
	bSpawnDebugSlingshotsAtStart = false;
	(void)bEnable;
	(void)InStartCellId;
#else
	bSpawnDebugSlingshotsAtStart = bEnable;
	DebugStartCellId = InStartCellId;
#endif
}

void AABTSM6SlingshotSystem::ConfigurePlanarTestMode(const FVector& InPlaneOrigin, const FVector& InPlaneUp)
{
	bPlanarTestMode = true;
	PlanarOrigin = InPlaneOrigin;
	PlanarUp = InPlaneUp.GetSafeNormal();
	if (PlanarUp.IsNearlyZero()) PlanarUp = FVector::UpVector;
	Planet.Reset();
}

bool AABTSM6SlingshotSystem::ResolveDependencies()
{
	if (!Party.IsValid()) for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It) { Party = *It; break; }
	if (!bPlanarTestMode && !Planet.IsValid()) for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It) if (It->IsPlanetReady()) { Planet = *It; break; }
	if (!BuildingMaterialSystem.IsValid()) for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It) { BuildingMaterialSystem = *It; break; }
	return Party.IsValid() && Party->IsPartyReady() && (bPlanarTestMode || Planet.IsValid());
}

void AABTSM6SlingshotSystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsStartupPhysicsWarmupComplete() && !bStartupPhysicsWarmupFailed)
	{
		UpdateStartupPhysicsWarmup(DeltaSeconds);
	}
	if (bSpawnDebugSlingshotsAtStart && !bDebugSlingshotsSpawned) SpawnDebugSlingshots();
	if (LaunchState == EABTSM6LaunchState::Ready || LaunchState == EABTSM6LaunchState::Pulling)
	{
		UpdatePouchAndPreview();
		if (LaunchState == EABTSM6LaunchState::Pulling)
		{
			if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
			{
				Audio->UpdateSlingshotPull(
					PouchLocation,
					ActiveCord.IsValid() ? ActiveCord->GetRestCordLengthCM() : 0.0f,
					PullAlpha);
			}
			RebuildCurrentTrajectoryPreview();
			DrawPredictedTrajectory();
		}
		else
		{
			ClearCurrentTrajectoryPreview();
		}
	}
	else if (LaunchState == EABTSM6LaunchState::Flying || LaunchState == EABTSM6LaunchState::Settling)
	{
		FlightElapsedSeconds += DeltaSeconds;
		UpdateActiveLaunchTelemetry();
		if (bCalibrationModeEnabled
			&& CalibrationSuccessReturnRemainingSeconds >= 0.0f)
		{
			CalibrationSuccessReturnRemainingSeconds -=
				FMath::Max(0.0f, DeltaSeconds);
			if (CalibrationSuccessReturnRemainingSeconds <= 0.0f)
			{
				UE_LOG(LogABTSRuntime, Log,
					TEXT("[ABTS][Calibration][Launch] E5ImpactHoldComplete Seq=%d Hold=%.2f"),
					ActiveLaunchCalibrationTelemetry.Sequence,
					FMath::Max(
						0.1f,
						CalibrationE5ImpactHoldSeconds));
				BeginReturn();
				return;
			}
		}
		if (bCalibrationModeEnabled
			&& LaunchState == EABTSM6LaunchState::Flying
			&& FlightElapsedSeconds >= FMath::Max(
				2.0f,
				SatellitePracticePredictionMaximumFlightSeconds > 0.0f
					? SatellitePracticePredictionMaximumFlightSeconds
					: CalibrationMaximumFlightSeconds))
		{
			NotifyCalibrationTargetEvent(TEXT("Timeout"), false);
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Calibration][Launch] ForcedReturn Seq=%d Flight=%.2f"),
				ActiveLaunchCalibrationTelemetry.Sequence,
				FlightElapsedSeconds);
			BeginReturn();
			return;
		}
		if (BlackFuseRemainingSeconds >= 0.0f && !bBlackDetonated)
		{
			BlackFuseRemainingSeconds -= DeltaSeconds;
			if (BlackFuseRemainingSeconds <= 0.0f) DetonateBlackBird(false);
		}
		if (LaunchState == EABTSM6LaunchState::Flying && LaunchedBird.IsValid()
			&& FlightElapsedSeconds > 1.0f && LaunchedBird->IsRadiallyGrounded())
		{
			BeginSettlement();
		}
		if (LaunchState == EABTSM6LaunchState::Settling) UpdatePhysicsSettlement(DeltaSeconds);
	}
	else if (LaunchState == EABTSM6LaunchState::Returning)
	{
		UpdateReturn(DeltaSeconds);
	}
}

bool AABTSM6SlingshotSystem::QueryDebugSurfaceTransform(
	const FVector& UnitDirection,
	const FVector& Forward,
	const float HeightOffsetCM,
	FTransform& OutTransform) const
{
	if (!Planet.IsValid()) return false;
	FVector Position, Normal;
	float Radius = 0.0f;
	int32 CellId = INDEX_NONE;
	if (!Planet->QuerySurface(UnitDirection, Position, Normal, Radius, CellId)) return false;
	FVector TangentForward = FVector::VectorPlaneProject(Forward, Normal).GetSafeNormal();
	if (TangentForward.IsNearlyZero()) TangentForward = FVector::VectorPlaneProject(FVector::ForwardVector, Normal).GetSafeNormal();
	OutTransform = FTransform(FRotationMatrix::MakeFromXZ(TangentForward, Normal).ToQuat(), Position + Normal * HeightOffsetCM);
	return true;
}

bool AABTSM6SlingshotSystem::SpawnDebugSlingshotPair(
	const FVector& CenterDirection,
	const FVector& LaunchDirection,
	const EABTSItemId StakeItem)
{
	if (!Planet.IsValid() || GetWorld() == nullptr) return false;
	const TSubclassOf<AABTSM71PlaceableSlingshotActor> SlingshotClass = StakeItem == EABTSItemId::ReinforcedStake
		? DebugReinforcedSlingshotClass : DebugSimpleSlingshotClass;
	if (!SlingshotClass) return false;
	FTransform SpawnTransform;
	if (!QueryDebugSurfaceTransform(CenterDirection, LaunchDirection, 0.0f, SpawnTransform)) return false;
	SpawnTransform.SetScale3D(DebugSlingshotActorScale.ComponentMax(FVector(0.01f)));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<AABTSM71PlaceableSlingshotActor>(SlingshotClass, SpawnTransform, Params) != nullptr;
}

void AABTSM6SlingshotSystem::SpawnDebugSlingshots()
{
#if UE_BUILD_SHIPPING
	bSpawnDebugSlingshotsAtStart = false;
	return;
#else
	if (!bSpawnDebugSlingshotsAtStart || bDebugSlingshotsSpawned || !ResolveDependencies()
		|| !Planet->LogicalCells.IsValidIndex(DebugStartCellId)) return;
	const FVector StartDirection = Planet->LogicalCells[DebugStartCellId].UnitCenter.GetSafeNormal();
	const FVector Reference = FMath::Abs(StartDirection.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
	const FVector Forward = FVector::CrossProduct(Reference, StartDirection).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(StartDirection, Forward).GetSafeNormal();
	int32 Spawned = 0;
	const FVector Directions[] = {Forward, -Forward, Right, -Right, (Forward + Right).GetSafeNormal(), (Forward - Right).GetSafeNormal(), (-Forward + Right).GetSafeNormal(), (-Forward - Right).GetSafeNormal()};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
	{
		const float RingDistanceCM = Index < 4 ? 850.0f : 1450.0f;
		const FVector SiteDirection = (StartDirection * Planet->GetPlanetRadiusCM() + Directions[Index] * RingDistanceCM).GetSafeNormal();
		const EABTSItemId StakeItem = Index < 4 ? EABTSItemId::SimpleStake : EABTSItemId::ReinforcedStake;
		Spawned += SpawnDebugSlingshotPair(SiteDirection, Directions[Index], StakeItem) ? 1 : 0;
	}
	bDebugSlingshotsSpawned = true;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][DebugSlingshots] Enabled=1 StartCell=%d Spawned=%d Simple=4 Reinforced=4"), DebugStartCellId, Spawned);
#endif
}

bool AABTSM6SlingshotSystem::IsBirdAllowed(const AABTSM25BirdCharacter& Bird, const AABTSM51SlingshotCord& Cord) const
{
	switch (Cord.GetSlingshotTier())
	{
	case EABTSSlingshotTier::Twig:
		return Bird.GetSlingshotCapability() == EABTSBirdSlingshotCapability::TwigScout;
	case EABTSSlingshotTier::Simple:
		return Bird.GetSlingshotCapability() != EABTSBirdSlingshotCapability::Reinforced;
	case EABTSSlingshotTier::Reinforced:
	case EABTSSlingshotTier::Space:
	default:
		return true;
	}
}

bool AABTSM6SlingshotSystem::TryEnterLaunchMode(AABTSM51SlingshotCord& Cord)
{
	// Building validation is a world-validity gate, not an optional part of the
	// HISM warmup. Keep it active when warmup is disabled and for actors that
	// may have appeared after the one-shot startup pass.
	if (!AreRuntimeBuildingsReadyForLaunch()) return false;
	if (!IsStartupPhysicsWarmupComplete())
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][StartupPhysics] Launch blocked: world Chaos settling is still in progress."));
		return false;
	}
	if (LaunchState != EABTSM6LaunchState::Inactive || !ResolveDependencies()) return false;
	const EABTSSlingshotTier Tier = Cord.GetSlingshotTier();
	if (Tier != EABTSSlingshotTier::Space && FindLaunchProfile(Tier) == nullptr)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M6][Enter] Rejected Reason=LaunchProfileUnavailable Tier=%d LaunchProfileHash=%llu"),
			static_cast<int32>(Tier),
			bLaunchProfileCatalogEnabled ? CalibrationLaunchProfileHash : 0);
		return false;
	}
	AABTSM25BirdCharacter* Bird = Party->GetControlledBird();
	if (Bird == nullptr || !IsBirdAllowed(*Bird, Cord))
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M6][Enter] Rejected Bird=%d Stake=%s"), Bird ? ABTSBirdIdToIndex(Bird->GetBirdId()) : -1, *ABTSGetItemFallbackLabel(Cord.GetStakeItem()));
		FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotEntryRejected,
			FABTSGuideSubjects::FromSlingshotTier(Tier), &Cord,
			Bird ? ABTSBirdIdToIndex(Bird->GetBirdId()) : -1);
		return false;
	}
	ActiveCord = &Cord;
	LaunchedBird = Bird;
	ClearCurrentTrajectoryPreview();
	bHasPendingLaunchCompletion = false;
	BuildLaunchFrame(Cord, *Bird);
	ConfigurePouchVisual(Cord);
	Party->SetSlingshotMode(true);
	ArrangeWaitingBirds();
	const FQuat RestPouchRotation = Cord.GetRestPouchTransform().GetRotation();
	const FQuat MountedBirdRotation =
		ABTSMakeSlingshotMountedBirdRotation(SlingForward, SlingUp);
	Bird->EnterSlingshotPouch(
		GetBirdInPouchLocation(RestPouchRotation),
		MountedBirdRotation);
	if (Bird->GetSelectedMovementMode() == EABTSBirdMovementMode::ChaosRigidBody)
	{
		if (UABTSChaosBirdMovementComponent* Movement = Bird->GetChaosMovementComponent()) Movement->OnBlockingImpact().AddUObject(this, &AABTSM6SlingshotSystem::HandleBirdImpact);
	}
	else if (UABTSRadialForceMovementComponent* Movement = Bird->GetForceMovementComponent())
	{
		Movement->OnBlockingImpact().AddUObject(this, &AABTSM6SlingshotSystem::HandleBirdImpact);
	}
	if (AABTSM6PlayerController* PC = Cast<AABTSM6PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->SetLaunchModeInputBlocked(true);
		if (SlingshotCamera) PC->SetViewTarget(SlingshotCamera);
	}
	PullAlpha = GetResolvedInitialPullAlpha();
	AimPlaneOffset = FVector::ZeroVector;
	LaunchState = EABTSM6LaunchState::Ready;
	if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
	{
		Audio->SetMusicState(EABTSMusicState::Aim);
	}
	if (SlingshotCamera)
	{
		SlingshotCamera->SetAimFrame(SlingCenter, SlingForward, SlingUp);
		if (!bPlanarTestMode && Planet.IsValid())
		{
			SlingshotCamera->ConfigureAimPrimarySurfaceGroundContext(Planet.Get());
		}
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Enter] Bird=%d Reinforced=%d"), ABTSBirdIdToIndex(Bird->GetBirdId()), Cord.GetStakeItem() == EABTSItemId::ReinforcedStake ? 1 : 0);
	FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotReady,
		FABTSGuideSubjects::FromSlingshotTier(Tier), &Cord,
		ABTSBirdIdToIndex(Bird->GetBirdId()), FMath::RoundToInt(PullAlpha * 100.0f));
	return true;
}

void AABTSM6SlingshotSystem::BuildLaunchFrame(AABTSM51SlingshotCord& Cord, AABTSM25BirdCharacter& Bird)
{
	SlingCenter = (Cord.GetEndpointA() + Cord.GetEndpointB()) * 0.5f;
	SlingUp = bPlanarTestMode ? PlanarUp : Planet->GetRadialUpAtWorldLocation(SlingCenter);
	SlingRight = FVector::VectorPlaneProject(Cord.GetEndpointB() - Cord.GetEndpointA(), SlingUp).GetSafeNormal();
	// The cord's tangent-plane normal is the fixed launch axis and camera axis.
	// Choose the side facing the bird only once on entry; cursor pull does not
	// mutate this frame.
	SlingForward = FVector::CrossProduct(SlingRight, SlingUp).GetSafeNormal();
	const FVector BirdSide = FVector::VectorPlaneProject(SlingCenter - Bird.GetActorLocation(), SlingUp).GetSafeNormal();
	if (SlingForward.IsNearlyZero()) SlingForward = BirdSide;
	if (!BirdSide.IsNearlyZero() && FVector::DotProduct(SlingForward, BirdSide) < 0.0f) SlingForward *= -1.0f;
	if (FVector::DotProduct(FVector::CrossProduct(SlingUp, SlingForward), SlingRight) < 0.0f) SlingRight *= -1.0f;
	RestPouchLocation = Cord.GetRestPouchTransform().GetLocation();
	PouchLocation = RestPouchLocation;
}

void AABTSM6SlingshotSystem::ArrangeWaitingBirds()
{
	if (!Party.IsValid() || (!bPlanarTestMode && !Planet.IsValid())) return;
	int32 WaitingIndex = 0;
	for (AABTSM25BirdCharacter* Bird : Party->GetPartyMembers())
	{
		if (Bird == nullptr || Bird == LaunchedBird.Get()) continue;
		const float Side = WaitingIndex % 2 == 0 ? -1.0f : 1.0f;
		const float Row = 1.0f + static_cast<float>(WaitingIndex / 2);
		const FVector Approx = SlingCenter + SlingRight * Side * (220.0f * Row) - SlingForward * 210.0f;
		FVector Direction = SlingUp;
		FVector WaitingLocation;
		if (bPlanarTestMode)
		{
			WaitingLocation = Approx;
			const float DesiredHeight = Bird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f;
			WaitingLocation += PlanarUp * (DesiredHeight - FVector::DotProduct(WaitingLocation - PlanarOrigin, PlanarUp));
		}
		else
		{
			Direction = (Approx - Planet->GetPlanetCenterWorld()).GetSafeNormal();
			const float Radius = Planet->GetSurfaceRadiusAtDirection(Direction) + Bird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f;
			WaitingLocation = Planet->GetPlanetCenterWorld() + Direction * Radius;
		}
		Bird->ResetRadialMovementState();
		Bird->SetActorLocationAndRotation(WaitingLocation, FRotationMatrix::MakeFromXZ(SlingForward, Direction).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
		++WaitingIndex;
	}
}

bool AABTSM6SlingshotSystem::BeginPull(APlayerController& Controller)
{
	if (LaunchState != EABTSM6LaunchState::Ready || !LaunchedBird.IsValid()) return false;
	FVector2D PouchScreen;
	float MouseX = 0.0f, MouseY = 0.0f;
	if (!Controller.ProjectWorldLocationToScreen(PouchLocation, PouchScreen) || !Controller.GetMousePosition(MouseX, MouseY)
		|| FVector2D::Distance(PouchScreen, FVector2D(MouseX, MouseY)) > 125.0f) return false;
	LaunchState = EABTSM6LaunchState::Pulling;
	UpdateAimFromCursor(Controller);
	if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
	{
		Audio->UpdateSlingshotPull(
			PouchLocation,
			ActiveCord.IsValid() ? ActiveCord->GetRestCordLengthCM() : 0.0f,
			PullAlpha);
	}
	if (ActiveCord.IsValid())
	{
		FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotPulling,
			FABTSGuideSubjects::FromSlingshotTier(ActiveCord->GetSlingshotTier()),
			ActiveCord.Get(), FMath::RoundToInt(PullAlpha * 100.0f));
	}
	return true;
}

void AABTSM6SlingshotSystem::UpdateAimFromCursor(APlayerController& Controller)
{
	if (LaunchState != EABTSM6LaunchState::Pulling) return;
	FVector RayOrigin, RayDirection;
	if (!Controller.DeprojectMousePositionToWorld(RayOrigin, RayDirection)) return;
	const FVector PlaneNormal = SlingshotCamera ? SlingshotCamera->GetActorForwardVector() : SlingForward;
	const float Denominator = FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::Abs(Denominator) <= SMALL_NUMBER) return;
	const float PullDistance = LaunchState == EABTSM6LaunchState::Pulling
		? FMath::Lerp(
			GetResolvedMinimumPullDistanceCM(),
			GetResolvedMaximumPullDistanceCM(),
			PullAlpha)
		: 0.0f;
	const FVector PulledPlaneCenter = RestPouchLocation - SlingForward * PullDistance;
	const float Distance = FVector::DotProduct(PulledPlaneCenter - RayOrigin, PlaneNormal) / Denominator;
	if (Distance <= 0.0f) return;
	AimPlaneOffset =
		((RayOrigin + RayDirection * Distance - PulledPlaneCenter)
			* GetResolvedAimSensitivityScale())
		.GetClampedToMaxSize(GetResolvedMaximumAimPlaneOffsetCM());
}

void AABTSM6SlingshotSystem::AdjustPullPower(const float MouseWheelValue)
{
	if (LaunchState != EABTSM6LaunchState::Ready && LaunchState != EABTSM6LaunchState::Pulling) return;
	const float PreviousPullAlpha = PullAlpha;
	// UE wheel axis is positive upward. Design contract: wheel down increases power.
	PullAlpha = FMath::Clamp(
		PullAlpha - MouseWheelValue * GetResolvedPullPowerWheelStep(),
		0.0f,
		1.0f);
	if (!FMath::IsNearlyEqual(PreviousPullAlpha, PullAlpha) && ActiveCord.IsValid())
	{
		FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotPowerChanged,
			FABTSGuideSubjects::FromSlingshotTier(ActiveCord->GetSlingshotTier()),
			ActiveCord.Get(), FMath::RoundToInt(PullAlpha * 100.0f));
	}
}

void AABTSM6SlingshotSystem::UpdatePouchAndPreview()
{
	if (!LaunchedBird.IsValid()) return;
	const float PullDistance = FMath::Lerp(
		GetResolvedMinimumPullDistanceCM(),
		GetResolvedMaximumPullDistanceCM(),
		PullAlpha);
	const FVector LaunchFocus = SlingCenter + SlingUp * 65.0f;
	const FVector UnobstructedPouchLocation =
		RestPouchLocation + AimPlaneOffset - SlingForward * PullDistance;
	const FVector UnobstructedDirection =
		(LaunchFocus - UnobstructedPouchLocation).GetSafeNormal();
	const FQuat PouchRotation =
		MakePulledPouchRotation(UnobstructedDirection, SlingRight);
	const float BirdRadiusCM = FMath::Max(
		1.0f, LaunchedBird->GetSlingshotTrajectoryCollisionRadiusCM());
	constexpr float ReleaseClearanceMarginCM = 10.0f;

	const auto ResolveClearance = [this, &PouchRotation, BirdRadiusCM](
		const FVector& CandidatePouchLocation,
		float& OutClearanceDeficitCM)
	{
		const FVector CandidateBirdCenter =
			CandidatePouchLocation
			+ PouchRotation.RotateVector(
				FVector(0.0f, 0.0f, BirdInPouchOffsetCM));
		FVector SurfacePosition = FVector::ZeroVector;
		FVector SurfaceNormal = SlingUp;
		bool bSurfaceResolved = false;
		if (bPlanarTestMode)
		{
			SurfacePosition = CandidateBirdCenter
				- PlanarUp * FVector::DotProduct(
					CandidateBirdCenter - PlanarOrigin, PlanarUp);
			SurfaceNormal = PlanarUp;
			bSurfaceResolved = true;
		}
		else if (Planet.IsValid())
		{
			float SurfaceRadiusCM = 0.0f;
			int32 SurfaceCellId = INDEX_NONE;
			const FVector SurfaceDirection =
				(CandidateBirdCenter - Planet->GetPlanetCenterWorld()).GetSafeNormal();
			bSurfaceResolved = !SurfaceDirection.IsNearlyZero()
				&& Planet->QuerySurface(
					SurfaceDirection,
					SurfacePosition,
					SurfaceNormal,
					SurfaceRadiusCM,
					SurfaceCellId);
		}
		if (!bSurfaceResolved)
		{
			OutClearanceDeficitCM = 0.0f;
			return false;
		}
		const float CurrentClearanceCM = FVector::DotProduct(
			CandidateBirdCenter - SurfacePosition,
			SurfaceNormal.GetSafeNormal());
		OutClearanceDeficitCM =
			BirdRadiusCM + ReleaseClearanceMarginCM - CurrentClearanceCM;
		return true;
	};

	PouchLocation = UnobstructedPouchLocation;
	float RetainedDrawScale = 1.0f;
	float RawClearanceDeficitCM = 0.0f;
	const bool bSurfaceResolved =
		ResolveClearance(UnobstructedPouchLocation, RawClearanceDeficitCM);
	if (bSurfaceResolved && RawClearanceDeficitCM > 0.01f)
	{
		// PullAlpha owns power. When terrain blocks the visual draw, shorten the
		// physical pouch along the same launch ray so direction and launch speed
		// remain authoritative while the bird stays above the primary surface.
		constexpr float MinimumRetainedDrawScale = 0.05f;
		float MinimumScaleDeficitCM = 0.0f;
		const FVector MinimumScalePouch =
			FABTSM6PouchClearanceGeometry::ContractAlongLaunchRay(
				LaunchFocus,
				UnobstructedPouchLocation,
				MinimumRetainedDrawScale);
		if (ResolveClearance(MinimumScalePouch, MinimumScaleDeficitCM)
			&& MinimumScaleDeficitCM <= 0.01f)
		{
			float SafeScale = MinimumRetainedDrawScale;
			float BlockedScale = 1.0f;
			for (int32 Iteration = 0; Iteration < 12; ++Iteration)
			{
				const float CandidateScale = (SafeScale + BlockedScale) * 0.5f;
				float CandidateDeficitCM = 0.0f;
				const FVector CandidatePouch =
					FABTSM6PouchClearanceGeometry::ContractAlongLaunchRay(
						LaunchFocus,
						UnobstructedPouchLocation,
						CandidateScale);
				if (ResolveClearance(CandidatePouch, CandidateDeficitCM)
					&& CandidateDeficitCM <= 0.01f)
				{
					SafeScale = CandidateScale;
				}
				else
				{
					BlockedScale = CandidateScale;
				}
			}
			RetainedDrawScale = SafeScale;
			PouchLocation =
				FABTSM6PouchClearanceGeometry::ContractAlongLaunchRay(
					LaunchFocus,
					UnobstructedPouchLocation,
					RetainedDrawScale);
		}
		else
		{
			UE_LOG(
				LogABTSRuntime,
				Error,
				TEXT("[ABTS][M6][PouchSurfaceClearance] Rejected Reason=LaunchFocusNotClear RawDeficit=%.2f MinimumScaleDeficit=%.2f Pull=%.3f"),
				RawClearanceDeficitCM,
				MinimumScaleDeficitCM,
				PullAlpha);
			PouchLocation = MinimumScalePouch;
			RetainedDrawScale = MinimumRetainedDrawScale;
		}
	}
	const FVector Direction = (LaunchFocus - PouchLocation).GetSafeNormal();
	const float DirectionDot = FVector::DotProduct(
		UnobstructedDirection,
		Direction);
	const bool bSurfaceClampActive = RetainedDrawScale < 0.9999f;
	const float ShortenedDrawCM = FVector::Distance(
		UnobstructedPouchLocation,
		PouchLocation);
	if (bSurfaceClampActive != bPouchSurfaceClampActive
		|| (bSurfaceClampActive
			&& !FMath::IsNearlyEqual(
				ShortenedDrawCM, LastPouchSurfaceAdjustmentCM, 1.0f)))
	{
		UE_LOG(
			LogABTSRuntime,
			Display,
			TEXT("[ABTS][M6][PouchSurfaceClearance] Active=%d Mode=LaunchRayContract Scale=%.4f Shortened=%.2f DirectionDot=%.9f Pull=%.3f BirdRadius=%.2f Margin=10.00"),
			bSurfaceClampActive ? 1 : 0,
			RetainedDrawScale,
			ShortenedDrawCM,
			DirectionDot,
			PullAlpha,
			BirdRadiusCM);
	}
	bPouchSurfaceClampActive = bSurfaceClampActive;
	LastPouchSurfaceAdjustmentCM = ShortenedDrawCM;
	const FQuat MountedBirdRotation =
		ABTSMakeSlingshotMountedBirdRotation(Direction, SlingUp);
	LaunchedBird->SetActorLocationAndRotation(
		GetBirdInPouchLocation(PouchRotation), MountedBirdRotation, false, nullptr, ETeleportType::TeleportPhysics);
	UpdatePouchVisual(PouchRotation);
}

void AABTSM6SlingshotSystem::ConfigurePouchVisual(const AABTSM51SlingshotCord& Cord)
{
	ActivePouchVisualSlot = Cord.GetPouchVisualSlot();
	// The cord actor owns the authoritative pouch and both elastic segments.
	// Keep this native component hidden for compatibility with existing blueprints.
	SetPouchVisualActive(false);
}

void AABTSM6SlingshotSystem::UpdatePouchVisual(const FQuat& PouchRotation)
{
	if (ActiveCord.IsValid()) ActiveCord->UpdatePulledPouchVisual(PouchLocation, PouchRotation);
}

void AABTSM6SlingshotSystem::SetPouchVisualActive(const bool bActive)
{
	if (PouchVisualMesh == nullptr) return;
	PouchVisualMesh->SetVisibility(bActive, true);
	PouchVisualMesh->SetHiddenInGame(!bActive, true);
}

FVector AABTSM6SlingshotSystem::GetBirdInPouchLocation(const FQuat& PouchRotation) const
{
	return PouchLocation + PouchRotation.RotateVector(FVector(0.0f, 0.0f, BirdInPouchOffsetCM));
}

void AABTSM6SlingshotSystem::EnsureTrajectoryVisualMaterials()
{
	const auto EnsureMaterial = [this](
		UInstancedStaticMeshComponent* Component,
		TObjectPtr<UMaterialInstanceDynamic>& Material,
		const FLinearColor& Color)
	{
		if (Component == nullptr) return;
		if (Material == nullptr && Component->GetMaterial(0) != nullptr)
		{
			Material = UMaterialInstanceDynamic::Create(Component->GetMaterial(0), this);
			if (Material != nullptr) Component->SetMaterial(0, Material);
		}
		if (Material != nullptr)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
	};
	const FABTSUIThemeSnapshot Theme = FABTSUITheme::Get();
	EnsureMaterial(TrajectoryUnderlayInstances, TrajectoryUnderlayMaterial, Theme.SlotBorder);
	EnsureMaterial(TrajectoryCoreInstances, TrajectoryCoreMaterial, Theme.AccentSecondary);
	EnsureMaterial(TrajectoryEndpointInstances, TrajectoryEndpointMaterial, Theme.AccentPrimary);
}

void AABTSM6SlingshotSystem::ClearTrajectoryVisualInstances()
{
	for (UInstancedStaticMeshComponent* Component : {
		TrajectoryUnderlayInstances.Get(),
		TrajectoryCoreInstances.Get(),
		TrajectoryEndpointInstances.Get() })
	{
		if (Component == nullptr) continue;
		Component->ClearInstances();
		Component->SetVisibility(false, true);
		Component->SetHiddenInGame(true, true);
	}
}

void AABTSM6SlingshotSystem::DrawPredictedTrajectory()
{
	if (LaunchState != EABTSM6LaunchState::Pulling || !bCurrentTrajectoryPreviewValid)
	{
		ClearTrajectoryVisualInstances();
		return;
	}
	EnsureTrajectoryVisualMaterials();
	if (TrajectoryUnderlayInstances == nullptr
		|| TrajectoryCoreInstances == nullptr
		|| TrajectoryEndpointInstances == nullptr
		|| TrajectoryUnderlayInstances->GetStaticMesh() == nullptr
		|| TrajectoryCoreInstances->GetStaticMesh() == nullptr
		|| TrajectoryEndpointInstances->GetStaticMesh() == nullptr)
	{
		ClearTrajectoryVisualInstances();
		return;
	}
	const float UnderlaySize = TrajectoryPointSize * FMath::Clamp(
		CVarFlightWorldTrajectoryUnderlayScale.GetValueOnGameThread(), 1.0f, 2.0f);
	const float CoreSize = TrajectoryPointSize * FMath::Clamp(
		CVarFlightWorldTrajectoryCoreScale.GetValueOnGameThread(), 0.25f, 1.0f);
	const float EndpointSize = TrajectoryPointSize * FMath::Clamp(
		CVarFlightWorldTrajectoryEndpointScale.GetValueOnGameThread(), 1.0f, 2.5f);
	const float ForegroundDepthBiasCM = FMath::Clamp(
		CVarFlightWorldTrajectoryForegroundDepthBiasCM.GetValueOnGameThread(), 0.05f, 3.0f);
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const bool bHasPlayerView = PlayerController != nullptr;
	if (bHasPlayerView)
	{
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	const int32 VisiblePointCount = FMath::Min(
		FMath::Clamp(TrajectorySampleCount, 8, 128),
		CurrentTrajectoryPreview.WorldPoints.Num());
	const int32 LastVisibleIndex = FMath::Max(0, (VisiblePointCount - 1) & ~1);
	TArray<FTransform> UnderlayTransforms;
	TArray<FTransform> CoreTransforms;
	TArray<FTransform> EndpointTransforms;
	UnderlayTransforms.Reserve((VisiblePointCount + 1) / 2);
	CoreTransforms.Reserve((VisiblePointCount + 1) / 2);
	EndpointTransforms.Reserve(1);
	for (int32 Index = 0; Index < VisiblePointCount; ++Index)
	{
		if ((Index & 1) == 0)
		{
			const FVector& Point = CurrentTrajectoryPreview.WorldPoints[Index];
			const FVector ForegroundPoint = bHasPlayerView
				? Point + (ViewLocation - Point).GetSafeNormal() * ForegroundDepthBiasCM
				: Point;
			UnderlayTransforms.Emplace(
				FQuat::Identity,
				Point,
				FVector(UnderlaySize / BasicShapeSphereDiameterCM));
			TArray<FTransform>& ForegroundTransforms =
				Index == LastVisibleIndex ? EndpointTransforms : CoreTransforms;
			ForegroundTransforms.Emplace(
				FQuat::Identity,
				ForegroundPoint,
				FVector((Index == LastVisibleIndex ? EndpointSize : CoreSize)
					/ BasicShapeSphereDiameterCM));
		}
	}
	TrajectoryUnderlayInstances->ClearInstances();
	TrajectoryCoreInstances->ClearInstances();
	TrajectoryEndpointInstances->ClearInstances();
	TrajectoryUnderlayInstances->AddInstances(UnderlayTransforms, false, true, false);
	TrajectoryCoreInstances->AddInstances(CoreTransforms, false, true, false);
	TrajectoryEndpointInstances->AddInstances(EndpointTransforms, false, true, false);
	for (UInstancedStaticMeshComponent* Component : {
		TrajectoryUnderlayInstances.Get(),
		TrajectoryCoreInstances.Get(),
		TrajectoryEndpointInstances.Get() })
	{
		Component->SetHiddenInGame(false, true);
		Component->SetVisibility(Component->GetInstanceCount() > 0, true);
	}
}

void AABTSM6SlingshotSystem::ReleaseLaunch()
{
	if (LaunchState != EABTSM6LaunchState::Pulling || !LaunchedBird.IsValid()) return;
	const FVector Velocity = ComputeLaunchVelocity();
	if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
	{
		Audio->PlaySlingshotRelease(
			PouchLocation,
			ActiveCord.IsValid() ? ActiveCord->GetRestCordLengthCM() : 0.0f,
			PullAlpha);
	}
	if (SlingshotCamera)
	{
		SlingshotCamera->LockSatelliteFlightIntent(
			bCurrentTrajectoryPreviewValid
				? CurrentTrajectoryPreview
				: FABTSM6TrajectoryPreview());
	}
	ClearCurrentTrajectoryPreview();
	SetPouchVisualActive(false);
	if (ActiveCord.IsValid()) ActiveCord->ResetPouchVisualToRest();
	// Ready/Pulling are aiming-only states. Scene objects must remain static
	// until the bird actually leaves the pouch.
	LaunchState = EABTSM6LaunchState::Flying;
	LaunchedBird->LaunchFromSlingshot(Velocity, GetResolvedFlightAirDragPerSecond());
	if (bCalibrationModeEnabled && ActiveCord.IsValid())
	{
		ActiveLaunchCalibrationTelemetry = FABTSM6LaunchCalibrationTelemetry();
		ActiveLaunchCalibrationTelemetry.Sequence = ++CalibrationLaunchSequence;
		ActiveLaunchCalibrationTelemetry.Tier = ActiveCord->GetSlingshotTier();
		ActiveLaunchCalibrationTelemetry.LaunchProfileHash =
			CalibrationLaunchProfileHash;
		ActiveLaunchCalibrationTelemetry.PullAlpha = PullAlpha;
		ActiveLaunchCalibrationTelemetry.AimPlaneOffsetCM = AimPlaneOffset;
		ActiveLaunchCalibrationTelemetry.InitialWorldLocation =
			LaunchedBird->GetActorLocation();
		ActiveLaunchCalibrationTelemetry.InitialWorldVelocity = Velocity;
		ActiveLaunchCalibrationTelemetry.InitialSpeedCMPerSec = Velocity.Size();
		LastCalibrationTelemetrySampleWorld =
			ActiveLaunchCalibrationTelemetry.InitialWorldLocation;
		CalibrationSatelliteBodyHitFrame = MAX_uint64;
		CalibrationSatelliteE5HitFrame = MAX_uint64;
		CalibrationSatelliteDecisionFrame = MAX_uint64;
		bActiveLaunchCalibrationTelemetry = true;
	}
	CalibrationSuccessReturnRemainingSeconds = -1.0f;
	bSatelliteLandingSettlementActive = false;
	LastSatelliteSurfaceContactWorldTimeSeconds = -BIG_NUMBER;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M6][Launch] Bird=%d Tier=%d Speed=%.1f Pull=%.2f Aim=(%.1f,%.1f,%.1f) LaunchProfileHash=%llu Calibration=%d"),
		ABTSBirdIdToIndex(LaunchedBird->GetBirdId()),
		ActiveCord.IsValid() ? static_cast<int32>(ActiveCord->GetSlingshotTier()) : -1,
		Velocity.Size(),
		PullAlpha,
		AimPlaneOffset.X,
		AimPlaneOffset.Y,
		AimPlaneOffset.Z,
		GetActiveLaunchProfile() != nullptr ? CalibrationLaunchProfileHash : 0,
		bCalibrationModeEnabled ? 1 : 0);
	if (ActiveCord.IsValid())
	{
		FABTSGuideEventBus::Publish(this, FABTSGuideEventIds::SlingshotLaunched,
			FABTSGuideSubjects::FromSlingshotTier(ActiveCord->GetSlingshotTier()),
			LaunchedBird.Get(), ABTSBirdIdToIndex(LaunchedBird->GetBirdId()),
			FMath::RoundToInt(Velocity.Size()));
	}
	if (!bCalibrationModeEnabled)
	{
		BeginLaunchGravityPhase();
	}
	FlightElapsedSeconds = 0.0f;
	BlackFuseRemainingSeconds = -1.0f;
	bBlackDetonated = false;
	FABTSM6PhysicsSettleConfig SettleConfig;
	SettleConfig.LinearSpeedThresholdCMPerSec = SettleLinearSpeedThresholdCMPerSec;
	SettleConfig.AngularSpeedThresholdDegPerSec = SettleAngularSpeedThresholdDegPerSec;
	SettleConfig.StableHoldSeconds = SettleStableHoldSeconds;
	SettleConfig.MinimumPostActivitySeconds = SettleMinimumPostActivitySeconds;
	SettleConfig.MaximumWaitSeconds = SettleMaximumWaitSeconds;
	SettleConfig.SampleIntervalSeconds = SettleSampleIntervalSeconds;
	PhysicsSettleMonitor.Configure(SettleConfig);
	PhysicsSettleMonitor.Reset(GetWorld()->GetTimeSeconds());
	if (SlingshotCamera)
	{
		if (bPlanarTestMode) SlingshotCamera->FollowBirdPlanar(LaunchedBird.Get(), PlanarUp);
		else SlingshotCamera->FollowBird(LaunchedBird.Get(), Planet.Get());
	}
}

const FABTSM6BirdImpactProfile& AABTSM6SlingshotSystem::GetBirdProfile(const EABTSBirdId BirdId) const
{
	if (const FABTSM6BirdImpactProfile* Found = BirdImpactProfiles.FindByPredicate([BirdId](const FABTSM6BirdImpactProfile& P){ return P.BirdId == BirdId; })) return *Found;
	static const FABTSM6BirdImpactProfile Default;
	return Default;
}

const FABTSM6MaterialImpactProfile& AABTSM6SlingshotSystem::GetMaterialProfile(const EABTSM6ImpactMaterial Material) const
{
	if (const FABTSM6MaterialImpactProfile* Found = MaterialImpactProfiles.FindByPredicate([Material](const FABTSM6MaterialImpactProfile& P){ return P.Material == Material; })) return *Found;
	static const FABTSM6MaterialImpactProfile Default;
	return Default;
}

EABTSM6ImpactMaterial AABTSM6SlingshotSystem::ResolveMaterial(const UPrimitiveComponent* Component) const
{
	if (Planet.IsValid() && Component == Planet->ForestHISM) return EABTSM6ImpactMaterial::Wood;
	if (Planet.IsValid() && Component == Planet->RockHISM) return EABTSM6ImpactMaterial::Stone;
	if (Component && Component->GetOwner() && Component->GetOwner()->IsA<AABTSM71TreeHISMActor>()) return EABTSM6ImpactMaterial::Wood;
	if (Component && Component->GetOwner() && Component->GetOwner()->IsA<AABTSM71RockHISMActor>()) return EABTSM6ImpactMaterial::Stone;
	if (const AABTSM71PlaceableBrickActor* Brick = Component ? Cast<AABTSM71PlaceableBrickActor>(Component->GetOwner()) : nullptr)
	{
		switch (Brick->GetBuildingMaterial())
		{
		case EABTSM7BuildingMaterial::Stone: return EABTSM6ImpactMaterial::Stone;
		case EABTSM7BuildingMaterial::Iron: return EABTSM6ImpactMaterial::Iron;
		case EABTSM7BuildingMaterial::Glass: return EABTSM6ImpactMaterial::Glass;
		case EABTSM7BuildingMaterial::Crystal: return EABTSM6ImpactMaterial::Glass;
		default: return EABTSM6ImpactMaterial::Wood;
		}
	}
	if (const AABTSM6DestructibleProxy* Proxy = Component ? Cast<AABTSM6DestructibleProxy>(Component->GetOwner()) : nullptr) return Proxy->GetImpactMaterial();
	const AActor* ComponentOwner = Component ? Component->GetOwner() : nullptr;
	return ComponentOwner && ((Planet.IsValid() && ComponentOwner == Planet.Get())
		|| ComponentOwner->IsA<AABTSM71PhysicsTestStage>())
		? EABTSM6ImpactMaterial::Terrain : EABTSM6ImpactMaterial::Building;
}

float AABTSM6SlingshotSystem::ComputeDamageGain(
	const FABTSM6MaterialImpactProfile& MaterialProfile,
	const float NormalSpeedCMPerSec,
	const float BreakThreshold) const
{
	if (NormalSpeedCMPerSec < SignificantImpactSpeedCMPerSec) return 0.0f;
	const float NormalizedSpeed = NormalSpeedCMPerSec / FMath::Max(BreakThreshold, 1.0f);
	return MaterialProfile.DamageAtBreakSpeed * FMath::Square(FMath::Max(0.0f, NormalizedSpeed));
}

uint64 AABTSM6SlingshotSystem::GetHISMDamageKey(const UHierarchicalInstancedStaticMeshComponent& HISM, const int32 InstanceIndex) const
{
	FTransform Transform;
	if (!HISM.GetInstanceTransform(InstanceIndex, Transform, true)) return 0;
	const FVector Location = Transform.GetLocation();
	const int32 X = FMath::RoundToInt(Location.X / 5.0f);
	const int32 Y = FMath::RoundToInt(Location.Y / 5.0f);
	const int32 Z = FMath::RoundToInt(Location.Z / 5.0f);
	uint32 Hash = PointerHash(&HISM);
	Hash = HashCombineFast(Hash, GetTypeHash(X));
	Hash = HashCombineFast(Hash, GetTypeHash(Y));
	Hash = HashCombineFast(Hash, GetTypeHash(Z));
	// Instance indices change after RemoveInstance; the quantized world transform
	// is the stable identity while this HISM entry remains static.
	return static_cast<uint64>(Hash);
}

int32 AABTSM6SlingshotSystem::PromoteHISMForLaunchGravity(UHierarchicalInstancedStaticMeshComponent& HISM)
{
	TArray<int32> Indices = HISM.GetInstancesOverlappingSphere(SlingCenter, LaunchGravityActivationRadiusCM, true);
	Indices.Sort(TGreater<int32>());
	int32 PromotedCount = 0;
	const EABTSM6ImpactMaterial Material = ResolveMaterial(&HISM);
	const FABTSM6MaterialImpactProfile& Profile = GetMaterialProfile(Material);
	for (const int32 Index : Indices)
	{
		if (PromoteOrBreakHISM(HISM, Index, Material, Profile, 0.0f, FVector::ZeroVector, 0.0f, BIG_NUMBER, 0.0f))
		{
			++PromotedCount;
		}
	}
	return PromotedCount;
}

void AABTSM6SlingshotSystem::BeginLaunchGravityPhase()
{
	if (BuildingMaterialSystem.IsValid())
	{
		BuildingMaterialSystem->BeginLaunchPhysics(
			bPlanarTestMode,
			bPlanarTestMode ? PlanarUp : Planet->GetPlanetCenterWorld(),
			LaunchObjectGravityAccelerationCMPerSec2,
			LaunchContactDamageGraceSeconds);
	}

	DynamicProxies.RemoveAllSwap([](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry)
	{
		return !Entry.IsValid();
	});
	const float ActivationRadiusSquared = FMath::Square(FMath::Max(0.0f, LaunchGravityActivationRadiusCM));
	int32 ReactivatedProxyCount = 0;
	if (bPlanarTestMode)
	{
		for (TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies)
		{
			if (AABTSM6DestructibleProxy* Proxy = WeakProxy.Get())
			{
				const UStaticMeshComponent* ProxyMesh = Proxy->GetMeshComponent();
				const FVector ProxyLocation = ProxyMesh ? ProxyMesh->GetComponentLocation() : Proxy->GetActorLocation();
				if (FVector::DistSquared(ProxyLocation, SlingCenter) > ActivationRadiusSquared) continue;
				Proxy->SetContactDamageGraceSeconds(LaunchContactDamageGraceSeconds);
				Proxy->Reactivate(FVector::ZeroVector);
				++ReactivatedProxyCount;
			}
		}
	}

	int32 PromotedCount = 0;
	if (bPlanarTestMode)
	{
		for (TActorIterator<AABTSM71PlaceableHISMActor> It(GetWorld()); It; ++It)
		{
			if (UHierarchicalInstancedStaticMeshComponent* HISM = It->GetHISM()) PromotedCount += PromoteHISMForLaunchGravity(*HISM);
		}
	}
	// Production scenery stays instanced/static until a real impact promotes the
	// exact hit primitive. Pre-promoting and re-waking every tree/rock in a 60 m
	// radius accumulated hundreds of unrelated Chaos bodies across launches.
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][LaunchGravity] Planar=%d Radius=%.1f PromotedHISM=%d ExistingProxies=%d ReactivatedProxies=%d ProductionPrePromotion=ImpactOnly FrozenDebrisReactivation=Disabled ContactGrace=%.3f"),
		bPlanarTestMode ? 1 : 0, LaunchGravityActivationRadiusCM, PromotedCount,
		DynamicProxies.Num(), ReactivatedProxyCount, LaunchContactDamageGraceSeconds);
}

bool AABTSM6SlingshotSystem::PromoteOrBreakHISM(
	UHierarchicalInstancedStaticMeshComponent& HISM,
	const int32 InstanceIndex,
	const EABTSM6ImpactMaterial Material,
	const FABTSM6MaterialImpactProfile& MaterialProfile,
	const float NormalSpeedCMPerSec,
	const FVector& ImpulseDirection,
	const float KnockThreshold,
	const float BreakThreshold,
	const float AccumulatedDamage)
{
	if (InstanceIndex < 0 || InstanceIndex >= HISM.GetInstanceCount() || NormalSpeedCMPerSec < KnockThreshold) return false;
	FTransform Transform;
	if (!HISM.GetInstanceTransform(InstanceIndex, Transform, true)) return false;
	UStaticMesh* Mesh = HISM.GetStaticMesh();
	const uint64 DamageKey = GetHISMDamageKey(HISM, InstanceIndex);
	HISM.RemoveInstance(InstanceIndex);
	HISMDamageByStableKey.Remove(DamageKey);
	if (AccumulatedDamage >= MaterialProfile.BreakDamage || NormalSpeedCMPerSec >= BreakThreshold * 1.35f)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Break] Material=%d Speed=%.1f Damage=%.1f/%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec, AccumulatedDamage, MaterialProfile.BreakDamage);
		return true;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM6DestructibleProxy* Proxy = GetWorld()->SpawnActor<AABTSM6DestructibleProxy>(ProxyClass, Transform, Params);
	if (Proxy)
	{
		Proxy->SetContactDamageGraceSeconds(LaunchContactDamageGraceSeconds);
		if (bPlanarTestMode)
		{
			Proxy->ActivateProxyPlanar(Mesh, Transform, Material, MaterialProfile, ImpulseDirection.GetSafeNormal() * NormalSpeedCMPerSec * MaterialProfile.PushVelocityTransfer, PlanarUp, 980.0f, AccumulatedDamage);
		}
		else
		{
			Proxy->ActivateProxy(Mesh, Transform, Material, MaterialProfile, ImpulseDirection.GetSafeNormal() * NormalSpeedCMPerSec * MaterialProfile.PushVelocityTransfer, Planet->GetPlanetCenterWorld(), 980.0f, AccumulatedDamage);
		}
		DynamicProxies.Add(Proxy);
	}
	return true;
}

bool AABTSM6SlingshotSystem::ResolveImpactFacilityObservationAnchor(
	const FHitResult& Hit,
	FVector& OutAnchor,
	FVector& OutExtent,
	FName& OutFacilityName) const
{
	OutAnchor = FVector::ZeroVector;
	OutExtent = FVector::ZeroVector;
	OutFacilityName = NAME_None;
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent == nullptr) return false;
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding
		: RequiredBuildingActors)
	{
		const AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
		if (Building == nullptr
			|| !Building->OwnsRuntimePrimitive(HitComponent))
		{
			continue;
		}
		int32 LiveModuleCount = 0;
		if (!Building->QueryLivePresentationAnchor(
			OutAnchor,
			LiveModuleCount))
		{
			return false;
		}
		FBox FacilityBounds(EForceInit::ForceInit);
		int32 BoundsModuleCount = 0;
		if (Building->QueryLivePresentationBounds(
			FacilityBounds,
			BoundsModuleCount))
		{
			OutAnchor = FacilityBounds.GetCenter();
			OutExtent = FacilityBounds.GetExtent();
		}
		OutFacilityName = Building->GetFName();
		return true;
	}
	return false;
}

void AABTSM6SlingshotSystem::HandleBirdImpact(const FHitResult& Hit, const float NormalSpeedCMPerSec, const FVector& IncomingVelocity)
{
	if ((LaunchState != EABTSM6LaunchState::Flying && LaunchState != EABTSM6LaunchState::Settling) || !LaunchedBird.IsValid()) return;
	LaunchedBird->NotifySlingshotPresentationImpact();
	if (SlingshotCamera)
	{
		FABTSM6ImpactObservationSample Observation;
		Observation.ImpactPoint = !Hit.ImpactPoint.IsNearlyZero()
			? FVector(Hit.ImpactPoint)
			: LaunchedBird->GetActorLocation();
		Observation.ImpactNormal = Hit.ImpactNormal.GetSafeNormal();
		Observation.IncomingVelocity = IncomingVelocity;
		Observation.NormalSpeedCMPerSec = NormalSpeedCMPerSec;
		FName FacilityName = NAME_None;
		if (ResolveImpactFacilityObservationAnchor(
			Hit,
			Observation.FacilityAnchor,
			Observation.FacilityExtent,
			FacilityName))
		{
			Observation.Authority =
				EABTSM6ImpactObservationAuthority::FacilityImpact;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M6][CameraImpactObservation] FacilityHit=%s Component=%s Impact=%s Anchor=%s Extent=%s Speed=%.1f"),
				*FacilityName.ToString(),
				*GetNameSafe(Hit.GetComponent()),
				*Observation.ImpactPoint.ToCompactString(),
				*Observation.FacilityAnchor.ToCompactString(),
				*Observation.FacilityExtent.ToCompactString(),
				NormalSpeedCMPerSec);
		}
		else
		{
			Observation.Authority =
				EABTSM6ImpactObservationAuthority::SurfaceImpact;
		}
		SlingshotCamera->NotifyBirdImpact(Observation);
	}
	const bool bHitSatelliteBody =
		Hit.GetActor() == SatellitePracticeBody.Get();
	const bool bHitSatelliteTarget =
		Hit.GetActor() == SatellitePracticeTarget.Get();
	if ((bHitSatelliteBody || bHitSatelliteTarget) && SlingshotCamera)
	{
		SlingshotCamera->NotifySatelliteSurfaceContact();
	}
	if (bHitSatelliteBody || bHitSatelliteTarget)
	{
		LastSatelliteSurfaceContactWorldTimeSeconds = GetWorld()->GetTimeSeconds();
		// A swept E5 witness can be finalized immediately before Chaos reports
		// the real blocking contact. Once physical support exists, cancel that
		// fixture-only short-return timer and let the shared settle monitor own
		// the bird and any present/future E5 debris.
		CalibrationSuccessReturnRemainingSeconds = -1.0f;
		if (!bSatelliteLandingSettlementActive)
		{
			bSatelliteLandingSettlementActive = true;
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M9][Settle] SatelliteSupportAcquired Target=%s Body=%d"),
				*GetNameSafe(Hit.GetActor()),
				bHitSatelliteBody ? 1 : 0);
			if (LaunchState == EABTSM6LaunchState::Flying)
			{
				BeginSettlement();
			}
		}
	}
	// A real Chaos blocking contact is the strongest calibration evidence.
	// The rig's swept centre-segment test remains a CCD/fallback path, while
	// NotifyCalibrationTargetEvent de-duplicates both sources.
	if (bCalibrationModeEnabled)
	{
		if (bHitSatelliteTarget)
		{
			NotifyCalibrationTargetEvent(
				TEXT("Satellite.Backside"),
				false);
		}
		else if (bHitSatelliteBody)
		{
			NotifyCalibrationTargetEvent(
				TEXT("Satellite.Body"),
				true);
		}
	}
	if (NormalSpeedCMPerSec >= SignificantImpactSpeedCMPerSec) MarkPhysicsActivity();
	const EABTSM6ImpactMaterial Material = ResolveMaterial(Hit.GetComponent());
	if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
	{
		Audio->PlayImpact(Hit.ImpactPoint, Material, NormalSpeedCMPerSec);
		if (bHitSatelliteBody || bHitSatelliteTarget) Audio->SetMusicState(EABTSMusicState::Satellite);
	}
	const FABTSM6BirdImpactProfile& BirdProfile = GetBirdProfile(LaunchedBird->GetBirdId());
	const FABTSM6MaterialImpactProfile& MaterialProfile = GetMaterialProfile(Material);
	const float KnockThreshold = BirdProfile.KnockSpeedCMPerSec * MaterialProfile.KnockThresholdMultiplier;
	const float BreakThreshold = BirdProfile.BreakSpeedCMPerSec * MaterialProfile.BreakThresholdMultiplier;
	const bool bHandledByM7 = BuildingMaterialSystem.IsValid()
		&& BuildingMaterialSystem->HandleBirdImpact(Hit.GetComponent(), Hit.Item, NormalSpeedCMPerSec, IncomingVelocity, LaunchedBird->GetBirdId());
	if (!bHandledByM7)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Hit.GetComponent()))
		{
			const uint64 DamageKey = GetHISMDamageKey(*HISM, Hit.Item);
			const float DamageBefore = HISMDamageByStableKey.FindRef(DamageKey);
			const float DamageAfter = DamageBefore + ComputeDamageGain(MaterialProfile, NormalSpeedCMPerSec, BreakThreshold);
			HISMDamageByStableKey.Add(DamageKey, DamageAfter);
			if (DamageAfter >= MaterialProfile.BreakDamage && NormalSpeedCMPerSec < KnockThreshold)
			{
				HISM->RemoveInstance(Hit.Item);
				HISMDamageByStableKey.Remove(DamageKey);
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][HISMDamageBreak] Material=%d Damage=%.1f/%.1f"), static_cast<int32>(Material), DamageAfter, MaterialProfile.BreakDamage);
			}
			else
			{
				PromoteOrBreakHISM(*HISM, Hit.Item, Material, MaterialProfile, NormalSpeedCMPerSec, IncomingVelocity, KnockThreshold, BreakThreshold, DamageAfter);
			}
		}
		else if (AABTSM6DestructibleProxy* Proxy = Cast<AABTSM6DestructibleProxy>(Hit.GetActor()))
		{
			const bool bBroken = Proxy->ApplyImpactDamage(ComputeDamageGain(MaterialProfile, NormalSpeedCMPerSec, BreakThreshold));
			if (bBroken || NormalSpeedCMPerSec >= BreakThreshold * 1.35f)
			{
				Proxy->Shatter();
				DynamicProxies.RemoveAllSwap([Proxy](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry){ return !Entry.IsValid() || Entry.Get() == Proxy; });
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][ProxyBreak] Material=%d Speed=%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec);
			}
			else if (NormalSpeedCMPerSec >= KnockThreshold)
			{
				Proxy->Reactivate(IncomingVelocity.GetSafeNormal() * NormalSpeedCMPerSec * MaterialProfile.PushVelocityTransfer);
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][ProxyDamaged] Material=%d Speed=%.1f Damage=%.1f/%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec, Proxy->GetCurrentDamage(), Proxy->GetBreakDamage());
			}
		}
	}
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	const FVector Tangent = FVector::VectorPlaneProject(IncomingVelocity, Normal);
	LaunchedBird->SetSlingshotVelocity(Tangent * MaterialProfile.BirdSpeedRetention + Normal * NormalSpeedCMPerSec * MaterialProfile.BirdRestitution);
	if (LaunchedBird->GetBirdId() == EABTSBirdId::Black && !bBlackDetonated && BlackFuseRemainingSeconds < 0.0f && NormalSpeedCMPerSec >= SignificantImpactSpeedCMPerSec)
	{
		BlackFuseRemainingSeconds = BlackAutoFuseSeconds;
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Impact] Bird=%d Material=%d NormalSpeed=%.1f Knock=%.1f Break=%.1f Item=%d"), ABTSBirdIdToIndex(LaunchedBird->GetBirdId()), static_cast<int32>(Material), NormalSpeedCMPerSec, KnockThreshold, BreakThreshold, Hit.Item);
}

void AABTSM6SlingshotSystem::HandleProxyImpact(AABTSM6DestructibleProxy& Proxy, const FHitResult& Hit, const float NormalSpeedCMPerSec)
{
	if (LaunchState != EABTSM6LaunchState::Flying && LaunchState != EABTSM6LaunchState::Settling) return;
	if (NormalSpeedCMPerSec >= SignificantImpactSpeedCMPerSec) MarkPhysicsActivity();
	if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Hit.GetComponent()))
	{
		const EABTSM6ImpactMaterial TargetMaterial = ResolveMaterial(HISM);
		const FABTSM6MaterialImpactProfile& TargetProfile = GetMaterialProfile(TargetMaterial);
		const float Damage = ComputeDamageGain(TargetProfile, NormalSpeedCMPerSec, ProxyChainBreakSpeedCMPerSec);
		PromoteOrBreakHISM(*HISM, Hit.Item, TargetMaterial, TargetProfile, NormalSpeedCMPerSec, Proxy.GetMeshComponent()->GetPhysicsLinearVelocity(), ProxyChainBreakSpeedCMPerSec * 0.65f, ProxyChainBreakSpeedCMPerSec, Damage);
	}
	if (NormalSpeedCMPerSec >= ProxyChainBreakSpeedCMPerSec) Proxy.Shatter();
}

bool AABTSM6SlingshotSystem::TryManualBlackDetonation(AActor* ClickedActor)
{
	if ((LaunchState != EABTSM6LaunchState::Flying && LaunchState != EABTSM6LaunchState::Settling)
		|| !LaunchedBird.IsValid() || ClickedActor != LaunchedBird.Get()
		|| LaunchedBird->GetBirdId() != EABTSBirdId::Black || bBlackDetonated) return false;
	DetonateBlackBird(true);
	return true;
}

void AABTSM6SlingshotSystem::DetonateBlackBird(const bool bManual)
{
	if (!LaunchedBird.IsValid() || bBlackDetonated) return;
	bBlackDetonated = true;
	if (UABTSAudioWorldSubsystem* Audio = GetWorld()->GetSubsystem<UABTSAudioWorldSubsystem>())
	{
		Audio->PlayExplosion(LaunchedBird->GetActorLocation(), true);
	}
	MarkPhysicsActivity();
	int32 BrokenInstances = 0;
	int32 ImpulsedInstances = 0;
	TArray<UHierarchicalInstancedStaticMeshComponent*> ExplosionHISMs;
	if (Planet.IsValid())
	{
		ExplosionHISMs.Add(Planet->ForestHISM.Get());
		ExplosionHISMs.Add(Planet->RockHISM.Get());
	}
	if (bPlanarTestMode)
	{
		for (TActorIterator<AABTSM71PlaceableHISMActor> It(GetWorld()); It; ++It) ExplosionHISMs.Add(It->GetHISM());
	}
	for (UHierarchicalInstancedStaticMeshComponent* HISM : ExplosionHISMs)
	{
		if (HISM == nullptr) continue;
		TArray<int32> Indices = HISM->GetInstancesOverlappingSphere(LaunchedBird->GetActorLocation(), BlackExplosionImpulseRadiusCM, true);
		Indices.Sort(TGreater<int32>());
		for (const int32 Index : Indices)
		{
			FTransform Transform;
			if (!HISM->GetInstanceTransform(Index, Transform, true)) continue;
			const FVector Delta = Transform.GetLocation() - LaunchedBird->GetActorLocation();
			if (Delta.Size() <= BlackExplosionRadiusCM)
			{
				if (HISM->RemoveInstance(Index)) ++BrokenInstances;
			}
			else
			{
				const EABTSM6ImpactMaterial Type = ResolveMaterial(HISM);
				const float Speed = BlackExplosionImpulseSpeedCMPerSec * (1.0f - Delta.Size() / FMath::Max(BlackExplosionImpulseRadiusCM, 1.0f));
				const FABTSM6MaterialImpactProfile& Profile = GetMaterialProfile(Type);
				if (PromoteOrBreakHISM(*HISM, Index, Type, Profile, Speed, Delta, 0.0f, BIG_NUMBER, 0.0f)) ++ImpulsedInstances;
			}
		}
	}
	int32 BrokenProxies = 0;
	for (int32 Index = DynamicProxies.Num() - 1; Index >= 0; --Index)
	{
		AABTSM6DestructibleProxy* Proxy = DynamicProxies[Index].Get();
		if (Proxy == nullptr) { DynamicProxies.RemoveAtSwap(Index); continue; }
		const FVector Delta = Proxy->GetActorLocation() - LaunchedBird->GetActorLocation();
		if (Delta.SizeSquared() <= FMath::Square(BlackExplosionRadiusCM))
		{
			Proxy->Shatter(); DynamicProxies.RemoveAtSwap(Index); ++BrokenProxies;
		}
		else if (Delta.SizeSquared() <= FMath::Square(BlackExplosionImpulseRadiusCM))
		{
			const float Speed = BlackExplosionImpulseSpeedCMPerSec * (1.0f - Delta.Size() / BlackExplosionImpulseRadiusCM);
			Proxy->Reactivate(Delta.GetSafeNormal() * Speed);
		}
	}
	if (BuildingMaterialSystem.IsValid()) BuildingMaterialSystem->ApplyRadialBlast(LaunchedBird->GetActorLocation(), BlackExplosionRadiusCM, BlackExplosionImpulseRadiusCM, BlackExplosionImpulseSpeedCMPerSec);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][BlackExplosion] Manual=%d DestroyRadius=%.1f ImpulseRadius=%.1f HISM=%d Impulsed=%d Proxies=%d"), bManual ? 1 : 0, BlackExplosionRadiusCM, BlackExplosionImpulseRadiusCM, BrokenInstances, ImpulsedInstances, BrokenProxies);
}

void AABTSM6SlingshotSystem::GatherActiveSlingshotPrimitives(
	TArray<UPrimitiveComponent*>& OutPrimitives) const
{
	OutPrimitives.Reset();
	const AABTSM51SlingshotCord* Cord = ActiveCord.Get();
	if (!IsValid(Cord) || !IsLaunchModeActive())
	{
		return;
	}

	auto AppendActorPrimitives = [&OutPrimitives](const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return;
		}
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor->GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsValid(Component))
			{
				OutPrimitives.AddUnique(Component);
			}
		}
	};

	AppendActorPrimitives(Cord);
	AppendActorPrimitives(Cord->GetStakeA());
	AppendActorPrimitives(Cord->GetStakeB());
	if (IsValid(PouchVisualMesh) && PouchVisualMesh->IsVisible())
	{
		OutPrimitives.AddUnique(PouchVisualMesh);
	}
}
