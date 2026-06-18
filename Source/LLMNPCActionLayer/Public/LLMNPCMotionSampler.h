#pragma once

#include "CoreMinimal.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionTypes.h"

class USkeletalMeshComponent;

class LLMNPCACTIONLAYER_API FLLMNPCMotionSampler
{
public:
	static void SampleClip(
		const FLLMMotionClip& Clip,
		const ULLMNPCControlManifest* Manifest,
		USkeletalMeshComponent* Mesh,
		const TMap<FString, TObjectPtr<AActor>>& TargetMap,
		float Time,
		FLLMProceduralPoseSnapshot& OutSnapshot
	);

	static float EvaluateFloatTrack(const FLLMMotionTrack& Track, float Time);
	static float EvaluateEnvelope(const FLLMMotionTrack& Track, float Time);
	static float EvaluateKeyframes(const TArray<FLLMMotionKeyFloat>& Keys, float Time);

private:
	static FVector BuildAnchorTargetCS(
		const FLLMMotionTrack& Track,
		const ULLMNPCControlManifest* Manifest,
		USkeletalMeshComponent* Mesh
	);

	static FVector BuildReachTargetCS(
		const FLLMMotionTrack& Track,
		USkeletalMeshComponent* Mesh,
		const TMap<FString, TObjectPtr<AActor>>& TargetMap
	);
};
