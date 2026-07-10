#pragma once

#include "Modules/ModuleManager.h"

class FLLMNPCActionLayerEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
