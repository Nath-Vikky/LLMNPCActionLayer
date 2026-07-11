#pragma once

#include "Modules/ModuleManager.h"

class SDockTab;
class FSpawnTabArgs;

class FLLMNPCActionLayerEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenProviderSettings();
	TSharedRef<SDockTab> SpawnProviderSettingsTab(const FSpawnTabArgs& SpawnTabArgs);
};
