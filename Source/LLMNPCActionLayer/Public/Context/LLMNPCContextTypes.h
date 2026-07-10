#pragma once

#include "CoreMinimal.h"
#include "LLMNPCContextTypes.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCEmotionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName PrimaryEmotion = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Intensity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Valence = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Arousal = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCPersonalitySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName ProfileId = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Expressiveness = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Shyness = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Sociability = 0.5f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FName> PersonalityTags;
};

USTRUCT(BlueprintType)
struct FLLMNPCRelationshipSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FString OtherActorRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Familiarity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Trust = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Affinity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FName> RelationshipTags;
};

USTRUCT(BlueprintType)
struct FLLMNPCSceneTargetContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName Category = TEXT("generic");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FName> SemanticTags;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	float Salience = 0.5f;
};

USTRUCT(BlueprintType)
struct FLLMNPCSelectionContextSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FLLMNPCEmotionSnapshot Emotion;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FLLMNPCPersonalitySnapshot Personality;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FLLMNPCRelationshipSnapshot Relationship;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FLLMNPCSceneTargetContext> AvailableTargets;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	TArray<FName> ActiveStates;
};

USTRUCT(BlueprintType)
struct FLLMNPCActionHistoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName SelectionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName ResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	FName ReasonTag = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context")
	double TimestampSeconds = 0.0;
};
