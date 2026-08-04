// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamAPreviewActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamAGenerator.h"
#include "Building/ABTSM73DAG5BShapeGrammarV2.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace ABTSM73BeamAPreview
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

	void AppendQuad(
		FMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D)
	{
		AppendTriangle(Buffers, A, B, C);
		AppendTriangle(Buffers, A, C, D);
		AppendTriangle(Buffers, C, B, A);
		AppendTriangle(Buffers, D, C, A);
	}

	void AppendOrientedBox(
		FMeshBuffers& Buffers,
		const FVector& A,
		const FVector& B,
		const float Thickness)
	{
		const FVector Delta = B - A;
		const FVector Forward = Delta.GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			return;
		}
		const FVector Reference = FMath::Abs(Forward.Z) < 0.95
			? FVector::UpVector
			: FVector::RightVector;
		const FVector Right = FVector::CrossProduct(
			Forward,
			Reference).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(
			Right,
			Forward).GetSafeNormal();
		const double Half = Thickness * 0.5;
		const FVector R = Right * Half;
		const FVector U = Up * Half;
		const FVector P0 = A - R - U;
		const FVector P1 = A + R - U;
		const FVector P2 = A + R + U;
		const FVector P3 = A - R + U;
		const FVector P4 = B - R - U;
		const FVector P5 = B + R - U;
		const FVector P6 = B + R + U;
		const FVector P7 = B - R + U;
		AppendQuad(Buffers, P0, P3, P2, P1);
		AppendQuad(Buffers, P4, P5, P6, P7);
		AppendQuad(Buffers, P0, P4, P7, P3);
		AppendQuad(Buffers, P1, P2, P6, P5);
		AppendQuad(Buffers, P0, P1, P5, P4);
		AppendQuad(Buffers, P3, P7, P6, P2);
	}

	void AppendJointCube(
		FMeshBuffers& Buffers,
		const FVector& Center,
		const float Size)
	{
		AppendOrientedBox(
			Buffers,
			Center - FVector(0.0, 0.0, Size * 0.5),
			Center + FVector(0.0, 0.0, Size * 0.5),
			Size);
	}

	int32 SectionForMember(const FABTSM73BeamAMember& Member)
	{
		switch (Member.Axis)
		{
		case EABTSM73BeamAFrameAxis::X:
			return 0;
		case EABTSM73BeamAFrameAxis::Y:
			return 1;
		case EABTSM73BeamAFrameAxis::Z:
			return 2;
		case EABTSM73BeamAFrameAxis::Diagonal:
		default:
			return 0;
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

AABTSM73BeamAPreviewActor::AABTSM73BeamAPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PreviewMesh = CreateDefaultSubobject<UProceduralMeshComponent>(
		TEXT("BeamStructuralIRPreview"));
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

void AABTSM73BeamAPreviewActor::OnConstruction(
	const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73BeamAPreviewActor::RegeneratePreview()
{
	using namespace ABTSM73BeamAPreview;
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->ClearAllMeshSections();
	PreviewMIDs.Reset();
	LastPreviewSummary = FABTSM73BeamAPreviewSummary();

	FABTSM73DAG5BShapeGrammarV2 SilhouetteGenerator;
	FABTSM73DAG5BV2GenerationResult Silhouette;
	FString Error;
	if (!SilhouetteGenerator.Generate(
		PreviewSettings.Silhouette,
		Silhouette,
		Error))
	{
		LastPreviewSummary.RejectReason = FString::Printf(
			TEXT("BeamASilhouette:%s"),
			*Error);
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M7.3-Beam-A][PreviewRejected]")
			TEXT(" Actor=%s Reason=%s"),
			*GetName(),
			*LastPreviewSummary.RejectReason);
		return;
	}

	FABTSM73BeamAGenerator BeamGenerator;
	FABTSM73BeamAGenerationResult Result;
	if (!BeamGenerator.Generate(
		PreviewSettings,
		Silhouette,
		Result,
		Error))
	{
		LastPreviewSummary = Result.Summary;
		LastPreviewSummary.RejectReason = Error;
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M7.3-Beam-A][PreviewRejected]")
			TEXT(" Actor=%s Reason=%s"),
			*GetName(),
			*Error);
		return;
	}

	TArray<FMeshBuffers> Sections;
	Sections.SetNum(4);
	for (const FABTSM73BeamAMember& Member : Result.Members)
	{
		if (!Result.Joints.IsValidIndex(Member.JointA)
			|| !Result.Joints.IsValidIndex(Member.JointB))
		{
			continue;
		}
		AppendOrientedBox(
			Sections[SectionForMember(Member)],
			Result.Joints[Member.JointA].LocalPosition,
			Result.Joints[Member.JointB].LocalPosition,
			PreviewSettings.BlockCrossSectionCM);
	}
	if (bShowJoints)
	{
		for (const FABTSM73BeamAJoint& Joint : Result.Joints)
		{
			AppendJointCube(
				Sections[3],
				Joint.LocalPosition,
				JointSizeCM);
		}
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
	PreviewMesh->SetCastShadow(false);
	PreviewMesh->SetHiddenInGame(true);

	const TArray<FLinearColor> Colors = {
		XMemberColor,
		YMemberColor,
		ZMemberColor,
		JointColor};
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
		TEXT("[ABTS][M7.3-Beam-A][PreviewGenerated]")
		TEXT(" Actor=%s Volumes=%d Bays=%d Joints=%d Members=%d")
		TEXT(" Assemblies=%d Bearings=%d X=%d Y=%d Z=%d Diagonal=%d")
		TEXT(" BayHash=%lld BeamHash=%lld"),
		*GetName(),
		LastPreviewSummary.SourceVolumeCount,
		LastPreviewSummary.BayCount,
		LastPreviewSummary.JointCount,
		LastPreviewSummary.MemberCount,
		LastPreviewSummary.AssemblyCount,
		LastPreviewSummary.BearingContactCount,
		LastPreviewSummary.XMemberCount,
		LastPreviewSummary.YMemberCount,
		LastPreviewSummary.ZMemberCount,
		LastPreviewSummary.DiagonalMemberCount,
		LastPreviewSummary.BayGraphHash,
		LastPreviewSummary.BeamGraphHash);
}
