// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG5BShapePreviewActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace ABTSM73DAG5BV2Preview
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

	void AppendTriangleOneSided(
		FMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const FVector& C)
	{
		const FVector Normal =
			FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const int32 BaseIndex = Buffers.Vertices.Num();
		Buffers.Vertices.Append({A, B, C});
		Buffers.Triangles.Append({
			BaseIndex,
			BaseIndex + 1,
			BaseIndex + 2});
		Buffers.Normals.Append({Normal, Normal, Normal});
		Buffers.UVs.Append({
			FVector2D(0.0, 0.0),
			FVector2D(1.0, 0.0),
			FVector2D(0.5, 1.0)});
		Buffers.Colors.Append({
			FLinearColor::White,
			FLinearColor::White,
			FLinearColor::White});
	}

	void AppendTriangle(
		FMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const FVector& C)
	{
		AppendTriangleOneSided(Buffers, A, B, C);
		AppendTriangleOneSided(Buffers, C, B, A);
	}

	void AppendQuad(
		FMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D)
	{
		AppendTriangle(Buffers, A, B, C);
		AppendTriangle(Buffers, A, C, D);
	}

	void AppendBox(
		const FBox& Box,
		FMeshBuffers& Buffers)
	{
		const FVector P000(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector P100(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector P110(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector P010(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector P001(Box.Min.X, Box.Min.Y, Box.Max.Z);
		const FVector P101(Box.Max.X, Box.Min.Y, Box.Max.Z);
		const FVector P111(Box.Max.X, Box.Max.Y, Box.Max.Z);
		const FVector P011(Box.Min.X, Box.Max.Y, Box.Max.Z);
		AppendQuad(Buffers, P000, P010, P110, P100);
		AppendQuad(Buffers, P001, P101, P111, P011);
		AppendQuad(Buffers, P000, P001, P011, P010);
		AppendQuad(Buffers, P100, P110, P111, P101);
		AppendQuad(Buffers, P000, P100, P101, P001);
		AppendQuad(Buffers, P010, P011, P111, P110);
	}

	void AppendPrismX(
		const FBox& Box,
		FMeshBuffers& Buffers)
	{
		const float MidX = (Box.Min.X + Box.Max.X) * 0.5f;
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector C(MidX, Box.Min.Y, Box.Max.Z);
		const FVector D(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector E(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector F(MidX, Box.Max.Y, Box.Max.Z);
		AppendTriangle(Buffers, A, C, B);
		AppendTriangle(Buffers, D, E, F);
		AppendQuad(Buffers, A, B, E, D);
		AppendQuad(Buffers, A, D, F, C);
		AppendQuad(Buffers, B, C, F, E);
	}

	void AppendPrismY(
		const FBox& Box,
		FMeshBuffers& Buffers)
	{
		const float MidY = (Box.Min.Y + Box.Max.Y) * 0.5f;
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector C(Box.Min.X, MidY, Box.Max.Z);
		const FVector D(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector E(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector F(Box.Max.X, MidY, Box.Max.Z);
		AppendTriangle(Buffers, A, B, C);
		AppendTriangle(Buffers, D, F, E);
		AppendQuad(Buffers, A, D, E, B);
		AppendQuad(Buffers, A, C, F, D);
		AppendQuad(Buffers, B, E, F, C);
	}

	void AppendPyramid(
		const FBox& Box,
		FMeshBuffers& Buffers)
	{
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector C(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector D(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector Apex(
			(Box.Min.X + Box.Max.X) * 0.5f,
			(Box.Min.Y + Box.Max.Y) * 0.5f,
			Box.Max.Z);
		AppendQuad(Buffers, A, D, C, B);
		AppendTriangle(Buffers, A, B, Apex);
		AppendTriangle(Buffers, B, C, Apex);
		AppendTriangle(Buffers, C, D, Apex);
		AppendTriangle(Buffers, D, A, Apex);
	}

	int32 SectionForPrimitive(
		const EABTSM73DAG5BV2Primitive Primitive)
	{
		switch (Primitive)
		{
		case EABTSM73DAG5BV2Primitive::TriangularPrismX:
		case EABTSM73DAG5BV2Primitive::TriangularPrismY:
			return 1;
		case EABTSM73DAG5BV2Primitive::Pyramid:
			return 2;
		case EABTSM73DAG5BV2Primitive::Box:
		default:
			return 0;
		}
	}

	void AppendVolume(
		const FABTSM73DAG5BV2Volume& Volume,
		FMeshBuffers& Buffers)
	{
		switch (Volume.Primitive)
		{
		case EABTSM73DAG5BV2Primitive::TriangularPrismX:
			AppendPrismX(Volume.LocalBounds, Buffers);
			break;
		case EABTSM73DAG5BV2Primitive::TriangularPrismY:
			AppendPrismY(Volume.LocalBounds, Buffers);
			break;
		case EABTSM73DAG5BV2Primitive::Pyramid:
			AppendPyramid(Volume.LocalBounds, Buffers);
			break;
		case EABTSM73DAG5BV2Primitive::Box:
		default:
			AppendBox(Volume.LocalBounds, Buffers);
			break;
		}
	}

	UMaterialInstanceDynamic* MakePreviewMaterial(
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

AABTSM73DAG5BShapePreviewActor::AABTSM73DAG5BShapePreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PreviewMesh = CreateDefaultSubobject<UProceduralMeshComponent>(
		TEXT("ComplexSilhouettePreview"));
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
}

void AABTSM73DAG5BShapePreviewActor::OnConstruction(
	const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73DAG5BShapePreviewActor::RegeneratePreview()
{
	using namespace ABTSM73DAG5BV2Preview;
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->ClearAllMeshSections();
	PreviewMIDs.Reset();
	LastPreviewSummary = FABTSM73DAG5BV2PreviewSummary();

	FABTSM73DAG5BShapeGrammarV2 Generator;
	FABTSM73DAG5BV2GenerationResult Result;
	FString Error;
	if (!Generator.Generate(PreviewSettings, Result, Error))
	{
		LastPreviewSummary = Result.Summary;
		LastPreviewSummary.RejectReason = Error;
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M7.3-DAG5Bv2][PreviewRejected] Actor=%s Reason=%s"),
			*GetName(),
			*Error);
		return;
	}

	TArray<FMeshBuffers> Sections;
	Sections.SetNum(3);
	for (const FABTSM73DAG5BV2Volume& Volume : Result.Volumes)
	{
		AppendVolume(
			Volume,
			Sections[SectionForPrimitive(Volume.Primitive)]);
	}
	for (int32 SectionIndex = 0;
		SectionIndex < Sections.Num();
		++SectionIndex)
	{
		FMeshBuffers& Buffers = Sections[SectionIndex];
		if (Buffers.Vertices.IsEmpty())
		{
			continue;
		}
		PreviewMesh->CreateMeshSection_LinearColor(
			SectionIndex,
			Buffers.Vertices,
			Buffers.Triangles,
			Buffers.Normals,
			Buffers.UVs,
			Buffers.Colors,
			Buffers.Tangents,
			false);
	}
	PreviewMesh->SetCastShadow(bCastPreviewShadow);
	PreviewMesh->SetHiddenInGame(true);

	const TArray<FLinearColor> Colors = {
		BoxColor,
		PrismColor,
		PyramidColor};
	for (int32 SectionIndex = 0;
		SectionIndex < Colors.Num();
		++SectionIndex)
	{
		UMaterialInstanceDynamic* MID = MakePreviewMaterial(
			this,
			PreviewMaterial,
			Colors[SectionIndex]);
		PreviewMIDs.Add(MID);
		if (MID != nullptr)
		{
			PreviewMesh->SetMaterial(SectionIndex, MID);
		}
	}

	LastPreviewSummary = Result.Summary;
	UE_LOG(
		LogABTSRuntime,
		Display,
		TEXT("[ABTS][M7.3-DAG5Bv2][PreviewGenerated] Actor=%s Archetype=%d")
		TEXT(" GrammarSteps=%d Volumes=%d Box=%d Prism=%d Pyramid=%d Span=%d")
		TEXT(" Propagation=%d Backtracks=%d GrammarHash=%lld")
		TEXT(" WFCHash=%lld ResultHash=%lld"),
		*GetName(),
		static_cast<int32>(LastPreviewSummary.ResolvedArchetype),
		LastPreviewSummary.GrammarStepCount,
		LastPreviewSummary.VolumeCount,
		LastPreviewSummary.BoxCount,
		LastPreviewSummary.PrismCount,
		LastPreviewSummary.PyramidCount,
		LastPreviewSummary.SupportedSpanCount,
		LastPreviewSummary.WFCPropagationOperationCount,
		LastPreviewSummary.WFCBacktrackStepCount,
		LastPreviewSummary.GrammarHash,
		LastPreviewSummary.WFCHash,
		LastPreviewSummary.ResultHash);
}
