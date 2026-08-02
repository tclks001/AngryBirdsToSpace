// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM51WorldActors.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Player/ABTSM51PlayerController.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void ConfigureInteractionMesh(UStaticMeshComponent& Mesh)
	{
		Mesh.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh.SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh.SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	void ConfigureM51VisualOnlyMesh(UStaticMeshComponent& Mesh)
	{
		Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh.SetGenerateOverlapEvents(false);
	}

	void SetSegmentBetween(
		UStaticMeshComponent& Mesh,
		const FVector& Start,
		const FVector& End,
		const float ThicknessCM,
		const FABTSSlingshotVisualSlot& VisualSlot)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length <= SMALL_NUMBER) return;
		const FVector Direction = Delta / Length;
		const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
		Mesh.SetWorldTransform(ABTSMakeSlingshotVisualTransform(
			Mesh.GetStaticMesh(),
			(Start + End) * 0.5f,
			Rotation,
			FVector(ThicknessCM, ThicknessCM, Length),
			VisualSlot,
			EABTSSlingshotVisualAnchor::BoundsCenter));
	}

	/** Each visible cord is assigned to the closest pouch side, so a rotated pouch cannot form an X. */
	void MatchCordEndpointsToPouchSides(
		const FVector& StakeAnchorA,
		const FVector& StakeAnchorB,
		FVector& InOutPouchAnchorA,
		FVector& InOutPouchAnchorB)
	{
		const float DirectCost = FVector::DistSquared(StakeAnchorA, InOutPouchAnchorA)
			+ FVector::DistSquared(StakeAnchorB, InOutPouchAnchorB);
		const float CrossedCost = FVector::DistSquared(StakeAnchorA, InOutPouchAnchorB)
			+ FVector::DistSquared(StakeAnchorB, InOutPouchAnchorA);
		if (CrossedCost < DirectCost)
		{
			Swap(InOutPouchAnchorA, InOutPouchAnchorB);
		}
	}

	FQuat BuildSlingshotVisualRotation(
		const FVector& EndpointA,
		const FVector& EndpointB,
		const AABTSM51SlingshotStake* StakeA,
		const AABTSM51SlingshotStake* StakeB)
	{
		const FVector Right = (EndpointB - EndpointA).GetSafeNormal();
		FVector Up = FVector::UpVector;
		if (StakeA != nullptr && StakeB != nullptr)
		{
			Up = (StakeA->GetUnitDirection() + StakeB->GetUnitDirection()).GetSafeNormal();
		}
		Up = FVector::VectorPlaneProject(Up, Right).GetSafeNormal();
		if (Up.IsNearlyZero()) Up = FVector::VectorPlaneProject(FVector::UpVector, Right).GetSafeNormal();
		const FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
		return FRotationMatrix::MakeFromXY(Forward, Right).ToQuat();
	}
}

AABTSM51PickupItem::AABTSM51PickupItem()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupVisual"));
	SetRootComponent(Visual);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) Visual->SetStaticMesh(Sphere.Object);
	Visual->SetRelativeScale3D(FVector(0.18f));
}

void AABTSM51PickupItem::InitializePickup(const EABTSItemId InItemId, const int32 InQuantity, const int32 InCellId)
{
	ItemId = InItemId;
	Quantity = FMath::Max(1, InQuantity);
	CellId = InCellId;
}

