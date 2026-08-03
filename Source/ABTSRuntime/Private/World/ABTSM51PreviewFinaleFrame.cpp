// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51PreviewFinaleFrame.h"

#include "PCG/ABTSM3MonthlyFinaleAnchor.h"

namespace
{
class FPreviewFinaleHash64
{
public:
	void AddBytes(const void* Data, const SIZE_T Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (SIZE_T Index = 0; Index < Size; ++Index)
		{
			Value ^= Bytes[Index];
			Value *= 1099511628211ull;
		}
	}

	template <typename TValue>
	void Add(const TValue& InValue)
	{
		AddBytes(&InValue, sizeof(TValue));
	}

	void AddVector(const FVector& Vector)
	{
		Add(Vector.X);
		Add(Vector.Y);
		Add(Vector.Z);
	}

	uint64 Get() const { return Value; }

private:
	uint64 Value = 1469598103934665603ull;
};

bool IsFinitePreviewVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}
}

bool FABTSM51PreviewFinaleFrameContext::IsUsable(
	const double Tolerance) const
{
	return Authority == EABTSM51FinaleFrameAuthority::PreviewTest
		&& SourceRouteCandidateId >= 0
		&& SourceSpatialCandidateHash != 0
		&& SourcePlanResultHash != 0
		&& SourcePreviewHash != 0
		&& !bMonthlyWorldAccepted
		&& Frame.IsUsable(Tolerance)
		&& ContextHash != 0
		&& static_cast<uint64>(ContextHash)
			== FABTSM51PreviewFinaleFrameAdapter::ComputeContextHash(*this);
}

