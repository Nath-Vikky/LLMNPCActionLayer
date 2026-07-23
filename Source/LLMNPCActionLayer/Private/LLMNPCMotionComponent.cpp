#include "LLMNPCMotionComponent.h"

#include "Animation/LLMNPCAnimationAssetPlayer.h"
#include "LLMNPCAPIClient.h"
#include "LLMNPCActionLayer.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionValidator.h"
#include "LLMNPCPostProcessAnimInstance.h"
#include "LLMNPCSettings.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Net/UnrealNetwork.h"

DECLARE_CYCLE_STAT(
	TEXT("Motion Component Tick"),
	STAT_LLMNPCMotionComponentTick,
	STATGROUP_LLMNPCActionLayer
);
DECLARE_CYCLE_STAT(
	TEXT("Motion Sampling"),
	STAT_LLMNPCMotionSampling,
	STATGROUP_LLMNPCActionLayer
);

ULLMNPCMotionComponent::ULLMNPCMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	SkeletonProfile = TSoftObjectPtr<ULLMNPCSkeletonProfile>(FSoftObjectPath(
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	));
}

void ULLMNPCMotionComponent::BeginPlay()
{
	Super::BeginPlay();

	Validator = NewObject<ULLMNPCMotionValidator>(this);
	Validator->Manifest = ControlManifest;
	APIClient = NewObject<ULLMNPCAPIClient>(this);
	const bool bHasVisualRuntime =
		!GetWorld() ||
		GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (bHasVisualRuntime)
	{
		AnimationAssetPlayer = NewObject<ULLMNPCAnimationAssetPlayer>(this);
		AnimationAssetPlayer->Initialize(GetOwner());
	}
	const int32 ResolvedMicroMotionSeed = MicroMotionSeed != 0
		? MicroMotionSeed
		: static_cast<int32>(GetTypeHash(GetOwner() ? GetOwner()->GetFName() : GetFName()) & 0x7fffffffU);
	MicroMotionState.Initialize(ResolvedMicroMotionSeed);
	RefreshPoseBoneBindings();

	if (
		bHasVisualRuntime &&
		bAutoInstallPostProcessAnimBP &&
		PostProcessInstallMode != ELLMNPCPostProcessInstallMode::Disabled
	)
	{
		InstallPostProcessAnimBP();
	}
}

void ULLMNPCMotionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AnimationAssetPlayer)
	{
		AnimationAssetPlayer->Shutdown();
	}
	RestorePostProcessAnimBP();
	Super::EndPlay(EndPlayReason);
}

void ULLMNPCMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_LLMNPCMotionComponentTick);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	float UpdateDeltaSeconds = 0.0f;
	if (!ShouldRunMotionUpdate(DeltaTime, UpdateDeltaSeconds))
	{
		return;
	}

	StartEligiblePlans();
	UpdateActivePlans(UpdateDeltaSeconds);
	if (CurrentMotionLOD != ELLMNPCMotionLODLevel::Minimal)
	{
		UpdateMicroMotion(UpdateDeltaSeconds);
	}
}

void ULLMNPCMotionComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps
) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ULLMNPCMotionComponent, ReplicatedMotionCommand);
}

void ULLMNPCMotionComponent::SetAmbientStyle(FName StyleTag)
{
	AmbientStyle = StyleTag.IsNone() ? FName(TEXT("neutral")) : StyleTag;
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
	return SubmitMotionPlanWithSource(
		MoveTemp(Plan),
		ELLMNPCMotionValidationSource::RuntimeModel
	);
}

bool ULLMNPCMotionComponent::SubmitCompiledTemplatePlan(FLLMMotionPlan Plan)
{
	return SubmitMotionPlanWithSource(
		MoveTemp(Plan),
		ELLMNPCMotionValidationSource::PublishedTemplate
	);
}