AABTSM51SlingshotDirtHole::AABTSM51SlingshotDirtHole()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DirtHoleVisual"));
	SetRootComponent(Visual);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DirtHole(TEXT("/Game/StaticMesh/SlingshotDirtHole/SM_SlingshotDitHole.SM_SlingshotDitHole"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DirtHoleMaterial(TEXT("/Game/StaticMesh/SlingshotDirtHole/MI_SlingshotDitHole.MI_SlingshotDitHole"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SteelHole(TEXT("/Game/StaticMesh/SlingshotSteelHole/SM_SlingshotSteelHole.SM_SlingshotSteelHole"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SteelHoleMaterial(TEXT("/Game/StaticMesh/SlingshotSteelHole/M_SlingshotSteelHole.M_SlingshotSteelHole"));
	StandardSlotMesh = DirtHole.Succeeded() ? DirtHole.Object : nullptr;
	StandardSlotMaterial = DirtHoleMaterial.Succeeded()
		? DirtHoleMaterial.Object
		: nullptr;
	FinaleSlotMesh = SteelHole.Succeeded() ? SteelHole.Object : nullptr;
	FinaleSlotMaterial = SteelHoleMaterial.Succeeded()
		? SteelHoleMaterial.Object
		: nullptr;
	ApplySlotVisual(EABTSSlingshotSlotKind::Standard);
	Visual->SetRelativeScale3D(FVector(0.55f, 0.55f, 0.06f));
}

void AABTSM51SlingshotDirtHole::ApplySlotVisual(
	const EABTSSlingshotSlotKind InSlotKind)
{
	const bool bFinale =
		InSlotKind == EABTSSlingshotSlotKind::FinaleSpace;
	Visual->SetStaticMesh(bFinale ? FinaleSlotMesh : StandardSlotMesh);
	Visual->SetMaterial(
		0,
		bFinale ? FinaleSlotMaterial : StandardSlotMaterial);
}

void AABTSM51SlingshotDirtHole::InitializeHole(const int32 InCellId)
{
	CellId = InCellId;
	SlotKind = EABTSSlingshotSlotKind::Standard;
	SlotSide = EABTSSlingshotSlotSide::None;
	SlotPairId = INDEX_NONE;
	ApplySlotVisual(SlotKind);
}

void AABTSM51SlingshotDirtHole::InitializeFinaleSpaceSlot(
	const int32 InCellId,
	const int32 InPairId,
	const EABTSSlingshotSlotSide InSide)
{
	CellId = InCellId;
	SlotKind = EABTSSlingshotSlotKind::FinaleSpace;
	SlotSide = InSide;
	SlotPairId = InPairId;
	ApplySlotVisual(SlotKind);
	Tags.AddUnique(FName(TEXT("ABTS.M11.FinalSpaceSlot")));
	Tags.AddUnique(InSide == EABTSSlingshotSlotSide::Left
		? FName(TEXT("ABTS.M11.FinalSpaceSlot.Left"))
		: FName(TEXT("ABTS.M11.FinalSpaceSlot.Right")));
}

void AABTSM51SlingshotDirtHole::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithDirtHole(this);
	}
}

AABTSM51SlingshotStake::AABTSM51SlingshotStake()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("StakeRoot"));
	SetRootComponent(Root);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StakeVisual"));
	Visual->SetupAttachment(Root);
	ConfigureInteractionMesh(*Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Stake(TEXT("/Game/StaticMesh/Stake/Simple/SM_Stake_Simple.SM_Stake_Simple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StakeMaterial(TEXT("/Game/StaticMesh/Stake/Simple/MI_Stake_Simple.MI_Stake_Simple"));
	if (Stake.Succeeded()) Visual->SetStaticMesh(Stake.Object);
	if (StakeMaterial.Succeeded()) Visual->SetMaterial(0, StakeMaterial.Object);
	Visual->SetRelativeScale3D(FVector(0.14f, 0.14f, 1.1f));
}

void AABTSM51SlingshotStake::InitializeStake(
	const EABTSItemId InStakeItem,
	const int32 InCellId,
	const FVector& InUnitDirection)
{
	StakeItem = InStakeItem;
	CellId = InCellId;
	UnitDirection = InUnitDirection.GetSafeNormal();
	EABTSSlingshotTier Tier = EABTSSlingshotTier::Simple;
	ABTSTryResolveSlingshotPartTier(StakeItem, Tier);
	const FABTSSlingshotVisualPreset Preset = ABTSMakeDefaultSlingshotVisualPreset(Tier);
	ApplyVisualSlot(Preset.StakeVisual, Preset.StakeDiameterCM, Preset.StakeHeightCM);
}

void AABTSM51SlingshotStake::SetInstalledSlotIdentity(
	const EABTSSlingshotSlotKind InSlotKind,
	const int32 InSlotPairId,
	const EABTSSlingshotSlotSide InSlotSide)
{
	InstalledSlotKind = InSlotKind;
	InstalledSlotPairId = InSlotPairId;
	InstalledSlotSide = InSlotSide;
}

FVector AABTSM51SlingshotStake::GetVisualTopWorldLocation() const
{
	const FVector Up = UnitDirection.IsNearlyZero() ? GetActorUpVector().GetSafeNormal() : UnitDirection;
	return GetActorLocation() + Up * (VisualHeightCM * 0.5f);
}

FVector AABTSM51SlingshotStake::GetVisualBottomWorldLocation() const
{
	const FVector Up = UnitDirection.IsNearlyZero() ? GetActorUpVector().GetSafeNormal() : UnitDirection;
	return GetActorLocation() - Up * (VisualHeightCM * 0.5f);
}

void AABTSM51SlingshotStake::ConfigureVisualDimensions(
	const float DiameterCM,
	const float HeightCM,
	UMaterialInterface* Material)
{
	const float SafeDiameterCM = FMath::Max(1.0f, DiameterCM);
	const float SafeHeightCM = FMath::Max(1.0f, HeightCM);
	VisualHeightCM = SafeHeightCM;
	StakeObstructionRadiusCM = SafeDiameterCM * 0.5f;
	const FVector BaseWorld = GetActorLocation() - GetActorUpVector() * (SafeHeightCM * 0.5f);
	Visual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		Visual->GetStaticMesh(),
		BaseWorld,
		GetActorQuat(),
		FVector(SafeDiameterCM, SafeDiameterCM, SafeHeightCM),
		FABTSSlingshotVisualSlot(),
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
	if (Material) Visual->SetMaterial(0, Material);
}

void AABTSM51SlingshotStake::ApplyVisualSlot(
	const FABTSSlingshotVisualSlot& VisualSlot,
	const float DiameterCM,
	const float HeightCM)
{
	if (VisualSlot.Mesh) Visual->SetStaticMesh(VisualSlot.Mesh);
	if (VisualSlot.Material) Visual->SetMaterial(0, VisualSlot.Material);
	const float SafeDiameterCM = FMath::Max(1.0f, DiameterCM);
	const float SafeHeightCM = FMath::Max(1.0f, HeightCM);
	VisualHeightCM = SafeHeightCM;
	StakeObstructionRadiusCM = SafeDiameterCM * 0.5f * FMath::Max(
		FMath::Abs(VisualSlot.LocalScale.X),
		FMath::Abs(VisualSlot.LocalScale.Y));
	const FVector BaseWorld = GetActorLocation() - GetActorUpVector() * (SafeHeightCM * 0.5f);
	Visual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		Visual->GetStaticMesh(),
		BaseWorld,
		GetActorQuat(),
		FVector(SafeDiameterCM, SafeDiameterCM, SafeHeightCM),
		VisualSlot,
		EABTSSlingshotVisualAnchor::BoundsBottomCenter));
}

void AABTSM51SlingshotStake::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithStake(this);
	}
}

