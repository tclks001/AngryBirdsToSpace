// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSM5InventoryHUDData.h"

bool FABTSM5InventoryHUDData::ResolveLayout(
	const float ViewportWidth,
	const float ViewportHeight,
	const float RequestedSlotSize,
	const int32 SlotCount,
	FABTSM5InventoryUILayout& OutLayout)
{
	OutLayout = FABTSM5InventoryUILayout();
	if (!FMath::IsFinite(ViewportWidth)
		|| !FMath::IsFinite(ViewportHeight)
		|| ViewportWidth < 800.0f
		|| ViewportHeight < 600.0f
		|| SlotCount <= 0
		|| SlotCount > 12)
	{
		return false;
	}

	OutLayout.bCompact = ViewportWidth < 1180.0f || ViewportHeight < 700.0f;
	const float SafeMargin = OutLayout.bCompact ? 16.0f : 28.0f;
	const float Gap = OutLayout.bCompact ? 5.0f : 7.0f;
	const float BagWidth = OutLayout.bCompact ? 64.0f : 76.0f;
	const float AvailableHotbarWidth = ViewportWidth - SafeMargin * 2.0f - BagWidth - Gap * 4.0f;
	const float SlotSize = FMath::Clamp(
		FMath::Min(RequestedSlotSize, AvailableHotbarWidth / (SlotCount + 1)),
		52.0f,
		96.0f);
	const float HotbarContentWidth = BagWidth + Gap + SlotSize * SlotCount + Gap * 2.0f + SlotSize;
	const float ShellPadding = 8.0f;
	const float ShellWidth = HotbarContentWidth + ShellPadding * 2.0f;
	const FVector2D ShellOrigin(
		(ViewportWidth - ShellWidth) * 0.5f,
		ViewportHeight - SlotSize - ShellPadding * 2.0f - SafeMargin);
	OutLayout.HotbarShell = FBox2D(
		ShellOrigin,
		ShellOrigin + FVector2D(ShellWidth, SlotSize + ShellPadding * 2.0f));

	const FVector2D ContentOrigin = ShellOrigin + FVector2D(ShellPadding);
	OutLayout.BagButton = FBox2D(ContentOrigin, ContentOrigin + FVector2D(BagWidth, SlotSize));
	OutLayout.HotbarSlots.Reserve(SlotCount);
	const float SlotsStartX = ContentOrigin.X + BagWidth + Gap;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const FVector2D SlotOrigin(SlotsStartX + SlotIndex * SlotSize, ContentOrigin.Y);
		OutLayout.HotbarSlots.Emplace(SlotOrigin, SlotOrigin + FVector2D(SlotSize));
	}
	const FVector2D HeldOrigin(
		SlotsStartX + SlotSize * SlotCount + Gap * 2.0f,
		ContentOrigin.Y);
	OutLayout.HeldSlot = FBox2D(HeldOrigin, HeldOrigin + FVector2D(SlotSize));

	const float ModalWidth = FMath::Min(ViewportWidth - SafeMargin * 2.0f, 1320.0f);
	const float ModalHeight = FMath::Min(ViewportHeight - SafeMargin * 2.0f, 760.0f);
	const FVector2D ModalOrigin(
		(ViewportWidth - ModalWidth) * 0.5f,
		(ViewportHeight - ModalHeight) * 0.5f);
	OutLayout.ModalShell = FBox2D(ModalOrigin, ModalOrigin + FVector2D(ModalWidth, ModalHeight));

	const float HeaderHeight = OutLayout.bCompact ? 54.0f : 64.0f;
	const float ContentGap = OutLayout.bCompact ? 10.0f : 14.0f;
	const float ContentPadding = OutLayout.bCompact ? 14.0f : 18.0f;
	const float ContentTop = ModalOrigin.Y + HeaderHeight;
	const float ContentHeight = ModalHeight - HeaderHeight - ContentPadding;
	const float InventoryWidth = FMath::Clamp(ModalWidth * 0.36f, 330.0f, 470.0f);
	const FVector2D InventoryOrigin(ModalOrigin.X + ContentPadding, ContentTop);
	OutLayout.InventoryPanel = FBox2D(
		InventoryOrigin,
		InventoryOrigin + FVector2D(InventoryWidth, ContentHeight));
	const FVector2D RecipeOrigin(OutLayout.InventoryPanel.Max.X + ContentGap, ContentTop);
	OutLayout.RecipePanel = FBox2D(
		RecipeOrigin,
		FVector2D(ModalOrigin.X + ModalWidth - ContentPadding, ContentTop + ContentHeight));
	const float CloseSize = OutLayout.bCompact ? 34.0f : 40.0f;
	const FVector2D CloseOrigin(
		ModalOrigin.X + ModalWidth - ContentPadding - CloseSize,
		ModalOrigin.Y + 10.0f);
	OutLayout.CloseButton = FBox2D(CloseOrigin, CloseOrigin + FVector2D(CloseSize));
	return OutLayout.InventoryPanel.bIsValid
		&& OutLayout.RecipePanel.bIsValid
		&& OutLayout.InventoryPanel.Max.X < OutLayout.RecipePanel.Min.X;
}

