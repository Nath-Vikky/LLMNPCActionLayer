#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"
#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"
#include "Quality/LLMNPCKinematicValidator.h"

class ULLMNPCControlManifest;
class ULLMNPCSkeletonProfile;

namespace LLMNPCAuthoringSandbox
{
inline constexpr const TCHAR* ReportSchemaVersion =
	TEXT("llmnpc.online_sandbox_report.v1");
inline constexpr const TCHAR* DraftRecordSchemaVersion =
	TEXT("llmnpc.sandbox_draft_record.v1");
}

enum class ELLMNPCAuthoringSandboxStage : uint8
{
	Idle,
	Parsing,
	Validating,
	Compiling,
	Preflight,
	Ready,
	Rejected
};

struct LLMNPCACTIONLAYER_API FLLMNPCAuthoringSandboxRequest
{
	FString RecipeJson;
	const ULLMNPCSkeletonProfile* SkeletonProfile = nullptr;
	const ULLMNPCControlManifest* ControlManifest = nullptr;
	TMap<FName, FString> TargetBindings;
	TSet<FName> ActiveBlockedStates;
	float MaxDurationSeconds =
		LLMNPCMotionRecipe::DefaultMaxDurationSeconds;
	int32 MaxPrimitiveCount =
		LLMNPCMotionRecipe::DefaultMaxPrimitiveCount;
};

struct LLMNPCACTIONLAYER_API FLLMNPCAuthoringSandboxPreflightResult
{
	bool bPassed = false;
	ELLMNPCAuthoringSandboxStage Stage =
		ELLMNPCAuthoringSandboxStage::Idle;
	FName ErrorCode = NAME_None;
	FString ErrorMessage;
	FString CanonicalRecipeJson;
	FLLMNPCMotionRecipeValidationResult RecipeValidation;
	FLLMNPCCompiledRecipeMetadata CompiledMetadata;
	FLLMNPCKinematicQualityReport KinematicReport;
	FLLMMotionPlan TransientPlan;
};

class LLMNPCACTIONLAYER_API FLLMNPCAuthoringSandbox
{
public:
	static bool IsBuildAvailable();

	static FString BuildCanonicalTargetRef(FName TargetSlot);

	static FLLMNPCAuthoringSandboxPreflightResult RunFullPreflight(
		const FLLMNPCAuthoringSandboxRequest& Request
	);
};
