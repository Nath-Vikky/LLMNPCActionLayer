#pragma once

#include "CoreMinimal.h"

struct FLLMNPCArmChainTransforms
{
	FTransform UpperCS;
	FTransform LowerCS;
	FTransform HandCS;
};

class LLMNPCACTIONLAYER_API FLLMNPCMotionMirror
{
public:
	static FLLMNPCArmChainTransforms MirrorRightArmFKAcrossSkeletonX(
		const FTransform& RightParentCS,
		const FLLMNPCArmChainTransforms& RightOriginal,
		const FRotator& RightUpperDelta,
		const FRotator& RightLowerDelta,
		const FRotator& RightHandDelta,
		const FTransform& LeftParentCS,
		const FLLMNPCArmChainTransforms& LeftOriginal,
		float Alpha
	);

	static FQuat MirrorComponentRotationAcrossSkeletonX(const FQuat& Rotation);
};
