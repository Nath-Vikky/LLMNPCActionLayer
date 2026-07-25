#include "Quality/LLMNPCPoseOutputContract.h"

void FLLMNPCPoseOutputContract::FinalizeBoneTransforms(
	TArray<FBoneTransform>& InOutBoneTransforms
)
{
	for (int32 Index = InOutBoneTransforms.Num() - 1; Index >= 0; --Index)
	{
		if (InOutBoneTransforms[Index].BoneIndex.GetInt() == INDEX_NONE)
		{
			InOutBoneTransforms.RemoveAtSwap(Index, 1, false);
		}
	}

	InOutBoneTransforms.Sort(FCompareBoneTransformIndex());
	for (int32 Index = InOutBoneTransforms.Num() - 1; Index > 0; --Index)
	{
		if (
			InOutBoneTransforms[Index].BoneIndex ==
			InOutBoneTransforms[Index - 1].BoneIndex
		)
		{
			InOutBoneTransforms.RemoveAt(Index - 1, 1, false);
		}
	}
}

bool FLLMNPCPoseOutputContract::IsValidFinalBuffer(
	const TArray<FBoneTransform>& BoneTransforms
)
{
	int32 LastIndex = INDEX_NONE;
	for (const FBoneTransform& BoneTransform : BoneTransforms)
	{
		const int32 CurrentIndex = BoneTransform.BoneIndex.GetInt();
		if (CurrentIndex == INDEX_NONE || CurrentIndex <= LastIndex)
		{
			return false;
		}
		LastIndex = CurrentIndex;
	}
	return true;
}
