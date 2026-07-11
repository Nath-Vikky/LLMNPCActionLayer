#include "MicroMotion/LLMNPCMicroMotionScheduler.h"

void FLLMNPCMicroMotionState::Initialize(int32 InSeed)
{
	Seed = InSeed == 0 ? 1 : InSeed;
	RandomStream.Initialize(Seed);
	Time = 0.0f;
	NodStartTime = -1.0f;
	NextNodTime = RandomStream.FRandRange(4.0f, 8.0f);
	NextGazeSwitchTime = 0.0f;
	GazeTargetRef.Reset();
}

void FLLMNPCMicroMotionScheduler::Update(
	const FLLMNPCMicroMotionConfig& Config,
	FLLMNPCMicroMotionState& State,
	float DeltaTime,
	const TMap<FString, FVector>& GazeTargetsCS,
	bool bHeadBusy,
	bool bChestBusy,
	bool bGazeBusy,
	FLLMProceduralPoseSnapshot& InOutSnapshot
)
{
	if (!Config.bEnabled || DeltaTime <= 0.0f)
	{
		return;
	}
	State.Time += DeltaTime;
	const float Amplitude = FMath::Clamp(Config.Amplitude, 0.0f, 2.0f);
	const float BreathPhase = State.Time * FMath::Max(0.05f, Config.BreathingFrequencyHz) * 2.0f * PI;

	if (!bChestBusy)
	{
		InOutSnapshot.ChestPitch += FMath::Sin(BreathPhase) * 0.55f * Amplitude;
		InOutSnapshot.ChestRoll += FMath::Sin(BreathPhase * 0.47f + 0.8f) * 0.22f * Amplitude;
		InOutSnapshot.GlobalAlpha = FMath::Max(InOutSnapshot.GlobalAlpha, 1.0f);
	}

	if (!bHeadBusy)
	{
		InOutSnapshot.HeadYaw += FMath::Sin(BreathPhase * 0.31f + 1.1f) * 0.35f * Amplitude;
		if (State.NodStartTime < 0.0f && State.Time >= State.NextNodTime)
		{
			State.NodStartTime = State.Time;
		}
		if (State.NodStartTime >= 0.0f)
		{
			const float NodTime = State.Time - State.NodStartTime;
			if (NodTime <= 0.55f)
			{
				InOutSnapshot.HeadPitch += FMath::Sin(NodTime / 0.55f * PI) * 1.2f * Amplitude;
				InOutSnapshot.GlobalAlpha = FMath::Max(InOutSnapshot.GlobalAlpha, 1.0f);
			}
			else
			{
				State.NodStartTime = -1.0f;
				State.NextNodTime = State.Time + State.RandomStream.FRandRange(5.0f, 10.0f);
			}
		}
	}

	if (!Config.bEnableGaze || bGazeBusy || bHeadBusy || GazeTargetsCS.IsEmpty())
	{
		return;
	}

	if (State.Time >= State.NextGazeSwitchTime || !GazeTargetsCS.Contains(State.GazeTargetRef))
	{
		TArray<FString> TargetRefs;
		GazeTargetsCS.GetKeys(TargetRefs);
		TargetRefs.Sort();
		if (TargetRefs.Num() > 1 && TargetRefs[0] == State.GazeTargetRef)
		{
			const int32 Offset = State.RandomStream.RandRange(1, TargetRefs.Num() - 1);
			State.GazeTargetRef = TargetRefs[Offset];
		}
		else
		{
			State.GazeTargetRef = TargetRefs[State.RandomStream.RandRange(0, TargetRefs.Num() - 1)];
		}
		const float MinInterval = FMath::Max(0.25f, static_cast<float>(Config.GazeSwitchInterval.X));
		const float MaxInterval = FMath::Max(MinInterval, static_cast<float>(Config.GazeSwitchInterval.Y));
		State.NextGazeSwitchTime = State.Time + State.RandomStream.FRandRange(MinInterval, MaxInterval);
	}

	if (const FVector* TargetCS = GazeTargetsCS.Find(State.GazeTargetRef))
	{
		InOutSnapshot.GazeTargetCS = *TargetCS;
		InOutSnapshot.GazeAlpha = FMath::Max(
			InOutSnapshot.GazeAlpha,
			FMath::Clamp(Config.GazeAlpha, 0.0f, 0.35f)
		);
		InOutSnapshot.GlobalAlpha = FMath::Max(InOutSnapshot.GlobalAlpha, 1.0f);
	}
}
