#pragma once

#include "CoreMinimal.h"
#include "LLMNPCUEPIArtifactAdapter.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCUEPIReconstructionSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString ProfileSchemaVersion;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString ProfileContentHash;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString ArtifactId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString ArtifactUri;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString SequencePath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString SkeletonPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString FullPoseArtifactUri;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString FullPoseArtifactPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	float PlayLengthSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	int32 DriverCurveCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	int32 DriverKeyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	int32 FullPoseSampleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	TArray<FName> RecommendedDriverBones;
};

class LLMNPCACTIONLAYEREDITOR_API FLLMNPCUEPIArtifactAdapter
{
public:
	static bool LoadReconstructionProfile(
		const FString& ProfileFilePath,
		FLLMNPCUEPIReconstructionSummary& OutSummary,
		FString& OutAuthoringContextJson,
		FString& OutError
	);

	static bool ParseReconstructionProfile(
		const FString& ProfileJson,
		FLLMNPCUEPIReconstructionSummary& OutSummary,
		FString& OutAuthoringContextJson,
		FString& OutError
	);

	static bool ValidateFullPoseArtifact(
		const FString& FullPoseJson,
		const FLLMNPCUEPIReconstructionSummary& Summary,
		FString& OutError
	);

	static FString HashJson(const FString& JsonString);
};