bool ULLMNPCMotionComponent::SubmitPublishedTemplate(
	FName TemplateOrPublicActionId,
	FLLMNPCTemplateModifiers Modifiers
)
{
	if (!CanSubmitLocally())
	{
		LastValidationError = TEXT("LLMNPC_MOTION_AUTHORITY_REQUIRED");
		return false;
	}

	ULLMNPCSkeletonProfile* ResolvedProfile = SkeletonProfile.LoadSynchronous();
	if (!ResolvedProfile)
	{
		LastValidationError = TEXT("LLMNPC_SKELETON_PROFILE_NOT_FOUND");
		return false;
	}
	FString ProfileValidationError;
	if (!ResolvedProfile->ValidateProfile(ProfileValidationError))
	{
		LastValidationError = ProfileValidationError;
		return false;
	}
	CachedPoseBoneBindings = ResolvedProfile->BuildPoseBoneBindings();

	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	USkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (
		!MeshAsset ||
		!ResolvedProfile->IsCompatibleSkeleton(MeshAsset->GetSkeleton())
	)
	{
		LastValidationError = TEXT("LLMNPC_SKELETON_PROFILE_INCOMPATIBLE");
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	ULLMNPCTemplateLibrarySubsystem* Library = GameInstance
		? GameInstance->GetSubsystem<ULLMNPCTemplateLibrarySubsystem>()
		: nullptr;
	if (!Library)
	{
		LastValidationError = TEXT("LLMNPC_TEMPLATE_LIBRARY_NOT_AVAILABLE");
		return false;
	}

	const ULLMNPCMotionTemplate* MotionTemplate = Library->FindPublishedTemplate(
		TemplateOrPublicActionId
	);
	if (!MotionTemplate)
	{
		MotionTemplate = Library->ResolvePublishedVariant(
			TemplateOrPublicActionId,
			ResolvedProfile->ProfileId,
			Modifiers.Style,
			Modifiers.RandomSeed
		);
	}

	if (!MotionTemplate)
	{
		LastValidationError = TEXT("LLMNPC_TEMPLATE_NOT_FOUND_OR_NOT_PUBLISHED");
		return false;
	}

	if (MotionTemplate->Kind == ELLMNPCTemplateKind::AnimationAsset)
	{
		return SubmitAnimationAssetTemplate(*MotionTemplate, Modifiers);
	}
	if (MotionTemplate->Kind != ELLMNPCTemplateKind::ProceduralMotion)
	{
		LastValidationError = TEXT("LLMNPC_TEMPLATE_KIND_NOT_EXECUTABLE");
		return false;
	}

	FLLMMotionPlan CompiledPlan;
	FString CompileError;
	if (!FLLMNPCTemplateCompiler::Compile(
		*MotionTemplate,
		Modifiers,
		*ResolvedProfile,
		CompiledPlan,
		CompileError
	))
	{
		LastValidationError = CompileError;
		return false;
	}

	return SubmitMotionPlanWithSource(
		MoveTemp(CompiledPlan),
		ELLMNPCMotionValidationSource::PublishedTemplate,
		MotionTemplate,
		PendingReplicatedStartOffsetSeconds
	);
}

bool ULLMNPCMotionComponent::SubmitMotionPlanWithSource(
	FLLMMotionPlan Plan,
	ELLMNPCMotionValidationSource Source,
	const ULLMNPCMotionTemplate* SourceTemplate,
	float InitialTimeSeconds
)
{
	if (!CanSubmitLocally())
	{
		LastValidationError = TEXT("LLMNPC_MOTION_AUTHORITY_REQUIRED");
		return false;
	}

	if (!Validator)
	{
		Validator = NewObject<ULLMNPCMotionValidator>(this);
	}

	Validator->Manifest = ControlManifest;

	FLLMMotionValidationResult Result = Validator->ValidateAndClamp(Plan, Source);
	if (!Result.bValid)
	{
		LastValidationError = Result.ErrorMessage;
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion: Plan rejected: %s"), *LastValidationError);
		return false;
	}

	FString TargetError;
	if (!ValidateTargetRefs(Plan, TargetError))
	{
		LastValidationError = TargetError;
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion: Plan rejected: %s"), *LastValidationError);
		return false;
	}

	if (Queue.Num() >= MaxQueueSize)
	{
		LastValidationError = TEXT("Motion queue is full.");
		return false;
	}

	FLLMNPCQueuedMotionPlan Request;
	Request.Plan = MoveTemp(Plan);
	Request.InitialTimeSeconds = FMath::Max(InitialTimeSeconds, 0.0f);
	if (Request.InitialTimeSeconds >= Request.Plan.Clip.Duration)
	{
		LastValidationError = TEXT("LLMNPC_REPLICATED_MOTION_STALE");
		return false;
	}
	Request.Channels = DeriveMotionChannels(Request.Plan);
	Request.QueuedAtSeconds = FPlatformTime::Seconds();
	Request.bReplicateOnStart =
		bReplicateMotionCommands &&
		!bApplyingReplicatedCommand &&
		GetOwner() &&
		GetOwner()->GetIsReplicated() &&
		GetOwner()->HasAuthority();
	if (SourceTemplate)
	{
		Request.SourceTemplateId = SourceTemplate->Metadata.TemplateId;
		Request.CooldownSeconds = SourceTemplate->Metadata.CooldownSeconds;
		for (const FName Channel : SourceTemplate->Metadata.RequiredChannels)
		{
			Request.Channels.AddUnique(Channel);
		}

		if (const double* LastStart = LastTemplateStartTimes.Find(Request.SourceTemplateId))
		{
			if (Request.QueuedAtSeconds - *LastStart < Request.CooldownSeconds)
			{
				LastValidationError = TEXT("LLMNPC_TEMPLATE_COOLDOWN_ACTIVE");
				return false;
			}
		}
	}

	FJsonObjectConverter::UStructToJsonObjectString(Request.Plan, LastAcceptedMotionJson);
	Queue.Add(MoveTemp(Request));
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
	SubmitPublishedTemplate(TEXT("gesture.nod"), FLLMNPCTemplateModifiers());
}

void ULLMNPCMotionComponent::TestWave(AActor* TargetActor)
{
	static_cast<void>(TargetActor);
	SubmitPublishedTemplate(
		TEXT("gesture.wave.right.manny.fk.v1"),
		FLLMNPCTemplateModifiers()
	);
}

bool ULLMNPCMotionComponent::SubmitSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, AActor* TargetActor)
{
	static_cast<void>(TargetActor);
	switch (Sample)
	{
	case ELLMNPCMotionDebugSample::Wave:
		return SubmitPublishedTemplate(
			TEXT("gesture.wave.right.manny.fk.v1"),
			FLLMNPCTemplateModifiers()
		);
	case ELLMNPCMotionDebugSample::InvalidUnknownControl:
		{
			FLLMMotionPlan Plan = BuildInvalidUnknownControlPlan();
			FJsonObjectConverter::UStructToJsonObjectString(Plan, LastRawMotionJson);
			return SubmitMotionPlanWithSource(
				MoveTemp(Plan),
				ELLMNPCMotionValidationSource::InternalDebug
			);
		}
	case ELLMNPCMotionDebugSample::Nod:
	default:
		return SubmitPublishedTemplate(
			TEXT("gesture.nod"),
			FLLMNPCTemplateModifiers()
		);
	}
}

