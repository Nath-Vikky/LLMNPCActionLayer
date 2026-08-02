#pragma once

#include "CoreMinimal.h"
#include "Behavior/LLMNPCBehaviorTypes.h"
#include "Components/ActorComponent.h"
#include "Context/LLMNPCContextTypes.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Engine/TimerHandle.h"
#include "Providers/LLMNPCModelProvider.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "LLMNPCDialogueComponent.generated.h"

class APlayerController;
class ILLMNPCModelProvider;
class ULLMNPCBehaviorCoordinator;
class ULLMNPCChatWidget;
class ULLMNPCConversationSession;
class ULLMNPCEmotionComponent;
class ULLMNPCMotionComponent;
class ULLMNPCPersonalityProfile;
class ULLMNPCRelationshipComponent;
class ULLMNPCSceneContextComponent;
class ULLMNPCSelectionAnalyticsSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FLLMNPCDialogueMessageEvent,
	const FLLMNPCConversationMessage&,
	Message
);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FLLMNPCDialogueStateEvent,
	ELLMNPCDialogueState,
	State
);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FLLMNPCDialogueTurnEvent,
	const FLLMNPCDialogueTurnResult&,
	Result
);

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULLMNPCDialogueComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	bool SendPlayerMessage(const FString& Message);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void CancelActiveRequest();

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void CancelActiveBehavior();

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void ResetConversation();

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void SetProviderKind(ELLMNPCModelProviderKind NewProviderKind);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void SetProviderId(FName NewProviderId);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void SetMotionComponent(ULLMNPCMotionComponent* InMotionComponent);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue")
	void RegisterTarget(const FString& TargetRef, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue|Context")
	void RegisterSceneTarget(
		const FString& TargetRef,
		AActor* TargetActor,
		FName Category,
		const TArray<FName>& SemanticTags,
		float Salience = 0.5f
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue|Context")
	void SetSceneStateActive(FName StateName, bool bActive);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue|Context")
	void SetEmotionContext(
		FName Emotion,
		float Intensity,
		float Valence = 0.0f,
		float Arousal = 0.0f
	);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue|Context")
	void ResetEmotionContext();

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue|UI")
	ULLMNPCChatWidget* CreateChatWidget(APlayerController* PlayerController, int32 ZOrder = 10);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue")
	bool IsRequestInFlight() const { return bRequestInFlight; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue")
	ELLMNPCDialogueState GetDialogueState() const { return State; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue")
	ULLMNPCConversationSession* GetConversationSession() const { return ConversationSession; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue|Debug")
	FLLMNPCDialogueDebugState GetDebugState() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue|Debug")
	FLLMNPCBehaviorDebugState GetBehaviorDebugState() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue|Context")
	FLLMNPCSelectionContextSnapshot GetSelectionContextSnapshot() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue|Context")
	const TArray<FLLMNPCTemplateCandidate>& GetLastOfferedCandidates() const { return LastOfferedCandidates; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue|Context")
	const TArray<FLLMNPCCandidateExclusion>& GetLastCandidateExclusions() const
	{
		return LastCandidateExclusions;
	}

	UPROPERTY(BlueprintAssignable, Category="LLM NPC|Dialogue")
	FLLMNPCDialogueMessageEvent OnMessageAdded;

	UPROPERTY(BlueprintAssignable, Category="LLM NPC|Dialogue")
	FLLMNPCDialogueStateEvent OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category="LLM NPC|Dialogue")
	FLLMNPCDialogueTurnEvent OnTurnCompleted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue")
	FName NPCId = TEXT("npc");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue")
	ELLMNPCModelProviderKind ProviderKind = ELLMNPCModelProviderKind::UseProjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue")
	FName ProviderIdOverride = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue", meta=(ClampMin="2", ClampMax="64"))
	int32 MaxHistoryMessages = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue")
	bool bEnableLocalCommandFallback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue")
	bool bAutoFindMotionComponent = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue|Context")
	bool bAutoFindContextComponents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue|Context")
	TObjectPtr<ULLMNPCPersonalityProfile> PersonalityProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Dialogue|UI")
	bool bAutoCreateChatWidget = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	ELLMNPCDialogueState State = ELLMNPCDialogueState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FLLMNPCDialogueTurnResult LastTurnResult;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FLLMNPCPhase8WatchdogRecoveryTest;
#endif

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCConversationSession> ConversationSession;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCBehaviorCoordinator> BehaviorCoordinator;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCMotionComponent> MotionComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCChatWidget> ChatWidget;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCEmotionComponent> EmotionComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCRelationshipComponent> RelationshipComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCSceneContextComponent> SceneContextComponent;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCSelectionAnalyticsSubsystem> SelectionAnalytics;

	UPROPERTY(Transient)
	TArray<FLLMNPCTemplateCandidate> LastOfferedCandidates;

	UPROPERTY(Transient)
	TArray<FLLMNPCCandidateExclusion> LastCandidateExclusions;

	TSharedPtr<ILLMNPCModelProvider> ModelProvider;
	TSharedPtr<ILLMNPCModelProvider> FallbackModelProvider;
	FLLMNPCModelTurnRequest ActiveRequest;
	bool bRequestInFlight = false;
	FName LastProviderId = NAME_None;
	FLLMNPCModelTurnResult LastProviderResult;
	int32 LastSourceCandidateCount = 0;
	int32 LastOfferedCandidateCount = 0;
	int32 LastExcludedCandidateCount = 0;
	FString LastRequestSchemaVersion;
	FTimerHandle RequestWatchdogHandle;
	TArray<FLLMNPCTemplateCandidate> ActiveOfferedCandidates;
	TArray<FLLMNPCCandidateExclusion> ActiveCandidateExclusions;

	void EnsureRuntimeObjects();
	void RecreateProvider();
	ELLMNPCModelProviderKind ResolveProviderKind() const;
	FName ResolveProviderId() const;
	void ArmRequestWatchdog();
	void ClearRequestWatchdog();
	void HandleRequestWatchdog();
	void SetState(ELLMNPCDialogueState NewState);
	void HandleProviderResult(
		const FLLMNPCModelTurnResult& ProviderResult,
		bool bUsedFallback,
		FName OriginalProviderError = NAME_None
	);
	void HandleProviderFailure(
		const FLLMNPCModelTurnResult& ProviderResult,
		bool bFallbackAlreadyUsed = false
	);
	void CompleteFromDecision(
		FLLMNPCModelTurnDecision Decision,
		bool bParseSucceeded,
		const FString& ParseError,
		bool bUsedFallback,
		FName OriginalProviderError
	);
	void CompleteFailure(FName ErrorCode, const FString& ErrorMessage, bool bAddAssistantMessage);
	void CompleteAnalytics(FName Outcome, FName ErrorCode, bool bUsedFallback);
	void ResetActiveSelection();
	FName ResolveSkeletonProfileId() const;
};
