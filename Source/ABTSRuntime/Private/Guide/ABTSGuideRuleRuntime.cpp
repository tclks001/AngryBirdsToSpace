// Copyright Epic Games, Inc. All Rights Reserved.

#include "Guide/ABTSGuideRuleRuntime.h"

#include "ABTSRuntime.h"

#define LOCTEXT_NAMESPACE "ABTSGuideRules"

namespace
{
	FABTSGuideFactCondition Fact(const FName EventId, const FName SubjectId = NAME_None, const int32 Count = 1)
	{
		FABTSGuideFactCondition Result;
		Result.Key.EventId = EventId;
		Result.Key.SubjectId = SubjectId;
		Result.MinimumCount = Count;
		return Result;
	}

	FABTSGuideDefinition Guide(
		const TCHAR* Id,
		const FText& Title,
		const FText& Body,
		const FText& Hint,
		const EABTSGuideAnchorMode AnchorMode,
		const EABTSGuidePictogram Pictogram)
	{
		FABTSGuideDefinition Result;
		Result.GuideId = FName(Id);
		Result.Title = Title;
		Result.Body = Body;
		Result.InputHint = Hint;
		Result.AnchorMode = AnchorMode;
		Result.Pictogram = Pictogram;
		return Result;
	}
}

FABTSGuideRuleRuntime::FABTSGuideRuleRuntime()
{
	BuildP0Definitions();
}

