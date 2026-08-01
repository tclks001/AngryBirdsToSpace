// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"

#include "ABTSRuntime.h"
#include "Calibration/ABTSCalibrationTargetProxy.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM3MonthlySatellitePracticeRuntimePrivate
{
constexpr TCHAR SatelliteGravityCVarName[] =
	TEXT("abts.Calibration.SatelliteGravity");

uint64 AddHashValue(uint64 Hash, const uint64 Value)
{
	for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
	{
		Hash ^= static_cast<uint8>((Value >> (ByteIndex * 8)) & 0xffull);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

uint64 ComputeRuntimeLayoutSnapshotHash(
	const int64 PreviewResultHash,
	const int64 CandidateHash,
	const uint64 GravitySnapshotHash)
{
	uint64 Hash = 14695981039346656037ull;
	Hash = AddHashValue(Hash, static_cast<uint64>(PreviewResultHash));
	Hash = AddHashValue(Hash, static_cast<uint64>(CandidateHash));
	Hash = AddHashValue(Hash, GravitySnapshotHash);
	return Hash;
}

bool HasPawnBlockingCollision(const AActor& Actor)
{
	TInlineComponentArray<UPrimitiveComponent*> Components;
	Actor.GetComponents(Components);
	for (const UPrimitiveComponent* Component : Components)
	{
		if (IsValid(Component)
			&& Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision
			&& Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
		{
			return true;
		}
	}
	return false;
}
}

AABTSM3MonthlySatellitePracticeRuntime::
	AABTSM3MonthlySatellitePracticeRuntime()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

bool AABTSM3MonthlySatellitePracticeRuntime::Configure(
	AABTSM3Planet& InPrimaryPlanet,
	const FABTSM3MonthlySatellitePreviewCandidate& InCandidate,
	const int64 InPreviewResultHash)
{
	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenSatellitePracticePresetV0();
	const FABTSM6LaunchProfileCatalog FrozenCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	const int64 ExpectedCandidateHash = static_cast<int64>(
		FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(
			InCandidate));
	const int64 ExpectedLaunchProfileHash = static_cast<int64>(
		FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
			FrozenCatalog));
	const int64 ExpectedPresetHash = static_cast<int64>(
		FABTSSlingshotSatelliteCalibrationModel::
			ComputeSatellitePracticePresetHash(FrozenPreset));
	if (!InPrimaryPlanet.IsPlanetReady()
		|| InPreviewResultHash == 0
		|| InCandidate.SourceRouteCandidateId == INDEX_NONE
		|| InCandidate.CandidateHash != ExpectedCandidateHash
		|| InCandidate.LaunchProfileHash != ExpectedLaunchProfileHash
		|| InCandidate.SatellitePracticePresetVersion != FrozenPreset.Version
		|| InCandidate.SatellitePracticePresetHash != ExpectedPresetHash
		|| InCandidate.SatelliteRadiusCM <= 0.0f
		|| InCandidate.SatelliteSurfaceGravityCMPerSec2 <= 0.0f
		|| InCandidate.E5TargetHalfExtentCM.GetMin() <= 0.0f)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] ConfigureRejected Candidate=%d CandidateHash=%016llX ExpectedCandidateHash=%016llX PreviewResultHash=%016llX"),
			InCandidate.SourceRouteCandidateId,
			static_cast<unsigned long long>(
				static_cast<uint64>(InCandidate.CandidateHash)),
			static_cast<unsigned long long>(
				static_cast<uint64>(ExpectedCandidateHash)),
			static_cast<unsigned long long>(
				static_cast<uint64>(InPreviewResultHash)));
		return false;
	}

	PrimaryPlanet = &InPrimaryPlanet;
	CandidateSnapshot = InCandidate;
	SourcePreviewResultHash = InPreviewResultHash;
	bConfigured = true;
	return true;
}

