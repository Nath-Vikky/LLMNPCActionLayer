#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Evaluation/LLMNPCForwardN7Evaluation.h"
#include "Evaluation/LLMNPCForwardN8Stability.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class AActor;
class ULLMNPCDialogueComponent;
class ULLMNPCMotionComponent;
class ULLMNPCTemplateLibrarySubsystem;
class ULLMNPCMotionTemplate;

template<typename OptionType>
class SComboBox;

struct FLLMNPCTestActorOption
{
	TWeakObjectPtr<ULLMNPCMotionComponent> MotionComponent;
	FText Label;
};

struct FLLMNPCTestTemplateOption
{
	FName TemplateId = NAME_None;
	FText Label;
	FVector2D AmplitudeRange = FVector2D(1.0f, 1.0f);
	FVector2D SpeedRange = FVector2D(1.0f, 1.0f);
	FVector2D DurationRange = FVector2D(1.0f, 1.0f);
	TArray<FName> AllowedStyles;
	TArray<FName> RequiredChannels;
	float CooldownSeconds = 0.0f;
	bool bAllowMirror = false;
	bool bRequiresTarget = false;
};

enum class ELLMNPCTestParameterPreset : uint8
{
	Minimum,
	Default,
	Maximum
};

enum class ELLMNPCTestContextPreset : uint8
{
	Neutral,
	Excited,
	RightHandOccupied,
	Walking
};

struct FLLMNPCTestExecutionRecord
{
	FDateTime TimestampUtc;
	FString ActorName;
	FName RequestedTemplateId = NAME_None;
	FName ResolvedTemplateId = NAME_None;
	FString Preset;
	FLLMNPCTemplateModifiers RequestedModifiers;
	float ResolvedAmplitude = 1.0f;
	float ResolvedSpeedScale = 1.0f;
	float ResolvedDurationScale = 1.0f;
	float ResolvedReachScale = 1.0f;
	float ResolvedHeightScale = 1.0f;
	float ResolvedGazeEngagement = 1.0f;
	float ResolvedPalmOrientationWeight = 1.0f;
	float ResolvedFingerPoseWeight = 1.0f;
	float ResolvedTorsoParticipation = 1.0f;
	bool bResolvedMirror = false;
	FString ExecutionMovementMode;
	float TargetDistanceCm = 0.0f;
	float TargetHeightRelativeCm = 0.0f;
	float AvailableSpace = 1.0f;
	bool bRightHandOccupied = false;
	bool bLeftHandOccupied = false;
	FName ModifierResultCode = NAME_None;
	FString ModifierResolutionTrace;
	bool bAccepted = false;
	FString ValidationError;
	TArray<FName> ActiveChannels;
};

struct FLLMNPCOnlineEvaluationRecord
{
	FDateTime TimestampUtc;
	FGuid RequestId;
	FString ActorName;
	FString InputHash;
	FName ProviderId = NAME_None;
	FString ProviderModelId;
	FString ConfigHash;
	bool bStrictProviderIdentity = false;
	bool bUsedLocalFallback = false;
	bool bResponseSchemaValid = false;
	FString RequestSchemaVersion;
	int32 SourceCandidateCount = 0;
	int32 OfferedCandidateCount = 0;
	int32 ExcludedCandidateCount = 0;
	TArray<FName> OfferedCandidateIds;
	TArray<FLLMNPCCandidateExclusion> CandidateExclusions;
	TArray<FName> ActiveStates;
	FName ContextEmotion = NAME_None;
	TArray<FString> AvailableTargetRefs;
	FName SelectedActionId = NAME_None;
	FName ResolvedTemplateId = NAME_None;
	bool bActionExecuted = false;
	bool bBehaviorStarted = false;
	FName ErrorCode = NAME_None;
	float TotalLatencySeconds = -1.0f;
	int32 AttemptCount = 0;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
	FString TargetRef;
	float RequestedAmplitude = 1.0f;
	float RequestedSpeedScale = 1.0f;
	float RequestedDurationScale = 1.0f;
	FName RequestedStyle = TEXT("neutral");
	bool bRequestedMirror = false;
	float ResolvedAmplitude = 1.0f;
	float ResolvedSpeedScale = 1.0f;
	float ResolvedDurationScale = 1.0f;
	FName ResolvedStyle = TEXT("neutral");
	bool bResolvedMirror = false;
	bool bModifiersClamped = false;
	FString ModifierResolutionTrace;
	FString ValidatorResult;
	bool bMatrixCase = false;
	FName MatrixCaseId = NAME_None;
	ELLMNPCForwardN7ExpectedSelection ExpectedSelection =
		ELLMNPCForwardN7ExpectedSelection::ExactAction;
	FName ExpectedActionId = NAME_None;
	FString ExpectedTargetRef;
	bool bCheckExpectedMirror = false;
	bool bExpectedMirror = false;
	bool bCheckExpectedStyle = false;
	FName ExpectedStyle = NAME_None;
	TArray<FName> ExpectedExclusionReasons;
	TArray<FName> CoverageTags;
	bool bProviderPassed = false;
	bool bSchemaPassed = false;
	bool bSelectionPassed = false;
	bool bExecutionPassed = false;
	bool bContextPassed = false;
	bool bStylePassed = false;
	bool bValidatorPassed = false;
	FString FailureReason;
	bool bPassed = false;
	bool bPlaybackRequired = false;
	bool bPlaybackObserved = false;
	bool bPlaybackCompleted = false;
	float PlaybackWaitSeconds = 0.0f;
	FString PlaybackWaitResult = TEXT("not_started");
	FString VisualReview = TEXT("pending");
};