AABTSM51SlingshotCord::AABTSM51SlingshotCord()
{
	PrimaryActorTick.bCanEverTick = false;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordVisual"));
	SetRootComponent(Visual);
	// The root is a legacy transform carrier only. It must never remain as an
	// invisible click target after the two visible cord segments replace it.
	ConfigureM51VisualOnlyMesh(*Visual);
	CordSegmentA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordSegmentA"));
	CordSegmentB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CordSegmentB"));
	PouchVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PouchVisual"));
	CordSegmentA->SetupAttachment(Visual);
	CordSegmentB->SetupAttachment(Visual);
	PouchVisual->SetupAttachment(Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cord(TEXT("/Game/StaticMesh/Cord/Simple/SM_Cord_Simple.SM_Cord_Simple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CordMaterial(TEXT("/Game/StaticMesh/Cord/Simple/MI_Cord_Simple.MI_Cord_Simple"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pouch(TEXT("/Game/StaticMesh/Pouch/Simple/SM_Pouch_Simple.SM_Pouch_Simple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PouchMaterial(TEXT("/Game/StaticMesh/Pouch/Simple/MI_Pouch_Simple.MI_Pouch_Simple"));
	if (Cord.Succeeded()) Visual->SetStaticMesh(Cord.Object);
	if (Cord.Succeeded())
	{
		DefaultCordCylinderMesh = Cord.Object;
		CordSegmentA->SetStaticMesh(DefaultCordCylinderMesh);
		CordSegmentB->SetStaticMesh(DefaultCordCylinderMesh);
	}
	if (Pouch.Succeeded())
	{
		DefaultPouchSphereMesh = Pouch.Object;
		PouchVisual->SetStaticMesh(DefaultPouchSphereMesh);
	}
	if (CordMaterial.Succeeded()) { CordSegmentA->SetMaterial(0, CordMaterial.Object); CordSegmentB->SetMaterial(0, CordMaterial.Object); }
	if (PouchMaterial.Succeeded()) PouchVisual->SetMaterial(0, PouchMaterial.Object);
	ConfigureM51VisualOnlyMesh(*CordSegmentA);
	ConfigureM51VisualOnlyMesh(*CordSegmentB);
	// Launch mode is entered by clicking the visible pouch, not an invisible
	// crossbar or either cord segment. The Actor click callback still identifies
	// this AABTSM51SlingshotCord for the controller.
	ConfigureInteractionMesh(*PouchVisual);
}

void AABTSM51SlingshotCord::InitializeCord(
	AABTSM51SlingshotStake* InStakeA,
	AABTSM51SlingshotStake* InStakeB,
	const FVector& InEndpointA,
	const FVector& InEndpointB)
{
	const EABTSItemId StakeItem = InStakeA ? InStakeA->GetStakeItem() : EABTSItemId::SimpleStake;
	EABTSSlingshotTier InferredTier = EABTSSlingshotTier::Simple;
	ABTSTryResolveSlingshotPartTier(StakeItem, InferredTier);
	InitializeCordWithTier(InStakeA, InStakeB, InEndpointA, InEndpointB, InferredTier);
}

void AABTSM51SlingshotCord::InitializeCordWithTier(
	AABTSM51SlingshotStake* InStakeA,
	AABTSM51SlingshotStake* InStakeB,
	const FVector& InEndpointA,
	const FVector& InEndpointB,
	const EABTSSlingshotTier InTier)
{
	StakeA = InStakeA;
	StakeB = InStakeB;
	SlingshotTier = InTier;
	EndpointA = InEndpointA;
	EndpointB = InEndpointB;
	const FVector Delta = EndpointB - EndpointA;
	const float Length = Delta.Size();
	if (Length <= SMALL_NUMBER) return;
	SetActorLocation((EndpointA + EndpointB) * 0.5f);
	SetActorRotation(FRotationMatrix::MakeFromX(Delta / Length).ToQuat());
	Visual->SetRelativeScale3D(FVector(Length / 100.0f, 0.035f, 0.035f));
	const FABTSSlingshotVisualPreset Preset = ABTSMakeDefaultSlingshotVisualPreset(InTier);
	ConfigureTwoCordVisuals(Preset.CordVisual, Preset.PouchVisual, Preset.ConnectionLayout,
		Preset.CordThicknessCM, Preset.PouchSizeCM);
}

void AABTSM51SlingshotCord::ConfigureVisualThickness(const float ThicknessCM, UMaterialInterface* Material)
{
	const FVector CurrentScale = Visual->GetRelativeScale3D();
	const float ThicknessScale = FMath::Max(1.0f, ThicknessCM) / 100.0f;
	CordThicknessCM = FMath::Max(0.1f, ThicknessCM);
	CordObstructionRadiusCM = CordThicknessCM * 0.5f;
	Visual->SetRelativeScale3D(FVector(CurrentScale.X, ThicknessScale, ThicknessScale));
	if (Material) Visual->SetMaterial(0, Material);
}

void AABTSM51SlingshotCord::ApplyVisualSlot(const FABTSSlingshotVisualSlot& VisualSlot, const float ThicknessCM)
{
	if (VisualSlot.Mesh) Visual->SetStaticMesh(VisualSlot.Mesh);
	if (VisualSlot.Material) Visual->SetMaterial(0, VisualSlot.Material);
	const FTransform BaseTransform = GetActorTransform();
	SetActorLocation(BaseTransform.TransformPosition(VisualSlot.LocalOffsetCM), false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation((BaseTransform.GetRotation() * VisualSlot.LocalRotation.Quaternion()).Rotator(), ETeleportType::TeleportPhysics);
	const FVector CurrentScale = Visual->GetRelativeScale3D();
	CordThicknessCM = FMath::Max(0.1f, ThicknessCM);
	CordObstructionRadiusCM = CordThicknessCM * 0.5f * FMath::Max(
		FMath::Abs(VisualSlot.LocalScale.X),
		FMath::Abs(VisualSlot.LocalScale.Y));
	Visual->SetRelativeScale3D(FVector(
		CurrentScale.X * VisualSlot.LocalScale.X,
		FMath::Max(1.0f, ThicknessCM) / 100.0f * VisualSlot.LocalScale.Y,
		FMath::Max(1.0f, ThicknessCM) / 100.0f * VisualSlot.LocalScale.Z));
}

void AABTSM51SlingshotCord::ConfigureTwoCordVisuals(
	const FABTSSlingshotVisualSlot& CordSlot,
	const FABTSSlingshotVisualSlot& PouchSlot,
	const FABTSSlingshotConnectionLayout& Layout,
	const float ThicknessCM,
	const FVector& InPouchSizeCM)
{
	CordVisualSlot = CordSlot;
	PouchVisualSlot = PouchSlot;
	ConnectionLayout = Layout;
	CordThicknessCM = FMath::Max(0.1f, ThicknessCM);
	CordObstructionRadiusCM = CordThicknessCM * 0.5f * FMath::Max(
		FMath::Abs(CordSlot.LocalScale.X),
		FMath::Abs(CordSlot.LocalScale.Y));
	PouchSizeCM = FVector(
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.X)),
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.Y)),
		FMath::Max(1.0f, FMath::Abs(InPouchSizeCM.Z)));

	CordSegmentA->SetStaticMesh(CordSlot.Mesh ? CordSlot.Mesh : DefaultCordCylinderMesh);
	CordSegmentB->SetStaticMesh(CordSlot.Mesh ? CordSlot.Mesh : DefaultCordCylinderMesh);
	PouchVisual->SetStaticMesh(PouchSlot.Mesh ? PouchSlot.Mesh : DefaultPouchSphereMesh);
	if (CordSlot.Material)
	{
		CordSegmentA->SetMaterial(0, CordSlot.Material);
		CordSegmentB->SetMaterial(0, CordSlot.Material);
	}
	if (PouchSlot.Material) PouchVisual->SetMaterial(0, PouchSlot.Material);

	// The original crossbar is no longer an interaction target. Only the visible
	// pouch keeps Visibility collision; both cord segments are presentation only.
	Visual->SetVisibility(false, false);
	Visual->SetHiddenInGame(true, false);
	CordSegmentA->SetVisibility(true, true);
	CordSegmentB->SetVisibility(true, true);
	PouchVisual->SetVisibility(true, true);
	ResetPouchVisualToRest();
}

