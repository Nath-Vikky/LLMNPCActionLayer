#include "LLMNPCActionLayerEditorModule.h"

#include "Authoring/LLMNPCSkeletonCapabilityExporter.h"
#include "Authoring/LLMNPCSkeletonProfileAuthoringSubsystem.h"
#include "Authoring/LLMNPCMannyValidationBaselineExporter.h"
#include "Authoring/LLMNPCMannyN2CatalogMigration.h"
#include "Authoring/LLMNPCMannyN3ContextMigration.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Online/LLMNPCCapabilitySmokeRunner.h"
#include "Online/LLMNPCCatalogSelectionSmokeRunner.h"
#include "Online/LLMNPCContextModifierSmokeRunner.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SLLMNPCMotionTestConsole.h"
#include "UI/SLLMNPCProviderSettings.h"
#include "UI/SLLMNPCTemplateWorkbench.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FLLMNPCActionLayerEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCActionLayerEditor, Log, All);

namespace
{
const FName ProviderSettingsTabName(TEXT("LLMNPCProviderSettings"));
const FName MotionTestConsoleTabName(TEXT("LLMNPCMotionTestConsole"));
const FName TemplateWorkbenchTabName(TEXT("LLMNPCTemplateWorkbench"));

void OpenTemplateWorkbenchTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TemplateWorkbenchTabName);
}

FAutoConsoleCommand OpenTemplateWorkbenchCommand(
	TEXT("LLMNPC.OpenTemplateWorkbench"),
	TEXT("Open the LLM NPC Template Workbench Nomad tab."),
	FConsoleCommandDelegate::CreateStatic(&OpenTemplateWorkbenchTab)
);

void RefreshMannyN1Profile()
{
	ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	ULLMNPCSkeletonProfileAuthoringSubsystem* Authoring =
		GEditor
		? GEditor->GetEditorSubsystem<ULLMNPCSkeletonProfileAuthoringSubsystem>()
		: nullptr;
	if (!Profile || !Authoring)
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Profile refresh could not load its asset or Editor subsystem.")
		);
		return;
	}

	const FLLMNPCSkeletonProfileAuthoringResult RefreshResult =
		Authoring->RefreshGeneratedProfile(Profile, true);
	if (!RefreshResult.bSuccess || !RefreshResult.QualityReport.bCapabilityReady)
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Profile refresh failed: %s %s CapabilityReady=%s"),
			*RefreshResult.ErrorCode.ToString(),
			*RefreshResult.Message,
			RefreshResult.QualityReport.bCapabilityReady ? TEXT("true") : TEXT("false")
		);
		return;
	}

	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (!Plugin.IsValid())
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Profile refresh could not resolve the plugin directory.")
		);
		return;
	}

	const FString OutputPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Capabilities"),
		TEXT("Manny"),
		TEXT("ue5_manny_v1.capability.json")
	);
	FLLMNPCSkeletonCapabilitySnapshot Snapshot;
	FString ExportError;
	if (!FLLMNPCSkeletonCapabilityExporter::ExportModelView(
		*Profile,
		nullptr,
		OutputPath,
		Snapshot,
		ExportError
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Capability export failed: %s"),
			*ExportError
		);
		return;
	}

	const FString BaselinePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Validation"),
		TEXT("Manny"),
		TEXT("MannyValidationBaseline.v1.json")
	);
	FString BaselineError;
	if (!FLLMNPCMannyValidationBaselineExporter::Export(
		*Profile,
		Snapshot,
		BaselinePath,
		BaselineError
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Validation Baseline export failed: %s"),
			*BaselineError
		);
		return;
	}

	UE_LOG(
		LogLLMNPCActionLayerEditor,
		Display,
		TEXT("LLMNPC N1 Manny Profile, Capability, and Validation Baseline refreshed. Hash=%s Capability=%s Baseline=%s"),
		*Snapshot.CapabilityHash,
		*OutputPath,
		*BaselinePath
	);
}

FAutoConsoleCommand RefreshMannyN1ProfileCommand(
	TEXT("LLMNPC.RefreshMannyN1Profile"),
	TEXT("Idempotently refresh the shipped Manny Profile to the Forward N1 contract."),
	FConsoleCommandDelegate::CreateStatic(&RefreshMannyN1Profile)
);

