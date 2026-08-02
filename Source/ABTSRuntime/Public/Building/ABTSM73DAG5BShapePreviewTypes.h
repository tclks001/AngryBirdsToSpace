// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ABTSM73DAG5BShapePreviewTypes.generated.h"

/** Coarse starting topology. Generic grammar rules recursively refine it. */
UENUM(BlueprintType)
enum class EABTSM73DAG5BV2Archetype : uint8
{
	Auto,
	TerracedCitadel,
	TwinTowerComplex,
	BridgedArcology,
	SpiredCampus
};

/** Visible primitive selected by the graph WFC after grammar expansion. */
UENUM(BlueprintType)
enum class EABTSM73DAG5BV2Primitive : uint8
{
	Box,
	TriangularPrismX,
	TriangularPrismY,
	Pyramid
};

/** Semantic role of one generated silhouette volume. */
UENUM(BlueprintType)
enum class EABTSM73DAG5BV2VolumeRole : uint8
{
	Foundation,
	Body,
	Annex,
	Bridge,
	Crown,
	/** Intentionally elevated span with two resolved endpoint support volumes. */
	SupportedSpan
};

/** Editor-facing parameters for the DAG5-B v2 silhouette-only prototype. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5BV2PreviewSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	int32 BuildingSeed = 735201;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 GeneratorVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar")
	EABTSM73DAG5BV2Archetype Archetype =
		EABTSM73DAG5BV2Archetype::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds",
		meta = (ClampMin = "400.0", ClampMax = "10000.0", Units = "cm"))
	float TargetWidthCM = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds",
		meta = (ClampMin = "300.0", ClampMax = "10000.0", Units = "cm"))
	float TargetDepthCM = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounds",
		meta = (ClampMin = "500.0", ClampMax = "12000.0", Units = "cm"))
	float TargetHeightCM = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar",
		meta = (ClampMin = "0", ClampMax = "5"))
	int32 MinGrammarDepth = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 MaxGrammarDepth = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar",
		meta = (ClampMin = "8", ClampMax = "256"))
	int32 MaxVolumeCount = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar",
		meta = (ClampMin = "80.0", ClampMax = "1000.0", Units = "cm"))
	float MinVolumeSpanCM = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Rules",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float StackWeight = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Rules",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float HorizontalSplitWeight = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Rules",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float SetbackWeight = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Rules",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float TerminalWeight = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Form",
		meta = (ClampMin = "0.04", ClampMax = "0.30"))
	float SplitGapRatio = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Form",
		meta = (ClampMin = "0.04", ClampMax = "0.35"))
	float SetbackRatio = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Form",
		meta = (ClampMin = "0.0", ClampMax = "0.30"))
	float MaxOffsetRatio = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Form",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BridgeChance = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Grammar|Form",
		meta = (ClampMin = "0.05", ClampMax = "0.30"))
	float BridgeThicknessRatio = 0.11f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Weights",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BoxWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Weights",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float PrismWeight = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Weights",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float PyramidWeight = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC")
	bool bRequirePrimitiveVariety = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Budget",
		meta = (ClampMin = "64", ClampMax = "131072"))
	int32 MaxWFCPropagationOperations = 32768;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WFC|Budget",
		meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxWFCBacktrackSteps = 64;
};

/** Compact editor-visible evidence from the latest preview rebuild. */
USTRUCT(BlueprintType)
struct FABTSM73DAG5BV2PreviewSummary
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	EABTSM73DAG5BV2Archetype ResolvedArchetype =
		EABTSM73DAG5BV2Archetype::Auto;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 GrammarStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 VolumeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 BoxCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 PrismCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 PyramidCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 SupportedSpanCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 WFCPropagationOperationCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	int32 WFCBacktrackStepCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 GrammarHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 WFCHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	int64 ResultHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result")
	FString RejectReason;
};
