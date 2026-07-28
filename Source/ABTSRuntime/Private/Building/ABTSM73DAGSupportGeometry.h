// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Building/ABTSM73DAGTypes.h"
#include "CoreMinimal.h"

/**
 * Shared physical support geometry used by DAG layout candidate filtering and
 * the DAG2.3 load solver. The solver stores the accepted centers and the module
 * compiler consumes them verbatim, preventing the support hull from drifting
 * away from the columns that Chaos actually receives.
 */
class FABTSM73DAGSupportGeometry
{
public:
	static bool MakeColumnCenters(
		const FBox2D& Region,
		const FABTSM73DAGLayoutSettings& Settings,
		EABTSM73DAGSupportPattern Pattern,
		float ColumnWidthCM,
		TArray<FVector2D>& OutCenters);
};
