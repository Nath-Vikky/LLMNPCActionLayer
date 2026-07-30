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

	static void SolveAtBlendedEffector(
		FTransform& InOutUpperTransform,
		FTransform& InOutLowerTransform,
		FTransform& InOutHandTransform,
		const FVector& DesiredEndPosition,
		const FVector& ConfiguredPoleDirection,
		const FVector& FallbackPoleDirection,
		float Alpha,
		float CurrentPoseWeight = 0.45f
	);

	static FQuat BuildPalmFacingRotation(
		const FQuat& CurrentHandRotation,
		const FVector& CurrentFingerDirection,
		const FVector& CurrentPalmNormal,
		const FVector& DesiredFingerDirection,
		const FVector& DesiredPalmNormal
	);

	static bool BuildStableContactPalmBasis(
		const FVector& ComponentForwardDirection,
		const FVector& ComponentUpDirection,
		const FVector& ContactDirection,
		bool bRightHand,
		FVector& OutFingerDirection,
		FVector& OutPalmNormal
	);

	static float ApplyConstrainedWristOrientation(
		const FTransform& LowerArmTransform,
		FTransform& InOutHandTransform,
		const FQuat& DesiredHandRotation,
		float Alpha,
		float MaxAxialTwistDegrees = 87.0f,
		float MaxWristSwingDegrees = 35.0f
	);
};
