// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Camera/ABTSM6SlingshotCamera.h"
#include "Components/CapsuleComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Movement/ABTSRadialForceMovementComponent.h"
#include "Movement/ABTSChaosBirdMovementComponent.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Player/ABTSM6PlayerController.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "World/ABTSM51WorldActors.h"

AABTSM6SlingshotSystem::AABTSM6SlingshotSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
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
	AddMaterial(EABTSM6ImpactMaterial::Building, 1.0f, 1.0f, 0.55f, 0.12f);
}

void AABTSM6SlingshotSystem::BeginPlay()
{
	Super::BeginPlay();
	ResolveDependencies();
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
	else if (LaunchState == EABTSM6LaunchState::Flying)
	{
		FlightElapsedSeconds += DeltaSeconds;
		QuietElapsedSeconds += DeltaSeconds;
		if (BlackFuseRemainingSeconds >= 0.0f && !bBlackDetonated)
		{
			BlackFuseRemainingSeconds -= DeltaSeconds;
			if (BlackFuseRemainingSeconds <= 0.0f) DetonateBlackBird(false);
		}
		if (LaunchedBird.IsValid() && FlightElapsedSeconds > 1.0f && LaunchedBird->IsRadiallyGrounded() && QuietElapsedSeconds >= PostLandingQuietSeconds)
		{
			BeginReturn();
		}
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
	Party->SetSlingshotMode(true);
	ArrangeWaitingBirds();
	Bird->EnterSlingshotPouch(RestPouchLocation, FRotationMatrix::MakeFromXZ(SlingForward, SlingUp).ToQuat());
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
	RestPouchLocation = SlingCenter - SlingForward * 115.0f + SlingUp * 42.0f;
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
	const float PullDistance = FMath::Lerp(MinPullDistanceCM, MaxPullDistanceCM, PullAlpha);
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
	LaunchedBird->SetActorLocationAndRotation(PouchLocation, FRotationMatrix::MakeFromXZ(Direction, SlingUp).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
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
	LaunchedBird->LaunchFromSlingshot(Velocity, FlightAirDragPerSecond);
	LaunchState = EABTSM6LaunchState::Flying;
	FlightElapsedSeconds = 0.0f;
	QuietElapsedSeconds = 0.0f;
	BlackFuseRemainingSeconds = -1.0f;
	bBlackDetonated = false;
	if (SlingshotCamera)
	{
		if (bPlanarTestMode) SlingshotCamera->FollowBirdPlanar(LaunchedBird.Get(), PlanarUp);
		else SlingshotCamera->FollowBird(LaunchedBird.Get(), Planet.Get());
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Launch] Bird=%d Speed=%.1f Pull=%.2f"), ABTSBirdIdToIndex(LaunchedBird->GetBirdId()), Velocity.Size(), PullAlpha);
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
	if (const AABTSM6DestructibleProxy* Proxy = Component ? Cast<AABTSM6DestructibleProxy>(Component->GetOwner()) : nullptr) return Proxy->GetImpactMaterial();
	const AActor* ComponentOwner = Component ? Component->GetOwner() : nullptr;
	return ComponentOwner && ((Planet.IsValid() && ComponentOwner == Planet.Get())
		|| ComponentOwner->IsA<AABTSM71PhysicsTestStage>())
		? EABTSM6ImpactMaterial::Terrain : EABTSM6ImpactMaterial::Building;
}

bool AABTSM6SlingshotSystem::PromoteOrBreakHISM(
	UHierarchicalInstancedStaticMeshComponent& HISM,
	const int32 InstanceIndex,
	const EABTSM6ImpactMaterial Material,
	const float NormalSpeedCMPerSec,
	const FVector& ImpulseDirection,
	const float KnockThreshold,
	const float BreakThreshold)
{
	if (InstanceIndex < 0 || InstanceIndex >= HISM.GetInstanceCount() || NormalSpeedCMPerSec < KnockThreshold) return false;
	FTransform Transform;
	if (!HISM.GetInstanceTransform(InstanceIndex, Transform, true)) return false;
	UStaticMesh* Mesh = HISM.GetStaticMesh();
	HISM.RemoveInstance(InstanceIndex);
	if (NormalSpeedCMPerSec >= BreakThreshold)
	{
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Break] Material=%d Speed=%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec);
		return true;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM6DestructibleProxy* Proxy = GetWorld()->SpawnActor<AABTSM6DestructibleProxy>(ProxyClass, Transform, Params);
	if (Proxy)
	{
		if (bPlanarTestMode)
		{
			Proxy->ActivateProxyPlanar(Mesh, Transform, Material, ImpulseDirection.GetSafeNormal() * NormalSpeedCMPerSec * 0.72f, PlanarUp, 980.0f);
		}
		else
		{
			Proxy->ActivateProxy(Mesh, Transform, Material, ImpulseDirection.GetSafeNormal() * NormalSpeedCMPerSec * 0.72f, Planet->GetPlanetCenterWorld(), 980.0f);
		}
		DynamicProxies.Add(Proxy);
	}
	return true;
}

void AABTSM6SlingshotSystem::HandleBirdImpact(const FHitResult& Hit, const float NormalSpeedCMPerSec, const FVector& IncomingVelocity)
{
	if (LaunchState != EABTSM6LaunchState::Flying || !LaunchedBird.IsValid()) return;
	if (NormalSpeedCMPerSec >= SignificantImpactSpeedCMPerSec) QuietElapsedSeconds = 0.0f;
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
			PromoteOrBreakHISM(*HISM, Hit.Item, Material, NormalSpeedCMPerSec, IncomingVelocity, KnockThreshold, BreakThreshold);
		}
		else if (AABTSM6DestructibleProxy* Proxy = Cast<AABTSM6DestructibleProxy>(Hit.GetActor()))
		{
			if (NormalSpeedCMPerSec >= BreakThreshold)
			{
				Proxy->Shatter();
				DynamicProxies.RemoveAllSwap([Proxy](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry){ return !Entry.IsValid() || Entry.Get() == Proxy; });
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][ProxyBreak] Material=%d Speed=%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec);
			}
			else if (NormalSpeedCMPerSec >= KnockThreshold)
			{
				Proxy->Reactivate(IncomingVelocity.GetSafeNormal() * NormalSpeedCMPerSec * 0.72f);
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][ProxyReactivated] Material=%d Speed=%.1f"), static_cast<int32>(Material), NormalSpeedCMPerSec);
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
	if (LaunchState != EABTSM6LaunchState::Flying) return;
	if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Hit.GetComponent()))
	{
		const EABTSM6ImpactMaterial TargetMaterial = ResolveMaterial(HISM);
		PromoteOrBreakHISM(*HISM, Hit.Item, TargetMaterial, NormalSpeedCMPerSec, Proxy.GetMeshComponent()->GetPhysicsLinearVelocity(), ProxyChainBreakSpeedCMPerSec * 0.65f, ProxyChainBreakSpeedCMPerSec);
	}
	if (NormalSpeedCMPerSec >= ProxyChainBreakSpeedCMPerSec) Proxy.Shatter();
}

