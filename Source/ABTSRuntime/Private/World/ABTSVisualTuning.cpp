// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSVisualTuning.h"

#include "ABTSRuntime.h"
#include "Crafting/ABTSCraftingStation.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"
#include "World/ABTSM8BridgeActors.h"

namespace
{
	using FABTSVisualTuningArray = TStaticArray<
		FABTSVisualTuningValue,
		static_cast<int32>(EABTSVisualTuningTarget::Count)>;

	FABTSVisualTuningArray MakeFrozenVisualTuningValues()
	{
		FABTSVisualTuningArray Values;
		Values[static_cast<int32>(EABTSVisualTuningTarget::Workbench)] = {3.0f, -30.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::Furnace)] = {3.0f, -25.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::Bridge)] = {1.5f, 40.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::StandardSlot)] = {2.0f, 0.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::FinaleSlot)] = {3.0f, 0.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::PickupBranch)] = {3.0f, -25.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::PickupStone)] = {3.0f, -10.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::PickupWood)] = {3.0f, 0.0f};
		Values[static_cast<int32>(EABTSVisualTuningTarget::PickupPlantFiber)] = {4.0f, -20.0f};
		return Values;
	}

	const FABTSVisualTuningArray& GetFrozenVisualTuningValues()
	{
		static const FABTSVisualTuningArray Values = MakeFrozenVisualTuningValues();
		return Values;
	}

	FABTSVisualTuningArray& GetMutableVisualTuningValues()
	{
		static FABTSVisualTuningArray Values = GetFrozenVisualTuningValues();
		return Values;
	}

	const TCHAR* GetVisualTuningCommandName(
		const EABTSVisualTuningTarget Target)
	{
		switch (Target)
		{
		case EABTSVisualTuningTarget::Workbench:
			return TEXT("ABTS.M51.Visual.Workbench");
		case EABTSVisualTuningTarget::Furnace:
			return TEXT("ABTS.M51.Visual.Furnace");
		case EABTSVisualTuningTarget::Bridge:
			return TEXT("ABTS.M8.Visual.Bridge");
		case EABTSVisualTuningTarget::StandardSlot:
			return TEXT("ABTS.M51.Visual.StandardSlot");
		case EABTSVisualTuningTarget::FinaleSlot:
			return TEXT("ABTS.M51.Visual.FinaleSlot");
		case EABTSVisualTuningTarget::PickupBranch:
			return TEXT("ABTS.M51.Visual.Pickup.Branch");
		case EABTSVisualTuningTarget::PickupStone:
			return TEXT("ABTS.M51.Visual.Pickup.Stone");
		case EABTSVisualTuningTarget::PickupWood:
			return TEXT("ABTS.M51.Visual.Pickup.Wood");
		case EABTSVisualTuningTarget::PickupPlantFiber:
			return TEXT("ABTS.M51.Visual.Pickup.PlantFiber");
		default:
			return TEXT("ABTS.Visual.Unknown");
		}
	}

	int32 RefreshVisualTuningInWorld(
		UWorld* World,
		const EABTSVisualTuningTarget Target)
	{
		if (World == nullptr) return 0;
		int32 RefreshedCount = 0;
		switch (Target)
		{
		case EABTSVisualTuningTarget::Workbench:
		case EABTSVisualTuningTarget::Furnace:
			for (TActorIterator<AABTSCraftingStation> It(World); It; ++It)
			{
				const bool bMatches =
					(Target == EABTSVisualTuningTarget::Workbench
						&& It->GetStationType() == EABTSCraftingStationType::Workbench)
					|| (Target == EABTSVisualTuningTarget::Furnace
						&& It->GetStationType() == EABTSCraftingStationType::Furnace);
				if (bMatches)
				{
					It->RefreshVisualTuning();
					++RefreshedCount;
				}
			}
			break;
		case EABTSVisualTuningTarget::Bridge:
			for (TActorIterator<AABTSM8BridgeActor> It(World); It; ++It)
			{
				It->RefreshVisualTuning();
				++RefreshedCount;
			}
			break;
		case EABTSVisualTuningTarget::StandardSlot:
		case EABTSVisualTuningTarget::FinaleSlot:
			for (TActorIterator<AABTSM51SlingshotDirtHole> It(World); It; ++It)
			{
				const bool bMatches =
					(Target == EABTSVisualTuningTarget::FinaleSlot)
						== It->IsFinaleSpaceSlot();
				if (bMatches)
				{
					It->RefreshVisualTuning();
					++RefreshedCount;
				}
			}
			break;
		case EABTSVisualTuningTarget::PickupBranch:
		case EABTSVisualTuningTarget::PickupStone:
		case EABTSVisualTuningTarget::PickupWood:
		case EABTSVisualTuningTarget::PickupPlantFiber:
			for (TActorIterator<AABTSM51PickupItem> It(World); It; ++It)
			{
				EABTSVisualTuningTarget PickupTarget =
					EABTSVisualTuningTarget::PickupBranch;
				switch (It->GetItemId())
				{
				case EABTSItemId::Stone:
					PickupTarget = EABTSVisualTuningTarget::PickupStone;
					break;
				case EABTSItemId::Wood:
					PickupTarget = EABTSVisualTuningTarget::PickupWood;
					break;
				case EABTSItemId::PlantFiber:
					PickupTarget = EABTSVisualTuningTarget::PickupPlantFiber;
					break;
				default:
					break;
				}
				if (PickupTarget == Target)
				{
					It->RefreshVisualTuning();
					++RefreshedCount;
				}
			}
			break;
		default:
			break;
		}
		return RefreshedCount;
	}

	void LogVisualTuningUsage(const EABTSVisualTuningTarget Target)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][VisualTuning] Accepted=0 Usage=%s <ScaleMultiplier> <LocalZOffsetCM> ScaleRange=[0.01,20] ZRange=[-1000,1000]"),
			GetVisualTuningCommandName(Target));
	}

	void SetVisualTuningCommand(
		const TArray<FString>& Args,
		UWorld* World,
		const EABTSVisualTuningTarget Target)
	{
		float ScaleMultiplier = 0.0f;
		float LocalZOffsetCM = 0.0f;
		if (Args.Num() != 2
			|| !LexTryParseString(ScaleMultiplier, *Args[0])
			|| !LexTryParseString(LocalZOffsetCM, *Args[1])
			|| !FMath::IsFinite(ScaleMultiplier)
			|| !FMath::IsFinite(LocalZOffsetCM)
			|| ScaleMultiplier < 0.01f
			|| ScaleMultiplier > 20.0f
			|| FMath::Abs(LocalZOffsetCM) > 1000.0f)
		{
			LogVisualTuningUsage(Target);
			return;
		}

		FABTSVisualTuningValue& Value =
			GetMutableVisualTuningValues()[static_cast<int32>(Target)];
		Value.ScaleMultiplier = ScaleMultiplier;
		Value.LocalZOffsetCM = LocalZOffsetCM;
		const int32 RefreshedCount = RefreshVisualTuningInWorld(World, Target);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][VisualTuning] Accepted=1 Target=%s Scale=%.4f LocalZ=%.2f Refreshed=%d FreezeCommand=%s %.4f %.2f"),
			ABTSGetVisualTuningTargetName(Target),
			ScaleMultiplier,
			LocalZOffsetCM,
			RefreshedCount,
			GetVisualTuningCommandName(Target),
			ScaleMultiplier,
			LocalZOffsetCM);
	}

	void VisualTuningStatusCommand(const TArray<FString>&, UWorld*)
	{
		for (int32 Index = 0;
			Index < static_cast<int32>(EABTSVisualTuningTarget::Count);
			++Index)
		{
			const EABTSVisualTuningTarget Target =
				static_cast<EABTSVisualTuningTarget>(Index);
			const FABTSVisualTuningValue& Value = ABTSGetVisualTuning(Target);
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][VisualTuning][Status] Target=%s Scale=%.4f LocalZ=%.2f FreezeCommand=%s %.4f %.2f"),
				ABTSGetVisualTuningTargetName(Target),
				Value.ScaleMultiplier,
				Value.LocalZOffsetCM,
				GetVisualTuningCommandName(Target),
				Value.ScaleMultiplier,
				Value.LocalZOffsetCM);
		}
	}

	void ResetVisualTuningCommand(const TArray<FString>&, UWorld* World)
	{
		for (int32 Index = 0;
			Index < static_cast<int32>(EABTSVisualTuningTarget::Count);
			++Index)
		{
			GetMutableVisualTuningValues()[Index] = GetFrozenVisualTuningValues()[Index];
			RefreshVisualTuningInWorld(
				World,
				static_cast<EABTSVisualTuningTarget>(Index));
		}
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][VisualTuning] ResetAll=1 Target=FrozenDefaults"));
	}

	void SpawnPickupShowcaseCommand(const TArray<FString>& Args, UWorld* World)
	{
		float DistanceCM = 450.0f;
		if (Args.Num() > 1
			|| (Args.Num() == 1
				&& (!LexTryParseString(DistanceCM, *Args[0])
					|| !FMath::IsFinite(DistanceCM)
					|| DistanceCM <= 0.0f)))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Usage=ABTS.M51.Pickup.SpawnShowcase [DistanceCM]"));
			return;
		}
		if (World != nullptr)
		{
			for (TActorIterator<AABTSM51WorldSystem> It(World); It; ++It)
			{
				It->SpawnPickupShowcase(DistanceCM);
				return;
			}
		}
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M5.1][PickupShowcase] Accepted=0 Reason=WorldSystemUnavailable"));
	}

	FAutoConsoleCommandWithWorldAndArgs GWorkbenchVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Workbench"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::Workbench));
	FAutoConsoleCommandWithWorldAndArgs GFurnaceVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Furnace"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::Furnace));
	FAutoConsoleCommandWithWorldAndArgs GBridgeVisualTuningCommand(
		TEXT("ABTS.M8.Visual.Bridge"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::Bridge));
	FAutoConsoleCommandWithWorldAndArgs GStandardSlotVisualTuningCommand(
		TEXT("ABTS.M51.Visual.StandardSlot"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::StandardSlot));
	FAutoConsoleCommandWithWorldAndArgs GFinaleSlotVisualTuningCommand(
		TEXT("ABTS.M51.Visual.FinaleSlot"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::FinaleSlot));
	FAutoConsoleCommandWithWorldAndArgs GPickupBranchVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Pickup.Branch"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::PickupBranch));
	FAutoConsoleCommandWithWorldAndArgs GPickupStoneVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Pickup.Stone"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::PickupStone));
	FAutoConsoleCommandWithWorldAndArgs GPickupWoodVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Pickup.Wood"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::PickupWood));
	FAutoConsoleCommandWithWorldAndArgs GPickupPlantFiberVisualTuningCommand(
		TEXT("ABTS.M51.Visual.Pickup.PlantFiber"),
		TEXT("PIE only. Args: ScaleMultiplier LocalZOffsetCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SetVisualTuningCommand,
			EABTSVisualTuningTarget::PickupPlantFiber));
	FAutoConsoleCommandWithWorldAndArgs GVisualTuningStatusCommand(
		TEXT("ABTS.Visual.Status"),
		TEXT("Logs copy-ready freeze commands for every temporary visual override."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&VisualTuningStatusCommand));
	FAutoConsoleCommandWithWorldAndArgs GResetVisualTuningCommand(
		TEXT("ABTS.Visual.ResetAll"),
		TEXT("Restores every temporary visual override to its frozen code default."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&ResetVisualTuningCommand));
	FAutoConsoleCommandWithWorldAndArgs GSpawnPickupShowcaseCommand(
		TEXT("ABTS.M51.Pickup.SpawnShowcase"),
		TEXT("PIE only. Spawns Branch, Stone, Wood and PlantFiber on safe ground around the current pawn. Optional arg: DistanceCM."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
			&SpawnPickupShowcaseCommand));
}

