#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimationAsset.h"
#include "Context/LLMNPCExecutionContextTypes.h"
#include "Engine/DataAsset.h"
#include "LLMNPCMotionTypes.h"
#include "Templates/LLMNPCTemplateCatalogTypes.h"
#include "LLMNPCMotionTemplate.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCTemplateKind : uint8
{
	ProceduralMotion,
	AnimationAsset,
	CompoundBehavior
};

USTRUCT(BlueprintType)
struct FLLMNPCModifierPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	int32 PolicyVersion = 2;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D ReachScaleRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D HeightScaleRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D LateralScaleRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FIntPoint CycleCountRange = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D GazeEngagementRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D PalmOrientationWeightRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D FingerPoseWeightRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D TorsoParticipationRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D BlendInScaleRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	FVector2D BlendOutScaleRange = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	bool bEnableDynamicTargetTracking = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking", ClampMin="1.0", ClampMax="2000.0"))
	float MaxTargetFollowSpeedCmPerSecond = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking", ClampMin="1.0", ClampMax="720.0"))
	float MaxTargetAngularSpeedDegreesPerSecond = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking", ClampMin="0.1", ClampMax="40.0"))
	float TargetInterpolationSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking", ClampMin="10.0", ClampMax="5000.0"))
	float TargetTeleportThresholdCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking", ClampMin="0.01", ClampMax="5.0"))
	float TargetLostFadeSeconds = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableDynamicTargetTracking"))
	ELLMNPCTargetLossPolicy TargetLossPolicy = ELLMNPCTargetLossPolicy::FadeOut;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context")
	bool bEnableObstacleAdaptation = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableObstacleAdaptation", ClampMin="0.1", ClampMax="1.0"))
	float MinObstacleAmplitudeScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableObstacleAdaptation", ClampMin="0.1", ClampMax="1.0"))
	float MinObstacleReachScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Context", meta=(EditCondition="bEnableObstacleAdaptation", ClampMin="0.0", ClampMax="1.0"))
	float ObstacleCancelClearance = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0", ClampMax="0.25"))
	float RandomAmplitudeJitter = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0", ClampMax="0.25"))
	float RandomSpeedJitter = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0", ClampMax="0.25"))
	float RandomFrequencyJitter = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0", ClampMax="0.5"))
	float RandomPhaseJitterRadians = 0.08f;
};

USTRUCT(BlueprintType)
struct FLLMNPCAnimationPlaybackPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation")
	FName SlotName = TEXT("DefaultSlot");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation", meta=(ClampMin="0.0", ClampMax="2.0"))
	float BlendInSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation", meta=(ClampMin="0.0", ClampMax="2.0"))
	float BlendOutSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation", meta=(ClampMin="0.0"))
	float StartPositionSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation", meta=(ClampMin="0.1", ClampMax="60.0"))
	float MaxDurationSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation")
	bool bInterruptible = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation")
	bool bStopOtherMontages = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Animation")
	bool bAllowRootMotion = false;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	FString CatalogSchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog", meta=(ClampMin="0"))
	int32 CatalogRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FName VariantId = TEXT("default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.01", ClampMax="100.0"))
	float VariantWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> VariantStyleTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog", meta=(MultiLine="true"))
	FString VisualDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> IntentTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> EmotionTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> PersonalityTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	TArray<FName> BodyRegionTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	TArray<FName> SpatialRequirementTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	TArray<FName> SemanticEffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	TArray<FName> TargetCategoryTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	TArray<FName> RequiredCapabilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> RequiredChannels;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> BlockedStates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FName SkeletonProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	TArray<FName> CompatibleSkeletonProfileIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bRequiresTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bCanRunWhileMoving = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	bool bAllowRuntimeModelSelection = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(ClampMin="0.0"))
	float CooldownSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Expressiveness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Energy = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SocialIntensity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	FString VariantDifference;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	FString SourceRecipeHash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	FString KinematicReportHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Template|Catalog")
	FString CatalogContentHash;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template")
	FLLMNPCAnimationPlaybackPolicy AnimationPlayback;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FString SourceProvenanceJson;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Template", meta=(MultiLine="true"))
	FString ValidationReportJson;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template")
	bool IsPublished() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template")
	bool SupportsSkeletonProfile(FName ProfileId) const;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Template")
	bool ValidateTemplate(FString& OutError) const;

	static FString BuildCatalogContentHash(const ULLMNPCMotionTemplate& Template);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