FString ULLMNPCMotionComponent::BuildSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, const FString& TargetRef) const
{
	static_cast<void>(TargetRef);
	if (Sample == ELLMNPCMotionDebugSample::InvalidUnknownControl)
	{
		FString JsonString;
		FJsonObjectConverter::UStructToJsonObjectString(
			BuildInvalidUnknownControlPlan(),
			JsonString
		);
		return JsonString;
	}

	const TCHAR* TemplatePath = Sample == ELLMNPCMotionDebugSample::Wave
		? TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_FK_v1.MT_Wave_Right_Manny_FK_v1")
		: TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1");
	const ULLMNPCMotionTemplate* MotionTemplate = LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		TemplatePath
	);
	const ULLMNPCSkeletonProfile* Profile = SkeletonProfile.LoadSynchronous();
	if (!MotionTemplate || !Profile)
	{
		return FString();
	}

	FLLMMotionPlan Plan;
	FString CompileError;
	if (!FLLMNPCTemplateCompiler::Compile(
		*MotionTemplate,
		FLLMNPCTemplateModifiers(),
		*Profile,
		Plan,
		CompileError
	))
	{
		return FString();
	}

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
	State.ActiveDuration = ActiveMotions.IsEmpty()
		? 0.0f
		: ActiveMotions[0].Request.Plan.Clip.Duration;
	State.QueueCount = Queue.Num();
	const bool bAnimationPlaying = AnimationAssetPlayer && AnimationAssetPlayer->IsPlaying();
	State.bHasActivePlan = bHasActivePlan || bAnimationPlaying;
	State.ActivePlanCount = ActiveMotions.Num() + (bAnimationPlaying ? 1 : 0);
	State.bMotionRequestInFlight = bMotionRequestInFlight;
	State.bPostProcessInstalled = bPostProcessInstalled;
	State.LastPostProcessError = LastPostProcessError;
	if (AnimationAssetPlayer)
	{
		const FLLMNPCAnimationPlaybackDebugState AnimationState = AnimationAssetPlayer->GetDebugState();
		State.bAnimationAssetPlaying = bAnimationPlaying;
		State.AnimationPlaybackState = StaticEnum<ELLMNPCAnimationPlaybackState>()->GetNameStringByValue(
			static_cast<int64>(AnimationState.State)
		);
		State.ActiveAnimationTemplateId = AnimationState.TemplateId;
		State.ActiveAnimationSlot = AnimationState.SlotName;
		State.ActiveAnimationPlayRate = AnimationState.PlayRate;
		State.LastAnimationError = AnimationState.ErrorCode;
	}
	State.MotionLODLevel = StaticEnum<ELLMNPCMotionLODLevel>()->GetNameStringByValue(
		static_cast<int64>(CurrentMotionLOD)
	);
	State.MotionLODUpdateIntervalSeconds =
		CurrentMotionLOD == ELLMNPCMotionLODLevel::Reduced
			? ReducedUpdateIntervalSeconds
			: (
				CurrentMotionLOD == ELLMNPCMotionLODLevel::Minimal
					? MinimalUpdateIntervalSeconds
					: 0.0f
			);
	State.ReplicatedMotionSequence = ReplicatedMotionCommand.Sequence;
	State.Snapshot = CurrentSnapshot;
	return State;
}

void ULLMNPCMotionComponent::StartEligiblePlans()
{
	const double NowSeconds = FPlatformTime::Seconds();
	for (int32 QueueIndex = Queue.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		if (NowSeconds - Queue[QueueIndex].QueuedAtSeconds > MaxQueueWaitSeconds)
		{
			UE_LOG(
				LogLLMNPCActionLayer,
				Warning,
				TEXT("LLMNPCMotion: Dropped queued clip %s after waiting %.2f seconds."),
				*Queue[QueueIndex].Plan.Clip.ClipId,
				NowSeconds - Queue[QueueIndex].QueuedAtSeconds
			);
			Queue.RemoveAt(QueueIndex);
		}
	}

	for (int32 QueueIndex = 0; QueueIndex < Queue.Num();)
	{
		const FLLMNPCQueuedMotionPlan& Candidate = Queue[QueueIndex];
		if (AnimationAssetPlayer && AnimationAssetPlayer->ConflictsWith(Candidate.Channels))
		{
			++QueueIndex;
			continue;
		}
		if (!Candidate.SourceTemplateId.IsNone() && Candidate.CooldownSeconds > 0.0f)
		{
			if (const double* LastStart = LastTemplateStartTimes.Find(Candidate.SourceTemplateId))
			{
				if (NowSeconds - *LastStart < Candidate.CooldownSeconds)
				{
					++QueueIndex;
					continue;
				}
			}
		}

		TArray<int32> ConflictingActiveIndices;
		bool bCanInterruptAll = true;

		for (int32 ActiveIndex = 0; ActiveIndex < ActiveMotions.Num(); ++ActiveIndex)
		{
			const FLLMNPCActiveMotionPlan& Active = ActiveMotions[ActiveIndex];
			if (!ChannelsConflict(Candidate.Channels, Active.Request.Channels))
			{
				continue;
			}

			ConflictingActiveIndices.Add(ActiveIndex);
			const FLLMMotionClip& ActiveClip = Active.Request.Plan.Clip;
			if (
				!ActiveClip.bInterruptible ||
				Candidate.Plan.Clip.Priority <= ActiveClip.Priority
			)
			{
				bCanInterruptAll = false;
			}
		}

		if (!ConflictingActiveIndices.IsEmpty() && !bCanInterruptAll)
		{
			++QueueIndex;
			continue;
		}

		for (int32 ConflictIndex = ConflictingActiveIndices.Num() - 1; ConflictIndex >= 0; --ConflictIndex)
		{
			ActiveMotions.RemoveAt(ConflictingActiveIndices[ConflictIndex]);
		}

		FLLMNPCActiveMotionPlan NewActive;
		NewActive.Request = MoveTemp(Queue[QueueIndex]);
		NewActive.Time = NewActive.Request.InitialTimeSeconds;
		if (!NewActive.Request.SourceTemplateId.IsNone())
		{
			LastTemplateStartTimes.Add(NewActive.Request.SourceTemplateId, NowSeconds);
		}

		ActiveMotions.Add(MoveTemp(NewActive));
		if (ActiveMotions.Last().Request.bReplicateOnStart)
		{
			PublishReplicatedPlan(ActiveMotions.Last().Request.Plan);
		}
		Queue.RemoveAt(QueueIndex);
	}

	bHasActivePlan = !ActiveMotions.IsEmpty();
}

