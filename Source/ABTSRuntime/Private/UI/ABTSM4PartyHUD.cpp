// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM4PartyHUD.h"

#include "ABTSRuntime.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Party/ABTSBirdPartySettings.h"

FName AABTSM4PartyHUD::MakeBirdHitBoxName(const int32 BirdIndex) const
{
	return FName(*FString::Printf(TEXT("ABTS_Bird_%d"), BirdIndex));
}

void AABTSM4PartyHUD::DrawHUD()
{
	Super::DrawHUD();
	AABTSBirdParty* ResolvedParty = FindParty();
	if (Canvas == nullptr || ResolvedParty == nullptr || !ResolvedParty->IsPartyReady()) return;

	const AABTSBirdPartySettings* Settings = ResolvedParty->GetResolvedSettings();
	const float Diameter = Settings ? Settings->PortraitDiameterPx : 72.0f;
	const float Gap = Settings ? Settings->PortraitGapPx : 18.0f;
	const float RightMargin = Settings ? Settings->RightMarginPx : 42.0f;
	const float TotalHeight = Diameter * 4.0f + Gap * 3.0f;
	const float X = Canvas->ClipX - RightMargin - Diameter;
	const float StartY = (Canvas->ClipY - TotalHeight) * 0.5f;
	UTexture2D* FillTexture = Canvas->DefaultTexture;

	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		const EABTSBirdId BirdId = static_cast<EABTSBirdId>(BirdIndex);
		const FABTSBirdPresentationConfig* Presentation = ResolvedParty->GetPresentation(BirdId);
		if (Presentation == nullptr) continue;
		const FVector2D Center(X + Diameter * 0.5f, StartY + BirdIndex * (Diameter + Gap) + Diameter * 0.5f);
		const bool bControlled = ResolvedParty->GetControlledBirdId() == BirdId;
		if (bControlled)
		{
			Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.61f), 48, FLinearColor(1.0f, 0.86f, 0.18f, 0.98f));
		}
		Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.53f), 48, FLinearColor(0.025f, 0.025f, 0.035f, 0.92f));
		if (Presentation->PortraitTexture)
		{
			Canvas->K2_DrawPolygon(Presentation->PortraitTexture, Center, FVector2D(Diameter * 0.48f), 48, FLinearColor::White);
		}
		else
		{
			Canvas->K2_DrawPolygon(FillTexture, Center, FVector2D(Diameter * 0.48f), 48, Presentation->FallbackColor);
		}
		AddHitBox(FVector2D(X, Center.Y - Diameter * 0.5f), FVector2D(Diameter), MakeBirdHitBoxName(BirdIndex), true, 10);
	}

	if (GEngine)
	{
		DrawText(TEXT("TAB: Switch Bird"), FLinearColor::White, X - 22.0f, StartY + TotalHeight + 20.0f, GEngine->GetSmallFont(), 0.9f, false);
	}
}

void AABTSM4PartyHUD::NotifyHitBoxClick(const FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	AABTSBirdParty* ResolvedParty = FindParty();
	if (ResolvedParty == nullptr) return;
	for (int32 BirdIndex = 0; BirdIndex < 4; ++BirdIndex)
	{
		if (BoxName == MakeBirdHitBoxName(BirdIndex))
		{
			ResolvedParty->SwitchControlledBird(static_cast<EABTSBirdId>(BirdIndex));
			return;
		}
	}
}

AABTSBirdParty* AABTSM4PartyHUD::FindParty()
{
	if (Party.IsValid()) return Party.Get();
	for (TActorIterator<AABTSBirdParty> It(GetWorld()); It; ++It)
	{
		Party = *It;
		return Party.Get();
	}
	return nullptr;
}
