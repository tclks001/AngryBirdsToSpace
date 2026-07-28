// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleActors.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void ConfigureVisualOnlyMesh(UStaticMeshComponent& Mesh)
	{
		Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh.SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh.SetGenerateOverlapEvents(false);
		Mesh.SetSimulatePhysics(false);
		Mesh.SetCanEverAffectNavigation(false);
	}

	void EnforceVisualOnlyActor(AActor& Actor)
	{
		Actor.SetActorTickEnabled(false);
		Actor.PrimaryActorTick.bCanEverTick = false;
		Actor.SetActorEnableCollision(false);
		Actor.SetCanBeDamaged(false);

		TInlineComponentArray<UActorComponent*> Components;
		Actor.GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component == nullptr)
			{
				continue;
			}

			Component->SetComponentTickEnabled(false);
			Component->PrimaryComponentTick.bCanEverTick = false;
			if (UPrimitiveComponent* Primitive =
				Cast<UPrimitiveComponent>(Component))
			{
				Primitive->SetCollisionEnabled(
					ECollisionEnabled::NoCollision);
				Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
				Primitive->SetGenerateOverlapEvents(false);
				Primitive->SetNotifyRigidBodyCollision(false);
				Primitive->SetSimulatePhysics(false);
				Primitive->SetEnableGravity(false);
				Primitive->SetCanEverAffectNavigation(false);
			}
		}
	}

	bool IsValidPresentationRadius(const double RadiusCM)
	{
		return FMath::IsFinite(RadiusCM) && RadiusCM > 0.0;
	}

	FQuat BuildTargetWorldRotation(
		const FABTSM11TargetSpec& TargetSpec,
		const FABTSM110FinaleLocalFrame& FinaleFrame)
	{
		const FVector LocalForward =
			FVector(TargetSpec.PresentationForward).GetSafeNormal();
		const FVector WorldForward = FinaleFrame.WorldTransform
			.TransformVectorNoScale(LocalForward).GetSafeNormal();

		FVector WorldUp = FVector::VectorPlaneProject(
			FinaleFrame.GetUp(), WorldForward).GetSafeNormal();
		if (WorldUp.IsNearlyZero())
		{
			WorldUp = FVector::VectorPlaneProject(
				FinaleFrame.GetRight(), WorldForward).GetSafeNormal();
		}
		if (WorldUp.IsNearlyZero())
		{
			WorldUp = FVector::VectorPlaneProject(
				FinaleFrame.GetForward(), WorldForward).GetSafeNormal();
		}
		return FRotationMatrix::MakeFromXZ(WorldForward, WorldUp).ToQuat();
	}
}

AABTSM11GravityBodyActor::AABTSM11GravityBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SceneRoot);
	ConfigureVisualOnlyMesh(*VisualMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMesh.Object);
	}
	EnforceVisualOnlyActor(*this);
}

bool AABTSM11GravityBodyActor::ConfigurePresentation(
	const FABTSM11GravityBodySpec& BodySpec,
	const FABTSM110FinaleLocalFrame& FinaleFrame,
	UStaticMesh* InMesh,
	const double InMeshReferenceRadiusCM)
{
	if (!BodySpec.IsValid()
		|| !BodySpec.IsAssist()
		|| !FinaleFrame.IsUsable()
		|| !IsValidPresentationRadius(InMeshReferenceRadiusCM))
	{
		return false;
	}

	const FVector WorldLocation = FinaleFrame.TransformLocalPosition(
		FVector(BodySpec.CenterCM));
	const FQuat WorldRotation =
		FinaleFrame.WorldTransform.GetRotation().GetNormalized();
	const double UniformVisualScale =
		BodySpec.VisualRadiusCM / InMeshReferenceRadiusCM;
	if (!FMath::IsFinite(UniformVisualScale) || UniformVisualScale <= 0.0)
	{
		return false;
	}

	if (InMesh != nullptr)
	{
		VisualMesh->SetStaticMesh(InMesh);
	}
	VisualMesh->SetRelativeScale3D(FVector(UniformVisualScale));
	SetActorLocationAndRotation(
		WorldLocation,
		WorldRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	EnforceVisualOnlyActor(*this);

	StableBodyId = BodySpec.BodyId;
	GravityRole = BodySpec.Role;
	bPresentationConfigured = true;
	return true;
}

AABTSM11UFOActor::AABTSM11UFOActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SceneRoot);
	ConfigureVisualOnlyMesh(*VisualMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMesh.Object);
	}
	EnforceVisualOnlyActor(*this);
}

bool AABTSM11UFOActor::ConfigurePresentation(
	const FABTSM11TargetSpec& TargetSpec,
	const FABTSM110FinaleLocalFrame& FinaleFrame,
	UStaticMesh* InMesh,
	const double InMeshReferenceRadiusCM,
	const double InVisualRadiusCM)
{
	const double ResolvedVisualRadiusCM =
		InVisualRadiusCM == 0.0
		? TargetSpec.GetGeometricContactRadiusCM()
		: InVisualRadiusCM;
	if (!TargetSpec.IsValid()
		|| !FinaleFrame.IsUsable()
		|| !IsValidPresentationRadius(InMeshReferenceRadiusCM)
		|| !IsValidPresentationRadius(ResolvedVisualRadiusCM))
	{
		return false;
	}

	const FVector WorldLocation = FinaleFrame.TransformLocalPosition(
		FVector(TargetSpec.GetGeometricContactCenterCM()));
	const FQuat WorldRotation =
		BuildTargetWorldRotation(TargetSpec, FinaleFrame);
	const double UniformVisualScale =
		ResolvedVisualRadiusCM / InMeshReferenceRadiusCM;
	if (!FMath::IsFinite(UniformVisualScale) || UniformVisualScale <= 0.0)
	{
		return false;
	}

	if (InMesh != nullptr)
	{
		VisualMesh->SetStaticMesh(InMesh);
	}
	VisualMesh->SetRelativeScale3D(FVector(UniformVisualScale));
	SetActorLocationAndRotation(
		WorldLocation,
		WorldRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	EnforceVisualOnlyActor(*this);

	StableTargetId = TargetSpec.TargetId;
	bPresentationConfigured = true;
	return true;
}