bool AABTSM3MonthlySatellitePracticeRuntime::ActivateSnapshot()
{
	if (bRuntimeActorsSpawned)
	{
		BindM6Target();
		ApplyGravityOverride(false);
		RefreshReadyState();
		return bRuntimeReady;
	}
	if (!bConfigured || !IsValid(PrimaryPlanet) || GetWorld() == nullptr)
	{
		return false;
	}

	// The explicit monthly preview supersedes M9's legacy TaskGraph satellite.
	// Keeping both would double-count gravity and leave the F7 wireframe detached
	// from the collision body used by M6.
	TArray<AABTSM9Satellite*> SupersededSatellites;
	for (TActorIterator<AABTSM9Satellite> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && *It != RuntimeSatellite.Get())
		{
			SupersededSatellites.Add(*It);
		}
	}
	for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
	{
		It->ClearSatellitePracticeTarget(nullptr);
	}
	for (AABTSM9Satellite* Satellite : SupersededSatellites)
	{
		Satellite->Destroy();
	}

	if (!SpawnSnapshotActors())
	{
		ClearOwnedRuntime();
		return false;
	}
	if (!SpawnPracticeSlingshot())
	{
		ClearOwnedRuntime();
		return false;
	}
	bRuntimeActorsSpawned = true;
	BindM6Target();
	ApplyGravityOverride(true);
	RefreshReadyState();

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice] Ready=%d Candidate=%d ReplacedLegacySatellites=%d SatelliteCenter=%s Radius=%.1f Gravity=%.1f E5Center=%s E5HalfExtent=%s SatelliteCollision=%d E5Collision=%d M6Target=%d PracticeSlingshot=%d PracticePouch=%s LaunchProfileHash=%016llX PresetHash=%016llX BaselineGravitySnapshotHash=%016llX RuntimeLayoutSnapshotHash=%016llX"),
		bRuntimeReady ? 1 : 0,
		RuntimeSnapshot.SourceRouteCandidateId,
		SupersededSatellites.Num(),
		*RuntimeSnapshot.SatelliteWorldTransform.GetLocation().ToCompactString(),
		RuntimeSnapshot.SatelliteRadiusCM,
		RuntimeSnapshot.SatelliteSurfaceGravityCMPerSec2,
		*RuntimeSnapshot.E5WorldTransform.GetLocation().ToCompactString(),
		*RuntimeSnapshot.E5HalfExtentCM.ToCompactString(),
		bSatelliteCollisionEnabled ? 1 : 0,
		bE5CollisionEnabled ? 1 : 0,
		bM6TargetBound ? 1 : 0,
		bPracticeSlingshotReady ? 1 : 0,
		GetRuntimePracticeCord()
			? *GetRuntimePracticeCord()->GetRestPouchTransform().GetLocation().ToCompactString()
			: TEXT("None"),
		static_cast<unsigned long long>(
			static_cast<uint64>(RuntimeSnapshot.LaunchProfileHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(RuntimeSnapshot.SatellitePracticePresetHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(RuntimeSnapshot.BaselineGravitySnapshotHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(RuntimeSnapshot.RuntimeLayoutSnapshotHash)));
	return bRuntimeReady;
}

bool AABTSM3MonthlySatellitePracticeRuntime::SpawnSnapshotActors()
{
	auto RejectSpawn = [](const TCHAR* Reason)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SpawnRejected Reason=%s"),
			Reason);
		return false;
	};
	FVector SurfaceWorld = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::ZeroVector;
	float SurfaceRadiusCM = 0.0f;
	int32 SurfaceCellId = INDEX_NONE;
	if (!PrimaryPlanet->QuerySurface(
			CandidateSnapshot.SatelliteAnchorDirection,
			SurfaceWorld,
			SurfaceNormal,
			SurfaceRadiusCM,
			SurfaceCellId)
		|| !SurfaceNormal.Normalize())
	{
		return RejectSpawn(TEXT("SatelliteSurfaceQuery"));
	}
	const float CenterClearanceCM = FVector::DotProduct(
		CandidateSnapshot.SatelliteCenterWorld - SurfaceWorld,
		SurfaceNormal);
	if (CenterClearanceCM < 0.0f)
	{
		return RejectSpawn(TEXT("NegativeSatelliteClearance"));
	}

	RuntimeSatellite =
		GetWorld()->SpawnActorDeferred<AABTSM9Satellite>(
			AABTSM9Satellite::StaticClass(),
			FTransform::Identity,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (RuntimeSatellite == nullptr
		|| !RuntimeSatellite->ConfigureFromPrimaryDirection(
			*PrimaryPlanet,
			CandidateSnapshot.SatelliteAnchorDirection,
			CandidateSnapshot.SatelliteRadiusCM,
			CenterClearanceCM,
			CandidateSnapshot.SatelliteSurfaceGravityCMPerSec2))
	{
		return RejectSpawn(TEXT("SatelliteSpawnOrConfigure"));
	}
	const FVector DirectionConfiguredCenter =
		RuntimeSatellite->GetConfiguredCenterWorld();
	// The continuous sphere is intentionally Static after FinishSpawning. Apply
	// the persisted candidate translation while the Actor is still deferred so
	// its collision body is born at the F7 snapshot instead of being moved later.
	RuntimeSatellite->SetActorLocation(
		CandidateSnapshot.SatelliteCenterWorld,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	RuntimeSatellite->bGravityEnabled = true;
	UGameplayStatics::FinishSpawningActor(
		RuntimeSatellite,
		FTransform::Identity);
	if (!RuntimeSatellite->IsPlanetReady()
		&& !RuntimeSatellite->RebuildPlanet())
	{
		return RejectSpawn(TEXT("SatelliteRebuild"));
	}
	if (!RuntimeSatellite->GetActorLocation().Equals(
			CandidateSnapshot.SatelliteCenterWorld,
			1.0f))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SpawnRejected Reason=SatelliteCenterMismatch Actual=%s DirectionConfigured=%s Candidate=%s"),
			*RuntimeSatellite->GetActorLocation().ToCompactString(),
			*DirectionConfiguredCenter.ToCompactString(),
			*CandidateSnapshot.SatelliteCenterWorld.ToCompactString());
		return false;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice] SnapshotCenterApplied DeltaFromM9Query=%.2f"),
		FVector::Distance(
			DirectionConfiguredCenter,
			CandidateSnapshot.SatelliteCenterWorld));
	RuntimeSatellite->SetActorEnableCollision(true);
	if (RuntimeSatellite->ContinuousSurface)
	{
		RuntimeSatellite->ContinuousSurface->SetCollisionEnabled(
			ECollisionEnabled::QueryAndPhysics);
		RuntimeSatellite->ContinuousSurface->SetCollisionResponseToAllChannels(
			ECR_Block);
		RuntimeSatellite->ContinuousSurface->RecreatePhysicsState();
	}

	RuntimeE5Target =
		GetWorld()->SpawnActorDeferred<AABTSCalibrationTargetProxy>(
			AABTSCalibrationTargetProxy::StaticClass(),
			CandidateSnapshot.E5TargetWorldTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (RuntimeE5Target == nullptr)
	{
		return RejectSpawn(TEXT("E5Spawn"));
	}
	RuntimeE5Target->ConfigureCube(
		TEXT("Satellite.Backside.E5"),
		CandidateSnapshot.E5TargetHalfExtentCM.GetMax(),
		FLinearColor(1.0f, 0.12f, 0.72f, 1.0f));
	UGameplayStatics::FinishSpawningActor(
		RuntimeE5Target,
		CandidateSnapshot.E5TargetWorldTransform);
	RuntimeE5Target->SetActorEnableCollision(true);
	RuntimeE5Target->AttachToActor(
		RuntimeSatellite,
		FAttachmentTransformRules::KeepWorldTransform);

	bSatelliteCollisionEnabled =
		ABTSM3MonthlySatellitePracticeRuntimePrivate::
			HasPawnBlockingCollision(*RuntimeSatellite);
	bE5CollisionEnabled =
		ABTSM3MonthlySatellitePracticeRuntimePrivate::
			HasPawnBlockingCollision(*RuntimeE5Target);

	const FABTSM6LaunchProfileCatalog FrozenCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	FABTSCalibrationGravitySnapshot GravitySnapshot;
	GravitySnapshot.PrimaryCenterWorld = PrimaryPlanet->GetPlanetCenterWorld();
	GravitySnapshot.PrimaryRadiusCM = PrimaryPlanet->GetPlanetRadiusCM();
	GravitySnapshot.PrimarySurfaceGravityCMPerSec2 = 980.0f;
	GravitySnapshot.SatelliteCenterWorld = RuntimeSatellite->GetPlanetCenterWorld();
	GravitySnapshot.SatelliteRadiusCM = RuntimeSatellite->GetPlanetRadiusCM();
	GravitySnapshot.SatelliteSurfaceGravityCMPerSec2 =
		RuntimeSatellite->GetSurfaceGravityAccelerationCMPerSec2();
	GravitySnapshot.FlightAirDragPerSecond =
		FrozenCatalog.FlightAirDragPerSecond;
	GravitySnapshot.bSatelliteGravityEnabled = true;
	const uint64 BaselineGravitySnapshotHash =
		FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
			GravitySnapshot);

	RuntimeSnapshot.bValid = true;
	RuntimeSnapshot.SourceRouteCandidateId =
		CandidateSnapshot.SourceRouteCandidateId;
	RuntimeSnapshot.SourcePreviewResultHash = SourcePreviewResultHash;
	RuntimeSnapshot.SourceCandidateHash = CandidateSnapshot.CandidateHash;
	RuntimeSnapshot.LaunchProfileHash = CandidateSnapshot.LaunchProfileHash;
	RuntimeSnapshot.SatellitePracticePresetVersion =
		CandidateSnapshot.SatellitePracticePresetVersion;
	RuntimeSnapshot.SatellitePracticePresetHash =
		CandidateSnapshot.SatellitePracticePresetHash;
	RuntimeSnapshot.SatelliteWorldTransform =
		RuntimeSatellite->GetActorTransform();
	RuntimeSnapshot.SatelliteRadiusCM =
		RuntimeSatellite->GetPlanetRadiusCM();
	RuntimeSnapshot.SatelliteSurfaceGravityCMPerSec2 =
		RuntimeSatellite->GetSurfaceGravityAccelerationCMPerSec2();
	RuntimeSnapshot.E5WorldTransform = RuntimeE5Target->GetActorTransform();
	RuntimeSnapshot.E5HalfExtentCM = RuntimeE5Target->GetTargetHalfExtentCM();
	RuntimeSnapshot.BaselineGravitySnapshotHash =
		static_cast<int64>(BaselineGravitySnapshotHash);
	RuntimeSnapshot.RuntimeLayoutSnapshotHash = static_cast<int64>(
		ABTSM3MonthlySatellitePracticeRuntimePrivate::
			ComputeRuntimeLayoutSnapshotHash(
				SourcePreviewResultHash,
				CandidateSnapshot.CandidateHash,
				BaselineGravitySnapshotHash));
	if (!bSatelliteCollisionEnabled)
	{
		return RejectSpawn(TEXT("SatellitePawnCollision"));
	}
	if (!bE5CollisionEnabled)
	{
		return RejectSpawn(TEXT("E5PawnCollision"));
	}
	return RuntimeSnapshot.bValid;
}

