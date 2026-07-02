#include "LLMNPCMotionComponent.h"

#include "LLMNPCAPIClient.h"
#include "LLMNPCActionLayer.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionValidator.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "JsonObjectConverter.h"

ULLMNPCMotionComponent::ULLMNPCMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULLMNPCMotionComponent::BeginPlay()
{
	Super::BeginPlay();

	Validator = NewObject<ULLMNPCMotionValidator>(this);
	Validator->Manifest = ControlManifest;
	APIClient = NewObject<ULLMNPCAPIClient>(this);

	if (bAutoInstallPostProcessAnimBP)
	{
		InstallPostProcessAnimBP();
	}
}

void ULLMNPCMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHasActivePlan)
	{
		StartNextPlan();
	}

	if (bHasActivePlan)
	{
		UpdateActivePlan(DeltaTime);
	}
	else
	{
		CurrentSnapshot = FLLMProceduralPoseSnapshot();
	}
}

bool ULLMNPCMotionComponent::SubmitMotionPlanJson(const FString& JsonString)
{
	LastRawMotionJson = JsonString;

	FLLMMotionPlan Plan;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &Plan, 0, 0))
	{
		LastValidationError = TEXT("MotionPlan JSON parse failed.");
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion: %s"), *LastValidationError);
		return false;
	}

	return SubmitMotionPlan(Plan);
}

bool ULLMNPCMotionComponent::SubmitMotionPlan(FLLMMotionPlan Plan)
{
	if (!Validator)
	{
		Validator = NewObject<ULLMNPCMotionValidator>(this);
	}

	Validator->Manifest = ControlManifest;

	FLLMMotionValidationResult Result = Validator->ValidateAndClamp(Plan);
	if (!Result.bValid)
	{
		LastValidationError = Result.ErrorMessage;
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion: Plan rejected: %s"), *LastValidationError);
		return false;
	}

	if (Queue.Num() >= MaxQueueSize)
	{
		LastValidationError = TEXT("Motion queue is full.");
		return false;
	}

	Queue.Add(Plan);
	FJsonObjectConverter::UStructToJsonObjectString(Plan, LastAcceptedMotionJson);
	LastValidationError.Reset();
	return true;
}

void ULLMNPCMotionComponent::RequestMotionPlanFromContext(const FString& ContextJson)
{
	if (bMotionRequestInFlight)
	{
		LastValidationError = TEXT("A motion request is already in flight.");
		return;
	}

	if (!APIClient)
	{
		APIClient = NewObject<ULLMNPCAPIClient>(this);
	}

	bMotionRequestInFlight = true;
	APIClient->RequestMotionPlan(
		ContextJson,
		FOnLLMMotionPlanReceived::CreateWeakLambda(
			this,
			[this](bool bSuccess, const FLLMMotionPlan& Plan)
			{
				bMotionRequestInFlight = false;

				if (!bSuccess)
				{
					LastValidationError = TEXT("MotionPlan request failed.");
					return;
				}

				SubmitMotionPlan(Plan);
			}
		)
	);
}

void ULLMNPCMotionComponent::RegisterTarget(const FString& TargetRef, AActor* TargetActor)
{
	const FString CleanRef = TargetRef.TrimStartAndEnd();
	if (CleanRef.IsEmpty())
	{
		return;
	}

	if (TargetActor)
	{
		TargetMap.Add(CleanRef, TargetActor);
	}
	else
	{
		TargetMap.Remove(CleanRef);
	}
}

void ULLMNPCMotionComponent::ClearTargets()
{
	TargetMap.Reset();
}

void ULLMNPCMotionComponent::ClearQueue()
{
	Queue.Reset();
}

void ULLMNPCMotionComponent::TestNod()
{
	SubmitMotionPlan(BuildNodMotionPlan());
}

void ULLMNPCMotionComponent::TestWave(AActor* TargetActor)
{
	const FString TargetRef = TargetActor ? TEXT("test_target") : FString();
	if (TargetActor)
	{
		RegisterTarget(TargetRef, TargetActor);
	}

	SubmitMotionPlan(BuildWaveMotionPlan(TargetRef));
}

