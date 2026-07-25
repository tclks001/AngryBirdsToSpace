// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGLayoutSettings;
struct FABTSM73StructureData;

/** Rebuilds the physical support graph from final axis-aligned collision boxes and audits DAG-2 intended contacts. */
class FABTSM73DAGContactGraphBuilder
{
public:
	bool RebuildAndAudit(const FABTSM73DAGLayoutSettings& Settings, FABTSM73StructureData& InOutData,
		FString& OutError) const;
};