bool AABTSM3MonthlySatellitePracticeRuntime::SpawnPracticeSlingshot()
{
	bPracticeSlingshotReady = false;
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const FVector LaunchUp = CandidateSnapshot.LaunchUpWorld.GetSafeNormal();
	const FVector LaunchForward = FVector::VectorPlaneProject(
		CandidateSnapshot.LaunchForwardWorld,
		LaunchUp).GetSafeNormal();
	if (LaunchUp.IsNearlyZero() || LaunchForward.IsNearlyZero())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=InvalidLaunchFrame"));
		return false;
	}

	// The monthly preview's reference height deliberately matches the native
	// reinforced device: 220 cm stake endpoint plus a -30 cm rest-pouch offset.
	// Spawning the device root at this exact pair midpoint makes M6 consume the
	// same pouch frame that placed the frozen satellite/E5 snapshot.
	const FABTSM3MonthlySatellitePreviewConfig PreviewDefaults;
	const FVector PairMidpoint = CandidateSnapshot.LaunchWorldLocation
		- LaunchUp * PreviewDefaults.ReferencePouchHeightCM;
	const FQuat Rotation = FRotationMatrix::MakeFromXZ(
		LaunchForward,
		LaunchUp).ToQuat();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RuntimePracticeSlingshot =
		GetWorld()->SpawnActor<AABTSM71ReinforcedSlingshotActor>(
			AABTSM71ReinforcedSlingshotActor::StaticClass(),
			FTransform(Rotation, PairMidpoint),
			SpawnParameters);
	// Minimal automation worlds do not route normal World BeginPlay. The actor's
	// native BeginPlay is the authority that creates its runtime stakes/cord, so
	// dispatch it only for that unbegun-world case. A real PIE actor has already
	// begun play when SpawnActor returns and never enters this branch.
	if (IsValid(RuntimePracticeSlingshot)
		&& !RuntimePracticeSlingshot->HasActorBegunPlay())
	{
		RuntimePracticeSlingshot->DispatchBeginPlay();
	}
	AABTSM51SlingshotCord* Cord = GetRuntimePracticeCord();
	if (!IsValid(RuntimePracticeSlingshot) || Cord == nullptr)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=SpawnOrCord"));
		return false;
	}

	const FVector ActualPouch = Cord->GetRestPouchTransform().GetLocation();
	const FVector ActualRight = FVector::VectorPlaneProject(
		Cord->GetEndpointB() - Cord->GetEndpointA(),
		LaunchUp).GetSafeNormal();
	const FVector ActualForward = FVector::CrossProduct(
		ActualRight,
		LaunchUp).GetSafeNormal();
	const float PouchErrorCM = FVector::Distance(
		ActualPouch,
		CandidateSnapshot.LaunchWorldLocation);
	const float ForwardDot = FVector::DotProduct(
		ActualForward,
		LaunchForward);
	bPracticeSlingshotReady =
		Cord->GetSlingshotTier() == EABTSSlingshotTier::Reinforced
		&& PouchErrorCM <= 1.0f
		&& ForwardDot >= 0.999f;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice][Slingshot] Ready=%d Root=%s Pouch=%s CandidatePouch=%s PouchError=%.3f ForwardDot=%.5f Tier=%d"),
		bPracticeSlingshotReady ? 1 : 0,
		*PairMidpoint.ToCompactString(),
		*ActualPouch.ToCompactString(),
		*CandidateSnapshot.LaunchWorldLocation.ToCompactString(),
		PouchErrorCM,
		ForwardDot,
		static_cast<int32>(Cord->GetSlingshotTier()));
	if (!bPracticeSlingshotReady)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=FrameMismatch PouchError=%.3f ForwardDot=%.5f"),
			PouchErrorCM,
			ForwardDot);
	}
	return bPracticeSlingshotReady;
}

