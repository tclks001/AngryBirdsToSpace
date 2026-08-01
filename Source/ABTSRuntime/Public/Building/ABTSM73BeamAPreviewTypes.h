// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73DAG5BShapePreviewTypes.h"
#include "ABTSM73BeamAPreviewTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM73BeamAFrameAxis : uint8
{
	X,
	Y,
	Z,
	Diagonal
};

UENUM(BlueprintType)
enum class EABTSM73BeamAJointRole : uint8
{
	GroundFoot,
	BeamEnd,
	ColumnHead,
	CrossBearing,
	RoofNode
};

UENUM(BlueprintType)
enum class EABTSM73BeamAMemberRole : uint8
{
	Post,
	PrimaryBeam,
	SecondaryBeam,
	RoofCourse
};

UENUM(BlueprintType)
enum class EABTSM73BeamAAssemblyType : uint8
{
	PostAndLintelBay,
	CrossBeamBay,
	RoofFrameBay,
	BridgeFrameBay,
	StackedFrameBay,
	LayeredRoofBay
};

/** Physical support semantics between two block surfaces. */
UENUM(BlueprintType)
enum class EABTSM73BeamABearingType : uint8
{
	CrossBearing,
	PostOnBeam,
	BeamOnPost,
	ParallelBearing
};

USTRUCT(BlueprintType)
struct FABTSM73BeamAPreviewSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette")
	FABTSM73DAG5BV2PreviewSettings Silhouette;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay",
		meta = (ClampMin = "120.0", ClampMax = "3000.0", Units = "cm"))
	float TargetBaySpanCM = 480.0f;

	/** All preview blocks have this fixed square cross-section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "12.0", ClampMax = "120.0", Units = "cm"))
	float BlockCrossSectionCM = 36.0f;

	/** Maximum number of equally spaced parallel blocks in one course. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "2", ClampMax = "16"))
	int32 MaxParallelBlocksPerCourse = 3;

	/** Minimum clear side gap retained between parallel blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "1.0", ClampMax = "240.0", Units = "cm"))
	float MinimumParallelBlockGapCM = 12.0f;

	/** Clear side gap below which the final parallel pair collapses to one centered block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block",
		meta = (ClampMin = "0.0", ClampMax = "240.0", Units = "cm"))
	float TwoBlockMergeGapCM = 4.0f;

	/** Maximum horizontal courses used to approximate one roof volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Roof",
		meta = (ClampMin = "2", ClampMax = "64"))
	int32 MaxRoofCourseCount = 32;

	/** Legacy roof-specific cap retained for serialized preview compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Roof",
		meta = (ClampMin = "1", ClampMax = "5"))
	int32 RoofBlocksPerCourse = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay",
		meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxBaysPerVolume = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay|Budget",
		meta = (ClampMin = "8", ClampMax = "4096"))
	int32 MaxBayCount = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay|Budget",
		meta = (ClampMin = "32", ClampMax = "32768"))
	int32 MaxJointCount = 8192;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay|Budget",
		meta = (ClampMin = "32", ClampMax = "65536"))
	int32 MaxMemberCount = 16384;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay|Budget",
		meta = (ClampMin = "32", ClampMax = "65536"))
	int32 MaxBearingContactCount = 16384;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay|Budget",
		meta = (ClampMin = "128", ClampMax = "1048576"))
	int32 MaxBearingPairChecks = 262144;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay",
		meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "cm"))
	float JointMergeToleranceCM = 0.5f;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamABay
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BayId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 SourceVolumeId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FBox LocalBounds = FBox(EForceInit::ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	EABTSM73BeamAFrameAxis PreferredAxis = EABTSM73BeamAFrameAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	TArray<int32> AdjacentBayIds;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamAJoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 JointId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FVector LocalPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAJointRole Role = EABTSM73BeamAJointRole::BeamEnd;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamAMember
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 MemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 JointA = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 JointB = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAMemberRole Role = EABTSM73BeamAMemberRole::PrimaryBeam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	float LengthCM = 0.0f;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamABearingContact
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 ContactId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 LowerMemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 UpperMemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamABearingType Type = EABTSM73BeamABearingType::CrossBearing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FVector LocalPosition = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	float ContactAreaCM2 = 0.0f;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamAAssembly
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 AssemblyId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 BayId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAAssemblyType Type =
		EABTSM73BeamAAssemblyType::PostAndLintelBay;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	TArray<int32> JointIds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	TArray<int32> MemberIds;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamAPreviewSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 SourceVolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BayCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 JointCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 MemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 AssemblyCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BearingContactCount = 0;

	/** Posts split around global horizontal courses during assembly closure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 SplitPostMemberCount = 0;

	/** Collinear overlapping members folded into a single physical block. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 MergedMemberCount = 0;

	/** Horizontal courses lifted onto the next legal stacking layer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ShiftedCourseCount = 0;

	/** Repair posts inserted between disconnected supported assemblies. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 GlobalSupportMemberCount = 0;

	/** Isolated redundant members removed only after support repair stalls. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 PrunedUnsupportedMemberCount = 0;

	/** Must be zero for an accepted preview. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 RemainingPenetrationCount = 0;

	/** Must be zero for an accepted preview. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 UnsupportedMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 XMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 YMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 ZMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 DiagonalMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 BayGraphHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 BeamGraphHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
