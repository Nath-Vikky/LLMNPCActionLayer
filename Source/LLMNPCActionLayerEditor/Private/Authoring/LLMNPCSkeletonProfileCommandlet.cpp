#include "Authoring/LLMNPCSkeletonProfileCommandlet.h"

#include "Authoring/LLMNPCSkeletonProfileAuthoringSubsystem.h"
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

	UE_LOG(
		LogTemp,
		Display,
		TEXT("LLMNPC_PROFILE_COMMANDLET_SUCCEEDED:%s:%s"),
		*Result.AssetPath,
		*Result.ReportPath
	);
	return 0;
}
