// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSSlingshotVisualTypes.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float MinimumMeshDimensionCM = 0.01f;

	FVector DivideDimensionsSafely(const FVector& Numerator, const FVector& Denominator)
	{
		return FVector(
			Numerator.X / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.X)),
			Numerator.Y / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.Y)),
			Numerator.Z / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.Z)));
	}

	UStaticMesh* LoadSlingshotMesh(const TCHAR* Path)
	{
		return Path != nullptr ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr;
	}

	UMaterialInterface* LoadSlingshotMaterial(const TCHAR* Path)
	{
		return Path != nullptr ? LoadObject<UMaterialInterface>(nullptr, Path) : nullptr;
	}
}

FABTSSlingshotVisualPreset ABTSMakeDefaultSlingshotVisualPreset(const EABTSSlingshotTier Tier)
{
	FABTSSlingshotVisualPreset Preset;
	Preset.StakeVisual.LocalRotation = FRotator(0.0f, 43.04357f, 0.0f);
	Preset.StakeVisual.LocalScale = FVector(3.0f, 3.0f, 1.1f);
	Preset.CordVisual.LocalScale = FVector(2.5f, 2.5f, 1.0f);
	Preset.PouchVisual.LocalScale = FVector(1.5f, 2.0f, 1.5f);
	Preset.ConnectionLayout.RestPouchOffsetCM = FVector(0.0f, 0.0f, -30.0f);
	Preset.ConnectionLayout.PouchAConnectionOffsetCM = FVector(0.0f, -27.0f, 0.0f);
	Preset.ConnectionLayout.PouchBConnectionOffsetCM = FVector(0.0f, 27.0f, 0.0f);

	const TCHAR* StakeMeshPath = TEXT("/Game/StaticMesh/Stake/Simple/SM_Stake_Simple.SM_Stake_Simple");
	const TCHAR* StakeMaterialPath = TEXT("/Game/StaticMesh/Stake/Simple/MI_Stake_Simple.MI_Stake_Simple");
	const TCHAR* CordMeshPath = TEXT("/Game/StaticMesh/Cord/Simple/SM_Cord_Simple.SM_Cord_Simple");
	const TCHAR* CordMaterialPath = TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple");
	const TCHAR* PouchMeshPath = TEXT("/Game/StaticMesh/Pouch/Simple/SM_Pouch_Simple.SM_Pouch_Simple");
	const TCHAR* PouchMaterialPath = TEXT("/Game/StaticMesh/Pouch/Simple/MI_Pouch_Simple.MI_Pouch_Simple");
	switch (Tier)
	{
	case EABTSSlingshotTier::Twig:
		StakeMeshPath = TEXT("/Game/StaticMesh/Stake/Twig/SM_Stake_Twig.SM_Stake_Twig");
		StakeMaterialPath = TEXT("/Game/StaticMesh/Stake/Twig/MI_Stake_Twig.MI_Stake_Twig");
		CordMeshPath = TEXT("/Game/StaticMesh/Cord/Twig/SM_Cord_Twig.SM_Cord_Twig");
		CordMaterialPath = TEXT("/Game/StaticMesh/Cord/Twig/MI_Cord_Twig.MI_Cord_Twig");
		PouchMeshPath = TEXT("/Game/StaticMesh/Pouch/Twig/SM_Pouch_Twig.SM_Pouch_Twig");
		PouchMaterialPath = TEXT("/Game/StaticMesh/Pouch/Twig/MI_Pouch_Twig.MI_Pouch_Twig");
		break;
	case EABTSSlingshotTier::Reinforced:
		StakeMeshPath = TEXT("/Game/StaticMesh/Stake/Reinforced/SM_Stack_Reinforced.SM_Stack_Reinforced");
		StakeMaterialPath = TEXT("/Game/StaticMesh/Stake/Reinforced/MI_Stack_Reinforced.MI_Stack_Reinforced");
		CordMeshPath = TEXT("/Game/StaticMesh/Cord/Reinforced/SM_Cord_Reinforced.SM_Cord_Reinforced");
		CordMaterialPath = TEXT("/Game/StaticMesh/Cord/Reinforced/MI_Cord_Reinforced.MI_Cord_Reinforced");
		PouchMeshPath = TEXT("/Game/StaticMesh/Pouch/Reinforced/SM_Pouch_Reinforced.SM_Pouch_Reinforced");
		PouchMaterialPath = TEXT("/Game/StaticMesh/Pouch/Reinforced/MI_Pouch_Reinforced.MI_Pouch_Reinforced");
		break;
	case EABTSSlingshotTier::Space:
		StakeMeshPath = TEXT("/Game/StaticMesh/Stake/Steel/SM_Stack_Steel.SM_Stack_Steel");
		StakeMaterialPath = TEXT("/Game/StaticMesh/Stake/Steel/MI_Stack_Steel.MI_Stack_Steel");
		CordMeshPath = TEXT("/Game/StaticMesh/Cord/Steel/SM_Cord_Steel.SM_Cord_Steel");
		CordMaterialPath = TEXT("/Game/StaticMesh/Cord/Steel/MI_Cord_Steel.MI_Cord_Steel");
		PouchMeshPath = TEXT("/Game/StaticMesh/Pouch/Steel/SM_Pouch_Steel.SM_Pouch_Steel");
		PouchMaterialPath = TEXT("/Game/StaticMesh/Pouch/Steel/MI_Pouch_Steel.MI_Pouch_Steel");
		break;
	case EABTSSlingshotTier::Simple:
	default:
		break;
	}
	Preset.StakeVisual.Mesh = LoadSlingshotMesh(StakeMeshPath);
	Preset.StakeVisual.Material = LoadSlingshotMaterial(StakeMaterialPath);
	Preset.CordVisual.Mesh = LoadSlingshotMesh(CordMeshPath);
	Preset.CordVisual.Material = LoadSlingshotMaterial(CordMaterialPath);
	Preset.PouchVisual.Mesh = LoadSlingshotMesh(PouchMeshPath);
	Preset.PouchVisual.Material = LoadSlingshotMaterial(PouchMaterialPath);
	return Preset;
}

