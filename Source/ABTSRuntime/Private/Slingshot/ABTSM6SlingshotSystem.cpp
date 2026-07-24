// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSChaosBirdMovementComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
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
	ProxyClass = AABTSM6DestructibleProxy::StaticClass();
	CameraClass = AABTSM6SlingshotCamera::StaticClass();

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
			default: break;
			}
			ApplyStaticPhysics(It->GetHISM(), Material, TEXT("ABTSPlanarBrickImpactPhysics"));
		}
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SlingshotCamera = GetWorld()->SpawnActor<AABTSM6SlingshotCamera>(CameraClass, FTransform::Identity, Params);
	SpawnDebugSlingshots();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6] System ready Camera=%d BirdProfiles=%d MaterialProfiles=%d"), SlingshotCamera ? 1 : 0, BirdImpactProfiles.Num(), MaterialImpactProfiles.Num());
}

void AABTSM6SlingshotSystem::ConfigureDebugSlingshots(const bool bEnable, const int32 InStartCellId)
{
	bSpawnDebugSlingshotsAtStart = bEnable;
	DebugStartCellId = InStartCellId;
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
	if (bSpawnDebugSlingshotsAtStart && !bDebugSlingshotsSpawned) SpawnDebugSlingshots();
	if (LaunchState == EABTSM6LaunchState::Ready || LaunchState == EABTSM6LaunchState::Pulling)
	{
		UpdatePouchAndPreview();
		DrawPredictedTrajectory();
	}
	else if (LaunchState == EABTSM6LaunchState::Flying || LaunchState == EABTSM6LaunchState::Settling)
	{
		FlightElapsedSeconds += DeltaSeconds;
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
	const FVector Center = Planet->GetPlanetCenterWorld();
	const FVector Up = CenterDirection.GetSafeNormal();
	const FVector Forward = FVector::VectorPlaneProject(LaunchDirection, Up).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	if (Forward.IsNearlyZero() || Right.IsNearlyZero()) return false;
	const float SurfaceRadius = Planet->GetSurfaceRadiusAtDirection(Up);
	const FVector StakeDirectionA = (Up * SurfaceRadius - Right * 105.0f).GetSafeNormal();
	const FVector StakeDirectionB = (Up * SurfaceRadius + Right * 105.0f).GetSafeNormal();
	FTransform TransformA, TransformB;
	if (!QueryDebugSurfaceTransform(StakeDirectionA, Forward, 58.0f, TransformA)
		|| !QueryDebugSurfaceTransform(StakeDirectionB, Forward, 58.0f, TransformB)) return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM51SlingshotStake* StakeA = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(AABTSM51SlingshotStake::StaticClass(), TransformA, Params);
	AABTSM51SlingshotStake* StakeB = GetWorld()->SpawnActor<AABTSM51SlingshotStake>(AABTSM51SlingshotStake::StaticClass(), TransformB, Params);
	if (StakeA == nullptr || StakeB == nullptr) return false;
	StakeA->InitializeStake(StakeItem, INDEX_NONE, StakeDirectionA);
	StakeB->InitializeStake(StakeItem, INDEX_NONE, StakeDirectionB);
	StakeA->SetHasCord(true);
	StakeB->SetHasCord(true);
	const FVector EndpointA = StakeA->GetActorLocation() + StakeDirectionA * 80.0f;
	const FVector EndpointB = StakeB->GetActorLocation() + StakeDirectionB * 80.0f;
	AABTSM51SlingshotCord* Cord = GetWorld()->SpawnActor<AABTSM51SlingshotCord>(AABTSM51SlingshotCord::StaticClass(), FTransform::Identity, Params);
	if (Cord == nullptr) return false;
	Cord->InitializeCord(StakeA, StakeB, EndpointA, EndpointB);
	return true;
}

void AABTSM6SlingshotSystem::SpawnDebugSlingshots()
{
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
	if (LaunchState != EABTSM6LaunchState::Inactive || !ResolveDependencies()) return false;
	AABTSM25BirdCharacter* Bird = Party->GetControlledBird();
	if (Bird == nullptr || !IsBirdAllowed(*Bird, Cord))
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M6][Enter] Rejected Bird=%d Stake=%s"), Bird ? ABTSBirdIdToIndex(Bird->GetBirdId()) : -1, *ABTSGetItemFallbackLabel(Cord.GetStakeItem()));
		return false;
	}
	ActiveCord = &Cord;
	LaunchedBird = Bird;
	BuildLaunchFrame(Cord, *Bird);
	ConfigurePouchVisual(Cord);
	Party->SetSlingshotMode(true);
	ArrangeWaitingBirds();
	const FQuat RestPouchRotation = Cord.GetRestPouchTransform().GetRotation();
	Bird->EnterSlingshotPouch(GetBirdInPouchLocation(RestPouchRotation), RestPouchRotation);
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
	PullAlpha = 0.55f;
	AimPlaneOffset = FVector::ZeroVector;
	LaunchState = EABTSM6LaunchState::Ready;
	if (SlingshotCamera) SlingshotCamera->SetAimFrame(SlingCenter, SlingForward, SlingUp);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Enter] Bird=%d Reinforced=%d"), ABTSBirdIdToIndex(Bird->GetBirdId()), Cord.GetStakeItem() == EABTSItemId::ReinforcedStake ? 1 : 0);
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
		? FMath::Lerp(MinPullDistanceCM, MaxPullDistanceCM, PullAlpha)
		: 0.0f;
	const FVector PulledPlaneCenter = RestPouchLocation - SlingForward * PullDistance;
	const float Distance = FVector::DotProduct(PulledPlaneCenter - RayOrigin, PlaneNormal) / Denominator;
	if (Distance <= 0.0f) return;
	AimPlaneOffset = (RayOrigin + RayDirection * Distance - PulledPlaneCenter).GetClampedToMaxSize(MaxAimPlaneOffsetCM);
}

