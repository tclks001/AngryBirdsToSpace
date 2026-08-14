// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamD1PreviewActor.h"

#include "ABTSRuntime.h"
#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace ABTSM73BeamD1Preview
{
	constexpr int32 MaximumRuntimeSystemSearchAttempts = 40;
	constexpr float RuntimeSystemSearchIntervalSeconds = 0.1f;

	void ConfigurePreview(
		UHierarchicalInstancedStaticMeshComponent& Component,
		USceneComponent& Parent)
	{
		Component.SetupAttachment(&Parent);
		Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component.SetGenerateOverlapEvents(false);
		Component.SetCanEverAffectNavigation(false);
		Component.SetHiddenInGame(true);
		Component.SetCastShadow(true);
	}

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
		FMeshBuffers& Buffers, const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		const int32 BaseIndex = Buffers.Vertices.Num();
		Buffers.Vertices.Append({A, B, C});
		Buffers.Triangles.Append({BaseIndex, BaseIndex + 1, BaseIndex + 2});
		Buffers.Normals.Append({Normal, Normal, Normal});
		Buffers.UVs.Append({FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.5, 1.0)});
		Buffers.Colors.Append({FLinearColor::White, FLinearColor::White, FLinearColor::White});
	}

	void AppendQuad(
		FMeshBuffers& Buffers, const FVector& A, const FVector& B,
		const FVector& C, const FVector& D)
	{
		AppendTriangle(Buffers, A, B, C);
		AppendTriangle(Buffers, A, C, D);
	}

	void AppendBox(const FBox& Box, FMeshBuffers& Buffers)
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

	void AppendPrismX(const FBox& Box, FMeshBuffers& Buffers)
	{
		const double MidX = (Box.Min.X + Box.Max.X) * 0.5;
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector C(MidX, Box.Min.Y, Box.Max.Z);
		const FVector D(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector E(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector F(MidX, Box.Max.Y, Box.Max.Z);
		AppendTriangle(Buffers, A, B, C);
		AppendTriangle(Buffers, D, F, E);
		AppendQuad(Buffers, A, D, E, B);
		AppendQuad(Buffers, A, C, F, D);
		AppendQuad(Buffers, B, E, F, C);
	}

	void AppendPrismY(const FBox& Box, FMeshBuffers& Buffers)
	{
		const double MidY = (Box.Min.Y + Box.Max.Y) * 0.5;
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector C(Box.Min.X, MidY, Box.Max.Z);
		const FVector D(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector E(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector F(Box.Max.X, MidY, Box.Max.Z);
		AppendTriangle(Buffers, A, C, B);
		AppendTriangle(Buffers, D, E, F);
		AppendQuad(Buffers, A, B, E, D);
		AppendQuad(Buffers, A, D, F, C);
		AppendQuad(Buffers, B, C, F, E);
	}

	void AppendPyramid(const FBox& Box, FMeshBuffers& Buffers)
	{
		const FVector A(Box.Min.X, Box.Min.Y, Box.Min.Z);
		const FVector B(Box.Max.X, Box.Min.Y, Box.Min.Z);
		const FVector C(Box.Max.X, Box.Max.Y, Box.Min.Z);
		const FVector D(Box.Min.X, Box.Max.Y, Box.Min.Z);
		const FVector Apex(
			(Box.Min.X + Box.Max.X) * 0.5,
			(Box.Min.Y + Box.Max.Y) * 0.5, Box.Max.Z);
		AppendQuad(Buffers, A, D, C, B);
		AppendTriangle(Buffers, A, B, Apex);
		AppendTriangle(Buffers, B, C, Apex);
		AppendTriangle(Buffers, C, D, Apex);
		AppendTriangle(Buffers, D, A, Apex);
	}

	void AppendVolume(const FABTSM73DAG5BV2Volume& Volume, FMeshBuffers& Buffers)
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
		default:
			AppendBox(Volume.LocalBounds, Buffers);
			break;
		}
	}

	int32 SectionForVolume(const FABTSM73DAG5BV2Volume& Volume)
	{
		if (Volume.Role == EABTSM73DAG5BV2VolumeRole::SupportedSpan)
		{
			return 3;
		}
		switch (Volume.Primitive)
		{
		case EABTSM73DAG5BV2Primitive::TriangularPrismX:
		case EABTSM73DAG5BV2Primitive::TriangularPrismY: return 1;
		case EABTSM73DAG5BV2Primitive::Pyramid: return 2;
		default: return 0;
		}
	}

	enum EDiagnosticVisibility : uint32
	{
		SemanticEnvelopeVisibility = 1 << 0,
		ProtectedVoidVisibility = 1 << 1,
		CoreIntentVisibility = 1 << 2,
		PairIntentVisibility = 1 << 3,
		CoreAndSharedVisibility = 1 << 4,
		CoreMergeRegionVisibility = 1 << 5,
		CompositeCoreXVisibility = 1 << 6,
		CompositeCoreYVisibility = 1 << 7,
		SemanticSupportDemandVisibility = 1 << 8,
		SupportProvinceVisibility = 1 << 9,
		SupportProvinceMainVisibility = 1 << 10,
		DemandCoreCouplingVisibility = 1 << 11,
		LocalPodiumHeightPlanVisibility = 1 << 12,
		Stage2CouplingOnlyVisibility = 1 << 13,
		Stage2ProvenanceVisibility = 1 << 14,
		Stage2CoreAndCouplingVisibility = 1 << 15,
		Stage2PerimeterCoreFacesVisibility = 1 << 16,
		Stage2FacadePartitionsVisibility = 1 << 17,
		Stage3ExteriorFramesVisibility = 1 << 18,
		Stage3ExteriorColumnsVisibility = 1 << 19,
		Stage3GroundSillVisibility = 1 << 20,
		Stage3GroundExteriorColumnsVisibility = 1 << 21,
		Stage3OverviewVisibility = 1 << 22,
		Stage4TopSurfaceIntentVisibility = 1 << 23,
		Stage4FloorTopFramesVisibility = 1 << 24,
		Stage4FacadeToTopVisibility = 1 << 25,
		Stage4FloorStyleInfillVisibility = 1 << 26,
		Stage4RoofCrownVisibility = 1 << 27,
		Stage4OverviewVisibility = 1 << 28
	};

	enum class EStage3OverviewBucket : uint8
	{
		None,
		Stage1CoreAndShared,
		Stage2Coupling,
		Stage3Exterior
	};

	EStage3OverviewBucket Stage3OverviewBucket(
		const EABTSM73BeamC3GenerationStage ProducedStage,
		const bool bReusedAsStage3Exterior)
	{
		if (bReusedAsStage3Exterior
			|| ProducedStage == EABTSM73BeamC3GenerationStage::CommonExteriorFrame)
		{
			return EStage3OverviewBucket::Stage3Exterior;
		}
		if (ProducedStage == EABTSM73BeamC3GenerationStage::CouplingCourses)
		{
			return EStage3OverviewBucket::Stage2Coupling;
		}
		if (ProducedStage == EABTSM73BeamC3GenerationStage::CoreAndShared)
		{
			return EStage3OverviewBucket::Stage1CoreAndShared;
		}
		return EStage3OverviewBucket::None;
	}

	uint32 DiagnosticVisibilityMask(
		const EABTSM73BeamC3Stage1DiagnosticLayer Layer)
	{
		switch (Layer)
		{
		case EABTSM73BeamC3Stage1DiagnosticLayer::WFCSemanticEnvelope:
			return SemanticEnvelopeVisibility | ProtectedVoidVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::CorePlacementIntent:
			return CoreIntentVisibility | PairIntentVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses:
			return CoreAndSharedVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::CoreMergeRegions:
			return CoreMergeRegionVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes:
			return CompositeCoreXVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes:
			return CompositeCoreYVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::SemanticSupportDemandDAG:
			return SemanticSupportDemandVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition:
			return SupportProvinceVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment:
			return SupportProvinceMainVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger:
			return DemandCoreCouplingVisibility;
		case EABTSM73BeamC3Stage1DiagnosticLayer::LocalPodiumHeightPlan:
			return LocalPodiumHeightPlanVisibility;
		default:
			return 0;
		}
	}

	uint32 DiagnosticVisibilityMask(
		const EABTSM73BeamC3Stage2DiagnosticLayer Layer)
	{
		switch (Layer)
		{
		case EABTSM73BeamC3Stage2DiagnosticLayer::CouplingCoursesOnly:
			return Stage2CouplingOnlyVisibility;
		case EABTSM73BeamC3Stage2DiagnosticLayer::CouplingProvenance:
			return Stage2ProvenanceVisibility;
		case EABTSM73BeamC3Stage2DiagnosticLayer::CoreAndCouplingCourses:
			return Stage2CoreAndCouplingVisibility;
		case EABTSM73BeamC3Stage2DiagnosticLayer::PerimeterCoreFaces:
			return Stage2PerimeterCoreFacesVisibility;
		case EABTSM73BeamC3Stage2DiagnosticLayer::FacadePartitionsAndHeightAnchors:
			return Stage2FacadePartitionsVisibility;
		default:
			return 0;
		}
	}

	uint32 DiagnosticVisibilityMask(
		const EABTSM73BeamC3Stage3DiagnosticLayer Layer)
	{
		switch (Layer)
		{
		case EABTSM73BeamC3Stage3DiagnosticLayer::ExteriorFramesOnly:
			return Stage3ExteriorFramesVisibility;
		case EABTSM73BeamC3Stage3DiagnosticLayer::GroundSillOnly:
			return Stage3GroundSillVisibility;
		case EABTSM73BeamC3Stage3DiagnosticLayer::GroundToFirstFrameColumns:
			return Stage3GroundExteriorColumnsVisibility;
		case EABTSM73BeamC3Stage3DiagnosticLayer::ExteriorColumnsOnly:
			return Stage3ExteriorColumnsVisibility;
		case EABTSM73BeamC3Stage3DiagnosticLayer::Stage123Overview:
			return Stage3OverviewVisibility;
		default:
			return 0;
		}
	}

	uint32 DiagnosticVisibilityMask(
		const EABTSM73BeamC3Stage4DiagnosticLayer Layer)
	{
		switch (Layer)
		{
		case EABTSM73BeamC3Stage4DiagnosticLayer::TopSurfaceIntent:
			return Stage4TopSurfaceIntentVisibility;
		case EABTSM73BeamC3Stage4DiagnosticLayer::FloorTopFrames:
			return Stage4FloorTopFramesVisibility;
		case EABTSM73BeamC3Stage4DiagnosticLayer::FacadeToTopConnections:
			return Stage4FacadeToTopVisibility;
		case EABTSM73BeamC3Stage4DiagnosticLayer::FloorStyleInfill:
			return Stage4FloorStyleInfillVisibility;
		case EABTSM73BeamC3Stage4DiagnosticLayer::RoofCrown:
			return Stage4RoofCrownVisibility;
		case EABTSM73BeamC3Stage4DiagnosticLayer::Stage14Overview:
			return Stage4OverviewVisibility;
		default:
			return 0;
		}
	}

	bool ShouldShowSemanticSupportDemandVolumes(const bool bHideVolumes)
	{
		return !bHideVolumes;
	}

	UMaterialInstanceDynamic* MakeDiagnosticMaterial(
		UObject* Owner, UMaterialInterface* Parent, const FLinearColor& Color)
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

	void AddBoxInstance(
		UHierarchicalInstancedStaticMeshComponent* Component, const FBox& Box)
	{
		if (Component == nullptr || !Box.IsValid)
		{
			return;
		}
		Component->AddInstance(FTransform(
			FQuat::Identity, Box.GetCenter(), Box.GetSize() / 100.0), false);
	}

	bool PreviewSupportProvinceWordContains(
		const TArray<uint64>& Words, const int32 BitIndex)
	{
		return BitIndex >= 0 && Words.IsValidIndex(BitIndex >> 6)
			&& (Words[BitIndex >> 6] & (uint64(1) << (BitIndex & 63))) != 0;
	}

	void AddSegmentInstance(
		UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& Start,
		const FVector& End,
		const double ThicknessCM = 12.0)
	{
		if (Component == nullptr)
		{
			return;
		}
		const FVector Delta = End - Start;
		const double LengthCM = Delta.Size();
		if (LengthCM <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}
		const FQuat Rotation = FQuat::FindBetweenNormals(
			FVector::ForwardVector, Delta / LengthCM);
		Component->AddInstance(FTransform(
			Rotation, (Start + End) * 0.5,
			FVector(LengthCM, ThicknessCM, ThicknessCM) / 100.0), false);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamC3V3PreviewDiagnosticContractsTest,
	"ABTS.M73DAG.BeamC3V3.Staged.PreviewDiagnosticContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamC3V3PreviewDiagnosticContractsTest::RunTest(const FString& Parameters)
{
	using namespace ABTSM73BeamD1Preview;
	const uint32 WFCMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::WFCSemanticEnvelope);
	const uint32 IntentMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CorePlacementIntent);
	const uint32 MembersMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses);
	const uint32 MergeMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CoreMergeRegions);
	const uint32 XMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes);
	const uint32 YMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes);
	const uint32 SupportDemandMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SemanticSupportDemandDAG);
	const uint32 SupportProvinceMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition);
	const uint32 SupportProvinceMainMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment);
	const uint32 DemandCoreCouplingMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger);
	const uint32 LocalPodiumHeightPlanMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::LocalPodiumHeightPlan);
	const uint32 Stage2PerimeterMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage2DiagnosticLayer::PerimeterCoreFaces);
	const uint32 Stage2FacadePartitionMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage2DiagnosticLayer::FacadePartitionsAndHeightAnchors);
	const uint32 Stage3FrameMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage3DiagnosticLayer::ExteriorFramesOnly);
	const uint32 Stage3ColumnMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage3DiagnosticLayer::ExteriorColumnsOnly);
	const uint32 Stage3GroundSillMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage3DiagnosticLayer::GroundSillOnly);
	const uint32 Stage3GroundColumnMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage3DiagnosticLayer::GroundToFirstFrameColumns);
	const uint32 Stage3OverviewMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage3DiagnosticLayer::Stage123Overview);
	const uint32 Stage4IntentMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::TopSurfaceIntent);
	const uint32 Stage4FramesMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::FloorTopFrames);
	const uint32 Stage4FacadeToTopMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::FacadeToTopConnections);
	const uint32 Stage4FloorStyleInfillMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::FloorStyleInfill);
	const uint32 Stage4RoofCrownMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::RoofCrown);
	const uint32 Stage4OverviewMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage4DiagnosticLayer::Stage14Overview);
	TestEqual(TEXT("WFC layer contains only envelope and protected void"),
		WFCMask, static_cast<uint32>(SemanticEnvelopeVisibility | ProtectedVoidVisibility));
	TestEqual(TEXT("Intent layer contains only core and pairing intent"),
		IntentMask, static_cast<uint32>(CoreIntentVisibility | PairIntentVisibility));
	TestEqual(TEXT("Member layer contains only actual core/shared members"),
		MembersMask, static_cast<uint32>(CoreAndSharedVisibility));
	TestEqual(TEXT("Merge layer contains only derived core merge regions"),
		MergeMask, static_cast<uint32>(CoreMergeRegionVisibility));
	TestEqual(TEXT("X lane layer contains only actual X core lanes"),
		XMask, static_cast<uint32>(CompositeCoreXVisibility));
	TestEqual(TEXT("Y lane layer contains only actual Y core lanes"),
		YMask, static_cast<uint32>(CompositeCoreYVisibility));
	TestEqual(TEXT("Support-demand layer contains only the semantic support graph"),
		SupportDemandMask,
		static_cast<uint32>(SemanticSupportDemandVisibility));
	TestEqual(TEXT("Support-province layer contains only the province partition"),
		SupportProvinceMask,
		static_cast<uint32>(SupportProvinceVisibility));
	TestEqual(TEXT("Stage-2 coupling-only layer is mutually exclusive"),
		DiagnosticVisibilityMask(
			EABTSM73BeamC3Stage2DiagnosticLayer::CouplingCoursesOnly),
		static_cast<uint32>(Stage2CouplingOnlyVisibility));
	TestEqual(TEXT("Stage-2 provenance layer is mutually exclusive"),
		DiagnosticVisibilityMask(
			EABTSM73BeamC3Stage2DiagnosticLayer::CouplingProvenance),
		static_cast<uint32>(Stage2ProvenanceVisibility));
	TestEqual(TEXT("Stage-2 combined layer is mutually exclusive"),
		DiagnosticVisibilityMask(
			EABTSM73BeamC3Stage2DiagnosticLayer::CoreAndCouplingCourses),
		static_cast<uint32>(Stage2CoreAndCouplingVisibility));
	TestEqual(TEXT("Stage-2 perimeter layer is mutually exclusive"),
		Stage2PerimeterMask,
		static_cast<uint32>(Stage2PerimeterCoreFacesVisibility));
	TestEqual(TEXT("Stage-2 facade-partition layer is mutually exclusive"),
		Stage2FacadePartitionMask,
		static_cast<uint32>(Stage2FacadePartitionsVisibility));
	TestEqual(TEXT("Stage-3 exterior-frame layer is mutually exclusive"),
		Stage3FrameMask, static_cast<uint32>(Stage3ExteriorFramesVisibility));
	TestEqual(TEXT("Stage-3 exterior-column layer is mutually exclusive"),
		Stage3ColumnMask, static_cast<uint32>(Stage3ExteriorColumnsVisibility));
	TestEqual(TEXT("Stage-3 ground-sill layer is mutually exclusive"),
		Stage3GroundSillMask, static_cast<uint32>(Stage3GroundSillVisibility));
	TestEqual(TEXT("Stage-3 ground-column layer is mutually exclusive"),
		Stage3GroundColumnMask,
		static_cast<uint32>(Stage3GroundExteriorColumnsVisibility));
	TestEqual(TEXT("Stage-3 overview layer has one independent visibility bit"),
		Stage3OverviewMask, static_cast<uint32>(Stage3OverviewVisibility));
	TestEqual(TEXT("Stage-4 TopSurface intent has one independent visibility bit"),
		Stage4IntentMask, static_cast<uint32>(Stage4TopSurfaceIntentVisibility));
	TestEqual(TEXT("Stage-4 floor/top frames have one independent visibility bit"),
		Stage4FramesMask, static_cast<uint32>(Stage4FloorTopFramesVisibility));
	TestEqual(TEXT("Stage-4 Facade-to-Top has one independent visibility bit"),
		Stage4FacadeToTopMask, static_cast<uint32>(Stage4FacadeToTopVisibility));
	TestEqual(TEXT("Stage-4 Floor / StyleInfill has one independent visibility bit"),
		Stage4FloorStyleInfillMask,
		static_cast<uint32>(Stage4FloorStyleInfillVisibility));
	TestEqual(TEXT("Stage-4 Roof / Crown has one independent visibility bit"),
		Stage4RoofCrownMask, static_cast<uint32>(Stage4RoofCrownVisibility));
	TestEqual(TEXT("Stage 1-4 overview has one independent visibility bit"),
		Stage4OverviewMask, static_cast<uint32>(Stage4OverviewVisibility));
	TestEqual(TEXT("Stage-4 diagnostic layers are mutually exclusive"),
		(Stage4IntentMask & Stage4FramesMask)
			| (Stage4IntentMask & Stage4FacadeToTopMask)
			| (Stage4IntentMask & Stage4FloorStyleInfillMask)
			| (Stage4FramesMask & Stage4FacadeToTopMask)
			| (Stage4FramesMask & Stage4FloorStyleInfillMask)
			| (Stage4FacadeToTopMask & Stage4FloorStyleInfillMask)
			| (Stage4IntentMask & Stage4RoofCrownMask)
			| (Stage4FramesMask & Stage4RoofCrownMask)
			| (Stage4FacadeToTopMask & Stage4RoofCrownMask)
			| (Stage4FloorStyleInfillMask & Stage4RoofCrownMask)
			| (Stage4OverviewMask & (Stage4IntentMask | Stage4FramesMask
				| Stage4FacadeToTopMask | Stage4FloorStyleInfillMask
				| Stage4RoofCrownMask)),
		static_cast<uint32>(0));
	TestEqual(TEXT("Stage-4 intent is disjoint from the Stage-3 overview"),
		Stage4IntentMask & Stage3OverviewMask, static_cast<uint32>(0));
	TestEqual(TEXT("Stage-3 frame and column layers are disjoint"),
		Stage3FrameMask & Stage3ColumnMask, static_cast<uint32>(0));
	TestEqual(TEXT("Stage-3 sill and ground-column layers are disjoint"),
		Stage3GroundSillMask & Stage3GroundColumnMask, static_cast<uint32>(0));
	TestEqual(TEXT("Stage-3 overview and individual layers are disjoint"),
		Stage3OverviewMask & (Stage3FrameMask | Stage3ColumnMask
			| Stage3GroundSillMask | Stage3GroundColumnMask),
		static_cast<uint32>(0));
	TestEqual(TEXT("Overview classifies Stage 1 core/shared members"),
		Stage3OverviewBucket(EABTSM73BeamC3GenerationStage::CoreAndShared, false),
		EStage3OverviewBucket::Stage1CoreAndShared);
	TestEqual(TEXT("Overview classifies Stage 2 coupling members"),
		Stage3OverviewBucket(EABTSM73BeamC3GenerationStage::CouplingCourses, false),
		EStage3OverviewBucket::Stage2Coupling);
	TestEqual(TEXT("Overview classifies Stage 3 exterior members"),
		Stage3OverviewBucket(EABTSM73BeamC3GenerationStage::CommonExteriorFrame, false),
		EStage3OverviewBucket::Stage3Exterior);
	TestEqual(TEXT("Overview gives a reused ground sill Stage 3 visual ownership"),
		Stage3OverviewBucket(EABTSM73BeamC3GenerationStage::CoreAndShared, true),
		EStage3OverviewBucket::Stage3Exterior);
	TestEqual(TEXT("Province-main layer contains only the assignment plan"),
		SupportProvinceMainMask,
		static_cast<uint32>(SupportProvinceMainVisibility));
	TestEqual(TEXT("Demand-core layer contains only the correspondence ledger"),
		DemandCoreCouplingMask,
		static_cast<uint32>(DemandCoreCouplingVisibility));
	TestEqual(TEXT("Local-podium layer contains only the height plan"),
		LocalPodiumHeightPlanMask,
		static_cast<uint32>(LocalPodiumHeightPlanVisibility));
	TestEqual(TEXT("WFC and intent layers are disjoint"),
		static_cast<uint16>(WFCMask & IntentMask), static_cast<uint16>(0));
	TestEqual(TEXT("WFC and member layers are disjoint"),
		static_cast<uint16>(WFCMask & MembersMask), static_cast<uint16>(0));
	TestEqual(TEXT("Intent and member layers are disjoint"),
		static_cast<uint16>(IntentMask & MembersMask), static_cast<uint16>(0));
	TestEqual(TEXT("Merge layer is disjoint from WFC"),
		static_cast<uint16>(MergeMask & WFCMask), static_cast<uint16>(0));
	TestEqual(TEXT("Merge layer is disjoint from intent"),
		static_cast<uint16>(MergeMask & IntentMask), static_cast<uint16>(0));
	TestEqual(TEXT("Merge layer is disjoint from members"),
		static_cast<uint16>(MergeMask & MembersMask), static_cast<uint16>(0));
	TestEqual(TEXT("X and Y lane layers are disjoint"),
		static_cast<uint16>(XMask & YMask), static_cast<uint16>(0));
	TestEqual(TEXT("X lane layer is disjoint from full members"),
		static_cast<uint16>(XMask & MembersMask), static_cast<uint16>(0));
	TestEqual(TEXT("Y lane layer is disjoint from full members"),
		static_cast<uint16>(YMask & MembersMask), static_cast<uint16>(0));
	TestEqual(TEXT("Support-demand graph is disjoint from WFC, intent, and members"),
		static_cast<uint16>(SupportDemandMask
			& (WFCMask | IntentMask | MembersMask)), static_cast<uint16>(0));
	TestEqual(TEXT("Support-province partition is disjoint from all prior layers"),
		static_cast<uint16>(SupportProvinceMask
			& (WFCMask | IntentMask | MembersMask | MergeMask | XMask | YMask
				| SupportDemandMask)), static_cast<uint16>(0));
	TestEqual(TEXT("Province-main assignment is disjoint from all prior layers"),
		static_cast<uint16>(SupportProvinceMainMask
			& (WFCMask | IntentMask | MembersMask | MergeMask | XMask | YMask
				| SupportDemandMask | SupportProvinceMask)), static_cast<uint16>(0));
	TestEqual(TEXT("Demand-core ledger is disjoint from all prior layers"),
		static_cast<uint16>(DemandCoreCouplingMask
			& (WFCMask | IntentMask | MembersMask | MergeMask | XMask | YMask
				| SupportDemandMask | SupportProvinceMask | SupportProvinceMainMask)),
		static_cast<uint16>(0));
	TestEqual(TEXT("Local-podium height plan is disjoint from all prior layers"),
		static_cast<uint16>(LocalPodiumHeightPlanMask
			& (WFCMask | IntentMask | MembersMask | MergeMask | XMask | YMask
				| SupportDemandMask | SupportProvinceMask | SupportProvinceMainMask
				| DemandCoreCouplingMask)), static_cast<uint16>(0));
	TestTrue(TEXT("Support-demand volumes are visible by default"),
		ShouldShowSemanticSupportDemandVolumes(false));
	TestFalse(TEXT("Lines-only option hides support-demand volumes"),
		ShouldShowSemanticSupportDemandVolumes(true));

	const FBox Bounds(FVector(-108.0, -72.0, 0.0), FVector(108.0, 72.0, 180.0));
	auto TestOutwardWinding = [this, &Bounds](
		const TCHAR* Label, const FMeshBuffers& Buffers, const int32 ExpectedTriangleCount)
	{
		TestEqual(FString::Printf(TEXT("%s triangle count"), Label),
			Buffers.Triangles.Num() / 3, ExpectedTriangleCount);
		for (int32 TriangleOffset = 0;
			TriangleOffset + 2 < Buffers.Triangles.Num(); TriangleOffset += 3)
		{
			const int32 IA = Buffers.Triangles[TriangleOffset];
			const int32 IB = Buffers.Triangles[TriangleOffset + 1];
			const int32 IC = Buffers.Triangles[TriangleOffset + 2];
			const FVector TriangleCenter =
				(Buffers.Vertices[IA] + Buffers.Vertices[IB] + Buffers.Vertices[IC]) / 3.0;
			const FVector GeometricNormal = FVector::CrossProduct(
				Buffers.Vertices[IB] - Buffers.Vertices[IA],
				Buffers.Vertices[IC] - Buffers.Vertices[IA]).GetSafeNormal();
			TestTrue(FString::Printf(TEXT("%s triangle %d faces outward"),
				Label, TriangleOffset / 3),
				FVector::DotProduct(GeometricNormal,
					TriangleCenter - Bounds.GetCenter()) > UE_DOUBLE_SMALL_NUMBER);
		}
	};

	FMeshBuffers BoxBuffers;
	AppendBox(Bounds, BoxBuffers);
	TestOutwardWinding(TEXT("Box"), BoxBuffers, 12);
	FMeshBuffers PrismXBuffers;
	AppendPrismX(Bounds, PrismXBuffers);
	TestOutwardWinding(TEXT("PrismX"), PrismXBuffers, 8);
	FMeshBuffers PrismYBuffers;
	AppendPrismY(Bounds, PrismYBuffers);
	TestOutwardWinding(TEXT("PrismY"), PrismYBuffers, 8);
	FMeshBuffers PyramidBuffers;
	AppendPyramid(Bounds, PyramidBuffers);
	TestOutwardWinding(TEXT("Pyramid"), PyramidBuffers, 6);
	return true;
}
#endif

