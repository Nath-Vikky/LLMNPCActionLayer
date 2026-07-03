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

	FLLMMotionTrack HandAnchor;
	HandAnchor.ControlId = TEXT("right_hand.ik");
	HandAnchor.ValueType = ELLMMotionValueType::Vector;
	HandAnchor.TrackType = ELLMMotionTrackType::Anchor;
	HandAnchor.Anchor = TEXT("right_wave");
	HandAnchor.StartTime = 0.0f;
	HandAnchor.EndTime = 2.2f;
	HandAnchor.Envelope = ELLMMotionEnvelope::None;
	HandAnchor.Offset = FVector::ZeroVector;
	Plan.Clip.Tracks.Add(HandAnchor);

	FLLMMotionTrack HandOffsetX;
	HandOffsetX.ControlId = TEXT("right_hand.local_offset.x");
	HandOffsetX.TrackType = ELLMMotionTrackType::Keyframes;
	HandOffsetX.ValueType = ELLMMotionValueType::Float;
	HandOffsetX.StartTime = 0.0f;
	HandOffsetX.EndTime = 2.2f;
	HandOffsetX.Envelope = ELLMMotionEnvelope::None;
	HandOffsetX.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, -10.0f},
		{0.42f, -1.0f},
		{0.68f, -14.0f},
		{0.82f, 21.0f},
		{0.96f, -15.0f},
		{1.10f, -10.0f},
		{1.24f, 16.0f},
		{1.52f, -4.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(HandOffsetX);

	FLLMMotionTrack HandOffsetY;
	HandOffsetY.ControlId = TEXT("right_hand.local_offset.y");
	HandOffsetY.TrackType = ELLMMotionTrackType::Keyframes;
	HandOffsetY.ValueType = ELLMMotionValueType::Float;
	HandOffsetY.StartTime = 0.0f;
	HandOffsetY.EndTime = 2.2f;
	HandOffsetY.Envelope = ELLMMotionEnvelope::None;
	HandOffsetY.FloatKeys = {
		{0.0f, -26.0f},
		{0.28f, -10.0f},
		{0.42f, -2.0f},
		{0.68f, 6.0f},
		{0.82f, -7.0f},
		{0.96f, 1.0f},
		{1.10f, 5.0f},
		{1.38f, -12.0f},
		{1.52f, -26.0f},
		{2.2f, -26.0f}
	};
	Plan.Clip.Tracks.Add(HandOffsetY);

	FLLMMotionTrack HandOffsetZ;
	HandOffsetZ.ControlId = TEXT("right_hand.local_offset.z");
	HandOffsetZ.TrackType = ELLMMotionTrackType::Keyframes;
	HandOffsetZ.ValueType = ELLMMotionValueType::Float;
	HandOffsetZ.StartTime = 0.0f;
	HandOffsetZ.EndTime = 2.2f;
	HandOffsetZ.Envelope = ELLMMotionEnvelope::None;
	HandOffsetZ.FloatKeys = {
		{0.0f, -60.0f},
		{0.28f, -50.0f},
		{0.42f, 3.0f},
		{0.56f, 4.0f},
		{0.82f, -3.0f},
		{0.96f, 6.0f},
		{1.24f, -13.0f},
		{1.38f, -44.0f},
		{1.52f, -59.0f},
		{2.2f, -60.0f}
	};
	Plan.Clip.Tracks.Add(HandOffsetZ);

	FLLMMotionTrack UpperArmPitch;
	UpperArmPitch.ControlId = TEXT("right_upperarm.pitch");
	UpperArmPitch.TrackType = ELLMMotionTrackType::Keyframes;
	UpperArmPitch.ValueType = ELLMMotionValueType::Float;
	UpperArmPitch.StartTime = 0.0f;
	UpperArmPitch.EndTime = 2.2f;
	UpperArmPitch.Envelope = ELLMMotionEnvelope::None;
	UpperArmPitch.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, 7.0f},
		{0.42f, 28.0f},
		{0.56f, 32.0f},
		{0.68f, 29.0f},
		{0.82f, 35.0f},
		{1.10f, 27.0f},
		{1.52f, 3.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(UpperArmPitch);

	FLLMMotionTrack UpperArmYaw;
	UpperArmYaw.ControlId = TEXT("right_upperarm.yaw");
	UpperArmYaw.TrackType = ELLMMotionTrackType::Keyframes;
	UpperArmYaw.ValueType = ELLMMotionValueType::Float;
	UpperArmYaw.StartTime = 0.0f;
	UpperArmYaw.EndTime = 2.2f;
	UpperArmYaw.Envelope = ELLMMotionEnvelope::None;
	UpperArmYaw.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, 5.0f},
		{0.42f, 22.0f},
		{0.68f, 26.0f},
		{0.82f, 31.0f},
		{0.96f, 21.0f},
		{1.24f, 24.0f},
		{1.52f, 0.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(UpperArmYaw);

	FLLMMotionTrack UpperArmRoll;
	UpperArmRoll.ControlId = TEXT("right_upperarm.roll");
	UpperArmRoll.TrackType = ELLMMotionTrackType::Keyframes;
	UpperArmRoll.ValueType = ELLMMotionValueType::Float;
	UpperArmRoll.StartTime = 0.0f;
	UpperArmRoll.EndTime = 2.2f;
	UpperArmRoll.Envelope = ELLMMotionEnvelope::None;
	UpperArmRoll.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, -24.0f},
		{0.42f, -51.0f},
		{0.56f, -47.0f},
		{0.68f, -55.0f},
		{0.82f, -30.0f},
		{1.10f, -55.0f},
		{1.24f, -30.0f},
		{1.52f, 0.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(UpperArmRoll);

	FLLMMotionTrack LowerArmYaw;
	LowerArmYaw.ControlId = TEXT("right_lowerarm.yaw");
	LowerArmYaw.TrackType = ELLMMotionTrackType::Keyframes;
	LowerArmYaw.ValueType = ELLMMotionValueType::Float;
	LowerArmYaw.StartTime = 0.0f;
	LowerArmYaw.EndTime = 2.2f;
	LowerArmYaw.Envelope = ELLMMotionEnvelope::None;
	LowerArmYaw.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, 15.0f},
		{0.42f, 55.0f},
		{0.56f, 60.0f},
		{0.68f, 35.0f},
		{0.82f, 65.0f},
		{0.96f, 40.0f},
		{1.24f, 62.0f},
		{1.52f, 0.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(LowerArmYaw);

	FLLMMotionTrack WristPitch;
	WristPitch.ControlId = TEXT("right_hand.pitch");
	WristPitch.TrackType = ELLMMotionTrackType::Keyframes;
	WristPitch.ValueType = ELLMMotionValueType::Float;
	WristPitch.StartTime = 0.0f;
	WristPitch.EndTime = 2.2f;
	WristPitch.Envelope = ELLMMotionEnvelope::None;
	WristPitch.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, 10.0f},
		{0.56f, -7.0f},
		{0.68f, 28.0f},
		{0.82f, -10.0f},
		{1.10f, 30.0f},
		{1.38f, -18.0f},
		{1.52f, -6.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(WristPitch);

	FLLMMotionTrack WristYaw;
	WristYaw.ControlId = TEXT("right_hand.yaw");
	WristYaw.TrackType = ELLMMotionTrackType::Keyframes;
	WristYaw.ValueType = ELLMMotionValueType::Float;
	WristYaw.StartTime = 0.0f;
	WristYaw.EndTime = 2.2f;
	WristYaw.Envelope = ELLMMotionEnvelope::None;
	WristYaw.FloatKeys = {
		{0.0f, 0.0f},
		{0.42f, 23.0f},
		{0.68f, 22.0f},
		{0.96f, 27.0f},
		{1.24f, 21.0f},
		{1.52f, 8.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(WristYaw);

	FLLMMotionTrack WristRoll;
	WristRoll.ControlId = TEXT("right_hand.roll");
	WristRoll.TrackType = ELLMMotionTrackType::Keyframes;
	WristRoll.ValueType = ELLMMotionValueType::Float;
	WristRoll.StartTime = 0.0f;
	WristRoll.EndTime = 2.2f;
	WristRoll.Envelope = ELLMMotionEnvelope::None;
	WristRoll.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, -5.0f},
		{0.42f, 30.0f},
		{0.56f, 42.0f},
		{0.68f, 25.0f},
		{0.82f, 42.0f},
		{1.10f, 20.0f},
		{1.24f, 35.0f},
		{1.52f, 16.0f},
		{2.2f, 0.0f}
	};
	Plan.Clip.Tracks.Add(WristRoll);

	FLLMMotionTrack FingerOpen;
	FingerOpen.ControlId = TEXT("right_fingers.open");
	FingerOpen.TrackType = ELLMMotionTrackType::Keyframes;
	FingerOpen.ValueType = ELLMMotionValueType::Float;
	FingerOpen.StartTime = 0.0f;
	FingerOpen.EndTime = 2.2f;
	FingerOpen.Envelope = ELLMMotionEnvelope::None;
	FingerOpen.FloatKeys = {
		{0.0f, 0.0f},
		{0.28f, 1.0f},
		{1.38f, 1.0f},
		{1.52f, 0.35f},
		{2.2f, 0.0f}
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