bool ULLMNPCMotionComponent::ValidateTargetRefs(const FLLMMotionPlan& Plan, FString& OutError) const
{
	OutError.Reset();
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		const FLLMControlDefinition* Definition = ControlManifest
			? ControlManifest->FindControl(Track.ControlId)
			: ULLMNPCControlManifest::FindBuiltInControl(Track.ControlId);
		const bool bRequiresTarget =
			(Definition && Definition->bRequiresTarget) ||
			Track.TrackType == ELLMMotionTrackType::IKReach ||
			Track.TrackType == ELLMMotionTrackType::LookAt;

		if (!bRequiresTarget && Track.TargetRef.IsEmpty())
		{
			continue;
		}

		const TObjectPtr<AActor>* Target = TargetMap.Find(Track.TargetRef);
		if (!Target)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_TARGET_NOT_REGISTERED:%s"),
				*Track.TargetRef
			);
			return false;
		}

		if (!IsValid(Target->Get()))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_TARGET_INVALID:%s"),
				*Track.TargetRef
			);
			return false;
		}
	}

	return true;
}

void ULLMNPCMotionComponent::UpdateActivePlans(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_LLMNPCMotionSampling);
	CurrentSnapshot = FLLMProceduralPoseSnapshot();
	CurrentSnapshot.BoneBindings = CachedPoseBoneBindings;
	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	const bool bEvaluatePose =
		!GetWorld() ||
		GetWorld()->GetNetMode() != NM_DedicatedServer;

	for (int32 ActiveIndex = ActiveMotions.Num() - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		FLLMNPCActiveMotionPlan& Active = ActiveMotions[ActiveIndex];
		Active.Time += DeltaTime;
		if (Active.Time >= Active.Request.Plan.Clip.Duration)
		{
			ActiveMotions.RemoveAt(ActiveIndex);
			continue;
		}

		if (bEvaluatePose)
		{
			FLLMProceduralPoseSnapshot SampledSnapshot;
			FLLMNPCMotionSampler::SampleClip(
				Active.Request.Plan.Clip,
				ControlManifest,
				Mesh,
				TargetMap,
				Active.Time,
				SampledSnapshot
			);
			MergeSnapshot(CurrentSnapshot, SampledSnapshot);
		}
	}

	bHasActivePlan = !ActiveMotions.IsEmpty();
	if (bHasActivePlan)
	{
		ActiveClipId = ActiveMotions[0].Request.Plan.Clip.ClipId;
		ActiveTime = ActiveMotions[0].Time;
	}
	else
	{
		ActiveTime = 0.0f;
		ActiveClipId.Reset();
	}
}

void ULLMNPCMotionComponent::RefreshPoseBoneBindings()
{
	CachedPoseBoneBindings = FLLMNPCPoseBoneBindings();
	if (const ULLMNPCSkeletonProfile* ResolvedProfile = SkeletonProfile.LoadSynchronous())
	{
		CachedPoseBoneBindings = ResolvedProfile->BuildPoseBoneBindings();
	}
}

ELLMNPCMotionLODLevel ULLMNPCMotionComponent::ResolveLODLevelForDistance(
	float DistanceCm,
	float InFullQualityDistanceCm,
	float InReducedQualityDistanceCm
)
{
	const float FullDistance = FMath::Max(InFullQualityDistanceCm, 0.0f);
	const float ReducedDistance = FMath::Max(InReducedQualityDistanceCm, FullDistance);
	if (DistanceCm <= FullDistance)
	{
		return ELLMNPCMotionLODLevel::Full;
	}
	if (DistanceCm <= ReducedDistance)
	{
		return ELLMNPCMotionLODLevel::Reduced;
	}
	return ELLMNPCMotionLODLevel::Minimal;
}

bool ULLMNPCMotionComponent::ShouldRunMotionUpdate(
	float DeltaTime,
	float& OutUpdateDeltaSeconds
)
{
	UpdateMotionLOD();
	LODAccumulatedDeltaSeconds += FMath::Max(DeltaTime, 0.0f);

	float RequiredInterval = 0.0f;
	switch (CurrentMotionLOD)
	{
	case ELLMNPCMotionLODLevel::Reduced:
		RequiredInterval = FMath::Max(ReducedUpdateIntervalSeconds, 0.016f);
		break;
	case ELLMNPCMotionLODLevel::Minimal:
		RequiredInterval = FMath::Max(MinimalUpdateIntervalSeconds, 0.05f);
		break;
	case ELLMNPCMotionLODLevel::Full:
	default:
		break;
	}

	if (RequiredInterval > 0.0f && LODAccumulatedDeltaSeconds < RequiredInterval)
	{
		return false;
	}

	OutUpdateDeltaSeconds = LODAccumulatedDeltaSeconds;
	LODAccumulatedDeltaSeconds = 0.0f;
	return true;
}

void ULLMNPCMotionComponent::UpdateMotionLOD()
{
	if (!bEnableAutomaticMotionLOD || !GetWorld())
	{
		CurrentMotionLOD = ELLMNPCMotionLODLevel::Full;
		return;
	}
	if (GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		CurrentMotionLOD = ELLMNPCMotionLODLevel::Minimal;
		return;
	}

	CurrentMotionLOD = ResolveLODLevelForDistance(
		ResolveNearestViewerDistanceCm(),
		FullQualityDistanceCm,
		ReducedQualityDistanceCm
	);
}

