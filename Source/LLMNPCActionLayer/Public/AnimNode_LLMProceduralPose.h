#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "LLMNPCMotionTypes.h"
#include "AnimNode_LLMProceduralPose.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct LLMNPCACTIONLAYER_API FAnimNode_LLMProceduralPose : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(PinShownByDefault))
	FLLMProceduralPoseSnapshot Snapshot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(PinShownByDefault))
	bool bEnableRightArmIK = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator RightHandPalmRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference HeadBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference ChestBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference RightUpperArmBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference RightLowerArmBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference RightHandBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightThumb01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightThumb02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightThumb03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightIndex01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightIndex02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightIndex03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightMiddle01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightMiddle02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightMiddle03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightRing01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightRing02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightRing03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightPinky01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightPinky02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Right Fingers")
	FBoneReference RightPinky03Bone;

	FAnimNode_LLMProceduralPose();

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

protected:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;

private:
	void ApplyAdditiveRotationCS(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		const FBoneReference& Bone,
		const FRotator& Rotation,
		float Alpha
	) const;

	void ApplySimpleRightArmIK(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	void ApplyRightArmAdditiveRotationsLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float Alpha
	) const;

	void PropagateRightHandChildrenCS(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	bool HasAnyRightFingerBone(const FBoneContainer& RequiredBones) const;

	void ApplyFingerRotationLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		const FBoneReference& Bone,
		FName FallbackBoneName,
		const FRotator& Rotation,
		float InAlpha
	) const;

	void ApplyRightFingerPoseLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float InAlpha
	) const;

	void AddGazeToRotations(FComponentSpacePoseContext& Output, FRotator& InOutHeadRotation, FRotator& InOutChestRotation) const;
};
