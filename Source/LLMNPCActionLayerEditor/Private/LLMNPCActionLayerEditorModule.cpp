#include "Modules/ModuleManager.h"

class FLLMNPCActionLayerEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FLLMNPCActionLayerEditorModule, LLMNPCActionLayerEditor)
