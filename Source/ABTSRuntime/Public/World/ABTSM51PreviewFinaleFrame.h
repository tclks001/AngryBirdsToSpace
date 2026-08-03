// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM110FinaleTypes.h"
#include "ABTSM51PreviewFinaleFrame.generated.h"

struct FABTSM3MonthlyFinaleAnchorPreview;

UENUM(BlueprintType)
enum class EABTSM51FinaleFrameAuthority : uint8
{
	None = 0,
	PreviewTest = 1
};

/**
 * Integration-owned Preview/Test context shared by M5.1 finale slots and M11.
 * It never replaces the M3 production FinaleLaunchFrame or publishes a
 * MonthlyAccepted world.
 */
USTRUCT(BlueprintType)
struct ABTSRUNTIME_API FABTSM51PreviewFinaleFrameContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	EABTSM51FinaleFrameAuthority Authority =
		EABTSM51FinaleFrameAuthority::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	int32 SourceRouteCandidateId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	int64 SourceSpatialCandidateHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	int64 SourcePlanResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	int64 SourcePreviewHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	FABTSM110FinaleLocalFrame Frame;

	/** Must remain false until a future monthly-world acceptance stage. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	bool bMonthlyWorldAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|Integration|Preview Finale")
	int64 ContextHash = 0;

	bool IsUsable(double Tolerance = 1.0e-3) const;
};

/** Converts one explicit M3R-5.2 anchor preview into the shared test frame. */
class ABTSRUNTIME_API FABTSM51PreviewFinaleFrameAdapter
{
public:
	static bool Build(
		const FABTSM3MonthlyFinaleAnchorPreview& Preview,
		const FABTSM110FinaleLocalFrame& CompatibilityFrame,
		FABTSM51PreviewFinaleFrameContext& OutContext,
		FString& OutFailure);

	static uint64 ComputeContextHash(
		const FABTSM51PreviewFinaleFrameContext& Context);
};
