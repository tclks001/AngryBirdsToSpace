// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM7PenetrationValidator.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingModule.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	constexpr float SweepProbeDistanceCM = 0.1f;
	constexpr float RepairPaddingCM = 0.01f;

	uint64 MakeComponentPairKey(const UPrimitiveComponent& A, const UPrimitiveComponent& B)
	{
		const uint32 AId = A.GetUniqueID();
		const uint32 BId = B.GetUniqueID();
		return (static_cast<uint64>(FMath::Min(AId, BId)) << 32) | FMath::Max(AId, BId);
	}

	bool QueryInitialPenetrations(UWorld& World, AABTSM7BuildingModule& Module, TArray<FHitResult>& OutHits)
	{
		UPrimitiveComponent* Component = Module.GetMeshComponent();
		if (Component == nullptr || !Component->IsRegistered() || Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			OutHits.Reset();
			return false;
		}

		FComponentQueryParams Params(SCENE_QUERY_STAT(ABTSM7InitialPenetration), &Module);
		Params.bFindInitialOverlaps = true;
		Params.bTraceComplex = false;
		Params.AddIgnoredActor(&Module);
		const FVector Start = Component->GetComponentLocation();
		FVector ProbeDirection = Component->GetComponentQuat().GetAxisX().GetSafeNormal();
		if (ProbeDirection.IsNearlyZero()) ProbeDirection = FVector::ForwardVector;
		return World.ComponentSweepMultiByChannel(
			OutHits,
			Component,
			Start,
			Start + ProbeDirection * SweepProbeDistanceCM,
			Component->GetComponentQuat(),
			Component->GetCollisionObjectType(),
			Params);
	}

	void LogPenetrationError(
		const TCHAR* Reason,
		const AABTSM7BuildingModule& Module,
		const UPrimitiveComponent& OtherComponent,
		const FHitResult& Hit,
		const float ToleranceCM)
	{
		const FBox ABounds = Module.GetMeshComponent()->Bounds.GetBox();
		const FBox BBounds = OtherComponent.Bounds.GetBox();
		const FVector BoundsOverlap(
			FMath::Min(ABounds.Max.X, BBounds.Max.X)
				- FMath::Max(ABounds.Min.X, BBounds.Min.X),
			FMath::Min(ABounds.Max.Y, BBounds.Max.Y)
				- FMath::Max(ABounds.Min.Y, BBounds.Min.Y),
			FMath::Min(ABounds.Max.Z, BBounds.Max.Z)
				- FMath::Max(ABounds.Min.Z, BBounds.Min.Z));
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][PenetrationValidation] %s A=%s B=%s Depth=%.4fcm Tolerance=%.4fcm Normal=(%.4f,%.4f,%.4f) ALocation=(%.2f,%.2f,%.2f) BLocation=(%.2f,%.2f,%.2f) ABounds=%s..%s BBounds=%s..%s BoundsOverlap=%s"),
			Reason,
			*GetNameSafe(&Module),
			*GetNameSafe(OtherComponent.GetOwner()),
			Hit.PenetrationDepth,
			ToleranceCM,
			Hit.Normal.X,
			Hit.Normal.Y,
			Hit.Normal.Z,
			Module.GetActorLocation().X,
			Module.GetActorLocation().Y,
			Module.GetActorLocation().Z,
			OtherComponent.GetComponentLocation().X,
			OtherComponent.GetComponentLocation().Y,
			OtherComponent.GetComponentLocation().Z,
			*ABounds.Min.ToString(), *ABounds.Max.ToString(),
			*BBounds.Min.ToString(), *BBounds.Max.ToString(),
			*BoundsOverlap.ToString());
	}

	struct FOrientedBounds
	{
		FVector Center = FVector::ZeroVector;
		FVector Axis[3] = {
			FVector::ForwardVector,
			FVector::RightVector,
			FVector::UpVector};
		FVector Extent = FVector::ZeroVector;
	};

	bool TryMakeOrientedBounds(
		const UPrimitiveComponent& Component,
		FOrientedBounds& OutBounds)
	{
		const UStaticMeshComponent* StaticMesh =
			Cast<UStaticMeshComponent>(&Component);
		if (StaticMesh == nullptr || StaticMesh->GetStaticMesh() == nullptr)
		{
			return false;
		}
		FVector LocalMin;
		FVector LocalMax;
		StaticMesh->GetLocalBounds(LocalMin, LocalMax);
		const FTransform& Transform = StaticMesh->GetComponentTransform();
		OutBounds.Center = Transform.TransformPosition(
			(LocalMin + LocalMax) * 0.5);
		OutBounds.Axis[0] = Transform.GetUnitAxis(EAxis::X);
		OutBounds.Axis[1] = Transform.GetUnitAxis(EAxis::Y);
		OutBounds.Axis[2] = Transform.GetUnitAxis(EAxis::Z);
		OutBounds.Extent = (LocalMax - LocalMin) * 0.5
			* Transform.GetScale3D().GetAbs();
		return !OutBounds.Extent.IsNearlyZero();
	}

	bool HasPositiveOrientedBoundsOverlap(
		const UPrimitiveComponent& A,
		const UPrimitiveComponent& B)
	{
		FOrientedBounds BoxA;
		FOrientedBounds BoxB;
		if (!TryMakeOrientedBounds(A, BoxA)
			|| !TryMakeOrientedBounds(B, BoxB))
		{
			const FBox ABounds = A.Bounds.GetBox();
			const FBox BBounds = B.Bounds.GetBox();
			return ABounds.Intersect(BBounds);
		}

		constexpr double ContactSlopCM = 0.01;
		constexpr double ParallelEpsilon = 1.0e-8;
		double Rotation[3][3];
		double AbsoluteRotation[3][3];
		for (int32 AIndex = 0; AIndex < 3; ++AIndex)
		{
			for (int32 BIndex = 0; BIndex < 3; ++BIndex)
			{
				Rotation[AIndex][BIndex] = FVector::DotProduct(
					BoxA.Axis[AIndex], BoxB.Axis[BIndex]);
				AbsoluteRotation[AIndex][BIndex] =
					FMath::Abs(Rotation[AIndex][BIndex]);
			}
		}
		const FVector CenterDelta = BoxB.Center - BoxA.Center;
		const double Translation[3] = {
			FVector::DotProduct(CenterDelta, BoxA.Axis[0]),
			FVector::DotProduct(CenterDelta, BoxA.Axis[1]),
			FVector::DotProduct(CenterDelta, BoxA.Axis[2])};
		const double AExtent[3] = {BoxA.Extent.X, BoxA.Extent.Y, BoxA.Extent.Z};
		const double BExtent[3] = {BoxB.Extent.X, BoxB.Extent.Y, BoxB.Extent.Z};

		for (int32 AIndex = 0; AIndex < 3; ++AIndex)
		{
			double BRadius = 0.0;
			for (int32 BIndex = 0; BIndex < 3; ++BIndex)
			{
				BRadius += BExtent[BIndex]
					* AbsoluteRotation[AIndex][BIndex];
			}
			if (AExtent[AIndex] + BRadius
					- FMath::Abs(Translation[AIndex]) <= ContactSlopCM)
			{
				return false;
			}
		}
		for (int32 BIndex = 0; BIndex < 3; ++BIndex)
		{
			double ARadius = 0.0;
			double ProjectedTranslation = 0.0;
			for (int32 AIndex = 0; AIndex < 3; ++AIndex)
			{
				ARadius += AExtent[AIndex]
					* AbsoluteRotation[AIndex][BIndex];
				ProjectedTranslation += Translation[AIndex]
					* Rotation[AIndex][BIndex];
			}
			if (ARadius + BExtent[BIndex]
					- FMath::Abs(ProjectedTranslation) <= ContactSlopCM)
			{
				return false;
			}
		}
		for (int32 AIndex = 0; AIndex < 3; ++AIndex)
		{
			const int32 ANext = (AIndex + 1) % 3;
			const int32 ALast = (AIndex + 2) % 3;
			for (int32 BIndex = 0; BIndex < 3; ++BIndex)
			{
				if (FVector::CrossProduct(
					BoxA.Axis[AIndex], BoxB.Axis[BIndex]).SizeSquared()
					<= ParallelEpsilon)
				{
					continue;
				}
				const int32 BNext = (BIndex + 1) % 3;
				const int32 BLast = (BIndex + 2) % 3;
				const double ARadius =
					AExtent[ANext] * AbsoluteRotation[ALast][BIndex]
					+ AExtent[ALast] * AbsoluteRotation[ANext][BIndex];
				const double BRadius =
					BExtent[BNext] * AbsoluteRotation[AIndex][BLast]
					+ BExtent[BLast] * AbsoluteRotation[AIndex][BNext];
				const double ProjectedTranslation = FMath::Abs(
					Translation[ALast] * Rotation[ANext][BIndex]
					- Translation[ANext] * Rotation[ALast][BIndex]);
				if (ARadius + BRadius - ProjectedTranslation <= ContactSlopCM)
				{
					return false;
				}
			}
		}
		return true;
	}
}