bool ULLMNPCMotionComponent::SubmitSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, AActor* TargetActor)
{
	const FString TargetRef = TargetActor ? TEXT("test_target") : FString();
	if (TargetActor)
	{
		RegisterTarget(TargetRef, TargetActor);
	}

	return SubmitMotionPlanJson(BuildSampleMotionPlanJson(Sample, TargetRef));
}

FString ULLMNPCMotionComponent::BuildSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, const FString& TargetRef) const
{
	const FLLMMotionPlan Plan = BuildSampleMotionPlan(Sample, TargetRef.TrimStartAndEnd());
	FString JsonString;
	FJsonObjectConverter::UStructToJsonObjectString(Plan, JsonString);
	return JsonString;
}

FLLMProceduralPoseSnapshot ULLMNPCMotionComponent::GetCurrentSnapshot() const
{
	return CurrentSnapshot;
}

FLLMNPCMotionDebugState ULLMNPCMotionComponent::GetDebugState() const
{
	FLLMNPCMotionDebugState State;
	State.LastRawMotionJson = LastRawMotionJson;
	State.LastAcceptedMotionJson = LastAcceptedMotionJson;
	State.LastValidationError = LastValidationError;
	State.ActiveClipId = ActiveClipId;
	State.ActiveTime = ActiveTime;
	State.ActiveDuration = bHasActivePlan ? ActivePlan.Clip.Duration : 0.0f;
	State.QueueCount = Queue.Num();
	State.bHasActivePlan = bHasActivePlan;
	State.bMotionRequestInFlight = bMotionRequestInFlight;
	State.Snapshot = CurrentSnapshot;
	return State;
}

bool ULLMNPCMotionComponent::StartNextPlan()
{
	if (Queue.Num() <= 0)
	{
		return false;
	}

	ActivePlan = Queue[0];
	Queue.RemoveAt(0);
	ActiveTime = 0.0f;
	ActiveClipId = ActivePlan.Clip.ClipId;
	bHasActivePlan = true;
	return true;
}

void ULLMNPCMotionComponent::UpdateActivePlan(float DeltaTime)
{
	ActiveTime += DeltaTime;

	FLLMNPCMotionSampler::SampleClip(
		ActivePlan.Clip,
		ControlManifest,
		GetOwnerMesh(),
		TargetMap,
		ActiveTime,
		CurrentSnapshot
	);

	if (ActiveTime >= ActivePlan.Clip.Duration)
	{
		bHasActivePlan = false;
		ActiveTime = 0.0f;
		ActiveClipId.Reset();
		CurrentSnapshot = FLLMProceduralPoseSnapshot();
	}
}

void ULLMNPCMotionComponent::InstallPostProcessAnimBP()
{
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh || !PostProcessAnimClass)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
	if (!SkeletalMesh)
	{
		return;
	}

	SkeletalMesh->SetPostProcessAnimBlueprint(PostProcessAnimClass);
	Mesh->InitAnim(true);
	UE_LOG(LogLLMNPCActionLayer, Log, TEXT("LLMNPCMotion: Installed post-process AnimBP %s on %s."), *PostProcessAnimClass->GetName(), *SkeletalMesh->GetName());
}

USkeletalMeshComponent* ULLMNPCMotionComponent::GetOwnerMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}

	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FLLMMotionPlan ULLMNPCMotionComponent::BuildNodMotionPlan()
{
	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("test_nod");
	Plan.Clip.ClipId = TEXT("test_nod_clip");
	Plan.Clip.Duration = 0.9f;
	Plan.Clip.BlendIn = 0.12f;
	Plan.Clip.BlendOut = 0.18f;

	FLLMMotionTrack HeadPitch;
	HeadPitch.ControlId = TEXT("head.pitch");
	HeadPitch.TrackType = ELLMMotionTrackType::Oscillator;
	HeadPitch.ValueType = ELLMMotionValueType::Float;
	HeadPitch.StartTime = 0.05f;
	HeadPitch.EndTime = 0.82f;
	HeadPitch.Amplitude = 10.0f;
	HeadPitch.Frequency = 2.0f;
	HeadPitch.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(HeadPitch);

	return Plan;
}

