#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "LLMNPCSkeletonProfileAuthoringSubsystem.generated.h"

class USkeleton;

USTRUCT(BlueprintType)
struct FLLMNPCSkeletonProfileAuthoringResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	FString ReportPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	TObjectPtr<ULLMNPCSkeletonProfile> ProfileAsset;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Skeleton Authoring")
	FLLMNPCSkeletonProfileQualityReport QualityReport;
};

UCLASS()
class LLMNPCACTIONLAYEREDITOR_API ULLMNPCSkeletonProfileAuthoringSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC|Skeleton Authoring")
	FLLMNPCSkeletonProfileAuthoringResult GenerateProfile(
		USkeleton* Skeleton,
		FName ProfileId,
		const FString& DestinationPackagePath = TEXT("/Game/LLMNPCActionLayer/SkeletonProfiles"),
		bool bEnableRuntimeAxisCalibration = false
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Skeleton Authoring")
	FLLMNPCSkeletonProfileAuthoringResult RefreshGeneratedProfile(
		ULLMNPCSkeletonProfile* Profile,
		bool bPreserveExistingCalibration = true
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Skeleton Authoring")
	FLLMNPCSkeletonProfileAuthoringResult SetAxisCalibration(
		ULLMNPCSkeletonProfile* Profile,
		FName SemanticBone,
		FVector PitchAxis,
		FVector YawAxis,
		FVector RollAxis,
		bool bEnableRuntimeAxisCalibration = true
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Skeleton Authoring")
	FLLMNPCSkeletonProfileAuthoringResult WriteQualityReport(
		ULLMNPCSkeletonProfile* Profile
	);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Skeleton Authoring")
	static FString GetSkeletonProfileReportDirectory();

private:
	static bool PopulateGeneratedProfile(
		ULLMNPCSkeletonProfile& Profile,
		USkeleton& Skeleton,
		bool bPreserveExistingCalibration,
		FString& OutError
	);

	static bool SaveProfileAsset(ULLMNPCSkeletonProfile& Profile, FString& OutError);
	static bool SaveQualityReport(
		const FLLMNPCSkeletonProfileQualityReport& Report,
		FString& OutPath,
		FString& OutError
	);
};
