#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Context/LLMNPCContextTypes.h"
#include "Context/LLMNPCExecutionContextTypes.h"
#include "Context/LLMNPCResolvedMotionModifiers.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionTypes.h"
#include "MicroMotion/LLMNPCMicroMotionScheduler.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "LLMNPCMotionComponent.generated.h"

class UAnimInstance;
class AActor;
class ULLMNPCAnimationAssetPlayer;
class ULLMNPCAPIClient;
class ULLMNPCModifierMappingProfile;
class ULLMNPCMotionValidator;
class ULLMNPCSkeletonProfile;
class ULLMNPCMotionTemplate;
class USkeletalMesh;
class USkeletalMeshComponent;
enum class ELLMNPCMotionValidationSource : uint8;

UENUM(BlueprintType)
enum class ELLMNPCMotionLODLevel : uint8
{
	Full,
	Reduced,
	Minimal
};

UENUM()
enum class ELLMNPCReplicatedMotionCommandKind : uint8
{
	None,
	ProceduralPlan,
	AnimationTemplate
};

USTRUCT()
struct FLLMNPCReplicatedTarget
{
	GENERATED_BODY()

	UPROPERTY()
	FString TargetRef;

	UPROPERTY()
	TObjectPtr<AActor> TargetActor;
};

USTRUCT()
struct FLLMNPCReplicatedMotionCommand
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ProtocolVersion = 1;

	UPROPERTY()
	int32 Sequence = 0;

	UPROPERTY()
	ELLMNPCReplicatedMotionCommandKind Kind = ELLMNPCReplicatedMotionCommandKind::None;

	UPROPERTY()
	FLLMMotionPlan ProceduralPlan;

	UPROPERTY()
	FName TemplateId = NAME_None;

	UPROPERTY()
	FLLMNPCTemplateModifiers TemplateModifiers;

	UPROPERTY()
	TArray<FLLMNPCReplicatedTarget> Targets;

	UPROPERTY()
	double ServerStartTimeSeconds = 0.0;
};

UENUM(BlueprintType)
enum class ELLMNPCPostProcessInstallMode : uint8
{
	Disabled,
	ComponentOverride,
	UseMeshAssetSetting
};

struct FLLMNPCQueuedMotionPlan
{
	FLLMMotionPlan Plan;
	TArray<FName> Channels;
	FName SourceTemplateId = NAME_None;
	float CooldownSeconds = 0.0f;
	double QueuedAtSeconds = 0.0;
	float InitialTimeSeconds = 0.0f;
	bool bReplicateOnStart = false;
	FLLMNPCResolvedMotionModifiers ResolvedModifiers;
	FLLMNPCModifierPolicy ModifierPolicy;
	TMap<FString, TWeakObjectPtr<AActor>> TargetActors;
	TMap<FString, FLLMNPCTargetRuntimeSample> TargetSamples;
};

