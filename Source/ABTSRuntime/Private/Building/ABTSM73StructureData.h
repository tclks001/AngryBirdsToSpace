// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"

struct FABTSM73BrickNode
{
	int32 NodeId = INDEX_NONE;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	EABTSM7BuildingMaterial OriginalMaterial = EABTSM7BuildingMaterial::Wood;
	FVector LocalCenter = FVector::ZeroVector;
	FVector DimensionsCM = FVector(100.0f);
	EABTSM73WeakPointRole WeakPointRole = EABTSM73WeakPointRole::None;
	float WeakPointScore = 0.0f;
	float UnsupportedMassRatio = 0.0f;
	float AttackExposure = 0.0f;
	int32 EstimatedHits = 0;
	bool bWeakPoint = false;
	bool bReinforcedCriticalNode = false;
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
};

struct FABTSM73SupportEdge
{
	int32 LowerNodeId = INDEX_NONE;
	int32 UpperNodeId = INDEX_NONE;
	float ContactAreaCM2 = 0.0f;
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
	TArray<int32> GroundNodeIds;
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
