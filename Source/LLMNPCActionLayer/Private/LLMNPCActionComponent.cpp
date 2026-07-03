#include "LLMNPCActionComponent.h"

#include "LLMNPCActionLayer.h"
#include "LLMNPCActionLLMClient.h"
#include "LLMNPCActionSettings.h"
#include "LLMNPCActionValidator.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"

namespace
{
float SmoothRange(float Edge0, float Edge1, float Value)
{
	if (FMath::IsNearlyEqual(Edge0, Edge1))
	{
		return Value >= Edge1 ? 1.0f : 0.0f;
	}

	const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float ActionSustainAlpha(float NormalizedTime)
{
	const float InAlpha = SmoothRange(0.0f, 0.2f, NormalizedTime);
	const float OutAlpha = 1.0f - SmoothRange(0.78f, 1.0f, NormalizedTime);
	return FMath::Clamp(InAlpha * OutAlpha, 0.0f, 1.0f);
}

}

ULLMNPCActionComponent::ULLMNPCActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void ULLMNPCActionComponent::BeginPlay()
{
	Super::BeginPlay();

	Validator = NewObject<ULLMNPCActionValidator>(this);
	LLMClient = NewObject<ULLMNPCActionLLMClient>(this);

	if (const ULLMNPCActionSettings* Settings = GetDefault<ULLMNPCActionSettings>())
	{
		MaxQueuedActions = FMath::Max(1, Settings->MaxActionsPerPlan);
	}
}

void ULLMNPCActionComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction
)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActionActive)
	{
		StartNextAction();
	}

	if (bActionActive)
	{
		UpdateActiveAction(DeltaTime);
	}
}

bool ULLMNPCActionComponent::SubmitActionPlanJson(const FString& JsonString)
{
	FLLMNPCActionPlan Plan;

	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &Plan, 0, 0))
	{
		LastRejectedReason = TEXT("Failed to parse action plan JSON.");
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: %s"), *LastRejectedReason);
		return false;
	}

	return SubmitActionPlan(Plan);
}

bool ULLMNPCActionComponent::SubmitActionPlan(const FLLMNPCActionPlan& Plan)
{
	LastRejectedReason.Reset();

	if (!Validator)
	{
		Validator = NewObject<ULLMNPCActionValidator>(this);
	}

	int32 AcceptedCount = 0;
	const int32 AvailableSlots = FMath::Max(0, MaxQueuedActions - ActionQueue.Num());
	const int32 PlanLimit = FMath::Min(AvailableSlots, Plan.Actions.Num());

	for (int32 Index = 0; Index < PlanLimit; ++Index)
	{
		FLLMNPCActionRequest Action = Plan.Actions[Index];

		FString ValidationReason;
		if (!Validator->ValidateAndClamp(Action, ActionManifest, ValidationReason))
		{
			LastRejectedReason = ValidationReason;
			UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: Rejected action. %s"), *ValidationReason);
			continue;
		}

		if (!CanAcceptAction(Action, ValidationReason))
		{
			LastRejectedReason = ValidationReason;
			UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: Rejected action. %s"), *ValidationReason);
			continue;
		}

		ActionQueue.Add(Action);
		++AcceptedCount;
	}

	if (Plan.Actions.Num() > PlanLimit)
	{
		LastRejectedReason = FString::Printf(
			TEXT("Plan had %d action(s), but only %d queue slot(s) were available."),
			Plan.Actions.Num(),
			AvailableSlots
		);
	}

	if (AcceptedCount > 0)
	{
		FJsonObjectConverter::UStructToJsonObjectString(Plan, LastAcceptedPlanJson);
	}

	return AcceptedCount > 0;
}

void ULLMNPCActionComponent::RequestActionPlanFromContext(const FString& ContextJson)
{
	if (bLLMRequestInFlight)
	{
		LastRejectedReason = TEXT("An LLM action request is already in flight.");
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: %s"), *LastRejectedReason);
		return;
	}

	if (!LLMClient)
	{
		LLMClient = NewObject<ULLMNPCActionLLMClient>(this);
	}

	bLLMRequestInFlight = true;
	LLMClient->RequestActionPlan(
		ContextJson,
		FOnLLMNPCActionPlanReceived::CreateWeakLambda(
			this,
			[this](bool bSuccess, const FLLMNPCActionPlan& Plan)
			{
				bLLMRequestInFlight = false;

				if (!bSuccess)
				{
					LastRejectedReason = TEXT("LLM action request failed.");
					return;
				}

				SubmitActionPlan(Plan);
			}
		)
	);
}