struct FLLMNPCActiveMotionPlan
{
	FLLMNPCQueuedMotionPlan Request;
	float Time = 0.0f;
};

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCMotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULLMNPCMotionComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	bool SubmitMotionPlanJson(const FString& JsonString);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	bool SubmitMotionPlan(FLLMMotionPlan Plan);

	bool SubmitCompiledTemplatePlan(FLLMMotionPlan Plan);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Authoring Sandbox")
	bool SubmitAuthoringSandboxPlan(FLLMMotionPlan Plan);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Authoring Sandbox")
	bool CancelMotionClip(const FString& ClipId);

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Authoring Sandbox")
	bool IsMotionClipPendingOrActive(const FString& ClipId) const;

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Templates")
	bool SubmitPublishedTemplate(FName TemplateOrPublicActionId, FLLMNPCTemplateModifiers Modifiers);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void RequestMotionPlanFromContext(const FString& ContextJson);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void RegisterTarget(const FString& TargetRef, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void ClearTargets();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion")
	void ClearQueue();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	void StopAllMotions();

#if WITH_EDITOR
	void ResetMotionTestState();
	bool PreviewAnimationAssetTemplate(
		const ULLMNPCMotionTemplate& Template,
		const FLLMNPCTemplateModifiers& Modifiers
	);
#endif

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Debug")
	void SetRuntimeDebugOverlayEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Debug")
	bool IsRuntimeDebugOverlayEnabled() const { return bShowRuntimeDebugOverlay; }

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Micro Motion")
	void SetAmbientStyle(FName StyleTag);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	void TestNod();

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	void TestWave(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC Motion|Test")
	bool SubmitSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, AActor* TargetActor);

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Test")
	FString BuildSampleMotionPlanJson(ELLMNPCMotionDebugSample Sample, const FString& TargetRef) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion")
	FLLMProceduralPoseSnapshot GetCurrentSnapshot() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|Debug")
	FLLMNPCMotionDebugState GetDebugState() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion")
	int32 GetQueueCount() const { return Queue.Num(); }

	UFUNCTION(BlueprintPure, Category="LLM NPC Motion|LOD")
	ELLMNPCMotionLODLevel GetCurrentMotionLOD() const { return CurrentMotionLOD; }

	static ELLMNPCMotionLODLevel ResolveLODLevelForDistance(
		float DistanceCm,
		float FullQualityDistanceCm,
		float ReducedQualityDistanceCm
	);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TObjectPtr<ULLMNPCControlManifest> ControlManifest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Templates")
	TSoftObjectPtr<ULLMNPCSkeletonProfile> SkeletonProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion|Context")
	TSoftObjectPtr<ULLMNPCModifierMappingProfile> ModifierMappingProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Motion")
	TSubclassOf<UAnimInstance> PostProcessAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Post Process")
	ELLMNPCPostProcessInstallMode PostProcessInstallMode = ELLMNPCPostProcessInstallMode::ComponentOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Legacy")
	bool bAutoInstallPostProcessAnimBP = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="1", ClampMax="8"))
	int32 MaxQueueSize = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.1", ClampMax="30.0"))
	float MaxQueueWaitSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Micro Motion")
	bool bEnableMicroMotion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Micro Motion", meta=(ClampMin="0.0", ClampMax="2.0"))
	float MicroMotionAmplitude = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Micro Motion", meta=(ClampMin="0.05", ClampMax="1.0"))
	float BreathingFrequencyHz = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Micro Motion")
	int32 MicroMotionSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Gaze Scheduler")
	bool bEnableGazeScheduler = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Gaze Scheduler", meta=(ClampMin="0.0", ClampMax="0.35"))
	float AmbientGazeAlpha = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Gaze Scheduler")
	FVector2D GazeSwitchInterval = FVector2D(2.5f, 5.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Networking")
	bool bReplicateMotionCommands = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Networking")
	bool bRequireAuthorityForReplicatedSubmission = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|LOD")
	bool bEnableAutomaticMotionLOD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|LOD", meta=(ClampMin="0.0"))
	float FullQualityDistanceCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|LOD", meta=(ClampMin="0.0"))
	float ReducedQualityDistanceCm = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|LOD", meta=(ClampMin="0.016", ClampMax="0.5"))
	float ReducedUpdateIntervalSeconds = 0.0667f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|LOD", meta=(ClampMin="0.05", ClampMax="1.0"))
	float MinimalUpdateIntervalSeconds = 0.25f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Micro Motion")
	FName AmbientStyle = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastValidationError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastRawMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastAcceptedMotionJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString ActiveClipId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bMotionRequestInFlight = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	float ActiveTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	bool bPostProcessInstalled = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FString LastPostProcessError;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion|Debug")
	FLLMProceduralPoseSnapshot CurrentSnapshot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion|Debug")
	bool bShowRuntimeDebugOverlay = false;

private:
	UPROPERTY(ReplicatedUsing=OnRep_ReplicatedMotionCommand)
	FLLMNPCReplicatedMotionCommand ReplicatedMotionCommand;

	UPROPERTY()
	TObjectPtr<ULLMNPCMotionValidator> Validator;

	UPROPERTY()
	TObjectPtr<ULLMNPCAPIClient> APIClient;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCAnimationAssetPlayer> AnimationAssetPlayer;

	TArray<FLLMNPCQueuedMotionPlan> Queue;
	TArray<FLLMNPCActiveMotionPlan> ActiveMotions;
	TMap<FName, double> LastTemplateStartTimes;

	UPROPERTY()
	TMap<FString, TObjectPtr<AActor>> TargetMap;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> InstalledPostProcessMesh;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> OriginalSkeletalMesh;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> TransientSkeletalMeshOverride;

	bool bHasActivePlan = false;
	bool bOriginalPostProcessDisabled = false;
	FLLMNPCMicroMotionState MicroMotionState;
	FLLMNPCPoseBoneBindings CachedPoseBoneBindings;
	ELLMNPCMotionLODLevel CurrentMotionLOD = ELLMNPCMotionLODLevel::Full;
	float LODAccumulatedDeltaSeconds = 0.0f;
	float PendingReplicatedStartOffsetSeconds = 0.0f;
	int32 LastAppliedReplicationSequence = 0;
	bool bApplyingReplicatedCommand = false;
	FName LastRequestedTemplateId = NAME_None;
	FName LastResolvedTemplateId = NAME_None;
	FLLMNPCTemplateModifiers LastRequestedTemplateModifiers;
	FLLMNPCTemplateResolvedModifiers LastResolvedTemplateModifiers;
	FLLMNPCResolvedMotionModifiers LastContextResolvedModifiers;
	FLLMNPCModifierResolutionTrace LastModifierResolutionTrace;
	FLLMNPCExecutionContextSnapshot LastExecutionContext;
	FString LastValidationSource;
	bool bLastSubmissionAccepted = false;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FLLMNPCMotionSchedulerTest;
	friend class FLLMNPCForwardN3DynamicTargetTest;
	friend class FLLMNPCForwardN7EThumbsUpRuntimeMergeTest;
#endif

	void StartEligiblePlans();
	bool SubmitMotionPlanWithSource(
		FLLMMotionPlan Plan,
		ELLMNPCMotionValidationSource Source,
		const ULLMNPCMotionTemplate* SourceTemplate = nullptr,
		float InitialTimeSeconds = 0.0f,
		const FLLMNPCResolvedMotionModifiers* ResolvedModifiers = nullptr
	);
	bool ValidateTargetRefs(const FLLMMotionPlan& Plan, FString& OutError) const;
	bool SubmitAnimationAssetTemplate(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers
	);
	void UpdateActivePlans(float DeltaTime);
	bool UpdateActiveTargetSamples(
		FLLMNPCActiveMotionPlan& Active,
		float DeltaTime
	);
	FLLMNPCExecutionContextSnapshot BuildExecutionContextSnapshot(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		const FLLMNPCSelectionContextSnapshot& SelectionContext
	) const;
	FLLMNPCSelectionContextSnapshot BuildSelectionContextSnapshot() const;
	AActor* ResolveTargetActor(const FString& TargetRef) const;
	void RefreshPoseBoneBindings();
	void UpdateMicroMotion(float DeltaTime);
	bool ShouldRunMotionUpdate(float DeltaTime, float& OutUpdateDeltaSeconds);
	void UpdateMotionLOD();
	float ResolveNearestViewerDistanceCm() const;
	bool CanSubmitLocally() const;
	void PublishReplicatedPlan(const FLLMMotionPlan& Plan);
	void PublishReplicatedAnimationTemplate(
		FName TemplateId,
		const FLLMNPCTemplateModifiers& Modifiers
	);
	void PopulateReplicatedTargets(
		const FLLMMotionPlan& Plan,
		TArray<FLLMNPCReplicatedTarget>& OutTargets
	) const;
	double GetSynchronizedServerTimeSeconds() const;

	UFUNCTION()
	void OnRep_ReplicatedMotionCommand();

	bool IsChannelActive(FName Channel) const;
	static TArray<FName> DeriveMotionChannels(const FLLMMotionPlan& Plan);
	static bool ChannelsConflict(const TArray<FName>& A, const TArray<FName>& B);
	static void MergeSnapshot(
		FLLMProceduralPoseSnapshot& InOutSnapshot,
		const FLLMProceduralPoseSnapshot& Snapshot
	);
	bool InstallPostProcessAnimBP();
	void RestorePostProcessAnimBP();
	void DrawRuntimeDebugOverlay() const;
	UClass* ResolvePostProcessAnimClass() const;
	USkeletalMeshComponent* GetOwnerMesh() const;

	static FLLMMotionPlan BuildForwardN1VisualReviewPlan(
		ELLMNPCMotionDebugSample Sample
	);
	static FLLMMotionPlan BuildInvalidUnknownControlPlan();
};