void FABTSGuideRuleRuntime::BuildP0Definitions()
{
	Definitions.Reset();

	FABTSGuideDefinition Collect = Guide(
		TEXT("Guide.P0.CollectResources"),
		LOCTEXT("CollectTitle", "先收集材料"),
		LOCTEXT("CollectBody", "靠近树枝和植物纤维，把它们收入背包。"),
		LOCTEXT("CollectHint", "移动到发光材料旁"),
		EABTSGuideAnchorMode::ControlledBird,
		EABTSGuidePictogram::CollectResources);
	Collect.RequiredFacts = { Fact(FABTSGuideEventIds::WorldReady) };
	Collect.CompletionFacts = {
		Fact(FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::Branch),
		Fact(FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::PlantFiber) };
	Definitions.Add(MoveTemp(Collect));

	FABTSGuideDefinition Install = Guide(
		TEXT("Guide.P0.InstallTwigStakes"),
		LOCTEXT("InstallTitle", "插入两根弹弓桩"),
		LOCTEXT("InstallBody", "拿起树枝，依次点击同一弹弓槽的两个土坑。"),
		LOCTEXT("InstallHint", "左键：安装弹弓桩"),
		EABTSGuideAnchorMode::ControlledBird,
		EABTSGuidePictogram::InstallStakes);
	Install.RequiredFacts = {
		Fact(FABTSGuideEventIds::WorldReady),
		Fact(FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::Branch),
		Fact(FABTSGuideEventIds::ItemAcquired, FABTSGuideSubjects::PlantFiber) };
	Install.CompletionFacts = { Fact(FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig, 2) };
	Definitions.Add(MoveTemp(Install));

	FABTSGuideDefinition FirstEndpoint = Guide(
		TEXT("Guide.P0.SelectFirstEndpoint"),
		LOCTEXT("FirstEndpointTitle", "连接第一根桩"),
		LOCTEXT("FirstEndpointBody", "拿起植物纤维，点击刚才安装的一根树枝。"),
		LOCTEXT("FirstEndpointHint", "左键：选择第一端"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::ConnectFirst);
	FirstEndpoint.RequiredFacts = { Fact(FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig, 2) };
	FirstEndpoint.CompletionFacts = { Fact(FABTSGuideEventIds::CordEndpointSelected, FABTSGuideSubjects::Twig) };
	FirstEndpoint.AnchorFact = { FABTSGuideEventIds::StakeInstalled, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(FirstEndpoint));

	FABTSGuideDefinition SecondEndpoint = Guide(
		TEXT("Guide.P0.SelectSecondEndpoint"),
		LOCTEXT("SecondEndpointTitle", "连接相邻的桩"),
		LOCTEXT("SecondEndpointBody", "再点击同一槽位的另一根桩，组成完整弹弓。"),
		LOCTEXT("SecondEndpointHint", "左键：连接第二端"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::ConnectSecond);
	SecondEndpoint.RequiredFacts = { Fact(FABTSGuideEventIds::CordEndpointSelected, FABTSGuideSubjects::Twig) };
	SecondEndpoint.CompletionFacts = { Fact(FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig) };
	SecondEndpoint.AnchorFact = { FABTSGuideEventIds::CordEndpointSelected, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(SecondEndpoint));

	FABTSGuideDefinition SwitchBlue = Guide(
		TEXT("Guide.P0.SwitchBlue"),
		LOCTEXT("SwitchBlueTitle", "换青翎来侦察"),
		LOCTEXT("SwitchBlueBody", "树枝弹弓只能由青翎执行侦察发射。"),
		LOCTEXT("SwitchBlueHint", "按 2 或点击青翎头像"),
		EABTSGuideAnchorMode::ControlledBird,
		EABTSGuidePictogram::SwitchBird);
	SwitchBlue.RequiredFacts = { Fact(FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig) };
	SwitchBlue.CompletionFacts = { Fact(FABTSGuideEventIds::ControlledBirdChanged, FABTSGuideSubjects::Blue) };
	Definitions.Add(MoveTemp(SwitchBlue));

	FABTSGuideDefinition Enter = Guide(
		TEXT("Guide.P0.EnterLaunchMode"),
		LOCTEXT("EnterTitle", "点击弹弓袋"),
		LOCTEXT("EnterBody", "点击两根树枝之间的弹弓袋，进入发射模式。"),
		LOCTEXT("EnterHint", "左键：进入发射模式"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::EnterLaunch);
	Enter.RequiredFacts = {
		Fact(FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig),
		Fact(FABTSGuideEventIds::ControlledBirdChanged, FABTSGuideSubjects::Blue) };
	Enter.CompletionFacts = { Fact(FABTSGuideEventIds::SlingshotReady, FABTSGuideSubjects::Twig) };
	Enter.AnchorFact = { FABTSGuideEventIds::SlingshotAssembled, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(Enter));

	FABTSGuideDefinition Pull = Guide(
		TEXT("Guide.P0.PullPouch"),
		LOCTEXT("PullTitle", "拖动弹弓袋"),
		LOCTEXT("PullBody", "按住弹弓袋向后拖动，调整发射方向。"),
		LOCTEXT("PullHint", "按住左键并拖动"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::PullPouch);
	Pull.RequiredFacts = { Fact(FABTSGuideEventIds::SlingshotReady, FABTSGuideSubjects::Twig) };
	Pull.CompletionFacts = { Fact(FABTSGuideEventIds::SlingshotPulling, FABTSGuideSubjects::Twig) };
	Pull.AnchorFact = { FABTSGuideEventIds::SlingshotReady, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(Pull));

	FABTSGuideDefinition Power = Guide(
		TEXT("Guide.P0.AdjustPower"),
		LOCTEXT("PowerTitle", "滚轮调整力度"),
		LOCTEXT("PowerBody", "拖动时滚动鼠标滚轮，观察轨迹长度和落点变化。"),
		LOCTEXT("PowerHint", "鼠标滚轮：调整力度"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::AdjustPower);
	Power.RequiredFacts = { Fact(FABTSGuideEventIds::SlingshotPulling, FABTSGuideSubjects::Twig) };
	Power.CompletionFacts = { Fact(FABTSGuideEventIds::SlingshotPowerChanged, FABTSGuideSubjects::Twig) };
	Power.AnchorFact = { FABTSGuideEventIds::SlingshotPulling, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(Power));

	FABTSGuideDefinition Release = Guide(
		TEXT("Guide.P0.ReleaseLaunch"),
		LOCTEXT("ReleaseTitle", "松开发射"),
		LOCTEXT("ReleaseBody", "确认轨迹后松开弹弓袋，让青翎飞向目标区域。"),
		LOCTEXT("ReleaseHint", "松开左键：发射"),
		EABTSGuideAnchorMode::EventActor,
		EABTSGuidePictogram::ReleaseLaunch);
	Release.RequiredFacts = { Fact(FABTSGuideEventIds::SlingshotPowerChanged, FABTSGuideSubjects::Twig) };
	Release.CompletionFacts = { Fact(FABTSGuideEventIds::SlingshotLaunched, FABTSGuideSubjects::Twig) };
	Release.AnchorFact = { FABTSGuideEventIds::SlingshotPowerChanged, FABTSGuideSubjects::Twig };
	Definitions.Add(MoveTemp(Release));
}

void FABTSGuideRuleRuntime::Reset()
{
	FactCounts.Reset();
	LastPayloads.Reset();
	CompletedGuideIds.Reset();
	ActiveDefinitionIndex = INDEX_NONE;
	ActiveSnapshot = FABTSGuidePresentationSnapshot();
}

int32 FABTSGuideRuleRuntime::GetEventCount(const FName EventId, const FName SubjectId) const
{
	return FactCounts.FindRef({ EventId, SubjectId });
}

bool FABTSGuideRuleRuntime::AreConditionsMet(const TArray<FABTSGuideFactCondition>& Conditions) const
{
	if (Conditions.IsEmpty()) return false;
	for (const FABTSGuideFactCondition& Condition : Conditions)
	{
		if (FactCounts.FindRef(Condition.Key) < Condition.MinimumCount) return false;
	}
	return true;
}

void FABTSGuideRuleRuntime::PublishEvent(const FName EventId, const FABTSGuideEventPayload& Payload)
{
	const FABTSGuideFactKey GenericKey { EventId, NAME_None };
	++FactCounts.FindOrAdd(GenericKey);
	LastPayloads.Add(GenericKey, Payload);
	if (!Payload.SubjectId.IsNone())
	{
		const FABTSGuideFactKey SubjectKey { EventId, Payload.SubjectId };
		++FactCounts.FindOrAdd(SubjectKey);
		LastPayloads.Add(SubjectKey, Payload);
	}
	EvaluateRules();
}

void FABTSGuideRuleRuntime::EvaluateRules()
{
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FABTSGuideDefinition& Definition = Definitions[Index];
		if (CompletedGuideIds.Contains(Definition.GuideId)
			|| !AreConditionsMet(Definition.CompletionFacts))
		{
			continue;
		}
		CompletedGuideIds.Add(Definition.GuideId);
		UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Guide][Complete] Guide=%s"), *Definition.GuideId.ToString());
		if (ActiveDefinitionIndex == Index)
		{
			ActiveDefinitionIndex = INDEX_NONE;
			ActiveSnapshot = FABTSGuidePresentationSnapshot();
		}
	}

	if (ActiveDefinitionIndex != INDEX_NONE) return;
	int32 CandidateIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FABTSGuideDefinition& Definition = Definitions[Index];
		if (CompletedGuideIds.Contains(Definition.GuideId)
			|| !AreConditionsMet(Definition.RequiredFacts))
		{
			continue;
		}
		if (CandidateIndex == INDEX_NONE
			|| Definition.Priority > Definitions[CandidateIndex].Priority)
		{
			CandidateIndex = Index;
		}
	}
	if (CandidateIndex != INDEX_NONE) ActivateGuide(CandidateIndex);
}

void FABTSGuideRuleRuntime::ActivateGuide(const int32 DefinitionIndex)
{
	ActiveDefinitionIndex = DefinitionIndex;
	const FABTSGuideDefinition& Definition = Definitions[DefinitionIndex];
	ActiveSnapshot = FABTSGuidePresentationSnapshot();
	ActiveSnapshot.GuideId = Definition.GuideId;
	ActiveSnapshot.Title = Definition.Title;
	ActiveSnapshot.Body = Definition.Body;
	ActiveSnapshot.InputHint = Definition.InputHint;
	ActiveSnapshot.StepNumber = DefinitionIndex + 1;
	ActiveSnapshot.TotalSteps = Definitions.Num();
	ActiveSnapshot.AnchorMode = Definition.AnchorMode;
	ActiveSnapshot.Pictogram = Definition.Pictogram;
	if (const FABTSGuideEventPayload* Payload = LastPayloads.Find(Definition.AnchorFact))
	{
		ActiveSnapshot.AnchorActor = Payload->AnchorActor;
		ActiveSnapshot.WorldLocation = Payload->WorldLocation;
		ActiveSnapshot.bHasWorldLocation = Payload->bHasWorldLocation;
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Guide][Activate] Guide=%s Step=%d/%d"),
		*Definition.GuideId.ToString(), ActiveSnapshot.StepNumber, ActiveSnapshot.TotalSteps);
}

bool FABTSGuideRuleRuntime::GetActiveGuide(FABTSGuidePresentationSnapshot& OutSnapshot) const
{
	if (ActiveDefinitionIndex == INDEX_NONE) return false;
	OutSnapshot = ActiveSnapshot;
	return true;
}

#undef LOCTEXT_NAMESPACE