void ULLMNPCActionComponent::RegisterTargetRef(const FString& TargetRef, AActor* TargetActor)
{
	const FName TargetName(*TargetRef.TrimStartAndEnd());
	if (TargetName.IsNone())
	{
		return;
	}

	if (TargetActor)
	{
		RegisteredTargets.Add(TargetName, TargetActor);
	}
	else
	{
		RegisteredTargets.Remove(TargetName);
	}
}

void ULLMNPCActionComponent::UnregisterTargetRef(const FString& TargetRef)
{
	RegisteredTargets.Remove(FName(*TargetRef.TrimStartAndEnd()));
}

void ULLMNPCActionComponent::ClearActionQueue()
{
	ActionQueue.Reset();
}

void ULLMNPCActionComponent::StopActiveAction()
{
	bActionActive = false;
	ActiveElapsed = 0.0f;
	ClearRuntimeState();
}

void ULLMNPCActionComponent::TestWave(AActor* TargetActor)
{
	static_cast<void>(TargetActor);

	FLLMNPCActionPlan Plan;
	Plan.Intent = TEXT("test_wave");

	FLLMNPCActionRequest Action;
	Action.ActionId = TEXT("gesture.wave");
	Action.TargetRef.Reset();
	Action.Hand = ELLMNPCHand::Right;
	Action.Emotion = ELLMNPCEmotion::Friendly;
	Action.Amplitude = 0.7f;
	Action.Speed = 0.7f;
	Action.Duration = 1.8f;
	Action.Height = 0.8f;
	Action.Beats = 3;

	Plan.Actions.Add(Action);
	SubmitActionPlan(Plan);
}

void ULLMNPCActionComponent::TestPoint(AActor* TargetActor)
{
	FLLMNPCActionPlan Plan;
	Plan.Intent = TEXT("test_point");

	FLLMNPCActionRequest Action;
	Action.ActionId = TEXT("gesture.point");
	Action.TargetRef = TargetActor ? TEXT("test_target") : TEXT("player");
	Action.Hand = ELLMNPCHand::Right;
	Action.Emotion = ELLMNPCEmotion::Urgent;
	Action.Amplitude = 0.8f;
	Action.Speed = 0.8f;
	Action.Duration = 1.3f;
	Action.Height = 0.6f;
	Action.Beats = 1;

	if (TargetActor)
	{
		RegisterTargetRef(Action.TargetRef, TargetActor);
	}

	Plan.Actions.Add(Action);
	SubmitActionPlan(Plan);
}

void ULLMNPCActionComponent::TestNod(AActor* TargetActor)
{
	FLLMNPCActionPlan Plan;
	Plan.Intent = TEXT("test_nod");

	FLLMNPCActionRequest Action;
	Action.ActionId = TEXT("gesture.nod");
	Action.TargetRef = TargetActor ? TEXT("test_target") : TEXT("player");
	Action.Hand = ELLMNPCHand::Auto;
	Action.Emotion = ELLMNPCEmotion::Neutral;
	Action.Amplitude = 0.5f;
	Action.Speed = 0.8f;
	Action.Duration = 0.9f;
	Action.Beats = 2;

	if (TargetActor)
	{
		RegisterTargetRef(Action.TargetRef, TargetActor);
	}

	Plan.Actions.Add(Action);
	SubmitActionPlan(Plan);
}

