// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM1HUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void AABTSM1HUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr || GEngine == nullptr)
	{
		return;
	}

	UFont* Font = GEngine->GetSmallFont();
	DrawText(TEXT("ANGRY BIRDS TO SPACE  |  M1 Independent Entry"), FLinearColor(1.0f, 0.78f, 0.22f), 36.0f, 32.0f, Font, 1.15f, false);
	DrawText(TEXT("WASD: Move   Mouse: Camera   Esc: Pause"), FLinearColor::White, 36.0f, 64.0f, Font, 0.92f, false);

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawLine(CenterX - 7.0f, CenterY, CenterX + 7.0f, CenterY, FLinearColor::White, 1.0f);
	DrawLine(CenterX, CenterY - 7.0f, CenterX, CenterY + 7.0f, FLinearColor::White, 1.0f);
}
