#include "Animation/LLMNPCAnimationAssetPlayer.h"

#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "TimerManager.h"

namespace
{
bool ChannelsConflict(const TArray<FName>& A, const TArray<FName>& B)
{
	static const FName FullBodyChannel(TEXT("full_body"));
	static const FName RightArmIKChannel(TEXT("right_arm_ik"));
	static const FName RightArmFKChannel(TEXT("right_arm_fk"));
	static const FName LeftArmIKChannel(TEXT("left_arm_ik"));
	static const FName LeftArmFKChannel(TEXT("left_arm_fk"));

	if (A.Contains(FullBodyChannel) || B.Contains(FullBodyChannel))
	{
		return !A.IsEmpty() && !B.IsEmpty();
	}
	for (const FName Channel : A)
	{
		if (B.Contains(Channel))
		{
			return true;
		}
	}
	return
		(A.Contains(RightArmIKChannel) && B.Contains(RightArmFKChannel)) ||
		(A.Contains(RightArmFKChannel) && B.Contains(RightArmIKChannel)) ||
		(A.Contains(LeftArmIKChannel) && B.Contains(LeftArmFKChannel)) ||
		(A.Contains(LeftArmFKChannel) && B.Contains(LeftArmIKChannel));
}
}

void ULLMNPCAnimationAssetPlayer::Initialize(AActor* InOwnerActor)
{
	OwnerActor = InOwnerActor;
	MeshComponent = ResolveMesh();
}

void ULLMNPCAnimationAssetPlayer::Shutdown()
{
	if (UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(TimeoutHandle);
	}
	if (UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr)
	{
		if (ActiveMontage)
		{
			AnimInstance->Montage_Stop(0.0f, ActiveMontage);
		}
	}
	State = ELLMNPCAnimationPlaybackState::Idle;
	ClearActivePlayback(false);
	MeshComponent = nullptr;
	OwnerActor = nullptr;
}

