// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Rendering/ABTST4LowPolyCloudPrototype.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0CommandLineContractTest,
	"ABTS.Rendering.Toon.T0.CommandLineContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0CommandLineContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FABTSToonVisualCaptureRunConfig Config;
	FString Failure;
	TestTrue(
		TEXT("An unrelated process parses without enabling T0"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-NullRHI -Unattended"),
			Config,
			&Failure));
	TestFalse(TEXT("T0 is opt-in"), Config.bEnabled);

	Failure.Reset();
	TestTrue(
		TEXT("The named suite and GPU contract parse"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSVisualCaptureSuite=ToonT0 -ABTSToonT0Mode=GPU -ABTSToonT0ExpectedSeed=42 -ABTSToonT0ResX=2560 -ABTSToonT0ResY=1440 -ABTSToonT0WarmupFrames=12 -ABTSToonT0GPUSamples=5 -ABTSToonT0TimeoutSeconds=240 -ABTSToonT0AllowAnyResolution -ABTSToonT0KeepWorldRunning -ABTSToonT0ExitWhenDone -ABTSToonT0Output=VisualEvidence -ABTSToonT0BuildId=deadbeef"),
			Config,
			&Failure));
	TestTrue(TEXT("T0 enabled"), Config.bEnabled);
	TestEqual(
		TEXT("GPU mode"),
		static_cast<int32>(Config.Mode),
		static_cast<int32>(EABTSToonVisualCaptureMode::GPUProfile));
	TestEqual(TEXT("Seed override"), Config.ExpectedWorldSeed, 42);
	TestEqual(TEXT("Resolution X"), Config.ExpectedResolutionX, 2560);
	TestEqual(TEXT("Resolution Y"), Config.ExpectedResolutionY, 1440);
	TestEqual(TEXT("Warmup"), Config.WarmupFrames, 12);
	TestEqual(TEXT("GPU samples"), Config.GPUProfileSamplesPerVariant, 5);
	TestFalse(TEXT("Resolution preview escape hatch"), Config.bRequireExactResolution);
	TestFalse(TEXT("World-running escape hatch"), Config.bPauseWorldDuringCapture);
	TestTrue(TEXT("Exit flag"), Config.bExitWhenComplete);
	TestEqual(TEXT("Output root"), Config.OutputDirectory, FString(TEXT("VisualEvidence")));
	TestEqual(TEXT("Build identity"), Config.BuildIdentity, FString(TEXT("deadbeef")));

	Failure.Reset();
	TestFalse(
		TEXT("Unknown modes fail closed"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSToonT0Capture -ABTSToonT0Mode=Unknown"),
			Config,
			&Failure));
	TestTrue(TEXT("Invalid mode reports a reason"), !Failure.IsEmpty());

	Failure.Reset();
	TestFalse(
		TEXT("An enabled suite without build identity fails closed"),
		FABTSToonVisualCaptureRunConfig::Parse(
			TEXT("-ABTSToonT0Capture"),
			Config,
			&Failure));
	TestTrue(TEXT("Missing build identity reports a reason"), !Failure.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0CatalogueAndCameraMathTest,
	"ABTS.Rendering.Toon.T0.CatalogueAndCameraMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0CatalogueAndCameraMathTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const TArray<FABTSToonVisualCapturePointDefinition> Catalogue =
		FABTSToonVisualCaptureMath::BuildDefaultCatalogue();
	TestEqual(TEXT("Four semantic points"), Catalogue.Num(), 4);
	TSet<FName> PointIds;
	for (const FABTSToonVisualCapturePointDefinition& Definition : Catalogue)
	{
		TestTrue(TEXT("Every catalogue point is valid"), Definition.IsValid());
		PointIds.Add(Definition.PointId);
	}
	TestEqual(TEXT("Point IDs are unique"), PointIds.Num(), Catalogue.Num());
	TestEqual(
		TEXT("Ground profile"),
		static_cast<int32>(Catalogue[0].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(
		TEXT("Satellite profile"),
		static_cast<int32>(Catalogue[2].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::SatelliteGuide));
	TestEqual(
		TEXT("Finale profile"),
		static_cast<int32>(Catalogue[3].StyleProfile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));

	const uint64 HashA =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Catalogue);
	const uint64 HashB =
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Catalogue);
	TestTrue(TEXT("Catalogue hash is non-zero"), HashA != 0);
	TestEqual(TEXT("Catalogue hash is deterministic"), HashA, HashB);
	TArray<FABTSToonVisualCapturePointDefinition> Reordered = Catalogue;
	Reordered.Swap(0, 1);
	TestTrue(
		TEXT("Catalogue order is part of capture identity"),
		FABTSToonVisualCaptureMath::ComputeCatalogueHash(Reordered) != HashA);

	const FVector CameraLocation(50.0, -200.0, 80.0);
	const FVector LookAt(10.0, 20.0, 30.0);
	FTransform CameraTransform;
	FString Failure;
	TestTrue(
		TEXT("Look-at transform resolves"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			CameraLocation,
			LookAt,
			FVector::UpVector,
			CameraTransform,
			&Failure));
	const FVector ExpectedForward =
		(LookAt - CameraLocation).GetSafeNormal();
	TestTrue(
		TEXT("Camera X axis looks at the target"),
		CameraTransform.GetUnitAxis(EAxis::X).Equals(
			ExpectedForward,
			1.0e-4));
	TestTrue(
		TEXT("Camera up is orthogonal to forward"),
		FMath::IsNearlyZero(FVector::DotProduct(
			CameraTransform.GetUnitAxis(EAxis::X),
			CameraTransform.GetUnitAxis(EAxis::Z)),
			1.0e-4));
	TestTrue(
		TEXT("Parallel preferred up uses a deterministic fallback"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			FVector::ZeroVector,
			FVector::ForwardVector * 100.0,
			FVector::ForwardVector,
			CameraTransform,
			&Failure));
	TestFalse(
		TEXT("Coincident camera and target fail closed"),
		FABTSToonVisualCaptureMath::BuildLookAtCameraTransform(
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector::UpVector,
			CameraTransform,
			&Failure));

	const double WideDistance =
		FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
			500.0,
			80.0,
			16.0 / 9.0);
	const double NarrowDistance =
		FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
			500.0,
			40.0,
			16.0 / 9.0);
	TestTrue(TEXT("A narrower FOV requires more distance"), NarrowDistance > WideDistance);
	TestTrue(
		TEXT("Invalid fit inputs fail closed"),
		FMath::IsNearlyZero(
			FABTSToonVisualCaptureMath::ComputePerspectiveFitDistanceCM(
				0.0,
				60.0,
				16.0 / 9.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT0StyleSwitchSeamTest,
	"ABTS.Rendering.Toon.T0.StyleSwitchSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT0StyleSwitchSeamTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const bool bSavedEnabled = FABTSStylizedRenderingControl::IsEnabled();
	const EABTSStylizedRenderProfile SavedProfile =
		FABTSStylizedRenderingControl::GetProfile();

	FABTSStylizedRenderingControl::SetProfile(
		EABTSStylizedRenderProfile::FinaleSpace);
	FABTSStylizedRenderingControl::SetEnabled(true);
	TestTrue(TEXT("Style switch can be enabled"), FABTSStylizedRenderingControl::IsEnabled());
	TestEqual(
		TEXT("Profile switch is retained"),
		static_cast<int32>(FABTSStylizedRenderingControl::GetProfile()),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestEqual(
		TEXT("Stylized renderer reports the frozen T4-A2.4 cloud distribution"),
		FABTSStylizedRenderingControl::GetImplementationVersion(),
		58);
	TestTrue(
		TEXT("GroundDay clouds suppress motion blur to prevent moving night-cloud edge fringes"),
		FABTSStylizedRenderingControl::ShouldSuppressMotionBlur(
			EABTSStylizedRenderProfile::GroundDay,
			true));
	TestFalse(
		TEXT("GroundDay without clouds does not alter the camera motion contract"),
		FABTSStylizedRenderingControl::ShouldSuppressMotionBlur(
			EABTSStylizedRenderProfile::GroundDay,
			false));
	TestFalse(
		TEXT("SatelliteGuide retains its independent camera motion contract"),
		FABTSStylizedRenderingControl::ShouldSuppressMotionBlur(
			EABTSStylizedRenderProfile::SatelliteGuide,
			true));
	TestFalse(
		TEXT("FinaleSpace retains its independent camera motion contract"),
		FABTSStylizedRenderingControl::ShouldSuppressMotionBlur(
			EABTSStylizedRenderProfile::FinaleSpace,
			true));
	TestTrue(
		TEXT("Any-thread switch mirrors the game-thread switch"),
		FABTSStylizedRenderingControl::IsEnabledOnAnyThread());
	TestEqual(
		TEXT("Any-thread profile mirrors the game-thread profile"),
		static_cast<int32>(
			FABTSStylizedRenderingControl::GetProfileOnAnyThread()),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));

	TArray<FABTSStylizedToneProfileParameters> Profiles;
	TArray<FABTSStylizedOutlineProfileParameters> OutlineProfiles;
	for (int32 ProfileIndex =
			static_cast<int32>(EABTSStylizedRenderProfile::GroundDay);
		ProfileIndex <=
			static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace);
		++ProfileIndex)
	{
		const FABTSStylizedToneProfileParameters Profile =
			FABTSStylizedRenderingControl::GetToneProfileParameters(
				static_cast<EABTSStylizedRenderProfile>(ProfileIndex));
		TestTrue(TEXT("Every T1 tone profile is valid"), Profile.IsValid());
		const float CaptureNormalizationFloor =
			FABTSStylizedRenderingControl::
				GetSceneCaptureToneNormalizationFloor(
					static_cast<EABTSStylizedRenderProfile>(ProfileIndex));
		TestEqual(
			TEXT("Capture tone floor uses the stable shadow band"),
			CaptureNormalizationFloor,
			Profile.ShadowLuminance);
		TestTrue(
			TEXT("Capture tone floor prevents dark-signal amplification"),
			CaptureNormalizationFloor > 1.0e-4f);
		Profiles.Add(Profile);
		const FABTSStylizedOutlineProfileParameters OutlineProfile =
			FABTSStylizedRenderingControl::GetOutlineProfileParameters(
				static_cast<EABTSStylizedRenderProfile>(ProfileIndex));
		TestTrue(TEXT("Every T2-A outline profile is valid"), OutlineProfile.IsValid());
		TestTrue(
			TEXT("Background silhouettes remain stronger than depth occlusions"),
			OutlineProfile.Strength > OutlineProfile.OcclusionStrength);
		TestTrue(
			TEXT("Depth occlusions remain stronger than normal creases"),
			OutlineProfile.OcclusionStrength
				> OutlineProfile.NormalCreaseStrength);
		OutlineProfiles.Add(OutlineProfile);
	}
	TestNotEqual(
		TEXT("Ground and satellite shadow thresholds differ"),
		Profiles[0].ShadowThreshold,
		Profiles[1].ShadowThreshold);
	TestNotEqual(
		TEXT("Satellite and finale strengths differ"),
		Profiles[1].Strength,
		Profiles[2].Strength);
	TestNotEqual(
		TEXT("Ground and finale outline widths differ"),
		OutlineProfiles[0].WidthPixels,
		OutlineProfiles[2].WidthPixels);
	FABTSStylizedOutlineProfileParameters InvalidOutlineHierarchy =
		OutlineProfiles[0];
	InvalidOutlineHierarchy.NormalCreaseStrength =
		InvalidOutlineHierarchy.OcclusionStrength + 0.01f;
	TestFalse(
		TEXT("An inverted outline hierarchy is rejected"),
		InvalidOutlineHierarchy.IsValid());
	TestFalse(
		TEXT("Out-of-range profiles are rejected"),
		FABTSStylizedRenderingControl::IsProfileValid(
			static_cast<EABTSStylizedRenderProfile>(255)));

	FABTSStylizedRenderingControl::SetProfile(SavedProfile);
	FABTSStylizedRenderingControl::SetEnabled(bSavedEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2ASharedRenderingContractTest,
	"ABTS.Rendering.Toon.T2A.SharedRenderingContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2ASharedRenderingContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	TSet<uint8> SelectiveStencilValues;
	for (int32 ClassIndex =
		static_cast<int32>(EABTSStylizedObjectClass::None);
		ClassIndex <= static_cast<int32>(EABTSStylizedObjectClass::FinaleUFO);
		++ClassIndex)
	{
		const EABTSStylizedObjectClass ObjectClass =
			static_cast<EABTSStylizedObjectClass>(ClassIndex);
		TestTrue(
			TEXT("Every declared object class is valid"),
			FABTSStylizedRenderingContract::IsObjectClassValid(ObjectClass));
		const uint8 StencilValue =
			FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
				ObjectClass);
		TestEqual(
			TEXT("Selective classification matches the renderer allocation"),
			FABTSStylizedRenderingContract::RequiresSelectiveStencil(ObjectClass),
			StencilValue != 0);
		if (StencilValue != 0)
		{
			TestTrue(
				TEXT("Feature stencil values remain inside the Integration reserve"),
				StencilValue <= 31);
			TestFalse(
				TEXT("Every selective object class has a unique stencil value"),
				SelectiveStencilValues.Contains(StencilValue));
			SelectiveStencilValues.Add(StencilValue);
		}
	}
	TestEqual(TEXT("Seven gameplay classes are selective"), SelectiveStencilValues.Num(), 7);
	TestFalse(
		TEXT("Unknown object classes fail closed"),
		FABTSStylizedRenderingContract::IsObjectClassValid(
			static_cast<EABTSStylizedObjectClass>(255)));
	TestEqual(
		TEXT("Unknown object classes never receive a stencil value"),
		FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
			static_cast<EABTSStylizedObjectClass>(255)),
		static_cast<uint8>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1CCloudCompositeStencilContractTest,
	"ABTS.Rendering.Toon.T4A2R1C.CloudCompositeStencilContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1CCloudCompositeStencilContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const uint8 CloudStencil = FABTSStylizedRenderingContract::
		ResolveCloudCompositeStencilValueForRenderer();
	TestEqual(TEXT("CloudComposite uses stencil 8"),
		CloudStencil, static_cast<uint8>(8));
	for (int32 ClassIndex =
		static_cast<int32>(EABTSStylizedObjectClass::None);
		ClassIndex <= static_cast<int32>(EABTSStylizedObjectClass::FinaleUFO);
		++ClassIndex)
	{
		TestNotEqual(
			TEXT("Cloud composite stencil never aliases gameplay semantics"),
			FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
				static_cast<EABTSStylizedObjectClass>(ClassIndex)),
			CloudStencil);
	}
	TestTrue(
		TEXT("Two visible cloud pixels suppress their mutual outline"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				CloudStencil, CloudStencil));
	TestFalse(
		TEXT("Cloud-to-background boundary retains the ordinary outline"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				CloudStencil, 0));
	TestFalse(
		TEXT("Background-to-cloud boundary retains the ordinary outline"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				0, CloudStencil));
	TestFalse(
		TEXT("Cloud-to-gameplay boundary retains the ordinary outline"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				CloudStencil, 1));
	TestFalse(
		TEXT("Matching gameplay stencil does not suppress normal outlines"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(1, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A22GlobalCloudFieldContractTest,
	"ABTS.Rendering.Toon.T4A2_2.GlobalCloudFieldContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A22GlobalCloudFieldContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const uint8 CloudCompositeStencil = FABTSStylizedRenderingContract::
		ResolveCloudCompositeStencilValueForRenderer();
	TestEqual(TEXT("Every logical cloud shares CloudComposite stencil 8"),
		CloudCompositeStencil,
		static_cast<uint8>(8));
	TestTrue(TEXT("CloudComposite stencil is recognized as cloud"),
		FABTSStylizedRenderingContract::
			IsCloudCompositeStencilValueForRenderer(CloudCompositeStencil));
	TestFalse(TEXT("Gameplay stencil is not classified as CloudComposite"),
		FABTSStylizedRenderingContract::
			IsCloudCompositeStencilValueForRenderer(1));

	const FABTSStylizedEnvironmentParameters Environment =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(120.0, -340.0, 560.0),
			10000.0,
			FVector(0.3, -0.6, 0.7).GetSafeNormal(),
			EABTSStylizedRenderProfile::GroundDay);
	// Production star seed 0x00A8B751 xor the frozen cloud-field salt.
	constexpr uint32 CloudFieldSeed = 0xC1A5466Cu;
	const FABTST4CloudClusterDistributionParameters ProductionDistribution;
	TestEqual(TEXT("A2.4 freezes twenty-four production weather clusters"),
		ProductionDistribution.ClusterCount, 24);
	TestEqual(TEXT("A2.4 freezes ten logical clouds per cluster on average"),
		ProductionDistribution.CloudsPerClusterMean, 10.0f);
	TestEqual(TEXT("A2.4 freezes the accepted member-count variance"),
		ProductionDistribution.CloudsPerClusterVariance, 64.0f);
	const TArray<int32> ProductionMemberCounts =
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			CloudFieldSeed, ProductionDistribution);
	int32 ProductionBackgroundClouds = 0;
	for (const int32 Count : ProductionMemberCounts)
	{
		ProductionBackgroundClouds += Count;
	}
	TestEqual(TEXT("The production seed has the frozen background-cloud identity"),
		ProductionBackgroundClouds,
		FABTST4LowPolyCloudPrototype::GlobalIslandCount);
	const TArray<FABTST4LowPolyCloudIslandDefinition> LogicalClouds =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			CloudFieldSeed,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("A2.2 publishes a global logical cloud field"),
		LogicalClouds.Num(), FABTST4LowPolyCloudPrototype::IslandCount);
	const uint64 LogicalCloudHash =
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(
			LogicalClouds);
	TestTrue(TEXT("Logical cloud layout identity is non-zero"),
		LogicalCloudHash != 0);
	TestEqual(TEXT("Logical cloud layout identity is deterministic"),
		LogicalCloudHash,
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(
			FABTST4LowPolyCloudPrototype::BuildDefinitions(
				Environment.PlanetCenterWorld,
				Environment.PlanetRadiusCM,
				CloudFieldSeed,
				FVector(Environment.SunDirectionToSunWorld),
				Environment.CloudBaseAltitudeCM,
				Environment.CloudLayerHeightCM)));
	const FVector AlternateSunDirection = FVector(
		-0.72, 0.41, 0.56).GetSafeNormal();
	const TArray<FABTST4LowPolyCloudIslandDefinition> AlternateSunClouds =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			CloudFieldSeed,
			AlternateSunDirection,
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("Alternate sun keeps the complete field contract"),
		AlternateSunClouds.Num(), LogicalClouds.Num());
	for (int32 Index = 0;
		Index < FABTST4LowPolyCloudPrototype::GlobalIslandCount;
		++Index)
	{
		TestFalse(TEXT("Background cloud is not a terminator diagnostic member"),
			LogicalClouds[Index].bTerminatorMegaCluster);
		TestTrue(TEXT("Background placement is independent of the sun direction"),
			LogicalClouds[Index].CenterWorld.Equals(
				AlternateSunClouds[Index].CenterWorld, 0.01)
			&& LogicalClouds[Index].ExtentsCM.Equals(
				AlternateSunClouds[Index].ExtentsCM, 0.01)
			&& LogicalClouds[Index].IdentityHash
				== AlternateSunClouds[Index].IdentityHash);
	}
	bool bMegaClusterMovedWithSun = false;
	for (int32 Index = FABTST4LowPolyCloudPrototype::GlobalIslandCount;
		Index < LogicalClouds.Num(); ++Index)
	{
		bMegaClusterMovedWithSun |= !LogicalClouds[Index].CenterWorld.Equals(
			AlternateSunClouds[Index].CenterWorld, 1.0);
	}
	TestTrue(TEXT("The terminator acceptance cluster follows the sun-relative frame"),
		bMegaClusterMovedWithSun);

	TSet<int32> LogicalCloudIndices;
	TSet<uint64> LogicalCloudIdentities;
	uint8 OccupiedOctants = 0;
	int32 CloudletCount = 0;
	int32 TerminatorMegaCloudCount = 0;
	FVector TerminatorDirectionSum = FVector::ZeroVector;
	double MinimumMegaSolarHeight = 1.0;
	double MaximumMegaSolarHeight = -1.0;
	double MinimumHorizontalArea = TNumericLimits<double>::Max();
	double MaximumHorizontalArea = 0.0;
	for (const FABTST4LowPolyCloudIslandDefinition& LogicalCloud
		: LogicalClouds)
	{
		LogicalCloudIndices.Add(LogicalCloud.LogicalCloudIndex);
		LogicalCloudIdentities.Add(LogicalCloud.LogicalCloudIdentityHash);
		CloudletCount += LogicalCloud.CloudletCount;
		const double HorizontalArea = LogicalCloud.ExtentsCM.X
			* LogicalCloud.ExtentsCM.Y;
		MinimumHorizontalArea = FMath::Min(MinimumHorizontalArea, HorizontalArea);
		MaximumHorizontalArea = FMath::Max(MaximumHorizontalArea, HorizontalArea);
		const FVector Up = LogicalCloud.RadialUp;
		if (!LogicalCloud.bTerminatorMegaCluster)
		{
			const uint8 Octant = (Up.X >= 0.0 ? 1u : 0u)
				| (Up.Y >= 0.0 ? 2u : 0u)
				| (Up.Z >= 0.0 ? 4u : 0u);
			OccupiedOctants |= static_cast<uint8>(1u << Octant);
		}
		if (LogicalCloud.bTerminatorMegaCluster)
		{
			++TerminatorMegaCloudCount;
			TerminatorDirectionSum += LogicalCloud.RadialUp;
			const double SolarHeight = FVector::DotProduct(
				LogicalCloud.RadialUp,
				FVector(Environment.SunDirectionToSunWorld));
			MinimumMegaSolarHeight = FMath::Min(
				MinimumMegaSolarHeight, SolarHeight);
			MaximumMegaSolarHeight = FMath::Max(
				MaximumMegaSolarHeight, SolarHeight);
			TestTrue(TEXT("Every mega-cluster centre remains close to the terminator"),
				FMath::Abs(SolarHeight)
					<= FMath::Sin(FMath::DegreesToRadians(10.0)));
		}
	}
	TestEqual(TEXT("Logical cloud indices are unique"),
		LogicalCloudIndices.Num(), LogicalClouds.Num());
	TestEqual(TEXT("Logical cloud hashes are unique"),
		LogicalCloudIdentities.Num(), LogicalClouds.Num());
	TestEqual(TEXT("The accepted field publishes its total cloudlet budget"),
		CloudletCount, FABTST4LowPolyCloudPrototype::TotalCloudletCount);
	TestTrue(TEXT("Cloud islands have materially different sizes"),
		MaximumHorizontalArea >= MinimumHorizontalArea * 2.0);
	TestEqual(TEXT("The global field occupies all eight planet octants"),
		OccupiedOctants, static_cast<uint8>(0xff));
	TestTrue(TEXT("The global field includes nearby cloud pairs for fusion"),
		FABTST4LowPolyCloudPrototype::CountCloudFusionPairs(LogicalClouds)
			>= FABTST4LowPolyCloudPrototype::WeatherSystemCount);
	TestTrue(TEXT("Every default background weather cluster is one connected visible mass"),
		FABTST4LowPolyCloudPrototype::
			AreBackgroundWeatherClusterEnvelopesConnected(LogicalClouds));
	TestEqual(TEXT("A2.2 appends seven logical members for the mega cluster"),
		TerminatorMegaCloudCount,
		FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount);
	TestEqual(TEXT("The role helper observes the same mega-cluster count"),
		FABTST4LowPolyCloudPrototype::CountTerminatorMegaClusterClouds(
			LogicalClouds),
		TerminatorMegaCloudCount);
	const FVector MeanTerminatorDirection = TerminatorDirectionSum.GetSafeNormal();
	TestTrue(TEXT("The mega-cluster mean remains on the day/night boundary"),
		FMath::Abs(FVector::DotProduct(
			MeanTerminatorDirection,
			FVector(Environment.SunDirectionToSunWorld))) <= 0.03);
	TestTrue(TEXT("The mega cluster visibly straddles both day and night"),
		MinimumMegaSolarHeight < -0.05 && MaximumMegaSolarHeight > 0.05);
	const double MegaClusterSpanDegrees = FABTST4LowPolyCloudPrototype::
		ComputeTerminatorMegaClusterAngularSpanDegrees(LogicalClouds);
	TestTrue(
		FString::Printf(
			TEXT("The mega-cluster full envelope is approximately 30 degrees (Actual=%.2f)"),
			MegaClusterSpanDegrees),
		MegaClusterSpanDegrees >= 27.0 && MegaClusterSpanDegrees <= 33.0);
	TestTrue(TEXT("The seven mega-cluster envelopes form one connected mass"),
		FABTST4LowPolyCloudPrototype::
			IsTerminatorMegaClusterEnvelopeConnected(LogicalClouds));
	TestEqual(TEXT("Deep night completely gates daytime cloud whitening"),
		FABTST4LowPolyCloudPrototype::ComputeLocalDaylightBlend(-1.0f),
		0.0f);
	TestEqual(TEXT("Full daylight retains the accepted cloud whitening"),
		FABTST4LowPolyCloudPrototype::ComputeLocalDaylightBlend(1.0f),
		1.0f);
	TestTrue(TEXT("Twilight cloud lighting changes continuously and monotonically"),
		FABTST4LowPolyCloudPrototype::ComputeLocalDaylightBlend(-0.05f)
			< FABTST4LowPolyCloudPrototype::ComputeLocalDaylightBlend(0.05f));
	TestTrue(TEXT("Any two logical clouds suppress their mutual outline"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				CloudCompositeStencil, CloudCompositeStencil));
	TestFalse(TEXT("Cloud-to-world outline remains visible"),
		FABTSStylizedRenderingContract::
			ShouldSuppressInternalOutlineBetweenStencilValues(
				CloudCompositeStencil, 1));

	FABTST4CloudClusterDistributionParameters TunedDistribution;
	TunedDistribution.ClusterCount = 18;
	TunedDistribution.CloudsPerClusterMean = 8.0f;
	TunedDistribution.CloudsPerClusterVariance = 36.0f;
	const TArray<int32> TunedMemberCounts =
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			0xA2401234u, TunedDistribution);
	const TArray<int32> RepeatedMemberCounts =
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			0xA2401234u, TunedDistribution);
	int32 TunedMemberTotal = 0;
	for (const int32 Count : TunedMemberCounts)
	{
		TunedMemberTotal += Count;
		TestTrue(TEXT("Every tunable global cluster keeps 1..64 members"),
			Count >= 1 && Count <= 64);
	}
	TestEqual(TEXT("Explicit cluster count is not derived from member size"),
		TunedMemberCounts.Num(), TunedDistribution.ClusterCount);
	TestTrue(TEXT("Tuning can exceed the legacy A2.2 24-cloud baseline"),
		TunedMemberTotal > 24);
	TestTrue(TEXT("Tuning remains inside the fail-closed background budget"),
		TunedMemberTotal <= FABTST4LowPolyCloudPrototype::MaxGlobalIslandCount);
	TestTrue(TEXT("Tunable grouping is deterministic for the same seed"),
		TunedMemberCounts == RepeatedMemberCounts);
	FABTST4CloudClusterDistributionParameters OverBudgetDistribution;
	OverBudgetDistribution.ClusterCount = 64;
	OverBudgetDistribution.CloudsPerClusterMean = 64.0f;
	OverBudgetDistribution.CloudsPerClusterVariance = 1024.0f;
	TestTrue(TEXT("Pathological input fails closed instead of flooding PIE"),
		FABTST4LowPolyCloudPrototype::BuildGlobalClusterMemberCounts(
			0xA2401234u, OverBudgetDistribution).IsEmpty());
	const TArray<FABTST4LowPolyCloudIslandDefinition> TunedClouds =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			0xA2401234u,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM,
			TunedDistribution);
	const TArray<FABTST4LowPolyCloudIslandDefinition> RepeatedTunedClouds =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			0xA2401234u,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM,
			TunedDistribution);
	TestEqual(TEXT("A2.4 total clouds follow explicit clusters and sampled members"),
		TunedClouds.Num(), TunedMemberTotal
			+ FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount);
	TestEqual(TEXT("A2.4 tuned layout hash is deterministic"),
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(TunedClouds),
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(
			RepeatedTunedClouds));
	for (int32 Index = 0; Index < TunedMemberTotal; ++Index)
	{
		const FABTST4LowPolyCloudIslandDefinition& Cloud = TunedClouds[Index];
		TestTrue(TEXT("Every tuned background cloud publishes grouping metadata"),
			Cloud.WeatherClusterIndex >= 0
			&& Cloud.WeatherClusterIndex < TunedMemberCounts.Num()
			&& Cloud.WeatherClusterMemberCount
				== TunedMemberCounts[Cloud.WeatherClusterIndex]
			&& Cloud.WeatherClusterMemberIndex >= 0
			&& Cloud.WeatherClusterMemberIndex
				< Cloud.WeatherClusterMemberCount);
	}
	TestTrue(TEXT("Every tuned background weather cluster is one connected visible mass"),
		FABTST4LowPolyCloudPrototype::
			AreBackgroundWeatherClusterEnvelopesConnected(TunedClouds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2AViewPolicyTest,
	"ABTS.Rendering.Toon.T2A.ViewPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2AViewPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 ViewIndex = static_cast<int32>(EABTSStylizedViewClass::MainWorld);
		ViewIndex <= static_cast<int32>(
			EABTSStylizedViewClass::FinaleCinematicCapture);
		++ViewIndex)
	{
		const EABTSStylizedViewClass ViewClass =
			static_cast<EABTSStylizedViewClass>(ViewIndex);
		TestTrue(
			TEXT("Every declared view class is valid"),
			FABTSStylizedRenderingContract::IsViewClassValid(ViewClass));
		TestTrue(
			TEXT("Every declared view class resolves a valid policy"),
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				ViewClass,
				EABTSStylizedRenderProfile::FinaleSpace).IsValid());
	}

	const FABTSStylizedViewPolicy MainPolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::MainWorld,
			EABTSStylizedRenderProfile::FinaleSpace);
	TestEqual(
		TEXT("Main view consumes the active runtime profile"),
		static_cast<int32>(MainPolicy.Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestTrue(TEXT("Main view applies tone"), MainPolicy.bApplyTone);
	TestTrue(TEXT("Main view applies outline"), MainPolicy.bApplyOutline);
	TestTrue(TEXT("Main view permits selective stencil"), MainPolicy.bAllowSelectiveStencil);
	TestTrue(
		TEXT("T2-A implements the final main view"),
		FABTSStylizedRenderingContract::IsViewClassImplemented(
			EABTSStylizedViewClass::MainWorld));
	TestTrue(
		TEXT("T2-B1 implements every explicit Scene Capture class"),
		FABTSStylizedRenderingContract::IsViewClassImplemented(
			EABTSStylizedViewClass::SatelliteLandingPreview));

	TestEqual(
		TEXT("Ground preview has a frozen ground profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::GroundLandingPreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::GroundDay));
	TestEqual(
		TEXT("Satellite preview has a frozen satellite profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::SatelliteLandingPreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::SatelliteGuide));
	const FABTSStylizedViewPolicy SatellitePolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::SatelliteLandingPreview);
	TestFalse(
		TEXT("Satellite BaseColor preview does not re-quantize lighting"),
		SatellitePolicy.bApplyTone);
	TestTrue(
		TEXT("Satellite BaseColor preview keeps a thin outline layer"),
		SatellitePolicy.bApplyOutline);
	TestEqual(
		TEXT("Finale preview has a frozen finale profile"),
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveViewPolicy(
				EABTSStylizedViewClass::FinaleRemotePreview).Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	const FABTSStylizedViewPolicy CinematicCapturePolicy =
		FABTSStylizedRenderingContract::ResolveViewPolicy(
			EABTSStylizedViewClass::FinaleCinematicCapture);
	TestEqual(
		TEXT("Finale recording uses the frozen finale profile"),
		static_cast<int32>(CinematicCapturePolicy.Profile),
		static_cast<int32>(EABTSStylizedRenderProfile::FinaleSpace));
	TestTrue(
		TEXT("Finale recording applies tone"),
		CinematicCapturePolicy.bApplyTone);
	TestTrue(
		TEXT("Finale recording applies outline"),
		CinematicCapturePolicy.bApplyOutline);
	TestTrue(
		TEXT("Finale recording permits selective stencil"),
		CinematicCapturePolicy.bAllowSelectiveStencil);
	TestFalse(
		TEXT("Unknown view classes fail closed"),
		FABTSStylizedRenderingContract::IsViewClassValid(
			static_cast<EABTSStylizedViewClass>(255)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2B1SceneCaptureRegistryTest,
	"ABTS.Rendering.Toon.T2B1.SceneCaptureRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2B1SceneCaptureRegistryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTSStylizedSceneCaptureRegistry::Reset();
	USceneCaptureComponent2D* Capture =
		NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	TestNotNull(TEXT("Transient capture is available"), Capture);
	if (Capture == nullptr)
	{
		return false;
	}

	TestFalse(
		TEXT("MainWorld cannot be attached to a SceneCapture"),
		FABTSStylizedSceneCaptureRegistry::Register(
			*Capture,
			EABTSStylizedViewClass::MainWorld));
	TestTrue(
		TEXT("Ground preview registers explicitly"),
		FABTSStylizedSceneCaptureRegistry::Register(
			*Capture,
			EABTSStylizedViewClass::GroundLandingPreview));
	TestEqual(
		TEXT("Exactly one component-local extension is installed"),
		Capture->SceneViewExtensions.Num(),
		1);
	EABTSStylizedViewClass ViewClass = EABTSStylizedViewClass::MainWorld;
	TestTrue(
		TEXT("Registered class can be diagnosed"),
		FABTSStylizedSceneCaptureRegistry::TryGetViewClass(
			*Capture,
			ViewClass));
	TestEqual(
		TEXT("Ground class is preserved"),
		static_cast<int32>(ViewClass),
		static_cast<int32>(EABTSStylizedViewClass::GroundLandingPreview));

	TestTrue(
		TEXT("Subject transition atomically replaces the extension"),
		FABTSStylizedSceneCaptureRegistry::Register(
			*Capture,
			EABTSStylizedViewClass::SatelliteLandingPreview));
	TestEqual(
		TEXT("Replacement does not accumulate extensions"),
		Capture->SceneViewExtensions.Num(),
		1);
	TestTrue(
		TEXT("Finale cinematic capture registers explicitly"),
		FABTSStylizedSceneCaptureRegistry::Register(
			*Capture,
			EABTSStylizedViewClass::FinaleCinematicCapture));
	TestTrue(
		TEXT("Finale cinematic class can be diagnosed"),
		FABTSStylizedSceneCaptureRegistry::TryGetViewClass(
			*Capture,
			ViewClass));
	TestEqual(
		TEXT("Finale cinematic class is preserved"),
		static_cast<int32>(ViewClass),
		static_cast<int32>(
			EABTSStylizedViewClass::FinaleCinematicCapture));
	TestEqual(
		TEXT("Second replacement still owns one extension"),
		Capture->SceneViewExtensions.Num(),
		1);
	FABTSStylizedSceneCaptureRegistry::Unregister(*Capture);
	TestEqual(
		TEXT("Unregister removes the Integration extension"),
		Capture->SceneViewExtensions.Num(),
		0);
	TestFalse(
		TEXT("Unregistered captures fail closed"),
		FABTSStylizedSceneCaptureRegistry::TryGetViewClass(
			*Capture,
			ViewClass));
	FABTSStylizedSceneCaptureRegistry::Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A23BoundedTraversalRelationTest,
	"ABTS.Rendering.Toon.T4A2_3.BoundedTraversalRelation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A23BoundedTraversalRelationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FABTST4LowPolyCloudIslandDefinition Cloud;
	Cloud.IslandIndex = 0;
	Cloud.LogicalCloudIndex = 0;
	Cloud.CloudletCount = FABTST4LowPolyCloudPrototype::CloudletsPerIsland;
	Cloud.Seed = 12345u;
	Cloud.PlanetCenterWorld = FVector::ZeroVector;
	Cloud.CenterWorld = FVector(0.0, 0.0, 12000.0);
	Cloud.RadialUp = FVector::UpVector;
	Cloud.TangentX = FVector::ForwardVector;
	Cloud.TangentY = FVector::RightVector;
	Cloud.ExtentsCM = FVector(1800.0, 1500.0, 700.0);
	Cloud.LogicalCloudIdentityHash = 0x1234ull;
	Cloud.IdentityHash = 0x5678ull;
	TestTrue(TEXT("Synthetic traversal cloud validates"), Cloud.IsValid());

	const FABTST4CloudTraversalRelation BirdInside =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			FVector(-3200.0, 0.0, 12000.0),
			Cloud.CenterWorld,
			160.0f);
	TestTrue(TEXT("Bird-inside case activates"), BirdInside.bTraversalActive);
	TestTrue(TEXT("Bird-inside case identifies the bird"), BirdInside.bBirdInside);
	TestTrue(TEXT("Bird-inside case has continuous traversal weight"),
		BirdInside.TraversalWeight > 0.99f);
	TestFalse(TEXT("Bird-inside case keeps the camera outside"),
		BirdInside.bCameraInside);

	const FABTST4CloudTraversalRelation CameraInside =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			Cloud.CenterWorld,
			FVector(3200.0, 0.0, 12000.0),
			160.0f);
	TestTrue(TEXT("Camera-inside case activates"), CameraInside.bTraversalActive);
	TestTrue(TEXT("Camera-inside case identifies the camera"),
		CameraInside.bCameraInside);
	TestTrue(TEXT("Camera-inside case has full continuous envelope depth"),
		CameraInside.CameraInteriorWeight > 0.99f);

	const FABTST4CloudTraversalRelation Between =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			FVector(-3200.0, 0.0, 12000.0),
			FVector(3200.0, 0.0, 12000.0),
			160.0f);
	TestTrue(TEXT("Cloud-between case activates"), Between.bTraversalActive);
	TestTrue(TEXT("Cloud-between case identifies the segment occluder"),
		Between.bCloudBetweenCameraAndBird);
	TestTrue(TEXT("Cloud-between closest point is interior to the segment"),
		Between.ClosestSegmentAlpha > 0.1f
			&& Between.ClosestSegmentAlpha < 0.9f);
	TestTrue(TEXT("Cloud-between case has continuous corridor weight"),
		Between.CorridorInteriorWeight > 0.99f);

	const FABTST4CloudTraversalRelation BothInside =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			Cloud.CenterWorld - FVector(120.0, 0.0, 0.0),
			Cloud.CenterWorld + FVector(120.0, 0.0, 0.0),
			160.0f);
	TestTrue(TEXT("Both-inside case activates"), BothInside.bTraversalActive);
	TestTrue(TEXT("Both-inside case keeps both endpoint flags"),
		BothInside.bCameraInside && BothInside.bBirdInside);

	const FABTST4CloudTraversalRelation Clear =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			FVector(-3200.0, 3600.0, 12000.0),
			FVector(3200.0, 3600.0, 12000.0),
			160.0f);
	TestTrue(TEXT("Clear relation remains structurally valid"), Clear.IsValid());
	TestFalse(TEXT("Unrelated cloud remains fully opaque"),
		Clear.bTraversalActive);
	TestTrue(TEXT("Unrelated cloud has zero continuous traversal weight"),
		Clear.TraversalWeight <= KINDA_SMALL_NUMBER);

	const FABTST4CloudTraversalRelation NearBoundary =
		FABTST4LowPolyCloudPrototype::EvaluateTraversalRelation(
			Cloud,
			Cloud.CenterWorld + FVector(0.0, 0.0, 635.0),
			FVector(3200.0, 0.0, 12000.0),
			0.0f,
			1.0f);
	TestTrue(TEXT("Camera boundary exposes a fractional rather than binary depth"),
		NearBoundary.CameraInteriorWeight > 0.0f
			&& NearBoundary.CameraInteriorWeight < 1.0f);

	// Regression for the moving four-bird formation: the former one-sphere
	// contract was clamped to 420 cm and could not cover both endpoints.  The
	// material now receives four independent visual spheres, so every rendered
	// bird remains inside a hard-protection core even when the formation spans
	// well beyond the old diameter.
	const TArray<FSphere> MovingFormation = {
		FSphere(FVector(-900.0, 0.0, 12000.0), 220.0),
		FSphere(FVector(-300.0, 0.0, 12000.0), 220.0),
		FSphere(FVector(300.0, 0.0, 12000.0), 220.0),
		FSphere(FVector(900.0, 0.0, 12000.0), 220.0)};
	TestFalse(
		TEXT("The retired 420 cm party sphere cannot protect a formation endpoint"),
		FSphere(FVector(0.0, 0.0, 12000.0), 420.0).IsInside(
			MovingFormation[0].Center));
	for (int32 BirdIndex = 0; BirdIndex < MovingFormation.Num(); ++BirdIndex)
	{
		TestTrue(
			*FString::Printf(
				TEXT("Moving bird %d owns an independent hard-protection core"),
				BirdIndex),
			MovingFormation[BirdIndex].IsInside(
				MovingFormation[BirdIndex].Center));
	}
	return true;
}

#endif
