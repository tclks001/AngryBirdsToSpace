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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded()) VisualMesh->SetStaticMesh(SphereMesh.Object);
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
	QuerySphere->SetSphereRadius(TargetRadiusCM, true);
	// Engine basic sphere has a 50 cm radius.
	VisualMesh->SetRelativeScale3D(
		FVector(FMath::Max(TargetRadiusCM / 50.0f, 0.01f)));
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