FLLMMotionPlan ULLMNPCMotionComponent::BuildWaveMotionPlan(const FString& TargetRef)
{
	const FString CleanTargetRef = TargetRef.TrimStartAndEnd();
	const bool bUseTarget = !CleanTargetRef.IsEmpty();

	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("test_wave");
	Plan.Clip.ClipId = TEXT("test_wave_clip");
	Plan.Clip.Duration = 1.7f;
	Plan.Clip.BlendIn = 0.18f;
	Plan.Clip.BlendOut = 0.25f;

	if (bUseTarget)
	{
		FLLMMotionTrack GazeTarget;
		GazeTarget.ControlId = TEXT("gaze.target");
		GazeTarget.TrackType = ELLMMotionTrackType::LookAt;
		GazeTarget.ValueType = ELLMMotionValueType::Vector;
		GazeTarget.TargetRef = CleanTargetRef;
		GazeTarget.StartTime = 0.0f;
		GazeTarget.EndTime = 1.7f;
		GazeTarget.Strength = 0.65f;
		Plan.Clip.Tracks.Add(GazeTarget);

		FLLMMotionTrack PalmTarget;
		PalmTarget.ControlId = TEXT("right_hand.palm_target");
		PalmTarget.TrackType = ELLMMotionTrackType::LookAt;
		PalmTarget.ValueType = ELLMMotionValueType::Vector;
		PalmTarget.TargetRef = CleanTargetRef;
		PalmTarget.StartTime = 0.2f;
		PalmTarget.EndTime = 1.45f;
		PalmTarget.Strength = 0.45f;
		Plan.Clip.Tracks.Add(PalmTarget);
	}

	FLLMMotionTrack HandAnchor;
	HandAnchor.ControlId = TEXT("right_hand.ik");
	HandAnchor.ValueType = ELLMMotionValueType::Vector;
	HandAnchor.TrackType = ELLMMotionTrackType::Anchor;
	HandAnchor.Anchor = TEXT("right_wave");
	HandAnchor.StartTime = 0.0f;
	HandAnchor.EndTime = 1.7f;
	HandAnchor.Offset = FVector::ZeroVector;
	Plan.Clip.Tracks.Add(HandAnchor);

	FLLMMotionTrack WaveOffset;
	WaveOffset.ControlId = TEXT("right_hand.local_offset.y");
	WaveOffset.TrackType = ELLMMotionTrackType::Oscillator;
	WaveOffset.ValueType = ELLMMotionValueType::Float;
	WaveOffset.StartTime = 0.35f;
	WaveOffset.EndTime = 1.35f;
	WaveOffset.Amplitude = 5.0f;
	WaveOffset.Frequency = 3.0f;
	WaveOffset.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WaveOffset);

	FLLMMotionTrack WaveLift;
	WaveLift.ControlId = TEXT("right_hand.local_offset.z");
	WaveLift.TrackType = ELLMMotionTrackType::Oscillator;
	WaveLift.ValueType = ELLMMotionValueType::Float;
	WaveLift.StartTime = 0.35f;
	WaveLift.EndTime = 1.35f;
	WaveLift.Amplitude = 3.0f;
	WaveLift.Frequency = 3.0f;
	WaveLift.Phase = HALF_PI;
	WaveLift.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WaveLift);

	FLLMMotionTrack UpperArmSway;
	UpperArmSway.ControlId = TEXT("right_upperarm.roll");
	UpperArmSway.TrackType = ELLMMotionTrackType::Oscillator;
	UpperArmSway.ValueType = ELLMMotionValueType::Float;
	UpperArmSway.StartTime = 0.35f;
	UpperArmSway.EndTime = 1.35f;
	UpperArmSway.Amplitude = 8.0f;
	UpperArmSway.Frequency = 2.0f;
	UpperArmSway.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(UpperArmSway);

	FLLMMotionTrack LowerArmSwing;
	LowerArmSwing.ControlId = TEXT("right_lowerarm.yaw");
	LowerArmSwing.TrackType = ELLMMotionTrackType::Oscillator;
	LowerArmSwing.ValueType = ELLMMotionValueType::Float;
	LowerArmSwing.StartTime = 0.32f;
	LowerArmSwing.EndTime = 1.38f;
	LowerArmSwing.Amplitude = 24.0f;
	LowerArmSwing.Frequency = 3.0f;
	LowerArmSwing.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(LowerArmSwing);

	FLLMMotionTrack LowerArmLift;
	LowerArmLift.ControlId = TEXT("right_lowerarm.pitch");
	LowerArmLift.TrackType = ELLMMotionTrackType::Oscillator;
	LowerArmLift.ValueType = ELLMMotionValueType::Float;
	LowerArmLift.StartTime = 0.32f;
	LowerArmLift.EndTime = 1.38f;
	LowerArmLift.Amplitude = 10.0f;
	LowerArmLift.Frequency = 3.0f;
	LowerArmLift.Phase = HALF_PI;
	LowerArmLift.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(LowerArmLift);

	FLLMMotionTrack WristWave;
	WristWave.ControlId = TEXT("right_hand.roll");
	WristWave.TrackType = ELLMMotionTrackType::Oscillator;
	WristWave.ValueType = ELLMMotionValueType::Float;
	WristWave.StartTime = 0.32f;
	WristWave.EndTime = 1.38f;
	WristWave.Amplitude = 30.0f;
	WristWave.Frequency = 3.0f;
	WristWave.Phase = HALF_PI;
	WristWave.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WristWave);

	FLLMMotionTrack WristYaw;
	WristYaw.ControlId = TEXT("right_hand.yaw");
	WristYaw.TrackType = ELLMMotionTrackType::Oscillator;
	WristYaw.ValueType = ELLMMotionValueType::Float;
	WristYaw.StartTime = 0.32f;
	WristYaw.EndTime = 1.38f;
	WristYaw.Amplitude = 14.0f;
	WristYaw.Frequency = 3.0f;
	WristYaw.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WristYaw);

	FLLMMotionTrack FingerOpen;
	FingerOpen.ControlId = TEXT("right_fingers.open");
	FingerOpen.TrackType = ELLMMotionTrackType::Keyframes;
	FingerOpen.ValueType = ELLMMotionValueType::Float;
	FingerOpen.StartTime = 0.0f;
	FingerOpen.EndTime = 1.7f;
	FingerOpen.FloatKeys = {
		{0.0f, 0.0f},
		{0.25f, 1.0f},
		{1.45f, 1.0f},
		{1.7f, 0.0f}
	};
	Plan.Clip.Tracks.Add(FingerOpen);

	return Plan;
}

