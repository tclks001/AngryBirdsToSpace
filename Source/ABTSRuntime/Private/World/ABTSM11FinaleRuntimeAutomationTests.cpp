// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Game/ABTSM10GameMode.h"
#include "Game/ABTSM11GameMode.h"
#include "Misc/AutomationTest.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleSystem.h"

namespace
{
	FABTSM110FinaleLocalFrame MakeM11BRuntimeTestFrame(
		const FVector& Origin = FVector(1000.0, -2000.0, 3000.0),
		const FQuat& Rotation = FQuat::Identity)
	{
		FABTSM110FinaleLocalFrame Frame;
		Frame.LayoutVersion = 1;
		Frame.LaunchTaskId = 6;
		Frame.AnchorCellId = 99;
		Frame.SlotPairId = 11001;
		Frame.WorldTransform = FTransform(Rotation, Origin);
		const FVector Right = Frame.GetRight();
		Frame.LeftSlotWorldLocation = Origin - Right * 105.0;
		Frame.RightSlotWorldLocation = Origin + Right * 105.0;
		Frame.bValid = true;
		return Frame;
	}

	bool RequestsHaveIdenticalLocalAuthority(
		const FABTSM11TrajectoryRequest& A,
		const FABTSM11TrajectoryRequest& B)
	{
		if (A.Scenario.ScenarioHash != B.Scenario.ScenarioHash
			|| A.InitialPositionCM != B.InitialPositionCM
			|| A.InitialVelocityCMPerSec != B.InitialVelocityCMPerSec
			|| A.Config.EnabledAssistMask != B.Config.EnabledAssistMask)
		{
			return false;
		}
		for (int32 Index = 0;
			Index < FABTSM11GravityScenario::BodyCount;
			++Index)
		{
			const FABTSM11GravityBodySpec& BodyA = A.Scenario.Bodies[Index];
			const FABTSM11GravityBodySpec& BodyB = B.Scenario.Bodies[Index];
			if (BodyA.BodyId != BodyB.BodyId
				|| BodyA.Role != BodyB.Role
				|| BodyA.CenterCM != BodyB.CenterCM
				|| BodyA.GravitationalParameterCM3PerSec2
					!= BodyB.GravitationalParameterCM3PerSec2
				|| BodyA.CollisionRadiusCM != BodyB.CollisionRadiusCM)
			{
				return false;
			}
		}
		return A.Scenario.Target.TargetId == B.Scenario.Target.TargetId
			&& A.Scenario.Target.CenterCM == B.Scenario.Target.CenterCM
			&& A.Scenario.Target.HitRadiusCM
				== B.Scenario.Target.HitRadiusCM
			&& A.Scenario.Target.GeometricContactRadiusCM
				== B.Scenario.Target.GeometricContactRadiusCM
			&& A.Scenario.Target.bUseSeparateGeometricContactCenter
				== B.Scenario.Target.bUseSeparateGeometricContactCenter
			&& A.Scenario.Target.GeometricContactCenterCM
				== B.Scenario.Target.GeometricContactCenterCM;
	}