class SLLMNPCMotionTestConsole final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLLMNPCMotionTestConsole) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLLMNPCMotionTestConsole() override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		const double InCurrentTime,
		const float InDeltaTime
	) override;

private:
	TArray<TSharedPtr<FLLMNPCTestActorOption>> ActorOptions;
	TSharedPtr<FLLMNPCTestActorOption> SelectedActor;
	TSharedPtr<SComboBox<TSharedPtr<FLLMNPCTestActorOption>>> ActorCombo;

	TArray<TSharedPtr<FLLMNPCTestTemplateOption>> TemplateOptions;
	TSharedPtr<FLLMNPCTestTemplateOption> SelectedTemplate;
	TSharedPtr<SComboBox<TSharedPtr<FLLMNPCTestTemplateOption>>> TemplateCombo;

	TArray<FLLMNPCTestExecutionRecord> ExecutionRecords;
	TArray<FLLMNPCOnlineEvaluationRecord> OnlineEvaluationRecords;
	TArray<FLLMNPCForwardN7MatrixCase> OnlineMatrixCases;
	TArray<FName> StabilityTemplateIds;
	FLLMNPCForwardN8StabilityConfig StabilityConfig;
	FLLMNPCForwardN8StabilityReport StabilityReport;
	FString TargetRef = TEXT("player");
	FString OnlineInput = TEXT("Greet the player with a friendly body gesture.");
	FString ActiveOnlineInputHash;
	FString ActiveOnlineConfigHash;
	FString ActiveOnlineExpectedModel;
	FName Style = TEXT("neutral");
	float Amplitude = 1.0f;
	float SpeedScale = 1.0f;
	float DurationScale = 1.0f;
	float TestTargetDistanceCm = 240.0f;
	float TestTargetHeightCm = 0.0f;
	int32 RandomSeed = 0;
	bool bMirror = false;
	bool bSweepRunning = false;
	bool bOnlineRunInFlight = false;
	bool bOnlineMatrixRunning = false;
	bool bOnlineMatrixCompleted = false;
	bool bOnlineMatrixCancelled = false;
	bool bOnlineMatrixWaitingForPlayback = false;
	bool bRestoreOnlineDialogueSettings = false;
	bool bStabilityRunning = false;
	bool bStabilityWaitingForPlayback = false;
	bool bStabilityPlaybackObserved = false;
	bool bStabilityPostProcessRequired = false;
	int32 SweepStepIndex = INDEX_NONE;
	int32 OnlineMatrixCaseIndex = INDEX_NONE;
	int32 OnlineMatrixPassedCount = 0;
	int32 OnlineMatrixFailedCount = 0;
	int32 StabilityRequestIndex = INDEX_NONE;
	double SweepNextActionTime = 0.0;
	double OnlineRequestStartedAt = 0.0;
	double OnlineMatrixNextCaseTime = 0.0;
	double OnlineMatrixPlaybackWaitStartedAt = 0.0;
	double OnlineMatrixPlaybackIdleSince = 0.0;
	double StabilityStartedAt = 0.0;
	double StabilityRequestStartedAt = 0.0;
	double StabilityPlaybackIdleSince = 0.0;
	double StabilityNextRequestTime = 0.0;
	double StabilityLastCheckpointAt = 0.0;
	float RuntimeRefreshAccumulator = 0.0f;
	FGuid ActiveOnlineRequestId;
	TWeakObjectPtr<AActor> N3TestTargetActor;
	TWeakObjectPtr<ULLMNPCDialogueComponent> ActiveOnlineDialogue;
	ELLMNPCModelProviderKind PreviousProviderKind =
		ELLMNPCModelProviderKind::UseProjectSettings;
	FName PreviousProviderIdOverride = NAME_None;
	bool bPreviousLocalFallback = true;
	FString OnlineMatrixHumanReview = TEXT("pending");
	FDateTime OnlineMatrixHumanReviewedAtUtc;
	FString LastSavedReportPath;
	FString StabilityReportPath;
	FText StatusText;
	bool bStatusError = false;

	TSharedRef<SWidget> MakeSectionHeader(const FText& Text) const;
	TSharedRef<SWidget> MakeFormRow(const FText& Label, const TSharedRef<SWidget>& Control) const;
	TSharedRef<SWidget> GenerateActorOption(TSharedPtr<FLLMNPCTestActorOption> Option) const;
	TSharedRef<SWidget> GenerateTemplateOption(TSharedPtr<FLLMNPCTestTemplateOption> Option) const;

	FText GetSelectedActorText() const;
	FText GetSelectedTemplateText() const;
	FText GetTemplatePolicyText() const;
	FText GetDebugStateText() const;
	FText GetOnlineTraceText() const;
	FText GetStabilityTraceText() const;
	FSlateColor GetStatusColor() const;
	ECheckBoxState GetOverlayCheckState() const;

	void RefreshRuntimeTargets(bool bRefreshLibrary);
	void RefreshTemplates(bool bRefreshLibrary);
	ULLMNPCMotionComponent* GetSelectedMotionComponent() const;
	ULLMNPCDialogueComponent* GetSelectedDialogueComponent() const;
	ULLMNPCTemplateLibrarySubsystem* GetTemplateLibrary() const;
	const ULLMNPCMotionTemplate* GetSelectedMotionTemplate() const;
	bool ApplyPreset(ELLMNPCTestParameterPreset Preset);
	bool ApplyContextPreset(ELLMNPCTestContextPreset Preset);
	bool PlaceN3TestTarget();
	bool ExecuteCurrent(const FString& PresetLabel);
	void ExecuteForwardN1ReviewSample(
		ELLMNPCMotionDebugSample Sample,
		const FText& Label
	);
	void StartSweepStep();
	void PollOnlineEvaluation();
	void CompleteOnlineEvaluation(ULLMNPCDialogueComponent& Dialogue);
	void PollOnlineMatrixPlayback(double CurrentTime);
	void CompleteOnlineMatrixCasePlayback(
		bool bPlaybackGatePassed,
		const FString& Result
	);
	const FLLMNPCForwardN7MatrixCase* GetActiveOnlineMatrixCase() const;
	bool PrepareOnlineMatrixCase(const FLLMNPCForwardN7MatrixCase& TestCase);
	void StartOnlineMatrixCase();
	void FinishOnlineMatrix();
	void ResetOnlineMatrixContext();
	void RestoreOnlineDialogueSettings();
	void TickStabilitySession(double CurrentTime, float DeltaTime);
	bool StartStabilitySession(const FLLMNPCForwardN8StabilityConfig& Config);
	bool SubmitNextStabilityRequest(double CurrentTime);
	void CompleteCurrentStabilityRequest(double CurrentTime);
	void FinishStabilitySession(
		const FString& Status,
		const FString& Error = FString()
	);
	bool SaveStabilityReport(const FString& OverridePath = FString());
	const FLLMNPCTestTemplateOption* FindStabilityTemplateOption(FName TemplateId) const;
	void SetStatus(const FText& Text, bool bError);

	void HandleActorChanged(
		TSharedPtr<FLLMNPCTestActorOption> Option,
		ESelectInfo::Type SelectInfo
	);
	void HandleTemplateChanged(
		TSharedPtr<FLLMNPCTestTemplateOption> Option,
		ESelectInfo::Type SelectInfo
	);
	void HandleOverlayChanged(ECheckBoxState State);

	FReply HandleRefresh();
	FReply HandleExecute();
	FReply HandleStop();
	FReply HandleMinimum();
	FReply HandleDefault();
	FReply HandleMaximum();
	FReply HandleRunSweep();
	FReply HandleContextNeutral();
	FReply HandleContextExcited();
	FReply HandleContextRightBusy();
	FReply HandleContextWalking();
	FReply HandlePlaceN3TestTarget();
	FReply HandleForwardN1ShoulderReview();
	FReply HandleForwardN1RelaxedReview();
	FReply HandleForwardN1CurlReview();
	FReply HandleRunOnlineEvaluation();
	FReply HandleRunOnlineMatrix();
	FReply HandleOnlineMatrixVisualPass();
	FReply HandleOnlineMatrixVisualFail();
	FReply ApplyOnlineMatrixHumanReview(const FString& Review);
	FReply HandleCancelOnlineEvaluation();
	FReply HandleRunStabilitySmoke();
	FReply HandleRunStabilityFormal();
	FReply HandleCancelStability();
	FReply HandleStabilityVisualPass();
	FReply HandleStabilityVisualFail();
	FReply ApplyStabilityHumanReview(const FString& Review);
	FReply HandleSaveReport();
};
