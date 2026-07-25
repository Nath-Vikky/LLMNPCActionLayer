#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCControlManifest.generated.h"

USTRUCT(BlueprintType)
struct FLLMWeightedBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName BoneName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FVector AxisMask = FVector(1.0f, 1.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct FLLMIKChainDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName RootBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName MidBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName EndBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FVector PoleVectorCS = FVector(0.0f, 40.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion", meta=(ClampMin="0.0"))
	float MaxReach = 90.0f;
};

USTRUCT(BlueprintType)
struct FLLMControlDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName ControlId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	ELLMControlSolverType SolverType = ELLMControlSolverType::AdditiveRotation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TArray<ELLMMotionTrackType> AllowedTrackTypes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TArray<FLLMWeightedBone> WeightedBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FLLMIKChainDefinition IKChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	float MinValue = -35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	float MaxValue = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	bool bRequiresTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	bool bAllowRuntimeModel = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	bool bAllowTemplateAuthoring = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion", meta=(DeprecatedProperty, DeprecationMessage="Use bAllowRuntimeModel."))
	bool bAllowLLM = true;
};

USTRUCT(BlueprintType)
struct FLLMAnchorDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName AnchorId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FName BoneName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FVector OffsetCS = FVector::ZeroVector;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCControlManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	FString ManifestVersion = TEXT("llmnpc.control_manifest.v1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TArray<FLLMControlDefinition> Controls;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TArray<FLLMAnchorDefinition> Anchors;

	const FLLMControlDefinition* FindControl(FName ControlId) const;
	const FLLMAnchorDefinition* FindAnchor(FName AnchorId) const;

	static const FLLMControlDefinition* FindBuiltInControl(FName ControlId);
	static const FLLMAnchorDefinition* FindBuiltInAnchor(FName AnchorId);
	static const TArray<FLLMControlDefinition>& GetBuiltInControls();
	static const TArray<FLLMAnchorDefinition>& GetBuiltInAnchors();
	static const FString& GetBuiltInManifestVersion();
};
