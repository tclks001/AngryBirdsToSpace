// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM8BridgeActors.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"
#include "World/ABTSVisualTuning.h"

AABTSM8WaterBarrierActor::AABTSM8WaterBarrierActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("WaterBarrierCollision"));
	SetRootComponent(Collision);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetCollisionObjectType(ABTSWalkBarrierChannel);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetGenerateOverlapEvents(false);
}

void AABTSM8WaterBarrierActor::InitializeBarrier(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& HalfExtentCM)
{
	EdgeKey = InEdge;
	Collision->SetCollisionObjectType(ABTSWalkBarrierChannel);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	BaseHalfExtentCM = HalfExtentCM.ComponentMax(FVector(1.0f));
	BlockingAlongIntervalsCM.Reset();
	BlockingAlongIntervalsCM.Emplace(-BaseHalfExtentCM.X, BaseHalfExtentCM.X);
	ClearCollisionPieces();
	Collision->SetBoxExtent(BaseHalfExtentCM, true);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AABTSM8WaterBarrierActor::OpenPassage()
{
	BlockingAlongIntervalsCM.Reset();
	ClearCollisionPieces();
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
}

bool AABTSM8WaterBarrierActor::OpenPassage(
	const FTransform& PassageTransform,
	const FVector& PassageDimensionsCM,
	const float SideClearanceCM)
{
	if (BlockingAlongIntervalsCM.IsEmpty()) return false;

	const FVector PassageHalfExtentCM =
		PassageDimensionsCM.ComponentMax(FVector(1.0f)) * 0.5f;
	const FVector BarrierCenterToPassageLocal =
		GetActorTransform().InverseTransformPosition(PassageTransform.GetLocation());
	const FVector BarrierAlong = GetActorTransform().GetUnitAxis(EAxis::X);
	const FVector BarrierAcross = GetActorTransform().GetUnitAxis(EAxis::Y);
	const FVector BarrierUp = GetActorTransform().GetUnitAxis(EAxis::Z);
	const FVector PassageSpan = PassageTransform.GetUnitAxis(EAxis::X);
	const FVector PassageAlong = PassageTransform.GetUnitAxis(EAxis::Y);
	const FVector PassageUp = PassageTransform.GetUnitAxis(EAxis::Z);
	const auto ProjectedHalfExtent = [
		&PassageHalfExtentCM,
		&PassageSpan,
		&PassageAlong,
		&PassageUp](const FVector& Axis)
	{
		return FMath::Abs(FVector::DotProduct(Axis, PassageSpan))
				* PassageHalfExtentCM.X
			+ FMath::Abs(FVector::DotProduct(Axis, PassageAlong))
				* PassageHalfExtentCM.Y
			+ FMath::Abs(FVector::DotProduct(Axis, PassageUp))
				* PassageHalfExtentCM.Z;
	};
	const float PassageAcrossHalfExtentCM = ProjectedHalfExtent(BarrierAcross);
	const float PassageUpHalfExtentCM = ProjectedHalfExtent(BarrierUp);
	if (FMath::Abs(BarrierCenterToPassageLocal.Y)
			> BaseHalfExtentCM.Y + PassageAcrossHalfExtentCM
		|| FMath::Abs(BarrierCenterToPassageLocal.Z)
			> BaseHalfExtentCM.Z + PassageUpHalfExtentCM)
	{
		return false;
	}

	const float PassageAlongHalfExtentCM =
		ProjectedHalfExtent(BarrierAlong) + FMath::Max(0.0f, SideClearanceCM);
	const float PassageMinCM = BarrierCenterToPassageLocal.X - PassageAlongHalfExtentCM;
	const float PassageMaxCM = BarrierCenterToPassageLocal.X + PassageAlongHalfExtentCM;
	TArray<FVector2D> RemainingIntervals;
	bool bCarved = false;
	for (const FVector2D& Interval : BlockingAlongIntervalsCM)
	{
		if (PassageMaxCM <= Interval.X || PassageMinCM >= Interval.Y)
		{
			RemainingIntervals.Add(Interval);
			continue;
		}
		bCarved = true;
		if (PassageMinCM > Interval.X + 1.0f)
		{
			RemainingIntervals.Emplace(Interval.X, FMath::Min(PassageMinCM, Interval.Y));
		}
		if (PassageMaxCM < Interval.Y - 1.0f)
		{
			RemainingIntervals.Emplace(FMath::Max(PassageMaxCM, Interval.X), Interval.Y);
		}
	}
	if (!bCarved) return false;

	BlockingAlongIntervalsCM = MoveTemp(RemainingIntervals);
	RebuildCollisionPieces();
	return true;
}

bool AABTSM8WaterBarrierActor::IsBlockingAtLocalAlongDistance(
	const float LocalAlongDistanceCM) const
{
	for (const FVector2D& Interval : BlockingAlongIntervalsCM)
	{
		if (LocalAlongDistanceCM >= Interval.X
			&& LocalAlongDistanceCM <= Interval.Y)
		{
			return true;
		}
	}
	return false;
}

void AABTSM8WaterBarrierActor::ClearCollisionPieces()
{
	for (UBoxComponent* Piece : CollisionPieces)
	{
		if (IsValid(Piece)) Piece->DestroyComponent();
	}
	CollisionPieces.Reset();
}

void AABTSM8WaterBarrierActor::RebuildCollisionPieces()
{
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ClearCollisionPieces();
	for (const FVector2D& Interval : BlockingAlongIntervalsCM)
	{
		const float HalfLengthCM = (Interval.Y - Interval.X) * 0.5f;
		if (HalfLengthCM <= 0.5f) continue;
		UBoxComponent* Piece = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
		if (Piece == nullptr) continue;
		Piece->SetupAttachment(Collision);
		Piece->SetCollisionProfileName(TEXT("BlockAll"));
		Piece->SetCollisionObjectType(ABTSWalkBarrierChannel);
		Piece->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Piece->SetGenerateOverlapEvents(false);
		Piece->SetBoxExtent(
			FVector(HalfLengthCM, BaseHalfExtentCM.Y, BaseHalfExtentCM.Z),
			false);
		Piece->SetRelativeLocation(FVector((Interval.X + Interval.Y) * 0.5f, 0.0f, 0.0f));
		AddInstanceComponent(Piece);
		Piece->RegisterComponent();
		CollisionPieces.Add(Piece);
	}
}

AABTSM8BridgeActor::AABTSM8BridgeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("BridgeRoot"));
	SetRootComponent(Root);
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("BridgeCollision"));
	Collision->SetupAttachment(Root);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetGenerateOverlapEvents(false);
	Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BridgeDeck"));
	Deck->SetupAttachment(Root);
	Deck->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Deck->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BridgeMesh(
		TEXT("/Game/StaticMesh/Bridge/SM_Bridge.SM_Bridge"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BridgeMaterial(
		TEXT("/Game/StaticMesh/Bridge/MI_Bridge.MI_Bridge"));
	if (BridgeMesh.Succeeded()) Deck->SetStaticMesh(BridgeMesh.Object);
	if (BridgeMaterial.Succeeded()) Deck->SetMaterial(0, BridgeMaterial.Object);
	RefreshVisualTuning();
}

void AABTSM8BridgeActor::InitializeBridge(const FABTSM3CellEdgeKey& InEdge, const FTransform& Transform, const FVector& DimensionsCM)
{
	EdgeKey = InEdge;
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	BaseDimensionsCM = DimensionsCM.ComponentMax(FVector(1.0f));
	Collision->SetBoxExtent(BaseDimensionsCM * 0.5f, true);
	RefreshVisualTuning();
}

void AABTSM8BridgeActor::RefreshVisualTuning()
{
	if (Deck == nullptr) return;
	const FABTSVisualTuningValue& Tuning = ABTSGetVisualTuning(
		EABTSVisualTuningTarget::Bridge);
	FVector MeshSizeCM(100.0f);
	FVector MeshBoundsOrigin = FVector::ZeroVector;
	if (const UStaticMesh* Mesh = Deck->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		MeshSizeCM = (Bounds.BoxExtent * 2.0f).ComponentMax(FVector(1.0f));
		MeshBoundsOrigin = Bounds.Origin;
	}
	// The authored bridge's X axis is its span axis. Fit only that axis and
	// carry the resulting scalar to Y/Z so the mesh keeps its authored shape.
	const float TunedUniformScale =
		(BaseDimensionsCM.X / MeshSizeCM.X) * Tuning.ScaleMultiplier;
	const FVector TunedScale(TunedUniformScale);
	Deck->SetRelativeScale3D(TunedScale);
	Deck->SetRelativeLocation(
		-MeshBoundsOrigin * TunedScale
			+ FVector(0.0f, 0.0f, Tuning.LocalZOffsetCM));
}
