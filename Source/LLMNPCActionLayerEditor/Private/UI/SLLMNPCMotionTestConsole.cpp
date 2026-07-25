#include "UI/SLLMNPCMotionTestConsole.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Dialogue/LLMNPCDialogueComponent.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Styling/AppStyle.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SLLMNPCMotionTestConsole"

namespace
{
FString PresetName(ELLMNPCTestParameterPreset Preset)
{
	switch (Preset)
	{
	case ELLMNPCTestParameterPreset::Minimum:
		return TEXT("minimum");
	case ELLMNPCTestParameterPreset::Maximum:
		return TEXT("maximum");
	case ELLMNPCTestParameterPreset::Default:
	default:
		return TEXT("default");
	}
}

FString JoinNames(const TArray<FName>& Names)
{
	TArray<FString> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(Name.ToString());
	}
	return FString::Join(Values, TEXT(", "));
}

FString HashUtf8Text(const FString& Value)
{
	const FTCHARToUTF8 Utf8(*Value);
	FMD5 Hash;
	Hash.Update(
		reinterpret_cast<const uint8*>(Utf8.Get()),
		static_cast<uint32>(Utf8.Length())
	);
	uint8 Digest[16];
	Hash.Final(Digest);
	return FString::Printf(TEXT("md5:%s"), *BytesToHex(Digest, UE_ARRAY_COUNT(Digest)));
}
}

void SLLMNPCMotionTestConsole::Construct(const FArguments& InArgs)
{
	static_cast<void>(InArgs);
	StatusText = LOCTEXT("WaitingForPIE", "Waiting for a PIE Manny motion component");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(20.0f, 16.0f))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "LLM NPC Motion Test Console"))
					.TextStyle(FAppStyle::Get(), "LargeText")
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("RuntimeSection", "PIE Runtime"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("NPC", "NPC"),
						SAssignNew(ActorCombo, SComboBox<TSharedPtr<FLLMNPCTestActorOption>>)
						.OptionsSource(&ActorOptions)
						.OnGenerateWidget(this, &SLLMNPCMotionTestConsole::GenerateActorOption)
						.OnSelectionChanged(this, &SLLMNPCMotionTestConsole::HandleActorChanged)
						[
							SNew(STextBlock).Text(this, &SLLMNPCMotionTestConsole::GetSelectedActorText)
						]
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Template", "Published Template"),
						SAssignNew(TemplateCombo, SComboBox<TSharedPtr<FLLMNPCTestTemplateOption>>)
						.OptionsSource(&TemplateOptions)
						.OnGenerateWidget(this, &SLLMNPCMotionTestConsole::GenerateTemplateOption)
						.OnSelectionChanged(this, &SLLMNPCMotionTestConsole::HandleTemplateChanged)
						[
							SNew(STextBlock).Text(this, &SLLMNPCMotionTestConsole::GetSelectedTemplateText)
						]
					)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 2.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SLLMNPCMotionTestConsole::GetTemplatePolicyText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("ModifiersSection", "Requested Modifiers"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Amplitude", "Amplitude"),
						SNew(SNumericEntryBox<float>)
						.MinValue(0.01f)
						.MaxValue(4.0f)
						.Value_Lambda([this]() { return TOptional<float>(Amplitude); })
						.OnValueChanged_Lambda([this](float Value) { Amplitude = Value; })
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Speed", "Speed Scale"),
						SNew(SNumericEntryBox<float>)
						.MinValue(0.01f)
						.MaxValue(4.0f)
						.Value_Lambda([this]() { return TOptional<float>(SpeedScale); })
						.OnValueChanged_Lambda([this](float Value) { SpeedScale = Value; })
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Duration", "Duration Scale"),
						SNew(SNumericEntryBox<float>)
						.MinValue(0.01f)
						.MaxValue(4.0f)
						.Value_Lambda([this]() { return TOptional<float>(DurationScale); })
						.OnValueChanged_Lambda([this](float Value) { DurationScale = Value; })
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Style", "Style"),
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromName(Style); })
						.OnTextChanged_Lambda([this](const FText& Value)
						{
							Style = FName(*Value.ToString().TrimStartAndEnd());
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("TargetRef", "Target Ref"),
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(TargetRef); })
						.OnTextChanged_Lambda([this](const FText& Value)
						{
							TargetRef = Value.ToString();
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("RandomSeed", "Random Seed"),
						SNew(SNumericEntryBox<int32>)
						.MinValue(0)
						.MaxValue(MAX_int32)
						.Value_Lambda([this]() { return TOptional<int32>(RandomSeed); })
						.OnValueChanged_Lambda([this](int32 Value) { RandomSeed = Value; })
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Mirror", "Mirror"),
						SNew(SCheckBox)
						.IsChecked_Lambda([this]()
						{
							return bMirror ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
						{
							bMirror = State == ECheckBoxState::Checked;
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("Overlay", "Viewport Debug Overlay"),
						SNew(SCheckBox)
						.IsChecked(this, &SLLMNPCMotionTestConsole::GetOverlayCheckState)
						.OnCheckStateChanged(this, &SLLMNPCMotionTestConsole::HandleOverlayChanged)
					)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 8.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f, 3.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh PIE NPCs and the Published template library."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleRefresh)
						[
							SNew(STextBlock).Text(LOCTEXT("Refresh", "Refresh"))
						]
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleExecute)
						[
							SNew(STextBlock).Text(LOCTEXT("Execute", "Execute"))
						]
					]
					+ SUniformGridPanel::Slot(2, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleStop)
						[
							SNew(STextBlock).Text(LOCTEXT("Stop", "Stop"))
						]
					]
					+ SUniformGridPanel::Slot(3, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleSaveReport)
						[
							SNew(STextBlock).Text(LOCTEXT("SaveReport", "Save Report"))
						]
					]
					+ SUniformGridPanel::Slot(0, 1)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleMinimum)
						[
							SNew(STextBlock).Text(LOCTEXT("Minimum", "Minimum"))
						]
					]
					+ SUniformGridPanel::Slot(1, 1)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleDefault)
						[
							SNew(STextBlock).Text(LOCTEXT("Default", "Default"))
						]
					]
					+ SUniformGridPanel::Slot(2, 1)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleMaximum)
						[
							SNew(STextBlock).Text(LOCTEXT("Maximum", "Maximum"))
						]
					]
					+ SUniformGridPanel::Slot(3, 1)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleRunSweep)
						[
							SNew(STextBlock).Text(LOCTEXT("RunSweep", "Run Sweep"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("ForwardN1ReviewSection", "Forward N1 Pose Review"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("ShoulderReviewTooltip", "Preview both calibrated shoulder outputs and automatic recovery."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleForwardN1ShoulderReview)
						[
							SNew(STextBlock).Text(LOCTEXT("ShoulderReview", "Shoulders"))
						]
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("RelaxedReviewTooltip", "Raise the right hand and preview the calibrated Relaxed finger pose."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleForwardN1RelaxedReview)
						[
							SNew(STextBlock).Text(LOCTEXT("RelaxedReview", "Relaxed Hand"))
						]
					]
					+ SUniformGridPanel::Slot(2, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("CurlReviewTooltip", "Raise the right hand and preview the calibrated Curl finger pose."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleForwardN1CurlReview)
						[
							SNew(STextBlock).Text(LOCTEXT("CurlReview", "Curl Hand"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 12.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return StatusText; })
					.ColorAndOpacity(this, &SLLMNPCMotionTestConsole::GetStatusColor)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("OnlineGateSection", "Strict Online PIE Gate"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("OnlineInput", "Natural Language"),
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(OnlineInput); })
						.OnTextChanged_Lambda([this](const FText& Value)
						{
							OnlineInput = Value.ToString();
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 6.0f, 0.0f, 4.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleRunOnlineEvaluation)
						[
							SNew(STextBlock).Text(LOCTEXT("RunOnlineEvaluation", "Run Online Selection"))
						]
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleCancelOnlineEvaluation)
						[
							SNew(STextBlock).Text(LOCTEXT("CancelOnlineEvaluation", "Cancel Online"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 5.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SLLMNPCMotionTestConsole::GetOnlineTraceText)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("MonoFont"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("DebugSection", "Runtime Debug State"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SLLMNPCMotionTestConsole::GetDebugStateText)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("MonoFont"))
				]
			]
		]
	];

	RefreshRuntimeTargets(false);
}

SLLMNPCMotionTestConsole::~SLLMNPCMotionTestConsole()
{
	if (ULLMNPCDialogueComponent* Dialogue = ActiveOnlineDialogue.Get())
	{
		if (bOnlineRunInFlight)
		{
			Dialogue->CancelActiveRequest();
		}
	}
	RestoreOnlineDialogueSettings();
}

void SLLMNPCMotionTestConsole::Tick(
	const FGeometry& AllottedGeometry,
	const double InCurrentTime,
	const float InDeltaTime
)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RuntimeRefreshAccumulator += InDeltaTime;
	if (RuntimeRefreshAccumulator >= 1.0f)
	{
		RuntimeRefreshAccumulator = 0.0f;
		if (!GetSelectedMotionComponent() && GEditor && GEditor->PlayWorld)
		{
			RefreshRuntimeTargets(false);
		}
		else if ((!GEditor || !GEditor->PlayWorld) && !ActorOptions.IsEmpty())
		{
			RefreshRuntimeTargets(false);
		}
	}

	PollOnlineEvaluation();
	if (!bSweepRunning || InCurrentTime < SweepNextActionTime)
	{
		return;
	}

	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion)
	{
		bSweepRunning = false;
		SetStatus(LOCTEXT("SweepTargetLost", "Sweep stopped because the PIE NPC is no longer available"), true);
		return;
	}

	const FLLMNPCMotionDebugState Debug = Motion->GetDebugState();
	if (Debug.bHasActivePlan || Debug.QueueCount > 0)
	{
		return;
	}

	if (SweepStepIndex >= 2)
	{
		bSweepRunning = false;
		SetStatus(LOCTEXT("SweepComplete", "Minimum / default / maximum sweep completed"), false);
		return;
	}
	++SweepStepIndex;
	StartSweepStep();
}

