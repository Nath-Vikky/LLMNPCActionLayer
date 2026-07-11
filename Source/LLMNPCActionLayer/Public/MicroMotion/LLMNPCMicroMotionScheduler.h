#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"

struct FLLMNPCMicroMotionConfig
{
	bool bEnabled = true;
	bool bEnableGaze = true;
	float Amplitude = 1.0f;
	float BreathingFrequencyHz = 0.2f;
	float GazeAlpha = 0.18f;
	FVector2D GazeSwitchInterval = FVector2D(2.5f, 5.0f);
};

struct FLLMNPCMicroMotionState
{
	FRandomStream RandomStream;
	float Time = 0.0f;
	float NextNodTime = 0.0f;
	float NodStartTime = -1.0f;
	float NextGazeSwitchTime = 0.0f;
	FString GazeTargetRef;
	int32 Seed = 0;

	void Initialize(int32 InSeed);
};

class LLMNPCACTIONLAYER_API FLLMNPCMicroMotionScheduler
{
public:
	static void Update(
		const FLLMNPCMicroMotionConfig& Config,
		FLLMNPCMicroMotionState& State,
		float DeltaTime,
		const TMap<FString, FVector>& GazeTargetsCS,
		bool bHeadBusy,
		bool bChestBusy,
		bool bGazeBusy,
		FLLMProceduralPoseSnapshot& InOutSnapshot
	);
};
