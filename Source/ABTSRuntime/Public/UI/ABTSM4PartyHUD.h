// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ABTSM4PartyHUD.generated.h"

class AABTSBirdParty;
class UTexture2D;
enum class EABTSBirdId : uint8;
struct FABTSUIThemeSnapshot;
struct FABTSGuidePresentationSnapshot;

/** Asset-optional fixed-order four-bird portrait HUD. */
UCLASS()
class ABTSRUNTIME_API AABTSM4PartyHUD : public AHUD
{
	GENERATED_BODY()

public:
	AABTSM4PartyHUD();
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	static const TCHAR* GetBirdPortraitAssetPath(EABTSBirdId BirdId);

protected:
	UTexture2D* GetBirdPortraitTexture(EABTSBirdId BirdId) const;

private:
	AABTSBirdParty* FindParty();
	FName MakeBirdHitBoxName(int32 BirdIndex) const;
	void DrawThemeDebugOverlay(const FABTSUIThemeSnapshot& Theme);
	void DrawGuideOverlay(const FABTSUIThemeSnapshot& Theme);
	void DrawGuidePictogram(
		const FABTSGuidePresentationSnapshot& Guide,
		const FBox2D& IconBox,
		const FABTSUIThemeSnapshot& Theme);

	TWeakObjectPtr<AABTSBirdParty> Party;
	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> BirdPortraitTextures;
};
