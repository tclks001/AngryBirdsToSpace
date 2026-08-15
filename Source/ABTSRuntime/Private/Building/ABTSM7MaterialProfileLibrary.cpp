// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7MaterialProfileLibrary.h"

namespace
{
	FABTSM7MaterialProfile MakeProfile(
		const EABTSM7BuildingMaterial Material,
		const float KnockSpeed,
		const float BreakSpeed,
		const FLinearColor& Color)
	{
		FABTSM7MaterialProfile Profile;
		Profile.Material = Material;
		Profile.KnockSpeedCMPerSec = KnockSpeed;
		Profile.BreakSpeedCMPerSec = BreakSpeed;
		Profile.FallbackColor = Color;
		switch (Material)
		{
		case EABTSM7BuildingMaterial::Wood:
			Profile.DynamicFriction = 0.72f;
			Profile.StaticFriction = 0.88f;
			Profile.Restitution = 0.05f;
			Profile.DensityGPerCubicCM = 0.62f;
			Profile.DamageAtBreakSpeed = 108.0f;
			Profile.PushVelocityTransfer = 0.86f;
			break;
		case EABTSM7BuildingMaterial::Stone:
			Profile.DynamicFriction = 0.82f;
			Profile.StaticFriction = 0.98f;
			Profile.Restitution = 0.16f;
			Profile.DensityGPerCubicCM = 2.55f;
			Profile.DamageAtBreakSpeed = 88.0f;
			Profile.PushVelocityTransfer = 0.52f;
			break;
		case EABTSM7BuildingMaterial::Iron:
			Profile.DynamicFriction = 0.56f;
			Profile.StaticFriction = 0.70f;
			Profile.Restitution = 0.24f;
			Profile.DensityGPerCubicCM = 7.85f;
			Profile.DamageAtBreakSpeed = 68.0f;
			Profile.PushVelocityTransfer = 0.38f;
			break;
		case EABTSM7BuildingMaterial::Glass:
		case EABTSM7BuildingMaterial::Crystal:
		default:
			Profile.DynamicFriction = 0.36f;
			Profile.StaticFriction = 0.46f;
			Profile.Restitution = 0.12f;
			Profile.DensityGPerCubicCM = 2.50f;
			Profile.DamageAtBreakSpeed = 160.0f;
			Profile.PushVelocityTransfer = 0.68f;
			break;
		}
		return Profile;
	}
}

TArray<FABTSM7MaterialProfile> FABTSM7MaterialProfileLibrary::MakeDefaultProfiles()
{
	TArray<FABTSM7MaterialProfile> Result;
	Result.Reserve(5);
	Result.Add(MakeProfile(EABTSM7BuildingMaterial::Wood, 460.0f, 900.0f, FLinearColor(0.38f, 0.13f, 0.035f)));
	Result.Add(MakeProfile(EABTSM7BuildingMaterial::Stone, 680.0f, 1280.0f, FLinearColor(0.32f, 0.34f, 0.38f)));
	Result.Add(MakeProfile(EABTSM7BuildingMaterial::Iron, 820.0f, 1580.0f, FLinearColor(0.12f, 0.16f, 0.20f)));
	Result.Add(MakeProfile(EABTSM7BuildingMaterial::Glass, 280.0f, 520.0f, FLinearColor(0.20f, 0.62f, 0.78f, 0.42f)));
	// Building Freeze V3 starts Crystal from the proven Glass physics baseline;
	// its distinct identity is visual and recoverable rather than a new tuning lane.
	Result.Add(MakeProfile(EABTSM7BuildingMaterial::Crystal, 280.0f, 520.0f, FLinearColor(0.35f, 0.72f, 1.00f, 1.00f)));
	return Result;
}

const FABTSM7MaterialProfile* FABTSM7MaterialProfileLibrary::FindProfile(
	const TConstArrayView<FABTSM7MaterialProfile> Profiles,
	const EABTSM7BuildingMaterial Material)
{
	for (const FABTSM7MaterialProfile& Profile : Profiles)
	{
		if (Profile.Material == Material) return &Profile;
	}
	return nullptr;
}

float FABTSM7MaterialProfileLibrary::ComputeBreakEffort(const FABTSM7MaterialProfile& Profile)
{
	return FMath::Max(1.0f, Profile.BreakSpeedCMPerSec)
		* FMath::Max(1.0f, Profile.BreakDamage)
		/ FMath::Max(1.0f, Profile.DamageAtBreakSpeed);
}
