// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamCPreviewActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73BeamBGenerator.h"
#include "Building/ABTSM73BeamCGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace ABTSM73BeamCPreview
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

	void AppendTriangle(FMeshBuffers& B, const FVector& A,
		const FVector& C, const FVector& D)
	{
		const FVector Normal = FVector::CrossProduct(C - A, D - A).GetSafeNormal();
		const int32 Base = B.Vertices.Num();
		B.Vertices.Append({A, C, D});
		B.Triangles.Append({Base, Base + 1, Base + 2});
		B.Normals.Append({Normal, Normal, Normal});
		B.UVs.Append({FVector2D(0.0, 0.0), FVector2D(1.0, 0.0),
			FVector2D(0.5, 1.0)});
		B.Colors.Append({FLinearColor::White, FLinearColor::White,
			FLinearColor::White});
	}

	void AppendQuad(FMeshBuffers& B, const FVector& A, const FVector& C,
		const FVector& D, const FVector& E)
	{
		AppendTriangle(B, A, C, D);
		AppendTriangle(B, A, D, E);
		AppendTriangle(B, D, C, A);
		AppendTriangle(B, E, D, A);
	}

	void AppendBox(FMeshBuffers& B, const FVector& A, const FVector& C,
		const double Thickness)
	{
		const FVector Forward = (C - A).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			return;
		}
		const FVector Reference = FMath::Abs(Forward.Z) < 0.95
			? FVector::UpVector : FVector::RightVector;
		const FVector Right = FVector::CrossProduct(Forward, Reference).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Right, Forward).GetSafeNormal();
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

	UMaterialInstanceDynamic* MakeMaterial(UObject* Owner,
		UMaterialInterface* Parent, const FLinearColor& Color)
	{
		if (Parent == nullptr)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Owner);
		if (MID != nullptr)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
		return MID;
	}

	double SafeRatio(const double Value, const double Maximum)
	{
		return Maximum > UE_DOUBLE_SMALL_NUMBER ? Value / Maximum : 0.0;
	}
}

AABTSM73BeamCPreviewActor::AABTSM73BeamCPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	PreviewMesh = CreateDefaultSubobject<UProceduralMeshComponent>(
		TEXT("BeamCLoadDAGPreview"));
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
	UtilizationColors = {
		FLinearColor(0.18f, 0.18f, 0.22f, 1.0f),
		FLinearColor(0.12f, 0.45f, 0.95f, 1.0f),
		FLinearColor(0.10f, 0.85f, 0.90f, 1.0f),
		FLinearColor(0.85f, 0.85f, 0.12f, 1.0f),
		FLinearColor(0.95f, 0.48f, 0.08f, 1.0f),
		FLinearColor(0.95f, 0.08f, 0.10f, 1.0f),
		FLinearColor(0.85f, 0.20f, 0.95f, 1.0f)};
}