void ApproveMannyN1ValidationBaseline()
{
	ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	ULLMNPCSkeletonProfileAuthoringSubsystem* Authoring =
		GEditor
		? GEditor->GetEditorSubsystem<ULLMNPCSkeletonProfileAuthoringSubsystem>()
		: nullptr;
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (!Profile || !Authoring || !Plugin.IsValid())
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Baseline approval could not resolve the Profile, Editor subsystem, or plugin directory.")
		);
		return;
	}

	FString Error;
	if (!FLLMNPCMannyValidationBaselineExporter::CalibrateThresholdsFromPublishedTemplates(
		*Profile,
		1.2f,
		Error
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Baseline calibration failed: %s"),
			*Error
		);
		return;
	}
	Profile->UpperBodyConstraints.ValidationBaselineVersion =
		TEXT("manny.validation.baseline.v1");
	const FLLMNPCSkeletonProfileAuthoringResult CalibratedRefresh =
		Authoring->RefreshGeneratedProfile(Profile, true);
	if (
		!CalibratedRefresh.bSuccess ||
		!CalibratedRefresh.QualityReport.bCapabilityReady
	)
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny calibrated Profile save failed: %s %s"),
			*CalibratedRefresh.ErrorCode.ToString(),
			*CalibratedRefresh.Message
		);
		return;
	}

	const FString CapabilityPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Capabilities"),
		TEXT("Manny"),
		TEXT("ue5_manny_v1.capability.json")
	);
	const FString BaselinePath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Validation"),
		TEXT("Manny"),
		TEXT("MannyValidationBaseline.v1.json")
	);
	FLLMNPCSkeletonCapabilitySnapshot CandidateCapability;
	if (!FLLMNPCSkeletonCapabilityExporter::ExportModelView(
		*Profile,
		nullptr,
		CapabilityPath,
		CandidateCapability,
		Error
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny calibrated Capability export failed: %s"),
			*Error
		);
		return;
	}
	FString BaselineHash;
	if (!FLLMNPCMannyValidationBaselineExporter::Export(
		*Profile,
		CandidateCapability,
		BaselinePath,
		Error,
		&BaselineHash
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Baseline candidate export failed: %s"),
			*Error
		);
		return;
	}

	Profile->UpperBodyConstraints.bKinematicBaselineApproved = true;
	Profile->UpperBodyConstraints.ValidationBaselineHash = BaselineHash;
	const FLLMNPCSkeletonProfileAuthoringResult ApprovedRefresh =
		Authoring->RefreshGeneratedProfile(Profile, true);
	if (!ApprovedRefresh.bSuccess)
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny approved Profile save failed: %s %s"),
			*ApprovedRefresh.ErrorCode.ToString(),
			*ApprovedRefresh.Message
		);
		return;
	}

	FLLMNPCSkeletonCapabilitySnapshot ApprovedCapability;
	if (!FLLMNPCSkeletonCapabilityExporter::ExportModelView(
		*Profile,
		nullptr,
		CapabilityPath,
		ApprovedCapability,
		Error
	))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny approved Capability export failed: %s"),
			*Error
		);
		return;
	}
	FString ApprovedBaselineHash;
	if (
		CandidateCapability.CapabilityHash != ApprovedCapability.CapabilityHash ||
		!FLLMNPCMannyValidationBaselineExporter::Export(
			*Profile,
			ApprovedCapability,
			BaselinePath,
			Error,
			&ApprovedBaselineHash
		) ||
		ApprovedBaselineHash != BaselineHash
	)
	{
		Profile->UpperBodyConstraints.bKinematicBaselineApproved = false;
		Profile->UpperBodyConstraints.ValidationBaselineHash.Reset();
		Authoring->RefreshGeneratedProfile(Profile, true);
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC N1 Manny Baseline approval became inconsistent and was cleared: %s"),
			*Error
		);
		return;
	}

	UE_LOG(
		LogLLMNPCActionLayerEditor,
		Display,
		TEXT("LLMNPC N1 Manny Validation Baseline APPROVED. CapabilityHash=%s BaselineHash=%s Headroom=1.2"),
		*ApprovedCapability.CapabilityHash,
		*ApprovedBaselineHash
	);
}

FAutoConsoleCommand ApproveMannyN1ValidationBaselineCommand(
	TEXT("LLMNPC.ApproveMannyN1ValidationBaseline"),
	TEXT("Calibrate Manny constraints from Published templates and finalize the human-approved N1 Baseline."),
	FConsoleCommandDelegate::CreateStatic(&ApproveMannyN1ValidationBaseline)
);

void RunMannyN1CapabilitySmoke()
{
	const bool bExitWhenComplete = FParse::Param(
		FCommandLine::Get(),
		TEXT("LLMNPCCapabilitySmokeExit")
	);
	FString Error;
	if (!FLLMNPCCapabilitySmokeRunner::Start(bExitWhenComplete, Error))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC Forward N1 Capability Smoke could not start: %s"),
			*Error
		);
		if (bExitWhenComplete)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

FAutoConsoleCommand RunMannyN1CapabilitySmokeCommand(
	TEXT("LLMNPC.RunMannyN1CapabilitySmoke"),
	TEXT("Run the strict online Manny Capability Smoke and write a sanitized Forward N1 report."),
	FConsoleCommandDelegate::CreateStatic(&RunMannyN1CapabilitySmoke)
);

void RunMannyN2CatalogSelectionSmoke()
{
	const bool bExitWhenComplete = FParse::Param(
		FCommandLine::Get(),
		TEXT("LLMNPCSelectionSmokeExit")
	);
	FString Error;
	if (!FLLMNPCCatalogSelectionSmokeRunner::Start(bExitWhenComplete, Error))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC Forward N2 Catalog Selection could not start: %s"),
			*Error
		);
		if (bExitWhenComplete)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