TSharedRef<SWidget> SLLMNPCMotionTestConsole::MakeSectionHeader(const FText& Text) const
{
	return SNew(STextBlock).Text(Text).TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle");
}

TSharedRef<SWidget> SLLMNPCMotionTestConsole::MakeFormRow(
	const FText& Label,
	const TSharedRef<SWidget>& Control
) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(180.0f)
			[
				SNew(STextBlock).Text(Label)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(8.0f, 3.0f)
		[
			SNew(SBox).MaxDesiredWidth(700.0f)[Control]
		];
}

TSharedRef<SWidget> SLLMNPCMotionTestConsole::GenerateActorOption(
	TSharedPtr<FLLMNPCTestActorOption> Option
) const
{
	return SNew(STextBlock).Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

TSharedRef<SWidget> SLLMNPCMotionTestConsole::GenerateTemplateOption(
	TSharedPtr<FLLMNPCTestTemplateOption> Option
) const
{
	return SNew(STextBlock).Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

FText SLLMNPCMotionTestConsole::GetSelectedActorText() const
{
	return SelectedActor.IsValid()
		? SelectedActor->Label
		: LOCTEXT("NoPIENPC", "No PIE NPC");
}

FText SLLMNPCMotionTestConsole::GetSelectedTemplateText() const
{
	return SelectedTemplate.IsValid()
		? SelectedTemplate->Label
		: LOCTEXT("NoPublishedTemplate", "No compatible Published template");
}

FText SLLMNPCMotionTestConsole::GetTemplatePolicyText() const
{
	if (!SelectedTemplate.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(
		TEXT("Amplitude %.2f-%.2f  Speed %.2f-%.2f  Duration %.2f-%.2f  Mirror %s  Target %s"),
		SelectedTemplate->AmplitudeRange.X,
		SelectedTemplate->AmplitudeRange.Y,
		SelectedTemplate->SpeedRange.X,
		SelectedTemplate->SpeedRange.Y,
		SelectedTemplate->DurationRange.X,
		SelectedTemplate->DurationRange.Y,
		SelectedTemplate->bAllowMirror ? TEXT("allowed") : TEXT("forbidden"),
		SelectedTemplate->bRequiresTarget ? TEXT("required") : TEXT("optional")
	));
}

FText SLLMNPCMotionTestConsole::GetDebugStateText() const
{
	const ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion)
	{
		return LOCTEXT("DebugUnavailable", "PIE runtime state unavailable");
	}

	const FLLMNPCMotionDebugState Debug = Motion->GetDebugState();
	const FString Validation = Debug.LastValidationError.IsEmpty()
		? (Debug.bLastSubmissionAccepted ? TEXT("accepted") : TEXT("idle"))
		: Debug.LastValidationError;
	return FText::FromString(FString::Printf(
		TEXT("Requested: %s\nResolved: %s\nActive source: %s\n")
		TEXT("Requested modifiers: amplitude %.3f, speed %.3f, duration %.3f, style %s, mirror %s, seed %d\n")
		TEXT("Resolved modifiers: amplitude %.3f, speed %.3f, duration %.3f, style %s, mirror %s, seed %d\n")
		TEXT("Target: %s\nChannels: %s\nValidator: %s (%s)\n")
		TEXT("Clip: %s  Time: %.3f / %.3f  Queue: %d  Active plans: %d\n")
		TEXT("Animation: %s  Template: %s  Slot: %s  Play rate: %.3f\n")
		TEXT("Pose alpha: %.3f  LOD: %s  Post Process: %s"),
		*Debug.LastRequestedTemplateId.ToString(),
		*Debug.LastResolvedTemplateId.ToString(),
		*Debug.ActiveSourceTemplateId.ToString(),
		Debug.RequestedAmplitude,
		Debug.RequestedSpeedScale,
		Debug.RequestedDurationScale,
		*Debug.RequestedStyle.ToString(),
		Debug.bRequestedMirror ? TEXT("true") : TEXT("false"),
		Debug.RequestedRandomSeed,
		Debug.ResolvedAmplitude,
		Debug.ResolvedSpeedScale,
		Debug.ResolvedDurationScale,
		*Debug.ResolvedStyle.ToString(),
		Debug.bResolvedMirror ? TEXT("true") : TEXT("false"),
		Debug.ResolvedRandomSeed,
		Debug.LastTargetRef.IsEmpty() ? TEXT("none") : *Debug.LastTargetRef,
		Debug.ActiveChannels.IsEmpty() ? TEXT("none") : *JoinNames(Debug.ActiveChannels),
		*Validation,
		Debug.LastValidationSource.IsEmpty() ? TEXT("none") : *Debug.LastValidationSource,
		Debug.ActiveClipId.IsEmpty() ? TEXT("none") : *Debug.ActiveClipId,
		Debug.ActiveTime,
		Debug.ActiveDuration,
		Debug.QueueCount,
		Debug.ActivePlanCount,
		Debug.AnimationPlaybackState.IsEmpty() ? TEXT("none") : *Debug.AnimationPlaybackState,
		*Debug.ActiveAnimationTemplateId.ToString(),
		*Debug.ActiveAnimationSlot.ToString(),
		Debug.ActiveAnimationPlayRate,
		Debug.Snapshot.GlobalAlpha,
		Debug.MotionLODLevel.IsEmpty() ? TEXT("unknown") : *Debug.MotionLODLevel,
		Debug.bPostProcessInstalled ? TEXT("installed") : TEXT("not installed")
	));
}

FText SLLMNPCMotionTestConsole::GetOnlineTraceText() const
{
	const FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	const ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent();
	if (!Dialogue)
	{
		return FText::FromString(FString::Printf(
			TEXT("Config: %s  Connection: %s\nDialogue component unavailable on selected PIE NPC"),
			OnlineConfig.IsLoaded() ? TEXT("loaded") : TEXT("not loaded"),
			OnlineConfig.HasPassingConnectionForCurrentConfig()
				? TEXT("verified")
				: TEXT("not verified")
		));
	}

	const FLLMNPCDialogueDebugState Debug = Dialogue->GetDebugState();
	const FGuid RequestId = Debug.ActiveRequestId.IsValid()
		? Debug.ActiveRequestId
		: Debug.LastRequestId;
	return FText::FromString(FString::Printf(
		TEXT("Config: %s  Connection: %s  Run: %s\n")
		TEXT("Provider: %s  Model: %s  Request: %s\n")
		TEXT("Context: %s\nCandidates: %d -> %d (%d excluded)\n")
		TEXT("Selected: %s  Resolved: %s  Fallback: %s  Error: %s"),
		OnlineConfig.IsLoaded() ? TEXT("loaded") : TEXT("not loaded"),
		OnlineConfig.HasPassingConnectionForCurrentConfig()
			? TEXT("verified")
			: TEXT("not verified"),
		bOnlineRunInFlight ? TEXT("in flight") : TEXT("idle"),
		*Debug.ProviderId.ToString(),
		Debug.ProviderModelId.IsEmpty() ? TEXT("pending") : *Debug.ProviderModelId,
		RequestId.IsValid()
			? *RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
			: TEXT("none"),
		*Debug.ContextSummary,
		Debug.SourceCandidateCount,
		Debug.OfferedCandidateCount,
		Debug.ExcludedCandidateCount,
		*Debug.LastSelectedActionId.ToString(),
		*Debug.LastResolvedTemplateId.ToString(),
		Debug.bUsedLocalFallback ? TEXT("yes") : TEXT("no"),
		Debug.LastErrorCode.IsNone() ? TEXT("none") : *Debug.LastErrorCode.ToString()
	));
}

FSlateColor SLLMNPCMotionTestConsole::GetStatusColor() const
{
	return bStatusError
		? FSlateColor(FLinearColor(0.95f, 0.28f, 0.24f))
		: FSlateColor(FLinearColor(0.25f, 0.78f, 0.42f));
}

ECheckBoxState SLLMNPCMotionTestConsole::GetOverlayCheckState() const
{
	const ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	return Motion && Motion->IsRuntimeDebugOverlayEnabled()
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SLLMNPCMotionTestConsole::RefreshRuntimeTargets(bool bRefreshLibrary)
{
	const FString PreviousActorName =
		GetSelectedMotionComponent() && GetSelectedMotionComponent()->GetOwner()
			? GetSelectedMotionComponent()->GetOwner()->GetName()
			: FString();
	ActorOptions.Reset();
	SelectedActor.Reset();

	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
	if (PlayWorld)
	{
		for (TObjectIterator<ULLMNPCMotionComponent> Iterator; Iterator; ++Iterator)
		{
			ULLMNPCMotionComponent* Motion = *Iterator;
			if (
				!IsValid(Motion) ||
				Motion->HasAnyFlags(RF_ClassDefaultObject) ||
				Motion->GetWorld() != PlayWorld ||
				!Motion->GetOwner()
			)
			{
				continue;
			}

			TSharedPtr<FLLMNPCTestActorOption> Option = MakeShared<FLLMNPCTestActorOption>();
			Option->MotionComponent = Motion;
			Option->Label = FText::FromString(Motion->GetOwner()->GetName());
			ActorOptions.Add(MoveTemp(Option));
		}
	}

	ActorOptions.Sort(
		[](const TSharedPtr<FLLMNPCTestActorOption>& A, const TSharedPtr<FLLMNPCTestActorOption>& B)
		{
			return A->Label.ToString() < B->Label.ToString();
		}
	);
	for (const TSharedPtr<FLLMNPCTestActorOption>& Option : ActorOptions)
	{
		if (
			Option->MotionComponent.IsValid() &&
			Option->MotionComponent->GetOwner() &&
			Option->MotionComponent->GetOwner()->GetName() == PreviousActorName
		)
		{
			SelectedActor = Option;
			break;
		}
	}
	if (!SelectedActor.IsValid() && !ActorOptions.IsEmpty())
	{
		SelectedActor = ActorOptions[0];
	}
	if (ActorCombo)
	{
		ActorCombo->RefreshOptions();
		ActorCombo->SetSelectedItem(SelectedActor);
	}

	RefreshTemplates(bRefreshLibrary);
	if (SelectedActor.IsValid())
	{
		SetStatus(
			FText::Format(
				LOCTEXT("PIETargetReady", "PIE target ready: {0}"),
				SelectedActor->Label
			),
			false
		);
	}
	else
	{
		SetStatus(LOCTEXT("NoPIETarget", "Start PIE in M_LLMNPC_Test, then refresh"), true);
	}
}

void SLLMNPCMotionTestConsole::RefreshTemplates(bool bRefreshLibrary)
{
	const FName PreviousTemplateId =
		SelectedTemplate.IsValid() ? SelectedTemplate->TemplateId : NAME_None;
	TemplateOptions.Reset();
	SelectedTemplate.Reset();

	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	ULLMNPCTemplateLibrarySubsystem* Library = GetTemplateLibrary();
	ULLMNPCSkeletonProfile* Profile = Motion ? Motion->SkeletonProfile.LoadSynchronous() : nullptr;
	if (Motion && Library && Profile)
	{
		if (bRefreshLibrary)
		{
			Library->RefreshLibrary();
		}

		TArray<FName> TemplateIds;
		Library->GetPublishedTemplateIdsForProfile(Profile->ProfileId, TemplateIds);
		for (const FName TemplateId : TemplateIds)
		{
			const ULLMNPCMotionTemplate* MotionTemplate =
				Library->FindPublishedTemplate(TemplateId);
			if (!MotionTemplate)
			{
				continue;
			}

			TSharedPtr<FLLMNPCTestTemplateOption> Option =
				MakeShared<FLLMNPCTestTemplateOption>();
			Option->TemplateId = TemplateId;
			Option->Label = FText::FromString(FString::Printf(
				TEXT("%s  [%s]"),
				MotionTemplate->Metadata.DisplayName.IsEmpty()
					? *TemplateId.ToString()
					: *MotionTemplate->Metadata.DisplayName.ToString(),
				*TemplateId.ToString()
			));
			Option->AmplitudeRange = MotionTemplate->ModifierPolicy.AmplitudeRange;
			Option->SpeedRange = MotionTemplate->ModifierPolicy.SpeedRange;
			Option->DurationRange = MotionTemplate->ModifierPolicy.DurationRange;
			Option->AllowedStyles = MotionTemplate->ModifierPolicy.AllowedStyleTags;
			Option->RequiredChannels = MotionTemplate->Metadata.RequiredChannels;
			Option->CooldownSeconds = MotionTemplate->Metadata.CooldownSeconds;
			Option->bAllowMirror = MotionTemplate->ModifierPolicy.bAllowMirror;
			Option->bRequiresTarget = MotionTemplate->Metadata.bRequiresTarget;
			TemplateOptions.Add(MoveTemp(Option));
		}
	}

	for (const TSharedPtr<FLLMNPCTestTemplateOption>& Option : TemplateOptions)
	{
		if (Option->TemplateId == PreviousTemplateId)
		{
			SelectedTemplate = Option;
			break;
		}
	}
	if (!SelectedTemplate.IsValid() && !TemplateOptions.IsEmpty())
	{
		SelectedTemplate = TemplateOptions[0];
	}
	if (TemplateCombo)
	{
		TemplateCombo->RefreshOptions();
		TemplateCombo->SetSelectedItem(SelectedTemplate);
	}
	ApplyPreset(ELLMNPCTestParameterPreset::Default);
}

ULLMNPCMotionComponent* SLLMNPCMotionTestConsole::GetSelectedMotionComponent() const
{
	return SelectedActor.IsValid() ? SelectedActor->MotionComponent.Get() : nullptr;
}

ULLMNPCDialogueComponent* SLLMNPCMotionTestConsole::GetSelectedDialogueComponent() const
{
	const ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	AActor* Owner = Motion ? Motion->GetOwner() : nullptr;
	return Owner ? Owner->FindComponentByClass<ULLMNPCDialogueComponent>() : nullptr;
}

ULLMNPCTemplateLibrarySubsystem* SLLMNPCMotionTestConsole::GetTemplateLibrary() const
{
	const ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	UGameInstance* GameInstance =
		Motion && Motion->GetWorld() ? Motion->GetWorld()->GetGameInstance() : nullptr;
	return GameInstance
		? GameInstance->GetSubsystem<ULLMNPCTemplateLibrarySubsystem>()
		: nullptr;
}

const ULLMNPCMotionTemplate* SLLMNPCMotionTestConsole::GetSelectedMotionTemplate() const
{
	ULLMNPCTemplateLibrarySubsystem* Library = GetTemplateLibrary();
	return Library && SelectedTemplate.IsValid()
		? Library->FindPublishedTemplate(SelectedTemplate->TemplateId)
		: nullptr;
}

bool SLLMNPCMotionTestConsole::ApplyPreset(ELLMNPCTestParameterPreset Preset)
{
	if (!SelectedTemplate.IsValid())
	{
		return false;
	}

	auto Resolve = [Preset](const FVector2D& Range)
	{
		switch (Preset)
		{
		case ELLMNPCTestParameterPreset::Minimum:
			return static_cast<float>(Range.X);
		case ELLMNPCTestParameterPreset::Maximum:
			return static_cast<float>(Range.Y);
		case ELLMNPCTestParameterPreset::Default:
		default:
			return FMath::Clamp(1.0f, static_cast<float>(Range.X), static_cast<float>(Range.Y));
		}
	};
	Amplitude = Resolve(SelectedTemplate->AmplitudeRange);
	SpeedScale = Resolve(SelectedTemplate->SpeedRange);
	DurationScale = Resolve(SelectedTemplate->DurationRange);
	bMirror = false;
	RandomSeed = 0;
	if (SelectedTemplate->AllowedStyles.Contains(TEXT("neutral")))
	{
		Style = TEXT("neutral");
	}
	else if (!SelectedTemplate->AllowedStyles.IsEmpty())
	{
		Style = SelectedTemplate->AllowedStyles[0];
	}
	else
	{
		Style = TEXT("neutral");
	}
	return true;
}

bool SLLMNPCMotionTestConsole::ExecuteCurrent(const FString& PresetLabel)
{
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion || !SelectedTemplate.IsValid())
	{
		SetStatus(LOCTEXT("ExecuteUnavailable", "PIE NPC or Published template is unavailable"), true);
		return false;
	}

	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.TargetRef = TargetRef.TrimStartAndEnd();
	Modifiers.Amplitude = Amplitude;
	Modifiers.SpeedScale = SpeedScale;
	Modifiers.DurationScale = DurationScale;
	Modifiers.Style = Style;
	Modifiers.bMirror = bMirror;
	Modifiers.RandomSeed = RandomSeed;
	if (!Modifiers.TargetRef.IsEmpty())
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(Motion->GetWorld(), 0))
		{
			Motion->RegisterTarget(Modifiers.TargetRef, PlayerPawn);
		}
	}

	const bool bAccepted = Motion->SubmitPublishedTemplate(
		SelectedTemplate->TemplateId,
		Modifiers
	);
	const FLLMNPCMotionDebugState Debug = Motion->GetDebugState();

	FLLMNPCTestExecutionRecord& Record = ExecutionRecords.AddDefaulted_GetRef();
	Record.TimestampUtc = FDateTime::UtcNow();
	Record.ActorName = Motion->GetOwner() ? Motion->GetOwner()->GetName() : FString();
	Record.RequestedTemplateId = SelectedTemplate->TemplateId;
	Record.ResolvedTemplateId = Debug.LastResolvedTemplateId;
	Record.Preset = PresetLabel;
	Record.RequestedModifiers = Modifiers;
	Record.ResolvedAmplitude = Debug.ResolvedAmplitude;
	Record.ResolvedSpeedScale = Debug.ResolvedSpeedScale;
	Record.ResolvedDurationScale = Debug.ResolvedDurationScale;
	Record.bAccepted = bAccepted;
	Record.ValidationError = Debug.LastValidationError;
	Record.ActiveChannels = Debug.ActiveChannels.IsEmpty()
		? SelectedTemplate->RequiredChannels
		: Debug.ActiveChannels;

	SetStatus(
		bAccepted
			? FText::Format(
				LOCTEXT("TemplateAccepted", "Accepted {0} ({1})"),
				FText::FromName(SelectedTemplate->TemplateId),
				FText::FromString(PresetLabel)
			)
			: FText::Format(
				LOCTEXT("TemplateRejected", "Rejected {0}: {1}"),
				FText::FromName(SelectedTemplate->TemplateId),
				FText::FromString(Debug.LastValidationError)
			),
		!bAccepted
	);
	return bAccepted;
}