void FABTSM5InventoryHUDData::BuildFacetedVertices(
	const FBox2D& Box,
	const float RequestedCutPx,
	TStaticArray<FVector2D, 8>& OutVertices)
{
	const FVector2D Size = Box.bIsValid ? Box.Max - Box.Min : FVector2D::ZeroVector;
	const float Cut = FMath::Clamp(RequestedCutPx, 0.0f, FMath::Min(Size.X, Size.Y) * 0.25f);
	OutVertices = {
		FVector2D(Box.Min.X + Cut, Box.Min.Y),
		FVector2D(Box.Max.X - Cut, Box.Min.Y),
		FVector2D(Box.Max.X, Box.Min.Y + Cut),
		FVector2D(Box.Max.X, Box.Max.Y - Cut),
		FVector2D(Box.Max.X - Cut, Box.Max.Y),
		FVector2D(Box.Min.X + Cut, Box.Max.Y),
		FVector2D(Box.Min.X, Box.Max.Y - Cut),
		FVector2D(Box.Min.X, Box.Min.Y + Cut)};
}

bool FABTSM5InventoryHUDData::FitAspectRatio(
	const FBox2D& Bounds,
	const FVector2D& SourceSize,
	FBox2D& OutBox)
{
	OutBox = FBox2D();
	if (!Bounds.bIsValid
		|| !FMath::IsFinite(SourceSize.X)
		|| !FMath::IsFinite(SourceSize.Y)
		|| SourceSize.X <= 0.0f
		|| SourceSize.Y <= 0.0f)
	{
		return false;
	}

	const FVector2D AvailableSize = Bounds.Max - Bounds.Min;
	if (AvailableSize.X <= 0.0f || AvailableSize.Y <= 0.0f) return false;
	const float Scale = FMath::Min(AvailableSize.X / SourceSize.X, AvailableSize.Y / SourceSize.Y);
	const FVector2D FittedSize = SourceSize * Scale;
	const FVector2D FittedMin = Bounds.GetCenter() - FittedSize * 0.5f;
	OutBox = FBox2D(FittedMin, FittedMin + FittedSize);
	return OutBox.bIsValid;
}

bool FABTSM5InventoryHUDData::ResolveCountBadgeLayout(
	const FBox2D& CardBox,
	const FVector2D& ScaledTextSize,
	const float RequestedHeightPx,
	const float RequestedPaddingXPx,
	const float RequestedEdgeInsetPx,
	FABTSM5CountBadgeLayout& OutLayout)
{
	OutLayout = FABTSM5CountBadgeLayout();
	if (!CardBox.bIsValid
		|| !FMath::IsFinite(ScaledTextSize.X)
		|| !FMath::IsFinite(ScaledTextSize.Y)
		|| ScaledTextSize.X <= 0.0f
		|| ScaledTextSize.Y <= 0.0f
		|| !FMath::IsFinite(RequestedHeightPx)
		|| !FMath::IsFinite(RequestedPaddingXPx)
		|| !FMath::IsFinite(RequestedEdgeInsetPx))
	{
		return false;
	}

	const FVector2D CardSize = CardBox.GetSize();
	const float EdgeInset = FMath::Clamp(RequestedEdgeInsetPx, 0.0f, FMath::Min(CardSize.X, CardSize.Y) * 0.20f);
	const float MaximumHeight = CardSize.Y - EdgeInset * 2.0f;
	if (MaximumHeight < 12.0f) return false;
	const float Height = FMath::Clamp(RequestedHeightPx, 12.0f, MaximumHeight);
	const float PaddingX = FMath::Clamp(RequestedPaddingXPx, 2.0f, 16.0f);
	const float MaximumWidth = CardSize.X - EdgeInset * 2.0f;
	const float Width = FMath::Min(FMath::Max(Height, ScaledTextSize.X + PaddingX * 2.0f), MaximumWidth);
	if (Width < ScaledTextSize.X + 2.0f) return false;

	const FVector2D BadgeMax = CardBox.Max - FVector2D(EdgeInset);
	const FVector2D BadgeMin = BadgeMax - FVector2D(Width, Height);
	OutLayout.BadgeBox = FBox2D(BadgeMin, BadgeMax);
	OutLayout.TextOrigin = BadgeMin + FVector2D(
		(Width - ScaledTextSize.X) * 0.5f,
		(Height - ScaledTextSize.Y) * 0.5f);
	return OutLayout.BadgeBox.bIsValid
		&& CardBox.IsInsideOrOn(OutLayout.BadgeBox.Min)
		&& CardBox.IsInsideOrOn(OutLayout.BadgeBox.Max);
}

