#pragma once

#include "CoreMinimal.h"

struct FLLMNPCAuthoringSandboxPreflightResult;
struct FLLMNPCAuthoringJsonResult;

struct FLLMNPCOnlineSandboxReportRecord
{
	FGuid RequestId;
	FName ProviderId = NAME_None;
	FString ProviderModelId;
	FString EndpointOrigin;
	FString NonSecretConfigHash;
	FString PromptVersion;
	FString PromptHash;
	FString CapabilityHash;
	FString RegistryVersion;
	FString RecipeHash;
	FString CompiledRecipeHash;
	FString KinematicReportHash;
	FName Outcome = TEXT("not_started");
	FName ErrorCode = NAME_None;
	bool bPreflightPassed = false;
	bool bTransientPlanSubmitted = false;
	bool bDraftRecordSaved = false;
	FString DraftRecordPath;
	FName HumanVisualDecision = TEXT("not_recorded");
	FString HumanVisualNotes;
	FDateTime StartedAtUtc;
	FDateTime UpdatedAtUtc;
	float TotalLatencySeconds = 0.0f;
	int32 AttemptCount = 0;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
	TArray<FString> PreflightIssueCodes;
};

class FLLMNPCOnlineSandboxReport
{
public:
	static void ApplyAuthoringResult(
		const FLLMNPCAuthoringJsonResult& Result,
		FLLMNPCOnlineSandboxReportRecord& InOutRecord
	);

	static void ApplyPreflightResult(
		const FLLMNPCAuthoringSandboxPreflightResult& Result,
		FLLMNPCOnlineSandboxReportRecord& InOutRecord
	);

	static bool Save(
		const FLLMNPCOnlineSandboxReportRecord& Record,
		FString& OutPath,
		FString& OutError
	);

	static bool SaveDraftRecord(
		const FString& CanonicalRecipeJson,
		const FLLMNPCOnlineSandboxReportRecord& Report,
		FString& OutPath,
		FString& OutError
	);
};
