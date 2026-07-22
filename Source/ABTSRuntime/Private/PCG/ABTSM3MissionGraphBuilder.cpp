// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM3PCGInternal.h"

namespace ABTSM3PCG
{
bool FMissionBuilder::Build(
	const int32 WorldSeed,
	const int32 AttemptIndex,
	const int32 TargetCells,
	TArray<FABTSM3TaskNode>& OutTasks,
	TArray<FABTSM3TaskLink>& OutLinks) const
{
	OutTasks.Reset();
	OutLinks.Reset();
	const TArray<FTaskSpec>& Specs = GetTaskSpecs();
	for (int32 Index = 0; Index < Specs.Num(); ++Index)
	{
		FABTSM3TaskNode& Task = OutTasks.AddDefaulted_GetRef();
		Task.TaskId = Index;
		Task.Type = Specs[Index].Type;
	}

	auto AddLink = [&OutTasks, &OutLinks](const int32 TaskA, const int32 TaskB, const EABTSM3TaskLinkRole Role, const EABTSM3ProgressKey Key)
	{
		FABTSM3TaskLink& Link = OutLinks.AddDefaulted_GetRef();
		Link.LinkId = OutLinks.Num() - 1;
		Link.TaskA = TaskA;
		Link.TaskB = TaskB;
		Link.Role = Role;
		Link.RequiredKey = Key;
		OutTasks[TaskA].LinkedTaskIds.AddUnique(TaskB);
		OutTasks[TaskB].LinkedTaskIds.AddUnique(TaskA);
	};

	AddLink(0, 1, EABTSM3TaskLinkRole::MainPath, EABTSM3ProgressKey::None);
	AddLink(1, 2, EABTSM3TaskLinkRole::MainPath, EABTSM3ProgressKey::BuildWorkbench);
	AddLink(2, 3, EABTSM3TaskLinkRole::MainPath, EABTSM3ProgressKey::SimpleSlingshotReady);
	AddLink(3, 4, EABTSM3TaskLinkRole::MainPath, EABTSM3ProgressKey::TargetDestroyed);
	AddLink(4, 5, EABTSM3TaskLinkRole::LockedGate, EABTSM3ProgressKey::BridgeBuilt);
	AddLink(5, 6, EABTSM3TaskLinkRole::MainPath, EABTSM3ProgressKey::ReinforcedSlingshotReady);
	AddLink(2, 7, EABTSM3TaskLinkRole::Branch, EABTSM3ProgressKey::SimpleSlingshotReady);
	AddLink(7, 8, EABTSM3TaskLinkRole::Branch, EABTSM3ProgressKey::ReinforcedSlingshotReady);
	AddLink(8, 6, EABTSM3TaskLinkRole::LateShortcut, EABTSM3ProgressKey::HaveCrystalCore);

	// Stable variation: the late shortcut may reconnect to Furnace instead of the
	// Launch Site, while its late key continues to prevent bridge bypass.
	FRandomStream Stream(MakeStageSeed(WorldSeed, TEXT("Mission"), AttemptIndex));
	if (Stream.RandRange(0, 1) == 1)
	{
		FABTSM3TaskLink& Shortcut = OutLinks.Last();
		OutTasks[Shortcut.TaskB].LinkedTaskIds.Remove(Shortcut.TaskA);
		OutTasks[Shortcut.TaskA].LinkedTaskIds.Remove(Shortcut.TaskB);
		Shortcut.TaskB = 5;
		OutTasks[Shortcut.TaskA].LinkedTaskIds.AddUnique(Shortcut.TaskB);
		OutTasks[Shortcut.TaskB].LinkedTaskIds.AddUnique(Shortcut.TaskA);
	}

	return TargetCells > 0 && OutTasks.Num() == Specs.Num();
}
}
