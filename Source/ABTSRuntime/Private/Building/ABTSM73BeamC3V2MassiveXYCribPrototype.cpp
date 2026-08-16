// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSM73BeamC3V2MassiveXYCribPrototype.h"

#include "Misc/Crc.h"

namespace ABTSM73BeamC3V2
{
	namespace
	{
		int32 QuantizeMillimeters(const float ValueCM)
		{
			return FMath::RoundToInt(ValueCM * 10.0f);
		}
	}

	FABTSM7BrickSpec FMassiveXYCribBrick::MakeBrickSpec(
		const EABTSM7BuildingMaterial Material) const
	{
		FABTSM7BrickSpec Spec;
		Spec.Material = Material;
		Spec.DimensionsCM = DimensionsCM;
		return Spec;
	}

	int32 FMassiveXYCribPrototype::ComputeMinimumBrickCount(
		const float TargetBodyHeightCM,
		const float LogSectionCM)
	{
		if (!FMath::IsFinite(TargetBodyHeightCM)
			|| !FMath::IsFinite(LogSectionCM)
			|| TargetBodyHeightCM <= 0.0f
			|| LogSectionCM <= 0.0f)
		{
			return INDEX_NONE;
		}

		const double PairCountValue = FMath::CeilToDouble(
			static_cast<double>(TargetBodyHeightCM)
			/ (2.0 * static_cast<double>(LogSectionCM)));
		constexpr int32 BricksPerPair = 4;
		if (PairCountValue
			> static_cast<double>(TNumericLimits<int32>::Max() / BricksPerPair))
		{
			return INDEX_NONE;
		}
		return static_cast<int32>(PairCountValue) * BricksPerPair;
	}

	bool FMassiveXYCribPrototype::Build(
		const FMassiveXYCribSettings& Settings,
		FMassiveXYCribResult& OutResult,
		FString& OutError)
	{
		OutResult = FMassiveXYCribResult();
		OutError.Reset();
		if (!FMath::IsFinite(Settings.TargetBodyHeightCM)
			|| Settings.TargetBodyHeightCM <= 0.0f)
		{
			OutError = TEXT("BeamC3V2InvalidTargetBodyHeight");
			OutResult.RejectReason = OutError;
			return false;
		}
		if (Settings.MaximumCoreBrickCount < 4)
		{
			OutError = TEXT("BeamC3V2InvalidCoreBrickBudget");
			OutResult.RejectReason = OutError;
			return false;
		}

		const int32 RequiredBrickCount = ComputeMinimumBrickCount(
			Settings.TargetBodyHeightCM,
			FMassiveXYCribSettings::LogSectionCM);
		if (RequiredBrickCount == INDEX_NONE)
		{
			OutError = TEXT("BeamC3V2InvalidGeometryContract");
			OutResult.RejectReason = OutError;
			return false;
		}
		if (RequiredBrickCount
			> FMassiveXYCribSettings::MaximumSupportedCoreBrickCount)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V2PrototypeScaleUnsupported:%d>%d"),
				RequiredBrickCount,
				FMassiveXYCribSettings::MaximumSupportedCoreBrickCount);
			OutResult.RejectReason = OutError;
			return false;
		}
		if (RequiredBrickCount > Settings.MaximumCoreBrickCount)
		{
			OutError = FString::Printf(
				TEXT("BeamC3V2CoreBudgetInsufficient:%d>%d"),
				RequiredBrickCount,
				Settings.MaximumCoreBrickCount);
			OutResult.RejectReason = OutError;
			return false;
		}

		OutResult.PairCount = RequiredBrickCount / 4;
		OutResult.CourseCount = OutResult.PairCount * 2;
		OutResult.RealizedBodyHeightCM =
			OutResult.CourseCount * FMassiveXYCribSettings::LogSectionCM;
		OutResult.AdjacentCourseContactCount =
			FMath::Max(0, OutResult.CourseCount - 1) * 4;
		OutResult.Bricks.Reserve(RequiredBrickCount);

		FString Canonical = FString::Printf(
			TEXT("BeamC3V2MassiveXYCrib:v2:P=%d:C=%d:S=%d:L=%d:O=%d"),
			OutResult.PairCount,
			OutResult.CourseCount,
			QuantizeMillimeters(FMassiveXYCribSettings::LogSectionCM),
			QuantizeMillimeters(FMassiveXYCribSettings::LogLengthCM),
			QuantizeMillimeters(
				FMassiveXYCribSettings::RailCenterOffsetCM));

		for (int32 CourseIndex = 0;
			CourseIndex < OutResult.CourseCount;
			++CourseIndex)
		{
			const ECourseAxis Axis = (CourseIndex % 2) == 0
				? ECourseAxis::X
				: ECourseAxis::Y;
			const float CenterZ =
				(static_cast<float>(CourseIndex) + 0.5f)
				* FMassiveXYCribSettings::LogSectionCM;
			for (int32 RailIndex = 0; RailIndex < 2; ++RailIndex)
			{
				const float RailCoordinate = RailIndex == 0
					? -FMassiveXYCribSettings::RailCenterOffsetCM
					: FMassiveXYCribSettings::RailCenterOffsetCM;
				FMassiveXYCribBrick& Brick = OutResult.Bricks.AddDefaulted_GetRef();
				Brick.CourseIndex = CourseIndex;
				Brick.RailIndex = RailIndex;
				Brick.Axis = Axis;
				if (Axis == ECourseAxis::X)
				{
					Brick.CenterCM = FVector(0.0f, RailCoordinate, CenterZ);
					Brick.DimensionsCM = FVector(
						FMassiveXYCribSettings::LogLengthCM,
						FMassiveXYCribSettings::LogSectionCM,
						FMassiveXYCribSettings::LogSectionCM);
				}
				else
				{
					Brick.CenterCM = FVector(RailCoordinate, 0.0f, CenterZ);
					Brick.DimensionsCM = FVector(
						FMassiveXYCribSettings::LogSectionCM,
						FMassiveXYCribSettings::LogLengthCM,
						FMassiveXYCribSettings::LogSectionCM);
				}
				Brick.Bounds = FBox::BuildAABB(
					Brick.CenterCM,
					Brick.DimensionsCM * 0.5f);
				Canonical += FString::Printf(
					TEXT("|%d,%d,%d:%d,%d,%d:%d,%d,%d"),
					CourseIndex,
					static_cast<int32>(Axis),
					RailIndex,
					QuantizeMillimeters(Brick.CenterCM.X),
					QuantizeMillimeters(Brick.CenterCM.Y),
					QuantizeMillimeters(Brick.CenterCM.Z),
					QuantizeMillimeters(Brick.DimensionsCM.X),
					QuantizeMillimeters(Brick.DimensionsCM.Y),
					QuantizeMillimeters(Brick.DimensionsCM.Z));
			}
		}

		OutResult.GeometryCrc32 = FCrc::StrCrc32(*Canonical);
		OutResult.bAccepted = true;
		OutResult.RejectReason.Reset();
		return true;
	}
}