float ULLMNPCMotionComponent::ResolveNearestViewerDistanceCm() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return 0.0f;
	}

	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		if (!PlayerController || !PlayerController->IsLocalController())
		{
			continue;
		}
		const AActor* ViewTarget = PlayerController->GetViewTarget();
		if (!ViewTarget)
		{
			continue;
		}
		NearestDistanceSquared = FMath::Min(
			NearestDistanceSquared,
			FVector::DistSquared(Owner->GetActorLocation(), ViewTarget->GetActorLocation())
		);
	}

	return NearestDistanceSquared == TNumericLimits<float>::Max()
		? 0.0f
		: FMath::Sqrt(NearestDistanceSquared);
}

bool ULLMNPCMotionComponent::CanSubmitLocally() const
{
	if (bApplyingReplicatedCommand || !bReplicateMotionCommands || !bRequireAuthorityForReplicatedSubmission)
	{
		return true;
	}

	const AActor* Owner = GetOwner();
	return !Owner || !Owner->GetIsReplicated() || Owner->HasAuthority();
}

void ULLMNPCMotionComponent::PublishReplicatedPlan(const FLLMMotionPlan& Plan)
{
	if (!GetOwner() || !GetOwner()->GetIsReplicated() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 NextSequence = ReplicatedMotionCommand.Sequence == MAX_int32
		? 1
		: ReplicatedMotionCommand.Sequence + 1;
	ReplicatedMotionCommand = FLLMNPCReplicatedMotionCommand();
	ReplicatedMotionCommand.Sequence = NextSequence;
	ReplicatedMotionCommand.Kind = ELLMNPCReplicatedMotionCommandKind::ProceduralPlan;
	ReplicatedMotionCommand.ProceduralPlan = Plan;
	ReplicatedMotionCommand.ServerStartTimeSeconds = GetSynchronizedServerTimeSeconds();
	PopulateReplicatedTargets(Plan, ReplicatedMotionCommand.Targets);
	GetOwner()->ForceNetUpdate();
}

void ULLMNPCMotionComponent::PublishReplicatedAnimationTemplate(
	FName TemplateId,
	const FLLMNPCTemplateModifiers& Modifiers
)
{
	if (!GetOwner() || !GetOwner()->GetIsReplicated() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 NextSequence = ReplicatedMotionCommand.Sequence == MAX_int32
		? 1
		: ReplicatedMotionCommand.Sequence + 1;
	ReplicatedMotionCommand = FLLMNPCReplicatedMotionCommand();
	ReplicatedMotionCommand.Sequence = NextSequence;
	ReplicatedMotionCommand.Kind = ELLMNPCReplicatedMotionCommandKind::AnimationTemplate;
	ReplicatedMotionCommand.TemplateId = TemplateId;
	ReplicatedMotionCommand.TemplateModifiers = Modifiers;
	ReplicatedMotionCommand.ServerStartTimeSeconds = GetSynchronizedServerTimeSeconds();
	if (!Modifiers.TargetRef.IsEmpty())
	{
		if (const TObjectPtr<AActor>* TargetActor = TargetMap.Find(Modifiers.TargetRef))
		{
			FLLMNPCReplicatedTarget& Target = ReplicatedMotionCommand.Targets.AddDefaulted_GetRef();
			Target.TargetRef = Modifiers.TargetRef;
			Target.TargetActor = TargetActor->Get();
		}
	}
	GetOwner()->ForceNetUpdate();
}

void ULLMNPCMotionComponent::PopulateReplicatedTargets(
	const FLLMMotionPlan& Plan,
	TArray<FLLMNPCReplicatedTarget>& OutTargets
) const
{
	OutTargets.Reset();
	TSet<FString> AddedTargetRefs;
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		if (Track.TargetRef.IsEmpty() || AddedTargetRefs.Contains(Track.TargetRef))
		{
			continue;
		}
		const TObjectPtr<AActor>* TargetActor = TargetMap.Find(Track.TargetRef);
		if (!TargetActor || !IsValid(TargetActor->Get()))
		{
			continue;
		}

		FLLMNPCReplicatedTarget& Target = OutTargets.AddDefaulted_GetRef();
		Target.TargetRef = Track.TargetRef;
		Target.TargetActor = TargetActor->Get();
		AddedTargetRefs.Add(Track.TargetRef);
	}
}

double ULLMNPCMotionComponent::GetSynchronizedServerTimeSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0;
}

void ULLMNPCMotionComponent::OnRep_ReplicatedMotionCommand()
{
	if (
		ReplicatedMotionCommand.ProtocolVersion != 1 ||
		ReplicatedMotionCommand.Sequence <= 0 ||
		ReplicatedMotionCommand.Sequence == LastAppliedReplicationSequence
	)
	{
		if (ReplicatedMotionCommand.ProtocolVersion != 1)
		{
			LastValidationError = TEXT("LLMNPC_REPLICATION_PROTOCOL_UNSUPPORTED");
		}
		return;
	}
	LastAppliedReplicationSequence = ReplicatedMotionCommand.Sequence;

	for (const FLLMNPCReplicatedTarget& Target : ReplicatedMotionCommand.Targets)
	{
		RegisterTarget(Target.TargetRef, Target.TargetActor);
	}

	PendingReplicatedStartOffsetSeconds = FMath::Max(
		static_cast<float>(
			GetSynchronizedServerTimeSeconds() - ReplicatedMotionCommand.ServerStartTimeSeconds
		),
		0.0f
	);
	bApplyingReplicatedCommand = true;
	switch (ReplicatedMotionCommand.Kind)
	{
	case ELLMNPCReplicatedMotionCommandKind::ProceduralPlan:
		SubmitMotionPlanWithSource(
			ReplicatedMotionCommand.ProceduralPlan,
			ELLMNPCMotionValidationSource::ReplicatedAuthority,
			nullptr,
			PendingReplicatedStartOffsetSeconds
		);
		break;
	case ELLMNPCReplicatedMotionCommandKind::AnimationTemplate:
		SubmitPublishedTemplate(
			ReplicatedMotionCommand.TemplateId,
			ReplicatedMotionCommand.TemplateModifiers
		);
		break;
	case ELLMNPCReplicatedMotionCommandKind::None:
	default:
		break;
	}
	bApplyingReplicatedCommand = false;
	PendingReplicatedStartOffsetSeconds = 0.0f;
}

