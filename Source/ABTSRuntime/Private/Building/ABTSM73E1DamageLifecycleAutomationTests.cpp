// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73StableBuildingActor.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73E1DamageLifecycleAutomationTest,
	"ABTS.M73DAG.BuildingFreezeV3.E1DamageLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73E1DamageLifecycleAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	FABTSM73E1DamageLifecycleState DirectCrystal;
	DirectCrystal.RecordChaosActivated();
	DirectCrystal.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	TestFalse(TEXT("Direct Crystal destruction is not collapse-chain evidence"),
		DirectCrystal.IsAccepted());
	TestFalse(TEXT("Direct Crystal destruction is not marked physical-chain"),
		DirectCrystal.bCrystalDestroyedByPhysicalChain);

	FABTSM73E1DamageLifecycleState ScriptedCrystal;
	ScriptedCrystal.RecordChaosActivated();
	ScriptedCrystal.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	ScriptedCrystal.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::UnknownOrScripted,
		/*bModuleBroken=*/true);
	TestFalse(TEXT("Scripted Crystal removal is fail-closed"),
		ScriptedCrystal.IsAccepted());

	FABTSM73E1DamageLifecycleState DirectBlastCrystal;
	DirectBlastCrystal.RecordChaosActivated();
	DirectBlastCrystal.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	DirectBlastCrystal.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::GameplayBlast,
		/*bModuleBroken=*/true);
	TestFalse(TEXT("Direct gameplay blast destruction is not contact-chain evidence"),
		DirectBlastCrystal.IsAccepted());

	FABTSM73E1DamageLifecycleState CollapseChain;
	CollapseChain.RecordChaosActivated();
	CollapseChain.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/false);
	TestFalse(TEXT("A real hit alone is not terminal evidence"),
		CollapseChain.IsAccepted());
	CollapseChain.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::ModuleContact,
		/*bModuleBroken=*/false);
	TestFalse(TEXT("Structural contact without Crystal destruction is incomplete"),
		CollapseChain.IsAccepted());
	CollapseChain.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::ModuleContact,
		/*bModuleBroken=*/true);
	TestTrue(TEXT("Real hit plus physical chain Crystal destruction is accepted"),
		CollapseChain.IsAccepted());
	TestEqual(TEXT("Both physical contact events remain observable"),
		CollapseChain.PhysicalContactDamageEventCount, 2);

	FABTSM73E1DamageLifecycleState SupportBreakChain;
	SupportBreakChain.RecordChaosActivated();
	SupportBreakChain.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	SupportBreakChain.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::ModuleContact,
		/*bModuleBroken=*/true);
	TestTrue(TEXT("Physical support break may propagate into Crystal contact break"),
		SupportBreakChain.IsAccepted());

	FABTSM73E1DamageLifecycleState NoChaos;
	NoChaos.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/true,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	NoChaos.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::ModuleContact,
		/*bModuleBroken=*/true);
	TestFalse(TEXT("The same events without production Chaos activation fail closed"),
		NoChaos.IsAccepted());

	FABTSM73E1DamageLifecycleState DeviceFirstHit;
	DeviceFirstHit.RecordChaosActivated();
	DeviceFirstHit.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/false,
		EABTSM73E1DamageCause::BirdImpact,
		/*bModuleBroken=*/true);
	DeviceFirstHit.RecordModuleDamage(
		/*bCertifiedTargetBrick=*/false,
		/*bCrystal=*/true,
		EABTSM73E1DamageCause::ModuleContact,
		/*bModuleBroken=*/true);
	TestFalse(TEXT("A device or cap cannot impersonate a descriptor Brick first hit"),
		DeviceFirstHit.IsAccepted());

	FABTSM73E1DestructibleModuleTargetSet OrderedTargetSet;
	OrderedTargetSet.ManifestEntryId = FName(TEXT("E1ColumnBreak"));
	OrderedTargetSet.DescriptorHash = 101;
	OrderedTargetSet.StaticGeometryHash = 202;
	FABTSM73E1DestructibleModuleTarget& LongBrick =
		OrderedTargetSet.OrderedBrickTargets.AddDefaulted_GetRef();
	LongBrick.BrickId = 0;
	LongBrick.FrozenWorldTransform = FTransform(
		FQuat::Identity, FVector(10.0, 20.0, 30.0), FVector::OneVector);
	LongBrick.HalfExtentCM = FVector(72.0, 9.0, 9.0);
	FABTSM73E1DestructibleModuleTarget& UnitBrick =
		OrderedTargetSet.OrderedBrickTargets.AddDefaulted_GetRef();
	UnitBrick.BrickId = 1;
	UnitBrick.FrozenWorldTransform = FTransform(
		FQuat::Identity, FVector(50.0, 20.0, 30.0), FVector::OneVector);
	UnitBrick.HalfExtentCM = FVector(18.0, 18.0, 18.0);
	TestTrue(TEXT("Ordered descriptor OBB rows are usable without live pointers in the pure test"),
		OrderedTargetSet.IsUsable(2, false));
	TestFalse(TEXT("Production target sets require live module/building/material ownership"),
		OrderedTargetSet.IsUsable(2, true));
	const uint32 HonestObbHash = OrderedTargetSet.ComputeOrderedGeometryHash();
	FABTSM73E1DestructibleModuleTargetSet CubeExpanded = OrderedTargetSet;
	CubeExpanded.OrderedBrickTargets[0].HalfExtentCM = FVector(72.0);
	TestNotEqual(TEXT("Max-axis cube expansion changes the target identity"),
		CubeExpanded.ComputeOrderedGeometryHash(), HonestObbHash);
	FABTSM73E1DestructibleModuleTargetSet Reordered = OrderedTargetSet;
	Reordered.OrderedBrickTargets.Swap(0, 1);
	TestFalse(TEXT("Descriptor Brick order is part of the authority"),
		Reordered.IsUsable(2, false));
	FABTSM73E1DestructibleModuleTargetSet ScaledObb = OrderedTargetSet;
	ScaledObb.OrderedBrickTargets[0].FrozenWorldTransform.SetScale3D(
		FVector(2.0, 1.0, 1.0));
	TestFalse(TEXT("OBB transforms must be unit scale because extent carries size"),
		ScaledObb.IsUsable(2, false));

	return true;
}

#endif
