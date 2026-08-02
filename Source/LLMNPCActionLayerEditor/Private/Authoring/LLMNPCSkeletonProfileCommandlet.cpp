#include "Authoring/LLMNPCSkeletonProfileCommandlet.h"

#include "Authoring/LLMNPCMannyValidationBaselineExporter.h"
#include "Authoring/LLMNPCSkeletonCapabilityExporter.h"
#include "Authoring/LLMNPCSkeletonProfileAuthoringSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

ULLMNPCSkeletonProfileCommandlet::ULLMNPCSkeletonProfileCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 ULLMNPCSkeletonProfileCommandlet::Main(const FString& Params)
{
	FString ProfilePath;
	if (!FParse::Value(*Params, TEXT("Profile="), ProfilePath) || ProfilePath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("LLMNPC_PROFILE_COMMANDLET_PROFILE_REQUIRED"));
		return 1;
	}

	const bool bPreserveCalibration = !FParse::Param(*Params, TEXT("ResetCalibration"));
	ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(nullptr, *ProfilePath);
	if (!Profile)
	{
		UE_LOG(LogTemp, Error, TEXT("LLMNPC_PROFILE_COMMANDLET_PROFILE_NOT_FOUND:%s"), *ProfilePath);
		return 2;
	}

	ULLMNPCSkeletonProfileAuthoringSubsystem* Authoring =
		NewObject<ULLMNPCSkeletonProfileAuthoringSubsystem>();
	const FLLMNPCSkeletonProfileAuthoringResult Result = Authoring->RefreshGeneratedProfile(
		Profile,
		bPreserveCalibration
	);
	if (!Result.bSuccess)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("LLMNPC_PROFILE_COMMANDLET_FAILED:%s:%s"),
			*Result.ErrorCode.ToString(),
			*Result.Message
		);
		return 3;
	}

	if (FParse::Param(*Params, TEXT("ExportResources")))
	{
		const TSharedPtr<IPlugin> Plugin =
			IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("LLMNPC_PROFILE_COMMANDLET_PLUGIN_NOT_FOUND"));
			return 4;
		}

		const FString CapabilityPath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources"),
			TEXT("Capabilities"),
			TEXT("Manny"),
			TEXT("ue5_manny_v1.capability.json")
		);
		FLLMNPCSkeletonCapabilitySnapshot Capability;
		FString ExportError;
		if (!FLLMNPCSkeletonCapabilityExporter::ExportModelView(
			*Profile,
			nullptr,
			CapabilityPath,
			Capability,
			ExportError
		))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("LLMNPC_PROFILE_COMMANDLET_CAPABILITY_EXPORT_FAILED:%s"),
				*ExportError
			);
			return 5;
		}

		const FString BaselinePath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources"),
			TEXT("Validation"),
			TEXT("Manny"),
			TEXT("MannyValidationBaseline.v1.json")
		);
		if (!FLLMNPCMannyValidationBaselineExporter::Export(
			*Profile,
			Capability,
			BaselinePath,
			ExportError
		))
		{
			UE_LOG(
				LogTemp,
				Error,
				TEXT("LLMNPC_PROFILE_COMMANDLET_BASELINE_EXPORT_FAILED:%s"),
				*ExportError
			);
			return 6;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("LLMNPC_PROFILE_COMMANDLET_RESOURCES_EXPORTED:%s:%s:%s"),
			*Capability.CapabilityHash,
			*CapabilityPath,
			*BaselinePath
		);
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("LLMNPC_PROFILE_COMMANDLET_SUCCEEDED:%s:%s"),
		*Result.AssetPath,
		*Result.ReportPath
	);
	return 0;
}
