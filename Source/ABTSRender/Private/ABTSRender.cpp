// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Rendering/ABTSStylizedToneViewExtension.h"

class FABTSRenderModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this,
			&FABTSRenderModule::HandlePostEngineInit);
	}

	virtual void ShutdownModule() override
	{
		if (PostEngineInitHandle.IsValid())
		{
			FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
			PostEngineInitHandle.Reset();
		}
		ABTSStylizedToneViewExtension::Shutdown();
	}

private:
	void HandlePostEngineInit()
	{
		ABTSStylizedToneViewExtension::Initialize();
	}

	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FABTSRenderModule, ABTSRender)