AABTSM73BeamD1PreviewActor::AABTSM73BeamD1PreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	WoodPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("WoodBrickPreview"));
	StonePreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("StoneBrickPreview"));
	IronPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("IronBrickPreview"));
	GlassPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("GlassBrickPreview"));
	CoreIntentPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("BeamC3CoreIntentPreview"));
	TowerChildIntentPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("BeamC3TowerChildIntentPreview"));
	CoreMergeRegionPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("BeamC3CoreMergeRegionPreview"));
	SharedPairIntentPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("BeamC3SharedPairIntentPreview"));
	ProtectedVoidPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("BeamC3ProtectedVoidPreview"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get(),
		CoreIntentPreview.Get(), TowerChildIntentPreview.Get(), CoreMergeRegionPreview.Get(),
		SharedPairIntentPreview.Get(), ProtectedVoidPreview.Get()})
	{
		ABTSM73BeamD1Preview::ConfigurePreview(*Preview, *Root);
	}
	SemanticEnvelopePreview = CreateDefaultSubobject<UProceduralMeshComponent>(
		TEXT("BeamC3SemanticEnvelopePreview"));
	SemanticEnvelopePreview->SetupAttachment(Root);
	SemanticEnvelopePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SemanticEnvelopePreview->SetGenerateOverlapEvents(false);
	SemanticEnvelopePreview->SetCanEverAffectNavigation(false);
	SemanticEnvelopePreview->SetHiddenInGame(true);
	SemanticEnvelopePreview->bUseAsyncCooking = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Wood.MI_Bricks_Wood"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stone(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Stone.MI_Bricks_Stone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Iron(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Steel.MI_Bricks_Steel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glass(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Glass.MI_Bricks_Glass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Basic(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get(),
		CoreIntentPreview.Get(), TowerChildIntentPreview.Get(), CoreMergeRegionPreview.Get(),
		SharedPairIntentPreview.Get(), ProtectedVoidPreview.Get()})
	{
		if (Cube.Succeeded())
		{
			Preview->SetStaticMesh(Cube.Object);
		}
	}
	if (Wood.Succeeded()) WoodPreview->SetMaterial(0, Wood.Object);
	if (Stone.Succeeded()) StonePreview->SetMaterial(0, Stone.Object);
	if (Iron.Succeeded()) IronPreview->SetMaterial(0, Iron.Object);
	if (Glass.Succeeded()) GlassPreview->SetMaterial(0, Glass.Object);
	if (Glass.Succeeded())
	{
		SemanticEnvelopeMaterial = Glass.Object;
		SemanticEnvelopePreview->SetMaterial(0, Glass.Object);
	}
	if (Basic.Succeeded())
	{
		DiagnosticSolidMaterial = Basic.Object;
	}
	if (Iron.Succeeded()) CoreIntentPreview->SetMaterial(0, Iron.Object);
	if (Stone.Succeeded()) TowerChildIntentPreview->SetMaterial(0, Stone.Object);
	if (Glass.Succeeded()) CoreMergeRegionPreview->SetMaterial(0, Glass.Object);
	if (Stone.Succeeded()) SharedPairIntentPreview->SetMaterial(0, Stone.Object);
	if (Glass.Succeeded()) ProtectedVoidPreview->SetMaterial(0, Glass.Object);
}

