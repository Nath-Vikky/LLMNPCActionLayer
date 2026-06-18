#include "Modules/ModuleManager.h"

class FLLMNPCActionLayerUncookedModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FLLMNPCActionLayerUncookedModule, LLMNPCActionLayerUncooked)
