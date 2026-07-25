#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
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
	int32 SourceCandidateCount = 0;
	int32 OfferedCandidateCount = 0;
	int32 ExcludedCandidateCount = 0;
	TArray<FName> OfferedCandidateIds;
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
	bool bPassed = false;
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
	FString TargetRef = TEXT("player");
	FString OnlineInput = TEXT("Greet the player with a friendly body gesture.");
	FString ActiveOnlineInputHash;
	FString ActiveOnlineConfigHash;
	FString ActiveOnlineExpectedModel;
	FName Style = TEXT("neutral");
	float Amplitude = 1.0f;
	float SpeedScale = 1.0f;
	float DurationScale = 1.0f;
	int32 RandomSeed = 0;
	bool bMirror = false;
	bool bSweepRunning = false;
	bool bOnlineRunInFlight = false;
	bool bRestoreOnlineDialogueSettings = false;
	int32 SweepStepIndex = INDEX_NONE;
	double SweepNextActionTime = 0.0;
	double OnlineRequestStartedAt = 0.0;
	float RuntimeRefreshAccumulator = 0.0f;
	FGuid ActiveOnlineRequestId;
	TWeakObjectPtr<ULLMNPCDialogueComponent> ActiveOnlineDialogue;
	ELLMNPCModelProviderKind PreviousProviderKind =
		ELLMNPCModelProviderKind::UseProjectSettings;
	FName PreviousProviderIdOverride = NAME_None;
	bool bPreviousLocalFallback = true;
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
	FSlateColor GetStatusColor() const;
	ECheckBoxState GetOverlayCheckState() const;

	void RefreshRuntimeTargets(bool bRefreshLibrary);
	void RefreshTemplates(bool bRefreshLibrary);
	ULLMNPCMotionComponent* GetSelectedMotionComponent() const;
	ULLMNPCDialogueComponent* GetSelectedDialogueComponent() const;
	ULLMNPCTemplateLibrarySubsystem* GetTemplateLibrary() const;
	const ULLMNPCMotionTemplate* GetSelectedMotionTemplate() const;
	bool ApplyPreset(ELLMNPCTestParameterPreset Preset);
	bool ExecuteCurrent(const FString& PresetLabel);
	void ExecuteForwardN1ReviewSample(
		ELLMNPCMotionDebugSample Sample,
		const FText& Label
	);
	void StartSweepStep();
	void PollOnlineEvaluation();
	void CompleteOnlineEvaluation(ULLMNPCDialogueComponent& Dialogue);
	void RestoreOnlineDialogueSettings();
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
	FReply HandleForwardN1ShoulderReview();
	FReply HandleForwardN1RelaxedReview();
	FReply HandleForwardN1CurlReview();
	FReply HandleRunOnlineEvaluation();
	FReply HandleCancelOnlineEvaluation();
	FReply HandleSaveReport();
};