bool AABTSM3MonthlySatellitePracticeRuntime::BindM6Target()
{
	if (!IsValid(RuntimeSatellite) || !IsValid(RuntimeE5Target)
		|| GetWorld() == nullptr)
	{
		bM6TargetBound = false;
		return false;
	}
	if (IsValid(BoundSlingshotSystem))
	{
		AABTSM9Satellite* BoundSatellite = nullptr;
		AActor* BoundTarget = nullptr;
		FVector BoundHalfExtent = FVector::ZeroVector;
		bM6TargetBound = BoundSlingshotSystem->CopySatellitePracticeTarget(
			BoundSatellite,
			BoundTarget,
			BoundHalfExtent)
			&& BoundSatellite == RuntimeSatellite.Get()
			&& BoundTarget == RuntimeE5Target.Get()
			&& BoundHalfExtent.Equals(RuntimeSnapshot.E5HalfExtentCM, 0.1f);
		if (bM6TargetBound)
		{
			return true;
		}
	}

	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenSatellitePracticePresetV0();
	for (TActorIterator<AABTSM6SlingshotSystem> It(GetWorld()); It; ++It)
	{
		BoundSlingshotSystem = *It;
		BoundSlingshotSystem->ConfigureSatellitePracticeTarget(
			*RuntimeSatellite,
			*RuntimeE5Target,
			RuntimeSnapshot.E5HalfExtentCM,
			FrozenPreset.IntegrationStepSeconds,
			FrozenPreset.MaximumFlightSeconds);
		bM6TargetBound = true;
		return true;
	}
	bM6TargetBound = false;
	return false;
}