bool ULLMNPCActionComponent::StartNextAction()
{
	if (ActionQueue.Num() <= 0)
	{
		ClearRuntimeState();
		return false;
	}

	ActiveAction = ActionQueue[0];
	ActionQueue.RemoveAt(0);

	ActiveElapsed = 0.0f;
	bActionActive = true;

	RuntimeState = FLLMNPCRuntimeGestureState();
	RuntimeState.bHasActiveGesture = true;
	RuntimeState.ActiveActionId = FName(*ActiveAction.ActionId);
	RuntimeState.ActiveTargetRef = FName(*ActiveAction.TargetRef);

	AActor* ResolvedTarget = nullptr;
	if (ActiveAction.ActionId != TEXT("gesture.wave") && ResolveTarget(ActiveAction.TargetRef, ResolvedTarget))
	{
		CurrentTargetActor = ResolvedTarget;
	}
	else
	{
		CurrentTargetActor = nullptr;
	}

	return true;
}

bool ULLMNPCActionComponent::ResolveTarget(const FString& TargetRef, AActor*& OutTarget) const
{
	OutTarget = nullptr;

	const FName TargetName(*TargetRef.TrimStartAndEnd());
	if (!TargetName.IsNone())
	{
		if (const TObjectPtr<AActor>* RegisteredActor = RegisteredTargets.Find(TargetName))
		{
			OutTarget = RegisteredActor->Get();
			if (IsValid(OutTarget))
			{
				return true;
			}
		}
	}

	if (TargetRef.Equals(TEXT("player"), ESearchCase::IgnoreCase))
	{
		OutTarget = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
		return IsValid(OutTarget);
	}

	return false;
}

bool ULLMNPCActionComponent::CanAcceptAction(const FLLMNPCActionRequest& Action, FString& OutReason) const
{
	OutReason.Reset();

	if (Action.BodyMask == ELLMNPCBodyMask::FullBody && !bCanUseFullBodyActions)
	{
		OutReason = FString::Printf(TEXT("%s requested FullBody, but full-body actions are disabled."), *Action.ActionId);
		return false;
	}

	if (Action.BodyMask == ELLMNPCBodyMask::UpperBody && bUpperBodyBusy)
	{
		OutReason = FString::Printf(TEXT("%s needs upper body, but upper body is busy."), *Action.ActionId);
		return false;
	}

	const bool bNeedsHand =
		Action.ActionId == TEXT("gesture.wave") ||
		Action.ActionId == TEXT("gesture.point");

	if (bNeedsHand && bRightHandBusy && (
		Action.Hand == ELLMNPCHand::Right ||
		Action.Hand == ELLMNPCHand::Auto ||
		Action.Hand == ELLMNPCHand::Both))
	{
		OutReason = FString::Printf(TEXT("%s needs the right hand, but right hand is busy."), *Action.ActionId);
		return false;
	}

	if (bNeedsHand && bLeftHandBusy && (
		Action.Hand == ELLMNPCHand::Left ||
		Action.Hand == ELLMNPCHand::Both))
	{
		OutReason = FString::Printf(TEXT("%s needs the left hand, but left hand is busy."), *Action.ActionId);
		return false;
	}

	if (ULLMNPCActionValidator::IsTargetRequired(Action.ActionId, ActionManifest))
	{
		AActor* TargetActor = nullptr;
		if (!ResolveTarget(Action.TargetRef, TargetActor))
		{
			OutReason = FString::Printf(
				TEXT("%s requires a resolvable TargetRef, but '%s' was not found."),
				*Action.ActionId,
				*Action.TargetRef
			);
			return false;
		}
	}

	return true;
}

void ULLMNPCActionComponent::UpdateActiveAction(float DeltaTime)
{
	ActiveElapsed += DeltaTime;

	const float Duration = FMath::Max(0.01f, ActiveAction.Duration);
	const float T = FMath::Clamp(ActiveElapsed / Duration, 0.0f, 1.0f);

	RuntimeState.bHasActiveGesture = true;
	RuntimeState.ActiveActionId = FName(*ActiveAction.ActionId);
	RuntimeState.ActiveTargetRef = FName(*ActiveAction.TargetRef);
	RuntimeState.GestureAlpha = FMath::Sin(T * PI);

	if (CurrentTargetActor)
	{
		RuntimeState.GazeTargetWS = CurrentTargetActor->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f);
	}

	if (ActiveAction.ActionId == TEXT("gaze.look_at"))
	{
		UpdateGaze(T);
	}
	else if (ActiveAction.ActionId == TEXT("gesture.wave"))
	{
		UpdateWave(T);
	}
	else if (ActiveAction.ActionId == TEXT("gesture.point"))
	{
		UpdateGaze(T);
		UpdatePoint(T);
	}
	else if (ActiveAction.ActionId == TEXT("gesture.nod"))
	{
		UpdateGaze(T);
		UpdateNod(T);
	}

	if (T >= 1.0f)
	{
		bActionActive = false;
		ActiveElapsed = 0.0f;
		ClearRuntimeState();
	}
}

