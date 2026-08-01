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
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "Terrain/ABTSM3Planet.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM3MonthlySatellitePracticeRuntimePrivate
{
constexpr TCHAR SatelliteGravityCVarName[] =
	TEXT("abts.Calibration.SatelliteGravity");
constexpr float MaximumSatelliteFacingErrorDegrees = 5.0f;

struct FFacingAlignedSatellitePlacement
{
	FVector AnchorDirection = FVector::ZeroVector;
	FVector SurfaceWorld = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::ZeroVector;
	FVector CenterWorld = FVector::ZeroVector;
	int32 AnchorCellId = INDEX_NONE;
	float FacingErrorDegrees = 180.0f;
	float CorrectionAzimuthDegrees = 0.0f;
};

bool ResolveFacingAlignedSatellitePlacement(
	AABTSM3Planet& Planet,
	const FTransform& LaunchTransform,
	const FABTSSatellitePracticePreset& Preset,
	FFacingAlignedSatellitePlacement& OutPlacement)
{
	const FVector LaunchWorld = LaunchTransform.GetLocation();
	const FVector LaunchForward = LaunchTransform.GetUnitAxis(EAxis::X).GetSafeNormal();
	const FVector LaunchUp = LaunchTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
	const FVector LaunchRadial =
		(LaunchWorld - Planet.GetPlanetCenterWorld()).GetSafeNormal();
	FVector ArcTangent = FVector::VectorPlaneProject(
		LaunchForward,
		LaunchRadial).GetSafeNormal();
	if (LaunchForward.IsNearlyZero()
		|| LaunchUp.IsNearlyZero()
		|| LaunchRadial.IsNearlyZero()
		|| ArcTangent.IsNearlyZero())
	{
		return false;
	}
	ArcTangent = ArcTangent.RotateAngleAxis(
		Preset.SatelliteAnchorAzimuthDegrees,
		LaunchRadial).GetSafeNormal();
	const float ArcRadians = FMath::DegreesToRadians(
		Preset.SatelliteAnchorArcDegrees);
	const float CenterClearanceCM = Planet.GetPlanetRadiusCM()
		* Preset.SatelliteCenterClearancePrimaryRatio;

	const auto Evaluate = [&](
		const float CorrectionAzimuthDegrees,
		FFacingAlignedSatellitePlacement& OutCandidate)
	{
		const FVector CorrectedTangent = ArcTangent.RotateAngleAxis(
			CorrectionAzimuthDegrees,
			LaunchRadial).GetSafeNormal();
		const FVector AnchorDirection =
			(LaunchRadial * FMath::Cos(ArcRadians)
				+ CorrectedTangent * FMath::Sin(ArcRadians)).GetSafeNormal();
		FVector SurfaceWorld = FVector::ZeroVector;
		FVector SurfaceNormal = FVector::ZeroVector;
		float SurfaceRadiusCM = 0.0f;
		int32 SurfaceCellId = INDEX_NONE;
		if (AnchorDirection.IsNearlyZero()
			|| !Planet.QuerySurface(
				AnchorDirection,
				SurfaceWorld,
				SurfaceNormal,
				SurfaceRadiusCM,
				SurfaceCellId)
			|| !SurfaceNormal.Normalize())
		{
			return false;
		}
		const FVector CenterWorld =
			SurfaceWorld + SurfaceNormal * CenterClearanceCM;
		const FVector SightTangent = FVector::VectorPlaneProject(
			CenterWorld - LaunchWorld,
			LaunchUp).GetSafeNormal();
		if (SightTangent.IsNearlyZero())
		{
			return false;
		}
		OutCandidate.AnchorDirection = AnchorDirection;
		OutCandidate.SurfaceWorld = SurfaceWorld;
		OutCandidate.SurfaceNormal = SurfaceNormal;
		OutCandidate.CenterWorld = CenterWorld;
		OutCandidate.AnchorCellId = SurfaceCellId;
		OutCandidate.FacingErrorDegrees = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(
				FVector::DotProduct(SightTangent, LaunchForward),
				-1.0f,
				1.0f)));
		OutCandidate.CorrectionAzimuthDegrees = CorrectionAzimuthDegrees;
		return FMath::IsFinite(OutCandidate.FacingErrorDegrees);
	};
	const auto TryCandidate = [&OutPlacement, &Evaluate](
		const float CorrectionAzimuthDegrees,
		bool& bInOutFound)
	{
		FFacingAlignedSatellitePlacement Candidate;
		if (!Evaluate(CorrectionAzimuthDegrees, Candidate))
		{
			return;
		}
		const bool bBetter = !bInOutFound
			|| Candidate.FacingErrorDegrees
				< OutPlacement.FacingErrorDegrees - 0.0001f
			|| (FMath::IsNearlyEqual(
					Candidate.FacingErrorDegrees,
					OutPlacement.FacingErrorDegrees,
					0.0001f)
				&& FMath::Abs(Candidate.CorrectionAzimuthDegrees)
					< FMath::Abs(OutPlacement.CorrectionAzimuthDegrees));
		if (bBetter)
		{
			OutPlacement = Candidate;
			bInOutFound = true;
		}
	};

	bool bFound = false;
	for (int32 Step = -45; Step <= 45; ++Step)
	{
		TryCandidate(static_cast<float>(Step) * 2.0f, bFound);
	}
	if (!bFound)
	{
		return false;
	}
	const float CoarseBest = OutPlacement.CorrectionAzimuthDegrees;
	for (int32 Step = -20; Step <= 20; ++Step)
	{
		TryCandidate(CoarseBest + static_cast<float>(Step) * 0.1f, bFound);
	}
	const float FineBest = OutPlacement.CorrectionAzimuthDegrees;
	for (int32 Step = -20; Step <= 20; ++Step)
	{
		TryCandidate(FineBest + static_cast<float>(Step) * 0.005f, bFound);
	}
	return bFound
		&& OutPlacement.FacingErrorDegrees
			<= MaximumSatelliteFacingErrorDegrees;
}

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
	const uint64 GravitySnapshotHash,
	const FTransform& PracticeLaunchWorldTransform,
	const int32 PracticeStakeACellId,
	const int32 PracticeStakeBCellId)
{
	uint64 Hash = 14695981039346656037ull;
	Hash = AddHashValue(Hash, static_cast<uint64>(PreviewResultHash));
	Hash = AddHashValue(Hash, static_cast<uint64>(CandidateHash));
	Hash = AddHashValue(Hash, GravitySnapshotHash);
	Hash = AddHashValue(Hash, static_cast<uint32>(PracticeStakeACellId));
	Hash = AddHashValue(Hash, static_cast<uint32>(PracticeStakeBCellId));
	const FVector Location = PracticeLaunchWorldTransform.GetLocation();
	FQuat Rotation = PracticeLaunchWorldTransform.GetRotation().GetNormalized();
	if (Rotation.W < 0.0)
	{
		Rotation.X = -Rotation.X;
		Rotation.Y = -Rotation.Y;
		Rotation.Z = -Rotation.Z;
		Rotation.W = -Rotation.W;
	}
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Location.X * 10.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Location.Y * 10.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Location.Z * 10.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Rotation.X * 100000.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Rotation.Y * 100000.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Rotation.Z * 100000.0)));
	Hash = AddHashValue(Hash, static_cast<uint64>(FMath::RoundToInt64(Rotation.W * 100000.0)));
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

	if (!SpawnPracticeSlingshot())
	{
		ClearOwnedRuntime();
		return false;
	}
	if (!SpawnSnapshotActors())
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
	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenSatellitePracticePresetV0();
	ABTSM3MonthlySatellitePracticeRuntimePrivate::
		FFacingAlignedSatellitePlacement SatellitePlacement;
	if (!ABTSM3MonthlySatellitePracticeRuntimePrivate::
			ResolveFacingAlignedSatellitePlacement(
				*PrimaryPlanet,
				RuntimeSnapshot.PracticeLaunchWorldTransform,
				FrozenPreset,
				SatellitePlacement))
	{
		return RejectSpawn(TEXT("SatelliteFacingAlignment"));
	}
	const float CenterClearanceCM = PrimaryPlanet->GetPlanetRadiusCM()
		* FrozenPreset.SatelliteCenterClearancePrimaryRatio;
	const FVector ExpectedSatelliteCenter = SatellitePlacement.CenterWorld;

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
			SatellitePlacement.AnchorDirection,
			CandidateSnapshot.SatelliteRadiusCM,
			CenterClearanceCM,
			CandidateSnapshot.SatelliteSurfaceGravityCMPerSec2))
	{
		return RejectSpawn(TEXT("SatelliteSpawnOrConfigure"));
	}
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
			ExpectedSatelliteCenter,
			1.0f))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SpawnRejected Reason=SatelliteCenterMismatch Actual=%s Expected=%s"),
			*RuntimeSatellite->GetActorLocation().ToCompactString(),
			*ExpectedSatelliteCenter.ToCompactString());
		return false;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice] RealCellSatelliteApplied AnchorCell=%d CandidateAnchorCell=%d DeltaFromPreview=%.2f"),
		SatellitePlacement.AnchorCellId,
		CandidateSnapshot.SatelliteAnchorCellId,
		FVector::Distance(
			RuntimeSatellite->GetActorLocation(),
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

	FABTSCalibrationGravitySnapshot GravitySnapshot;
	const FABTSM6LaunchProfileCatalog FrozenCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
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
	FTransform ResolvedE5Transform = FTransform::Identity;
	FString TargetFailure;
	if (!FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldTransform(
			RuntimeSnapshot.PracticeLaunchWorldTransform.GetLocation(),
			GravitySnapshot,
			FrozenPreset,
			ResolvedE5Transform,
			&TargetFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SpawnRejected Reason=E5Transform Detail=%s"),
			*TargetFailure);
		return false;
	}

	RuntimeE5Target =
		GetWorld()->SpawnActorDeferred<AABTSCalibrationTargetProxy>(
			AABTSCalibrationTargetProxy::StaticClass(),
			ResolvedE5Transform,
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
		ResolvedE5Transform);
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
	RuntimeSnapshot.SatelliteAnchorCellId = SatellitePlacement.AnchorCellId;
	RuntimeSnapshot.SatelliteFacingErrorDegrees =
		SatellitePlacement.FacingErrorDegrees;
	RuntimeSnapshot.SatelliteFacingCorrectionAzimuthDegrees =
		SatellitePlacement.CorrectionAzimuthDegrees;
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
				BaselineGravitySnapshotHash,
				RuntimeSnapshot.PracticeLaunchWorldTransform,
				RuntimeSnapshot.PracticeStakeACellId,
				RuntimeSnapshot.PracticeStakeBCellId));
	if (!bSatelliteCollisionEnabled)
	{
		return RejectSpawn(TEXT("SatellitePawnCollision"));
	}
	if (!bE5CollisionEnabled)
	{
		return RejectSpawn(TEXT("E5PawnCollision"));
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice][Facing] ErrorDegrees=%.3f MaximumDegrees=%.3f CorrectionAzimuthDegrees=%.3f AnchorCell=%d"),
		RuntimeSnapshot.SatelliteFacingErrorDegrees,
		ABTSM3MonthlySatellitePracticeRuntimePrivate::
			MaximumSatelliteFacingErrorDegrees,
		RuntimeSnapshot.SatelliteFacingCorrectionAzimuthDegrees,
		RuntimeSnapshot.SatelliteAnchorCellId);
	return RuntimeSnapshot.bValid;
}

