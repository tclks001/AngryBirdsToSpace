// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace ABTSToonT3A0AutomationPrivate
{
	const EABTSStylizedMaterialFamily Families[] = {
		EABTSStylizedMaterialFamily::M3Surface,
		EABTSStylizedMaterialFamily::M3BackgroundProp,
		EABTSStylizedMaterialFamily::CuteBirdBody,
		EABTSStylizedMaterialFamily::CuteBirdFace,
		EABTSStylizedMaterialFamily::SlingshotOrganic,
		EABTSStylizedMaterialFamily::SlingshotMetal,
		EABTSStylizedMaterialFamily::M7Wood,
		EABTSStylizedMaterialFamily::M7Stone,
		EABTSStylizedMaterialFamily::M7Steel,
		EABTSStylizedMaterialFamily::M7Glass,
		EABTSStylizedMaterialFamily::FinalePlanet,
		EABTSStylizedMaterialFamily::FinaleUFO,
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT3A0MaterialContractTest,
	"ABTS.Rendering.Toon.T3A0.MaterialContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT3A0MaterialContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("T3-A0 contract version is frozen"),
		FABTSStylizedMaterialContract::GetVersion(), 1);
	TestNotEqual(TEXT("Contract identity hash is non-zero"),
		FABTSStylizedMaterialContract::GetContractHash(), 0u);
	TestFalse(TEXT("None is never a publishable family"),
		FABTSStylizedMaterialContract::IsFamilyValid(
			EABTSStylizedMaterialFamily::None));

	TSet<FString> Names;
	for (const EABTSStylizedMaterialFamily Family
		: ABTSToonT3A0AutomationPrivate::Families)
	{
		TestTrue(
			FString::Printf(TEXT("%s is valid"),
				FABTSStylizedMaterialContract::LexToString(Family)),
			FABTSStylizedMaterialContract::IsFamilyValid(Family));
		TestTrue(
			FString::Printf(TEXT("%s defaults stay in the stable domain"),
				FABTSStylizedMaterialContract::LexToString(Family)),
			FABTSStylizedMaterialContract::ResolveDefaultParameters(Family).IsValid());
		Names.Add(FABTSStylizedMaterialContract::LexToString(Family));
	}
	TestEqual(TEXT("Every family has a unique diagnostic name"), Names.Num(),
		static_cast<int32>(
			UE_ARRAY_COUNT(ABTSToonT3A0AutomationPrivate::Families)));

	TestEqual(TEXT("M3 surface preserves the code-owned MID path"),
		FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::M3Surface),
		EABTSStylizedMaterialAdoptionMode::InPlaceStyleParameter);
	TestEqual(TEXT("Bird body uses reversible slot replacement"),
		FABTSStylizedMaterialContract::ResolveAdoptionMode(
			EABTSStylizedMaterialFamily::CuteBirdBody),
		EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride);
	TestEqual(TEXT("M7 family identity is reserved for the M7 owner"),
		FABTSStylizedMaterialContract::ResolveOwner(
			EABTSStylizedMaterialFamily::M7Wood),
		EABTSStylizedMaterialOwner::M7);
	TestTrue(TEXT("Glass preserves opacity semantics"),
		FABTSStylizedMaterialContract::RequiresOpacityPreservation(
			EABTSStylizedMaterialFamily::M7Glass));
	TestFalse(TEXT("Opaque families do not acquire glass semantics"),
		FABTSStylizedMaterialContract::RequiresOpacityPreservation(
			EABTSStylizedMaterialFamily::FinaleUFO));

	const FName ParameterNames[] = {
		FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
		FABTSStylizedMaterialContract::GetBaseColorTintParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessFloorParameterName(),
		FABTSStylizedMaterialContract::GetRoughnessScaleParameterName(),
		FABTSStylizedMaterialContract::GetSpecularScaleParameterName(),
		FABTSStylizedMaterialContract::GetMetallicScaleParameterName(),
		FABTSStylizedMaterialContract::GetRimStrengthParameterName(),
		FABTSStylizedMaterialContract::GetRimPowerParameterName(),
	};
	TSet<FName> UniqueParameterNames;
	for (const FName& ParameterName : ParameterNames)
	{
		TestFalse(TEXT("Stable parameter names are never None"),
			ParameterName.IsNone());
		UniqueParameterNames.Add(ParameterName);
	}
	TestEqual(TEXT("Stable material parameters are unique"),
		UniqueParameterNames.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(ParameterNames)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT3A0MaterialOverrideRegistryTest,
	"ABTS.Rendering.Toon.T3A0.MaterialOverrideRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT3A0MaterialOverrideRegistryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UStaticMeshComponent* Component =
		NewObject<UStaticMeshComponent>(GetTransientPackage());
	UMaterial* Original = NewObject<UMaterial>(GetTransientPackage());
	UMaterial* Stylized = NewObject<UMaterial>(GetTransientPackage());
	UMaterial* External = NewObject<UMaterial>(GetTransientPackage());
	TestNotNull(TEXT("Transient component exists"), Component);
	TestNotNull(TEXT("Transient baseline material exists"), Original);
	TestNotNull(TEXT("Transient stylized material exists"), Stylized);
	if (Component == nullptr || Original == nullptr || Stylized == nullptr
		|| External == nullptr)
	{
		return false;
	}

	Component->SetMaterial(0, Original);
	FABTSStylizedMaterialSlotBinding Binding;
	Binding.Component = Component;
	Binding.MaterialSlotIndex = 0;
	Binding.StylizedMaterial = Stylized;
	Binding.Family = EABTSStylizedMaterialFamily::CuteBirdBody;
	const TArray<FABTSStylizedMaterialSlotBinding> Desired{Binding};

	FABTSStylizedMaterialOverrideRegistry Registry;
	Registry.Apply(Desired, true);
	TestEqual(TEXT("Style On applies the candidate interface"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(Stylized));
	TestEqual(TEXT("One material slot is owned"), Registry.Num(), 1);
	TestEqual(TEXT("Valid binding is not rejected"),
		Registry.GetRejectedBindingCount(), 0);

	Registry.Apply(Desired, false);
	TestEqual(TEXT("Style Off restores the exact baseline interface"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(Original));
	TestEqual(TEXT("Style Off releases all material slots"), Registry.Num(), 0);

	Registry.Apply(Desired, true);
	Component->SetMaterial(0, External);
	Registry.Apply(Desired, false);
	TestEqual(TEXT("External material ownership is never overwritten"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(External));
	TestEqual(TEXT("External takeover is recorded as a conflict"),
		Registry.GetConflictCount(), 1);
	TestEqual(TEXT("Conflicted slot is no longer owned"), Registry.Num(), 0);
	Registry.Apply(Desired, true);
	TestEqual(TEXT("Persistent external ownership remains fail closed"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(External));
	TestEqual(TEXT("Persistent external ownership remains one conflict"),
		Registry.GetConflictCount(), 1);

	Registry.RestoreAll();
	Component->SetMaterial(0, Original);
	FABTSStylizedMaterialSlotBinding ConflictingBinding = Binding;
	ConflictingBinding.StylizedMaterial = External;
	ConflictingBinding.Family = EABTSStylizedMaterialFamily::CuteBirdFace;
	const TArray<FABTSStylizedMaterialSlotBinding> DuplicateDesired{
		Binding,
		ConflictingBinding,
	};
	Registry.Apply(DuplicateDesired, true);
	TestEqual(TEXT("Contradictory providers fail closed on the original material"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(Original));
	TestEqual(TEXT("Contradictory providers produce one slot conflict"),
		Registry.GetConflictCount(), 1);
	TestEqual(TEXT("Contradictory slot is never owned"), Registry.Num(), 0);

	Registry.RestoreAll();
	Component->SetMaterial(0, External);
	Binding.Family = EABTSStylizedMaterialFamily::M3Surface;
	Registry.Apply(TArray<FABTSStylizedMaterialSlotBinding>{Binding}, true);
	TestEqual(TEXT("In-place MID families cannot enter slot registry"),
		Registry.GetRejectedBindingCount(), 1);
	TestEqual(TEXT("Rejected in-place binding leaves material untouched"),
		Component->GetMaterial(0), static_cast<UMaterialInterface*>(External));
	Registry.RestoreAll();
	return true;
}

#endif