bool AABTSM6SlingshotSystem::TryManualBlackDetonation(AActor* ClickedActor)
{
	if (LaunchState != EABTSM6LaunchState::Flying || !LaunchedBird.IsValid() || ClickedActor != LaunchedBird.Get() || LaunchedBird->GetBirdId() != EABTSBirdId::Black || bBlackDetonated) return false;
	DetonateBlackBird(true);
	return true;
}

void AABTSM6SlingshotSystem::DetonateBlackBird(const bool bManual)
{
	if (!LaunchedBird.IsValid() || bBlackDetonated) return;
	bBlackDetonated = true;
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
				if (PromoteOrBreakHISM(*HISM, Index, Type, Speed, Delta, 0.0f, BIG_NUMBER)) ++ImpulsedInstances;
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

void AABTSM6SlingshotSystem::FreezeDynamicProxies()
{
	for (TWeakObjectPtr<AABTSM6DestructibleProxy>& WeakProxy : DynamicProxies) if (AABTSM6DestructibleProxy* Proxy = WeakProxy.Get()) Proxy->Freeze();
	if (BuildingMaterialSystem.IsValid()) BuildingMaterialSystem->FreezeDynamicModules();
}

void AABTSM6SlingshotSystem::BeginReturn()
{
	if (!LaunchedBird.IsValid()) return;
	FreezeDynamicProxies();
	LaunchedBird->BeginSlingshotReturn();
	ReturnStartLocation = LaunchedBird->GetActorLocation();
	const FVector ApproxTarget = SlingCenter - SlingForward * 230.0f;
	if (bPlanarTestMode)
	{
		ReturnTargetLocation = ApproxTarget;
		const float DesiredHeight = LaunchedBird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f;
		ReturnTargetLocation += PlanarUp * (DesiredHeight - FVector::DotProduct(ReturnTargetLocation - PlanarOrigin, PlanarUp));
	}
	else
	{
		const FVector Direction = (ApproxTarget - Planet->GetPlanetCenterWorld()).GetSafeNormal();
		ReturnTargetLocation = Planet->GetPlanetCenterWorld() + Direction * (Planet->GetSurfaceRadiusAtDirection(Direction) + LaunchedBird->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.0f);
	}
	ReturnElapsedSeconds = 0.0f;
	LaunchState = EABTSM6LaunchState::Returning;
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Return] Begin FlightSeconds=%.2f Proxies=%d"), FlightElapsedSeconds, DynamicProxies.Num());
}