bool ULLMNPCAnimationAssetPlayer::Play(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers,
	FString& OutError,
	float ElapsedPlaybackSeconds
)
{
	OutError.Reset();
	UAnimationAsset* AnimationAsset = nullptr;
	float PlayRate = 1.0f;
	if (!ValidatePlaybackRequest(MotionTemplate, Modifiers, AnimationAsset, PlayRate, OutError))
	{
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}
	ElapsedPlaybackSeconds = FMath::Max(ElapsedPlaybackSeconds, 0.0f);
	if (ElapsedPlaybackSeconds >= MotionTemplate.AnimationPlayback.MaxDurationSeconds)
	{
		OutError = TEXT("LLMNPC_ANIMATION_REPLICATION_STALE");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}
	const float BaseStartPosition = MotionTemplate.AnimationPlayback.StartPositionSeconds;
	float EffectiveStartPosition = BaseStartPosition + ElapsedPlaybackSeconds * PlayRate;
	const float AnimationLength = AnimationAsset->GetPlayLength();
	if (MotionTemplate.AnimationPlayback.bLoop)
	{
		const float RemainingLength = FMath::Max(AnimationLength - BaseStartPosition, KINDA_SMALL_NUMBER);
		EffectiveStartPosition = BaseStartPosition + FMath::Fmod(
			ElapsedPlaybackSeconds * PlayRate,
			RemainingLength
		);
	}
	else if (EffectiveStartPosition >= AnimationLength)
	{
		OutError = TEXT("LLMNPC_ANIMATION_REPLICATION_STALE");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}

	if (!MeshComponent)
	{
		MeshComponent = ResolveMesh();
	}
	if (!MeshComponent || !MeshComponent->GetSkeletalMeshAsset())
	{
		OutError = TEXT("LLMNPC_ANIMATION_MESH_MISSING");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}

	USkeleton* MeshSkeleton = MeshComponent->GetSkeletalMeshAsset()->GetSkeleton();
	USkeleton* AnimationSkeleton = AnimationAsset->GetSkeleton();
	if (!MeshSkeleton || !AnimationSkeleton || MeshSkeleton != AnimationSkeleton)
	{
		OutError = TEXT("LLMNPC_ANIMATION_SKELETON_INCOMPATIBLE");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}

	UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance();
	if (!AnimInstance)
	{
		OutError = TEXT("LLMNPC_ANIMATION_INSTANCE_MISSING");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}

	if (IsPlaying())
	{
		if (!bActiveInterruptible)
		{
			OutError = TEXT("LLMNPC_ANIMATION_ACTIVE_NOT_INTERRUPTIBLE");
			LastErrorCode = OutError;
			return false;
		}
		Stop(true);
	}

	UAnimMontage* PlayedMontage = nullptr;
	if (UAnimMontage* MontageAsset = Cast<UAnimMontage>(AnimationAsset))
	{
		const float PlayedLength = AnimInstance->Montage_PlayWithBlendIn(
			MontageAsset,
			FAlphaBlendArgs(MotionTemplate.AnimationPlayback.BlendInSeconds),
			PlayRate,
			EMontagePlayReturnType::MontageLength,
			EffectiveStartPosition,
			MotionTemplate.AnimationPlayback.bStopOtherMontages
		);
		if (PlayedLength > 0.0f)
		{
			PlayedMontage = MontageAsset;
		}
	}
	else if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(AnimationAsset))
	{
		if (!AnimationSkeleton->ContainsSlotName(MotionTemplate.AnimationPlayback.SlotName))
		{
			OutError = TEXT("LLMNPC_ANIMATION_SLOT_NOT_REGISTERED");
			State = ELLMNPCAnimationPlaybackState::Failed;
			LastErrorCode = OutError;
			return false;
		}
		const float RemainingLength = FMath::Max(
			Sequence->GetPlayLength() - EffectiveStartPosition,
			KINDA_SMALL_NUMBER
		);
		const int32 LoopCount = MotionTemplate.AnimationPlayback.bLoop
			? FMath::Clamp(
				FMath::CeilToInt(MotionTemplate.AnimationPlayback.MaxDurationSeconds * PlayRate / RemainingLength),
				1,
				1000
			)
			: 1;
		PlayedMontage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
			Sequence,
			MotionTemplate.AnimationPlayback.SlotName,
			MotionTemplate.AnimationPlayback.BlendInSeconds,
			MotionTemplate.AnimationPlayback.BlendOutSeconds,
			PlayRate,
			LoopCount,
			-1.0f,
			EffectiveStartPosition
		);
	}

	if (!PlayedMontage)
	{
		OutError = TEXT("LLMNPC_ANIMATION_PLAY_FAILED");
		State = ELLMNPCAnimationPlaybackState::Failed;
		LastErrorCode = OutError;
		return false;
	}

	ActiveMontage = PlayedMontage;
	ActiveTemplateId = MotionTemplate.Metadata.TemplateId;
	ActiveSlotName = MotionTemplate.AnimationPlayback.SlotName;
	ActivePlayRate = PlayRate;
	ActiveBlendOutSeconds = MotionTemplate.AnimationPlayback.BlendOutSeconds;
	bActiveInterruptible = MotionTemplate.AnimationPlayback.bInterruptible;
	ActiveChannels = MotionTemplate.Metadata.RequiredChannels;
	LastErrorCode.Reset();
	State = ELLMNPCAnimationPlaybackState::Playing;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &ULLMNPCAnimationAssetPlayer::HandleMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, PlayedMontage);

	if (UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr)
	{
		World->GetTimerManager().SetTimer(
			TimeoutHandle,
			this,
			&ULLMNPCAnimationAssetPlayer::HandleTimeout,
			FMath::Max(
				MotionTemplate.AnimationPlayback.MaxDurationSeconds - ElapsedPlaybackSeconds,
				0.01f
			),
			false
		);
	}
	return true;
}

void ULLMNPCAnimationAssetPlayer::Stop(bool bInterrupted)
{
	if (!ActiveMontage)
	{
		return;
	}
	if (UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(TimeoutHandle);
	}
	State = bInterrupted
		? ELLMNPCAnimationPlaybackState::Interrupted
		: ELLMNPCAnimationPlaybackState::Completed;
	if (UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(ActiveBlendOutSeconds, ActiveMontage);
	}
	ClearActivePlayback(true);
}

bool ULLMNPCAnimationAssetPlayer::ConflictsWith(const TArray<FName>& Channels) const
{
	return IsPlaying() && ChannelsConflict(ActiveChannels, Channels);
}

FLLMNPCAnimationPlaybackDebugState ULLMNPCAnimationAssetPlayer::GetDebugState() const
{
	FLLMNPCAnimationPlaybackDebugState DebugState;
	DebugState.State = State;
	DebugState.TemplateId = ActiveTemplateId;
	DebugState.SlotName = ActiveSlotName;
	DebugState.PlayRate = ActivePlayRate;
	DebugState.ErrorCode = LastErrorCode;
	return DebugState;
}

