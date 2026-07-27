#pragma once

#include "CoreMinimal.h"
#include "Authoring/LLMNPCUEPIArtifactAdapter.h"
#include "EditorSubsystem.h"
#include "LLMNPCTemplateAuthoringSubsystem.generated.h"

class AActor;
class UAnimationAsset;
class ULLMNPCMotionTemplate;
class ULLMNPCPublicActionDefinition;
struct FLLMMotionPlan;

struct FLLMNPCMotionRecipeDraftCatalogSpec
{
	FString AssetName;
	FName TemplateId = NAME_None;
	FName PublicActionId = NAME_None;
	FString PublicActionAssetName;
	FString SemanticVersion = TEXT("1.0.0");
	FName VariantId = TEXT("generated_recipe");
	FString DisplayName;
	FString SelectionSummary;
	FString VisualDescription;
	TArray<FString> SuitableWhen;
	TArray<FString> AvoidWhen;
	TArray<FName> IntentTags;
	TArray<FName> EmotionTags;
	TArray<FName> VariantStyleTags;
	TArray<FName> BodyRegionTags;
	TArray<FName> SpatialRequirementTags;
	TArray<FName> SemanticEffectTags;
	TArray<FName> TargetCategoryTags;
	FName GestureFamily = NAME_None;
	FName DefaultStyle = TEXT("neutral");
	TArray<FString> SearchKeywords;
	bool bCanRunWhileMoving = true;
	float Expressiveness = 0.5f;
	float Energy = 0.5f;
	float SocialIntensity = 0.5f;
};

struct FLLMNPCMotionRecipeGenerationEvidence
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
	FString SystemPrompt;
	FString UserJson;
	FString RecipeSchemaJson;
	FString CapabilityModelViewJson;
	FString RawResponseJson;
	FName TriggerSource = TEXT("ManualWorkbench");
	FName SourceTemplateId = NAME_None;
	FString SourceRecipeHash;
	FString ReviewFeedback;
	FString CompiledRecipeHash;
	FString KinematicReportHash;
	FDateTime GeneratedAtUtc;
	int32 HttpStatus = 0;
	int32 AttemptCount = 0;
	float TotalLatencySeconds = 0.0f;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FLLMNPCAuthoringOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FString OutputPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	TObjectPtr<ULLMNPCMotionTemplate> TemplateAsset;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	TObjectPtr<ULLMNPCPublicActionDefinition> PublicActionAsset;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Authoring")
	FLLMNPCUEPIReconstructionSummary ReconstructionSummary;
};