void AABTSM73BeamD1PreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73BeamD1PreviewActor::BeginPlay()
{
	Super::BeginPlay();
	RegeneratePreview();
	if (!bSpawnRuntimeModulesInPIE || GetWorld() == nullptr)
	{
		return;
	}
	RuntimeSystemSearchAttempts = 0;
	TryInitializeRuntimeBuilding();
}

void AABTSM73BeamD1PreviewActor::TryInitializeRuntimeBuilding()
{
	if (!bSpawnRuntimeModulesInPIE || GetWorld() == nullptr
		|| !RuntimeModules.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It)
	{
		const bool bInitialized = InitializeRuntimeBuilding(*It);
		GetWorldTimerManager().ClearTimer(RuntimeSystemSearchTimer);
		if (bInitialized)
		{
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-Beam-D1][RuntimeModulesSpawned]")
				TEXT(" Actor=%s Modules=%d Attempts=%d"),
				*GetName(), GetRuntimeModuleCountForValidation(),
				RuntimeSystemSearchAttempts + 1);
		}
		else
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-D1][RuntimeModulesRejected]")
				TEXT(" Actor=%s Accepted=%d Bricks=%d ExistingModules=%d"),
				*GetName(), LastSummary.bAccepted ? 1 : 0,
				CompiledBricks.Num(), GetRuntimeModuleCountForValidation());
		}
		return;
	}

	++RuntimeSystemSearchAttempts;
	if (RuntimeSystemSearchAttempts
		< ABTSM73BeamD1Preview::MaximumRuntimeSystemSearchAttempts)
	{
		GetWorldTimerManager().SetTimer(
			RuntimeSystemSearchTimer,
			this,
			&AABTSM73BeamD1PreviewActor::TryInitializeRuntimeBuilding,
			ABTSM73BeamD1Preview::RuntimeSystemSearchIntervalSeconds,
			false);
		return;
	}

	UE_LOG(LogABTSRuntime, Warning,
		TEXT("[ABTS][M7.3-Beam-D1][RuntimeMaterialSystemTimeout]")
		TEXT(" Actor=%s Attempts=%d"),
		*GetName(), RuntimeSystemSearchAttempts);
}

void AABTSM73BeamD1PreviewActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld() != nullptr)
	{
		GetWorldTimerManager().ClearTimer(RuntimeSystemSearchTimer);
	}
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Module : RuntimeModules)
	{
		if (Module.IsValid())
		{
			Module->Destroy();
		}
	}
	RuntimeModules.Reset();
	Super::EndPlay(EndPlayReason);
}

void AABTSM73BeamD1PreviewActor::ClearPreview()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		if (Preview != nullptr)
		{
			Preview->ClearInstances();
			Preview->SetVisibility(bShowEditorPreview, true);
		}
	}
	ClearStageDiagnostics();
}

bool AABTSM73BeamD1PreviewActor::ConfigureForAutomatedCapture(
	const EABTSM73BeamDemoBuilding InDemoBuilding,
	const EABTSM73BeamC3Stage4DiagnosticLayer InLayer,
	FString& OutError)
{
	DemoBuilding = InDemoBuilding;
	GenerationStopStage = EABTSM73BeamC3GenerationStage::FloorInfillRoof;
	Stage4DiagnosticLayer = InLayer;
	bShowEditorPreview = true;
	bSpawnRuntimeModulesInPIE = false;
	RegeneratePreview();
	if (!LastSummary.bAccepted)
	{
		OutError = FString::Printf(
			TEXT("Stage4PreviewRejected:Demo=%d:Layer=%d"),
			static_cast<int32>(InDemoBuilding), static_cast<int32>(InLayer));
		return false;
	}

	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get(),
		CoreIntentPreview.Get(), TowerChildIntentPreview.Get(), CoreMergeRegionPreview.Get(),
		SharedPairIntentPreview.Get(), ProtectedVoidPreview.Get()})
	{
		if (Preview != nullptr)
		{
			Preview->SetHiddenInGame(false);
			Preview->MarkRenderStateDirty();
		}
	}
	if (SemanticEnvelopePreview != nullptr)
	{
		SemanticEnvelopePreview->SetHiddenInGame(false);
		SemanticEnvelopePreview->MarkRenderStateDirty();
	}
	OutError.Reset();
	return true;
}

bool AABTSM73BeamD1PreviewActor::ConfigureStage5ProductionForAutomatedCapture(
	const EABTSM73BeamDemoBuilding InDemoBuilding,
	const bool bAdditionsOnly,
	FString& OutError)
{
	ClearPreview();
	CompiledBricks.Reset();
	LastSummary = FABTSM73BeamD1Summary();
	DemoBuilding = InDemoBuilding;
	bShowEditorPreview = true;
	bSpawnRuntimeModulesInPIE = false;

	FABTSM73BeamDemoManifestEntry DemoEntry;
	if (!FABTSM73BeamDemoManifest::Resolve(InDemoBuilding, DemoEntry, OutError))
	{
		return false;
	}
	Settings = DemoEntry.Settings;

	FABTSM73BeamD1BrickCompiler Compiler;
	FABTSM73BeamD1Stage5Result Result;
	if (!Compiler.GenerateStage5(Settings, Result, OutError))
	{
		LastSummary = Result.Summary;
		return false;
	}

	LastSummary = Result.Summary;
	CompiledBricks = Result.Bricks;
	const int32 ReachabilityEnd = Result.Stage4ActiveMemberCount
		+ Result.ReachabilitySupportPostCount;
	for (const FABTSM73BeamD1BrickBinding& Brick : CompiledBricks)
	{
		if (bAdditionsOnly && Brick.BrickId < Result.Stage4ActiveMemberCount)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Preview =
			Brick.BrickId < Result.Stage4ActiveMemberCount
				? WoodPreview.Get()
				: Brick.BrickId < ReachabilityEnd
					? IronPreview.Get() : StonePreview.Get();
		if (Preview == nullptr)
		{
			continue;
		}
		FTransform InstanceTransform = Brick.LocalTransform;
		InstanceTransform.SetScale3D(Brick.BrickSpec.DimensionsCM / 100.0f);
		Preview->AddInstance(InstanceTransform, false);
	}

	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		if (Preview != nullptr)
		{
			Preview->SetVisibility(Preview->GetInstanceCount() > 0, true);
			Preview->SetHiddenInGame(false);
			Preview->MarkRenderStateDirty();
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-D1][Stage5ProductionCapturePrepared]")
		TEXT(" Demo=%s AdditionsOnly=%d Stage4=%d Reachability=%d Closure=%d Bricks=%d"),
		*DemoEntry.StableId.ToString(), bAdditionsOnly ? 1 : 0,
		Result.Stage4ActiveMemberCount, Result.ReachabilitySupportPostCount,
		Result.StructuralClosureMemberCount, Result.Bricks.Num());
	OutError.Reset();
	return true;
}

FBox AABTSM73BeamD1PreviewActor::GetAutomatedCaptureBounds() const
{
	FBox Bounds(EForceInit::ForceInit);
	for (const UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get(),
		CoreIntentPreview.Get(), TowerChildIntentPreview.Get(), CoreMergeRegionPreview.Get(),
		SharedPairIntentPreview.Get(), ProtectedVoidPreview.Get()})
	{
		if (Preview != nullptr && Preview->IsVisible()
			&& Preview->GetInstanceCount() > 0)
		{
			Bounds += Preview->CalcBounds(Preview->GetComponentTransform()).GetBox();
		}
	}
	if (SemanticEnvelopePreview != nullptr && SemanticEnvelopePreview->IsVisible())
	{
		Bounds += SemanticEnvelopePreview->CalcBounds(
			SemanticEnvelopePreview->GetComponentTransform()).GetBox();
	}
	return Bounds;
}

void AABTSM73BeamD1PreviewActor::ClearStageDiagnostics()
{
	if (SemanticEnvelopePreview != nullptr)
	{
		SemanticEnvelopePreview->ClearAllMeshSections();
		SemanticEnvelopePreview->SetVisibility(false, true);
	}
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		CoreIntentPreview.Get(), TowerChildIntentPreview.Get(), CoreMergeRegionPreview.Get(),
		SharedPairIntentPreview.Get(), ProtectedVoidPreview.Get()})
	{
		if (Preview != nullptr)
		{
			Preview->ClearInstances();
			Preview->SetVisibility(false, true);
		}
	}
	StageDiagnosticMIDs.Reset();
}

UHierarchicalInstancedStaticMeshComponent* AABTSM73BeamD1PreviewActor::GetPreview(
	const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Stone: return StonePreview;
	case EABTSM7BuildingMaterial::Iron: return IronPreview;
	case EABTSM7BuildingMaterial::Glass: return GlassPreview;
	default: return WoodPreview;
	}
}

