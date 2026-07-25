#pragma once

#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "CoreMinimal.h"

class ULLMNPCSkeletonProfile;
struct FLLMNPCModelTurnResult;

struct FLLMNPCCapabilitySmokeChallenge
{
	FName ChallengeId = NAME_None;
	FString ContextJson;
	FString ModelViewHash;
	FString PayloadHash;
	FString ExpectedAssistantText;
	TArray<FName> ExpectedCapabilityIds;
	bool bRestrictedFieldScanPassed = false;
	bool bPrivateIdentifierScanPassed = false;
};

struct FLLMNPCCapabilitySmokeValidation
{
	bool bSchemaValid = false;
	bool bNoActionContractValid = false;
	bool bExactCapabilitySelection = false;
	FName ErrorCode = NAME_None;
	TArray<FName> ObservedCapabilityIds;
};

class FLLMNPCCapabilitySmokeRunner
{
public:
	static bool BuildChallenge(
		const ULLMNPCSkeletonProfile& Profile,
		FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
		FLLMNPCCapabilitySmokeChallenge& OutChallenge,
		FString& OutError
	);

	static bool ValidateResponse(
		const FString& ResponseJson,
		const FLLMNPCCapabilitySmokeChallenge& Challenge,
		FLLMNPCCapabilitySmokeValidation& OutValidation
	);

	static bool Start(bool bExitEditorWhenComplete, FString& OutError);
	static bool IsRunning();
	static void Cancel();
};