void ULLMNPCActionComponent::ClearRuntimeState()
{
	RuntimeState = FLLMNPCRuntimeGestureState();
}

void ULLMNPCActionComponent::UpdateGaze(float NormalizedTime)
{
	if (!CurrentTargetActor)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector EyeFallback = Owner->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f);
	const FVector Head = GetSafeBoneLocation(TEXT("head"), EyeFallback);
	const FVector Target = CurrentTargetActor->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f);
	const FVector LocalDirection = Owner->GetActorTransform().InverseTransformVectorNoScale(Target - Head).GetSafeNormal();

	const float Alpha = ActionSustainAlpha(NormalizedTime);
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Y, LocalDirection.X));
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(LocalDirection.Z, FMath::Max(1.0f, FVector2D(LocalDirection.X, LocalDirection.Y).Size())));

	RuntimeState.HeadYawOffset = FMath::Clamp(Yaw, -35.0f, 35.0f) * Alpha;
	RuntimeState.HeadPitchOffset += FMath::Clamp(Pitch, -20.0f, 20.0f) * Alpha * 0.35f;
}

void ULLMNPCActionComponent::UpdateWave(float NormalizedTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector Head = GetSafeBoneLocation(TEXT("head"), Owner->GetActorLocation() + FVector(0.0f, 0.0f, 90.0f));
	const FVector Right = Owner->GetActorRightVector();
	const FVector Forward = Owner->GetActorForwardVector();

	const float Alpha = ActionSustainAlpha(NormalizedTime);
	const float EmotionAmplitude = GetEmotionAmplitudeScale();
	const float EmotionSpeed = GetEmotionSpeedScale();
	const float BeatPhase = NormalizedTime * FMath::Max(1, ActiveAction.Beats) * 2.0f * PI * EmotionSpeed * ActiveAction.Speed;
	const float SideWave = FMath::Sin(BeatPhase) * 18.0f * ActiveAction.Amplitude * EmotionAmplitude;

	const FVector BaseTarget =
		Head
		+ Right * 35.0f
		+ Forward * 18.0f
		+ FVector(0.0f, 0.0f, 10.0f + ActiveAction.Height * 20.0f);

	if (ShouldUseRightHand())
	{
		RuntimeState.RightHandIKAlpha = Alpha;
		RuntimeState.RightHandIKTargetWS = BaseTarget + Right * SideWave;
		RuntimeState.FingerOpenAlphaRight = Alpha;
	}

	if (ShouldUseLeftHand())
	{
		RuntimeState.LeftHandIKAlpha = Alpha;
		RuntimeState.LeftHandIKTargetWS = BaseTarget - Right * 70.0f - Right * SideWave;
	}

	RuntimeState.ChestYawOffset = FMath::Sin(BeatPhase * 0.5f) * 3.0f * Alpha * GetEmotionChestYawScale();
}