void AABTSM6SlingshotSystem::UpdateReturn(const float DeltaSeconds)
{
	if (!LaunchedBird.IsValid()) { FinishReturn(); return; }
	ReturnElapsedSeconds += DeltaSeconds;
	const float Alpha = FMath::Clamp(ReturnElapsedSeconds / FMath::Max(ReturnDurationSeconds, 0.1f), 0.0f, 1.0f);
	if (bPlanarTestMode)
	{
		const FVector Location = FMath::Lerp(ReturnStartLocation, ReturnTargetLocation, FMath::SmoothStep(0.0f, 1.0f, Alpha))
			+ PlanarUp * (FMath::Sin(Alpha * PI) * 280.0f);
		LaunchedBird->SetActorLocationAndRotation(Location, FRotationMatrix::MakeFromXZ(SlingForward, PlanarUp).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
		if (Alpha >= 1.0f) FinishReturn();
		return;
	}
	const FVector Center = Planet->GetPlanetCenterWorld();
	const FVector StartOffset = ReturnStartLocation - Center;
	const FVector EndOffset = ReturnTargetLocation - Center;
	const FQuat Arc = FQuat::FindBetweenNormals(StartOffset.GetSafeNormal(), EndOffset.GetSafeNormal());
	const FVector Direction = FQuat::Slerp(FQuat::Identity, Arc, FMath::SmoothStep(0.0f, 1.0f, Alpha)).RotateVector(StartOffset.GetSafeNormal()).GetSafeNormal();
	const float Radius = FMath::Lerp(StartOffset.Size(), EndOffset.Size(), Alpha) + FMath::Sin(Alpha * PI) * 280.0f;
	LaunchedBird->SetActorLocationAndRotation(Center + Direction * Radius, FRotationMatrix::MakeFromXZ(SlingForward, Direction).ToQuat(), false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha >= 1.0f) FinishReturn();
}

void AABTSM6SlingshotSystem::FinishReturn()
{
	if (LaunchedBird.IsValid())
	{
		if (UABTSRadialForceMovementComponent* Movement = LaunchedBird->GetForceMovementComponent()) Movement->OnBlockingImpact().RemoveAll(this);
		if (UABTSChaosBirdMovementComponent* Movement = LaunchedBird->GetChaosMovementComponent()) Movement->OnBlockingImpact().RemoveAll(this);
		LaunchedBird->FinishSlingshotReturn();
	}
	if (Party.IsValid()) Party->SetSlingshotMode(false);
	if (AABTSM6PlayerController* PC = Cast<AABTSM6PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->SetLaunchModeInputBlocked(false);
		PC->RestorePartyCameraView();
	}
	LaunchState = EABTSM6LaunchState::Inactive;
	ActiveCord.Reset();
	LaunchedBird.Reset();
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M6][Return] Complete StaticProxies=%d"), DynamicProxies.Num());
}
