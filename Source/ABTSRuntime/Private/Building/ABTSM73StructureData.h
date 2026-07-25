// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"

enum class EABTSM73BrickSemanticRole : uint8
{
	Unknown,
	Column,
	Deck,
	Connector,
	Rail,
	WeakSupport,
	Carrier,
	Payload
};

struct FABTSM73BrickNode
{
	int32 NodeId = INDEX_NONE;
	/** DAG-2 source macro node. INDEX_NONE means Legacy or a physical helper such as a column. */
	int32 MacroNodeId = INDEX_NONE;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	EABTSM7BuildingMaterial OriginalMaterial = EABTSM7BuildingMaterial::Wood;
	FVector LocalCenter = FVector::ZeroVector;
	FVector DimensionsCM = FVector(100.0f);
	EABTSM73BrickSemanticRole SemanticRole = EABTSM73BrickSemanticRole::Unknown;
	int32 StoreyIndex = INDEX_NONE;
	int32 BayIndex = INDEX_NONE;
	EABTSM73WeakPointRole WeakPointRole = EABTSM73WeakPointRole::None;
	float WeakPointScore = 0.0f;
	float UnsupportedMassRatio = 0.0f;
	float AttackExposure = 0.0f;
	int32 EstimatedHits = 0;
	bool bWeakPoint = false;
	bool bReinforcedCriticalNode = false;
};

struct FABTSM73StructuralWeaknessIntent
{
	EABTSM73StructuralWeaknessPattern Pattern = EABTSM73StructuralWeaknessPattern::Auto;
	EABTSM73PredictedCollapseMode ExpectedCollapseMode = EABTSM73PredictedCollapseMode::None;
	int32 CandidateNodeId = INDEX_NONE;
	int32 CarrierNodeId = INDEX_NONE;
	int32 BayIndex = INDEX_NONE;
	FVector ExpectedTipDirectionLocal = FVector::ForwardVector;
	TArray<int32> DirectSupportNodeIds;
	TArray<int32> PayloadNodeIds;
};

struct FABTSM73FailureProbeResult
{
	bool bValid = false;
	bool bWouldReseat = true;
	int32 CandidateNodeId = INDEX_NONE;
	int32 CarrierNodeId = INDEX_NONE;
	EABTSM73StructuralWeaknessPattern Pattern = EABTSM73StructuralWeaknessPattern::Auto;
	EABTSM73PredictedCollapseMode CollapseMode = EABTSM73PredictedCollapseMode::None;
	FVector AffectedCenterOfMassLocal = FVector::ZeroVector;
	FVector TipDirectionLocal = FVector::ZeroVector;
	float AffectedMassRatio = 0.0f;
	float InitialSupportMarginCM = 0.0f;
	float TipMarginCM = 0.0f;
	float ReseatRisk = 1.0f;
	TArray<int32> AffectedNodeIds;
	FString RejectReason;
};

struct FABTSM73WeakPointRecord
{
	int32 NodeId = INDEX_NONE;
	EABTSM73WeakPointRole Role = EABTSM73WeakPointRole::None;
	float UnsupportedMassRatio = 0.0f;
	float Exposure = 0.0f;
	float Readability = 0.0f;
	float LocalBreakEffort = 0.0f;
	float Score = 0.0f;
	int32 EstimatedHits = 0;
	TArray<int32> UnsupportedNodeIds;
	TArray<int32> AffectedNodeIds;
	EABTSM73StructuralWeaknessPattern StructuralPattern = EABTSM73StructuralWeaknessPattern::Auto;
	EABTSM73PredictedCollapseMode CollapseMode = EABTSM73PredictedCollapseMode::None;
	float InitialSupportMarginCM = 0.0f;
	float TipMarginCM = 0.0f;
	float ReseatRisk = 1.0f;
};

struct FABTSM73SupportEdge
{
	int32 LowerNodeId = INDEX_NONE;
	int32 UpperNodeId = INDEX_NONE;
	float ContactAreaCM2 = 0.0f;
};

/** DAG-2 logical support mapped to its physical plate/column contact chain. */
struct FABTSM73DAGPhysicalSupportMapping
{
	int32 SupportMacroNodeId = INDEX_NONE;
	int32 LoadMacroNodeId = INDEX_NONE;
	int32 SupportPlateNodeId = INDEX_NONE;
	int32 LoadPlateNodeId = INDEX_NONE;
	EABTSM73DAGSupportPattern SupportPattern = EABTSM73DAGSupportPattern::ThreeColumnTripod;
	float RealizedColumnWidthCM = 0.0f;
	TArray<int32> ColumnNodeIds;
};

struct FABTSM73GroundSample
{
	FVector2D LocalXY = FVector2D::ZeroVector;
	FVector WorldPosition = FVector::ZeroVector;
	FVector WorldNormal = FVector::UpVector;
	float LocalHeightCM = 0.0f;
	int32 CellId = INDEX_NONE;
	bool bBuildable = true;
};

struct FABTSM73FoundationFoot
{
	FVector2D LocalXY = FVector2D::ZeroVector;
	float GroundHeightCM = 0.0f;
	float BottomHeightCM = 0.0f;
	float TopHeightCM = 0.0f;
};

struct FABTSM73StructureData
{
	TArray<FABTSM73BrickNode> Bricks;
	TArray<FABTSM73SupportEdge> SupportEdges;
	TArray<FABTSM73DAGPhysicalSupportMapping> DAGPhysicalSupportMappings;
	TArray<int32> GroundNodeIds;
	TArray<FABTSM73StructuralWeaknessIntent> StructuralWeaknessIntents;
	TArray<FABTSM73FailureProbeResult> FailureProbeResults;
	TArray<FABTSM73WeakPointRecord> WeakPoints;
	TArray<int32> ReinforcedNodeIds;
	TArray<FVector2D> GroundSupportPoints;
	TArray<FABTSM73GroundSample> GroundSamples;
	TArray<FABTSM73FoundationFoot> FoundationFeet;
	FBox LocalBounds = FBox(EForceInit::ForceInit);
	FVector2D FootprintHalfExtent = FVector2D::ZeroVector;
	float FoundationCapBottomCM = 0.0f;
	float FoundationCapTopCM = 0.0f;
	float CurvatureDropCM = 0.0f;
	float MaxSlopeDegrees = 0.0f;
	float TerrainDeltaCM = 0.0f;
	float MaxFoundationDepthCM = 0.0f;
	float BestWeakPointScore = 0.0f;
	float PredictedWeakCollapseRatio = 0.0f;
	float PredictedNonWeakEffect = 0.0f;
	float DifficultyScore = 0.0f;
	int32 EstimatedWeakPointHits = 0;
	int32 DAGMacroNodeCount = 0;
	int32 DAGSelectedSupportCount = 0;
	int32 DAGMissingRequiredContactCount = 0;
	int32 DAGUnexpectedBypassCount = 0;
	float DAGMinSupportContactAreaRatio = 0.0f;
	uint32 DAGTopologyHash = 0;
};

struct FABTSM73GroundContext
{
	bool bValid = false;
	bool bPlanar = true;
	FTransform AnchorTransform = FTransform::Identity;
	FVector GravityUp = FVector::UpVector;
	FVector PlaneOrigin = FVector::ZeroVector;
	TWeakObjectPtr<class AABTSM3Planet> Planet;
	TWeakObjectPtr<class AABTSM71PhysicsTestStage> TestStage;
	int32 AnchorCellId = INDEX_NONE;
};
