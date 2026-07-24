#include "LLMNPCActionLayerEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SLLMNPCMotionTestConsole.h"
#include "UI/SLLMNPCProviderSettings.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FLLMNPCActionLayerEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCActionLayerEditor, Log, All);

namespace
{
const FName ProviderSettingsTabName(TEXT("LLMNPCProviderSettings"));
const FName MotionTestConsoleTabName(TEXT("LLMNPCMotionTestConsole"));
}

void FLLMNPCActionLayerEditorModule::StartupModule()
{
	const FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::LoadProjectConfig();
	if (!OnlineConfig.IsLoaded())
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Warning,
			TEXT("LLMNPC online test config was not loaded: %s"),
			*OnlineConfig.ErrorCode.ToString()
		);
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ProviderSettingsTabName,
		FOnSpawnTab::CreateRaw(this, &FLLMNPCActionLayerEditorModule::SpawnProviderSettingsTab)
	)
		.SetDisplayName(LOCTEXT("ProviderSettingsTabTitle", "LLM NPC Provider"))
		.SetTooltipText(LOCTEXT("ProviderSettingsTabTooltip", "Configure and test LLM NPC model providers."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MotionTestConsoleTabName,
		FOnSpawnTab::CreateRaw(this, &FLLMNPCActionLayerEditorModule::SpawnMotionTestConsoleTab)
	)
		.SetDisplayName(LOCTEXT("MotionTestConsoleTabTitle", "LLM NPC Motion Test"))
		.SetTooltipText(LOCTEXT("MotionTestConsoleTabTooltip", "Run Published motion templates against PIE NPCs."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Play"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLLMNPCActionLayerEditorModule::RegisterMenus)
	);
}

void FLLMNPCActionLayerEditorModule::ShutdownModule()
{
	FLLMNPCOnlineTestConfigLoader::ClearSession();
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ProviderSettingsTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MotionTestConsoleTabName);
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
		Section.AddMenuEntry(
			"LLMNPC_OpenMotionTestConsole",
			LOCTEXT("OpenMotionTestConsole", "LLM NPC Motion Test Console"),
			LOCTEXT("OpenMotionTestConsoleTooltip", "Test Published templates and inspect runtime motion state during PIE."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Play"),
			FUIAction(FExecuteAction::CreateRaw(this, &FLLMNPCActionLayerEditorModule::OpenMotionTestConsole))
		);
	}
}

void FLLMNPCActionLayerEditorModule::OpenProviderSettings()
{
	FGlobalTabmanager::Get()->TryInvokeTab(ProviderSettingsTabName);
}

void FLLMNPCActionLayerEditorModule::OpenMotionTestConsole()
{
	FGlobalTabmanager::Get()->TryInvokeTab(MotionTestConsoleTabName);
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

TSharedRef<SDockTab> FLLMNPCActionLayerEditorModule::SpawnMotionTestConsoleTab(
	const FSpawnTabArgs& SpawnTabArgs
)
{
	static_cast<void>(SpawnTabArgs);
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SLLMNPCMotionTestConsole)
		];
}

IMPLEMENT_MODULE(FLLMNPCActionLayerEditorModule, LLMNPCActionLayerEditor)

#undef LOCTEXT_NAMESPACE