bool AABTSM3MonthlySatellitePracticeRuntime::SpawnPracticeSlingshot()
{
	bPracticeSlingshotReady = false;
	if (GetWorld() == nullptr)
	{
		return false;
	}

	if (!PrimaryPlanet->LogicalCells.IsValidIndex(
			CandidateSnapshot.ReferenceSlotACellId)
		|| !PrimaryPlanet->LogicalCells.IsValidIndex(
			CandidateSnapshot.ReferenceSlotBCellId))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=InvalidReferenceCells A=%d B=%d"),
			CandidateSnapshot.ReferenceSlotACellId,
			CandidateSnapshot.ReferenceSlotBCellId);
		return false;
	}

	struct FResolvedStakeSurface
	{
		FVector World = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		int32 CellId = INDEX_NONE;
	};
	const auto ResolveStakeSurface = [this](
		const int32 RequestedCellId,
		FResolvedStakeSurface& OutSurface)
	{
		float RadiusCM = 0.0f;
		return PrimaryPlanet->QuerySurface(
			PrimaryPlanet->LogicalCells[RequestedCellId].UnitCenter,
			OutSurface.World,
			OutSurface.Normal,
			RadiusCM,
			OutSurface.CellId)
			&& OutSurface.Normal.Normalize();
	};
	FResolvedStakeSurface SurfaceA;
	FResolvedStakeSurface SurfaceB;
	if (!ResolveStakeSurface(CandidateSnapshot.ReferenceSlotACellId, SurfaceA)
		|| !ResolveStakeSurface(CandidateSnapshot.ReferenceSlotBCellId, SurfaceB)
		|| SurfaceA.CellId != CandidateSnapshot.ReferenceSlotACellId
		|| SurfaceB.CellId != CandidateSnapshot.ReferenceSlotBCellId)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=StakeSurfaceCellMismatch RequestedA=%d ResolvedA=%d RequestedB=%d ResolvedB=%d"),
			CandidateSnapshot.ReferenceSlotACellId,
			SurfaceA.CellId,
			CandidateSnapshot.ReferenceSlotBCellId,
			SurfaceB.CellId);
		return false;
	}

	const FABTSSlingshotVisualPreset VisualPreset =
		ABTSMakeDefaultSlingshotVisualPreset(EABTSSlingshotTier::Reinforced);
	const auto SpawnGroundedStake = [this, &VisualPreset](
		const int32 RequestedCellId,
		const FResolvedStakeSurface& Surface)
	{
		FVector Forward = FVector::VectorPlaneProject(
			CandidateSnapshot.LaunchForwardWorld,
			Surface.Normal).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::VectorPlaneProject(
				FVector::ForwardVector,
				Surface.Normal).GetSafeNormal();
		}
		const FTransform StakeTransform(
			FRotationMatrix::MakeFromXZ(Forward, Surface.Normal).ToQuat(),
			Surface.World + Surface.Normal * (VisualPreset.StakeHeightCM * 0.5f));
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM51SlingshotStake* Stake =
			GetWorld()->SpawnActor<AABTSM51SlingshotStake>(
				AABTSM51SlingshotStake::StaticClass(),
				StakeTransform,
				SpawnParameters);
		if (Stake != nullptr)
		{
			Stake->InitializeStake(
				EABTSItemId::ReinforcedStake,
				RequestedCellId,
				Surface.Normal);
		}
		return Stake;
	};
	RuntimePracticeStakeA = SpawnGroundedStake(
		CandidateSnapshot.ReferenceSlotACellId,
		SurfaceA);
	RuntimePracticeStakeB = SpawnGroundedStake(
		CandidateSnapshot.ReferenceSlotBCellId,
		SurfaceB);
	if (!IsValid(RuntimePracticeStakeA) || !IsValid(RuntimePracticeStakeB))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=StakeSpawn"));
		return false;
	}

	AABTSM51SlingshotStake* CordStakeA = RuntimePracticeStakeA.Get();
	AABTSM51SlingshotStake* CordStakeB = RuntimePracticeStakeB.Get();
	FVector EndpointA = CordStakeA->GetVisualTopWorldLocation();
	FVector EndpointB = CordStakeB->GetVisualTopWorldLocation();
	FVector AverageUp = (SurfaceA.Normal + SurfaceB.Normal).GetSafeNormal();
	const FVector InitialRight = (EndpointB - EndpointA).GetSafeNormal();
	const FVector InitialForward = FVector::CrossProduct(
		InitialRight,
		FVector::VectorPlaneProject(AverageUp, InitialRight).GetSafeNormal()).GetSafeNormal();
	if (FVector::DotProduct(InitialForward, CandidateSnapshot.LaunchForwardWorld) < 0.0f)
	{
		Swap(CordStakeA, CordStakeB);
		Swap(EndpointA, EndpointB);
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RuntimePracticeCord = GetWorld()->SpawnActor<AABTSM51SlingshotCord>(
		AABTSM51SlingshotCord::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (!IsValid(RuntimePracticeCord))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=CordSpawn"));
		return false;
	}
	RuntimePracticeCord->InitializeCordWithTier(
		CordStakeA,
		CordStakeB,
		EndpointA,
		EndpointB,
		EABTSSlingshotTier::Reinforced);
	CordStakeA->SetHasCord(true);
	CordStakeB->SetHasCord(true);

	const FTransform ActualLaunchTransform =
		RuntimePracticeCord->GetRestPouchTransform();
	const FVector ActualPouch = ActualLaunchTransform.GetLocation();
	const FVector ActualForward = ActualLaunchTransform.GetUnitAxis(EAxis::X);
	const float ForwardDot = FVector::DotProduct(
		ActualForward,
		CandidateSnapshot.LaunchForwardWorld);
	const float StakeAGroundErrorCM = FVector::Distance(
		RuntimePracticeStakeA->GetVisualBottomWorldLocation(),
		SurfaceA.World);
	const float StakeBGroundErrorCM = FVector::Distance(
		RuntimePracticeStakeB->GetVisualBottomWorldLocation(),
		SurfaceB.World);
	bPracticeSlingshotReady =
		RuntimePracticeCord->GetSlingshotTier() == EABTSSlingshotTier::Reinforced
		&& StakeAGroundErrorCM <= 1.0f
		&& StakeBGroundErrorCM <= 1.0f
		&& ForwardDot >= 0.0f;
	RuntimeSnapshot.PracticeStakeACellId =
		CandidateSnapshot.ReferenceSlotACellId;
	RuntimeSnapshot.PracticeStakeBCellId =
		CandidateSnapshot.ReferenceSlotBCellId;
	RuntimeSnapshot.PracticeStakeASurfaceWorld = SurfaceA.World;
	RuntimeSnapshot.PracticeStakeBSurfaceWorld = SurfaceB.World;
	RuntimeSnapshot.PracticeLaunchWorldTransform = ActualLaunchTransform;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.1][RuntimePractice][Slingshot] Ready=%d CellA=%d ResolvedA=%d GroundA=%.3f CellB=%d ResolvedB=%d GroundB=%.3f Pouch=%s CandidatePouchDelta=%.2f ForwardDot=%.5f Tier=%d"),
		bPracticeSlingshotReady ? 1 : 0,
		CandidateSnapshot.ReferenceSlotACellId,
		SurfaceA.CellId,
		StakeAGroundErrorCM,
		CandidateSnapshot.ReferenceSlotBCellId,
		SurfaceB.CellId,
		StakeBGroundErrorCM,
		*ActualPouch.ToCompactString(),
		FVector::Distance(ActualPouch, CandidateSnapshot.LaunchWorldLocation),
		ForwardDot,
		static_cast<int32>(RuntimePracticeCord->GetSlingshotTier()));
	if (!bPracticeSlingshotReady)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.1][RuntimePractice] SlingshotRejected Reason=GroundOrFrameMismatch GroundA=%.3f GroundB=%.3f ForwardDot=%.5f"),
			StakeAGroundErrorCM,
			StakeBGroundErrorCM,
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
	return RuntimePracticeCord.Get();
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
	if (IsValid(RuntimePracticeCord)
		&& !RuntimePracticeCord->IsActorBeingDestroyed())
	{
		RuntimePracticeCord->Destroy();
	}
	RuntimePracticeCord = nullptr;
	if (IsValid(RuntimePracticeStakeA)
		&& !RuntimePracticeStakeA->IsActorBeingDestroyed())
	{
		RuntimePracticeStakeA->Destroy();
	}
	RuntimePracticeStakeA = nullptr;
	if (IsValid(RuntimePracticeStakeB)
		&& !RuntimePracticeStakeB->IsActorBeingDestroyed())
	{
		RuntimePracticeStakeB->Destroy();
	}
	RuntimePracticeStakeB = nullptr;
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
