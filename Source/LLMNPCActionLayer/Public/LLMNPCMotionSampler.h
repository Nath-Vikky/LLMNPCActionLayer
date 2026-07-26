#pragma once

#include "CoreMinimal.h"
#include "Context/LLMNPCExecutionContextTypes.h"
#include "Context/LLMNPCResolvedMotionModifiers.h"
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
		FLLMProceduralPoseSnapshot& OutSnapshot,
		const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples = nullptr,
		const FLLMNPCResolvedMotionModifiers* ResolvedModifiers = nullptr
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
		const TMap<FString, TObjectPtr<AActor>>& TargetMap,
		const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples,
		const FLLMNPCResolvedMotionModifiers* ResolvedModifiers,
		float& OutTargetAlpha,
		bool bLeftHand
	);

	static bool ResolveTargetLocationWS(
		const FString& TargetRef,
		const TMap<FString, TObjectPtr<AActor>>& TargetMap,
		const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples,
		FVector& OutLocationWS,
		float& OutTargetAlpha
	);
};