bool FABTSM5InventoryHUDData::ConsumesPrimaryPointer(
	const FABTSM5InventoryUILayout& Layout,
	const FVector2D& ScreenPosition,
	const bool bCraftingInterfaceOpen)
{
	// The backpack is modal: even its dimmed surround belongs to UI and must never place a
	// held world item. When closed, the complete hotbar shell (including gaps and labels)
	// remains an input-safe region instead of relying only on individual item hit boxes.
	return bCraftingInterfaceOpen
		|| (Layout.HotbarShell.bIsValid
			&& Layout.HotbarShell.IsInsideOrOn(ScreenPosition));
}

const TCHAR* FABTSM5InventoryHUDData::GetRecipeHitBoxPrefix()
{
	return TEXT("ABTS_M5_Recipe_");
}

bool FABTSM5InventoryHUDData::TryParseRecipeHitBoxIndex(
	const FName& HitBoxName,
	int32& OutRecipeIndex)
{
	OutRecipeIndex = INDEX_NONE;
	const FString Name = HitBoxName.ToString();
	const FString Prefix(GetRecipeHitBoxPrefix());
	if (!Name.StartsWith(Prefix, ESearchCase::CaseSensitive)) return false;

	const FString Suffix = Name.RightChop(Prefix.Len());
	if (Suffix.IsEmpty()) return false;
	for (const TCHAR Character : Suffix)
	{
		if (!FChar::IsDigit(Character)) return false;
	}

	const int64 ParsedIndex = FCString::Atoi64(*Suffix);
	if (ParsedIndex < 0 || ParsedIndex > MAX_int32) return false;
	OutRecipeIndex = static_cast<int32>(ParsedIndex);
	return true;
}

const TCHAR* FABTSM5InventoryHUDData::GetItemIconAssetPath(const EABTSItemId ItemId)
{
	switch (ItemId)
	{
	case EABTSItemId::Branch: return TEXT("/Game/Icons/Items/Branch.Branch");
	case EABTSItemId::Stone: return TEXT("/Game/Icons/Items/Stone.Stone");
	case EABTSItemId::Wood: return TEXT("/Game/Icons/Items/Wood.Wood");
	case EABTSItemId::PlantFiber: return TEXT("/Game/Icons/Items/PlantFiber.PlantFiber");
	case EABTSItemId::MetalParts: return TEXT("/Game/Icons/Items/MetalParts.MetalParts");
	case EABTSItemId::CrystalCore: return TEXT("/Game/Icons/Items/CrystalCore.CrystalCore");
	case EABTSItemId::WorkbenchKit: return TEXT("/Game/Icons/Items/WorkbenchKit.WorkbenchKit");
	case EABTSItemId::SimpleStake: return TEXT("/Game/Icons/Items/SimpleStake.SimpleStake");
	case EABTSItemId::SimpleCord: return TEXT("/Game/Icons/Items/SimpleCord.SimpleCord");
	case EABTSItemId::FurnaceKit: return TEXT("/Game/Icons/Items/FurnaceKit.FurnaceKit");
	case EABTSItemId::ReinforcedStake: return TEXT("/Game/Icons/Items/ReinforcedStake.ReinforcedStake");
	case EABTSItemId::ReinforcedCord: return TEXT("/Game/Icons/Items/ReinforcedCord.ReinforcedCord");
	case EABTSItemId::Glass: return TEXT("/Game/Icons/Items/Glass.Glass");
	case EABTSItemId::BridgeKit: return TEXT("/Game/Icons/Items/BridgeKit.BridgeKit");
	case EABTSItemId::SpaceStake: return TEXT("/Game/Icons/Items/SpaceStake.SpaceStake");
	case EABTSItemId::SpaceCord: return TEXT("/Game/Icons/Items/SpaceCord.SpaceCord");
	default: return nullptr;
	}
}

const TCHAR* FABTSM5InventoryHUDData::GetActionIconAtlasAssetPath()
{
	return TEXT("/Game/UI/Icons/T_ABTS_ActionIconAtlas_v001.T_ABTS_ActionIconAtlas_v001");
}

bool FABTSM5InventoryHUDData::GetActionIconUV(const EABTSM5ActionIcon Icon, FBox2D& OutUV)
{
	OutUV = FBox2D();
	const int32 IconIndex = static_cast<int32>(Icon);
	if (IconIndex < 0 || IconIndex >= static_cast<int32>(EABTSM5ActionIcon::Count))
	{
		return false;
	}

	constexpr int32 ColumnCount = 4;
	constexpr int32 RowCount = 2;
	const int32 Column = IconIndex % ColumnCount;
	const int32 Row = IconIndex / ColumnCount;
	const FVector2D CellSize(1.0f / ColumnCount, 1.0f / RowCount);
	const FVector2D UVMin(Column * CellSize.X, Row * CellSize.Y);
	OutUV = FBox2D(UVMin, UVMin + CellSize);
	return true;
}