void SLLMNPCMotionTestConsole::StartSweepStep()
{
	static const ELLMNPCTestParameterPreset Steps[] = {
		ELLMNPCTestParameterPreset::Minimum,
		ELLMNPCTestParameterPreset::Default,
		ELLMNPCTestParameterPreset::Maximum
	};
	if (!bSweepRunning || SweepStepIndex < 0 || SweepStepIndex >= UE_ARRAY_COUNT(Steps))
	{
		return;
	}

	const ELLMNPCTestParameterPreset Preset = Steps[SweepStepIndex];
	ApplyPreset(Preset);
	if (!ExecuteCurrent(PresetName(Preset)))
	{
		bSweepRunning = false;
		return;
	}
	SweepNextActionTime = FPlatformTime::Seconds() +
		FMath::Max(SelectedTemplate->CooldownSeconds, 0.25f);
}

void SLLMNPCMotionTestConsole::PollOnlineEvaluation()
{
	if (!bOnlineRunInFlight)
	{
		return;
	}

	ULLMNPCDialogueComponent* Dialogue = ActiveOnlineDialogue.Get();
	if (!Dialogue)
	{
		bOnlineRunInFlight = false;
		bRestoreOnlineDialogueSettings = false;
		ActiveOnlineRequestId.Invalidate();
		SetStatus(
			LOCTEXT("OnlineTargetLost", "Online evaluation stopped because the PIE NPC was destroyed"),
			true
		);
		return;
	}
	if (Dialogue->IsRequestInFlight())
	{
		return;
	}

	const FLLMNPCDialogueTurnResult& Turn = Dialogue->LastTurnResult;
	if (
		ActiveOnlineRequestId.IsValid() &&
		Turn.RequestId != ActiveOnlineRequestId
	)
	{
		return;
	}
	CompleteOnlineEvaluation(*Dialogue);
}

