// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Guide/ABTSGuideRuleRuntime.h"

#include "Inventory/ABTSInventoryTypes.h"
#include "Misc/AutomationTest.h"

namespace
{
	void Publish(FABTSGuideRuleRuntime& Runtime, const FName EventId, const FName SubjectId = NAME_None)
	{
		FABTSGuideEventPayload Payload;
		Payload.SubjectId = SubjectId;
		Runtime.PublishEvent(EventId, Payload);
	}

	FName ActiveGuideId(const FABTSGuideRuleRuntime& Runtime)
	{
		FABTSGuidePresentationSnapshot Snapshot;
		return Runtime.GetActiveGuide(Snapshot) ? Snapshot.GuideId : NAME_None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGuideP0RuleSequenceTest,
	"ABTS.Guide.P0.RuleSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGuideP0RuleSequenceTest::RunTest(const FString& Parameters)
{
	FABTSGuideRuleRuntime Runtime;
	TestEqual(TEXT("P0 definition count"), Runtime.GetDefinitions().Num(), 9);
	TestTrue(TEXT("No guide before world ready"), ActiveGuideId(Runtime).IsNone());

	Publish(Runtime, FABTSGuideEventIds::WorldReady);
	TestEqual(TEXT("World ready starts collection"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.CollectResources")));
	Publish(Runtime, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::Branch);
	TestEqual(TEXT("One material keeps collection active"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.CollectResources")));
	Publish(Runtime, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::PlantFiber);
	TestEqual(TEXT("Both materials advance to stake installation"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.InstallTwigStakes")));

	Publish(Runtime, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("One twig stake is insufficient"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.InstallTwigStakes")));
	Publish(Runtime, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("Two twig stakes advance to first endpoint"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.SelectFirstEndpoint")));
	Publish(Runtime, FABTSGuideEventIds::CordEndpointSelected, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("First endpoint advances to second"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.SelectSecondEndpoint")));
	Publish(Runtime, FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("Assembly requests Blue"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.SwitchBlue")));
	Publish(Runtime, FABTSGuideEventIds::ControlledBirdChanged, FABTSGuideSubjects::Blue);
	TestEqual(TEXT("Blue requests pouch click"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.EnterLaunchMode")));
	Publish(Runtime, FABTSGuideEventIds::SlingshotReady, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("Ready requests pull"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.PullPouch")));
	Publish(Runtime, FABTSGuideEventIds::SlingshotPulling, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("Pull requests power change"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.AdjustPower")));
	Publish(Runtime, FABTSGuideEventIds::SlingshotPowerChanged, FABTSGuideSubjects::Twig);
	TestEqual(TEXT("Power change requests release"), ActiveGuideId(Runtime), FName(TEXT("Guide.P0.ReleaseLaunch")));
	Publish(Runtime, FABTSGuideEventIds::SlingshotLaunched, FABTSGuideSubjects::Twig);
	TestTrue(TEXT("Launch completes the current P0 visual sequence"), ActiveGuideId(Runtime).IsNone());

	Publish(Runtime, FABTSGuideEventIds::SlingshotCompleted, FABTSGuideSubjects::Twig);
	Publish(Runtime, FABTSGuideEventIds::ScoutRevealed, FABTSGuideSubjects::Blue);
	TestEqual(TEXT("Completion event was recorded"),
		Runtime.GetEventCount(FABTSGuideEventIds::SlingshotCompleted, FABTSGuideSubjects::Twig), 1);
	TestEqual(TEXT("Scout reveal event was recorded"),
		Runtime.GetEventCount(FABTSGuideEventIds::ScoutRevealed, FABTSGuideSubjects::Blue), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGuideP0FastForwardTest,
	"ABTS.Guide.P0.FastForwardAndSubjectIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGuideP0FastForwardTest::RunTest(const FString& Parameters)
{
	FABTSGuideRuleRuntime Isolated;
	Publish(Isolated, FABTSGuideEventIds::WorldReady);
	Publish(Isolated, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::Branch);
	Publish(Isolated, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::PlantFiber);
	Publish(Isolated, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Simple);
	Publish(Isolated, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Simple);
	TestEqual(TEXT("Simple stakes cannot satisfy twig guide"),
		ActiveGuideId(Isolated), FName(TEXT("Guide.P0.InstallTwigStakes")));

	FABTSGuideRuleRuntime FastForward;
	Publish(FastForward, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::Branch);
	Publish(FastForward, FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::PlantFiber);
	Publish(FastForward, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::CordEndpointSelected, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::ControlledBirdChanged, FABTSGuideSubjects::Blue);
	Publish(FastForward, FABTSGuideEventIds::SlingshotReady, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::SlingshotPulling, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::SlingshotPowerChanged, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::SlingshotLaunched, FABTSGuideSubjects::Twig);
	Publish(FastForward, FABTSGuideEventIds::WorldReady);
	TestTrue(TEXT("Already completed production state does not replay stale guides"), ActiveGuideId(FastForward).IsNone());
	TestEqual(TEXT("All nine guides fast-forwarded"), FastForward.GetCompletedGuideIds().Num(), 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGuideP0EventCatalogTest,
	"ABTS.Guide.P0.EventCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGuideP0EventCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FName> EventIds = {
		FABTSGuideEventIds::WorldReady,
		FABTSGuideEventIds::ControlledBirdChanged,
		FABTSGuideEventIds::ItemAcquired,
		FABTSGuideEventIds::HeldItemChanged,
		FABTSGuideEventIds::StakeInstalled,
		FABTSGuideEventIds::CordEndpointSelected,
		FABTSGuideEventIds::SlingshotAssembled,
		FABTSGuideEventIds::SlingshotEntryRejected,
		FABTSGuideEventIds::SlingshotReady,
		FABTSGuideEventIds::SlingshotPulling,
		FABTSGuideEventIds::SlingshotPowerChanged,
		FABTSGuideEventIds::SlingshotLaunched,
		FABTSGuideEventIds::SlingshotCompleted,
		FABTSGuideEventIds::ScoutRevealed,
		FABTSGuideEventIds::BuildingMaterialRecovered,
		FABTSGuideEventIds::BridgeBuilt,
		FABTSGuideEventIds::BuildingTargetReady,
		FABTSGuideEventIds::BuildingImpactAccepted,
		FABTSGuideEventIds::SatellitePracticeReady,
		FABTSGuideEventIds::SatelliteAssistPreviewed,
		FABTSGuideEventIds::FinaleReady,
		FABTSGuideEventIds::FinaleAiming,
		FABTSGuideEventIds::FinalePrefixStable,
		FABTSGuideEventIds::FinaleLaunched };
	TSet<FName> UniqueIds;
	for (const FName EventId : EventIds) UniqueIds.Add(EventId);
	TestEqual(TEXT("Every P0 event ID is non-empty and unique"), UniqueIds.Num(), EventIds.Num());
	TestFalse(TEXT("P0 event catalog excludes NAME_None"), UniqueIds.Contains(NAME_None));
	TestEqual(TEXT("Raw branch remains an acquisition subject"),
		FABTSGuideSubjects::FromItem(EABTSItemId::Branch), FABTSGuideSubjects::Branch);
	TestEqual(TEXT("Crafted simple parts do not alias twig tier"),
		FABTSGuideSubjects::FromItem(EABTSItemId::SimpleStake), FABTSGuideSubjects::Simple);
	FABTSGuideRuleRuntime Runtime;
	TSet<uint8> Pictograms;
	for (const FABTSGuideDefinition& Definition : Runtime.GetDefinitions())
	{
		TestTrue(*FString::Printf(TEXT("Guide %s has a P0 pictogram"), *Definition.GuideId.ToString()),
			Definition.Pictogram != EABTSGuidePictogram::None);
		Pictograms.Add(static_cast<uint8>(Definition.Pictogram));
	}
	TestEqual(TEXT("Every current P0 step has a distinct pictogram"),
		Pictograms.Num(), Runtime.GetDefinitions().Num());
	return true;
}

#endif