bool FABTSM51PreviewFinaleFrameAdapter::Build(
	const FABTSM3MonthlyFinaleAnchorPreview& Preview,
	const FABTSM110FinaleLocalFrame& CompatibilityFrame,
	FABTSM51PreviewFinaleFrameContext& OutContext,
	FString& OutFailure)
{
	OutContext = FABTSM51PreviewFinaleFrameContext();
	OutFailure.Reset();
	const auto Reject = [&OutFailure](const TCHAR* Reason)
	{
		OutFailure = Reason;
		return false;
	};

	if (!Preview.bPreviewValid
		|| Preview.bMonthlyWorldAccepted
		|| Preview.SourceRouteCandidateId < 0
		|| Preview.SourceSpatialCandidateHash == 0
		|| Preview.SourcePlanResultHash == 0
		|| Preview.PreviewHash == 0)
	{
		return Reject(TEXT("InvalidM3FinaleAnchorPreview"));
	}
	if (!CompatibilityFrame.IsUsable())
	{
		return Reject(TEXT("InvalidCompatibilityFinaleFrame"));
	}
	if (!IsFinitePreviewVector(Preview.FrameOriginWorld)
		|| !IsFinitePreviewVector(Preview.ForwardWorld)
		|| !IsFinitePreviewVector(Preview.RightWorld)
		|| !IsFinitePreviewVector(Preview.UpWorld)
		|| !IsFinitePreviewVector(Preview.LeftSlotWorldLocation)
		|| !IsFinitePreviewVector(Preview.RightSlotWorldLocation))
	{
		return Reject(TEXT("NonFinitePreviewFrame"));
	}

	const FVector Forward = Preview.ForwardWorld.GetSafeNormal();
	const FVector Right = Preview.RightWorld.GetSafeNormal();
	const FVector Up = Preview.UpWorld.GetSafeNormal();
	const double BasisTolerance = 1.0e-3;
	if (Forward.IsNearlyZero()
		|| Right.IsNearlyZero()
		|| Up.IsNearlyZero()
		|| FMath::Abs(FVector::DotProduct(Forward, Right)) > BasisTolerance
		|| FMath::Abs(FVector::DotProduct(Forward, Up)) > BasisTolerance
		|| FMath::Abs(FVector::DotProduct(Right, Up)) > BasisTolerance
		|| FVector::DotProduct(FVector::CrossProduct(Forward, Right), Up)
			< 1.0 - BasisTolerance)
	{
		return Reject(TEXT("InvalidPreviewBasis"));
	}

	const FVector SlotMidpoint =
		(Preview.LeftSlotWorldLocation + Preview.RightSlotWorldLocation) * 0.5;
	const FVector PairDelta =
		Preview.RightSlotWorldLocation - Preview.LeftSlotWorldLocation;
	const FVector PlanarPair =
		FVector::VectorPlaneProject(PairDelta, Up);
	if (!Preview.FrameOriginWorld.Equals(SlotMidpoint, 1.0e-3)
		|| PairDelta.IsNearlyZero()
		|| PlanarPair.IsNearlyZero()
		|| FVector::DotProduct(PlanarPair.GetSafeNormal(), Right)
			< 1.0 - BasisTolerance)
	{
		return Reject(TEXT("PreviewSlotFrameMismatch"));
	}

	FABTSM110FinaleLocalFrame Frame;
	Frame.LayoutVersion = CompatibilityFrame.LayoutVersion;
	Frame.LaunchTaskId = CompatibilityFrame.LaunchTaskId;
	Frame.AnchorCellId = Preview.AnchorCellId;
	Frame.SlotPairId = CompatibilityFrame.SlotPairId;
	Frame.WorldTransform = FTransform(
		FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(),
		Preview.FrameOriginWorld,
		FVector::OneVector);
	Frame.LeftSlotWorldLocation = Preview.LeftSlotWorldLocation;
	Frame.RightSlotWorldLocation = Preview.RightSlotWorldLocation;
	Frame.bValid = true;
	if (!Frame.IsUsable()
		|| FVector::DotProduct(Frame.GetRight(), Right)
			< 1.0 - BasisTolerance)
	{
		return Reject(TEXT("ConstructedFinaleFrameInvalid"));
	}

	OutContext.Authority = EABTSM51FinaleFrameAuthority::PreviewTest;
	OutContext.SourceRouteCandidateId = Preview.SourceRouteCandidateId;
	OutContext.SourceSpatialCandidateHash =
		Preview.SourceSpatialCandidateHash;
	OutContext.SourcePlanResultHash = Preview.SourcePlanResultHash;
	OutContext.SourcePreviewHash = Preview.PreviewHash;
	OutContext.Frame = Frame;
	OutContext.bMonthlyWorldAccepted = false;
	OutContext.ContextHash = static_cast<int64>(ComputeContextHash(OutContext));
	if (!OutContext.IsUsable())
	{
		OutContext = FABTSM51PreviewFinaleFrameContext();
		return Reject(TEXT("PreviewFinaleContextInvalid"));
	}
	return true;
}

uint64 FABTSM51PreviewFinaleFrameAdapter::ComputeContextHash(
	const FABTSM51PreviewFinaleFrameContext& Context)
{
	FPreviewFinaleHash64 Hash;
	const uint8 Authority = static_cast<uint8>(Context.Authority);
	Hash.Add(Authority);
	Hash.Add(Context.SourceRouteCandidateId);
	Hash.Add(Context.SourceSpatialCandidateHash);
	Hash.Add(Context.SourcePlanResultHash);
	Hash.Add(Context.SourcePreviewHash);
	Hash.Add(Context.Frame.LayoutVersion);
	Hash.Add(Context.Frame.LaunchTaskId);
	Hash.Add(Context.Frame.AnchorCellId);
	Hash.Add(Context.Frame.SlotPairId);
	Hash.AddVector(Context.Frame.GetOrigin());
	Hash.AddVector(Context.Frame.GetForward());
	Hash.AddVector(Context.Frame.GetRight());
	Hash.AddVector(Context.Frame.GetUp());
	Hash.AddVector(Context.Frame.LeftSlotWorldLocation);
	Hash.AddVector(Context.Frame.RightSlotWorldLocation);
	Hash.Add(Context.Frame.bValid);
	Hash.Add(Context.bMonthlyWorldAccepted);
	return Hash.Get();
}