FLLMMotionPlan ULLMNPCMotionComponent::BuildInvalidUnknownControlPlan()
{
	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("test_invalid_unknown_control");
	Plan.Clip.ClipId = TEXT("test_invalid_unknown_control_clip");
	Plan.Clip.Duration = 0.5f;
	Plan.Clip.BlendIn = 0.05f;
	Plan.Clip.BlendOut = 0.05f;

	FLLMMotionTrack BadTrack;
	BadTrack.ControlId = TEXT("debug.unknown_control");
	BadTrack.TrackType = ELLMMotionTrackType::Hold;
	BadTrack.ValueType = ELLMMotionValueType::Float;
	BadTrack.StartTime = 0.0f;
	BadTrack.EndTime = 0.5f;
	BadTrack.Amplitude = 1.0f;
	Plan.Clip.Tracks.Add(BadTrack);

	return Plan;
}

FLLMMotionPlan ULLMNPCMotionComponent::BuildSampleMotionPlan(ELLMNPCMotionDebugSample Sample, const FString& TargetRef)
{
	switch (Sample)
	{
	case ELLMNPCMotionDebugSample::Wave:
		return BuildWaveMotionPlan(TargetRef);
	case ELLMNPCMotionDebugSample::InvalidUnknownControl:
		return BuildInvalidUnknownControlPlan();
	case ELLMNPCMotionDebugSample::Nod:
	default:
		return BuildNodMotionPlan();
	}
}