FTransform ABTSMakeSlingshotVisualTransform(
	const UStaticMesh* Mesh,
	const FVector& TargetAnchorWorld,
	const FQuat& TargetRotation,
	const FVector& TargetSizeCM,
	const FABTSSlingshotVisualSlot& VisualSlot,
	const EABTSSlingshotVisualAnchor Anchor)
{
	const FBoxSphereBounds Bounds = Mesh
		? Mesh->GetBounds()
		: FBoxSphereBounds(FVector::ZeroVector, FVector(50.0f), 86.60254f);
	const FVector SourceSizeCM = Bounds.BoxExtent * 2.0f;
	const FVector FitScale = DivideDimensionsSafely(TargetSizeCM.GetAbs(), SourceSizeCM);
	const FVector LocalFineScale(
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.X)),
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.Y)),
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.Z)));
	const FVector FinalScale = FitScale * LocalFineScale;
	const FQuat FinalRotation = TargetRotation * VisualSlot.LocalRotation.Quaternion();

	FVector SourceAnchor = Bounds.Origin;
	if (Anchor == EABTSSlingshotVisualAnchor::BoundsBottomCenter)
	{
		SourceAnchor.Z -= Bounds.BoxExtent.Z;
	}

	const FVector OffsetWorld = TargetRotation.RotateVector(VisualSlot.LocalOffsetCM);
	const FVector PivotCorrectionWorld = FinalRotation.RotateVector(SourceAnchor * FinalScale);
	return FTransform(
		FinalRotation,
		TargetAnchorWorld + OffsetWorld - PivotCorrectionWorld,
		FinalScale);
}

FVector ABTSScaleSlingshotPouchConnectionOffset(
	const FVector& AuthoredOffsetCM,
	const FABTSSlingshotVisualSlot& PouchVisualSlot)
{
	return AuthoredOffsetCM * FVector(
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.X)),
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.Y)),
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.Z)));
}
