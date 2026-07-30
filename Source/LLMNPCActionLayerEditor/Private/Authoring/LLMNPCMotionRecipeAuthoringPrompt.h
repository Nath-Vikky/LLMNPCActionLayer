#pragma once

#include "CoreMinimal.h"

class ULLMNPCMotionTemplate;
struct FLLMNPCSkeletonCapabilitySnapshot;

namespace LLMNPCMotionRecipeAuthoring
{
inline constexpr const TCHAR* PromptVersion =
	TEXT("llmnpc.motion_recipe_authoring_prompt.v5");
inline constexpr const TCHAR* LegacyPromptVersionV4 =
	TEXT("llmnpc.motion_recipe_authoring_prompt.v4");
inline constexpr const TCHAR* LegacyPromptVersionV3 =
	TEXT("llmnpc.motion_recipe_authoring_prompt.v3");
inline constexpr const TCHAR* ResponseSchemaVersion =
	TEXT("llmnpc.motion_recipe_authoring_response.v1");
inline constexpr const TCHAR* JobSchemaVersion =
	TEXT("llmnpc.motion_recipe_authoring_job.v2");
inline constexpr const TCHAR* DefaultAuthoringContractId =
	TEXT("gesture.shrug");
inline constexpr const TCHAR* ProceduralClapAuthoringContractId =
	TEXT("gesture.clap.procedural");
inline constexpr const TCHAR* ProceduralBeckonAuthoringContractId =
	TEXT("gesture.beckon.procedural");
inline constexpr const TCHAR* ManualTriggerSource =
	TEXT("ManualWorkbench");
inline constexpr const TCHAR* RegenerationTriggerSource =
	TEXT("RegenerateRejectedDraft");
}

struct FLLMNPCMotionRecipeAuthoringContract
{
	FName ContractId = NAME_None;
	FName PublicActionId = NAME_None;
	FText DisplayName;
	FString DefaultDesiredAction;
	FName Phase = NAME_None;
	FName RequiredIntent = NAME_None;
	TSet<FName> AllowedPrimitiveIds;
	int32 PrimitiveCount = 1;
	double MinDurationSeconds = 0.05;
	double MaxDurationSeconds = 4.0;
	bool bPrimitiveCoversRecipe = true;
	TSet<FName> AllowedTargetSlots;
	bool bTargetRequired = false;
	bool bAllowMirror = false;
	FString TargetContract =
		TEXT("No scene target is available for this generation request.");
	FString TimingContract;
	FString RevisionPolicy;
};

struct FLLMNPCMotionRecipeRequestContext
{
	FName AuthoringContractId =
		LLMNPCMotionRecipeAuthoring::DefaultAuthoringContractId;
	FName TriggerSource =
		LLMNPCMotionRecipeAuthoring::ManualTriggerSource;
	FName SourceTemplateId = NAME_None;
	FString SourceRecipeHash;
	FString ReviewFeedback;

	bool IsRegeneration() const
	{
		return
			TriggerSource ==
				LLMNPCMotionRecipeAuthoring::
					RegenerationTriggerSource;
	}
};

struct FLLMNPCMotionRecipePromptPackage
{
	FString PromptVersion = LLMNPCMotionRecipeAuthoring::PromptVersion;
	FString SystemPrompt;
	FString UserJson;
	FString RecipeSchemaJson;
	FString CapabilityModelViewJson;
	FString PromptHash;
	FString CapabilityHash;
	FString RegistryVersion;
	int32 SimilarTemplateCount = 0;
	FLLMNPCMotionRecipeAuthoringContract AuthoringContract;
	FLLMNPCMotionRecipeRequestContext RequestContext;
};

struct FLLMNPCMotionRecipeCatalogDraft
{
	FString DisplayName;
	FString SelectionSummary;
	FString VisualDescription;
	TArray<FString> SuitableWhen;
	TArray<FString> AvoidWhen;
};

struct FLLMNPCMotionRecipeAuthoringResponse
{
	bool bUnsupported = false;
	FString UnsupportedReason;
	FString RecipeJson;
	FLLMNPCMotionRecipeCatalogDraft CatalogDraft;
};

class FLLMNPCMotionRecipeAuthoringPrompt
{
public:
	static const TArray<FLLMNPCMotionRecipeAuthoringContract>&
		GetContracts();

	static const FLLMNPCMotionRecipeAuthoringContract* FindContract(
		FName ContractId
	);

	static const FLLMNPCMotionRecipeAuthoringContract*
		FindContractForPublicAction(FName PublicActionId);

	static bool Build(
		const FString& DesiredAction,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const TArray<const ULLMNPCMotionTemplate*>& PublishedExamples,
		FLLMNPCMotionRecipePromptPackage& OutPackage,
		FString& OutError,
		const FLLMNPCMotionRecipeRequestContext& RequestContext =
			FLLMNPCMotionRecipeRequestContext()
	);

	static bool ParseResponse(
		const FString& ResponseJson,
		FLLMNPCMotionRecipeAuthoringResponse& OutResponse,
		FString& OutError
	);

	static bool ValidateRecipeForCapability(
		const FLLMNPCMotionRecipeAuthoringResponse& Response,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const TSet<FName>& AllowedPrimitiveIds,
		FName RequiredIntent,
		int32 MaxPrimitiveCount,
		FString& OutError
	);

	static bool ValidateRecipeForCapability(
		const FLLMNPCMotionRecipeAuthoringResponse& Response,
		const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
		const FLLMNPCMotionRecipeAuthoringContract& Contract,
		FString& OutError
	);
};