void ULLMNPCMotionComponent::UpdateMicroMotion(float DeltaTime)
{
	FLLMNPCMicroMotionConfig Config;
	const FLLMNPCStylePreset AmbientPreset = ULLMNPCStyleResolver::GetBuiltInPreset(AmbientStyle);
	Config.bEnabled = bEnableMicroMotion;
	Config.bEnableGaze = bEnableGazeScheduler;
	Config.Amplitude = MicroMotionAmplitude * AmbientPreset.MicroMotionScale;
	Config.BreathingFrequencyHz = BreathingFrequencyHz;
	Config.GazeAlpha = AmbientGazeAlpha * FMath::Lerp(0.5f, 1.25f, AmbientPreset.GazeEngagement);
	Config.GazeSwitchInterval = GazeSwitchInterval;

	TMap<FString, FVector> GazeTargetsCS;
	if (USkeletalMeshComponent* Mesh = GetOwnerMesh())
	{
		for (const TPair<FString, TObjectPtr<AActor>>& Pair : TargetMap)
		{
			if (IsValid(Pair.Value))
			{
				GazeTargetsCS.Add(
					Pair.Key,
					Mesh->GetComponentTransform().InverseTransformPosition(
						Pair.Value->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f)
					)
				);
			}
		}
	}

	FLLMNPCMicroMotionScheduler::Update(
		Config,
		MicroMotionState,
		DeltaTime,
		GazeTargetsCS,
		IsChannelActive(TEXT("head")),
		IsChannelActive(TEXT("chest")),
		IsChannelActive(TEXT("gaze")),
		CurrentSnapshot
	);
}

bool ULLMNPCMotionComponent::IsChannelActive(FName Channel) const
{
	if (
		AnimationAssetPlayer &&
		AnimationAssetPlayer->IsPlaying() &&
		(
			AnimationAssetPlayer->GetActiveChannels().Contains(Channel) ||
			AnimationAssetPlayer->GetActiveChannels().Contains(TEXT("full_body"))
		)
	)
	{
		return true;
	}
	for (const FLLMNPCActiveMotionPlan& Active : ActiveMotions)
	{
		if (
			Active.Request.Channels.Contains(Channel) ||
			Active.Request.Channels.Contains(TEXT("full_body"))
		)
		{
			return true;
		}
	}
	return false;
}

bool ULLMNPCMotionComponent::SubmitAnimationAssetTemplate(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers
)
{
	for (const FLLMNPCActiveMotionPlan& Active : ActiveMotions)
	{
		if (ChannelsConflict(MotionTemplate.Metadata.RequiredChannels, Active.Request.Channels))
		{
			LastValidationError = TEXT("LLMNPC_ANIMATION_CHANNEL_CONFLICT");
			return false;
		}
	}

	const double NowSeconds = FPlatformTime::Seconds();
	if (const double* LastStart = LastTemplateStartTimes.Find(MotionTemplate.Metadata.TemplateId))
	{
		if (NowSeconds - *LastStart < MotionTemplate.Metadata.CooldownSeconds)
		{
			LastValidationError = TEXT("LLMNPC_TEMPLATE_COOLDOWN_ACTIVE");
			return false;
		}
	}

	if (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		UAnimationAsset* AnimationAsset = nullptr;
		float PlayRate = 1.0f;
		FString ValidationError;
		if (!ULLMNPCAnimationAssetPlayer::ValidatePlaybackRequest(
			MotionTemplate,
			Modifiers,
			AnimationAsset,
			PlayRate,
			ValidationError
		))
		{
			LastValidationError = ValidationError;
			return false;
		}
		LastTemplateStartTimes.Add(MotionTemplate.Metadata.TemplateId, NowSeconds);
		LastAcceptedMotionJson = FString::Printf(
			TEXT("{\"kind\":\"animation_asset\",\"template_id\":\"%s\"}"),
			*MotionTemplate.Metadata.TemplateId.ToString()
		);
		LastValidationError.Reset();
		if (bReplicateMotionCommands && !bApplyingReplicatedCommand)
		{
			PublishReplicatedAnimationTemplate(MotionTemplate.Metadata.TemplateId, Modifiers);
		}
		return true;
	}

	if (!AnimationAssetPlayer)
	{
		AnimationAssetPlayer = NewObject<ULLMNPCAnimationAssetPlayer>(this);
		AnimationAssetPlayer->Initialize(GetOwner());
	}
	if (AnimationAssetPlayer->ConflictsWith(MotionTemplate.Metadata.RequiredChannels))
	{
		if (!AnimationAssetPlayer->CanInterrupt())
		{
			LastValidationError = TEXT("LLMNPC_ANIMATION_ACTIVE_NOT_INTERRUPTIBLE");
			return false;
		}
		AnimationAssetPlayer->Stop(true);
	}

	FString PlaybackError;
	if (!AnimationAssetPlayer->Play(
		MotionTemplate,
		Modifiers,
		PlaybackError,
		PendingReplicatedStartOffsetSeconds
	))
	{
		LastValidationError = PlaybackError;
		return false;
	}

	LastTemplateStartTimes.Add(MotionTemplate.Metadata.TemplateId, NowSeconds);
	LastAcceptedMotionJson = FString::Printf(
		TEXT("{\"kind\":\"animation_asset\",\"template_id\":\"%s\"}"),
		*MotionTemplate.Metadata.TemplateId.ToString()
	);
	LastValidationError.Reset();
	if (
		bReplicateMotionCommands &&
		!bApplyingReplicatedCommand &&
		GetOwner() &&
		GetOwner()->GetIsReplicated() &&
		GetOwner()->HasAuthority()
	)
	{
		PublishReplicatedAnimationTemplate(MotionTemplate.Metadata.TemplateId, Modifiers);
	}
	return true;
}