void SLLMNPCMotionTestConsole::CompleteOnlineEvaluation(
	ULLMNPCDialogueComponent& Dialogue
)
{
	const FLLMNPCDialogueTurnResult& Turn = Dialogue.LastTurnResult;
	const FLLMNPCDialogueDebugState DialogueDebug = Dialogue.GetDebugState();
	const FLLMNPCMotionDebugState MotionDebug = GetSelectedMotionComponent()
		? GetSelectedMotionComponent()->GetDebugState()
		: FLLMNPCMotionDebugState();

	FLLMNPCOnlineEvaluationRecord& Record =
		OnlineEvaluationRecords.AddDefaulted_GetRef();
	Record.TimestampUtc = FDateTime::UtcNow();
	Record.RequestId = Turn.RequestId;
	Record.ActorName = Dialogue.GetOwner()
		? Dialogue.GetOwner()->GetName()
		: FString();
	Record.InputHash = ActiveOnlineInputHash;
	Record.ProviderId = Turn.ProviderId;
	Record.ProviderModelId = Turn.ProviderModelId;
	Record.ConfigHash = ActiveOnlineConfigHash;
	Record.bUsedLocalFallback = Turn.bUsedLocalFallback;
	Record.SourceCandidateCount = DialogueDebug.SourceCandidateCount;
	Record.OfferedCandidateCount = DialogueDebug.OfferedCandidateCount;
	Record.ExcludedCandidateCount = DialogueDebug.ExcludedCandidateCount;
	for (const FLLMNPCTemplateCandidate& Candidate : Dialogue.GetLastOfferedCandidates())
	{
		Record.OfferedCandidateIds.Add(Candidate.SelectionId);
	}
	Record.SelectedActionId = Turn.SelectedActionId;
	Record.ResolvedTemplateId = Turn.ResolvedTemplateId;
	Record.bActionExecuted = Turn.bActionExecuted;
	Record.bBehaviorStarted = Turn.bBehaviorStarted;
	Record.ErrorCode = Turn.ErrorCode;
	Record.TotalLatencySeconds = Turn.TotalLatencySeconds >= 0.0f
		? Turn.TotalLatencySeconds
		: static_cast<float>(
			FMath::Max(FPlatformTime::Seconds() - OnlineRequestStartedAt, 0.0)
		);
	Record.AttemptCount = Turn.AttemptCount;
	Record.PromptTokens = Turn.PromptTokens;
	Record.CompletionTokens = Turn.CompletionTokens;
	Record.TotalTokens = Turn.TotalTokens;
	Record.TargetRef = MotionDebug.LastTargetRef;
	Record.RequestedAmplitude = MotionDebug.RequestedAmplitude;
	Record.RequestedSpeedScale = MotionDebug.RequestedSpeedScale;
	Record.RequestedDurationScale = MotionDebug.RequestedDurationScale;
	Record.RequestedStyle = MotionDebug.RequestedStyle;
	Record.bRequestedMirror = MotionDebug.bRequestedMirror;
	Record.ResolvedAmplitude = MotionDebug.ResolvedAmplitude;
	Record.ResolvedSpeedScale = MotionDebug.ResolvedSpeedScale;
	Record.ResolvedDurationScale = MotionDebug.ResolvedDurationScale;
	Record.ResolvedStyle = MotionDebug.ResolvedStyle;
	Record.bResolvedMirror = MotionDebug.bResolvedMirror;
	Record.bModifiersClamped = MotionDebug.bModifiersClamped;
	Record.ModifierResolutionTrace = MotionDebug.ModifierResolutionTrace;
	Record.ValidatorResult = MotionDebug.LastValidationError.IsEmpty()
		? (MotionDebug.bLastSubmissionAccepted ? TEXT("accepted") : TEXT("not_run"))
		: MotionDebug.LastValidationError;
	Record.bStrictProviderIdentity =
		Record.ProviderId == FName(TEXT("deepseek_direct_editor")) &&
		Record.ProviderModelId == ActiveOnlineExpectedModel &&
		!Record.bUsedLocalFallback &&
		FLLMNPCOnlineTestConfigLoader::GetState().HasPassingConnectionForCurrentConfig() &&
		FLLMNPCOnlineTestConfigLoader::GetState().NonSecretConfigHash ==
			ActiveOnlineConfigHash;
	Record.bPassed =
		Record.bStrictProviderIdentity &&
		Record.ErrorCode.IsNone() &&
		!Record.SelectedActionId.IsNone() &&
		!Record.ResolvedTemplateId.IsNone() &&
		(Record.bActionExecuted || Record.bBehaviorStarted);

	bOnlineRunInFlight = false;
	ActiveOnlineRequestId.Invalidate();
	SetStatus(
		Record.bPassed
			? FText::Format(
				LOCTEXT(
					"OnlineEvaluationPassed",
					"Strict online selection passed: {0} -> {1}"
				),
				FText::FromName(Record.SelectedActionId),
				FText::FromName(Record.ResolvedTemplateId)
			)
			: FText::Format(
				LOCTEXT(
					"OnlineEvaluationFailed",
					"Strict online selection did not pass: provider={0}, model={1}, error={2}"
				),
				FText::FromName(Record.ProviderId),
				FText::FromString(Record.ProviderModelId),
				FText::FromName(
					Record.ErrorCode.IsNone()
						? FName(TEXT("LLMNPC_ONLINE_ACTION_NOT_EXECUTED"))
						: Record.ErrorCode
				)
			),
		!Record.bPassed
	);
	RestoreOnlineDialogueSettings();
}