bool ULLMNPCAnimationAssetPlayer::ValidatePlaybackRequest(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers,
	UAnimationAsset*& OutAnimationAsset,
	float& OutPlayRate,
	FString& OutError
)
{
	OutAnimationAsset = nullptr;
	OutPlayRate = 1.0f;
	OutError.Reset();

	FString TemplateError;
	if (!MotionTemplate.ValidateTemplate(TemplateError))
	{
		OutError = TemplateError;
		return false;
	}
	if (!MotionTemplate.IsPublished())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_NOT_PUBLISHED");
		return false;
	}
	if (MotionTemplate.Kind != ELLMNPCTemplateKind::AnimationAsset)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_KIND_NOT_ANIMATION_ASSET");
		return false;
	}
	if (
		!FMath::IsFinite(Modifiers.Amplitude) ||
		!FMath::IsFinite(Modifiers.SpeedScale) ||
		!FMath::IsFinite(Modifiers.DurationScale)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_MODIFIER_NON_FINITE");
		return false;
	}
	if (Modifiers.bMirror)
	{
		OutError = TEXT("LLMNPC_ANIMATION_MIRROR_REQUIRES_APPROVED_VARIANT");
		return false;
	}
	if (MotionTemplate.Metadata.bRequiresTarget && Modifiers.TargetRef.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_TARGET_REQUIRED");
		return false;
	}
	if (
		!Modifiers.Style.IsNone() &&
		!MotionTemplate.ModifierPolicy.AllowedStyleTags.IsEmpty() &&
		!MotionTemplate.ModifierPolicy.AllowedStyleTags.Contains(Modifiers.Style)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_STYLE_FORBIDDEN");
		return false;
	}

	OutAnimationAsset = MotionTemplate.AnimationAsset.LoadSynchronous();
	UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(OutAnimationAsset);
	if (!SequenceBase)
	{
		OutAnimationAsset = nullptr;
		OutError = TEXT("LLMNPC_ANIMATION_ASSET_TYPE_UNSUPPORTED");
		return false;
	}
	if (!MotionTemplate.AnimationPlayback.bAllowRootMotion && SequenceBase->HasRootMotion())
	{
		OutAnimationAsset = nullptr;
		OutError = TEXT("LLMNPC_ANIMATION_ROOT_MOTION_FORBIDDEN");
		return false;
	}
	if (
		SequenceBase->GetPlayLength() <= 0.0f ||
		MotionTemplate.AnimationPlayback.StartPositionSeconds >= SequenceBase->GetPlayLength()
	)
	{
		OutAnimationAsset = nullptr;
		OutError = TEXT("LLMNPC_ANIMATION_START_POSITION_INVALID");
		return false;
	}

	const float SpeedScale = FMath::Clamp(
		Modifiers.SpeedScale,
		MotionTemplate.ModifierPolicy.SpeedRange.X,
		MotionTemplate.ModifierPolicy.SpeedRange.Y
	);
	const float DurationScale = FMath::Clamp(
		Modifiers.DurationScale,
		MotionTemplate.ModifierPolicy.DurationRange.X,
		MotionTemplate.ModifierPolicy.DurationRange.Y
	);
	OutPlayRate = SpeedScale / FMath::Max(DurationScale, KINDA_SMALL_NUMBER);
	return true;
}

void ULLMNPCAnimationAssetPlayer::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

USkeletalMeshComponent* ULLMNPCAnimationAssetPlayer::ResolveMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(OwnerActor))
	{
		return Character->GetMesh();
	}
	return OwnerActor ? OwnerActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void ULLMNPCAnimationAssetPlayer::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveMontage)
	{
		return;
	}
	if (UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(TimeoutHandle);
	}
	State = bInterrupted
		? ELLMNPCAnimationPlaybackState::Interrupted
		: ELLMNPCAnimationPlaybackState::Completed;
	ClearActivePlayback(true);
}

void ULLMNPCAnimationAssetPlayer::HandleTimeout()
{
	if (!ActiveMontage)
	{
		return;
	}
	State = ELLMNPCAnimationPlaybackState::TimedOut;
	LastErrorCode = TEXT("LLMNPC_ANIMATION_TIMEOUT");
	if (UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(ActiveBlendOutSeconds, ActiveMontage);
	}
	ClearActivePlayback(true);
}

void ULLMNPCAnimationAssetPlayer::ClearActivePlayback(bool bPreserveTerminalState)
{
	ActiveMontage = nullptr;
	bActiveInterruptible = false;
	ActiveChannels.Reset();
	if (!bPreserveTerminalState)
	{
		ActiveTemplateId = NAME_None;
		ActiveSlotName = NAME_None;
		ActivePlayRate = 1.0f;
		LastErrorCode.Reset();
	}
}