void AABTSM3MonthlySatellitePracticeRuntime::ApplyGravityOverride(
	const bool bForceLog)
{
	if (!IsValid(RuntimeSatellite))
	{
		return;
	}
	IConsoleVariable* GravityCVar = IConsoleManager::Get().FindConsoleVariable(
		ABTSM3MonthlySatellitePracticeRuntimePrivate::SatelliteGravityCVarName);
	const int32 GravityOverride = GravityCVar != nullptr
		? GravityCVar->GetInt()
		: -1;
	const bool bGravityEnabled = GravityOverride < 0
		? true
		: GravityOverride != 0;
	RuntimeSatellite->bGravityEnabled = bGravityEnabled;
	if (!bForceLog && GravityOverride == LastGravityOverride)
	{
		return;
	}
	LastGravityOverride = GravityOverride;
	const uint64 WorldGravityHash = ABTSM9Gravity::
		GetSatelliteGravitySnapshotHash(
			GetWorld(),
			IsValid(PrimaryPlanet)
				? PrimaryPlanet->GetPlanetCenterWorld()
				: FVector::ZeroVector);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice][Gravity] Override=%d Enabled=%d WorldGravityHash=%016llX RuntimeLayoutSnapshotHash=%016llX"),
		GravityOverride,
		bGravityEnabled ? 1 : 0,
		static_cast<unsigned long long>(WorldGravityHash),
		static_cast<unsigned long long>(
			static_cast<uint64>(RuntimeSnapshot.RuntimeLayoutSnapshotHash)));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			0x4D395347,
			5.0f,
			bGravityEnabled ? FColor::Cyan : FColor::Orange,
			FString::Printf(
				TEXT("M3R5.1 Satellite Gravity %s  abts.Calibration.SatelliteGravity=%d  Collision SAT/E5=%d/%d"),
				bGravityEnabled ? TEXT("ON") : TEXT("OFF"),
				GravityOverride,
				bSatelliteCollisionEnabled ? 1 : 0,
				bE5CollisionEnabled ? 1 : 0));
	}
}

