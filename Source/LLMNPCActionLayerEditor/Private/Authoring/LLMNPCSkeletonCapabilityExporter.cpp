#include "Authoring/LLMNPCSkeletonCapabilityExporter.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool FLLMNPCSkeletonCapabilityExporter::ExportModelView(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* ControlManifest,
	const FString& OutputFilename,
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	OutError.Reset();
	const FLLMNPCSkeletonCapabilityBuildResult BuildResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			Profile,
			ControlManifest,
			OutSnapshot
		);
	if (!BuildResult.bSucceeded)
	{
		OutError = BuildResult.Errors.IsEmpty()
			? TEXT("LLMNPC_CAPABILITY_BUILD_FAILED")
			: FString::Join(BuildResult.Errors, TEXT(";"));
		return false;
	}

	FString Json;
	if (!FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
		OutSnapshot,
		Json,
		OutError
	))
	{
		return false;
	}

	const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(OutputFilename);
	if (!AbsoluteFilename.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_EXPORT_EXTENSION_INVALID");
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(AbsoluteFilename),
		true
	))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_EXPORT_DIRECTORY_FAILED");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(
		Json,
		*AbsoluteFilename,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_EXPORT_WRITE_FAILED");
		return false;
	}
	return true;
}
