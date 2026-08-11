// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamD1PreviewActor.h"

#include "ABTSRuntime.h"
#include "ABTSM73BeamD1BrickCompiler.h"
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

	enum EDiagnosticVisibility : uint16
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
		DemandCoreCouplingVisibility = 1 << 11
	};

	uint16 DiagnosticVisibilityMask(
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
	const uint16 WFCMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::WFCSemanticEnvelope);
	const uint16 IntentMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CorePlacementIntent);
	const uint16 MembersMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses);
	const uint16 MergeMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CoreMergeRegions);
	const uint16 XMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes);
	const uint16 YMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes);
	const uint16 SupportDemandMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SemanticSupportDemandDAG);
	const uint16 SupportProvinceMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition);
	const uint16 SupportProvinceMainMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment);
	const uint16 DemandCoreCouplingMask = DiagnosticVisibilityMask(
		EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger);
	TestEqual(TEXT("WFC layer contains only envelope and protected void"),
		WFCMask, static_cast<uint16>(SemanticEnvelopeVisibility | ProtectedVoidVisibility));
	TestEqual(TEXT("Intent layer contains only core and pairing intent"),
		IntentMask, static_cast<uint16>(CoreIntentVisibility | PairIntentVisibility));
	TestEqual(TEXT("Member layer contains only actual core/shared members"),
		MembersMask, static_cast<uint16>(CoreAndSharedVisibility));
	TestEqual(TEXT("Merge layer contains only derived core merge regions"),
		MergeMask, static_cast<uint16>(CoreMergeRegionVisibility));
	TestEqual(TEXT("X lane layer contains only actual X core lanes"),
		XMask, static_cast<uint16>(CompositeCoreXVisibility));
	TestEqual(TEXT("Y lane layer contains only actual Y core lanes"),
		YMask, static_cast<uint16>(CompositeCoreYVisibility));
	TestEqual(TEXT("Support-demand layer contains only the semantic support graph"),
		SupportDemandMask,
		static_cast<uint16>(SemanticSupportDemandVisibility));
	TestEqual(TEXT("Support-province layer contains only the province partition"),
		SupportProvinceMask,
		static_cast<uint16>(SupportProvinceVisibility));
	TestEqual(TEXT("Province-main layer contains only the assignment plan"),
		SupportProvinceMainMask,
		static_cast<uint16>(SupportProvinceMainVisibility));
	TestEqual(TEXT("Demand-core layer contains only the correspondence ledger"),
		DemandCoreCouplingMask,
		static_cast<uint16>(DemandCoreCouplingVisibility));
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
				static_cast<int32>(Stage1DiagnosticLayer), *Error);
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
		const uint16 VisibilityMask =
			ABTSM73BeamD1Preview::DiagnosticVisibilityMask(EffectiveLayer);
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
				UHierarchicalInstancedStaticMeshComponent* IntentPreview =
					Core.HierarchyRole == ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
						? TowerChildIntentPreview.Get() : CoreIntentPreview.Get();
				ABTSM73BeamD1Preview::AddBoxInstance(IntentPreview, Core.LocalBounds);
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
			| ABTSM73BeamD1Preview::CompositeCoreYVisibility)) != 0)
		{
			const bool bXOnly =
				(VisibilityMask & ABTSM73BeamD1Preview::CompositeCoreXVisibility) != 0;
			const bool bYOnly =
				(VisibilityMask & ABTSM73BeamD1Preview::CompositeCoreYVisibility) != 0;
			const ABTSM73BeamC3V3::FPlan& Plan = StageResult.Skeleton.Plan;
			for (const ABTSM73BeamC3V3::FPlannedMember& Member :
				Plan.Members)
			{
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
				if (bXOnly || bYOnly)
				{
					const ABTSM73BeamC3V3::FCoreCellPlan* OriginCore =
						Plan.CoreCells.IsValidIndex(Member.OriginCoreCellId)
							? &Plan.CoreCells[Member.OriginCoreCellId] : nullptr;
					Preview = OriginCore != nullptr
						&& OriginCore->HierarchyRole
							== ABTSM73BeamC3V3::ECoreHierarchyRole::TowerChild
						? StonePreview.Get() : WoodPreview.Get();
				}
				else
				{
					Preview = Member.SkeletonKind
						== ABTSM73BeamC3V3::ESkeletonMemberKind::SharedCourse
						? IronPreview.Get()
						: Member.SkeletonKind
							== ABTSM73BeamC3V3::ESkeletonMemberKind::BridgeDiaphragm
							? StonePreview.Get() : WoodPreview.Get();
				}
				Preview->AddInstance(FTransform(
					FQuat::Identity, Center, Dimensions / 100.0), false);
			}
		}
		if (EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CoreAndSharedCourses
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreXLanes
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::CompositeCoreYLanes
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvincePartition
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::SupportProvinceMainAssignment
			&& EffectiveLayer != EABTSM73BeamC3Stage1DiagnosticLayer::DemandCoreCouplingLedger)
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
			TEXT(" Cores=%d Main=%d Children=%d HighRegions=%d BoundHigh=%d PairIntents=%d Members=%d")
			TEXT(" EnvelopeHash=%lld Stage1Hash=%lld StaticDAG=%d Physical=NotEvaluated"),
			*GetName(), static_cast<int32>(GenerationStopStage),
			static_cast<int32>(EffectiveLayer),
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
			StageResult.Skeleton.Plan.CoreCells.Num(),
			StageResult.Skeleton.Plan.Summary.PodiumMainCoreCellCount,
			StageResult.Skeleton.Plan.Summary.TowerChildCoreCellCount,
			StageResult.Skeleton.Plan.Summary.HighProjectionRegionCount,
			StageResult.Skeleton.Plan.Summary.BoundHighProjectionRegionCount,
			StageResult.Skeleton.Plan.SharedCourseIntents.Num(),
			StageResult.Skeleton.Plan.Members.Num(),
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
