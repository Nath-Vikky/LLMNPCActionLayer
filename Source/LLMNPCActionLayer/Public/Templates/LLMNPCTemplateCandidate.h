#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCatalogTypes.h"
#include "LLMNPCTemplateCandidate.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCTemplateCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FName SelectionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FString SelectionSummary;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FString> SuitableWhen;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FString> AvoidWhen;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> IntentTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> EmotionTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> PersonalityTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> BodyRegionTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> SemanticEffectTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> TargetCategoryTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> RequiredChannels;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> BlockedStates;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	bool bRequiresTarget = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FVector2D AmplitudeRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FVector2D SpeedRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FVector2D DurationRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FName> AllowedStyles;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FLLMNPCCandidateStyleOption> StyleOptions;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	float CooldownSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	TArray<FString> AllowedTargetRefs;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FString DefaultTargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	float RecommendedAmplitude = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	float RecommendedSpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	float RecommendedDurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FName RecommendedStyle = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	bool bAllowMirror = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	bool bMirrorRecommended = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	int32 DefinitionRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	FString CatalogHash;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template Candidate")
	float RelevanceScore = 0.0f;
};