	void TestVisualOnlyActorContract(
		FAutomationTestBase& Test,
		const AActor& Actor,
		const FString& Label)
	{
		Test.TestFalse(
			*FString::Printf(TEXT("%s Actor collision is disabled"), *Label),
			Actor.GetActorEnableCollision());
		Test.TestFalse(
			*FString::Printf(TEXT("%s Actor tick is disabled"), *Label),
			Actor.IsActorTickEnabled());
		Test.TestFalse(
			*FString::Printf(TEXT("%s Actor can never tick"), *Label),
			Actor.PrimaryActorTick.bCanEverTick);

		TInlineComponentArray<UActorComponent*> Components;
		Actor.GetComponents(Components);
		Test.TestTrue(
			*FString::Printf(
				TEXT("%s owns at least its native presentation components"),
				*Label),
			Components.Num() >= 2);
		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			UActorComponent* Component = Components[Index];
			Test.TestNotNull(
				*FString::Printf(
					TEXT("%s component %d exists"),
					*Label,
					Index),
				Component);
			if (Component == nullptr)
			{
				continue;
			}
			Test.TestFalse(
				*FString::Printf(
					TEXT("%s component %d tick is disabled"),
					*Label,
					Index),
				Component->IsComponentTickEnabled());
			Test.TestFalse(
				*FString::Printf(
					TEXT("%s component %d can never tick"),
					*Label,
					Index),
				Component->PrimaryComponentTick.bCanEverTick);

			if (const UPrimitiveComponent* Primitive =
				Cast<UPrimitiveComponent>(Component))
			{
				Test.TestEqual(
					*FString::Printf(
						TEXT("%s primitive %d has no collision"),
						*Label,
						Index),
					Primitive->GetCollisionEnabled(),
					ECollisionEnabled::NoCollision);
				Test.TestFalse(
					*FString::Printf(
						TEXT("%s primitive %d creates no overlaps"),
						*Label,
						Index),
					Primitive->GetGenerateOverlapEvents());
				Test.TestFalse(
					*FString::Printf(
						TEXT("%s primitive %d never simulates Chaos"),
						*Label,
						Index),
					Primitive->IsSimulatingPhysics());
				Test.TestFalse(
					*FString::Printf(
						TEXT("%s primitive %d never affects navigation"),
						*Label,
						Index),
					Primitive->CanEverAffectNavigation());
			}
		}
	}

	void ResignM11BCertifiedBundleForTamperTest(
		FABTSM11FinaleLayoutPreset& Preset)
	{
		Preset.PresetSourceHash = 0;
		Preset.PresetHash = 0;
		Preset.ScanContractHash = 0;
		Preset.CertifiedBundleHash = 0;
		Preset.CanonicalScenario.ScenarioHash = 1;
		for (FABTSM11PrefixTrustRegion& Region
			: Preset.PrefixTrustRegions)
		{
			Region.RegionHash =
				FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(Region);
		}
		Preset.PresetSourceHash =
			FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset);
		Preset.PresetHash =
			FABTSM11FinaleLayoutHash::ComputePresetHash(Preset);
		Preset.CanonicalScenario.ScenarioHash =
			FABTSM11FinaleLayoutHash::FoldScenarioHash(Preset.PresetHash);
		Preset.ScanContractHash =
			FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset);
		Preset.CertifiedBundleHash =
			FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(Preset);
	}

	class FScopedM11BAutomationWorld
	{
	public:
		FScopedM11BAutomationWorld()
		{
			const UWorld::InitializationValues Values =
				UWorld::InitializationValues()
					.InitializeScenes(false)
					.AllowAudioPlayback(false)
					.RequiresHitProxies(false)
					.CreatePhysicsScene(false)
					.CreateNavigation(false)
					.CreateAISystem(false)
					.ShouldSimulatePhysics(false)
					.EnableTraceCollision(false)
					.SetTransactional(false)
					.CreateFXSystem(false);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM11BRuntimeAutomationWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FScopedM11BAutomationWorld()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BNativePresentationIsolationTest,
	"ABTS.M11B.Runtime.NativePresentationIsolation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11BNativePresentationIsolationTest::RunTest(
	const FString& Parameters)
{
	TestFalse(
		TEXT("Gravity-body presentation is not a Blueprint base"),
		AABTSM11GravityBodyActor::StaticClass()
			->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase")));
	TestFalse(
		TEXT("UFO presentation is not a Blueprint base"),
		AABTSM11UFOActor::StaticClass()
			->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase")));

	FScopedM11BAutomationWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient automation World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM11GravityBodyActor* Body =
		World->SpawnActor<AABTSM11GravityBodyActor>(
			AABTSM11GravityBodyActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AABTSM11UFOActor* UFO =
		World->SpawnActor<AABTSM11UFOActor>(
			AABTSM11UFOActor::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Native gravity-body Actor spawns"), Body);
	TestNotNull(TEXT("Native UFO Actor spawns"), UFO);
	if (Body == nullptr || UFO == nullptr)
	{
		return false;
	}
	TestTrue(
		TEXT("Gravity-body instance has the exact native class"),
		Body->GetClass() == AABTSM11GravityBodyActor::StaticClass());
	TestTrue(
		TEXT("UFO instance has the exact native class"),
		UFO->GetClass() == AABTSM11UFOActor::StaticClass());

	const auto EnableForbiddenPresentationState = [](AActor& Actor)
	{
		Actor.PrimaryActorTick.bCanEverTick = true;
		Actor.SetActorTickEnabled(true);
		Actor.SetActorEnableCollision(true);
		TInlineComponentArray<UActorComponent*> Components;
		Actor.GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component == nullptr)
			{
				continue;
			}
			Component->PrimaryComponentTick.bCanEverTick = true;
			Component->SetComponentTickEnabled(true);
			if (UPrimitiveComponent* Primitive =
				Cast<UPrimitiveComponent>(Component))
			{
				Primitive->SetCollisionEnabled(
					ECollisionEnabled::QueryOnly);
				Primitive->SetGenerateOverlapEvents(true);
				Primitive->SetCanEverAffectNavigation(true);
			}
		}
	};
	EnableForbiddenPresentationState(*Body);
	EnableForbiddenPresentationState(*UFO);

	const FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const FABTSM110FinaleLocalFrame Frame =
		MakeM11BRuntimeTestFrame();
	TestTrue(
		TEXT("Gravity-body ConfigurePresentation accepts the analytic spec"),
		Body->ConfigurePresentation(
			Preset.CanonicalScenario.GetAssist(1),
			Frame));
	TestTrue(
		TEXT("UFO ConfigurePresentation accepts the analytic spec"),
		UFO->ConfigurePresentation(
			Preset.CanonicalScenario.Target,
			Frame));
	TestVisualOnlyActorContract(*this, *Body, TEXT("NativeAssist"));
	TestVisualOnlyActorContract(*this, *UFO, TEXT("NativeUFO"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BRuntimeCompatibilityTest,
	"ABTS.M11B.Runtime.CompatibilityBoundary",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11BRuntimeCompatibilityTest::RunTest(
	const FString& Parameters)
{
	FABTSM11FinaleLayoutPreset Preset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
	const FABTSM110FinaleLocalFrame Frame =
		MakeM11BRuntimeTestFrame();
	FString Failure;
	TestTrue(
		TEXT("Certified v1 accepts GeneratorVersion=3, FrameLayout=1 and reference radii"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			Preset,
			3,
			Preset.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));

	FABTSM11FinaleLayoutPreset MissingBundle = Preset;
	MissingBundle.CertifiedBundleHash = 0;
	TestFalse(
		TEXT("A certification manifest without its bundle identity fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			MissingBundle,
			3,
			MissingBundle.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));

	FABTSM11FinaleLayoutPreset WrongNonZeroCertification = Preset;
	WrongNonZeroCertification.CertificationHash ^= 0x1ull;
	WrongNonZeroCertification.CertifiedBundleHash =
		FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(
			WrongNonZeroCertification);
	TestTrue(
		TEXT("A re-bundled wrong non-zero certification hash is internally self-consistent"),
		WrongNonZeroCertification.IsValid(&Failure));
	TestFalse(
		TEXT("Runtime rejects a wrong but non-zero certification identity"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			WrongNonZeroCertification,
			3,
			WrongNonZeroCertification.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));
	TestEqual(
		TEXT("Wrong certification fails at the frozen manifest boundary"),
		Failure,
		FString(TEXT("CertifiedBundleManifestMismatch")));

	FABTSM11FinaleLayoutPreset ResignedSourceTamper = Preset;
	ResignedSourceTamper.PrimaryCompatibilityToleranceCM += 1.0;
	ResignM11BCertifiedBundleForTamperTest(ResignedSourceTamper);
	TestTrue(
		TEXT("A source tamper can be made internally hash-consistent"),
		ResignedSourceTamper.IsValid(&Failure));
	TestFalse(
		TEXT("Runtime rejects a self-consistently re-signed source layout"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			ResignedSourceTamper,
			3,
			ResignedSourceTamper.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));

	FABTSM11FinaleLayoutPreset ResignedTrustTamper = Preset;
	ResignedTrustTamper.PrefixTrustRegions[1].CaptureMarginCells += 0.25;
	ResignM11BCertifiedBundleForTamperTest(ResignedTrustTamper);
	TestTrue(
		TEXT("A trust-region tamper can be made internally hash-consistent"),
		ResignedTrustTamper.IsValid(&Failure));
	TestFalse(
		TEXT("Runtime rejects a self-consistently re-signed trust region"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			ResignedTrustTamper,
			3,
			ResignedTrustTamper.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));

	FABTSM11FinaleLayoutPreset CorruptBundle = Preset;
	CorruptBundle.CertifiedBundleHash ^= 0x1ull;
	TestFalse(
		TEXT("A tampered bundle hash fails its structural integrity check"),
		CorruptBundle.IsValid(&Failure));
	TestFalse(
		TEXT("A different M3 generator version fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			Preset,
			2,
			Preset.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));

	FABTSM110FinaleLocalFrame WrongFrameVersion = Frame;
	WrongFrameVersion.LayoutVersion = 2;
	TestFalse(
		TEXT("A different finale-frame layout version fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			Preset,
			3,
			Preset.ReferencePrimaryRadiusCM,
			WrongFrameVersion,
			&Failure));

	FABTSM110FinaleLocalFrame ScaledFrame = Frame;
	ScaledFrame.WorldTransform.SetScale3D(FVector(2.0, 1.0, 1.0));
	TestFalse(
		TEXT("A scaled finale frame fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			Preset,
			3,
			Preset.ReferencePrimaryRadiusCM,
			ScaledFrame,
			&Failure));

	TestFalse(
		TEXT("A primary outside the certified radius tolerance fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			Preset,
			3,
			Preset.ReferencePrimaryRadiusCM
				+ Preset.PrimaryCompatibilityToleranceCM
				+ 1.0,
			Frame,
			&Failure));

	FABTSM11FinaleLayoutPreset WrongLaunchRadius = Preset;
	WrongLaunchRadius.ReferenceLaunchRadiusCM +=
		WrongLaunchRadius.PrimaryCompatibilityToleranceCM + 1.0;
	ResignM11BCertifiedBundleForTamperTest(WrongLaunchRadius);
	TestFalse(
		TEXT("A preset whose canonical pouch radius does not match its reference fails closed"),
		AABTSM11FinaleSystem::ValidateRuntimeBoundary(
			WrongLaunchRadius,
			3,
			WrongLaunchRadius.ReferencePrimaryRadiusCM,
			Frame,
			&Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11BRuntimePresentationTest,
	"ABTS.M11B.Runtime.ActorAuthority",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11BRuntimePresentationTest::RunTest(
	const FString& Parameters)
{
	TestTrue(
		TEXT("M11 GameMode extends the accepted M10 entry"),
		AABTSM11GameMode::StaticClass()->IsChildOf(
			AABTSM10GameMode::StaticClass()));
	TestNull(
		TEXT("M11 GameMode exposes no overridable finale-system class"),
		AABTSM11GameMode::StaticClass()->FindPropertyByName(
			TEXT("FinaleSystemClass")));
	TestEqual(
		TEXT("Runtime contract exposes exactly three assist presentations"),
		AABTSM11FinaleSystem::ExpectedAssistPresentationCount,
		3);
	TestFalse(
		TEXT("Gravity-body presentation is not a Blueprint base"),
		AABTSM11GravityBodyActor::StaticClass()
			->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase")));
	TestFalse(
		TEXT("UFO presentation is not a Blueprint base"),
		AABTSM11UFOActor::StaticClass()
			->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase")));
	TestEqual(
		TEXT("M11 gravity data remains primary plus exactly three assists"),
		FABTSM11GravityScenario::BodyCount,
		4);
	for (int32 Index = 0;
		Index < FABTSM11GravityScenario::BodyCount;
		++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Gravity role %d is fixed and ordered"), Index),
			static_cast<int32>(
				FABTSM11FinaleLayoutPreset::MakeCertifiedV1()
					.CanonicalScenario.Bodies[Index].Role),
			Index);
	}

	FScopedM11BAutomationWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient automation World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM11FinaleSystem* System =
		World->SpawnActor<AABTSM11FinaleSystem>(
			AABTSM11FinaleSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Finale system Actor spawns"), System);
	if (System == nullptr)
	{
		return false;
	}

	const FQuat FrameRotation =
		FQuat(FVector::UpVector, FMath::DegreesToRadians(37.0));
	const FABTSM110FinaleLocalFrame Frame =
		MakeM11BRuntimeTestFrame(
			FVector(12345.0, -54321.0, 6789.0),
			FrameRotation);
	FABTSM11FinaleLayoutPreset ExpectedPreset =
		FABTSM11FinaleLayoutPreset::MakeCertifiedV1();

	AABTSM11FinaleSystem* PartialRollbackSystem =
		World->SpawnActor<AABTSM11FinaleSystem>(
			AABTSM11FinaleSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(
		TEXT("Partial-rollback finale system Actor spawns"),
		PartialRollbackSystem);
	if (PartialRollbackSystem != nullptr)
	{
		// The three assist Actors configure first. This invalid presentation
		// radius then forces the later UFO ConfigurePresentation call to fail,
		// exercising the pending-Actor rollback branch rather than a
		// pre-spawn compatibility rejection.
		PartialRollbackSystem->UFOMeshReferenceRadiusCM = 0.0;
		AddExpectedErrorPlain(
			TEXT("[ABTS][M11-B][FinaleSystem] Rejected Reason=UFOPresentationConfigureFailed"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		TestFalse(
			TEXT("A late UFO configuration failure rejects initialization"),
			PartialRollbackSystem->InitializeFromCertifiedPreset(
				ExpectedPreset,
				3,
				ExpectedPreset.ReferencePrimaryRadiusCM,
				Frame));
		TestEqual(
			TEXT("Late failure commits no assist Actors"),
			PartialRollbackSystem->GetSpawnedAssistActorCount(),
			0);
		TestFalse(
			TEXT("Late failure commits no UFO Actor"),
			PartialRollbackSystem->HasSpawnedUFOActor());
		TestEqual(
			TEXT("Late failure publishes the Failed state"),
			static_cast<int32>(
				PartialRollbackSystem->GetSystemState()),
			static_cast<int32>(
				EABTSM11FinaleSystemState::Failed));

		int32 OwnedAssistActorCount = 0;
		for (TActorIterator<AABTSM11GravityBodyActor> It(World);
			It;
			++It)
		{
			OwnedAssistActorCount +=
				IsValid(*It)
				&& It->GetOwner() == PartialRollbackSystem
				? 1
				: 0;
		}
		int32 OwnedUFOActorCount = 0;
		for (TActorIterator<AABTSM11UFOActor> It(World);
			It;
			++It)
		{
			OwnedUFOActorCount +=
				IsValid(*It)
				&& It->GetOwner() == PartialRollbackSystem
				? 1
				: 0;
		}
		TestEqual(
			TEXT("Late failure destroys every pending assist Actor"),
			OwnedAssistActorCount,
			0);
		TestEqual(
			TEXT("Late failure destroys the pending UFO Actor"),
			OwnedUFOActorCount,
			0);
	}

	AABTSM11FinaleSystem* RejectedSystem =
		World->SpawnActor<AABTSM11FinaleSystem>(
			AABTSM11FinaleSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Rejected-path finale system Actor spawns"), RejectedSystem);
	if (RejectedSystem != nullptr)
	{
		AddExpectedErrorPlain(
			TEXT("[ABTS][M11-B][FinaleSystem] Rejected Reason=IncompatibleGeneratorVersion"),
			EAutomationExpectedErrorFlags::Contains,
			1);
		TestFalse(
			TEXT("An incompatible generator cannot instantiate presentations"),
			RejectedSystem->InitializeFromCertifiedPreset(
				ExpectedPreset,
				2,
				ExpectedPreset.ReferencePrimaryRadiusCM,
				Frame));
		TestEqual(
			TEXT("Failed initialization commits no assist Actors"),
			RejectedSystem->GetSpawnedAssistActorCount(),
			0);
		TestFalse(
			TEXT("Failed initialization commits no UFO Actor"),
			RejectedSystem->HasSpawnedUFOActor());
		TestEqual(
			TEXT("Failed initialization publishes the Failed state"),
			static_cast<int32>(RejectedSystem->GetSystemState()),
			static_cast<int32>(EABTSM11FinaleSystemState::Failed));
	}

	TestTrue(
		*FString::Printf(
			TEXT("Finale system initializes transactionally: %s"),
			*System->GetFailureReason()),
		System->InitializeFromCertifiedPreset(
			ExpectedPreset,
			3,
			ExpectedPreset.ReferencePrimaryRadiusCM,
			Frame));
	if (!System->IsLayoutReady())
	{
		return false;
	}
	TestEqual(
		TEXT("Exactly three gravity-body Actors commit"),
		System->GetSpawnedAssistActorCount(),
		3);
	TestTrue(
		TEXT("Exactly one non-gravitating UFO Actor commits"),
		System->HasSpawnedUFOActor());

	const TArray<TObjectPtr<AABTSM11GravityBodyActor>>& BodyActors =
		System->GetGravityBodyActors();
	for (int32 Index = 0; Index < BodyActors.Num(); ++Index)
	{
		const AABTSM11GravityBodyActor* Actor = BodyActors[Index];
		TestNotNull(
			*FString::Printf(TEXT("Assist Actor %d is valid"), Index + 1),
			Actor);
		if (Actor == nullptr)
		{
			continue;
		}
		TestTrue(
			*FString::Printf(
				TEXT("Assist Actor %d uses the fixed native class"),
				Index + 1),
			Actor->GetClass()
				== AABTSM11GravityBodyActor::StaticClass());
		const FABTSM11GravityBodySpec& Spec =
			ExpectedPreset.CanonicalScenario.GetAssist(Index + 1);
		const FVector ExpectedWorldPosition =
			Frame.TransformLocalPosition(FVector(Spec.CenterCM));
		AddInfo(FString::Printf(
			TEXT("Assist%d Expected=%s Actual=%s Delta=%.9f"),
			Index + 1,
			*ExpectedWorldPosition.ToCompactString(),
			*Actor->GetActorLocation().ToCompactString(),
			FVector::Distance(
				ExpectedWorldPosition,
				Actor->GetActorLocation())));
		TestTrue(
			*FString::Printf(
				TEXT("Assist Actor %d receives the one-way frame transform"),
				Index + 1),
			Actor->GetActorLocation().Equals(
				ExpectedWorldPosition,
				0.01));
		const UStaticMeshComponent* Mesh =
			Actor->GetVisualMeshComponent();
		TestNotNull(TEXT("Assist presentation owns one mesh component"), Mesh);
		if (Mesh != nullptr)
		{
			TestEqual(
				TEXT("Assist presentation collision is disabled"),
				Mesh->GetCollisionEnabled(),
				ECollisionEnabled::NoCollision);
			TestFalse(
				TEXT("Assist presentation never simulates Chaos"),
				Mesh->IsSimulatingPhysics());
			TestFalse(
				TEXT("Assist presentation never affects navigation"),
				Mesh->CanEverAffectNavigation());
		}
		TestFalse(
			TEXT("Assist presentation never ticks"),
			Actor->PrimaryActorTick.bCanEverTick);
		TestVisualOnlyActorContract(
			*this,
			*Actor,
			FString::Printf(TEXT("Assist%d"), Index + 1));
	}

	const AABTSM11UFOActor* UFO = System->GetUFOActor();
	TestNotNull(TEXT("UFO presentation exists"), UFO);
	if (UFO != nullptr)
	{
		TestTrue(
			TEXT("UFO Actor uses the fixed native class"),
			UFO->GetClass() == AABTSM11UFOActor::StaticClass());
		const FVector ExpectedWorldPosition =
			Frame.TransformLocalPosition(
				FVector(ExpectedPreset.CanonicalScenario.Target
					.GetGeometricContactCenterCM()));
		AddInfo(FString::Printf(
			TEXT("UFO Expected=%s Actual=%s Delta=%.9f"),
			*ExpectedWorldPosition.ToCompactString(),
			*UFO->GetActorLocation().ToCompactString(),
			FVector::Distance(
				ExpectedWorldPosition,
				UFO->GetActorLocation())));
		TestTrue(
			TEXT("UFO receives the same one-way frame transform"),
			UFO->GetActorLocation().Equals(
				ExpectedWorldPosition,
				0.01));
		TestFalse(
			TEXT("UFO presentation never ticks"),
			UFO->PrimaryActorTick.bCanEverTick);
		const UStaticMeshComponent* Mesh =
			UFO->GetVisualMeshComponent();
		TestNotNull(TEXT("UFO presentation owns one mesh component"), Mesh);
		if (Mesh != nullptr)
		{
			TestEqual(
				TEXT("UFO presentation collision is disabled"),
				Mesh->GetCollisionEnabled(),
				ECollisionEnabled::NoCollision);
			TestFalse(
				TEXT("UFO presentation never simulates Chaos"),
				Mesh->IsSimulatingPhysics());
		}
		TestVisualOnlyActorContract(
			*this,
			*UFO,
			TEXT("UFO"));
	}

	FABTSM11TrajectoryRequest BeforeActorMutation;
	FABTSM11TrajectoryRequest AfterActorMutation;
	FString Failure;
	TestTrue(
		TEXT("Ready system builds a local authoritative request"),
		System->BuildRequest(
			ExpectedPreset.NominalInput,
			0x7u,
			BeforeActorMutation,
			&Failure));
	for (AABTSM11GravityBodyActor* Actor : BodyActors)
	{
		if (IsValid(Actor))
		{
			Actor->SetActorLocationAndRotation(
				Actor->GetActorLocation()
					+ FVector(9000.0, -8000.0, 7000.0),
				FQuat(FVector::ForwardVector, 0.75));
		}
	}
	if (IsValid(System->GetUFOActor()))
	{
		System->GetUFOActor()->SetActorLocation(
			FVector(-1.0e6, 2.0e6, -3.0e6));
	}
	TestTrue(
		TEXT("Actor mutation cannot block local request compilation"),
		System->BuildRequest(
			ExpectedPreset.NominalInput,
			0x7u,
			AfterActorMutation,
			&Failure));
	TestTrue(
		TEXT("Actor transforms never feed back into the solver scenario"),
		RequestsHaveIdenticalLocalAuthority(
			BeforeActorMutation,
			AfterActorMutation));

	const int32 ActorCountBeforeRepeat =
		System->GetSpawnedAssistActorCount();
	AddExpectedErrorPlain(
		TEXT("[ABTS][M11-B][FinaleSystem] Reinitialization rejected State=1."),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(
		TEXT("A second initialization is rejected instead of duplicating Actors"),
		System->InitializeFromRuntimeData(
			3,
			ExpectedPreset.ReferencePrimaryRadiusCM,
			Frame));
	TestEqual(
		TEXT("Rejected reinitialization preserves the committed Actor set"),
		System->GetSpawnedAssistActorCount(),
		3);
	TestEqual(
		TEXT("The successful initialization had committed three Actors"),
		ActorCountBeforeRepeat,
		3);
	return true;
}

#endif
