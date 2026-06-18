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

	SubmitMotionPlan(Plan);
}

void ULLMNPCMotionComponent::TestWave(AActor* TargetActor)
{
	if (TargetActor)
	{
		RegisterTarget(TEXT("test_target"), TargetActor);
	}

	FLLMMotionPlan Plan;
	Plan.Intent = TEXT("test_wave");
	Plan.Clip.ClipId = TEXT("test_wave_clip");
	Plan.Clip.Duration = 1.7f;
	Plan.Clip.BlendIn = 0.18f;
	Plan.Clip.BlendOut = 0.25f;

	if (TargetActor)
	{
		FLLMMotionTrack GazeTarget;
		GazeTarget.ControlId = TEXT("gaze.target");
		GazeTarget.TrackType = ELLMMotionTrackType::LookAt;
		GazeTarget.ValueType = ELLMMotionValueType::Vector;
		GazeTarget.TargetRef = TEXT("test_target");
		GazeTarget.StartTime = 0.0f;
		GazeTarget.EndTime = 1.7f;
		GazeTarget.Strength = 0.65f;
		Plan.Clip.Tracks.Add(GazeTarget);

		FLLMMotionTrack PalmTarget;
		PalmTarget.ControlId = TEXT("right_hand.palm_target");
		PalmTarget.TrackType = ELLMMotionTrackType::LookAt;
		PalmTarget.ValueType = ELLMMotionValueType::Vector;
		PalmTarget.TargetRef = TEXT("test_target");
		PalmTarget.StartTime = 0.2f;
		PalmTarget.EndTime = 1.45f;
		PalmTarget.Strength = 1.0f;
		Plan.Clip.Tracks.Add(PalmTarget);
	}

	FLLMMotionTrack HandAnchor;
	HandAnchor.ControlId = TEXT("right_hand.ik");
	HandAnchor.ValueType = ELLMMotionValueType::Vector;
	HandAnchor.StartTime = 0.0f;
	HandAnchor.EndTime = 1.7f;
	if (TargetActor)
	{
		HandAnchor.TrackType = ELLMMotionTrackType::IKReach;
		HandAnchor.TargetRef = TEXT("test_target");
		HandAnchor.Reach = 0.72f;
		HandAnchor.Offset = FVector(0.0f, 10.0f, 18.0f);
	}
	else
	{
		HandAnchor.TrackType = ELLMMotionTrackType::Anchor;
		HandAnchor.Anchor = TEXT("head_right");
		HandAnchor.Offset = FVector(15.0f, 8.0f, 5.0f);
	}
	Plan.Clip.Tracks.Add(HandAnchor);

	FLLMMotionTrack WaveOffset;
	WaveOffset.ControlId = TEXT("right_hand.local_offset.y");
	WaveOffset.TrackType = ELLMMotionTrackType::Oscillator;
	WaveOffset.ValueType = ELLMMotionValueType::Float;
	WaveOffset.StartTime = 0.35f;
	WaveOffset.EndTime = 1.35f;
	WaveOffset.Amplitude = 16.0f;
	WaveOffset.Frequency = 3.0f;
	WaveOffset.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WaveOffset);

	FLLMMotionTrack WaveLift;
	WaveLift.ControlId = TEXT("right_hand.local_offset.z");
	WaveLift.TrackType = ELLMMotionTrackType::Oscillator;
	WaveLift.ValueType = ELLMMotionValueType::Float;
	WaveLift.StartTime = 0.35f;
	WaveLift.EndTime = 1.35f;
	WaveLift.Amplitude = 5.0f;
	WaveLift.Frequency = 3.0f;
	WaveLift.Phase = HALF_PI;
	WaveLift.Envelope = ELLMMotionEnvelope::Smooth;
	Plan.Clip.Tracks.Add(WaveLift);

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

	SubmitMotionPlan(Plan);
}

FLLMProceduralPoseSnapshot ULLMNPCMotionComponent::GetCurrentSnapshot() const
{
	return CurrentSnapshot;
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
