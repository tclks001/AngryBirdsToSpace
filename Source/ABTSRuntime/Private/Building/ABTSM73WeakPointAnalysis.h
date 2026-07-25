// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"

struct FABTSM7MaterialProfile;
struct FABTSM73BrickNode;
struct FABTSM73DifficultySettings;
struct FABTSM73StructureData;

namespace ABTSM73WeakPointAnalysis
{
	struct FMaterialRank
	{
		EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
		float BreakEffort = 1.0f;
		int32 HitTier = 1;
	};

	struct FWeakCandidate
	{
		int32 NodeId = INDEX_NONE;
		EABTSM73WeakPointRole Role = EABTSM73WeakPointRole::None;
		float UnsupportedMassRatio = 0.0f;
		float Exposure = 0.0f;
		float Readability = 0.0f;
		float LocalBreakEffort = 1.0f;
		float Score = 0.0f;
		float InitialSupportMarginCM = 0.0f;
		float TipMarginCM = 0.0f;
		float ReseatRisk = 1.0f;
		uint32 TieBreaker = 0;
		bool bAuthoredStructuralWeakness = false;
		EABTSM73StructuralWeaknessPattern StructuralPattern = EABTSM73StructuralWeaknessPattern::Auto;
		EABTSM73PredictedCollapseMode CollapseMode = EABTSM73PredictedCollapseMode::None;
		TArray<int32> UnsupportedNodeIds;
		TArray<int32> AffectedNodeIds;
	};

	inline constexpr EABTSM7BuildingMaterial AllMaterials[] = {
		EABTSM7BuildingMaterial::Wood,
		EABTSM7BuildingMaterial::Stone,
		EABTSM7BuildingMaterial::Iron,
		EABTSM7BuildingMaterial::Glass
	};

	void BuildResolvedProfiles(
		TConstArrayView<FABTSM7MaterialProfile> Input,
		TArray<FABTSM7MaterialProfile>& OutProfiles);

	void BuildMaterialRanks(
		TConstArrayView<FABTSM7MaterialProfile> Profiles,
		TArray<FMaterialRank>& OutRanks);

	const FMaterialRank* FindMaterialRank(
		TConstArrayView<FMaterialRank> Ranks,
		EABTSM7BuildingMaterial Material);

	float ComputeExposure(
		const FABTSM73BrickNode& Candidate,
		const FABTSM73StructureData& Data,
		const FVector& AttackDirection);

	EABTSM73WeakPointRole ClassifyRole(
		const FABTSM73BrickNode& Node,
		const FABTSM73StructureData& Data);

	float RoleReadability(EABTSM73WeakPointRole Role);

	bool BuildGraph(
		const FABTSM73StructureData& Data,
		TMap<int32, int32>& OutNodeIndices,
		TMap<int32, TArray<int32>>& OutChildren,
		FString& OutError);

	void ProbeRemoval(
		int32 RemovedNodeId,
		const FABTSM73StructureData& Data,
		const TMap<int32, TArray<int32>>& Children,
		TConstArrayView<FABTSM7MaterialProfile> Profiles,
		TArray<int32>& OutUnsupported,
		float& OutMassRatio);

	float AffectedOverlap(const FWeakCandidate& A, const FWeakCandidate& B);

	float ComputeCandidateScore(
		float UnsupportedRatio,
		float Exposure,
		float Readability,
		int32 HitTier,
		const FABTSM73DifficultySettings& Settings);
}
