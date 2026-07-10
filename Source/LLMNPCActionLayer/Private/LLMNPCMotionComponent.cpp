#include "LLMNPCMotionComponent.h"

#include "LLMNPCAPIClient.h"
#include "LLMNPCActionLayer.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionValidator.h"
#include "LLMNPCPostProcessAnimInstance.h"
#include "LLMNPCSettings.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"

ULLMNPCMotionComponent::ULLMNPCMotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
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

	if (bAutoInstallPostProcessAnimBP && PostProcessInstallMode != ELLMNPCPostProcessInstallMode::Disabled)
	{
		InstallPostProcessAnimBP();
	}
}

void ULLMNPCMotionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestorePostProcessAnimBP();
	Super::EndPlay(EndPlayReason);
}

void ULLMNPCMotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	StartEligiblePlans();
	UpdateActivePlans(DeltaTime);
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
	ULLMNPCSkeletonProfile* ResolvedProfile = SkeletonProfile.LoadSynchronous();
	if (!ResolvedProfile)
	{
		LastValidationError = TEXT("LLMNPC_SKELETON_PROFILE_NOT_FOUND");
		return false;
	}

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
		MotionTemplate = Library->FindPublishedVariant(
			TemplateOrPublicActionId,
			ResolvedProfile->ProfileId
		);
	}

	if (!MotionTemplate)
	{
		LastValidationError = TEXT("LLMNPC_TEMPLATE_NOT_FOUND_OR_NOT_PUBLISHED");
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
		MotionTemplate
	);
}

bool ULLMNPCMotionComponent::SubmitMotionPlanWithSource(
	FLLMMotionPlan Plan,
	ELLMNPCMotionValidationSource Source,
	const ULLMNPCMotionTemplate* SourceTemplate
)
{
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
	Request.Channels = DeriveMotionChannels(Request.Plan);
	Request.QueuedAtSeconds = FPlatformTime::Seconds();
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
	State.bHasActivePlan = bHasActivePlan;
	State.ActivePlanCount = ActiveMotions.Num();
	State.bMotionRequestInFlight = bMotionRequestInFlight;
	State.bPostProcessInstalled = bPostProcessInstalled;
	State.LastPostProcessError = LastPostProcessError;
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
		NewActive.Time = 0.0f;
		if (!NewActive.Request.SourceTemplateId.IsNone())
		{
			LastTemplateStartTimes.Add(NewActive.Request.SourceTemplateId, NowSeconds);
		}

		ActiveMotions.Add(MoveTemp(NewActive));
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
	CurrentSnapshot = FLLMProceduralPoseSnapshot();
	USkeletalMeshComponent* Mesh = GetOwnerMesh();

	for (int32 ActiveIndex = ActiveMotions.Num() - 1; ActiveIndex >= 0; --ActiveIndex)
	{
		FLLMNPCActiveMotionPlan& Active = ActiveMotions[ActiveIndex];
		Active.Time += DeltaTime;
		if (Active.Time >= Active.Request.Plan.Clip.Duration)
		{
			ActiveMotions.RemoveAt(ActiveIndex);
			continue;
		}

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
			Control.StartsWith(TEXT("right_fingers.")) ||
			Control == TEXT("right_hand.palm_target")
		)
		{
			Channel = TEXT("right_hand_pose");
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
		(A.Contains(RightArmFKChannel) && B.Contains(RightArmIKChannel));
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
	if (Snapshot.GazeAlpha > InOutSnapshot.GazeAlpha)
	{
		InOutSnapshot.GazeAlpha = Snapshot.GazeAlpha;
		InOutSnapshot.GazeTargetCS = Snapshot.GazeTargetCS;
	}

	InOutSnapshot.RightFingersOpen = FMath::Max(InOutSnapshot.RightFingersOpen, Snapshot.RightFingersOpen);
	InOutSnapshot.RightFingersPoint = FMath::Max(InOutSnapshot.RightFingersPoint, Snapshot.RightFingersPoint);
	InOutSnapshot.LeftFingersOpen = FMath::Max(InOutSnapshot.LeftFingersOpen, Snapshot.LeftFingersOpen);
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