UCLASS()
class LLMNPCACTIONLAYEREDITOR_API ULLMNPCTemplateAuthoringSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|UEPI")
	FLLMNPCAuthoringOperationResult BuildAuthoringContextFromUEPIProfile(
		const FString& ReconstructionProfileFilePath,
		const FString& OutputFilePath = TEXT("")
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Draft")
	FLLMNPCAuthoringOperationResult ImportDraftFromFile(
		const FString& DraftFilePath,
		const FString& DestinationPackagePath = TEXT("/Game/LLMNPCActionLayer/Authoring/Drafts")
	);

	FLLMNPCAuthoringOperationResult ImportDraftJson(
		const FString& DraftJson,
		const FString& DestinationPackagePath
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Draft")
	FLLMNPCAuthoringOperationResult ImportAnimationDraftFromFile(
		const FString& DraftFilePath,
		UAnimationAsset* SelectedAnimationAsset,
		const FString& DestinationPackagePath = TEXT("/Game/LLMNPCActionLayer/Authoring/Drafts")
	);

	FLLMNPCAuthoringOperationResult ImportAnimationDraftJson(
		const FString& DraftJson,
		UAnimationAsset* SelectedAnimationAsset,
		const FString& DestinationPackagePath
	);

	FLLMNPCAuthoringOperationResult CreateMotionRecipeDraft(
		const FString& RecipeJson,
		FName SkeletonProfileId,
		const FLLMNPCMotionRecipeDraftCatalogSpec& CatalogSpec,
		const FLLMNPCMotionRecipeGenerationEvidence& Evidence,
		const FString& DestinationPackagePath,
		const FString& PublicActionDraftDestinationPath =
			TEXT("/Game/LLMNPCActionLayer/Authoring/PublicActions")
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Quality")
	FLLMNPCAuthoringOperationResult GenerateQualityReport(
		ULLMNPCMotionTemplate* Template,
		const FString& ReconstructionProfileFilePath,
		const FString& FullPoseArtifactFilePath = TEXT("")
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult MarkTemplatePreviewed(
		ULLMNPCMotionTemplate* Template,
		const FString& PreviewNotes
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult ApproveTemplate(
		ULLMNPCMotionTemplate* Template,
		const FString& Reviewer,
		const FString& ReviewNotes
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult RejectTemplate(
		ULLMNPCMotionTemplate* Template,
		const FString& Reviewer,
		const FString& RejectionReason
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Publish")
	FLLMNPCAuthoringOperationResult PublishTemplate(
		ULLMNPCMotionTemplate* Template,
		const FString& DestinationPackagePath = TEXT("")
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult MarkPublicActionPreviewed(
		ULLMNPCPublicActionDefinition* Definition,
		const FString& PreviewNotes
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult ApprovePublicAction(
		ULLMNPCPublicActionDefinition* Definition,
		const FString& Reviewer,
		const FString& ReviewNotes
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Review")
	FLLMNPCAuthoringOperationResult RejectPublicAction(
		ULLMNPCPublicActionDefinition* Definition,
		const FString& Reviewer,
		const FString& RejectionReason
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Publish")
	FLLMNPCAuthoringOperationResult PublishPublicAction(
		ULLMNPCPublicActionDefinition* Definition,
		const FString& DestinationPackagePath = TEXT("")
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Authoring|Preview")
	FLLMNPCAuthoringOperationResult PreviewTemplateOnActor(
		ULLMNPCMotionTemplate* Template,
		AActor* PreviewActor
	);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Publish")
	bool CanPublishTemplate(const ULLMNPCMotionTemplate* Template, FString& OutError) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Publish")
	bool CanPublishPublicAction(
		const ULLMNPCPublicActionDefinition* Definition,
		FString& OutError
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Paths")
	static FString GetDraftDirectory();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Paths")
	static FString GetReportDirectory();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Paths")
	static FString GetRejectedDirectory();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Paths")
	static FString GetPublishedSourceDirectory();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Authoring|Paths")
	static FString GetPublishedPublicActionSourceDirectory();

	static bool CompileTemplateForPreview(
		const ULLMNPCMotionTemplate& Template,
		FLLMMotionPlan& OutPlan,
		FString& OutError
	);

private:
	static bool EnsureAuthoringDirectories(FString& OutError);
	static bool SaveTemplateAsset(ULLMNPCMotionTemplate* Template, FString& OutError);
	static bool SavePublicActionAsset(
		ULLMNPCPublicActionDefinition* Definition,
		FString& OutError
	);
	static bool ExportPublishedTemplateSource(
		const ULLMNPCMotionTemplate& Template,
		FString& OutPath,
		FString& OutError
	);
	static bool ExportPublishedPublicActionSource(
		const ULLMNPCPublicActionDefinition& Definition,
		FString& OutPath,
		FString& OutError
	);
	static FString BuildTemplateContentHash(const ULLMNPCMotionTemplate& Template);
	static bool HasCurrentPassingQualityReport(
		const ULLMNPCMotionTemplate& Template,
		FString& OutError
	);
	static bool ValidateProvenanceForPublish(
		const ULLMNPCMotionTemplate& Template,
		FString& OutError
	);
	static bool ValidateTemplateCatalogForPublish(
		const ULLMNPCMotionTemplate& Template,
		FString& OutError
	);
};
