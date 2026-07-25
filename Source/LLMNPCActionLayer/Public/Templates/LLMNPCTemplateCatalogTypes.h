#pragma once

#include "CoreMinimal.h"
#include "LLMNPCTemplateCatalogTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCTemplateReviewState : uint8
{
	Draft,
	Generated,
	Previewed,
	HumanApproved,
	Published,
	Deprecated,
	Rejected
};

UENUM(BlueprintType)
enum class ELLMNPCActionVocabularyField : uint8
{
	GestureFamily,
	Intent,
	Emotion,
	Personality,
	BodyRegion,
	SpatialRequirement,
	TargetCategory,
	SemanticEffect,
	SuitableState,
	AvoidState,
	VariantStyle
};

USTRUCT(BlueprintType)
struct FLLMNPCCandidateStyleOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FName Style = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FVector2D AmplitudeRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FVector2D SpeedRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FVector2D DurationRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	bool bMirrorAllowed = false;
};

USTRUCT(BlueprintType)
struct FLLMNPCCatalogDiagnostic
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FName Code = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FString FieldPath;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Catalog")
	FString Message;
};

namespace LLMNPCCatalog
{
	inline const FString SchemaVersion(TEXT("llmnpc.template_catalog.v1"));
	inline const FString PublicActionSchemaVersion(TEXT("llmnpc.public_action_definition.v1"));
	inline const FString VocabularySchemaVersion(TEXT("llmnpc.action_vocabulary.v1"));
}