FAutoConsoleCommand RunMannyN2CatalogSelectionSmokeCommand(
	TEXT("LLMNPC.RunMannyN2CatalogSelectionSmoke"),
	TEXT("Run the versioned real-model Manny N2 Catalog selection suite and write a sanitized report."),
	FConsoleCommandDelegate::CreateStatic(&RunMannyN2CatalogSelectionSmoke)
);

void RunMannyN3ContextModifierSmoke()
{
	const bool bExitWhenComplete = FParse::Param(
		FCommandLine::Get(),
		TEXT("LLMNPCContextSmokeExit")
	);
	FString Error;
	if (!FLLMNPCContextModifierSmokeRunner::Start(bExitWhenComplete, Error))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC Forward N3 Context Modifier could not start: %s"),
			*Error
		);
		if (bExitWhenComplete)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

FAutoConsoleCommand RunMannyN3ContextModifierSmokeCommand(
	TEXT("LLMNPC.RunMannyN3ContextModifierSmoke"),
	TEXT("Run the real-model Manny N3 context adaptation suite and write a sanitized trace report."),
	FConsoleCommandDelegate::CreateStatic(&RunMannyN3ContextModifierSmoke)
);

void MigrateMannyN2Catalog()
{
	FString CatalogHash;
	FString Error;
	if (!FLLMNPCMannyN2CatalogMigration::Run(CatalogHash, Error))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC Forward N2 Manny Catalog migration failed: %s"),
			*Error
		);
		return;
	}
	UE_LOG(
		LogLLMNPCActionLayerEditor,
		Display,
		TEXT("LLMNPC Forward N2 Manny Catalog migrated. CatalogHash=%s"),
		*CatalogHash
	);
}

FAutoConsoleCommand MigrateMannyN2CatalogCommand(
	TEXT("LLMNPC.MigrateMannyN2Catalog"),
	TEXT("Idempotently create the Manny Forward N2 vocabulary, Public Actions, and Catalog metadata."),
	FConsoleCommandDelegate::CreateStatic(&MigrateMannyN2Catalog)
);

void MigrateMannyN3Context()
{
	FString CatalogHash;
	FString Error;
	if (!FLLMNPCMannyN3ContextMigration::Run(CatalogHash, Error))
	{
		UE_LOG(
			LogLLMNPCActionLayerEditor,
			Error,
			TEXT("LLMNPC Forward N3 Manny Context migration failed: %s"),
			*Error
		);
		return;
	}
	UE_LOG(
		LogLLMNPCActionLayerEditor,
		Display,
		TEXT("LLMNPC Forward N3 Manny Context migrated. CatalogHash=%s"),
		*CatalogHash
	);
}

FAutoConsoleCommand MigrateMannyN3ContextCommand(
	TEXT("LLMNPC.MigrateMannyN3Context"),
	TEXT("Idempotently upgrade Manny policies and create the Forward N3 modifier mapping profile."),
	FConsoleCommandDelegate::CreateStatic(&MigrateMannyN3Context)
);
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
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TemplateWorkbenchTabName,
		FOnSpawnTab::CreateRaw(this, &FLLMNPCActionLayerEditorModule::SpawnTemplateWorkbenchTab)
	)
		.SetDisplayName(LOCTEXT("TemplateWorkbenchTabTitle", "LLM NPC Template Workbench"))
		.SetTooltipText(LOCTEXT("TemplateWorkbenchTabTooltip", "Browse, preview, validate, review, and publish motion catalog assets."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLLMNPCActionLayerEditorModule::RegisterMenus)
	);
}

void FLLMNPCActionLayerEditorModule::ShutdownModule()
{
	FLLMNPCCapabilitySmokeRunner::Cancel();
	FLLMNPCCatalogSelectionSmokeRunner::Cancel();
	FLLMNPCContextModifierSmokeRunner::Cancel();
	FLLMNPCOnlineTestConfigLoader::ClearSession();
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ProviderSettingsTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MotionTestConsoleTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TemplateWorkbenchTabName);
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
		Section.AddMenuEntry(
			"LLMNPC_OpenTemplateWorkbench",
			LOCTEXT("OpenTemplateWorkbench", "LLM NPC Template Workbench"),
			LOCTEXT("OpenTemplateWorkbenchTooltip", "Browse, preview, validate, review, and publish motion catalog assets."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
			FUIAction(FExecuteAction::CreateRaw(this, &FLLMNPCActionLayerEditorModule::OpenTemplateWorkbench))
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

void FLLMNPCActionLayerEditorModule::OpenTemplateWorkbench()
{
	OpenTemplateWorkbenchTab();
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

TSharedRef<SDockTab> FLLMNPCActionLayerEditorModule::SpawnTemplateWorkbenchTab(
	const FSpawnTabArgs& SpawnTabArgs
)
{
	static_cast<void>(SpawnTabArgs);
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SLLMNPCTemplateWorkbench)
		];
}

IMPLEMENT_MODULE(FLLMNPCActionLayerEditorModule, LLMNPCActionLayerEditor)

#undef LOCTEXT_NAMESPACE