void AABTSM6SlingshotSystem::AdjustPullPower(const float MouseWheelValue)
{
	if (LaunchState != EABTSM6LaunchState::Ready && LaunchState != EABTSM6LaunchState::Pulling) return;
	// UE wheel axis is positive upward. Design contract: wheel down increases power.
	PullAlpha = FMath::Clamp(PullAlpha - MouseWheelValue * PullPowerWheelStep, 0.0f, 1.0f);
}

void AABTSM6SlingshotSystem::UpdatePouchAndPreview()
{
	if (!LaunchedBird.IsValid()) return;
	const float PullDistance = FMath::Lerp(MinPullDistanceCM, MaxPullDistanceCM, PullAlpha);
	PouchLocation = RestPouchLocation + AimPlaneOffset - SlingForward * PullDistance;
	const FVector Direction = (SlingCenter + SlingUp * 65.0f - PouchLocation).GetSafeNormal();
	const FQuat PouchRotation = MakePulledPouchRotation(Direction, SlingRight);
	LaunchedBird->SetActorLocationAndRotation(
		GetBirdInPouchLocation(PouchRotation), PouchRotation, false, nullptr, ETeleportType::TeleportPhysics);
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

FVector AABTSM6SlingshotSystem::ComputeLaunchVelocity() const
{
	const FVector Direction = (SlingCenter + SlingUp * 65.0f - PouchLocation).GetSafeNormal();
	return Direction * FMath::Lerp(MinLaunchSpeedCMPerSec, MaxLaunchSpeedCMPerSec, PullAlpha);
}

void AABTSM6SlingshotSystem::DrawPredictedTrajectory() const
{
	if ((!bPlanarTestMode && !Planet.IsValid()) || LaunchState != EABTSM6LaunchState::Pulling) return;
	FVector Position = PouchLocation;
	FVector Velocity = ComputeLaunchVelocity();
	const FVector Center = bPlanarTestMode ? FVector::ZeroVector : Planet->GetPlanetCenterWorld();
	const float Mu = bPlanarTestMode ? 0.0f : 980.0f * FMath::Square(Planet->GetPlanetRadiusCM());
	for (int32 Index = 0; Index < TrajectorySampleCount; ++Index)
	{
		if ((Index & 1) == 0) DrawDebugPoint(GetWorld(), Position, TrajectoryPointSize, FColor(176, 224, 255), false, 0.0f, 0);
		const FVector ToCenter = Center - Position;
		const float Radius = FMath::Max(ToCenter.Size(), 1.0f);
		const FVector Gravity = bPlanarTestMode
			? -PlanarUp * 980.0f
			: ToCenter / Radius * (Mu / FMath::Square(Radius));
		const FVector Acceleration = Gravity - Velocity * FlightAirDragPerSecond;
		Velocity += Acceleration * TrajectoryStepSeconds;
		Position += Velocity * TrajectoryStepSeconds;
	}
}

void AABTSM6SlingshotSystem::ReleaseLaunch()
{
	if (LaunchState != EABTSM6LaunchState::Pulling || !LaunchedBird.IsValid()) return;
	const FVector Velocity = ComputeLaunchVelocity();
	SetPouchVisualActive(false);
	if (ActiveCord.IsValid()) ActiveCord->ResetPouchVisualToRest();
	// Ready/Pulling are aiming-only states. Scene objects must remain static
	// until the bird actually leaves the pouch.
	LaunchState = EABTSM6LaunchState::Flying;
	LaunchedBird->LaunchFromSlingshot(Velocity, FlightAirDragPerSecond);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Launch] Bird=%d Speed=%.1f Pull=%.2f"), ABTSBirdIdToIndex(LaunchedBird->GetBirdId()), Velocity.Size(), PullAlpha);
	BeginLaunchGravityPhase();
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

	for (TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies)
	{
		if (AABTSM6DestructibleProxy* Proxy = WeakProxy.Get())
		{
			Proxy->SetContactDamageGraceSeconds(LaunchContactDamageGraceSeconds);
			Proxy->Reactivate(FVector::ZeroVector);
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
	else if (Planet.IsValid())
	{
		PromotedCount += PromoteHISMForLaunchGravity(*Planet->ForestHISM);
		PromotedCount += PromoteHISMForLaunchGravity(*Planet->RockHISM);
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][LaunchGravity] Planar=%d Radius=%.1f PromotedHISM=%d ExistingProxies=%d ContactGrace=%.3f"),
		bPlanarTestMode ? 1 : 0, LaunchGravityActivationRadiusCM, PromotedCount, DynamicProxies.Num(), LaunchContactDamageGraceSeconds);
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

void AABTSM6SlingshotSystem::HandleBirdImpact(const FHitResult& Hit, const float NormalSpeedCMPerSec, const FVector& IncomingVelocity)
{
	if ((LaunchState != EABTSM6LaunchState::Flying && LaunchState != EABTSM6LaunchState::Settling) || !LaunchedBird.IsValid()) return;
	if (NormalSpeedCMPerSec >= SignificantImpactSpeedCMPerSec) MarkPhysicsActivity();
	const EABTSM6ImpactMaterial Material = ResolveMaterial(Hit.GetComponent());
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
