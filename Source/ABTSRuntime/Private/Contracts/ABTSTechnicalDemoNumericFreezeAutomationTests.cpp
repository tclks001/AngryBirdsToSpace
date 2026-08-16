// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Contracts/ABTSTechnicalDemoNumericFreeze.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSTechnicalDemoNumericFreezeV1Test,
	"ABTS.Contracts.TechnicalDemoNumericFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSTechnicalDemoNumericFreezeV1Test::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Frozen recipe topology hash"),
		FABTSTechnicalDemoNumericFreeze::ComputeRecipeTopologyHash(
			FABTSTechnicalDemoNumericFreeze::GetFrozenRecipeTopologyV1()),
		FABTSTechnicalDemoNumericFreeze::FrozenRecipeTopologyHash);
	TestEqual(
		TEXT("Frozen aggregate manifest hash"),
		FABTSTechnicalDemoNumericFreeze::ComputeManifestHash(),
		FABTSTechnicalDemoNumericFreeze::FrozenManifestHash);

	FString Failure;
	const bool bCurrentProjectMatches =
		FABTSTechnicalDemoNumericFreeze::ValidateCurrentProject(&Failure);
	TestTrue(
		*FString::Printf(TEXT("Current project matches manifest: %s"), *Failure),
		bCurrentProjectMatches);

	TArray<FABTSFrozenRecipeTopologyEntry> Tampered;
	for (const FABTSFrozenRecipeTopologyEntry& Entry :
		FABTSTechnicalDemoNumericFreeze::GetFrozenRecipeTopologyV1())
	{
		Tampered.Add(Entry);
	}
	Tampered[0].OutputQuantity += 1;
	TestNotEqual(
		TEXT("Recipe topology tamper changes identity"),
		FABTSTechnicalDemoNumericFreeze::ComputeRecipeTopologyHash(Tampered),
		FABTSTechnicalDemoNumericFreeze::FrozenRecipeTopologyHash);

	AddInfo(FString::Printf(
		TEXT("TechnicalDemoNumericFreeze:Version=%d:ManifestHash=%016llX:LaunchHash=%016llX:FixedSixLayout=%016llX:M7Catalog=%016llX:M11Bundle=%016llX:RecipeTopology=%016llX"),
		FABTSTechnicalDemoNumericFreeze::ManifestVersion,
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenManifestHash),
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenLaunchProfileHash),
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenFixedSixLayoutHash),
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenM7PlacementCatalogHash),
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenM11CertifiedBundleHash),
		static_cast<unsigned long long>(
			FABTSTechnicalDemoNumericFreeze::FrozenRecipeTopologyHash)));
	return true;
}

#endif
