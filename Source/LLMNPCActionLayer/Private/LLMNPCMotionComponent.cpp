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
	static_cast<void>(TargetActor);
	SubmitMotionPlan(BuildWaveMotionPlan(FString()));
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
	static_cast<void>(TargetRef);

	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("test_wave");
	Plan.Clip.ClipId = TEXT("test_wave_clip");
	Plan.Clip.Duration = 2.2f;
	Plan.Clip.BlendIn = 0.12f;
	Plan.Clip.BlendOut = 0.28f;

	const auto AddKeyTrack = [&Plan](FName ControlId, const TArray<FLLMMotionKeyFloat>& Keys)
	{
		FLLMMotionTrack Track;
		Track.ControlId = ControlId;
		Track.TrackType = ELLMMotionTrackType::Keyframes;
		Track.ValueType = ELLMMotionValueType::Float;
		Track.StartTime = 0.0f;
		Track.EndTime = 2.2f;
		Track.Envelope = ELLMMotionEnvelope::None;
		Track.FloatKeys = Keys;
		Plan.Clip.Tracks.Add(Track);
	};

	// These tracks are UE FQuat delta rotators from the Waving full-pose sample:
	// Delta * first-frame-local-rotation = source-frame-local-rotation.
	AddKeyTrack(TEXT("right_upperarm.pitch"), {
		{0.0f, 0.0f},
		{0.22f, 0.4f},
		{0.43f, -17.5f},
		{0.67f, -49.9f},
		{0.88f, -5.5f},
		{1.10f, -45.6f},
		{1.32f, 14.0f},
		{1.53f, 3.2f},
		{1.77f, 0.9f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_upperarm.yaw"), {
		{0.0f, 0.0f},
		{0.22f, -13.6f},
		{0.43f, -11.1f},
		{0.67f, -11.5f},
		{0.88f, -3.1f},
		{1.10f, -13.2f},
		{1.32f, -2.3f},
		{1.53f, -5.0f},
		{1.77f, -2.7f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_upperarm.roll"), {
		{0.0f, 0.0f},
		{0.22f, -6.2f},
		{0.43f, -75.2f},
		{0.67f, -83.7f},
		{0.88f, -80.0f},
		{1.10f, -77.7f},
		{1.32f, -25.8f},
		{1.53f, -3.9f},
		{1.77f, -0.3f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_lowerarm.pitch"), {
		{0.0f, 0.0f},
		{0.22f, -0.1f},
		{0.43f, -3.3f},
		{0.67f, -2.2f},
		{0.88f, -3.3f},
		{1.10f, -2.6f},
		{1.32f, -3.3f},
		{1.53f, -0.1f},
		{1.77f, 0.1f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_lowerarm.yaw"), {
		{0.0f, 0.0f},
		{0.22f, 2.1f},
		{0.43f, 85.2f},
		{0.67f, 40.6f},
		{0.88f, 91.4f},
		{1.10f, 52.6f},
		{1.32f, 82.4f},
		{1.53f, 0.8f},
		{1.77f, -1.2f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_lowerarm.roll"), {
		{0.0f, 0.0f},
		{0.22f, 0.0f},
		{0.43f, -3.0f},
		{0.67f, -0.8f},
		{0.88f, -3.4f},
		{1.10f, -1.3f},
		{1.32f, -2.9f},
		{1.53f, 0.0f},
		{1.77f, 0.0f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_hand.pitch"), {
		{0.0f, 0.0f},
		{0.22f, -5.6f},
		{0.43f, -0.2f},
		{0.67f, 43.1f},
		{0.88f, -10.0f},
		{1.10f, 28.9f},
		{1.32f, -9.9f},
		{1.53f, -6.3f},
		{1.77f, -1.9f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_hand.yaw"), {
		{0.0f, 0.0f},
		{0.22f, 4.0f},
		{0.43f, 20.0f},
		{0.67f, 16.9f},
		{0.88f, 16.4f},
		{1.10f, 15.0f},
		{1.32f, 7.4f},
		{1.53f, 6.7f},
		{1.77f, 0.0f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_hand.roll"), {
		{0.0f, 0.0f},
		{0.22f, -9.2f},
		{0.43f, 56.3f},
		{0.67f, 42.3f},
		{0.88f, 57.5f},
		{1.10f, 30.9f},
		{1.32f, 32.1f},
		{1.53f, 6.5f},
		{1.77f, 1.6f},
		{2.2f, 0.0f}
	});

	AddKeyTrack(TEXT("right_fingers.open"), {
		{0.0f, 0.0f},
		{0.35f, 1.0f},
		{1.45f, 1.0f},
		{1.65f, 0.35f},
		{2.2f, 0.0f}
	});

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