void AABTSM73BeamD1PreviewActor::RegeneratePreview()
{
	ClearPreview();
	CompiledBricks.Reset();
	LastSummary = FABTSM73BeamD1Summary();
	FString Error;
	if (DemoBuilding != EABTSM73BeamDemoBuilding::Custom)
	{
		FABTSM73BeamDemoManifestEntry DemoEntry;
		if (!FABTSM73BeamDemoManifest::Resolve(DemoBuilding, DemoEntry, Error))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-D1][DemoManifestRejected]")
				TEXT(" Actor=%s Entry=%d ManifestVersion=%d Reason=%s"),
				*GetName(), static_cast<int32>(DemoBuilding),
				FABTSM73BeamDemoManifest::Version, *Error);
			return;
		}
		Settings = DemoEntry.Settings;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-D1][DemoManifestApplied]")
			TEXT(" Actor=%s Entry=%s ManifestVersion=%d ManifestHash=%lld")
			TEXT(" Profile=%s Tier=E%d Seed=%d"),
			*GetName(), *DemoEntry.StableId.ToString(),
			FABTSM73BeamDemoManifest::Version,
			FABTSM73BeamDemoManifest::CalculateHash(),
			*Settings.GameplayProfileId.ToString(), Settings.DifficultyTier + 1,
			Settings.BuildingSeed);
	}
	FABTSM73BeamD1BrickCompiler Compiler;
	if (GenerationStopStage != EABTSM73BeamC3GenerationStage::StaticDAG)
	{
		FABTSM73BeamD1StagePreviewResult StageResult;
		if (!Compiler.GenerateStagePreview(
			Settings, GenerationStopStage, StageResult, Error))
		{
			LastSummary = StageResult.Summary;
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M7.3-Beam-D1][StagePreviewRejected]")
				TEXT(" Actor=%s Stage=%d Layer=%d Reason=%s"),
				*GetName(), static_cast<int32>(GenerationStopStage),
				GenerationStopStage == EABTSM73BeamC3GenerationStage::CouplingCourses
					? static_cast<int32>(Stage2DiagnosticLayer)
					: GenerationStopStage
						== EABTSM73BeamC3GenerationStage::CommonExteriorFrame
							? static_cast<int32>(Stage3DiagnosticLayer)
							: GenerationStopStage
								== EABTSM73BeamC3GenerationStage::FloorInfillRoof
									? static_cast<int32>(Stage4DiagnosticLayer)
									: static_cast<int32>(Stage1DiagnosticLayer), *Error);
			return;
		}
		LastSummary = StageResult.Summary;
		if (!bShowEditorPreview)
		{
			return;
		}

		const EABTSM73BeamC3Stage1DiagnosticLayer EffectiveLayer =
			GenerationStopStage == EABTSM73BeamC3GenerationStage::SemanticEnvelope
				? EABTSM73BeamC3Stage1DiagnosticLayer::WFCSemanticEnvelope
				: Stage1DiagnosticLayer;
		const bool bStage2 = GenerationStopStage
			== EABTSM73BeamC3GenerationStage::CouplingCourses;
		const bool bStage3 = GenerationStopStage
			== EABTSM73BeamC3GenerationStage::CommonExteriorFrame;
		const bool bStage4 = GenerationStopStage
			== EABTSM73BeamC3GenerationStage::FloorInfillRoof;
		const uint32 VisibilityMask = bStage4
			? ABTSM73BeamD1Preview::DiagnosticVisibilityMask(Stage4DiagnosticLayer)
			: bStage3
			? ABTSM73BeamD1Preview::DiagnosticVisibilityMask(Stage3DiagnosticLayer)
			: bStage2
				? ABTSM73BeamD1Preview::DiagnosticVisibilityMask(Stage2DiagnosticLayer)
				: ABTSM73BeamD1Preview::DiagnosticVisibilityMask(EffectiveLayer);
		const bool bShowEnvelope =
			(VisibilityMask & ABTSM73BeamD1Preview::SemanticEnvelopeVisibility) != 0;
		if (bShowEnvelope && SemanticEnvelopePreview != nullptr)
		{
			TArray<ABTSM73BeamD1Preview::FMeshBuffers> Sections;
			Sections.SetNum(4);
			for (const FABTSM73DAG5BV2Volume& Volume : StageResult.Silhouette.Volumes)
			{
				ABTSM73BeamD1Preview::AppendVolume(
					Volume, Sections[ABTSM73BeamD1Preview::SectionForVolume(Volume)]);
			}
			for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
			{
				ABTSM73BeamD1Preview::FMeshBuffers& Buffers = Sections[SectionIndex];
				if (Buffers.Vertices.IsEmpty())
				{
					continue;
				}
				SemanticEnvelopePreview->CreateMeshSection_LinearColor(
					SectionIndex, Buffers.Vertices, Buffers.Triangles,
					Buffers.Normals, Buffers.UVs, Buffers.Colors,
					Buffers.Tangents, false);
				const FLinearColor Color = SectionIndex == 3
					? FLinearColor(1.0f, 0.35f, 0.05f, 0.42f)
					: SectionIndex == 2
						? FLinearColor(0.9f, 0.18f, 0.42f, 0.24f)
						: SectionIndex == 1
							? FLinearColor(0.05f, 0.75f, 0.58f, 0.24f)
							: FLinearColor(0.08f, 0.34f, 0.9f, 0.24f);
				UMaterialInterface* Parent = SectionIndex == 3
					? DiagnosticSolidMaterial.Get() : SemanticEnvelopeMaterial.Get();
				UMaterialInstanceDynamic* MID =
					ABTSM73BeamD1Preview::MakeDiagnosticMaterial(this, Parent, Color);
				if (MID != nullptr)
				{
					StageDiagnosticMIDs.Add(MID);
					SemanticEnvelopePreview->SetMaterial(SectionIndex, MID);
				}
			}
			SemanticEnvelopePreview->SetVisibility(true, true);
		}

		if ((VisibilityMask & ABTSM73BeamD1Preview::ProtectedVoidVisibility) != 0)
		{
			if (!StageResult.Skeleton.Plan.ReservedSupportVoids.IsEmpty())
			{
				for (const FABTSM73BeamASupportVoid& Void :
					StageResult.Skeleton.Plan.ReservedSupportVoids)
				{
					ABTSM73BeamD1Preview::AddBoxInstance(
						ProtectedVoidPreview, Void.Bounds);
				}
			}
			else
			{
				for (const FABTSM73DAG5BV2Volume& Volume : StageResult.Silhouette.Volumes)
				{
					if (Volume.Role != EABTSM73DAG5BV2VolumeRole::SupportedSpan
						|| (Volume.SpanAxisIndex != 0 && Volume.SpanAxisIndex != 1))
					{
						continue;
					}
					double GroundZ = Volume.LocalBounds.Min.Z;
					for (const FABTSM73DAG5BV2Volume& Support : StageResult.Silhouette.Volumes)
					{
						if (Support.VolumeId == Volume.NegativeSupportVolumeId
							|| Support.VolumeId == Volume.PositiveSupportVolumeId)
						{
							GroundZ = FMath::Min(GroundZ, Support.LocalBounds.Min.Z);
						}
					}
					FVector Minimum = Volume.LocalBounds.Min;
					FVector Maximum = Volume.LocalBounds.Max;
					Minimum.Z = GroundZ;
					Maximum.Z = Volume.LocalBounds.Min.Z;
					Minimum[Volume.SpanAxisIndex] = Volume.SpanOpeningMinCM;
					Maximum[Volume.SpanAxisIndex] = Volume.SpanOpeningMaxCM;
					ABTSM73BeamD1Preview::AddBoxInstance(
						ProtectedVoidPreview, FBox(Minimum, Maximum));
				}
			}
			ProtectedVoidPreview->SetVisibility(
				ProtectedVoidPreview->GetInstanceCount() > 0, true);
		}

		if ((VisibilityMask
			& ABTSM73BeamD1Preview::SemanticSupportDemandVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			const bool bShowSupportDemandVolumes =
				ABTSM73BeamD1Preview::ShouldShowSemanticSupportDemandVolumes(
					bHideSemanticSupportDemandVolumes);
			for (const ABTSM73BeamC3V3::FSemanticSupportVolumeNodeDiagnostic& Node
				: Plan.SemanticSupportVolumeNodes)
			{
				if (bShowSupportDemandVolumes && Node.bSquareBody)
				{
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreMergeRegionPreview, Node.LocalBounds);
				}
				for (const int32 ChildNodeId : Node.ChildNodeIds)
				{
					if (!Plan.SemanticSupportVolumeNodes.IsValidIndex(ChildNodeId))
					{
						continue;
					}
					const ABTSM73BeamC3V3::FSemanticSupportVolumeNodeDiagnostic& Child =
						Plan.SemanticSupportVolumeNodes[ChildNodeId];
					ABTSM73BeamD1Preview::AddSegmentInstance(
						SharedPairIntentPreview,
						Node.LocalBounds.GetCenter(), Child.LocalBounds.GetCenter());
				}
				if (Node.bTerminalBody || Node.bGraphTerminal)
				{
					const FVector Center = Node.LocalBounds.GetCenter();
					const double HalfExtent = Node.bTerminalBody ? 18.0 : 12.0;
					UHierarchicalInstancedStaticMeshComponent* Marker =
						Node.bTerminalBody ? StonePreview.Get()
						: Node.Role == EABTSM73DAG5BV2VolumeRole::Crown
							? GlassPreview.Get() : IronPreview.Get();
					ABTSM73BeamD1Preview::AddBoxInstance(Marker, FBox(
						Center - FVector(HalfExtent),
						Center + FVector(HalfExtent)));
				}
			}
			for (const ABTSM73BeamC3V3::FSemanticTerminalDemandDiagnostic& Demand
				: Plan.SemanticTerminalDemands)
			{
				if (bShowSupportDemandVolumes)
				{
					const FBox DemandBounds = Demand.bHasContinuousCoreFit
						? Demand.ContinuousCoreFitBounds
						: Demand.GroundProjectionBounds;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, DemandBounds);
				}
				const FVector LoadCenter = Demand.TerminalLoadBounds.GetCenter();
				ABTSM73BeamD1Preview::AddBoxInstance(
					TowerChildIntentPreview, FBox(
						LoadCenter - FVector(8.0),
						LoadCenter + FVector(8.0)));
			}
			CoreMergeRegionPreview->SetVisibility(
				bShowSupportDemandVolumes
					&& CoreMergeRegionPreview->GetInstanceCount() > 0, true);
			CoreIntentPreview->SetVisibility(
				bShowSupportDemandVolumes
					&& CoreIntentPreview->GetInstanceCount() > 0, true);
			SharedPairIntentPreview->SetVisibility(
				SharedPairIntentPreview->GetInstanceCount() > 0, true);
			for (UHierarchicalInstancedStaticMeshComponent* Marker : {
				StonePreview.Get(), GlassPreview.Get(), IronPreview.Get(),
				TowerChildIntentPreview.Get()})
			{
				Marker->SetVisibility(Marker->GetInstanceCount() > 0, true);
			}
		}
		else if ((VisibilityMask
			& ABTSM73BeamD1Preview::LocalPodiumHeightPlanVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			TArray<UHierarchicalInstancedStaticMeshComponent*> RegionPreviews{
				WoodPreview.Get(), StonePreview.Get(), GlassPreview.Get(),
				TowerChildIntentPreview.Get()};
			for (const ABTSM73BeamC3V3::FLocalPodiumHeightRegionDiagnostic& Region
				: Plan.LocalPodiumHeightRegions)
			{
				UHierarchicalInstancedStaticMeshComponent* RegionPreview =
					RegionPreviews[Region.RegionId % RegionPreviews.Num()];
				for (const int32 ProvinceId : Region.ProvinceIds)
				{
					const ABTSM73BeamC3V3::FSupportProvinceDiagnostic* Province =
						Plan.SupportProvinces.FindByPredicate(
							[ProvinceId](
								const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Candidate)
							{
								return Candidate.ProvinceId == ProvinceId;
							});
					if (Province == nullptr)
					{
						continue;
					}
					const double GroundZ = Province->GroundBounds.Min.Z;
					const double ActualTopZ = GroundZ
						+ Region.ActualPodiumTopCourse * 36.0;
					const double SelectedTopZ = GroundZ
						+ Region.SelectedTopCourse * 36.0;
					const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic*
						SelectedCandidate = Plan.LocalPodiumHeightCandidates.FindByPredicate(
							[ProvinceId, &Region](
								const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate)
							{
								return Candidate.ProvinceId == ProvinceId
									&& Candidate.CandidateTopCourse
										== Region.SelectedTopCourse
									&& Candidate.bAccepted && Candidate.bSelected;
							});
					if (SelectedCandidate == nullptr)
					{
						continue;
					}
					for (int32 BitIndex = 0;
						BitIndex < Province->SizeX * Province->SizeY; ++BitIndex)
					{
						if (!ABTSM73BeamD1Preview::PreviewSupportProvinceWordContains(
							SelectedCandidate->PersistentCellWords, BitIndex))
						{
							continue;
						}
						const int32 X = BitIndex % Province->SizeX;
						const int32 Y = BitIndex / Province->SizeX;
						const double CenterX =
							(Province->MinimumXUnit + X) * 36.0;
						const double CenterY =
							(Province->MinimumYUnit + Y) * 36.0;
						ABTSM73BeamD1Preview::AddBoxInstance(RegionPreview, FBox(
							FVector(CenterX - 16.0, CenterY - 16.0,
								SelectedTopZ - 5.0),
							FVector(CenterX + 16.0, CenterY + 16.0,
								SelectedTopZ)));
						if (Region.bRaisesActualPodium)
						{
							ABTSM73BeamD1Preview::AddBoxInstance(
								CoreIntentPreview, FBox(
									FVector(CenterX - 14.0, CenterY - 14.0,
										ActualTopZ - 3.0),
									FVector(CenterX + 14.0, CenterY + 14.0,
										ActualTopZ)));
						}
					}
					const FVector ActualAnchor(
						Province->GroundCentroid.X, Province->GroundCentroid.Y,
						ActualTopZ);
					const FVector SelectedAnchor(
						Province->GroundCentroid.X, Province->GroundCentroid.Y,
						SelectedTopZ);
					ABTSM73BeamD1Preview::AddSegmentInstance(
						SharedPairIntentPreview, ActualAnchor, SelectedAnchor, 8.0);
				}
				for (int32 FirstIndex = 0;
					FirstIndex < Region.ProvinceIds.Num(); ++FirstIndex)
				{
					const int32 FirstProvinceId = Region.ProvinceIds[FirstIndex];
					const ABTSM73BeamC3V3::FSupportProvinceDiagnostic* FirstProvince =
						Plan.SupportProvinces.FindByPredicate(
							[FirstProvinceId](
								const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province)
							{
								return Province.ProvinceId == FirstProvinceId;
							});
					const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic* FirstCandidate =
						Plan.LocalPodiumHeightCandidates.FindByPredicate(
							[FirstProvinceId, &Region](
								const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate)
							{
								return Candidate.ProvinceId == FirstProvinceId
									&& Candidate.CandidateTopCourse == Region.SelectedTopCourse
									&& Candidate.bSelected;
							});
					if (FirstProvince == nullptr || FirstCandidate == nullptr)
					{
						continue;
					}
					for (int32 SecondIndex = FirstIndex + 1;
						SecondIndex < Region.ProvinceIds.Num(); ++SecondIndex)
					{
						const int32 SecondProvinceId = Region.ProvinceIds[SecondIndex];
						if (!FirstProvince->AdjacentProvinceIds.Contains(SecondProvinceId))
						{
							continue;
						}
						const ABTSM73BeamC3V3::FSupportProvinceDiagnostic* SecondProvince =
							Plan.SupportProvinces.FindByPredicate(
								[SecondProvinceId](
									const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province)
								{
									return Province.ProvinceId == SecondProvinceId;
								});
						const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic* SecondCandidate =
							Plan.LocalPodiumHeightCandidates.FindByPredicate(
								[SecondProvinceId, &Region](
									const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate)
								{
									return Candidate.ProvinceId == SecondProvinceId
										&& Candidate.CandidateTopCourse == Region.SelectedTopCourse
										&& Candidate.bSelected;
								});
						if (SecondProvince == nullptr || SecondCandidate == nullptr)
						{
							continue;
						}
						int32 MinimumManhattan = MAX_int32;
						FVector FirstCenter = FVector::ZeroVector;
						FVector SecondCenter = FVector::ZeroVector;
						const double Z = FirstProvince->GroundBounds.Min.Z
							+ Region.SelectedTopCourse * 36.0;
						for (int32 FirstBit = 0;
							FirstBit < FirstProvince->SizeX * FirstProvince->SizeY;
							++FirstBit)
						{
							if (!ABTSM73BeamD1Preview::PreviewSupportProvinceWordContains(
								FirstCandidate->PersistentCellWords, FirstBit))
							{
								continue;
							}
							const int32 FirstX = FirstBit % FirstProvince->SizeX;
							const int32 FirstY = FirstBit / FirstProvince->SizeX;
							for (int32 SecondBit = 0;
								SecondBit < SecondProvince->SizeX * SecondProvince->SizeY;
								++SecondBit)
							{
								if (!ABTSM73BeamD1Preview::PreviewSupportProvinceWordContains(
									SecondCandidate->PersistentCellWords, SecondBit))
								{
									continue;
								}
								const int32 SecondX = SecondBit % SecondProvince->SizeX;
								const int32 SecondY = SecondBit / SecondProvince->SizeX;
								const int32 Manhattan = FMath::Abs(FirstX - SecondX)
									+ FMath::Abs(FirstY - SecondY);
								if (Manhattan < MinimumManhattan)
								{
									MinimumManhattan = Manhattan;
									FirstCenter = FVector(
										(FirstProvince->MinimumXUnit + FirstX) * 36.0,
										(FirstProvince->MinimumYUnit + FirstY) * 36.0, Z);
									SecondCenter = FVector(
										(SecondProvince->MinimumXUnit + SecondX) * 36.0,
										(SecondProvince->MinimumYUnit + SecondY) * 36.0, Z);
								}
							}
						}
						if (MinimumManhattan > 1 && MinimumManhattan != MAX_int32)
						{
							ABTSM73BeamD1Preview::AddSegmentInstance(
								SharedPairIntentPreview,
								FirstCenter, SecondCenter, 12.0);
						}
					}
				}
			}
			for (const ABTSM73BeamC3V3::FLocalPodiumHeightCandidateDiagnostic& Candidate
				: Plan.LocalPodiumHeightCandidates)
			{
				if (Candidate.bAccepted
					|| !Plan.SupportProvinces.IsValidIndex(Candidate.ProvinceId))
				{
					continue;
				}
				const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province =
					Plan.SupportProvinces[Candidate.ProvinceId];
				const double Z = Province.GroundBounds.Min.Z
					+ Candidate.CandidateTopCourse * 36.0;
				const FVector Center(
					Province.AnchorXUnit * 36.0,
					Province.AnchorYUnit * 36.0, Z);
				ABTSM73BeamD1Preview::AddBoxInstance(IronPreview, FBox(
					Center - FVector(7.0), Center + FVector(7.0)));
			}
			for (UHierarchicalInstancedStaticMeshComponent* Preview : RegionPreviews)
			{
				Preview->SetVisibility(Preview->GetInstanceCount() > 0, true);
			}
			CoreIntentPreview->SetVisibility(
				CoreIntentPreview->GetInstanceCount() > 0, true);
			SharedPairIntentPreview->SetVisibility(
				SharedPairIntentPreview->GetInstanceCount() > 0, true);
			IronPreview->SetVisibility(IronPreview->GetInstanceCount() > 0, true);
		}
		else if ((VisibilityMask
			& ABTSM73BeamD1Preview::DemandCoreCouplingVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			TSet<int32> DrawnChildIds;
			TSet<int32> DrawnMainIds;
			for (const ABTSM73BeamC3V3::FSemanticDemandCoreBindingDiagnostic& Binding
				: Plan.SemanticDemandCoreBindings)
			{
				FBox DemandPlate = Binding.TerminalLoadBounds.IsValid
					? Binding.TerminalLoadBounds : Binding.DemandBodyBounds;
				DemandPlate.Min.Z = DemandPlate.Max.Z - 8.0;
				ABTSM73BeamD1Preview::AddBoxInstance(GlassPreview, DemandPlate);
				FVector DemandAnchor = DemandPlate.GetCenter();
				DemandAnchor.Z = DemandPlate.Max.Z;
				if (!Plan.CoreCells.IsValidIndex(Binding.BoundTowerChildCoreCellId))
				{
					ABTSM73BeamD1Preview::AddSegmentInstance(
						IronPreview, DemandAnchor,
						DemandAnchor + FVector(0.0, 0.0, 144.0), 18.0);
					continue;
				}
				const ABTSM73BeamC3V3::FCoreCellPlan& Child =
					Plan.CoreCells[Binding.BoundTowerChildCoreCellId];
				if (!DrawnChildIds.Contains(Child.CoreCellId))
				{
					FBox ChildTopPlate = Child.LocalBounds;
					ChildTopPlate.Min.Z = ChildTopPlate.Max.Z - 12.0;
					ABTSM73BeamD1Preview::AddBoxInstance(
						TowerChildIntentPreview, ChildTopPlate);
					DrawnChildIds.Add(Child.CoreCellId);
				}
				FVector ChildAnchor = Child.LocalBounds.GetCenter();
				ChildAnchor.Z = Child.LocalBounds.Max.Z;
				ABTSM73BeamD1Preview::AddSegmentInstance(
					SharedPairIntentPreview, DemandAnchor, ChildAnchor, 8.0);
				if (!Plan.CoreCells.IsValidIndex(Binding.AssignedPodiumMainCoreCellId))
				{
					continue;
				}
				const ABTSM73BeamC3V3::FCoreCellPlan& Main =
					Plan.CoreCells[Binding.AssignedPodiumMainCoreCellId];
				if (!DrawnMainIds.Contains(Main.CoreCellId))
				{
					FBox MainTopPlate = Main.LocalBounds;
					MainTopPlate.Min.Z = MainTopPlate.Max.Z - 12.0;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, MainTopPlate);
					DrawnMainIds.Add(Main.CoreCellId);
				}
				FVector MainAnchor = Main.LocalBounds.GetCenter();
				MainAnchor.Z = Main.LocalBounds.Max.Z;
				ABTSM73BeamD1Preview::AddSegmentInstance(
					Binding.bDirectMainCoupling ? WoodPreview.Get() : IronPreview.Get(),
					ChildAnchor, MainAnchor, Binding.bDirectMainCoupling ? 8.0 : 16.0);
			}
			for (UHierarchicalInstancedStaticMeshComponent* Preview : {
				GlassPreview.Get(), WoodPreview.Get(), IronPreview.Get(),
				CoreIntentPreview.Get(), TowerChildIntentPreview.Get(),
				SharedPairIntentPreview.Get()})
			{
				Preview->SetVisibility(Preview->GetInstanceCount() > 0, true);
			}
		}
		else if ((VisibilityMask
			& ABTSM73BeamD1Preview::SupportProvinceMainVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			TArray<UHierarchicalInstancedStaticMeshComponent*> ProvincePreviews{
				WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()};
			TSet<int32> DrawnGroundCoreIds;
			for (const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province
				: Plan.SupportProvinces)
			{
				UHierarchicalInstancedStaticMeshComponent* ProvincePreview =
					ProvincePreviews[Province.ProvinceId % ProvincePreviews.Num()];
				const double GroundZ = Province.GroundBounds.Min.Z;
				const double ProposedTopZ = GroundZ
					+ Province.ProposedPodiumTopCourse * 36.0;
				for (int32 BitIndex = 0;
					BitIndex < Province.SizeX * Province.SizeY; ++BitIndex)
				{
					if (!ABTSM73BeamD1Preview::PreviewSupportProvinceWordContains(
						Province.GroundCellWords, BitIndex))
					{
						continue;
					}
					const int32 X = BitIndex % Province.SizeX;
					const int32 Y = BitIndex / Province.SizeX;
					const double CenterX = (Province.MinimumXUnit + X) * 36.0;
					const double CenterY = (Province.MinimumYUnit + Y) * 36.0;
					ABTSM73BeamD1Preview::AddBoxInstance(ProvincePreview, FBox(
						FVector(CenterX - 16.0, CenterY - 16.0, GroundZ),
						FVector(CenterX + 16.0, CenterY + 16.0, GroundZ + 4.0)));
					ABTSM73BeamD1Preview::AddBoxInstance(ProvincePreview, FBox(
						FVector(CenterX - 16.0, CenterY - 16.0, ProposedTopZ - 4.0),
						FVector(CenterX + 16.0, CenterY + 16.0, ProposedTopZ)));
				}
				if (!Plan.CoreCells.IsValidIndex(Province.BoundGroundCoreCellId))
				{
					continue;
				}
				const ABTSM73BeamC3V3::FCoreCellPlan& GroundCore =
					Plan.CoreCells[Province.BoundGroundCoreCellId];
				if (!DrawnGroundCoreIds.Contains(GroundCore.CoreCellId))
				{
					FBox GroundPlate = GroundCore.LocalBounds;
					GroundPlate.Max.Z = GroundPlate.Min.Z + 12.0;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, GroundPlate);
					FBox TopPlate = GroundCore.LocalBounds;
					TopPlate.Min.Z = TopPlate.Max.Z - 12.0;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, TopPlate);
					DrawnGroundCoreIds.Add(GroundCore.CoreCellId);
				}
				const FVector ProvinceAnchor(
					Province.AnchorXUnit * 36.0,
					Province.AnchorYUnit * 36.0,
					ProposedTopZ);
				FVector CoreAnchor = GroundCore.LocalBounds.GetCenter();
				CoreAnchor.Z = GroundCore.LocalBounds.Max.Z;
				ABTSM73BeamD1Preview::AddSegmentInstance(
					SharedPairIntentPreview, ProvinceAnchor, CoreAnchor, 8.0);
			}
			for (UHierarchicalInstancedStaticMeshComponent* ProvincePreview
				: ProvincePreviews)
			{
				ProvincePreview->SetVisibility(
					ProvincePreview->GetInstanceCount() > 0, true);
			}
			CoreIntentPreview->SetVisibility(
				CoreIntentPreview->GetInstanceCount() > 0, true);
			SharedPairIntentPreview->SetVisibility(
				SharedPairIntentPreview->GetInstanceCount() > 0, true);
		}
		else if ((VisibilityMask
			& ABTSM73BeamD1Preview::SupportProvinceVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			TArray<UHierarchicalInstancedStaticMeshComponent*> ProvincePreviews{
				WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()};
			for (const ABTSM73BeamC3V3::FSupportProvinceDiagnostic& Province
				: Plan.SupportProvinces)
			{
				UHierarchicalInstancedStaticMeshComponent* ProvincePreview =
					ProvincePreviews[Province.ProvinceId % ProvincePreviews.Num()];
				const double GroundZ = Province.GroundBounds.Min.Z;
				const double ProposedTopZ = GroundZ
					+ Province.ProposedPodiumTopCourse * 36.0;
				for (int32 BitIndex = 0;
					BitIndex < Province.SizeX * Province.SizeY; ++BitIndex)
				{
					if (!ABTSM73BeamD1Preview::PreviewSupportProvinceWordContains(
						Province.GroundCellWords, BitIndex))
					{
						continue;
					}
					const int32 X = BitIndex % Province.SizeX;
					const int32 Y = BitIndex / Province.SizeX;
					const double CenterX = (Province.MinimumXUnit + X) * 36.0;
					const double CenterY = (Province.MinimumYUnit + Y) * 36.0;
					ABTSM73BeamD1Preview::AddBoxInstance(ProvincePreview, FBox(
						FVector(CenterX - 16.0, CenterY - 16.0, GroundZ),
						FVector(CenterX + 16.0, CenterY + 16.0, GroundZ + 4.0)));
					ABTSM73BeamD1Preview::AddBoxInstance(ProvincePreview, FBox(
						FVector(CenterX - 16.0, CenterY - 16.0, ProposedTopZ - 4.0),
						FVector(CenterX + 16.0, CenterY + 16.0, ProposedTopZ)));
				}
				ABTSM73BeamD1Preview::AddBoxInstance(CoreIntentPreview, FBox(
					FVector(Province.GroundCentroid.X - 9.0,
						Province.GroundCentroid.Y - 9.0, GroundZ),
					FVector(Province.GroundCentroid.X + 9.0,
						Province.GroundCentroid.Y + 9.0, ProposedTopZ)));
				const FVector ProvinceAnchor(
					Province.GroundCentroid.X, Province.GroundCentroid.Y, ProposedTopZ);
				for (const int32 DemandId : Province.DemandIds)
				{
					if (Plan.SemanticTerminalDemands.IsValidIndex(DemandId))
					{
						ABTSM73BeamD1Preview::AddSegmentInstance(
							SharedPairIntentPreview, ProvinceAnchor,
							Plan.SemanticTerminalDemands[DemandId].BodyBounds.GetCenter(),
							8.0);
					}
				}
			}
			for (UHierarchicalInstancedStaticMeshComponent* ProvincePreview
				: ProvincePreviews)
			{
				ProvincePreview->SetVisibility(
					ProvincePreview->GetInstanceCount() > 0, true);
			}
			CoreIntentPreview->SetVisibility(
				CoreIntentPreview->GetInstanceCount() > 0, true);
			SharedPairIntentPreview->SetVisibility(
				SharedPairIntentPreview->GetInstanceCount() > 0, true);
		}
		else if ((VisibilityMask & ABTSM73BeamD1Preview::CoreMergeRegionVisibility) != 0)
		{
			for (const ABTSM73BeamC3V3::FCoreMergeRegionPlan& Region :
				StageResult.Skeleton.Plan.CoreMergeRegions)
			{
				for (const FBox& SourceBase : Region.GroundSourceBounds)
				{
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreMergeRegionPreview, SourceBase);
				}
			}
			CoreMergeRegionPreview->SetVisibility(
				CoreMergeRegionPreview->GetInstanceCount() > 0, true);
		}
		else if ((VisibilityMask & ABTSM73BeamD1Preview::CoreIntentVisibility) != 0)
		{
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
			{
				if (Core.HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::PodiumMain
					&& Core.RaisedPodiumMainReservationBounds.IsValid
					&& Core.RaisedPodiumMainTopCourseIndex
						> Core.BodyTopCourseIndex)
				{
					FBox GroundMainBounds = Core.LocalBounds;
					GroundMainBounds.Max.Z =
						Core.RaisedPodiumMainReservationBounds.Min.Z;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, GroundMainBounds);
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview,
						Core.RaisedPodiumMainReservationBounds);
				}
				else if (Core.HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
					&& Core.LocalPodiumHeightRegionId != INDEX_NONE
					&& Core.LocalPodiumTopCourseIndex > 0
					&& Plan.Components.IsValidIndex(Core.ComponentId))
				{
					const double SplitZ = Plan.Components[Core.ComponentId].GroundPlaneZCM
						+ Core.LocalPodiumTopCourseIndex * 36.0;
					FBox MainLegBounds = Core.LocalBounds;
					FBox ChildOnlyBounds = Core.LocalBounds;
					MainLegBounds.Max.Z = SplitZ;
					ChildOnlyBounds.Min.Z = SplitZ;
					ABTSM73BeamD1Preview::AddBoxInstance(
						CoreIntentPreview, MainLegBounds);
					ABTSM73BeamD1Preview::AddBoxInstance(
						TowerChildIntentPreview, ChildOnlyBounds);
				}
				else
				{
					UHierarchicalInstancedStaticMeshComponent* IntentPreview =
						Core.HierarchyRole
							== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
								? TowerChildIntentPreview.Get() : CoreIntentPreview.Get();
					ABTSM73BeamD1Preview::AddBoxInstance(
						IntentPreview, Core.LocalBounds);
				}
			}
			for (const ABTSM73BeamC3V3::FSharedCourseIntent& Intent :
				Plan.SharedCourseIntents)
			{
				const ABTSM73BeamC3V3::FCoreCellPlan* Negative = Plan.CoreCells.FindByPredicate(
					[&Intent](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
					{
						return Core.CoreCellId == Intent.NegativeCoreCellId;
					});
				const ABTSM73BeamC3V3::FCoreCellPlan* Positive = Plan.CoreCells.FindByPredicate(
					[&Intent](const ABTSM73BeamC3V3::FCoreCellPlan& Core)
					{
						return Core.CoreCellId == Intent.PositiveCoreCellId;
					});
				if (Negative == nullptr || Positive == nullptr || Intent.CourseIndices.IsEmpty())
				{
					continue;
				}
				const int32 AxisIndex = Intent.Axis == EABTSM73BeamAFrameAxis::X ? 0 : 1;
				const int32 CrossAxisIndex = AxisIndex == 0 ? 1 : 0;
				FVector Minimum = Negative->LocalBounds.GetCenter();
				FVector Maximum = Positive->LocalBounds.GetCenter();
				if (Minimum[AxisIndex] > Maximum[AxisIndex])
				{
					Swap(Minimum, Maximum);
				}
				const double GroundZ = Plan.Components.IsValidIndex(Negative->ComponentId)
					? Plan.Components[Negative->ComponentId].GroundPlaneZCM : 0.0;
				const double Z = GroundZ
					+ (Intent.CourseIndices[0] + 0.5) * 36.0;
				Minimum[CrossAxisIndex] -= 9.0;
				Maximum[CrossAxisIndex] = Minimum[CrossAxisIndex] + 18.0;
				Minimum.Z = Z - 9.0;
				Maximum.Z = Z + 9.0;
				ABTSM73BeamD1Preview::AddBoxInstance(
					SharedPairIntentPreview, FBox(Minimum, Maximum));
			}
			CoreIntentPreview->SetVisibility(CoreIntentPreview->GetInstanceCount() > 0, true);
			TowerChildIntentPreview->SetVisibility(
				TowerChildIntentPreview->GetInstanceCount() > 0, true);
			SharedPairIntentPreview->SetVisibility(
				SharedPairIntentPreview->GetInstanceCount() > 0, true);
		}
		else if ((VisibilityMask & (ABTSM73BeamD1Preview::CoreAndSharedVisibility
			| ABTSM73BeamD1Preview::CompositeCoreXVisibility
			| ABTSM73BeamD1Preview::CompositeCoreYVisibility
			| ABTSM73BeamD1Preview::Stage2CouplingOnlyVisibility
			| ABTSM73BeamD1Preview::Stage2ProvenanceVisibility
			| ABTSM73BeamD1Preview::Stage2CoreAndCouplingVisibility
			| ABTSM73BeamD1Preview::Stage2PerimeterCoreFacesVisibility
			| ABTSM73BeamD1Preview::Stage2FacadePartitionsVisibility
			| ABTSM73BeamD1Preview::Stage3ExteriorFramesVisibility
			| ABTSM73BeamD1Preview::Stage3ExteriorColumnsVisibility
			| ABTSM73BeamD1Preview::Stage3GroundSillVisibility
			| ABTSM73BeamD1Preview::Stage3GroundExteriorColumnsVisibility
			| ABTSM73BeamD1Preview::Stage3OverviewVisibility
			| ABTSM73BeamD1Preview::Stage4TopSurfaceIntentVisibility
			| ABTSM73BeamD1Preview::Stage4FloorTopFramesVisibility
			| ABTSM73BeamD1Preview::Stage4FacadeToTopVisibility
			| ABTSM73BeamD1Preview::Stage4FloorStyleInfillVisibility
			| ABTSM73BeamD1Preview::Stage4RoofCrownVisibility
			| ABTSM73BeamD1Preview::Stage4OverviewVisibility)) != 0)
		{
			const bool bXOnly =
				(VisibilityMask & ABTSM73BeamD1Preview::CompositeCoreXVisibility) != 0;
			const bool bYOnly =
				(VisibilityMask & ABTSM73BeamD1Preview::CompositeCoreYVisibility) != 0;
			const bool bCouplingOnly =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage2CouplingOnlyVisibility) != 0;
			const bool bProvenance =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage2ProvenanceVisibility) != 0;
			const bool bCoreAndCoupling =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage2CoreAndCouplingVisibility) != 0;
			const bool bPerimeterFaces =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage2PerimeterCoreFacesVisibility) != 0;
			const bool bFacadePartitions =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage2FacadePartitionsVisibility) != 0;
			const bool bExteriorFrames =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage3ExteriorFramesVisibility) != 0;
			const bool bExteriorColumns =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage3ExteriorColumnsVisibility) != 0;
			const bool bGroundSill =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage3GroundSillVisibility) != 0;
			const bool bGroundExteriorColumns =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage3GroundExteriorColumnsVisibility) != 0;
			const bool bStage3Overview =
				(VisibilityMask & ABTSM73BeamD1Preview::Stage3OverviewVisibility) != 0;
			const bool bStage4Intent =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4TopSurfaceIntentVisibility) != 0;
			const bool bStage4Frames =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4FloorTopFramesVisibility) != 0;
			const bool bStage4FacadeToTop =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4FacadeToTopVisibility) != 0;
			const bool bStage4FloorStyleInfill =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4FloorStyleInfillVisibility) != 0;
			const bool bStage4RoofCrown =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4RoofCrownVisibility) != 0;
			const bool bStage4Overview =
				(VisibilityMask
					& ABTSM73BeamD1Preview::Stage4OverviewVisibility) != 0;
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			if (bStage4Intent)
			{
				for (const ABTSM73BeamC3V3::FTopSurfaceIntentPlan& Intent
					: Plan.TopSurfaceIntents)
				{
					const bool bNormalX = Intent.FaceMask == ABTSM73BeamC3V3::NegativeX
						|| Intent.FaceMask == ABTSM73BeamC3V3::PositiveX;
					const int32 NormalAxis = bNormalX ? 0 : 1;
					const int32 TangentAxis = bNormalX ? 1 : 0;
					const double GroundZ = Plan.Components.IsValidIndex(Intent.ComponentId)
						? Plan.Components[Intent.ComponentId].GroundPlaneZCM : 0.0;
					const double FrameZ = GroundZ
						+ (Intent.ExteriorFrameCourseIndex + 0.5) * 36.0;
					FVector From = FVector::ZeroVector;
					From[NormalAxis] = Intent.FacadeCoordinateCM;
					From[TangentAxis] = (Intent.TangentMinimumCM
						+ Intent.TangentMaximumCM) * 0.5;
					From.Z = FrameZ;
					FVector To = From;
					UHierarchicalInstancedStaticMeshComponent* TargetPreview = nullptr;
					if (Intent.Intent
						== ABTSM73BeamC3V3::EFacadeDownwardIntent::GroundSill)
					{
						To.Z = GroundZ;
						TargetPreview = WoodPreview.Get();
					}
					else if (Intent.Intent
						== ABTSM73BeamC3V3::EFacadeDownwardIntent::TopSurface)
					{
						To.Z = GroundZ + Intent.TargetSurfaceCourseIndex * 36.0;
						const bool bSetback = Intent.TopSurfaceAuthority
							== ABTSM73BeamC3V3::ETopSurfaceAuthorityKind::ExposedSetbackTop;
						TargetPreview = bSetback ? IronPreview.Get() : GlassPreview.Get();
						FBox Surface = Intent.TargetSurfaceBounds;
						Surface.Min[TangentAxis] = Intent.TargetSupportTangentMinimumCM;
						Surface.Max[TangentAxis] = Intent.TargetSupportTangentMaximumCM;
						Surface.Min[NormalAxis] = FMath::Max(Surface.Min[NormalAxis],
							From[NormalAxis] - 18.0);
						Surface.Max[NormalAxis] = FMath::Min(Surface.Max[NormalAxis],
							From[NormalAxis] + 18.0);
						Surface.Min.Z = To.Z - 4.0;
						Surface.Max.Z = To.Z + 4.0;
						ABTSM73BeamD1Preview::AddBoxInstance(
							bSetback ? GlassPreview.Get() : IronPreview.Get(), Surface);
					}
					else if (Intent.Intent == ABTSM73BeamC3V3::
						EFacadeDownwardIntent::GroundedCoreAnchor)
					{
						const ABTSM73BeamC3V3::FCommonExteriorFramePlan* Frame =
							Plan.CommonExteriorFrames.FindByPredicate(
								[&Intent](const ABTSM73BeamC3V3::
									FCommonExteriorFramePlan& Candidate)
								{
									return Candidate.ExteriorFrameId
										== Intent.ExteriorFrameId;
								});
						const ABTSM73BeamC3V3::FFacadeHeightAnchorBand* Band =
							Frame != nullptr
							? Plan.FacadeHeightAnchorBands.FindByPredicate(
								[Frame](const ABTSM73BeamC3V3::
									FFacadeHeightAnchorBand& Candidate)
								{
									return Candidate.AnchorBandId == Frame->AnchorBandId;
								}) : nullptr;
						const ABTSM73BeamC3V3::FCoreCellPlan* Core = Band != nullptr
							? Plan.CoreCells.FindByPredicate(
								[Band](const ABTSM73BeamC3V3::FCoreCellPlan& Candidate)
								{
									return Candidate.CoreCellId == Band->OriginCoreCellId;
								}) : nullptr;
						if (Core != nullptr)
						{
							const bool bPositive = Intent.FaceMask
								== ABTSM73BeamC3V3::PositiveX
								|| Intent.FaceMask == ABTSM73BeamC3V3::PositiveY;
							To[NormalAxis] = bPositive
								? Core->LocalBounds.Max[NormalAxis]
								: Core->LocalBounds.Min[NormalAxis];
						}
						TargetPreview = GlassPreview.Get();
					}
					else
					{
						To.Z = FMath::Max(GroundZ, FrameZ - 72.0);
						TargetPreview = StonePreview.Get();
					}
					ABTSM73BeamD1Preview::AddSegmentInstance(
						TargetPreview, From, To, 14.0);
				}
				for (UHierarchicalInstancedStaticMeshComponent* Preview : {
					WoodPreview.Get(), IronPreview.Get(), GlassPreview.Get(),
					StonePreview.Get()})
				{
					Preview->SetVisibility(Preview->GetInstanceCount() > 0, true);
				}
			}
			for (int32 MemberIndex = 0; MemberIndex < Plan.Members.Num(); ++MemberIndex)
			{
				const ABTSM73BeamC3V3::FPlannedMember& Member = Plan.Members[MemberIndex];
				const bool bGroundSillLedgerMember = (bGroundSill || bStage3Overview)
					&& Plan.GroundSillSegments.ContainsByPredicate(
						[MemberIndex](
							const ABTSM73BeamC3V3::FGroundSillSegmentPlan& Segment)
						{
							return Segment.MemberIndex == MemberIndex;
						});
				const bool bExteriorFrameLedgerMember = (bExteriorFrames || bStage3Overview)
					&& Plan.CommonExteriorFrames.ContainsByPredicate(
						[MemberIndex](
							const ABTSM73BeamC3V3::FCommonExteriorFramePlan& Frame)
						{
							return Frame.MemberIndex == MemberIndex;
						});
				const ABTSM73BeamC3V3::FTopSurfaceFrameSegmentPlan* TopFrameSegment =
					(bStage4Frames || bStage4FacadeToTop)
					? Plan.TopSurfaceFrameSegments.FindByPredicate(
						[MemberIndex](
							const ABTSM73BeamC3V3::FTopSurfaceFrameSegmentPlan& Segment)
						{
							return Segment.MemberIndex == MemberIndex;
						}) : nullptr;
				const bool bDeferredStage4Junction = bStage4Frames
					&& Plan.TopSurfaceFrameDeferredJunctions.ContainsByPredicate(
						[MemberIndex](const ABTSM73BeamC3V3::
							FTopSurfaceFrameDeferredJunctionPlan& Junction)
						{
							return !Junction.bResolvedByFacadeToTop
								&& Junction.BlockingStage3ColumnMemberIndex == MemberIndex;
						});
				const bool bFacadeToTopSourceMember = bStage4FacadeToTop
					&& Plan.FacadeToTopConnections.ContainsByPredicate(
						[MemberIndex](const ABTSM73BeamC3V3::
							FFacadeToTopConnectionPlan& Connection)
						{
							return Connection.UpperExteriorFrameMemberIndex == MemberIndex
								|| Connection.TopSurfaceFrameMemberIndex == MemberIndex;
						});
				const bool bFacadeToTopClosureMember = bStage4FacadeToTop
					&& (Member.SkeletonKind
							== ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeToTopSeat
						|| Member.SkeletonKind
							== ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeToTopPost);
				const ABTSM73BeamC3V3::FFloorStyleInfillSpanPlan* FloorInfillSpan =
					bStage4FloorStyleInfill
					? Plan.FloorStyleInfillSpans.FindByPredicate(
						[MemberIndex](const ABTSM73BeamC3V3::
							FFloorStyleInfillSpanPlan& Span)
						{
							return Span.MemberIndex == MemberIndex;
						}) : nullptr;
				const bool bFloorInfillSupportMember = bStage4FloorStyleInfill
					&& Plan.FloorStyleInfillSpans.ContainsByPredicate(
						[MemberIndex](const ABTSM73BeamC3V3::
							FFloorStyleInfillSpanPlan& Span)
						{
							return Span.NegativeSupportMemberIndex == MemberIndex
								|| Span.PositiveSupportMemberIndex == MemberIndex;
						});
				const ABTSM73BeamC3V3::FRoofCrownCoursePlan* RoofCrownCourse =
					bStage4RoofCrown
					? Plan.RoofCrownCourses.FindByPredicate(
						[MemberIndex](const ABTSM73BeamC3V3::FRoofCrownCoursePlan& Roof)
						{
							return Roof.CarrierMemberIndex == MemberIndex
								|| Roof.SupportPostMemberIndices.Contains(MemberIndex)
								|| Roof.ClosureMemberIndices.Contains(MemberIndex);
						}) : nullptr;
				const bool bCoupling = Member.ProducedStage
					== EABTSM73BeamC3GenerationStage::CouplingCourses;
				const ABTSM73BeamD1Preview::EStage3OverviewBucket OverviewBucket =
					ABTSM73BeamD1Preview::Stage3OverviewBucket(
						Member.ProducedStage,
						bGroundSillLedgerMember || bExteriorFrameLedgerMember);
				bool bProvenanceParent = false;
				if (bProvenance)
				{
					for (const ABTSM73BeamC3V3::FPlannedMember& Candidate : Plan.Members)
					{
						bProvenanceParent |= Candidate.ProducedStage
							== EABTSM73BeamC3GenerationStage::CouplingCourses
							&& Candidate.ParentStage1MemberIndex == MemberIndex;
					}
				}
				if (Member.bSuppressedByStage4FacadeToTop
					|| bStage4Intent
					|| (bStage4Frames && TopFrameSegment == nullptr
						&& !bDeferredStage4Junction)
					|| (bStage4FacadeToTop && !bFacadeToTopSourceMember
						&& !bFacadeToTopClosureMember)
					|| (bStage4FloorStyleInfill && FloorInfillSpan == nullptr
						&& !bFloorInfillSupportMember)
					|| (bStage4RoofCrown && RoofCrownCourse == nullptr)
					|| (bExteriorFrames && Member.SkeletonKind
						!= ABTSM73BeamC3V3::ESkeletonMemberKind::FacadeCourse
						&& !bExteriorFrameLedgerMember)
					|| (bExteriorColumns && Member.SkeletonKind
						!= ABTSM73BeamC3V3::ESkeletonMemberKind::ExteriorPost)
					|| (bGroundSill && Member.SkeletonKind
						!= ABTSM73BeamC3V3::ESkeletonMemberKind::GroundSillCourse
						&& !bGroundSillLedgerMember)
					|| (bGroundExteriorColumns && Member.SkeletonKind
						!= ABTSM73BeamC3V3::ESkeletonMemberKind::GroundExteriorPost)
					|| (bStage3Overview && OverviewBucket
						== ABTSM73BeamD1Preview::EStage3OverviewBucket::None)
					|| (bCouplingOnly && !bCoupling)
					|| (bProvenance && !bCoupling && !bProvenanceParent)
					|| (bCoreAndCoupling && !bCoupling
						&& Member.ProducedStage
							!= EABTSM73BeamC3GenerationStage::CoreAndShared)
					|| bFacadePartitions
					|| (bPerimeterFaces && (bCoupling
						|| !Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId))))
				{
					continue;
				}
				if ((bXOnly || bYOnly)
					&& (Member.SkeletonKind
							!= ABTSM73BeamC3V3::ESkeletonMemberKind::CoreCourse
						|| (bXOnly && Member.Axis != EABTSM73BeamAFrameAxis::X)
						|| (bYOnly && Member.Axis != EABTSM73BeamAFrameAxis::Y)))
				{
					continue;
				}
				const FVector Center = (Member.LocalStart + Member.LocalEnd) * 0.5;
				FVector Dimensions(36.0, 36.0, 36.0);
				Dimensions[static_cast<int32>(Member.Axis)] =
					FVector::Distance(Member.LocalStart, Member.LocalEnd);
				UHierarchicalInstancedStaticMeshComponent* Preview = nullptr;
				const ABTSM73BeamC3V3::FCoreCellPlan* OriginCore =
					Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId)
						? &Plan.CoreCells[Member.OriginCoreCellId] : nullptr;
				const bool bChildOnlyCourse = OriginCore != nullptr
					&& OriginCore->HierarchyRole
						== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
					&& (OriginCore->LocalPodiumHeightRegionId == INDEX_NONE
						|| Member.CourseIndex
							>= OriginCore->LocalPodiumTopCourseIndex);
				if (bStage4Overview)
				{
					switch (Member.ProducedStage)
					{
					case EABTSM73BeamC3GenerationStage::CoreAndShared:
						Preview = WoodPreview.Get();
						break;
					case EABTSM73BeamC3GenerationStage::CouplingCourses:
						Preview = GlassPreview.Get();
						break;
					case EABTSM73BeamC3GenerationStage::CommonExteriorFrame:
						Preview = IronPreview.Get();
						break;
					case EABTSM73BeamC3GenerationStage::FloorInfillRoof:
						Preview = StonePreview.Get();
						break;
					default:
						continue;
					}
				}
				else if (bStage4RoofCrown)
				{
					Preview = RoofCrownCourse != nullptr
						&& RoofCrownCourse->SupportPostMemberIndices.Contains(MemberIndex)
						? IronPreview.Get()
						: RoofCrownCourse != nullptr
							&& RoofCrownCourse->CarrierMemberIndex == MemberIndex
							? GlassPreview.Get() : StonePreview.Get();
				}
				else if (bStage4FloorStyleInfill)
				{
					Preview = FloorInfillSpan == nullptr
						? GlassPreview.Get()
						: FloorInfillSpan->bStyleInfill
							? StonePreview.Get()
							: FloorInfillSpan->bReusesExistingMember
								? GlassPreview.Get() : IronPreview.Get();
				}
				else if (bStage4FacadeToTop)
				{
					Preview = bFacadeToTopClosureMember
						? (Member.Axis == EABTSM73BeamAFrameAxis::Z
							? StonePreview.Get() : IronPreview.Get())
						: GlassPreview.Get();
				}
				else if (bStage4Frames)
				{
					Preview = bDeferredStage4Junction
						? StonePreview.Get()
						: TopFrameSegment != nullptr
						&& TopFrameSegment->bReusesExistingMember
						? GlassPreview.Get() : IronPreview.Get();
				}
				else if (bStage3Overview)
				{
					switch (OverviewBucket)
					{
					case ABTSM73BeamD1Preview::EStage3OverviewBucket::Stage1CoreAndShared:
						Preview = WoodPreview.Get();
						break;
					case ABTSM73BeamD1Preview::EStage3OverviewBucket::Stage2Coupling:
						Preview = GlassPreview.Get();
						break;
					case ABTSM73BeamD1Preview::EStage3OverviewBucket::Stage3Exterior:
						Preview = IronPreview.Get();
						break;
					default:
						continue;
					}
				}
				else if (bPerimeterFaces || bFacadePartitions)
				{
					Preview = OriginCore != nullptr && OriginCore->PerimeterFaceMask != 0
						? IronPreview.Get() : GlassPreview.Get();
				}
				else if (bExteriorFrames || bGroundSill)
				{
					Preview = IronPreview.Get();
				}
				else if (bExteriorColumns || bGroundExteriorColumns)
				{
					Preview = StonePreview.Get();
				}
				else if (bCoupling)
				{
					Preview = IronPreview.Get();
					if (bProvenance)
					{
						ABTSM73BeamD1Preview::AddBoxInstance(
							CoreIntentPreview,
							FBox(Member.LocalStart - FVector(12.0),
								Member.LocalStart + FVector(12.0)));
						ABTSM73BeamD1Preview::AddBoxInstance(
							SharedPairIntentPreview,
							FBox(Member.LocalEnd - FVector(12.0),
								Member.LocalEnd + FVector(12.0)));
					}
				}
				else if (bProvenanceParent)
				{
					Preview = GlassPreview.Get();
				}
				else if (bXOnly || bYOnly)
				{
					Preview = bChildOnlyCourse
						? StonePreview.Get() : WoodPreview.Get();
				}
				else
				{
					Preview = Member.SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse
						? IronPreview.Get()
						: Member.SkeletonKind
							== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm
							? StonePreview.Get()
							: bChildOnlyCourse ? StonePreview.Get() : WoodPreview.Get();
				}
				Preview->AddInstance(FTransform(
					FQuat::Identity, Center, Dimensions / 100.0), false);
			}
			if (bPerimeterFaces)
			{
				for (const ABTSM73BeamC3V3::FCoreCellPlan& Core : Plan.CoreCells)
				{
					const double GroundZ = Plan.Components.IsValidIndex(Core.ComponentId)
						? Plan.Components[Core.ComponentId].GroundPlaneZCM : 0.0;
					for (const ABTSM73BeamC3V3::FPerimeterFaceExposure& Exposure
						: Core.PerimeterFaceExposures)
					{
						const uint8 FaceMask = Exposure.FaceMask;
						const bool bXAxis = FaceMask == ABTSM73BeamC3V3::NegativeX
							|| FaceMask == ABTSM73BeamC3V3::PositiveX;
						const bool bPositive = FaceMask == ABTSM73BeamC3V3::PositiveX
							|| FaceMask == ABTSM73BeamC3V3::PositiveY;
						const double Z = GroundZ + (Exposure.CourseIndex + 0.5) * 36.0;
						FVector Minimum = FVector::ZeroVector;
						FVector Maximum = Minimum;
						const int32 AxisIndex = bXAxis ? 0 : 1;
						const int32 TangentIndex = bXAxis ? 1 : 0;
						Minimum[AxisIndex] = Maximum[AxisIndex] = Exposure.FacadeCoordinateCM
							+ (bPositive ? 6.0 : -6.0);
						Minimum[TangentIndex] = Exposure.TangentMinimumCM;
						Maximum[TangentIndex] = Exposure.TangentMaximumCM;
						Minimum[AxisIndex] -= 6.0;
						Maximum[AxisIndex] += 6.0;
						Minimum.Z = Z - 12.0;
						Maximum.Z = Z + 12.0;
						ABTSM73BeamD1Preview::AddBoxInstance(
							SharedPairIntentPreview, FBox(Minimum, Maximum));
					}
				}
				SharedPairIntentPreview->SetVisibility(
					SharedPairIntentPreview->GetInstanceCount() > 0, true);
			}
			if (bFacadePartitions)
			{
				for (const ABTSM73BeamC3V3::FFacadePartitionPlan& Partition
					: Plan.FacadePartitions)
				{
					const bool bXAxis = Partition.FaceMask == ABTSM73BeamC3V3::NegativeX
						|| Partition.FaceMask == ABTSM73BeamC3V3::PositiveX;
					const bool bPositive = Partition.FaceMask == ABTSM73BeamC3V3::PositiveX
						|| Partition.FaceMask == ABTSM73BeamC3V3::PositiveY;
					const double GroundZ = Plan.Components.IsValidIndex(Partition.ComponentId)
						? Plan.Components[Partition.ComponentId].GroundPlaneZCM : 0.0;
					const int32 AxisIndex = bXAxis ? 0 : 1;
					const int32 TangentIndex = bXAxis ? 1 : 0;
					UHierarchicalInstancedStaticMeshComponent* PartitionPreview =
						!Partition.AnchorBandIds.IsEmpty()
							? IronPreview.Get()
							: (!Partition.PerimeterCoreCellIds.IsEmpty()
								? StonePreview.Get() : GlassPreview.Get());
					for (const ABTSM73BeamC3V3::FFacadePartitionCourseSpan& Span
						: Partition.CourseSpans)
					{
						FVector Minimum = FVector::ZeroVector;
						FVector Maximum = FVector::ZeroVector;
						Minimum[AxisIndex] = Maximum[AxisIndex] = Partition.FacadeCoordinateCM
							+ (bPositive ? 10.0 : -10.0);
						Minimum[AxisIndex] -= 4.0;
						Maximum[AxisIndex] += 4.0;
						Minimum[TangentIndex] = Span.TangentMinimumCM;
						Maximum[TangentIndex] = Span.TangentMaximumCM;
						Minimum.Z = GroundZ + Span.CourseIndex * 36.0;
						Maximum.Z = Minimum.Z + 36.0;
						ABTSM73BeamD1Preview::AddBoxInstance(
							PartitionPreview, FBox(Minimum, Maximum));
					}
				}
				for (const ABTSM73BeamC3V3::FFacadeHeightAnchorBand& Band
					: Plan.FacadeHeightAnchorBands)
				{
					if (!Plan.Members.IsValidIndex(Band.LowerMemberIndex)
						|| !Plan.Members.IsValidIndex(Band.UpperMemberIndex))
					{
						continue;
					}
					const ABTSM73BeamC3V3::FPlannedMember& Lower =
						Plan.Members[Band.LowerMemberIndex];
					const ABTSM73BeamC3V3::FPlannedMember& Upper =
						Plan.Members[Band.UpperMemberIndex];
					const int32 AxisIndex = static_cast<int32>(Lower.Axis);
					const bool bPositive = (Band.FaceMask
						& (ABTSM73BeamC3V3::PositiveX | ABTSM73BeamC3V3::PositiveY)) != 0;
					FVector LowerEndpoint = bPositive ? Lower.LocalEnd : Lower.LocalStart;
					FVector UpperEndpoint = bPositive ? Upper.LocalEnd : Upper.LocalStart;
					LowerEndpoint[AxisIndex] = Band.FacadeCoordinateCM;
					UpperEndpoint[AxisIndex] = Band.FacadeCoordinateCM;
					ABTSM73BeamD1Preview::AddSegmentInstance(
						SharedPairIntentPreview, LowerEndpoint, UpperEndpoint, 16.0);
				}
				for (UHierarchicalInstancedStaticMeshComponent* Preview : {
					GlassPreview.Get(), StonePreview.Get(), IronPreview.Get(),
					SharedPairIntentPreview.Get()})
				{
					Preview->SetVisibility(Preview->GetInstanceCount() > 0, true);
				}
			}
			if (bProvenance)
			{
				CoreIntentPreview->SetVisibility(
					CoreIntentPreview->GetInstanceCount() > 0, true);
				SharedPairIntentPreview->SetVisibility(
					SharedPairIntentPreview->GetInstanceCount() > 0, true);
			}
		}
		const bool bShowsMembers = bStage2 || bStage3 || bStage4
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger
			|| EffectiveLayer == EABTSM73BeamC3Stage1DiagnosticLayer::LocalPodiumHeightPlan;
		if (!bShowsMembers
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::LocalPodiumHeightPlan)
		{
			for (UHierarchicalInstancedStaticMeshComponent* Preview : {
				WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
			{
				Preview->SetVisibility(false, true);
			}
		}

		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-Beam-D1][StagePreviewGenerated]")
			TEXT(" Actor=%s Stage=%d Layer=%d HideSupportDemandVolumes=%d Profile=%s Tier=%d")
			TEXT(" Volumes=%d SupportNodes=%d LoadBranches=%d MultiBranchBodies=%d UnrepresentedBranches=%d SemanticDemands=%d MergeLedger=%d SupportDemandHash=%lld")
			TEXT(" DemandCoreRows=%d UnmappedDemands=%d AmbiguousDemands=%d ChildOutsideBody=%d ChildWithoutDirectMain=%d ReusedChildren=%d OrphanChildren=%d DemandCoreHash=%lld")
			TEXT(" Provinces=%d ProvinceCells=%d ProvinceBoundaries=%d ProvinceHash=%lld BoundProvinces=%d ProvinceGroundCores=%d ProvinceMainBindingHash=%lld")
			TEXT(" LocalPodiumCandidates=%d RejectedLocalPodiumCandidates=%d LocalPodiumRegions=%d RaisedLocalPodiumRegions=%d AppliedLocalPodiumRegions=%d LocalPodiumLegMembers=%d RaisedMainReservations=%d RaisedMainMembers=%d LocalPodiumHash=%lld")
			TEXT(" Cores=%d Main=%d Children=%d HighRegions=%d BoundHigh=%d PairIntents=%d Members=%d")
			TEXT(" CouplingCourses=%d CouplingFaces=%u CouplingOtherCoreViolations=%d CouplingBandEndpointViolations=%d FacadePartitions=%d PartitionPerimeter=%d PartitionAnchored=%d DeferredPartitions=%d HeightAnchorBands=%d PartitionBindingViolations=%d PerimeterCores=%d PerimeterFaces=%d PerimeterExposureSpans=%d")
			TEXT(" ExteriorFrames=%d ExteriorFramesEmitted=%d ExteriorFramesReused=%d AnchorWithoutFrame=%d FrameWithoutDownward=%d CrossPartitionColumns=%d GroundSillLoops=%d GroundSillSegments=%d GroundSillEmitted=%d GroundSillReused=%d GroundSillConflictOmissions=%d ExteriorColumns=%d GroundExteriorColumns=%d ExteriorColumnSegments=%d GroundExteriorColumnSegments=%d GroundColumnConflictOmissions=%d Stage3ParentViolations=%d Stage3ClampViolations=%d Stage3ColumnFrameViolations=%d Stage3FacadeFitViolations=%d Stage3Hash=%lld")
			TEXT(" Stage4Intents=%d Stage4Ground=%d Stage4Top=%d Stage4TopFrames=%d Stage4TopFramesEmitted=%d Stage4TopFramesReused=%d Stage4TopFrameBindingViolations=%d Stage4TopFrameConflicts=%d Stage4DeferredFacadeJunctions=%d Stage4IntentHash=%lld Stage4TopFrameHash=%lld Stage4TimingMs=%.3f/%.3f")
			TEXT(" Stage4FloorPairs=%d Stage4Floor=%d Stage4Style=%d Stage4FloorReused=%d Stage4FloorDeferred=%d Stage4FloorHash=%lld")
			TEXT(" Stage4RoofVolumes=%d Stage4RoofBands=%d Stage4RoofCourses=%d Stage4RoofMembers=%d Stage4RoofPosts=%d Stage4RoofReused=%d Stage4RoofDeferred=%d Stage4RoofOccluded=%d Stage4RoofUnsupported=%d Stage4RoofBindingViolations=%d Stage4RoofConflicts=%d Stage4RoofHash=%lld Stage4FloorRoofTimingMs=%.3f/%.3f")
			TEXT(" EnvelopeHash=%lld Stage1Hash=%lld StaticDAG=%d Physical=NotEvaluated"),
			*GetName(), static_cast<int32>(GenerationStopStage),
			bStage4 ? static_cast<int32>(Stage4DiagnosticLayer)
				: bStage3 ? static_cast<int32>(Stage3DiagnosticLayer)
				: bStage2 ? static_cast<int32>(Stage2DiagnosticLayer)
					: static_cast<int32>(EffectiveLayer),
			bHideSemanticSupportDemandVolumes ? 1 : 0,
			*LastSummary.GameplayProfileId.ToString(), LastSummary.DifficultyTier,
			StageResult.Silhouette.Volumes.Num(),
			StageResult.Skeleton.Plan.Summary.SemanticSupportNodeCount,
			StageResult.Skeleton.Plan.Summary.SemanticTerminalLoadBranchCount,
			StageResult.Skeleton.Plan.Summary.MultiBranchTerminalBodyCount,
			StageResult.Skeleton.Plan.Summary
				.UnrepresentedSemanticTerminalLoadBranchCount,
			StageResult.Skeleton.Plan.Summary.SemanticTerminalDemandCount,
			StageResult.Skeleton.Plan.Summary.SemanticSupportLedgerCount,
			StageResult.Skeleton.Plan.Summary.SemanticSupportDemandHash,
			StageResult.Skeleton.Plan.Summary.SemanticDemandCoreBindingCount,
			StageResult.Skeleton.Plan.Summary.UnmappedSemanticDemandCount,
			StageResult.Skeleton.Plan.Summary.AmbiguousSemanticDemandCount,
			StageResult.Skeleton.Plan.Summary.SemanticDemandChildOutsideBodyCount,
			StageResult.Skeleton.Plan.Summary.SemanticDemandChildWithoutDirectMainCouplingCount,
			StageResult.Skeleton.Plan.Summary.ReusedTowerChildBindingCount,
			StageResult.Skeleton.Plan.Summary.UnreferencedTowerChildCount,
			StageResult.Skeleton.Plan.Summary.SemanticDemandCoreBindingHash,
			StageResult.Skeleton.Plan.Summary.SupportProvinceCount,
			StageResult.Skeleton.Plan.Summary.SupportProvinceGroundCellCount,
			StageResult.Skeleton.Plan.Summary.SupportProvinceBoundaryCount,
			StageResult.Skeleton.Plan.Summary.SupportProvinceHash,
			StageResult.Skeleton.Plan.Summary.BoundSupportProvinceCount,
			StageResult.Skeleton.Plan.Summary.DistinctProvinceGroundCoreCount,
			StageResult.Skeleton.Plan.Summary.SupportProvinceMainBindingHash,
			StageResult.Skeleton.Plan.Summary.LocalPodiumHeightCandidateCount,
			StageResult.Skeleton.Plan.Summary
				.RejectedLocalPodiumHeightCandidateCount,
			StageResult.Skeleton.Plan.Summary.LocalPodiumHeightRegionCount,
			StageResult.Skeleton.Plan.Summary.RaisedLocalPodiumHeightRegionCount,
			StageResult.Skeleton.Plan.Summary.AppliedLocalPodiumHeightRegionCount,
			StageResult.Skeleton.Plan.Summary.LocalPodiumLegMemberCount,
			StageResult.Skeleton.Plan.Summary.RaisedPodiumMainReservationCount,
			StageResult.Skeleton.Plan.Summary.RaisedPodiumMainMemberCount,
			StageResult.Skeleton.Plan.Summary.LocalPodiumHeightPlanHash,
			StageResult.Skeleton.Plan.CoreCells.Num(),
			StageResult.Skeleton.Plan.Summary.PodiumMainCoreCellCount,
			StageResult.Skeleton.Plan.Summary.TowerChildCoreCellCount,
			StageResult.Skeleton.Plan.Summary.HighProjectionRegionCount,
			StageResult.Skeleton.Plan.Summary.BoundHighProjectionRegionCount,
			StageResult.Skeleton.Plan.SharedCourseIntents.Num(),
			StageResult.Skeleton.Plan.Members.Num(),
			StageResult.Skeleton.Plan.Summary.CouplingCourseCount,
			StageResult.Skeleton.Plan.Summary.CouplingFaceMask,
			StageResult.Skeleton.Plan.Summary.CouplingOtherCoreViolationCount,
			StageResult.Skeleton.Plan.Summary.CouplingBandEndpointViolationCount,
			StageResult.Skeleton.Plan.Summary.FacadePartitionCount,
			StageResult.Skeleton.Plan.Summary.FacadePartitionWithPerimeterCoreCount,
			StageResult.Skeleton.Plan.Summary.FacadePartitionWithHeightAnchorCount,
			StageResult.Skeleton.Plan.Summary.DeferredFacadePartitionCount,
			StageResult.Skeleton.Plan.Summary.FacadeHeightAnchorBandCount,
			StageResult.Skeleton.Plan.Summary.FacadePartitionBindingViolationCount,
			StageResult.Skeleton.Plan.Summary.PerimeterCoreCount,
			StageResult.Skeleton.Plan.Summary.PerimeterCoreFaceCount,
			StageResult.Skeleton.Plan.Summary.PerimeterFaceExposureSpanCount,
			StageResult.Skeleton.Plan.Summary.CommonExteriorFrameCount,
			StageResult.Skeleton.Plan.Summary.EmittedExteriorFrameCount,
			StageResult.Skeleton.Plan.Summary.ReusedExteriorFrameCount,
			StageResult.Skeleton.Plan.Summary.Stage3AnchorBandWithoutFrameCount,
			StageResult.Skeleton.Plan.Summary
				.Stage3FrameDownwardConnectionViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage3CrossPartitionColumnCount,
			StageResult.Skeleton.Plan.Summary.GroundSillLoopCount,
			StageResult.Skeleton.Plan.Summary.GroundSillSegmentCount,
			StageResult.Skeleton.Plan.Summary.EmittedGroundSillSegmentCount,
			StageResult.Skeleton.Plan.Summary.ReusedGroundSillSegmentCount,
			StageResult.Skeleton.Plan.Summary.GroundSillConflictOmissionCount,
			StageResult.Skeleton.Plan.Summary.ExteriorColumnCount,
			StageResult.Skeleton.Plan.Summary.GroundExteriorColumnCount,
			StageResult.Skeleton.Plan.Summary.ExteriorColumnSegmentCount,
			StageResult.Skeleton.Plan.Summary.GroundExteriorColumnSegmentCount,
			StageResult.Skeleton.Plan.Summary
				.GroundExteriorColumnConflictOmissionCount,
			StageResult.Skeleton.Plan.Summary.Stage3ParentViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage3ClampViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage3ColumnFrameViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage3FacadeFitViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage3PlanHash,
			StageResult.Skeleton.Plan.Summary.Stage4TopSurfaceIntentCount,
			StageResult.Skeleton.Plan.Summary.Stage4GroundSillIntentCount,
			StageResult.Skeleton.Plan.Summary.Stage4ResolvedTopSurfaceIntentCount,
			StageResult.Skeleton.Plan.Summary.Stage4TopFrameSegmentCount,
			StageResult.Skeleton.Plan.Summary.Stage4EmittedTopFrameSegmentCount,
			StageResult.Skeleton.Plan.Summary.Stage4ReusedTopFrameSegmentCount,
			StageResult.Skeleton.Plan.Summary.Stage4TopFrameBindingViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage4TopFrameConflictCount,
			StageResult.Skeleton.Plan.Summary
				.Stage4DeferredFacadeColumnJunctionCount,
			StageResult.Skeleton.Plan.Summary.Stage4IntentHash,
			StageResult.Skeleton.Plan.Summary.Stage4TopFrameHash,
			StageResult.Skeleton.Plan.Summary.Stage4IntentMilliseconds,
			StageResult.Skeleton.Plan.Summary.Stage4TopFrameMilliseconds,
			StageResult.Skeleton.Plan.Summary.Stage4FloorSupportPairCount,
			StageResult.Skeleton.Plan.Summary.Stage4FloorSpanCount,
			StageResult.Skeleton.Plan.Summary.Stage4StyleInfillSpanCount,
			StageResult.Skeleton.Plan.Summary.Stage4ReusedFloorSpanCount,
			StageResult.Skeleton.Plan.Summary.Stage4DeferredFloorSpanCount,
			StageResult.Skeleton.Plan.Summary.Stage4FloorStyleInfillHash,
			StageResult.Skeleton.Plan.Summary.Stage4RoofCrownVolumeCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofBandCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofCourseCount,
			StageResult.Skeleton.Plan.Summary.Stage4EmittedRoofMemberCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofPostMemberCount,
			StageResult.Skeleton.Plan.Summary.Stage4ReusedRoofCarrierCount,
			StageResult.Skeleton.Plan.Summary.Stage4DeferredRoofCandidateCount,
			StageResult.Skeleton.Plan.Summary.Stage4OccludedRoofCourseCount,
			StageResult.Skeleton.Plan.Summary.Stage4UnsupportedRoofMemberCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofBindingViolationCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofConflictCount,
			StageResult.Skeleton.Plan.Summary.Stage4RoofCrownHash,
			StageResult.Skeleton.Plan.Summary.Stage4FloorStyleInfillMilliseconds,
			StageResult.Skeleton.Plan.Summary.Stage4RoofCrownMilliseconds,
			LastSummary.SkeletonFirstEnvelopeHash,
			LastSummary.SkeletonFirstFinalGeometryHash,
			LastSummary.bStageStaticDAGEvaluated ? 1 : 0);
		return;
	}

	FABTSM73BeamD1GenerationResult Result;
	if (!Compiler.Generate(Settings, Result, Error))
	{
		LastSummary = Result.Summary;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-D1][PreviewRejected] Actor=%s Reason=%s"),
			*GetName(), *Error);
		return;
	}
	CompiledBricks = MoveTemp(Result.Bricks);
	LastSummary = Result.Summary;
	if (bShowEditorPreview)
	{
		for (const FABTSM73BeamD1BrickBinding& Brick : CompiledBricks)
		{
			if (UHierarchicalInstancedStaticMeshComponent* Preview =
				GetPreview(Brick.BrickSpec.Material))
			{
				FTransform InstanceTransform = Brick.LocalTransform;
				InstanceTransform.SetScale3D(
					Brick.BrickSpec.DimensionsCM / 100.0f);
				Preview->AddInstance(InstanceTransform, false);
			}
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-D1][PreviewGenerated]")
		TEXT(" Actor=%s Profile=%s Tier=%d Members=%d Bricks=%d")
		TEXT(" Target=%d-%d Attempt=%d Volumes=%d Box=%d Prism=%d Pyramid=%d RoofBricks=%d Motifs=%d Spans=%d Certified=%d")
		TEXT(" ClosurePass=%d AddedPosts=%d ContactMismatch=%d SupportViolations=%d Advisory=%d")
		TEXT(" Stations=%d/%d AxisDensity=%.3f ClosureRatio=%.3f Quality=%d")
		TEXT(" Wood=%d Stone=%d Iron=%d Glass=%d Weak=%d Device=%d Hash=%lld"),
		*GetName(), *LastSummary.GameplayProfileId.ToString(),
		LastSummary.DifficultyTier, LastSummary.MemberCount,
		LastSummary.BrickCount, LastSummary.TargetMinimumBrickCount,
		LastSummary.TargetMaximumBrickCount,
		LastSummary.VisualCandidateAttempt,
		LastSummary.SemanticVolumeCount,
		LastSummary.SemanticBoxCount,
		LastSummary.SemanticPrismCount,
		LastSummary.SemanticPyramidCount,
		LastSummary.RoofCourseBrickCount,
		LastSummary.DistinctMotifCount,
		LastSummary.SupportedSpanCount,
		LastSummary.bVisualComplexityCertified ? 1 : 0,
		LastSummary.StructuralClosurePassCount,
		LastSummary.AddedStructuralSupportPostCount,
		LastSummary.RealContactMismatchCount,
		LastSummary.RemainingSupportViolationCount,
		LastSummary.SupportResultantAdvisoryCount,
		LastSummary.XColumnStationCount,
		LastSummary.YColumnStationCount,
		LastSummary.AxisStationDensityRatio,
		LastSummary.StructuralClosurePostRatio,
		LastSummary.bAssemblyQualityCertified ? 1 : 0,
		LastSummary.WoodBrickCount,
		LastSummary.StoneBrickCount, LastSummary.IronBrickCount,
		LastSummary.GlassBrickCount, LastSummary.WeaknessCandidateCount,
		LastSummary.DeviceRoleCount, LastSummary.BrickGeometryHash);
}