void SLLMNPCMotionTestConsole::RestoreOnlineDialogueSettings()
{
	if (!bRestoreOnlineDialogueSettings)
	{
		ActiveOnlineDialogue.Reset();
		return;
	}

	if (ULLMNPCDialogueComponent* Dialogue = ActiveOnlineDialogue.Get())
	{
		Dialogue->bEnableLocalCommandFallback = bPreviousLocalFallback;
		Dialogue->SetProviderKind(PreviousProviderKind);
		if (!PreviousProviderIdOverride.IsNone())
		{
			Dialogue->SetProviderId(PreviousProviderIdOverride);
		}
	}
	bRestoreOnlineDialogueSettings = false;
	ActiveOnlineDialogue.Reset();
}

void SLLMNPCMotionTestConsole::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusError = bError;
}

void SLLMNPCMotionTestConsole::HandleActorChanged(
	TSharedPtr<FLLMNPCTestActorOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	if (Option.IsValid())
	{
		if (bOnlineRunInFlight)
		{
			HandleCancelOnlineEvaluation();
		}
		SelectedActor = MoveTemp(Option);
		RefreshTemplates(false);
	}
}

void SLLMNPCMotionTestConsole::HandleTemplateChanged(
	TSharedPtr<FLLMNPCTestTemplateOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	if (Option.IsValid())
	{
		SelectedTemplate = MoveTemp(Option);
		ApplyPreset(ELLMNPCTestParameterPreset::Default);
	}
}

