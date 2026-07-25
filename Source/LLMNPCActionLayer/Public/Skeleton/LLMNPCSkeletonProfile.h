#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LLMNPCMotionTypes.h"
#include "Skeleton/LLMNPCSkeletonConstraintTypes.h"
#include "LLMNPCSkeletonProfile.generated.h"

class USkeleton;

USTRUCT(BlueprintType)
struct FLLMNPCBoneAxisBasis
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector PitchAxis = FVector::RightVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector YawAxis = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector RollAxis = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FRotator MinAdditiveRotation = FRotator(-45.0f, -60.0f, -45.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FRotator MaxAdditiveRotation = FRotator(45.0f, 60.0f, 45.0f);
};

USTRUCT(BlueprintType)
struct FLLMNPCIKChainProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName ChainId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName RootBoneSemantic = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName MidBoneSemantic = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName EndBoneSemantic = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector PoleDirectionCS = FVector::BackwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinReachScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxReachScale = 0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton", meta=(ClampMin="0.0", ClampMax="180.0"))
	float PoleSafetyConeDegrees = 70.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCFingerPoseProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName PoseId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TMap<FName, FRotator> SemanticBoneRotations;
};

USTRUCT(BlueprintType)
struct FLLMNPCSkeletonProfileQualityReport
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	FName ProfileId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	FString SkeletonPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	FString SkeletonSignature;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	int32 MappedBoneCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float CoreBoneCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float FingerBoneCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float FingerPoseCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float AxisCalibrationCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float ShoulderCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float ShoulderAxisCalibrationCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float ExtendedFingerPoseCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float KinematicConstraintCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	float CollisionProxyCoverage = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	int32 IKChainCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	bool bSignatureCurrent = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	bool bPassed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	bool bCapabilityReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	TArray<FString> Errors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Quality")
	TArray<FString> Warnings;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCSkeletonProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FString SemanticVersion = TEXT("1.0.0");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TSoftObjectPtr<USkeleton> Skeleton;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TMap<FName, FName> SemanticBoneMap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TMap<FName, FLLMNPCBoneAxisBasis> AxisBases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	bool bApplyAxisCalibrationAtRuntime = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector ComponentForwardDirectionCS = FVector::RightVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FVector ComponentUpDirectionCS = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TArray<FLLMNPCIKChainProfile> IKChains;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	TArray<FLLMNPCFingerPoseProfile> FingerPoses;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Calibration")
	int32 FingerPoseCalibrationRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	TArray<FLLMNPCKinematicControlConstraint> ControlConstraints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	FLLMNPCUpperBodyConstraintProfile UpperBodyConstraints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	TArray<FLLMNPCCollisionProxyProfile> CollisionProxies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton|Constraints")
	TArray<FName> StableGroundContactBoneSemantics;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Skeleton")
	FString SkeletonSignature;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Skeleton")
	FName FindBoneName(FName SemanticBone) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Skeleton")
	FLLMNPCPoseBoneBindings BuildPoseBoneBindings() const;

	const FLLMNPCKinematicControlConstraint* FindControlConstraint(FName ControlId) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Skeleton")
	bool IsCompatibleSkeleton(const USkeleton* CandidateSkeleton) const;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Skeleton")
	bool ValidateProfile(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Skeleton")
	FLLMNPCSkeletonProfileQualityReport BuildQualityReport() const;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="LLM NPC|Skeleton")
	void RefreshSkeletonSignature();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	static FString BuildSkeletonSignature(
		const USkeleton* InSkeleton,
		const FString& ProfileVersion
	);
};
