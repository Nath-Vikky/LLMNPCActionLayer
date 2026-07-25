#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"
#include "Skeleton/LLMNPCSkeletonConstraintTypes.h"
#include "LLMNPCSkeletonCapabilityTypes.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCCapabilityParameterRange
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName ParameterId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName ValueType = TEXT("normalized_float");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName Unit = TEXT("normalized");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	double MinValue = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	double MaxValue = 1.0;
};

USTRUCT(BlueprintType)
struct FLLMNPCSemanticCapability
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName CapabilityId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName BodyRegion = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FName> SupportedSides;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FLLMNPCCapabilityParameterRange> ParameterRanges;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FName> Requires;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FName> ConflictsWith;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FName> TargetModes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	bool bRuntimeRecipeAllowed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	bool bAuthoringOnly = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString Description;

	// Internal controls participate in validation and hashing but are never serialized to the model view.
	TArray<FName> InternalControlIds;
};

USTRUCT(BlueprintType)
struct FLLMNPCCapabilityGlobalLimits
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	float MaxActionDurationSeconds = 4.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	int32 MaxPrimitiveCount = 12;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FName> AllowedBodyRegions = {
		TEXT("head"),
		TEXT("gaze"),
		TEXT("chest"),
		TEXT("shoulders"),
		TEXT("arms"),
		TEXT("hands")
	};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	bool bAllowsLowerBody = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	bool bAllowsRootMotion = false;
};

USTRUCT(BlueprintType)
struct FLLMNPCSkeletonCapabilitySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString SchemaVersion = TEXT("llmnpc.skeleton_capability.v1");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString ProfileSemanticVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString SkeletonSignature;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString ControlManifestVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString CapabilityHash;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FString GeneratedAt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FLLMNPCSemanticCapability> Capabilities;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	FLLMNPCCapabilityGlobalLimits GlobalLimits;

	// UE-only view. Model serialization deliberately omits these fields.
	FLLMNPCPoseBoneBindings InternalPoseBindings;
	TArray<FLLMNPCKinematicControlConstraint> InternalControlConstraints;
	TArray<FLLMNPCCollisionProxyProfile> InternalCollisionProxies;
};

USTRUCT(BlueprintType)
struct FLLMNPCSkeletonCapabilityBuildResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FString> Errors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Capability")
	TArray<FString> Warnings;
};