void AABTSM3MonthlySatellitePracticeRuntime::RefreshReadyState()
{
	bRuntimeReady = RuntimeSnapshot.bValid
		&& IsValid(RuntimeSatellite)
		&& IsValid(RuntimeE5Target)
		&& bSatelliteCollisionEnabled
		&& bE5CollisionEnabled
		&& bM6TargetBound
		&& bPracticeSlingshotReady;
}

void AABTSM3MonthlySatellitePracticeRuntime::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRuntimeActorsSpawned)
	{
		ActivateSnapshot();
		return;
	}
	BindM6Target();
	ApplyGravityOverride(false);
	LogGravityEvidence(DeltaSeconds);
	RefreshReadyState();
}

void AABTSM3MonthlySatellitePracticeRuntime::LogGravityEvidence(
	const float DeltaSeconds)
{
	GravityEvidenceLogRemainingSeconds = FMath::Max(
		0.0f,
		GravityEvidenceLogRemainingSeconds - FMath::Max(0.0f, DeltaSeconds));
	if (GravityEvidenceLogRemainingSeconds > 0.0f
		|| !IsValid(RuntimeSatellite))
	{
		return;
	}
	GravityEvidenceLogRemainingSeconds = 1.0f;

	for (TActorIterator<AABTSM25BirdCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsSlingshotFlightActive())
		{
			continue;
		}
		const FVector Location = It->GetActorLocation();
		const FVector SatelliteAcceleration =
			ABTSM9Gravity::GetSatelliteAcceleration(GetWorld(), Location);
		const float SurfaceClearanceCM = FVector::Distance(
			Location,
			RuntimeSatellite->GetPlanetCenterWorld())
			- RuntimeSatellite->GetPlanetRadiusCM();
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R5.1][RuntimePractice][FlightGravity] Bird=%s Enabled=%d Acceleration=%.3f SurfaceClearance=%.1f Location=%s"),
			*GetNameSafe(*It),
			RuntimeSatellite->bGravityEnabled ? 1 : 0,
			SatelliteAcceleration.Size(),
			SurfaceClearanceCM,
			*Location.ToCompactString());
		break;
	}
}

