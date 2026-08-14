// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/ABTSM5InventoryHUD.h"
#include "ABTSM10ScoutMapHUD.generated.h"

class AABTSBirdParty;
class AABTSM10ScoutMapSystem;
class UTexture2D;

/** M5 inventory/party HUD plus the fixed-frame M10 scout disc. */
UCLASS()
class ABTSRUNTIME_API AABTSM10ScoutMapHUD : public AABTSM5InventoryHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	struct FFlightInstrumentLayout
	{
		float Margin = 0.0f;
		float ScoutDiameter = 0.0f;
		float OrbitalTop = 0.0f;
		float OrbitalDiameter = 0.0f;
	};

	FFlightInstrumentLayout ResolveFlightInstrumentLayout(
		const AABTSM10ScoutMapSystem& System) const;
	void DrawScoutMap(AABTSM10ScoutMapSystem& System);
	void DrawLandingPreview(AABTSM10ScoutMapSystem& System);
	void DrawOrbitalOverview(AABTSM10ScoutMapSystem& System);
	void DrawTrajectoryPreview(
		AABTSM10ScoutMapSystem& System,
		const FVector2D& MapCenter,
		float MapRadius);
	void DrawDashedMapSegment(
		const FVector2D& Start,
		const FVector2D& End,
		const FLinearColor& Color,
		float Thickness,
		float DashLength,
		float GapLength,
		float& InOutPatternDistance);
	void DrawEnvironmentMarker(const FVector2D& Center, UTexture2D* Texture, float SizePx, const FLinearColor& FallbackColor) const;
	void DrawFlightUIReferencePreview();
	void UpdateOffscreenFlightUICapture(AABTSM10ScoutMapSystem* System);
	AABTSM10ScoutMapSystem* FindScoutMapSystem();
	AABTSBirdParty* FindScoutParty();

	TWeakObjectPtr<AABTSM10ScoutMapSystem> ScoutMapSystem;
	TWeakObjectPtr<AABTSBirdParty> ScoutParty;
	bool bFlightCaptureParsed = false;
	bool bFlightCaptureInitialized = false;
	bool bFlightCaptureRequested = false;
	bool bFlightCaptureFinished = false;
	int32 FlightCaptureFrame = 0;
	FString FlightCaptureMode;
	FString FlightCaptureOutputPath;
};
