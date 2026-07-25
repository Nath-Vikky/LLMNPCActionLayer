#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

class LLMNPCACTIONLAYER_API FLLMNPCPoseOutputContract
{
public:
	// Skeletal controls must emit one transform per valid Compact Pose index in ascending order.
	static void FinalizeBoneTransforms(TArray<FBoneTransform>& InOutBoneTransforms);

	static bool IsValidFinalBuffer(const TArray<FBoneTransform>& BoneTransforms);
};