AABTSM51SlingshotCord*
AABTSM3MonthlySatellitePracticeRuntime::GetRuntimePracticeCord() const
{
	return IsValid(RuntimePracticeSlingshot)
		? RuntimePracticeSlingshot->GetRuntimeCord()
		: nullptr;
}

bool AABTSM3MonthlySatellitePracticeRuntime::IsSatelliteGravityEnabled() const
{
	return IsValid(RuntimeSatellite)
		&& RuntimeSatellite->bGravityEnabled;
}

void AABTSM3MonthlySatellitePracticeRuntime::ClearOwnedRuntime()
{
	if (IsValid(BoundSlingshotSystem))
	{
		BoundSlingshotSystem->ClearSatellitePracticeTarget(
			RuntimeE5Target.Get());
	}
	BoundSlingshotSystem = nullptr;
	if (IsValid(RuntimeE5Target)
		&& !RuntimeE5Target->IsActorBeingDestroyed())
	{
		RuntimeE5Target->Destroy();
	}
	RuntimeE5Target = nullptr;
	if (IsValid(RuntimePracticeSlingshot))
	{
		TArray<AActor*> OwnedSlingshotActors;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->GetOwner() == RuntimePracticeSlingshot.Get())
			{
				OwnedSlingshotActors.Add(*It);
			}
		}
		for (AActor* OwnedActor : OwnedSlingshotActors)
		{
			if (IsValid(OwnedActor) && !OwnedActor->IsActorBeingDestroyed())
			{
				OwnedActor->Destroy();
			}
		}
		RuntimePracticeSlingshot->Destroy();
	}
	RuntimePracticeSlingshot = nullptr;
	if (IsValid(RuntimeSatellite)
		&& !RuntimeSatellite->IsActorBeingDestroyed())
	{
		RuntimeSatellite->Destroy();
	}
	RuntimeSatellite = nullptr;
	bRuntimeActorsSpawned = false;
	bRuntimeReady = false;
	bSatelliteCollisionEnabled = false;
	bE5CollisionEnabled = false;
	bM6TargetBound = false;
	bPracticeSlingshotReady = false;
}

void AABTSM3MonthlySatellitePracticeRuntime::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ClearOwnedRuntime();
	Super::EndPlay(EndPlayReason);
}
