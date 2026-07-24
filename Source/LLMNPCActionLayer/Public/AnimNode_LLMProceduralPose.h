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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(PinShownByDefault))
	bool bEnableLeftArmIK = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Skeleton")
	bool bUseSnapshotBoneBindings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator RightHandPalmRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion")
	FRotator LeftHandPalmRotationOffset = FRotator::ZeroRotator;

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

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference LeftUpperArmBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference LeftLowerArmBone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones")
	FBoneReference LeftHandBone;

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

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftThumb01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftThumb02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftThumb03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftIndex01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftIndex02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftIndex03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftMiddle01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftMiddle02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftMiddle03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftRing01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftRing02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftRing03Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftPinky01Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftPinky02Bone;

	UPROPERTY(EditAnywhere, Category="LLM NPC Motion|Bones|Left Fingers")
	FBoneReference LeftPinky03Bone;

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
		FName BoundBoneName,
		const FRotator& Rotation,
		float Alpha
	) const;

	void ApplySimpleRightArmIK(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	void ApplySimpleLeftArmIK(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	void ApplyRightArmAdditiveRotationsLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float Alpha
	) const;

	void ApplyLeftArmAdditiveRotationsLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float Alpha
	) const;

	void ApplyRightOpenPalmIKOrientation(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	void PropagateRightHandChildrenCS(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	void PropagateLeftHandChildrenCS(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms
	) const;

	bool HasAnyRightFingerBone(const FBoneContainer& RequiredBones) const;
	bool HasAnyLeftFingerBone(const FBoneContainer& RequiredBones) const;

	void ApplyFingerRotationLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		const FBoneReference& Bone,
		FName BoundBoneName,
		FName FallbackBoneName,
		const FRotator& Rotation,
		float InAlpha
	) const;

	void ApplyRightFingerPoseLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float InAlpha
	) const;

	void ApplyLeftFingerPoseLocal(
		FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms,
		float InAlpha
	) const;

	void AddGazeToRotations(FComponentSpacePoseContext& Output, FRotator& InOutHeadRotation, FRotator& InOutChestRotation) const;
};
