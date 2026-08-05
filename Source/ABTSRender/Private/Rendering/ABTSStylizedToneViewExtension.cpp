// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedToneViewExtension.h"

#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "SceneRenderTargetParameters.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace ABTSStylizedToneViewExtensionPrivate
{
	BEGIN_SHADER_PARAMETER_STRUCT(FABTSStylizedCompositePassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
		SHADER_PARAMETER(FVector2f, ViewportInvSize)
		SHADER_PARAMETER(float, ShadowThreshold)
		SHADER_PARAMETER(float, HighlightThreshold)
		SHADER_PARAMETER(float, TransitionSoftness)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, ShadowLuminance)
		SHADER_PARAMETER(float, MidLuminance)
		SHADER_PARAMETER(float, HighlightLuminance)
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(FVector3f, ShadowTint)
		SHADER_PARAMETER(FVector3f, MidTint)
		SHADER_PARAMETER(FVector3f, HighlightTint)
		SHADER_PARAMETER(float, OutlineWidthPixels)
		SHADER_PARAMETER(float, OutlineDepthThreshold)
		SHADER_PARAMETER(float, OutlineDepthSoftness)
		SHADER_PARAMETER(float, OutlineNormalThreshold)
		SHADER_PARAMETER(float, OutlineNormalSoftness)
		SHADER_PARAMETER(float, OutlineStrength)
		SHADER_PARAMETER(FVector3f, OutlineColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FABTSStylizedCompositePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FABTSStylizedCompositePS);
		SHADER_USE_PARAMETER_STRUCT(FABTSStylizedCompositePS, FGlobalShader);
		using FParameters = FABTSStylizedCompositePassParameters;

		static bool ShouldCompilePermutation(
			const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(
				Parameters.Platform,
				ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(
		FABTSStylizedCompositePS,
		"/Project/Private/ABTSStylizedTone.usf",
		"ABTSStylizedToneAndOutlineMainPS",
		SF_Pixel);

	class FABTSStylizedToneSceneViewExtension final
		: public FSceneViewExtensionBase
	{
	public:
		FABTSStylizedToneSceneViewExtension(const FAutoRegister& AutoRegister)
			: FSceneViewExtensionBase(AutoRegister)
		{
		}

		virtual void SubscribeToPostProcessingPass(
			EPostProcessingPass Pass,
			const FSceneView& InView,
			FPostProcessingPassDelegateArray& InOutPassCallbacks,
			bool bIsPassEnabled) override
		{
			if (Pass != EPostProcessingPass::Tonemap
				|| !bIsPassEnabled
				|| !FABTSStylizedRenderingControl::IsEnabledOnAnyThread()
				|| InView.bIsSceneCapture
				|| InView.bIsReflectionCapture
				|| InView.bIsPlanarReflection)
			{
				return;
			}

			const FABTSStylizedViewPolicy ViewPolicy =
				FABTSStylizedRenderingContract::ResolveViewPolicy(
					EABTSStylizedViewClass::MainWorld,
					FABTSStylizedRenderingControl::GetProfileOnAnyThread());
			if (!ViewPolicy.IsValid()
				|| !FABTSStylizedRenderingContract::IsViewClassImplemented(
					EABTSStylizedViewClass::MainWorld))
			{
				return;
			}

			const FABTSStylizedToneProfileParameters ToneProfile =
				FABTSStylizedRenderingControl::GetToneProfileParameters(
					ViewPolicy.Profile);
			const FABTSStylizedOutlineProfileParameters OutlineProfile =
				FABTSStylizedRenderingControl::GetOutlineProfileParameters(
					ViewPolicy.Profile);
			InOutPassCallbacks.AddDefaulted_GetRef().BindLambda(
				[ToneProfile, OutlineProfile](
					FRDGBuilder& GraphBuilder,
					const FSceneView& View,
					const FPostProcessMaterialInputs& Inputs)
				{
					return AddCompositePass(
						GraphBuilder,
						View,
						Inputs,
						ToneProfile,
						OutlineProfile);
				});
		}

	private:
		static FScreenPassTexture AddCompositePass(
			FRDGBuilder& GraphBuilder,
			const FSceneView& View,
			const FPostProcessMaterialInputs& Inputs,
			const FABTSStylizedToneProfileParameters& ToneProfile,
			const FABTSStylizedOutlineProfileParameters& OutlineProfile)
		{
			const FScreenPassTexture SceneColor(
				Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
			check(SceneColor.IsValid());

			FScreenPassRenderTarget Output = Inputs.OverrideOutput;
			if (!Output.IsValid())
			{
				Output = FScreenPassRenderTarget::CreateFromInput(
					GraphBuilder,
					SceneColor,
					ERenderTargetLoadAction::ENoAction,
					TEXT("ABTSStylizedComposite"));
			}

			const FScreenPassTextureViewport InputViewport(SceneColor);
			const FScreenPassTextureViewport OutputViewport(Output);
			FABTSStylizedCompositePassParameters* PassParameters =
				GraphBuilder.AllocParameters<FABTSStylizedCompositePassParameters>();
			PassParameters->SceneTextures = GetSceneTextureShaderParameters(View);
			PassParameters->SceneColorTexture = SceneColor.Texture;
			PassParameters->SceneColorSampler =
				TStaticSamplerState<
					SF_Bilinear,
					AM_Clamp,
					AM_Clamp,
					AM_Clamp>::GetRHI();
			PassParameters->ViewportInvSize = FVector2f(
				1.0f / static_cast<float>(FMath::Max(OutputViewport.Rect.Width(), 1)),
				1.0f / static_cast<float>(FMath::Max(OutputViewport.Rect.Height(), 1)));
			PassParameters->ShadowThreshold = ToneProfile.ShadowThreshold;
			PassParameters->HighlightThreshold = ToneProfile.HighlightThreshold;
			PassParameters->TransitionSoftness = ToneProfile.TransitionSoftness;
			PassParameters->Strength = ToneProfile.Strength;
			PassParameters->ShadowLuminance = ToneProfile.ShadowLuminance;
			PassParameters->MidLuminance = ToneProfile.MidLuminance;
			PassParameters->HighlightLuminance = ToneProfile.HighlightLuminance;
			PassParameters->Saturation = ToneProfile.Saturation;
			PassParameters->ShadowTint = ToneProfile.ShadowTint;
			PassParameters->MidTint = ToneProfile.MidTint;
			PassParameters->HighlightTint = ToneProfile.HighlightTint;
			PassParameters->OutlineWidthPixels = OutlineProfile.WidthPixels;
			PassParameters->OutlineDepthThreshold = OutlineProfile.DepthThreshold;
			PassParameters->OutlineDepthSoftness = OutlineProfile.DepthSoftness;
			PassParameters->OutlineNormalThreshold = OutlineProfile.NormalThreshold;
			PassParameters->OutlineNormalSoftness = OutlineProfile.NormalSoftness;
			PassParameters->OutlineStrength = OutlineProfile.Strength;
			PassParameters->OutlineColor = OutlineProfile.Color;
			PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

			const TShaderMapRef<FABTSStylizedCompositePS> PixelShader(
				GetGlobalShaderMap(View.GetFeatureLevel()));
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("ABTS Stylized ToneAndOutline"),
				View,
				OutputViewport,
				InputViewport,
				PixelShader,
				PassParameters);
			return MoveTemp(Output);
		}
	};

	TSharedPtr<FABTSStylizedToneSceneViewExtension, ESPMode::ThreadSafe>
		GViewExtension;
}

void ABTSStylizedToneViewExtension::Initialize()
{
	using namespace ABTSStylizedToneViewExtensionPrivate;
	if (!GViewExtension.IsValid())
	{
		GViewExtension =
			FSceneViewExtensions::NewExtension<
				FABTSStylizedToneSceneViewExtension>();
	}
}

void ABTSStylizedToneViewExtension::Shutdown()
{
	ABTSStylizedToneViewExtensionPrivate::GViewExtension.Reset();
}
