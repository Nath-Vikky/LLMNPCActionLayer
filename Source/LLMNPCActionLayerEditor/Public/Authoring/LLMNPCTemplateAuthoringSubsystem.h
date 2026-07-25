#pragma once

#include "CoreMinimal.h"
#include "Authoring/LLMNPCUEPIArtifactAdapter.h"
#include "EditorSubsystem.h"
#include "LLMNPCTemplateAuthoringSubsystem.generated.h"

class AActor;
class ULLMNPCMotionTemplate;
class ULLMNPCPublicActionDefinition;
struct FLLMMotionPlan;

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
