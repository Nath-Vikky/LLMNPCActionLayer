#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LLMNPCModifierMappingProfile.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCModifierMappingStage : uint8
{
	Personality,
	Emotion,
	Relationship
};

UENUM(BlueprintType)
enum class ELLMNPCModifierInputField : uint8
{
	PersonalityExpressiveness,
	PersonalityShyness,
	PersonalitySociability,
	EmotionIntensity,
	EmotionValence,
	EmotionArousal,
	RelationshipFamiliarity,
	RelationshipTrust,
	RelationshipAffinity
};

UENUM(BlueprintType)
enum class ELLMNPCResolvedModifierField : uint8
{
	Amplitude,
	SpeedScale,
	DurationScale,
	ReachScale,
	HeightScale,
	LateralScale,
	GazeEngagement,
	PalmOrientationWeight,
	FingerPoseWeight,
	TorsoParticipation,
	BlendInScale,
	BlendOutScale
};

UENUM(BlueprintType)
enum class ELLMNPCModifierResponseCurve : uint8
{
	Linear,
	SmoothStep,
	EaseIn,
	EaseOut
};

UENUM(BlueprintType)
enum class ELLMNPCModifierCombinationMode : uint8
{
	Multiply,
	Add,
	Min,
	Max,
	OverrideIfAllowed
};

USTRUCT(BlueprintType)
struct FLLMNPCModifierMappingRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	ELLMNPCModifierMappingStage Stage =
		ELLMNPCModifierMappingStage::Personality;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	ELLMNPCModifierInputField InputField =
		ELLMNPCModifierInputField::PersonalityExpressiveness;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	FVector2D InputRange = FVector2D(0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	ELLMNPCResolvedModifierField OutputModifier =
		ELLMNPCResolvedModifierField::Amplitude;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	FVector2D ContributionRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	ELLMNPCModifierResponseCurve ResponseCurve =
		ELLMNPCModifierResponseCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	ELLMNPCModifierCombinationMode CombinationMode =
		ELLMNPCModifierCombinationMode::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	FName RequiredTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	int32 Priority = 0;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCModifierMappingProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	FString SchemaVersion = TEXT("llmnpc.modifier_mapping_profile.v1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	FName ProfileId = TEXT("manny.default.v1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Mapping")
	TArray<FLLMNPCModifierMappingRule> Rules;

	bool Validate(FString& OutError) const;

	static TArray<FLLMNPCModifierMappingRule> BuildMannyDefaultRules();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
