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
	void OpenMotionTestConsole();
	void OpenTemplateWorkbench();
	TSharedRef<SDockTab> SpawnProviderSettingsTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> SpawnMotionTestConsoleTab(const FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> SpawnTemplateWorkbenchTab(const FSpawnTabArgs& SpawnTabArgs);
};