void SLLMNPCMotionTestConsole::HandleOverlayChanged(ECheckBoxState State)
{
	if (ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
	{
		Motion->SetRuntimeDebugOverlayEnabled(State == ECheckBoxState::Checked);
	}
}

FReply SLLMNPCMotionTestConsole::HandleRefresh()
{
	if (bOnlineRunInFlight)
	{
		HandleCancelOnlineEvaluation();
	}
	RefreshRuntimeTargets(true);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleExecute()
{
	bSweepRunning = false;
	ExecuteCurrent(TEXT("custom"));
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleStop()
{
	bSweepRunning = false;
	if (ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
	{
		Motion->StopAllMotions();
		SetStatus(LOCTEXT("MotionStopped", "All queued and active test motions stopped"), false);
	}
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleMinimum()
{
	bSweepRunning = false;
	if (ApplyPreset(ELLMNPCTestParameterPreset::Minimum))
	{
		ExecuteCurrent(TEXT("minimum"));
	}
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleDefault()
{
	bSweepRunning = false;
	if (ApplyPreset(ELLMNPCTestParameterPreset::Default))
	{
		ExecuteCurrent(TEXT("default"));
	}
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleMaximum()
{
	bSweepRunning = false;
	if (ApplyPreset(ELLMNPCTestParameterPreset::Maximum))
	{
		ExecuteCurrent(TEXT("maximum"));
	}
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleRunSweep()
{
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion || !SelectedTemplate.IsValid())
	{
		SetStatus(LOCTEXT("SweepUnavailable", "PIE NPC or Published template is unavailable"), true);
		return FReply::Handled();
	}

	Motion->ResetMotionTestState();
	bSweepRunning = true;
	SweepStepIndex = 0;
	StartSweepStep();
	return FReply::Handled();
}

void SLLMNPCMotionTestConsole::ExecuteForwardN1ReviewSample(
	ELLMNPCMotionDebugSample Sample,
	const FText& Label
)
{
	bSweepRunning = false;
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion)
	{
		SetStatus(
			LOCTEXT("ForwardN1ReviewUnavailable", "Start PIE and select a Manny NPC before running the pose review"),
			true
		);
		return;
	}

	Motion->StopAllMotions();
	const bool bAccepted = Motion->SubmitSampleMotionPlanJson(
		Sample,
		nullptr
	);
	const FLLMNPCMotionDebugState Debug = Motion->GetDebugState();
	SetStatus(
		bAccepted
			? FText::Format(
				LOCTEXT("ForwardN1ReviewStarted", "Forward N1 review started: {0}"),
				Label
			)
			: FText::Format(
				LOCTEXT("ForwardN1ReviewRejected", "Forward N1 review rejected: {0}"),
				FText::FromString(Debug.LastValidationError)
			),
		!bAccepted
	);
}

FReply SLLMNPCMotionTestConsole::HandleForwardN1ShoulderReview()
{
	ExecuteForwardN1ReviewSample(
		ELLMNPCMotionDebugSample::ForwardN1ShoulderShrug,
		LOCTEXT("ForwardN1ShoulderLabel", "Shoulders")
	);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleForwardN1RelaxedReview()
{
	ExecuteForwardN1ReviewSample(
		ELLMNPCMotionDebugSample::ForwardN1HandRelaxed,
		LOCTEXT("ForwardN1RelaxedLabel", "Relaxed Hand")
	);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleForwardN1CurlReview()
{
	ExecuteForwardN1ReviewSample(
		ELLMNPCMotionDebugSample::ForwardN1HandCurl,
		LOCTEXT("ForwardN1CurlLabel", "Curl Hand")
	);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleRunOnlineEvaluation()
{
	if (bOnlineRunInFlight)
	{
		SetStatus(LOCTEXT("OnlineAlreadyRunning", "An online evaluation is already in flight"), true);
		return FReply::Handled();
	}

	ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent();
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Dialogue || !Motion)
	{
		SetStatus(
			LOCTEXT(
				"OnlineDialogueUnavailable",
				"The selected PIE NPC needs both Dialogue and Motion components"
			),
			true
		);
		return FReply::Handled();
	}

	const FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	if (!OnlineConfig.HasPassingConnectionForCurrentConfig())
	{
		SetStatus(
			LOCTEXT(
				"OnlineConnectionGateMissing",
				"Load env.txt and pass the strict Test Connection gate first"
			),
			true
		);
		return FReply::Handled();
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || !Settings->bAllowDirectProviderCallInEditorOnly)
	{
		SetStatus(
			LOCTEXT(
				"OnlineDirectDisabled",
				"Direct Editor provider access is disabled in LLM NPC Provider settings"
			),
			true
		);
		return FReply::Handled();
	}

	const FString CleanInput = OnlineInput.TrimStartAndEnd();
	if (CleanInput.IsEmpty())
	{
		SetStatus(LOCTEXT("OnlineInputEmpty", "Natural-language input cannot be empty"), true);
		return FReply::Handled();
	}

	bSweepRunning = false;
	Motion->ResetMotionTestState();
	Dialogue->ResetConversation();
	PreviousProviderKind = Dialogue->ProviderKind;
	PreviousProviderIdOverride = Dialogue->ProviderIdOverride;
	bPreviousLocalFallback = Dialogue->bEnableLocalCommandFallback;
	ActiveOnlineDialogue = Dialogue;
	bRestoreOnlineDialogueSettings = true;
	Dialogue->SetProviderKind(ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly);
	Dialogue->bEnableLocalCommandFallback = false;
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(Motion->GetWorld(), 0))
	{
		Dialogue->RegisterTarget(TEXT("player"), PlayerPawn);
	}

	ActiveOnlineInputHash = HashUtf8Text(CleanInput);
	ActiveOnlineConfigHash = OnlineConfig.NonSecretConfigHash;
	ActiveOnlineExpectedModel = OnlineConfig.Model;
	OnlineRequestStartedAt = FPlatformTime::Seconds();
	bOnlineRunInFlight = true;
	if (!Dialogue->SendPlayerMessage(CleanInput))
	{
		bOnlineRunInFlight = false;
		RestoreOnlineDialogueSettings();
		SetStatus(
			LOCTEXT("OnlineRequestRejected", "The Dialogue component rejected the online request"),
			true
		);
		return FReply::Handled();
	}

	const FLLMNPCDialogueDebugState Debug = Dialogue->GetDebugState();
	ActiveOnlineRequestId = Debug.ActiveRequestId.IsValid()
		? Debug.ActiveRequestId
		: Debug.LastRequestId;
	SetStatus(
		FText::Format(
			LOCTEXT("OnlineRequestStarted", "Strict online request started: {0}"),
			FText::FromString(
				ActiveOnlineRequestId.IsValid()
					? ActiveOnlineRequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
					: FString(TEXT("pending"))
			)
		),
		false
	);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleCancelOnlineEvaluation()
{
	if (ULLMNPCDialogueComponent* Dialogue = ActiveOnlineDialogue.Get())
	{
		if (bOnlineRunInFlight)
		{
			Dialogue->CancelActiveRequest();
		}
	}
	bOnlineRunInFlight = false;
	ActiveOnlineRequestId.Invalidate();
	RestoreOnlineDialogueSettings();
	SetStatus(LOCTEXT("OnlineEvaluationCancelled", "Online evaluation cancelled"), false);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleSaveReport()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.forward_n0_motion_test_report.v1"));
	Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("human_review"), TEXT("pending"));

	const FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	TSharedRef<FJsonObject> Online = MakeShared<FJsonObject>();
	Online->SetStringField(
		TEXT("status"),
		OnlineConfig.IsLoaded() ? TEXT("loaded") : TEXT("not_loaded")
	);
	Online->SetStringField(TEXT("model"), OnlineConfig.Model);
	Online->SetStringField(TEXT("endpoint_origin"), OnlineConfig.EndpointOrigin);
	Online->SetStringField(TEXT("config_hash"), OnlineConfig.NonSecretConfigHash);
	Online->SetBoolField(TEXT("credential_present"), OnlineConfig.bCredentialPresent);
	Online->SetBoolField(
		TEXT("connection_test_passed"),
		OnlineConfig.HasPassingConnectionForCurrentConfig()
	);
	Online->SetStringField(
		TEXT("connection_provider_id"),
		OnlineConfig.ConnectionProviderId.ToString()
	);
	Online->SetStringField(TEXT("connection_model"), OnlineConfig.ConnectionModel);
	Online->SetStringField(
		TEXT("connection_tested_at_utc"),
		OnlineConfig.ConnectionTestedAtUtc.GetTicks() > 0
			? OnlineConfig.ConnectionTestedAtUtc.ToIso8601()
			: FString()
	);
	Online->SetStringField(
		TEXT("connection_error_code"),
		OnlineConfig.ConnectionErrorCode.ToString()
	);
	Online->SetNumberField(
		TEXT("connection_http_status"),
		OnlineConfig.ConnectionHttpStatus
	);
	Online->SetNumberField(
		TEXT("connection_latency_seconds"),
		OnlineConfig.ConnectionLatencySeconds
	);
	Root->SetObjectField(TEXT("online_test_session"), Online);

	if (ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
	{
		Root->SetStringField(
			TEXT("actor"),
			Motion->GetOwner() ? Motion->GetOwner()->GetName() : FString()
		);
		ULLMNPCSkeletonProfile* Profile = Motion->SkeletonProfile.LoadSynchronous();
		Root->SetStringField(
			TEXT("skeleton_profile_id"),
			Profile ? Profile->ProfileId.ToString() : FString()
		);
	}

	TArray<TSharedPtr<FJsonValue>> Events;
	for (const FLLMNPCTestExecutionRecord& Record : ExecutionRecords)
	{
		TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetStringField(TEXT("timestamp_utc"), Record.TimestampUtc.ToIso8601());
		Event->SetStringField(TEXT("actor"), Record.ActorName);
		Event->SetStringField(TEXT("requested_template_id"), Record.RequestedTemplateId.ToString());
		Event->SetStringField(TEXT("resolved_template_id"), Record.ResolvedTemplateId.ToString());
		Event->SetStringField(TEXT("preset"), Record.Preset);
		Event->SetBoolField(TEXT("accepted"), Record.bAccepted);
		Event->SetStringField(TEXT("validation_error"), Record.ValidationError);
		Event->SetStringField(TEXT("target_ref"), Record.RequestedModifiers.TargetRef);
		Event->SetNumberField(TEXT("requested_amplitude"), Record.RequestedModifiers.Amplitude);
		Event->SetNumberField(TEXT("requested_speed_scale"), Record.RequestedModifiers.SpeedScale);
		Event->SetNumberField(TEXT("requested_duration_scale"), Record.RequestedModifiers.DurationScale);
		Event->SetStringField(TEXT("requested_style"), Record.RequestedModifiers.Style.ToString());
		Event->SetBoolField(TEXT("requested_mirror"), Record.RequestedModifiers.bMirror);
		Event->SetNumberField(TEXT("requested_seed"), Record.RequestedModifiers.RandomSeed);
		Event->SetNumberField(TEXT("resolved_amplitude"), Record.ResolvedAmplitude);
		Event->SetNumberField(TEXT("resolved_speed_scale"), Record.ResolvedSpeedScale);
		Event->SetNumberField(TEXT("resolved_duration_scale"), Record.ResolvedDurationScale);
		TArray<TSharedPtr<FJsonValue>> Channels;
		for (const FName Channel : Record.ActiveChannels)
		{
			Channels.Add(MakeShared<FJsonValueString>(Channel.ToString()));
		}
		Event->SetArrayField(TEXT("active_channels"), Channels);
		Events.Add(MakeShared<FJsonValueObject>(Event));
	}
	Root->SetArrayField(TEXT("executions"), Events);

	TArray<TSharedPtr<FJsonValue>> OnlineEvaluations;
	for (const FLLMNPCOnlineEvaluationRecord& Record : OnlineEvaluationRecords)
	{
		TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
		Event->SetStringField(TEXT("timestamp_utc"), Record.TimestampUtc.ToIso8601());
		Event->SetStringField(
			TEXT("request_id"),
			Record.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
		);
		Event->SetStringField(TEXT("actor"), Record.ActorName);
		Event->SetStringField(TEXT("input_hash"), Record.InputHash);
		Event->SetStringField(TEXT("provider_id"), Record.ProviderId.ToString());
		Event->SetStringField(TEXT("provider_model_id"), Record.ProviderModelId);
		Event->SetStringField(TEXT("config_hash"), Record.ConfigHash);
		Event->SetBoolField(
			TEXT("strict_provider_identity"),
			Record.bStrictProviderIdentity
		);
		Event->SetBoolField(TEXT("used_local_fallback"), Record.bUsedLocalFallback);
		Event->SetNumberField(
			TEXT("source_candidate_count"),
			Record.SourceCandidateCount
		);
		Event->SetNumberField(
			TEXT("offered_candidate_count"),
			Record.OfferedCandidateCount
		);
		Event->SetNumberField(
			TEXT("excluded_candidate_count"),
			Record.ExcludedCandidateCount
		);
		TArray<TSharedPtr<FJsonValue>> OfferedIds;
		for (const FName CandidateId : Record.OfferedCandidateIds)
		{
			OfferedIds.Add(MakeShared<FJsonValueString>(CandidateId.ToString()));
		}
		Event->SetArrayField(TEXT("offered_candidate_ids"), OfferedIds);
		Event->SetStringField(
			TEXT("selected_action_id"),
			Record.SelectedActionId.ToString()
		);
		Event->SetStringField(
			TEXT("resolved_template_id"),
			Record.ResolvedTemplateId.ToString()
		);
		Event->SetBoolField(TEXT("action_executed"), Record.bActionExecuted);
		Event->SetBoolField(TEXT("behavior_started"), Record.bBehaviorStarted);
		Event->SetStringField(TEXT("error_code"), Record.ErrorCode.ToString());
		Event->SetNumberField(
			TEXT("total_latency_seconds"),
			Record.TotalLatencySeconds
		);
		Event->SetNumberField(TEXT("attempt_count"), Record.AttemptCount);
		Event->SetNumberField(TEXT("prompt_tokens"), Record.PromptTokens);
		Event->SetNumberField(TEXT("completion_tokens"), Record.CompletionTokens);
		Event->SetNumberField(TEXT("total_tokens"), Record.TotalTokens);
		Event->SetStringField(TEXT("target_ref"), Record.TargetRef);
		Event->SetNumberField(
			TEXT("requested_amplitude"),
			Record.RequestedAmplitude
		);
		Event->SetNumberField(
			TEXT("requested_speed_scale"),
			Record.RequestedSpeedScale
		);
		Event->SetNumberField(
			TEXT("requested_duration_scale"),
			Record.RequestedDurationScale
		);
		Event->SetStringField(
			TEXT("requested_style"),
			Record.RequestedStyle.ToString()
		);
		Event->SetBoolField(TEXT("requested_mirror"), Record.bRequestedMirror);
		Event->SetNumberField(
			TEXT("resolved_amplitude"),
			Record.ResolvedAmplitude
		);
		Event->SetNumberField(
			TEXT("resolved_speed_scale"),
			Record.ResolvedSpeedScale
		);
		Event->SetNumberField(
			TEXT("resolved_duration_scale"),
			Record.ResolvedDurationScale
		);
		Event->SetStringField(
			TEXT("resolved_style"),
			Record.ResolvedStyle.ToString()
		);
		Event->SetBoolField(TEXT("resolved_mirror"), Record.bResolvedMirror);
		Event->SetBoolField(TEXT("modifiers_clamped"), Record.bModifiersClamped);
		Event->SetStringField(
			TEXT("modifier_resolution_trace"),
			Record.ModifierResolutionTrace
		);
		Event->SetStringField(TEXT("validator_result"), Record.ValidatorResult);
		Event->SetBoolField(TEXT("passed"), Record.bPassed);
		Event->SetStringField(TEXT("visual_review"), TEXT("pending"));
		OnlineEvaluations.Add(MakeShared<FJsonValueObject>(Event));
	}
	Root->SetArrayField(TEXT("online_evaluations"), OnlineEvaluations);

	FString Json;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, Json))
	{
		SetStatus(LOCTEXT("ReportSerializeFailed", "Motion test report serialization failed"), true);
		return FReply::Handled();
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/ForwardN0/Reports")
	);
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString ReportPath = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("motion_test_%s.json"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))
		)
	);
	if (!FFileHelper::SaveStringToFile(
		Json,
		*ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		SetStatus(LOCTEXT("ReportWriteFailed", "Motion test report could not be written"), true);
		return FReply::Handled();
	}

	SetStatus(
		FText::Format(
			LOCTEXT("ReportSaved", "Motion test report saved: {0}"),
			FText::FromString(ReportPath)
		),
		false
	);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
