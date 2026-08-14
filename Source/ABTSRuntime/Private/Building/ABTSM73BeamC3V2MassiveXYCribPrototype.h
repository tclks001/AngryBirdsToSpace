// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM7BuildingTypes.h"

namespace ABTSM73BeamC3V2
{
	enum class ECourseAxis : uint8
	{
		X,
		Y
	};

	/**
	 * Stage-0 experiment contract for the no-Z stability core.
	 *
	 * These dimensions are deliberately constants rather than a search space.
	 * The prototype must be accepted or rejected before any production IR or
	 * Profile/Tier candidate search is changed.
	 */
	struct FMassiveXYCribSettings
	{
		static constexpr float StandardSectionCM = 36.0f;
		static constexpr int32 LogSectionMultiplier = 3;
		static constexpr float LogSectionCM =
			StandardSectionCM * LogSectionMultiplier;
		static constexpr float LogLengthCM = LogSectionCM * 4.0f;
		static constexpr float RailCenterOffsetCM =
			StandardSectionCM * 4.0f;
		/** Stage-0 is an evidence fixture, not an unbounded allocation API. */
		static constexpr int32 MaximumSupportedCoreBrickCount = 4096;

		/** Six complete X/Y pairs: the quantized worst E1 (TipOver) body height. */
		float TargetBodyHeightCM = 1296.0f;
		int32 MaximumCoreBrickCount = 24;
		EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	};

	struct FMassiveXYCribBrick
	{
		int32 CourseIndex = INDEX_NONE;
		int32 RailIndex = INDEX_NONE;
		ECourseAxis Axis = ECourseAxis::X;
		FVector CenterCM = FVector::ZeroVector;
		FVector DimensionsCM = FVector::ZeroVector;
		FBox Bounds = FBox(EForceInit::ForceInit);

		FABTSM7BrickSpec MakeBrickSpec(
			EABTSM7BuildingMaterial Material) const;
	};

	struct FMassiveXYCribResult
	{
		bool bAccepted = false;
		int32 PairCount = 0;
		int32 CourseCount = 0;
		int32 AdjacentCourseContactCount = 0;
		float RealizedBodyHeightCM = 0.0f;
		/** CRC32 of realized Brick geometry only; requested height is not identity. */
		uint32 GeometryCrc32 = 0;
		TArray<FMassiveXYCribBrick> Bricks;
		FString RejectReason;
	};

	class FMassiveXYCribPrototype final
	{
	public:
		/** Two rails per course and one complete X/Y pair per height quantum. */
		static int32 ComputeMinimumBrickCount(
			float TargetBodyHeightCM,
			float LogSectionCM);

		static bool Build(
			const FMassiveXYCribSettings& Settings,
			FMassiveXYCribResult& OutResult,
			FString& OutError);
	};
}