FTransform AABTSM51SlingshotCord::GetRestPouchTransform() const
{
	const FQuat LayoutRotation = BuildSlingshotVisualRotation(EndpointA, EndpointB, StakeA.Get(), StakeB.Get());
	const FVector StakeAnchorA = EndpointA + LayoutRotation.RotateVector(ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector StakeAnchorB = EndpointB + LayoutRotation.RotateVector(ConnectionLayout.StakeBConnectionOffsetCM);
	const FVector Center = (StakeAnchorA + StakeAnchorB) * 0.5f
		+ LayoutRotation.RotateVector(ConnectionLayout.RestPouchOffsetCM);
	return FTransform(LayoutRotation, Center, FVector::OneVector);
}

void AABTSM51SlingshotCord::ResetPouchVisualToRest()
{
	UpdatePulledPouchVisual(GetRestPouchTransform().GetLocation(), GetRestPouchTransform().GetRotation());
}

void AABTSM51SlingshotCord::UpdatePulledPouchVisual(const FVector& WorldLocation, const FQuat& WorldRotation)
{
	if (CordSegmentA == nullptr || CordSegmentB == nullptr || PouchVisual == nullptr) return;
	const FQuat LayoutRotation = BuildSlingshotVisualRotation(EndpointA, EndpointB, StakeA.Get(), StakeB.Get());
	const FVector StakeAnchorA = EndpointA + LayoutRotation.RotateVector(ConnectionLayout.StakeAConnectionOffsetCM);
	const FVector StakeAnchorB = EndpointB + LayoutRotation.RotateVector(ConnectionLayout.StakeBConnectionOffsetCM);
	FVector PouchAnchorA = WorldLocation + WorldRotation.RotateVector(
		ABTSScaleSlingshotPouchConnectionOffset(ConnectionLayout.PouchAConnectionOffsetCM, PouchVisualSlot));
	FVector PouchAnchorB = WorldLocation + WorldRotation.RotateVector(
		ABTSScaleSlingshotPouchConnectionOffset(ConnectionLayout.PouchBConnectionOffsetCM, PouchVisualSlot));
	MatchCordEndpointsToPouchSides(StakeAnchorA, StakeAnchorB, PouchAnchorA, PouchAnchorB);
	SetSegmentBetween(*CordSegmentA, StakeAnchorA, PouchAnchorA, CordThicknessCM, CordVisualSlot);
	SetSegmentBetween(*CordSegmentB, StakeAnchorB, PouchAnchorB, CordThicknessCM, CordVisualSlot);

	PouchVisual->SetWorldTransform(ABTSMakeSlingshotVisualTransform(
		PouchVisual->GetStaticMesh(),
		WorldLocation,
		WorldRotation,
		PouchSizeCM,
		PouchVisualSlot,
		EABTSSlingshotVisualAnchor::BoundsCenter));
	CordSegmentA->SetVisibility(true, true);
	CordSegmentB->SetVisibility(true, true);
	PouchVisual->SetVisibility(true, true);
}

EABTSItemId AABTSM51SlingshotCord::GetStakeItem() const
{
	return StakeA.IsValid() ? StakeA->GetStakeItem() : EABTSItemId::SimpleStake;
}

bool AABTSM51SlingshotCord::IsFinaleSpaceSlingshot() const
{
	const AABTSM51SlingshotStake* First = StakeA.Get();
	const AABTSM51SlingshotStake* Second = StakeB.Get();
	if (SlingshotTier != EABTSSlingshotTier::Space || First == nullptr || Second == nullptr)
	{
		return false;
	}
	const int32 PairId = First->GetInstalledSlotPairId();
	const bool bOppositeSides =
		(First->GetInstalledSlotSide() == EABTSSlingshotSlotSide::Left
			&& Second->GetInstalledSlotSide() == EABTSSlingshotSlotSide::Right)
		|| (First->GetInstalledSlotSide() == EABTSSlingshotSlotSide::Right
			&& Second->GetInstalledSlotSide() == EABTSSlingshotSlotSide::Left);
	return PairId != INDEX_NONE
		&& PairId == Second->GetInstalledSlotPairId()
		&& First->GetInstalledSlotKind() == EABTSSlingshotSlotKind::FinaleSpace
		&& Second->GetInstalledSlotKind() == EABTSSlingshotSlotKind::FinaleSpace
		&& bOppositeSides;
}

int32 AABTSM51SlingshotCord::GetFinaleSlotPairId() const
{
	return IsFinaleSpaceSlingshot() ? StakeA->GetInstalledSlotPairId() : INDEX_NONE;
}

void AABTSM51SlingshotCord::NotifyActorOnClicked(const FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);
	if (ButtonPressed != EKeys::LeftMouseButton || GetWorld() == nullptr) return;
	if (AABTSM51PlayerController* Controller = Cast<AABTSM51PlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		Controller->InteractWithSlingshotCord(this);
	}
}