TArray<FName> ULLMNPCMotionComponent::DeriveMotionChannels(const FLLMMotionPlan& Plan)
{
	TArray<FName> Channels;
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		const FString Control = Track.ControlId.ToString();
		FName Channel = NAME_None;
		if (Control.StartsWith(TEXT("head.")))
		{
			Channel = TEXT("head");
		}
		else if (Control == TEXT("gaze.target"))
		{
			Channel = TEXT("gaze");
		}
		else if (Control.StartsWith(TEXT("chest.")))
		{
			Channel = TEXT("chest");
		}
		else if (
			Control == TEXT("right_hand.ik") ||
			Control.StartsWith(TEXT("right_hand.local_offset."))
		)
		{
			Channel = TEXT("right_arm_ik");
		}
		else if (
			Control == TEXT("left_hand.ik") ||
			Control.StartsWith(TEXT("left_hand.local_offset."))
		)
		{
			Channel = TEXT("left_arm_ik");
		}
		else if (
			Control.StartsWith(TEXT("right_upperarm.")) ||
			Control.StartsWith(TEXT("right_lowerarm.")) ||
			Control == TEXT("right_hand.pitch") ||
			Control == TEXT("right_hand.yaw") ||
			Control == TEXT("right_hand.roll")
		)
		{
			Channel = TEXT("right_arm_fk");
		}
		else if (
			Control.StartsWith(TEXT("left_upperarm.")) ||
			Control.StartsWith(TEXT("left_lowerarm.")) ||
			Control.StartsWith(TEXT("mirror_left_upperarm.")) ||
			Control.StartsWith(TEXT("mirror_left_lowerarm.")) ||
			Control == TEXT("left_hand.pitch") ||
			Control == TEXT("left_hand.yaw") ||
			Control == TEXT("left_hand.roll") ||
			Control.StartsWith(TEXT("mirror_left_hand."))
		)
		{
			Channel = TEXT("left_arm_fk");
		}
		else if (
			Control.StartsWith(TEXT("right_fingers.")) ||
			Control == TEXT("right_hand.palm_target")
		)
		{
			Channel = TEXT("right_hand_pose");
		}
		else if (
			Control.StartsWith(TEXT("left_fingers.")) ||
			Control == TEXT("left_hand.palm_target")
		)
		{
			Channel = TEXT("left_hand_pose");
		}

		if (!Channel.IsNone())
		{
			Channels.AddUnique(Channel);
		}
	}

	return Channels;
}

bool ULLMNPCMotionComponent::ChannelsConflict(const TArray<FName>& A, const TArray<FName>& B)
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

void ULLMNPCMotionComponent::MergeSnapshot(
	FLLMProceduralPoseSnapshot& InOutSnapshot,
	const FLLMProceduralPoseSnapshot& Snapshot
)
{
	InOutSnapshot.GlobalAlpha = FMath::Max(InOutSnapshot.GlobalAlpha, Snapshot.GlobalAlpha);
	InOutSnapshot.HeadPitch += Snapshot.HeadPitch;
	InOutSnapshot.HeadYaw += Snapshot.HeadYaw;
	InOutSnapshot.HeadRoll += Snapshot.HeadRoll;
	InOutSnapshot.ChestPitch += Snapshot.ChestPitch;
	InOutSnapshot.ChestYaw += Snapshot.ChestYaw;
	InOutSnapshot.ChestRoll += Snapshot.ChestRoll;
	InOutSnapshot.RightHandLocalOffsetCS += Snapshot.RightHandLocalOffsetCS;
	InOutSnapshot.RightUpperArmAdditiveRotation += Snapshot.RightUpperArmAdditiveRotation;
	InOutSnapshot.RightLowerArmAdditiveRotation += Snapshot.RightLowerArmAdditiveRotation;
	InOutSnapshot.RightHandAdditiveRotation += Snapshot.RightHandAdditiveRotation;
	InOutSnapshot.LeftUpperArmAdditiveRotation += Snapshot.LeftUpperArmAdditiveRotation;
	InOutSnapshot.LeftLowerArmAdditiveRotation += Snapshot.LeftLowerArmAdditiveRotation;
	InOutSnapshot.LeftHandAdditiveRotation += Snapshot.LeftHandAdditiveRotation;
	InOutSnapshot.bLeftArmFKMirroredSource =
		InOutSnapshot.bLeftArmFKMirroredSource || Snapshot.bLeftArmFKMirroredSource;

	if (Snapshot.RightHandIKAlpha > InOutSnapshot.RightHandIKAlpha)
	{
		InOutSnapshot.RightHandIKAlpha = Snapshot.RightHandIKAlpha;
		InOutSnapshot.RightHandIKTargetCS = Snapshot.RightHandIKTargetCS;
	}
	if (Snapshot.RightHandPalmAlpha > InOutSnapshot.RightHandPalmAlpha)
	{
		InOutSnapshot.RightHandPalmAlpha = Snapshot.RightHandPalmAlpha;
		InOutSnapshot.RightHandPalmTargetCS = Snapshot.RightHandPalmTargetCS;
	}
	if (Snapshot.LeftHandIKAlpha > InOutSnapshot.LeftHandIKAlpha)
	{
		InOutSnapshot.LeftHandIKAlpha = Snapshot.LeftHandIKAlpha;
		InOutSnapshot.LeftHandIKTargetCS = Snapshot.LeftHandIKTargetCS;
	}
	InOutSnapshot.LeftHandLocalOffsetCS += Snapshot.LeftHandLocalOffsetCS;
	if (Snapshot.LeftHandPalmAlpha > InOutSnapshot.LeftHandPalmAlpha)
	{
		InOutSnapshot.LeftHandPalmAlpha = Snapshot.LeftHandPalmAlpha;
		InOutSnapshot.LeftHandPalmTargetCS = Snapshot.LeftHandPalmTargetCS;
	}
	if (Snapshot.GazeAlpha > InOutSnapshot.GazeAlpha)
	{
		InOutSnapshot.GazeAlpha = Snapshot.GazeAlpha;
		InOutSnapshot.GazeTargetCS = Snapshot.GazeTargetCS;
	}

	InOutSnapshot.RightFingersOpen = FMath::Max(InOutSnapshot.RightFingersOpen, Snapshot.RightFingersOpen);
	InOutSnapshot.RightFingersPoint = FMath::Max(InOutSnapshot.RightFingersPoint, Snapshot.RightFingersPoint);
	InOutSnapshot.LeftFingersOpen = FMath::Max(InOutSnapshot.LeftFingersOpen, Snapshot.LeftFingersOpen);
	InOutSnapshot.LeftFingersPoint = FMath::Max(InOutSnapshot.LeftFingersPoint, Snapshot.LeftFingersPoint);
}

