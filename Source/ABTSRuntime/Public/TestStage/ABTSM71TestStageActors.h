// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerStart.h"
#include "Slingshot/ABTSSlingshotTypes.h"
#include "ABTSM71TestStageActors.generated.h"

class AABTSM51SlingshotCord;
class AABTSM51SlingshotStake;
class UArrowComponent;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

/** A transformable planar floor. Its actor location is the gameplay floor plane. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "M7.1 Physics Test Stage"))
class ABTSRUNTIME_API AABTSM71PhysicsTestStage : public AActor
{
	GENERATED_BODY()

public:
	AABTSM71PhysicsTestStage();
	virtual void OnConstruction(const FTransform& Transform) override;

	FVector GetPlaneOrigin() const { return GetActorLocation(); }
	FVector GetPlaneUp() const { return GetActorUpVector().GetSafeNormal(); }
	UStaticMeshComponent* GetFloorComponent() const { return Floor; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Floor;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Floor", meta = (ClampMin = "100.0"))
	FVector2D FloorSizeCM = FVector2D(12000.0f, 12000.0f);

	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Floor", meta = (ClampMin = "10.0"))
	float FloorThicknessCM = 100.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Floor")
	TObjectPtr<UMaterialInterface> FloorMaterial;
};

/** Explicit placeable start used by the M7.1 game mode. Local X is the initial facing direction. */
UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Bird Player Start"))
class ABTSRUNTIME_API AABTSM71PlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	AABTSM71PlayerStart(const FObjectInitializer& ObjectInitializer);
};

/** One editor-placeable HISM instance. Actor transform owns arbitrary XYZ scale. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class ABTSRUNTIME_API AABTSM71PlaceableHISMActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM71PlaceableHISMActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UHierarchicalInstancedStaticMeshComponent* GetHISM() const { return HISM; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1|Assets")
	TObjectPtr<UStaticMesh> InstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1|Assets")
	TObjectPtr<UMaterialInterface> InstanceMaterial;
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Tree HISM"))
class ABTSRUNTIME_API AABTSM71TreeHISMActor : public AABTSM71PlaceableHISMActor
{
	GENERATED_BODY()
public:
	AABTSM71TreeHISMActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Rock HISM"))
class ABTSRUNTIME_API AABTSM71RockHISMActor : public AABTSM71PlaceableHISMActor
{
	GENERATED_BODY()
public:
	AABTSM71RockHISMActor();
};

/** Transformable editor brick. Dimensions come from actor XYZ scale in M7.1. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "M7.1 Placeable Brick"))
class ABTSRUNTIME_API AABTSM71PlaceableBrickActor : public AABTSM71PlaceableHISMActor
{
	GENERATED_BODY()
public:
	AABTSM71PlaceableBrickActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	EABTSM7BuildingMaterial GetBuildingMaterial() const { return BuildingMaterial; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1|Brick")
	EABTSM7BuildingMaterial BuildingMaterial = EABTSM7BuildingMaterial::Wood;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7.1|Brick")
	FLinearColor FallbackColor = FLinearColor(0.38f, 0.13f, 0.035f);
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Wood Brick"))
class ABTSRUNTIME_API AABTSM71WoodBrickActor : public AABTSM71PlaceableBrickActor
{
	GENERATED_BODY()
public: AABTSM71WoodBrickActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Stone Brick"))
class ABTSRUNTIME_API AABTSM71StoneBrickActor : public AABTSM71PlaceableBrickActor
{
	GENERATED_BODY()
public: AABTSM71StoneBrickActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Iron Brick"))
class ABTSRUNTIME_API AABTSM71IronBrickActor : public AABTSM71PlaceableBrickActor
{
	GENERATED_BODY()
public: AABTSM71IronBrickActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Glass Brick"))
class ABTSRUNTIME_API AABTSM71GlassBrickActor : public AABTSM71PlaceableBrickActor
{
	GENERATED_BODY()
public: AABTSM71GlassBrickActor();
};

/** Complete editor-placeable slingshot. Local Y separates stakes; local +X is launch direction. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "M7.1 Complete Slingshot"))
class ABTSRUNTIME_API AABTSM71PlaceableSlingshotActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM71PlaceableSlingshotActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

protected:
	void SetSlingshotTier(EABTSSlingshotTier InTier) { SlingshotTier = InTier; }

private:
	void UpdatePreview();
	void SpawnRuntimeSlingshot();

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UStaticMeshComponent> StakePreviewA;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UStaticMeshComponent> StakePreviewB;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UStaticMeshComponent> CordPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UArrowComponent> LaunchDirection;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot")
	EABTSSlingshotTier SlingshotTier = EABTSSlingshotTier::Simple;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot", meta = (ClampMin = "50.0"))
	float BaseStakeSpacingCM = 210.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot", meta = (ClampMin = "20.0"))
	float StakeHeightCM = 220.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot", meta = (ClampMin = "1.0"))
	float StakeDiameterCM = 28.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot", meta = (ClampMin = "1.0"))
	float CordThicknessCM = 3.5f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UMaterialInterface> StakeMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.1|Slingshot")
	TObjectPtr<UMaterialInterface> CordMaterial;

	TWeakObjectPtr<AABTSM51SlingshotStake> RuntimeStakeA;
	TWeakObjectPtr<AABTSM51SlingshotStake> RuntimeStakeB;
	TWeakObjectPtr<AABTSM51SlingshotCord> RuntimeCord;
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Twig Slingshot"))
class ABTSRUNTIME_API AABTSM71TwigSlingshotActor : public AABTSM71PlaceableSlingshotActor
{
	GENERATED_BODY()
public: AABTSM71TwigSlingshotActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Simple Slingshot"))
class ABTSRUNTIME_API AABTSM71SimpleSlingshotActor : public AABTSM71PlaceableSlingshotActor
{
	GENERATED_BODY()
public: AABTSM71SimpleSlingshotActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Reinforced Slingshot"))
class ABTSRUNTIME_API AABTSM71ReinforcedSlingshotActor : public AABTSM71PlaceableSlingshotActor
{
	GENERATED_BODY()
public: AABTSM71ReinforcedSlingshotActor();
};

UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Space Slingshot"))
class ABTSRUNTIME_API AABTSM71SpaceSlingshotActor : public AABTSM71PlaceableSlingshotActor
{
	GENERATED_BODY()
public: AABTSM71SpaceSlingshotActor();
};

/** Marker reserved for the future modular building generator. */
UCLASS(BlueprintType, meta = (DisplayName = "M7.1 Modular Building Anchor"))
class ABTSRUNTIME_API AABTSM71ModularBuildingAnchor : public AActor
{
	GENERATED_BODY()
public:
	AABTSM71ModularBuildingAnchor();
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> ForwardArrow;
};
