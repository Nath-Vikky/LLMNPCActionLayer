#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "Engine/DataAsset.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCMotionTemplate.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCTemplateKind : uint8
{
	ProceduralMotion,
	AnimationAsset,
	CompoundBehavior
};

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

USTRUCT(BlueprintType)
struct FLLMNPCModifierPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FVector2D AmplitudeRange = FVector2D(0.8f, 1.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FVector2D SpeedRange = FVector2D(0.85f, 1.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FVector2D DurationRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bAllowMirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> AllowedStyleTags;
};

USTRUCT(BlueprintType)
struct FLLMNPCTemplateMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FName TemplateId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FName PublicActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FString SemanticVersion = TEXT("1.0.0");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> IntentTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> EmotionTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> PersonalityTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> RequiredChannels;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> BlockedStates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FName SkeletonProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bRequiresTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bCanRunWhileMoving = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bAllowRuntimeModelSelection = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0"))
	float CooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	ELLMNPCTemplateReviewState ReviewState = ELLMNPCTemplateReviewState::Draft;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCMotionTemplate : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	ELLMNPCTemplateKind Kind = ELLMNPCTemplateKind::ProceduralMotion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FLLMNPCTemplateMetadata Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FLLMNPCModifierPolicy ModifierPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FLLMMotionClip ProceduralClip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TSoftObjectPtr<UAnimationAsset> AnimationAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FString SourceProvenanceJson;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FString ValidationReportJson;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template")
	bool IsPublished() const;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Template")
	bool ValidateTemplate(FString& OutError) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