bool ULLMNPCMotionComponent::InstallPostProcessAnimBP()
{
	RestorePostProcessAnimBP();
	LastPostProcessError.Reset();

	USkeletalMeshComponent* Mesh = GetOwnerMesh();
	if (!Mesh)
	{
		LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_MESH_NOT_FOUND");
		return false;
	}

	USkeletalMesh* SourceMesh = Mesh->GetSkeletalMeshAsset();
	if (!SourceMesh)
	{
		LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_SKELETAL_MESH_NOT_FOUND");
		return false;
	}

	UClass* DesiredClass = ResolvePostProcessAnimClass();
	if (!DesiredClass)
	{
		LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_CLASS_NOT_FOUND");
		return false;
	}

	if (!DesiredClass->IsChildOf(ULLMNPCPostProcessAnimInstance::StaticClass()))
	{
		LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_CLASS_INCOMPATIBLE");
		return false;
	}

	InstalledPostProcessMesh = Mesh;
	bOriginalPostProcessDisabled = Mesh->GetDisablePostProcessBlueprint();

	UClass* MeshPostProcessClass = *SourceMesh->GetPostProcessAnimBlueprint();
	if (PostProcessInstallMode == ELLMNPCPostProcessInstallMode::UseMeshAssetSetting)
	{
		if (MeshPostProcessClass != DesiredClass)
		{
			LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_MESH_ASSET_CLASS_MISMATCH");
			InstalledPostProcessMesh = nullptr;
			return false;
		}
	}
	else if (MeshPostProcessClass != DesiredClass)
	{
		OriginalSkeletalMesh = SourceMesh;
		const FName OverrideName = MakeUniqueObjectName(
			Mesh,
			USkeletalMesh::StaticClass(),
			FName(*FString::Printf(TEXT("%s_LLMNPCPostProcess"), *SourceMesh->GetName()))
		);
		TransientSkeletalMeshOverride = DuplicateObject<USkeletalMesh>(SourceMesh, Mesh, OverrideName);
		if (!TransientSkeletalMeshOverride)
		{
			LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_TRANSIENT_MESH_FAILED");
			InstalledPostProcessMesh = nullptr;
			OriginalSkeletalMesh = nullptr;
			return false;
		}

		TransientSkeletalMeshOverride->ClearFlags(RF_Public | RF_Standalone);
		TransientSkeletalMeshOverride->SetFlags(RF_Transient | RF_DuplicateTransient);
		TransientSkeletalMeshOverride->SetPostProcessAnimBlueprint(DesiredClass);
		Mesh->SetSkeletalMesh(TransientSkeletalMeshOverride, true);
	}

	Mesh->SetDisablePostProcessBlueprint(false);
	Mesh->InitAnim(true);

	UAnimInstance* InstalledInstance = Mesh->GetPostProcessInstance();
	if (!InstalledInstance || InstalledInstance->GetClass() != DesiredClass)
	{
		LastPostProcessError = TEXT("LLMNPC_POST_PROCESS_INSTANCE_NOT_CREATED");
		RestorePostProcessAnimBP();
		return false;
	}

	bPostProcessInstalled = true;
	UE_LOG(
		LogLLMNPCActionLayer,
		Log,
		TEXT("LLMNPCMotion: Installed post-process AnimBP %s on component %s using mode %s."),
		*DesiredClass->GetName(),
		*Mesh->GetName(),
		*UEnum::GetValueAsString(PostProcessInstallMode)
	);
	return true;
}

void ULLMNPCMotionComponent::RestorePostProcessAnimBP()
{
	if (InstalledPostProcessMesh)
	{
		if (
			TransientSkeletalMeshOverride &&
			OriginalSkeletalMesh &&
			InstalledPostProcessMesh->GetSkeletalMeshAsset() == TransientSkeletalMeshOverride
		)
		{
			InstalledPostProcessMesh->SetSkeletalMesh(OriginalSkeletalMesh, true);
		}

		InstalledPostProcessMesh->SetDisablePostProcessBlueprint(bOriginalPostProcessDisabled);
	}

	bPostProcessInstalled = false;
	InstalledPostProcessMesh = nullptr;
	OriginalSkeletalMesh = nullptr;
	TransientSkeletalMeshOverride = nullptr;
}

UClass* ULLMNPCMotionComponent::ResolvePostProcessAnimClass() const
{
	if (PostProcessAnimClass)
	{
		return PostProcessAnimClass.Get();
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	return Settings ? Settings->DefaultPostProcessAnimClass.LoadSynchronous() : nullptr;
}

USkeletalMeshComponent* ULLMNPCMotionComponent::GetOwnerMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}

	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
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
