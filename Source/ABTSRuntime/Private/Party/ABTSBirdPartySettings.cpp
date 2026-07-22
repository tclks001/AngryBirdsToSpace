// Copyright Epic Games, Inc. All Rights Reserved.

#include "Party/ABTSBirdPartySettings.h"

AABTSBirdPartySettings::AABTSBirdPartySettings()
{
	PrimaryActorTick.bCanEverTick = false;

	const auto AddBird = [this](
		const EABTSBirdId BirdId,
		const TCHAR* Name,
		const FLinearColor Color,
		const EABTSBirdSlingshotCapability Capability)
	{
		FABTSBirdPresentationConfig& Bird = Birds.AddDefaulted_GetRef();
		Bird.BirdId = BirdId;
		Bird.DisplayName = FText::FromString(Name);
		Bird.FallbackColor = Color;
		Bird.SlingshotCapability = Capability;
	};
	AddBird(EABTSBirdId::Red, TEXT("绯翼"), FLinearColor(0.85f, 0.06f, 0.04f), EABTSBirdSlingshotCapability::Simple);
	AddBird(EABTSBirdId::Blue, TEXT("青翎"), FLinearColor(0.04f, 0.28f, 0.95f), EABTSBirdSlingshotCapability::TwigScout);
	AddBird(EABTSBirdId::Yellow, TEXT("棱喙"), FLinearColor(1.0f, 0.72f, 0.02f), EABTSBirdSlingshotCapability::Simple);
	AddBird(EABTSBirdId::Black, TEXT("玄爪"), FLinearColor(0.015f, 0.015f, 0.02f), EABTSBirdSlingshotCapability::Reinforced);
}