FABTSM7PenetrationValidationStats FABTSM7PenetrationValidator::ValidateAndRepair(
	UWorld& World,
	const TArray<AABTSM7BuildingModule*>& PendingModules,
	const float RepairToleranceCM,
	const int32 MaximumRepairPasses)
{
	FABTSM7PenetrationValidationStats Stats;
	Stats.PendingModuleCount = PendingModules.Num();
	const float SafeToleranceCM = FMath::Max(0.0f, RepairToleranceCM);
	const int32 SafePassCount = FMath::Clamp(MaximumRepairPasses, 1, 32);
	TSet<const AABTSM7BuildingModule*> PendingSet;
	for (const AABTSM7BuildingModule* Module : PendingModules) if (IsValid(Module)) PendingSet.Add(Module);

	TSet<uint64> DetectedPairs;
	TSet<uint64> LoggedLargePairs;
	for (int32 Pass = 0; Pass < SafePassCount; ++Pass)
	{
		bool bRepairedThisPass = false;
		TSet<uint64> ProcessedThisPass;
		for (AABTSM7BuildingModule* Module : PendingModules)
		{
			if (!IsValid(Module) || Module->IsDynamic()) continue;
			UPrimitiveComponent* Component = Module->GetMeshComponent();
			if (Component == nullptr) continue;
			TArray<FHitResult> Hits;
			QueryInitialPenetrations(World, *Module, Hits);
			for (const FHitResult& Hit : Hits)
			{
				UPrimitiveComponent* OtherComponent = Hit.GetComponent();
				if (!Hit.bBlockingHit || !Hit.bStartPenetrating || Hit.PenetrationDepth <= UE_KINDA_SMALL_NUMBER
					|| OtherComponent == nullptr || OtherComponent == Component || OtherComponent->GetOwner() == Module)
				{
					continue;
				}
				if (!HasPositiveOrientedBoundsOverlap(*Component, *OtherComponent))
				{
					continue;
				}

				const uint64 PairKey = MakeComponentPairKey(*Component, *OtherComponent);
				if (ProcessedThisPass.Contains(PairKey)) continue;
				ProcessedThisPass.Add(PairKey);
				DetectedPairs.Add(PairKey);
				Stats.MaximumDetectedDepthCM = FMath::Max(Stats.MaximumDetectedDepthCM, Hit.PenetrationDepth);

				if (Hit.PenetrationDepth > SafeToleranceCM)
				{
					if (!LoggedLargePairs.Contains(PairKey))
					{
						LoggedLargePairs.Add(PairKey);
						LogPenetrationError(TEXT("DepthExceedsTolerance"), *Module, *OtherComponent, Hit, SafeToleranceCM);
					}
					continue;
				}

				const FVector RepairNormal = Hit.Normal.GetSafeNormal();
				if (RepairNormal.IsNearlyZero())
				{
					if (!LoggedLargePairs.Contains(PairKey))
					{
						LoggedLargePairs.Add(PairKey);
						LogPenetrationError(TEXT("InvalidRepairNormal"), *Module, *OtherComponent, Hit, SafeToleranceCM);
					}
					continue;
				}

				const FVector Correction = RepairNormal * (Hit.PenetrationDepth + RepairPaddingCM);
				AABTSM7BuildingModule* OtherModule = Cast<AABTSM7BuildingModule>(OtherComponent->GetOwner());
				if (OtherModule != nullptr && PendingSet.Contains(OtherModule) && !OtherModule->IsDynamic())
				{
					Module->AddActorWorldOffset(Correction * 0.5f, false, nullptr, ETeleportType::TeleportPhysics);
					OtherModule->AddActorWorldOffset(Correction * -0.5f, false, nullptr, ETeleportType::TeleportPhysics);
				}
				else
				{
					Module->AddActorWorldOffset(Correction, false, nullptr, ETeleportType::TeleportPhysics);
				}
				++Stats.RepairCount;
				bRepairedThisPass = true;
				// The remaining hits were measured before this correction. Re-query
				// this module on the next pass instead of applying stale MTDs.
				break;
			}
		}
		if (!bRepairedThisPass) break;
	}

	TSet<uint64> RemainingPairs;
	for (AABTSM7BuildingModule* Module : PendingModules)
	{
		if (!IsValid(Module) || Module->IsDynamic()) continue;
		UPrimitiveComponent* Component = Module->GetMeshComponent();
		if (Component == nullptr) continue;
		TArray<FHitResult> Hits;
		QueryInitialPenetrations(World, *Module, Hits);
		for (const FHitResult& Hit : Hits)
		{
			UPrimitiveComponent* OtherComponent = Hit.GetComponent();
			if (!Hit.bBlockingHit || !Hit.bStartPenetrating || Hit.PenetrationDepth <= UE_KINDA_SMALL_NUMBER
				|| OtherComponent == nullptr || OtherComponent == Component || OtherComponent->GetOwner() == Module)
			{
				continue;
			}
			if (!HasPositiveOrientedBoundsOverlap(*Component, *OtherComponent))
			{
				continue;
			}
			const uint64 PairKey = MakeComponentPairKey(*Component, *OtherComponent);
			if (RemainingPairs.Contains(PairKey)) continue;
			RemainingPairs.Add(PairKey);
			if (Hit.PenetrationDepth <= SafeToleranceCM)
			{
				++Stats.RemainingSmallPairCount;
				LogPenetrationError(TEXT("RepairPassesExhausted"), *Module, *OtherComponent, Hit, SafeToleranceCM);
			}
			else if (!LoggedLargePairs.Contains(PairKey))
			{
				LoggedLargePairs.Add(PairKey);
				LogPenetrationError(TEXT("DepthExceedsTolerance"), *Module, *OtherComponent, Hit, SafeToleranceCM);
			}
		}
	}

	Stats.DetectedPairCount = DetectedPairs.Num();
	Stats.LargeErrorPairCount = LoggedLargePairs.Num();
	return Stats;
}
