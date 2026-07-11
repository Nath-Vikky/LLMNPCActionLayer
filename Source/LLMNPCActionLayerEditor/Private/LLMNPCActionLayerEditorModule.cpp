#include "LLMNPCActionLayerEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SLLMNPCProviderSettings.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FLLMNPCActionLayerEditorModule"

namespace
{
const FName ProviderSettingsTabName(TEXT("LLMNPCProviderSettings"));
}

void FLLMNPCActionLayerEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ProviderSettingsTabName,
		FOnSpawnTab::CreateRaw(this, &FLLMNPCActionLayerEditorModule::SpawnProviderSettingsTab)
	)
		.SetDisplayName(LOCTEXT("ProviderSettingsTabTitle", "LLM NPC Provider"))
		.SetTooltipText(LOCTEXT("ProviderSettingsTabTooltip", "Configure and test LLM NPC model providers."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLLMNPCActionLayerEditorModule::RegisterMenus)
	);
}

void FLLMNPCActionLayerEditorModule::ShutdownModule()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ProviderSettingsTabName);
}

void FLLMNPCActionLayerEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"))
	{
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("LLMNPCActionLayer");
		Section.AddMenuEntry(
			"LLMNPC_OpenProviderSettings",
			LOCTEXT("OpenProviderSettings", "LLM NPC Provider Settings"),
			LOCTEXT("OpenProviderSettingsTooltip", "Configure and test LLM NPC model providers."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
			FUIAction(FExecuteAction::CreateRaw(this, &FLLMNPCActionLayerEditorModule::OpenProviderSettings))
		);
	}
}

void FLLMNPCActionLayerEditorModule::OpenProviderSettings()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ProviderSettingsTabName);
}

TSharedRef<SDockTab> FLLMNPCActionLayerEditorModule::SpawnProviderSettingsTab(const FSpawnTabArgs& SpawnTabArgs)
{
	static_cast<void>(SpawnTabArgs);
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SLLMNPCProviderSettings)
		];
}

IMPLEMENT_MODULE(FLLMNPCActionLayerEditorModule, LLMNPCActionLayerEditor)

#undef LOCTEXT_NAMESPACE
