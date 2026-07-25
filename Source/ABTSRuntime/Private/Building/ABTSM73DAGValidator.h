// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FABTSM73DAGGenerationResult;
struct FABTSM73DAGGenerationSettings;

/** Structural validation for the pure-data derivation tree and compiled support DAG. */
class FABTSM73DAGValidator
{
public:
	bool Validate(
		const FABTSM73DAGGenerationSettings& Settings,
		const FABTSM73DAGGenerationResult& Result,
		FString& OutError) const;
};

