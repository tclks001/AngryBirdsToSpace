// Copyright Epic Games, Inc. All Rights Reserved.

#include "Calibration/ABTSCalibrationTargetProxy.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AABTSCalibrationTargetProxy::AABTSCalibrationTargetProxy()
{
	PrimaryActorTick.bCanEverTick = false;
	QuerySphere = CreateDefaultSubobject<USphereComponent>(TEXT("QuerySphere"));
	SetRootComponent(QuerySphere);
	QuerySphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	QuerySphere->SetCollisionObjectType(ECC_WorldDynamic);
	QuerySphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	QuerySphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	QuerySphere->SetGenerateOverlapEvents(true);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(QuerySphere);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetGenerateOverlapEvents(false);
	VisualMesh->SetCastShadow(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(QuerySphere);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(80.0f);
	Label->SetTextRenderColor(FColor::White);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SphereStaticMesh = SphereMeshFinder.Succeeded()
		? SphereMeshFinder.Object
		: nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeStaticMesh = CubeMeshFinder.Succeeded()
		? CubeMeshFinder.Object
		: nullptr;
	if (SphereStaticMesh) VisualMesh->SetStaticMesh(SphereStaticMesh);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ShapeMaterial.Succeeded()
		? ShapeMaterial.Object
		: UMaterial::GetDefaultMaterial(MD_Surface);
}

void AABTSCalibrationTargetProxy::Configure(
	const FName InTargetId,
	const float InRadiusCM,
	const FLinearColor& InColor)
{
	TargetId = InTargetId;
	TargetRadiusCM = FMath::Max(10.0f, InRadiusCM);
	TargetColor = InColor;
	bCubeTarget = false;
	RefreshPresentation();
}

void AABTSCalibrationTargetProxy::ConfigureCube(
	const FName InTargetId,
	const float InHalfExtentCM,
	const FLinearColor& InColor)
{
	TargetId = InTargetId;
	TargetRadiusCM = FMath::Max(10.0f, InHalfExtentCM);
	TargetColor = InColor;
	bCubeTarget = true;
	RefreshPresentation();
}

void AABTSCalibrationTargetProxy::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void AABTSCalibrationTargetProxy::MarkHit()
{
	if (bWasHit) return;
	bWasHit = true;
	TargetColor = FLinearColor(0.15f, 1.0f, 0.25f, 1.0f);
	RefreshPresentation();
}

void AABTSCalibrationTargetProxy::RefreshPresentation()
{
	if (QuerySphere == nullptr || VisualMesh == nullptr || Label == nullptr) return;
	if (bCubeTarget)
	{
		QuerySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		QuerySphere->SetGenerateOverlapEvents(false);
		VisualMesh->SetStaticMesh(CubeStaticMesh);
		// Engine basic cube has a 100 cm side length.
		VisualMesh->SetRelativeScale3D(
			FVector(FMath::Max(TargetRadiusCM * 2.0f / 100.0f, 0.01f)));
		VisualMesh->SetCollisionObjectType(ECC_WorldDynamic);
		VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		VisualMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		QuerySphere->SetSphereRadius(TargetRadiusCM, true);
		QuerySphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		QuerySphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		QuerySphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		QuerySphere->SetGenerateOverlapEvents(true);
		VisualMesh->SetStaticMesh(SphereStaticMesh);
		// Engine basic sphere has a 50 cm radius.
		VisualMesh->SetRelativeScale3D(
			FVector(FMath::Max(TargetRadiusCM / 50.0f, 0.01f)));
		VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VisualMesh->SetGenerateOverlapEvents(false);
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(
		BaseMaterial
			? BaseMaterial.Get()
			: UMaterial::GetDefaultMaterial(MD_Surface),
		this);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), TargetColor);
		MID->SetVectorParameterValue(TEXT("BaseColor"), TargetColor);
		VisualMesh->SetMaterial(0, MID);
	}
	Label->SetText(FText::FromName(TargetId));
	Label->SetRelativeLocation(
		FVector(0.0f, 0.0f, TargetRadiusCM + 70.0f));
	Label->SetTextRenderColor(TargetColor.ToFColor(true));
}
