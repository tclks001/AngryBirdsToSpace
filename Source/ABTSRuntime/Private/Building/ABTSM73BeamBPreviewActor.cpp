// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamBPreviewActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace ABTSM73BeamBPreview
{
	struct FMeshBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
	};

	void AppendTriangle(
		FMeshBuffers& B,
		const FVector& A,
		const FVector& C,
		const FVector& D)
	{
		const FVector Normal = FVector::CrossProduct(C - A, D - A)
			.GetSafeNormal();
		const int32 Base = B.Vertices.Num();
		B.Vertices.Append({A, C, D});
		B.Triangles.Append({Base, Base + 1, Base + 2});
		B.Normals.Append({Normal, Normal, Normal});
		B.UVs.Append({FVector2D(0.0, 0.0), FVector2D(1.0, 0.0),
			FVector2D(0.5, 1.0)});
		B.Colors.Append({FLinearColor::White, FLinearColor::White,
			FLinearColor::White});
	}

	void AppendQuad(
		FMeshBuffers& B,
		const FVector& A,
		const FVector& C,
		const FVector& D,
		const FVector& E)
	{
		AppendTriangle(B, A, C, D);
		AppendTriangle(B, A, D, E);
		AppendTriangle(B, D, C, A);
		AppendTriangle(B, E, D, A);
	}

	void AppendBox(
		FMeshBuffers& B,
		const FVector& A,
		const FVector& C,
		const double Thickness)
	{
		const FVector Forward = (C - A).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			return;
		}
		const FVector Reference = FMath::Abs(Forward.Z) < 0.95
			? FVector::UpVector : FVector::RightVector;
		const FVector Right = FVector::CrossProduct(Forward, Reference)
			.GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, Forward)
			.GetSafeNormal();
		const FVector R = Right * Thickness * 0.5;
		const FVector U = Up * Thickness * 0.5;
		const FVector P0 = A - R - U;
		const FVector P1 = A + R - U;
		const FVector P2 = A + R + U;
		const FVector P3 = A - R + U;
		const FVector P4 = C - R - U;
		const FVector P5 = C + R - U;
		const FVector P6 = C + R + U;
		const FVector P7 = C - R + U;
		AppendQuad(B, P0, P3, P2, P1);
		AppendQuad(B, P4, P5, P6, P7);
		AppendQuad(B, P0, P4, P7, P3);
		AppendQuad(B, P1, P2, P6, P5);
		AppendQuad(B, P0, P1, P5, P4);
		AppendQuad(B, P3, P7, P6, P2);
	}

	UMaterialInstanceDynamic* MakeMaterial(
		UObject* Owner,
		UMaterialInterface* Parent,
		const FLinearColor& Color)
	{
		if (Parent == nullptr)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID =
			UMaterialInstanceDynamic::Create(Parent, Owner);
		if (MID != nullptr)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
		return MID;
	}
}

AABTSM73BeamBPreviewActor::AABTSM73BeamBPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	PreviewMesh = CreateDefaultSubobject<UProceduralMeshComponent>(
		TEXT("BeamBMotifPreview"));
	PreviewMesh->SetupAttachment(Root);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetCanEverAffectNavigation(false);
	PreviewMesh->SetHiddenInGame(true);
	PreviewMesh->bUseAsyncCooking = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	if (BasicMaterial.Succeeded())
	{
		PreviewMaterial = BasicMaterial.Object;
	}
	MotifColors = {
		FLinearColor(0.95f, 0.55f, 0.12f, 1.0f),
		FLinearColor(0.20f, 0.65f, 0.95f, 1.0f),
		FLinearColor(0.15f, 0.85f, 0.45f, 1.0f),
		FLinearColor(0.82f, 0.30f, 0.90f, 1.0f),
		FLinearColor(0.95f, 0.22f, 0.25f, 1.0f),
		FLinearColor(0.92f, 0.82f, 0.14f, 1.0f),
		FLinearColor(0.25f, 0.95f, 0.90f, 1.0f),
		FLinearColor(0.55f, 0.38f, 0.20f, 1.0f)};
}

void AABTSM73BeamBPreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73BeamBPreviewActor::RegeneratePreview()
{
	using namespace ABTSM73BeamBPreview;
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->ClearAllMeshSections();
	PreviewMIDs.Reset();
	LastPreviewSummary = FABTSM73BeamBPreviewSummary();

	FString Error;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73DAG5BShapeGrammarV2 SilhouetteGenerator;
	if (!SilhouetteGenerator.Generate(
		PreviewSettings.BeamA.Silhouette, Silhouette, Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamBSilhouette:%s"), *Error);
		return;
	}
	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamAGenerator BeamAGenerator;
	if (!BeamAGenerator.Generate(
		PreviewSettings.BeamA, Silhouette, BeamA, Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamBUpstream:%s"), *Error);
		return;
	}
	FABTSM73BeamBGenerationResult Result;
	FABTSM73BeamBGenerator Generator;
	if (!Generator.Generate(
		PreviewSettings, Silhouette, BeamA, Result, Error))
	{
		LastPreviewSummary = Result.Summary;
		LastPreviewSummary.RejectReason = Error;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-B][PreviewRejected] Actor=%s Reason=%s"),
			*GetName(), *Error);
		return;
	}

	TArray<FMeshBuffers> Sections;
	Sections.SetNum(8);
	for (const FABTSM73BeamBPlannedMember& Member : Result.PlannedMembers)
	{
		const int32 Section = FMath::Clamp(
			static_cast<int32>(Member.Motif), 0, Sections.Num() - 1);
		AppendBox(Sections[Section], Member.LocalStart, Member.LocalEnd,
			PreviewSettings.BeamA.BlockCrossSectionCM);
	}
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		FMeshBuffers& Buffers = Sections[Index];
		if (!Buffers.Vertices.IsEmpty())
		{
			PreviewMesh->CreateMeshSection_LinearColor(Index,
				Buffers.Vertices, Buffers.Triangles, Buffers.Normals,
				Buffers.UVs, Buffers.Colors, Buffers.Tangents, false);
		}
		const FLinearColor Color = MotifColors.IsValidIndex(Index)
			? MotifColors[Index] : FLinearColor::White;
		UMaterialInstanceDynamic* MID = MakeMaterial(
			this, PreviewMaterial, Color);
		PreviewMIDs.Add(MID);
		if (MID != nullptr)
		{
			PreviewMesh->SetMaterial(Index, MID);
		}
	}
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->SetHiddenInGame(true);
	LastPreviewSummary = Result.Summary;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-B][PreviewGenerated]")
		TEXT(" Actor=%s Bays=%d Motifs=%d WFC=%d Steps=%d Members=%d")
		TEXT(" PortViolations=%d BoundsViolations=%d Hash=%lld"),
		*GetName(), LastPreviewSummary.BayCount,
		LastPreviewSummary.DistinctMotifCount,
		LastPreviewSummary.WFCPropagationOperationCount,
		LastPreviewSummary.GrammarStepCount,
		LastPreviewSummary.PlannedMemberCount,
		LastPreviewSummary.PortViolationCount,
		LastPreviewSummary.OutOfBoundsMemberCount,
		LastPreviewSummary.ResultHash);
}
