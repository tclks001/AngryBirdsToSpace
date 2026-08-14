// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Stable ownership identities for T3 material-family migration. */
enum class EABTSStylizedMaterialOwner : uint8
{
	Integration = 0,
	M3,
	M7,
	M11
};

/**
 * How a material family preserves the accepted Style Off baseline.
 *
 * ReversibleSlotOverride keeps the original material interface and restores it
 * when styling is disabled. InPlaceStyleParameter is reserved for code-owned
 * MIDs whose runtime parameter injection must remain intact (the M3 SDF terrain).
 */
enum class EABTSStylizedMaterialAdoptionMode : uint8
{
	ReversibleSlotOverride = 0,
	InPlaceStyleParameter
};

/** Stable semantic identities. Feature worktrees must not invent local aliases. */
enum class EABTSStylizedMaterialFamily : uint8
{
	None = 0,
	M3Surface,
	M3BackgroundProp,
	CuteBirdBody,
	CuteBirdFace,
	SlingshotOrganic,
	SlingshotMetal,
	M7Wood,
	M7Stone,
	M7Steel,
	M7Glass,
	FinalePlanet,
	FinaleUFO
};

/**
 * Provisional family defaults. Values are art-tunable; names, units and valid
 * ranges are the stable T3-A0 contract.
 */
struct ABTSRENDER_API FABTSStylizedSurfaceParameters
{
	FLinearColor BaseColorTint = FLinearColor::White;
	float RoughnessFloor = 0.60f;
	float RoughnessScale = 1.0f;
	float SpecularScale = 0.35f;
	float MetallicScale = 1.0f;
	float RimStrength = 0.0f;
	float RimPower = 4.0f;

	bool IsValid() const;
};

/** Integration-owned public contract consumed by T3 family adapters. */
class ABTSRENDER_API FABTSStylizedMaterialContract
{
public:
	static int32 GetVersion();
	static uint32 GetContractHash();

	static bool IsFamilyValid(EABTSStylizedMaterialFamily Family);
	static EABTSStylizedMaterialOwner ResolveOwner(
		EABTSStylizedMaterialFamily Family);
	static EABTSStylizedMaterialAdoptionMode ResolveAdoptionMode(
		EABTSStylizedMaterialFamily Family);
	static FABTSStylizedSurfaceParameters ResolveDefaultParameters(
		EABTSStylizedMaterialFamily Family);
	static bool RequiresOpacityPreservation(
		EABTSStylizedMaterialFamily Family);

	static const TCHAR* LexToString(EABTSStylizedMaterialFamily Family);
	static const TCHAR* LexToString(EABTSStylizedMaterialOwner Owner);
	static const TCHAR* LexToString(EABTSStylizedMaterialAdoptionMode Mode);

	/** Zero must reproduce the accepted pre-T3 surface branch. */
	static const FName& GetStyleEnabledParameterName();
	static const FName& GetBaseColorTintParameterName();
	static const FName& GetRoughnessFloorParameterName();
	static const FName& GetRoughnessScaleParameterName();
	static const FName& GetSpecularScaleParameterName();
	static const FName& GetMetallicScaleParameterName();
	static const FName& GetRimStrengthParameterName();
	static const FName& GetRimPowerParameterName();
};
