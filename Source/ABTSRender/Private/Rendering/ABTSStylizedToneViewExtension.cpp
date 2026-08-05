// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedToneViewExtension.h"

#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace ABTSStylizedToneViewExtensionPrivate
{
	BEGIN_SHADER_PARAMETER_STRUCT(FABTSStylizedTonePassParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
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
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FABTSStylizedTonePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FABTSStylizedTonePS);
		SHADER_USE_PARAMETER_STRUCT(FABTSStylizedTonePS, FGlobalShader);
		using FParameters = FABTSStylizedTonePassParameters;

		static bool ShouldCompilePermutation(
			const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(
				Parameters.Platform,
				ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(
		FABTSStylizedTonePS,
		"/Project/Private/ABTSStylizedTone.usf",
		"ABTSStylizedToneMainPS",
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
			(void)InView;
			if (Pass != EPostProcessingPass::Tonemap
				|| !bIsPassEnabled
				|| !FABTSStylizedRenderingControl::IsEnabledOnAnyThread())
			{
				return;
			}

			const FABTSStylizedToneProfileParameters Profile =
				FABTSStylizedRenderingControl::GetToneProfileParameters(
					FABTSStylizedRenderingControl::GetProfileOnAnyThread());
			InOutPassCallbacks.AddDefaulted_GetRef().BindLambda(
				[Profile](
					FRDGBuilder& GraphBuilder,
					const FSceneView& View,
					const FPostProcessMaterialInputs& Inputs)
				{
					return AddTonePass(GraphBuilder, View, Inputs, Profile);
				});
		}

	private:
		static FScreenPassTexture AddTonePass(
			FRDGBuilder& GraphBuilder,
			const FSceneView& View,
			const FPostProcessMaterialInputs& Inputs,
			const FABTSStylizedToneProfileParameters& Profile)
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
					TEXT("ABTSStylizedTone"));
			}

			FABTSStylizedTonePassParameters* PassParameters =
				GraphBuilder.AllocParameters<FABTSStylizedTonePassParameters>();
			PassParameters->SceneColorTexture = SceneColor.Texture;
			PassParameters->SceneColorSampler =
				TStaticSamplerState<
					SF_Bilinear,
					AM_Clamp,
					AM_Clamp,
					AM_Clamp>::GetRHI();
			PassParameters->ShadowThreshold = Profile.ShadowThreshold;
			PassParameters->HighlightThreshold = Profile.HighlightThreshold;
			PassParameters->TransitionSoftness = Profile.TransitionSoftness;
			PassParameters->Strength = Profile.Strength;
			PassParameters->ShadowLuminance = Profile.ShadowLuminance;
			PassParameters->MidLuminance = Profile.MidLuminance;
			PassParameters->HighlightLuminance = Profile.HighlightLuminance;
			PassParameters->Saturation = Profile.Saturation;
			PassParameters->ShadowTint = Profile.ShadowTint;
			PassParameters->MidTint = Profile.MidTint;
			PassParameters->HighlightTint = Profile.HighlightTint;
			PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

			const FScreenPassTextureViewport InputViewport(SceneColor);
			const FScreenPassTextureViewport OutputViewport(Output);
			const TShaderMapRef<FABTSStylizedTonePS> PixelShader(
				GetGlobalShaderMap(View.GetFeatureLevel()));
			AddDrawScreenPass(
				GraphBuilder,
				RDG_EVENT_NAME("ABTS Stylized Tone"),
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