const FABTSVisualTuningValue& ABTSGetVisualTuning(
	const EABTSVisualTuningTarget Target)
{
	const int32 Index = FMath::Clamp(
		static_cast<int32>(Target),
		0,
		static_cast<int32>(EABTSVisualTuningTarget::Count) - 1);
	return GetMutableVisualTuningValues()[Index];
}

const TCHAR* ABTSGetVisualTuningTargetName(
	const EABTSVisualTuningTarget Target)
{
	switch (Target)
	{
	case EABTSVisualTuningTarget::Workbench: return TEXT("Workbench");
	case EABTSVisualTuningTarget::Furnace: return TEXT("Furnace");
	case EABTSVisualTuningTarget::Bridge: return TEXT("Bridge");
	case EABTSVisualTuningTarget::StandardSlot: return TEXT("StandardSlot");
	case EABTSVisualTuningTarget::FinaleSlot: return TEXT("FinaleSlot");
	case EABTSVisualTuningTarget::PickupBranch: return TEXT("Pickup.Branch");
	case EABTSVisualTuningTarget::PickupStone: return TEXT("Pickup.Stone");
	case EABTSVisualTuningTarget::PickupWood: return TEXT("Pickup.Wood");
	case EABTSVisualTuningTarget::PickupPlantFiber: return TEXT("Pickup.PlantFiber");
	default: return TEXT("Unknown");
	}
}