void ULLMNPCActionComponent::UpdatePoint(float NormalizedTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !CurrentTargetActor)
	{
		return;
	}

	const FVector Target = CurrentTargetActor->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
	const float Alpha = ActionSustainAlpha(NormalizedTime);
	const float Reach = FMath::Lerp(45.0f, 85.0f, ActiveAction.Amplitude * GetEmotionAmplitudeScale());

	if (ShouldUseRightHand())
	{
		const FVector ShoulderFallback = Owner->GetActorLocation() + Owner->GetActorRightVector() * 22.0f + FVector(0.0f, 0.0f, 65.0f);
		const FVector Shoulder = GetSafeBoneLocation(TEXT("upperarm_r"), ShoulderFallback);
		const FVector Direction = (Target - Shoulder).GetSafeNormal(SMALL_NUMBER, Owner->GetActorForwardVector());

		RuntimeState.RightHandIKAlpha = Alpha;
		RuntimeState.RightHandIKTargetWS = Shoulder + Direction * Reach;
		RuntimeState.FingerPointAlphaRight = Alpha;
	}

	if (ShouldUseLeftHand())
	{
		const FVector ShoulderFallback = Owner->GetActorLocation() - Owner->GetActorRightVector() * 22.0f + FVector(0.0f, 0.0f, 65.0f);
		const FVector Shoulder = GetSafeBoneLocation(TEXT("upperarm_l"), ShoulderFallback);
		const FVector Direction = (Target - Shoulder).GetSafeNormal(SMALL_NUMBER, Owner->GetActorForwardVector());

		RuntimeState.LeftHandIKAlpha = Alpha;
		RuntimeState.LeftHandIKTargetWS = Shoulder + Direction * Reach;
	}

	RuntimeState.ChestYawOffset = 12.0f * Alpha * GetEmotionChestYawScale();
}

void ULLMNPCActionComponent::UpdateNod(float NormalizedTime)
{
	const int32 Beats = FMath::Max(1, ActiveAction.Beats);
	const float EmotionSpeed = GetEmotionSpeedScale();
	const float Phase = NormalizedTime * Beats * 2.0f * PI * EmotionSpeed * ActiveAction.Speed;
	const float Alpha = FMath::Sin(NormalizedTime * PI);
	const float Nod = FMath::Sin(Phase) * 10.0f * ActiveAction.Amplitude * GetEmotionAmplitudeScale();

	RuntimeState.HeadPitchOffset += Nod * Alpha;
}

bool ULLMNPCActionComponent::ShouldUseRightHand() const
{
	return ActiveAction.Hand == ELLMNPCHand::Auto ||
		ActiveAction.Hand == ELLMNPCHand::Right ||
		ActiveAction.Hand == ELLMNPCHand::Both;
}

bool ULLMNPCActionComponent::ShouldUseLeftHand() const
{
	return ActiveAction.Hand == ELLMNPCHand::Left ||
		ActiveAction.Hand == ELLMNPCHand::Both;
}

float ULLMNPCActionComponent::GetEmotionAmplitudeScale() const
{
	switch (ActiveAction.Emotion)
	{
	case ELLMNPCEmotion::Friendly:
		return 1.1f;
	case ELLMNPCEmotion::Urgent:
		return 1.2f;
	case ELLMNPCEmotion::Angry:
		return 1.15f;
	case ELLMNPCEmotion::Shy:
		return 0.55f;
	case ELLMNPCEmotion::Sad:
		return 0.7f;
	default:
		return 1.0f;
	}
}

float ULLMNPCActionComponent::GetEmotionSpeedScale() const
{
	switch (ActiveAction.Emotion)
	{
	case ELLMNPCEmotion::Urgent:
		return 1.35f;
	case ELLMNPCEmotion::Angry:
		return 1.2f;
	case ELLMNPCEmotion::Shy:
	case ELLMNPCEmotion::Sad:
		return 0.75f;
	default:
		return 1.0f;
	}
}

float ULLMNPCActionComponent::GetEmotionChestYawScale() const
{
	switch (ActiveAction.Emotion)
	{
	case ELLMNPCEmotion::Angry:
	case ELLMNPCEmotion::Urgent:
		return 1.25f;
	case ELLMNPCEmotion::Shy:
		return 0.4f;
	default:
		return 1.0f;
	}
}

USkeletalMeshComponent* ULLMNPCActionComponent::GetOwnerMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}

	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FVector ULLMNPCActionComponent::GetSafeBoneLocation(const FName BoneOrSocketName, const FVector& Fallback) const
{
	const USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh)
	{
		return Fallback;
	}

	if (Mesh->DoesSocketExist(BoneOrSocketName) || Mesh->GetBoneIndex(BoneOrSocketName) != INDEX_NONE)
	{
		return Mesh->GetSocketLocation(BoneOrSocketName);
	}

	return Fallback;
}
