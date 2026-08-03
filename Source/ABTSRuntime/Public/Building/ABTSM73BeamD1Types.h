// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"
#include "ABTSM73BeamD1Types.generated.h"

UENUM(BlueprintType)
enum class EABTSM73BeamD1StructuralRole : uint8
{
	PrimaryFrame,
	SecondaryFrame,
	Connector
};

UENUM(BlueprintType)
enum class EABTSM73BeamD1DeviceRole : uint8
{
	None,
	Anchor,
	Payload
};

USTRUCT(BlueprintType)
struct FABTSM73BeamD1Settings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	FName GameplayProfileId = TEXT("ColumnBreak");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile",
		meta = (ClampMin = "0", ClampMax = "5"))
	int32 DifficultyTier = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	int32 BuildingSeed = 940211;
};

/** Complete one-to-one binding from a closed Beam Member to one real M7 Brick. */
USTRUCT(BlueprintType)
struct FABTSM73BeamD1BrickBinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BrickId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 MemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	EABTSM73BeamD1StructuralRole StructuralRole =
		EABTSM73BeamD1StructuralRole::PrimaryFrame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	bool bWeaknessCandidate = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	EABTSM73BeamD1DeviceRole DeviceRole = EABTSM73BeamD1DeviceRole::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FABTSM7BrickSpec BrickSpec;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Brick")
	FBox LocalBounds = FBox(EForceInit::ForceInit);
};

USTRUCT(BlueprintType)
struct FABTSM73BeamD1Summary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName GameplayProfileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 DifficultyTier = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FName ResolvedM7ProfileId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 MemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 CompleteReferenceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 WoodBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 StoneBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 IronBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Material")
	int32 GlassBrickCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	int32 WeaknessCandidateCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Role")
	int32 DeviceRoleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 StrictPenetrationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 ResolvedSettingsHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 UpstreamBeamHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 BrickGeometryHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};

