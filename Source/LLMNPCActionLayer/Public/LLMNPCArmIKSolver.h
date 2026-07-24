#pragma once

#include "CoreMinimal.h"

class LLMNPCACTIONLAYER_API FLLMNPCArmIKSolver
{
public:
	static FVector BuildStableJointTarget(
		const FVector& RootPosition,
		const FVector& CurrentJointPosition,
		const FVector& DesiredEndPosition,
		const FVector& ConfiguredPoleDirection,
		const FVector& FallbackPoleDirection,
		float ChainLength,
		float CurrentPoseWeight = 0.45f
	);

	static void MaintainEndEffectorRelativeRotation(
		const FTransform& OriginalParentTransform,
		const FTransform& OriginalEndTransform,
		const FTransform& SolvedParentTransform,
		FTransform& InOutSolvedEndTransform
	);

	static FQuat BuildPalmFacingRotation(
		const FQuat& CurrentHandRotation,
		const FVector& CurrentFingerDirection,
		const FVector& CurrentPalmNormal,
		const FVector& DesiredFingerDirection,
		const FVector& DesiredPalmNormal
	);
};