void AABTSM73BeamCPreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73BeamCPreviewActor::RegeneratePreview()
{
	using namespace ABTSM73BeamCPreview;
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->ClearAllMeshSections();
	PreviewMIDs.Reset();
	LastPreviewSummary = FABTSM73BeamCPreviewSummary();

	FString Error;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FABTSM73DAG5BShapeGrammarV2 ShapeGenerator;
	if (!ShapeGenerator.Generate(
		PreviewSettings.BeamB.BeamA.Silhouette, Silhouette, Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamCSilhouette:%s"), *Error);
		return;
	}
	FABTSM73BeamAGenerationResult BeamA;
	FABTSM73BeamAGenerator BeamAGenerator;
	if (!BeamAGenerator.Generate(
		PreviewSettings.BeamB.BeamA, Silhouette, BeamA, Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamCBeamA:%s"), *Error);
		return;
	}
	FABTSM73BeamBGenerationResult BeamB;
	FABTSM73BeamBGenerator BeamBGenerator;
	if (!BeamBGenerator.Generate(
		PreviewSettings.BeamB, Silhouette, BeamA, BeamB, Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamCBeamB:%s"), *Error);
		return;
	}
	FABTSM73BeamCGenerationResult BeamC;
	FABTSM73BeamCGenerator BeamCGenerator;
	if (!BeamCGenerator.Generate(
		PreviewSettings, BeamB.ClosedAssembly, BeamC, Error))
	{
		LastPreviewSummary = BeamC.Summary;
		LastPreviewSummary.RejectReason = Error;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-C][PreviewRejected] Actor=%s Reason=%s"),
			*GetName(), *Error);
		return;
	}

	TArray<FMeshBuffers> Sections;
	Sections.SetNum(7);
	for (const FABTSM73BeamAMember& Member : BeamB.ClosedAssembly.Members)
	{
		if (!BeamC.Nodes.IsValidIndex(Member.MemberId)
			|| !BeamB.ClosedAssembly.Joints.IsValidIndex(Member.JointA)
			|| !BeamB.ClosedAssembly.Joints.IsValidIndex(Member.JointB))
		{
			continue;
		}
		const FABTSM73BeamCLoadNode& Node = BeamC.Nodes[Member.MemberId];
		int32 Section = 0;
		if (!Node.bGround)
		{
			const double Utilization = FMath::Max3(
				SafeRatio(Node.SpanUtilization,
					PreviewSettings.MaximumSpanUtilization),
				SafeRatio(Node.CantileverRatio,
					PreviewSettings.MaximumCantileverRatio),
				SafeRatio(Node.ColumnSlenderness,
					PreviewSettings.MaximumColumnSlenderness));
			Section = Utilization < 0.25 ? 1
				: Utilization < 0.50 ? 2
				: Utilization < 0.75 ? 3
				: Utilization < 1.0 ? 4 : 5;
		}
		AppendBox(Sections[Section],
			BeamB.ClosedAssembly.Joints[Member.JointA].LocalPosition,
			BeamB.ClosedAssembly.Joints[Member.JointB].LocalPosition,
			PreviewSettings.BeamB.BeamA.BlockCrossSectionCM);
	}
	if (bShowLoadPaths)
	{
		for (const FABTSM73BeamCLoadEdge& Edge : BeamC.Edges)
		{
			if (Edge.ReactionLoadKG <= 0.0f
				|| !BeamC.Nodes.IsValidIndex(Edge.UpperMemberId))
			{
				continue;
			}
			AppendBox(Sections[6],
				BeamC.Nodes[Edge.UpperMemberId].LoadResultant,
				Edge.ContactPosition,
				FMath::Max(2.0f,
					PreviewSettings.BeamB.BeamA.BlockCrossSectionCM * 0.10f));
		}
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
		const FLinearColor Color = UtilizationColors.IsValidIndex(Index)
			? UtilizationColors[Index] : FLinearColor::White;
		UMaterialInstanceDynamic* MID = MakeMaterial(this, PreviewMaterial, Color);
		PreviewMIDs.Add(MID);
		if (MID != nullptr)
		{
			PreviewMesh->SetMaterial(Index, MID);
		}
	}
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->SetHiddenInGame(true);
	LastPreviewSummary = BeamC.Summary;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-C][PreviewGenerated]")
		TEXT(" Actor=%s Nodes=%d Edges=%d Ground=%d")
		TEXT(" SelfLoad=%.2f GroundReaction=%.2f")
		TEXT(" MaxSpanUtil=%.3f MaxSlenderness=%.3f Hash=%lld"),
		*GetName(), LastPreviewSummary.LoadNodeCount,
		LastPreviewSummary.LoadEdgeCount,
		LastPreviewSummary.GroundNodeCount,
		LastPreviewSummary.TotalSelfLoadKG,
		LastPreviewSummary.TotalGroundReactionKG,
		LastPreviewSummary.MaximumObservedSpanUtilization,
		LastPreviewSummary.MaximumObservedColumnSlenderness,
		LastPreviewSummary.LoadDAGHash);
}
