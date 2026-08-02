// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamAPreviewTypes.h"
#include "ABTSM73BeamBPreviewTypes.generated.h"

UENUM(BlueprintType)
enum class EABTSM73BeamBMotif : uint8
{
	PostAndLintel,
	PortalFrame,
	CrossBeam,
	TwoLayerCrib,
	TransferFrame,
	CantileverBay,
	BracedBay,
	BridgeBay
};

UENUM(BlueprintType)
enum class EABTSM73BeamBGrammarRule : uint8
{
	BeamToGrillage,
	AlternateCribLayer,
	AddTransferTier,
	AddCantileverRoot,
	TriangulateBay,
	RefinePortal,
	AddBridgeSeat
};

USTRUCT(BlueprintType)
struct FABTSM73BeamBPreviewSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beam-A")
	FABTSM73BeamAPreviewSettings BeamA;

	/** Number of bounded graph-grammar refinement rounds after WFC collapse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif Grammar",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 GrammarDepth = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif WFC")
	bool bRequireMotifVariety = true;

	/** Legacy serialized switch; CantileverBay is no longer generated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif WFC",
		meta = (AdvancedDisplay))
	bool bAllowCantilever = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif WFC")
	bool bAllowBracedBay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif WFC|Budget",
		meta = (ClampMin = "32", ClampMax = "262144"))
	int32 MaxWFCPropagationOperations = 32768;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif WFC|Budget",
		meta = (ClampMin = "0", ClampMax = "8192"))
	int32 MaxWFCBacktrackSteps = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif Grammar|Budget",
		meta = (ClampMin = "8", ClampMax = "32768"))
	int32 MaxGrammarStepCount = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif Grammar|Budget",
		meta = (ClampMin = "32", ClampMax = "65536"))
	int32 MaxPlannedMemberCount = 16384;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamBPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BayId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamBMotif Motif = EABTSM73BeamBMotif::PostAndLintel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Orientation = EABTSM73BeamAFrameAxis::X;

	/** X-/X+/Y-/Y+/Lower/Upper bit mask used by local WFC propagation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	uint8 PortMask = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 FirstPlannedMemberIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 PlannedMemberCount = 0;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamBPlannedMember
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 PlannedMemberId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 BayId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamBMotif Motif = EABTSM73BeamBMotif::PostAndLintel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAFrameAxis Axis = EABTSM73BeamAFrameAxis::X;

	/** Structural role preserved when importing Beam-A semantic roof members. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamAMemberRole Role =
		EABTSM73BeamAMemberRole::PrimaryBeam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FVector LocalStart = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geometry")
	FVector LocalEnd = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamBGrammarStep
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 StepId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 BayId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	EABTSM73BeamBGrammarRule Rule = EABTSM73BeamBGrammarRule::RefinePortal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Topology")
	int32 AddedMemberCount = 0;
};

USTRUCT(BlueprintType)
struct FABTSM73BeamBPreviewSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BayCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 PlacementCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 DistinctMotifCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 WFCPropagationOperationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 WFCBacktrackStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 GrammarStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 PlannedMemberCount = 0;

	/** Member count after compiling to Beam-A IR and running global closure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosedMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosedBearingContactCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosureSplitPostMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosureMergedMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosureShiftedCourseCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosureSupportMemberCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Closure")
	int32 ClosurePrunedMemberCount = 0;

	/** One physical support ledger is required at each supported-span endpoint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Bridge")
	int32 BridgeSeatMemberCount = 0;

	/** Endpoint bridge-seat bearings proven after global closure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Bridge")
	int32 BridgeEndpointBearingCount = 0;

	/** Final bridge-rail endpoints that bear directly on their designated seat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result|Bridge")
	int32 BridgeRailEndpointBearingCount = 0;

	/** Must be zero for every accepted Beam-B result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 RemainingPenetrationCount = 0;

	/** Must be zero for every accepted Beam-B result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 UnsupportedMemberCount = 0;

	/** Beam-B currently permits XYZ members only. Must remain zero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 DiagonalMemberCount = 0;

	/** Planned semantic roof courses outside or not tapering within their Shape Grammar envelope. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 SemanticEnvelopeViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 PortViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 OutOfBoundsMemberCount = 0;

	/** Must remain zero: the bridge Assembly may not be rescued by its own ground post. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 BridgeGroundRescuePostCount = 0;

	/** Must remain zero: every declared endpoint must bear on its designated support. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 BridgeEndpointBearingViolationCount = 0;

	/** Must remain zero: every expected load rail must bear on its endpoint seat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	int32 BridgeRailEndpointBearingViolationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 MotifWFCHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 GraphGrammarHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 ResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