bool AABTSM73BeamD1PreviewActor::InitializeRuntimeBuilding(
	AABTSM7BuildingMaterialSystem* MaterialSystem)
{
	if (MaterialSystem == nullptr || !RuntimeModules.IsEmpty())
	{
		return false;
	}
	if (GenerationStopStage != EABTSM73BeamC3GenerationStage::StaticDAG)
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-D1][StagePreviewRuntimeBlocked]")
			TEXT(" Actor=%s Stage=%d Physical=NotEvaluated"),
			*GetName(), static_cast<int32>(GenerationStopStage));
		return false;
	}
	if (!LastSummary.bAccepted || CompiledBricks.IsEmpty())
	{
		RegeneratePreview();
	}
	if (!LastSummary.bAccepted)
	{
		return false;
	}
	for (const FABTSM73BeamD1BrickBinding& Brick : CompiledBricks)
	{
		const FTransform WorldTransform =
			Brick.LocalTransform * GetActorTransform();
		AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
			Brick.BrickSpec, WorldTransform);
		if (Module == nullptr)
		{
			for (const TWeakObjectPtr<AABTSM7BuildingModule>& Spawned : RuntimeModules)
			{
				if (Spawned.IsValid()) Spawned->Destroy();
			}
			RuntimeModules.Reset();
			return false;
		}
		RuntimeModules.Add(Module);
	}
	ClearPreview();
	return RuntimeModules.Num() == CompiledBricks.Num();
}

int32 AABTSM73BeamD1PreviewActor::GetRuntimeModuleCountForValidation() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Module : RuntimeModules)
	{
		Count += Module.IsValid() ? 1 : 0;
	}
	return Count;
}
