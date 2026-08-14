// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSSharedStylizedMaterialAdapter.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"
#include "UObject/UObjectGlobals.h"

namespace ABTSSharedStylizedMaterialAdapterAutomationPrivate
{
	UMaterialInterface* LoadT3A2Material(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path);
	}

	bool HasScalarParameter(
		const UMaterialInterface& Material,
		const FName ParameterName)
	{
		float Value = 0.0f;
		return Material.GetScalarParameterValue(
			FMaterialParameterInfo(ParameterName),
			Value);
	}

	bool HasVectorParameter(
		const UMaterialInterface& Material,
		const FName ParameterName)
	{
		FLinearColor Value;
		return Material.GetVectorParameterValue(
			FMaterialParameterInfo(ParameterName),
			Value);
	}

	UTexture* GetTextureParameter(
		const UMaterialInterface& Material,
		const FName ParameterName)
	{
		UTexture* Value = nullptr;
		return Material.GetTextureParameterValue(
			FMaterialParameterInfo(ParameterName),
			Value)
			? Value
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT3A2SharedMaterialAdapterTest,
	"ABTS.Rendering.Toon.T3A2.SharedMaterialAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT3A2SharedMaterialAdapterTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSSharedStylizedMaterialAdapterAutomationPrivate;
	TestEqual(
		TEXT("The accepted shared catalog is complete"),
		FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount(),
		22);
	TestNotEqual(
		TEXT("The shared catalog has a diagnostic identity"),
		FABTSSharedStylizedMaterialAdapter::GetCatalogHash(),
		0u);
	TArray<UMaterialInterface*> PreloadedMaterials;
	int32 PreloadFailureCount = INDEX_NONE;
	TestEqual(
		TEXT("Startup preload resolves every shared catalog candidate"),
		FABTSSharedStylizedMaterialAdapter::PreloadCatalogMaterials(
			PreloadedMaterials,
			PreloadFailureCount),
		22);
	TestEqual(
		TEXT("Startup preload has no missing candidate"),
		PreloadFailureCount,
		0);
	TestEqual(
		TEXT("Startup preload retains one strong candidate per catalog entry"),
		PreloadedMaterials.Num(),
		22);
	TSet<UMaterialInterface*> UniquePreloadedMaterials;
	for (UMaterialInterface* Material : PreloadedMaterials)
	{
		TestNotNull(TEXT("Every preloaded shared candidate is valid"), Material);
		if (Material)
		{
			UniquePreloadedMaterials.Add(Material);
		}
	}
	TestEqual(
		TEXT("The startup preload catalog contains no duplicate candidates"),
		UniquePreloadedMaterials.Num(),
		22);

	struct FExpectedSource
	{
		const TCHAR* Path;
		EABTSStylizedMaterialFamily Family;
	};
	const FExpectedSource ExpectedSources[]
	{
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_3.M_CuteBird_3"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_10.M_CuteBird_10"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_16.M_CuteBird_16"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_0.M_CuteBird_0"), EABTSStylizedMaterialFamily::CuteBirdBody},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_3.M_Dino_face_3"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_6.M_Dino_face_6"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_17.M_Dino_face_17"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_1.M_Dino_face_1"), EABTSStylizedMaterialFamily::CuteBirdFace},
		{TEXT("/Game/StaticMesh/Stake/Twig/MI_Stake_Twig.MI_Stake_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Twig/MI_Cord_Twig.MI_Cord_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Twig/MI_Pouch_Twig.MI_Pouch_Twig"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Simple/MI_Stake_Simple.MI_Stake_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Simple/MI_Pouch_Simple.MI_Pouch_Simple"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Reinforced/MI_Stack_Reinforced.MI_Stack_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Cord/Reinforced/MI_Cord_Reinforced.MI_Cord_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Pouch/Reinforced/MI_Pouch_Reinforced.MI_Pouch_Reinforced"), EABTSStylizedMaterialFamily::SlingshotOrganic},
		{TEXT("/Game/StaticMesh/Stake/Steel/MI_Stack_Steel.MI_Stack_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
		{TEXT("/Game/StaticMesh/Cord/Steel/MI_Cord_Steel.MI_Cord_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
		{TEXT("/Game/StaticMesh/Pouch/Steel/MI_Pouch_Steel.MI_Pouch_Steel"), EABTSStylizedMaterialFamily::SlingshotMetal},
	};
	for (const FExpectedSource& Expected : ExpectedSources)
	{
		UMaterialInterface* Source = LoadT3A2Material(Expected.Path);
		UMaterialInterface* Target = nullptr;
		EABTSStylizedMaterialFamily ResolvedFamily =
			EABTSStylizedMaterialFamily::None;
		TestNotNull(
			*FString::Printf(TEXT("Source loads: %s"), Expected.Path),
			Source);
		if (Source)
		{
			TestTrue(
				*FString::Printf(TEXT("Candidate resolves: %s"), Expected.Path),
				FABTSSharedStylizedMaterialAdapter::TryResolveMaterial(
					*Source,
					Target,
					ResolvedFamily));
			TestNotNull(
				*FString::Printf(TEXT("Candidate loads: %s"), Expected.Path),
				Target);
			TestEqual(
				*FString::Printf(TEXT("Family matches: %s"), Expected.Path),
				ResolvedFamily,
				Expected.Family);
		}
	}

	UMaterialInterface* BodySource = LoadT3A2Material(
		TEXT("/Game/CuteBird/Materials/CuteBirdColor_Materials/M_CuteBird_12.M_CuteBird_12"));
	UMaterialInterface* FaceSource = LoadT3A2Material(
		TEXT("/Game/CuteBird/Materials/Face_Materials/M_Dino_face_23.M_Dino_face_23"));
	TestNotNull(TEXT("Accepted red body source loads"), BodySource);
	TestNotNull(TEXT("Accepted red face source loads"), FaceSource);
	if (!BodySource || !FaceSource)
	{
		return false;
	}

	USkeletalMesh* BirdMesh = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/CuteBird/Meshes/SM_Cute_Bird.SM_Cute_Bird"));
	TestNotNull(TEXT("Accepted two-slot bird mesh loads"), BirdMesh);
	USkeletalMeshComponent* Component =
		NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Transient two-slot consumer is available"), Component);
	if (!BirdMesh || !Component)
	{
		return false;
	}
	Component->SetSkeletalMesh(BirdMesh);
	// Deliberately reverse the normal body/face slot order. Semantic family must
	// come from the explicit source identity, never from slot 0 or slot 1.
	Component->SetMaterial(0, FaceSource);
	Component->SetMaterial(1, BodySource);
	TArray<UPrimitiveComponent*> Primitives{Component};
	TArray<FABTSStylizedMaterialSlotBinding> Bindings;
	TestEqual(
		TEXT("Both accepted source slots publish bindings"),
		FABTSSharedStylizedMaterialAdapter::GatherPrimitiveBindings(
			Primitives,
			Bindings),
		2);
	TestEqual(TEXT("Two bindings are emitted"), Bindings.Num(), 2);
	if (Bindings.Num() != 2)
	{
		return false;
	}
	TestEqual(
		TEXT("Slot zero remains a face by source identity"),
		Bindings[0].Family,
		EABTSStylizedMaterialFamily::CuteBirdFace);
	TestEqual(
		TEXT("Slot one remains a body by source identity"),
		Bindings[1].Family,
		EABTSStylizedMaterialFamily::CuteBirdBody);
	TestEqual(
		TEXT("The face candidate preserves masked rendering"),
		Bindings[0].StylizedMaterial->GetBlendMode(),
		BLEND_Masked);
	TestTrue(
		TEXT("The face candidate is compiled for skeletal meshes"),
		Bindings[0].StylizedMaterial->GetUsageByFlag(MATUSAGE_SkeletalMesh));
	TestTrue(
		TEXT("The body candidate is compiled for skeletal meshes"),
		Bindings[1].StylizedMaterial->GetUsageByFlag(MATUSAGE_SkeletalMesh));

	const FName SourceColorTextureName(TEXT("ABTS_SourceColorTexture"));
	UTexture* RedFaceTexture = GetTextureParameter(
		*Bindings[0].StylizedMaterial,
		SourceColorTextureName);
	UTexture* RedBodyTexture = GetTextureParameter(
		*Bindings[1].StylizedMaterial,
		SourceColorTextureName);
	TestNotNull(TEXT("The face candidate retains its source face texture"), RedFaceTexture);
	TestNotNull(TEXT("The body candidate retains its source body texture"), RedBodyTexture);
	TestEqual(
		TEXT("The red face texture identity is preserved"),
		GetPathNameSafe(RedFaceTexture),
		FString(TEXT("/Game/CuteBird/Textures/Face_Textures/T_Dino_Face23.T_Dino_Face23")));
	TestEqual(
		TEXT("The red body texture identity is preserved"),
		GetPathNameSafe(RedBodyTexture),
		FString(TEXT("/Game/CuteBird/Textures/CuteBirdColor_Textures/T_Cutebird_12.T_Cutebird_12")));

	UMaterialInterface* BlueBodyCandidate = LoadT3A2Material(
		TEXT("/Game/Toon/Shared/Birds/MI_ABTS_Toon_BirdBody_Blue.MI_ABTS_Toon_BirdBody_Blue"));
	UTexture* BlueBodyTexture = BlueBodyCandidate
		? GetTextureParameter(*BlueBodyCandidate, SourceColorTextureName)
		: nullptr;
	TestNotNull(TEXT("The blue body candidate loads"), BlueBodyCandidate);
	TestNotNull(TEXT("The blue body candidate retains its source texture"), BlueBodyTexture);
	TestTrue(
		TEXT("Different bird identities do not collapse to one texture"),
		BlueBodyTexture != RedBodyTexture);
	if (BlueBodyCandidate)
	{
		const UMaterialInstance* BlueBodyInstance =
			Cast<UMaterialInstance>(BlueBodyCandidate);
		TestNotNull(
			TEXT("The blue candidate remains a material instance"),
			BlueBodyInstance);
		TestEqual(
			TEXT("Blue preserves its accepted Normal-sampled color parent"),
			GetPathNameSafe(BlueBodyInstance ? BlueBodyInstance->Parent : nullptr),
			FString(TEXT("/Game/Toon/Shared/Masters/M_ABTS_Toon_BirdBody_LegacyNormalColor.M_ABTS_Toon_BirdBody_LegacyNormalColor")));
	}

	const TArray<FName> RequiredScalars
	{
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
		FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
		FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
		FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
		FABTSStylizedMaterialContract::GetRimPowerParameterName()
	};
	for (const FABTSStylizedMaterialSlotBinding& Binding : Bindings)
	{
		for (const FName ParameterName : RequiredScalars)
		{
			TestTrue(
				*FString::Printf(
					TEXT("%s exposes %s"),
					*GetNameSafe(Binding.StylizedMaterial),
					*ParameterName.ToString()),
				HasScalarParameter(*Binding.StylizedMaterial, ParameterName));
		}
		TestTrue(
			TEXT("Every shared candidate exposes ABTS_BaseColorTint"),
			HasVectorParameter(
				*Binding.StylizedMaterial,
				FABTSStylizedMaterialContract::GetBaseColorTintParameterName()));
	}

	FABTSStylizedMaterialOverrideRegistry Registry;
	Registry.Apply(Bindings, true);
	TestEqual(TEXT("Both shared slots are reversibly owned"), Registry.Num(), 2);
	TestTrue(
		TEXT("Face candidate replaces the accepted source"),
		Component->GetMaterial(0) == Bindings[0].StylizedMaterial);
	TestTrue(
		TEXT("Body candidate replaces the accepted source"),
		Component->GetMaterial(1) == Bindings[1].StylizedMaterial);

	Bindings.Reset();
	TestEqual(
		TEXT("An already applied candidate republishes the same desired binding"),
		FABTSSharedStylizedMaterialAdapter::GatherPrimitiveBindings(
			Primitives,
			Bindings),
		2);
	Registry.Apply(Bindings, true);
	TestEqual(TEXT("Refresh does not oscillate or duplicate slots"), Registry.Num(), 2);
	Bindings.Reset();
	Registry.Apply(Bindings, false);
	TestTrue(
		TEXT("Style Off restores the exact face interface"),
		Component->GetMaterial(0) == FaceSource);
	TestTrue(
		TEXT("Style Off restores the exact body interface"),
		Component->GetMaterial(1) == BodySource);

	UMaterialInterface* OrganicSource = LoadT3A2Material(
		TEXT("/Game/StaticMesh/Stake/Reinforced/MI_Stack_Reinforced.MI_Stack_Reinforced"));
	UMaterialInterface* MetalSource = LoadT3A2Material(
		TEXT("/Game/StaticMesh/Stake/Steel/MI_Stack_Steel.MI_Stack_Steel"));
	UMaterialInterface* Candidate = nullptr;
	EABTSStylizedMaterialFamily Family = EABTSStylizedMaterialFamily::None;
	TestTrue(
		TEXT("Reinforced stake resolves"),
		OrganicSource && FABTSSharedStylizedMaterialAdapter::TryResolveMaterial(
			*OrganicSource,
			Candidate,
			Family));
	TestEqual(
		TEXT("Reinforced single-slot surface remains Organic"),
		Family,
		EABTSStylizedMaterialFamily::SlingshotOrganic);
	TestTrue(
		TEXT("Textured slingshot candidates are compiled for static meshes"),
		Candidate && Candidate->GetUsageByFlag(MATUSAGE_StaticMesh));
	TestTrue(
		TEXT("Textured slingshot candidates are compiled for Nanite"),
		Candidate && Candidate->GetUsageByFlag(MATUSAGE_Nanite));
	TestTrue(
		TEXT("Steel stake resolves"),
		MetalSource && FABTSSharedStylizedMaterialAdapter::TryResolveMaterial(
			*MetalSource,
			Candidate,
			Family));
	TestEqual(
		TEXT("Steel tier is Metal"),
		Family,
		EABTSStylizedMaterialFamily::SlingshotMetal);

	UMaterialInterface* CordSource = LoadT3A2Material(
		TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple"));
	UMaterialInterface* CordCandidate = nullptr;
	TestTrue(
		TEXT("Simple cord resolves"),
		CordSource && FABTSSharedStylizedMaterialAdapter::TryResolveMaterial(
			*CordSource,
			CordCandidate,
			Family));
	TestTrue(
		TEXT("Solid slingshot candidates are compiled for static meshes"),
		CordCandidate && CordCandidate->GetUsageByFlag(MATUSAGE_StaticMesh));
	TestTrue(
		TEXT("Solid slingshot candidates are compiled for Nanite"),
		CordCandidate && CordCandidate->GetUsageByFlag(MATUSAGE_Nanite));

	return true;
}

#endif
