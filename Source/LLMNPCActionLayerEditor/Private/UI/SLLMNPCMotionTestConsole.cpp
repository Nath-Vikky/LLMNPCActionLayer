#include "UI/SLLMNPCMotionTestConsole.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Dialogue/LLMNPCDialogueComponent.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/TargetPoint.h"
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
constexpr double OnlineMatrixPlaybackTimeoutSeconds = 20.0;
constexpr double OnlineMatrixPlaybackIdleDwellSeconds = 0.9;
constexpr double OnlineMatrixNoActionObservationSeconds = 1.25;
constexpr double OnlineMatrixInterCaseDelaySeconds = 0.15;
constexpr float OnlineMatrixMinimumRepeatSuppressionSeconds = 5.5f;

bool IsOnlinePlaybackBusy(const FLLMNPCMotionDebugState& Debug)
{
	return
		Debug.bHasActivePlan ||
		Debug.ActivePlanCount > 0 ||
		Debug.QueueCount > 0 ||
		Debug.bMotionRequestInFlight ||
		Debug.bAnimationAssetPlaying;
}

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

TArray<TSharedPtr<FJsonValue>> NamesToJsonValues(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> StringsToJsonValues(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Strings.Num());
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
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
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("ForwardN3ContextSection", "Forward N3 Context"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("ForwardN3ContextPreset", "Context Preset"),
						SNew(SUniformGridPanel)
						.SlotPadding(FMargin(4.0f, 0.0f))
						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SButton)
								.OnClicked(this, &SLLMNPCMotionTestConsole::HandleContextNeutral)
								[
									SNew(STextBlock).Text(LOCTEXT("ContextNeutral", "Neutral"))
								]
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
								.OnClicked(this, &SLLMNPCMotionTestConsole::HandleContextExcited)
								[
									SNew(STextBlock).Text(LOCTEXT("ContextExcited", "Excited"))
								]
						]
						+ SUniformGridPanel::Slot(2, 0)
						[
							SNew(SButton)
								.OnClicked(this, &SLLMNPCMotionTestConsole::HandleContextRightBusy)
								[
									SNew(STextBlock).Text(LOCTEXT("ContextRightBusy", "Right Busy"))
								]
						]
						+ SUniformGridPanel::Slot(3, 0)
						[
							SNew(SButton)
								.OnClicked(this, &SLLMNPCMotionTestConsole::HandleContextWalking)
								[
									SNew(STextBlock).Text(LOCTEXT("ContextWalking", "Walking"))
								]
						]
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("ForwardN3TargetDistance", "Test Target Distance"),
						SNew(SNumericEntryBox<float>)
						.MinValue(30.0f)
						.MaxValue(1000.0f)
						.Value_Lambda([this]()
						{
							return TOptional<float>(TestTargetDistanceCm);
						})
						.OnValueChanged_Lambda([this](float Value)
						{
							TestTargetDistanceCm = Value;
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("ForwardN3TargetHeight", "Test Target Height"),
						SNew(SNumericEntryBox<float>)
						.MinValue(-250.0f)
						.MaxValue(250.0f)
						.Value_Lambda([this]()
						{
							return TOptional<float>(TestTargetHeightCm);
						})
						.OnValueChanged_Lambda([this](float Value)
						{
							TestTargetHeightCm = Value;
						})
					)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 3.0f, 0.0f, 4.0f)
				[
					SNew(SButton)
						.ToolTipText(LOCTEXT(
							"ForwardN3PlaceTargetTooltip",
							"Place or move a transient PIE target relative to the selected NPC."
						))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandlePlaceN3TestTarget)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("ForwardN3PlaceTarget", "Place / Move Target"))
								.Justification(ETextJustify::Center)
						]
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
						.ToolTipText(LOCTEXT("RunOnlineMatrixTooltip", "Run the locked N7-F strict online selection matrix sequentially."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleRunOnlineMatrix)
						[
							SNew(STextBlock).Text(LOCTEXT("RunOnlineMatrix", "Run N7-F Matrix"))
						]
					]
					+ SUniformGridPanel::Slot(2, 0)
					[
						SNew(SButton)
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleCancelOnlineEvaluation)
						[
							SNew(STextBlock).Text(LOCTEXT("CancelOnlineEvaluation", "Cancel Online"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(188.0f, 2.0f, 0.0f, 4.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.IsEnabled_Lambda([this]()
						{
							return bOnlineMatrixCompleted && !bOnlineMatrixRunning;
						})
						.ToolTipText(LOCTEXT("OnlineMatrixVisualPassTooltip", "Human confirmation that every case in the completed matrix looked acceptable in PIE."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleOnlineMatrixVisualPass)
						[
							SNew(STextBlock).Text(LOCTEXT("OnlineMatrixVisualPass", "Human Visual Pass"))
						]
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.IsEnabled_Lambda([this]()
						{
							return bOnlineMatrixCompleted && !bOnlineMatrixRunning;
						})
						.ToolTipText(LOCTEXT("OnlineMatrixVisualFailTooltip", "Human confirmation that at least one case in the completed matrix had a visual problem."))
						.OnClicked(this, &SLLMNPCMotionTestConsole::HandleOnlineMatrixVisualFail)
						[
							SNew(STextBlock).Text(LOCTEXT("OnlineMatrixVisualFail", "Human Visual Fail"))
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
	if (AActor* TestTarget = N3TestTargetActor.Get())
	{
		TestTarget->Destroy();
	}
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

	if (AActor* TestTarget = N3TestTargetActor.Get())
	{
		UWorld* TargetWorld = TestTarget->GetWorld();
		if (TargetWorld && GEditor && TargetWorld == GEditor->PlayWorld)
		{
			DrawDebugSphere(
				TargetWorld,
				TestTarget->GetActorLocation(),
				12.0f,
				12,
				FColor::Green,
				false,
				0.0f,
				0,
				1.5f
			);
			if (
				const ULLMNPCMotionComponent* Motion =
					GetSelectedMotionComponent()
			)
			{
				if (const AActor* Owner = Motion->GetOwner())
				{
					DrawDebugLine(
						TargetWorld,
						Owner->GetActorLocation(),
						TestTarget->GetActorLocation(),
						FColor::Cyan,
						false,
						0.0f,
						0,
						0.75f
					);
				}
			}
		}
	}

	PollOnlineEvaluation();
	if (bOnlineMatrixRunning && !bOnlineRunInFlight)
	{
		if (bOnlineMatrixWaitingForPlayback)
		{
			PollOnlineMatrixPlayback(InCurrentTime);
		}
		else if (InCurrentTime >= OnlineMatrixNextCaseTime)
		{
			StartOnlineMatrixCase();
		}
	}
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
		TEXT("Resolved core: amplitude %.3f, speed %.3f, duration %.3f, style %s, mirror %s, seed %d\n")
		TEXT("Resolved body: reach %.3f, height %.3f, lateral %.3f, cycles %d, gaze %.3f, palm %.3f, fingers %.3f, torso %.3f, blend %.3f / %.3f\n")
		TEXT("Execution: movement %s, target %s, distance %.1f, height %.1f, space %.2f, right busy %s, left busy %s\n")
		TEXT("Modifier result: %s, fallback %s\nTrace: %s\n")
		TEXT("Channels: %s\nValidator: %s (%s)\n")
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
		Debug.ResolvedReachScale,
		Debug.ResolvedHeightScale,
		Debug.ResolvedLateralScale,
		Debug.ResolvedCycleCount,
		Debug.ResolvedGazeEngagement,
		Debug.ResolvedPalmOrientationWeight,
		Debug.ResolvedFingerPoseWeight,
		Debug.ResolvedTorsoParticipation,
		Debug.ResolvedBlendInScale,
		Debug.ResolvedBlendOutScale,
		Debug.ExecutionMovementMode.IsEmpty()
			? TEXT("unknown")
			: *Debug.ExecutionMovementMode,
		Debug.LastTargetRef.IsEmpty() ? TEXT("none") : *Debug.LastTargetRef,
		Debug.TargetDistanceCm,
		Debug.TargetHeightRelativeCm,
		Debug.AvailableSpace,
		Debug.bRightHandOccupied ? TEXT("yes") : TEXT("no"),
		Debug.bLeftHandOccupied ? TEXT("yes") : TEXT("no"),
		*Debug.ModifierResultCode.ToString(),
		Debug.bModifierFallbackRequired ? TEXT("yes") : TEXT("no"),
		Debug.ModifierResolutionTrace.IsEmpty()
			? TEXT("none")
			: *Debug.ModifierResolutionTrace,
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
	const FLLMNPCForwardN7MatrixCase* MatrixCase = GetActiveOnlineMatrixCase();
	const FString MatrixTrace = FString::Printf(
		TEXT("Matrix: %s  Stage: %s  Progress: %d/%d  Passed: %d  Failed: %d  Human: %s  Case: %s\n"),
		bOnlineMatrixRunning
			? TEXT("running")
			: (bOnlineMatrixCompleted
				? TEXT("complete")
				: (bOnlineMatrixCancelled ? TEXT("cancelled") : TEXT("idle"))),
		bOnlineRunInFlight
			? TEXT("model request")
			: (bOnlineMatrixWaitingForPlayback
				? TEXT("full playback")
				: TEXT("idle")),
		FMath::Max(OnlineMatrixCaseIndex, 0),
		OnlineMatrixCases.Num(),
		OnlineMatrixPassedCount,
		OnlineMatrixFailedCount,
		*OnlineMatrixHumanReview,
		MatrixCase ? *MatrixCase->CaseId.ToString() : TEXT("none")
	);
	if (!Dialogue)
	{
		return FText::FromString(FString::Printf(
			TEXT("%sConfig: %s  Connection: %s\nDialogue component unavailable on selected PIE NPC"),
			*MatrixTrace,
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
		TEXT("%sConfig: %s  Connection: %s  Run: %s\n")
		TEXT("Provider: %s  Model: %s  Request: %s\n")
		TEXT("Context: %s\nCandidates: %d -> %d (%d excluded)\n")
		TEXT("Selected: %s  Resolved: %s  Fallback: %s  Error: %s"),
		*MatrixTrace,
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

bool SLLMNPCMotionTestConsole::ApplyContextPreset(
	ELLMNPCTestContextPreset Preset
)
{
	ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent();
	if (!Dialogue)
	{
		SetStatus(
			LOCTEXT(
				"ForwardN3ContextUnavailable",
				"The selected PIE NPC has no dialogue context component"
			),
			true
		);
		return false;
	}

	for (const FName State : {
		FName(TEXT("right_hand_busy")),
		FName(TEXT("right_hand_occupied")),
		FName(TEXT("left_hand_busy")),
		FName(TEXT("left_hand_occupied")),
		FName(TEXT("upper_body_busy")),
		FName(TEXT("upper_body_occupied")),
		FName(TEXT("walking")),
		FName(TEXT("running")),
		FName(TEXT("sprinting")),
		FName(TEXT("turning")),
		FName(TEXT("falling"))
	})
	{
		Dialogue->SetSceneStateActive(State, false);
	}
	Dialogue->ResetEmotionContext();

	FText Label = LOCTEXT("ForwardN3NeutralLabel", "Neutral");
	switch (Preset)
	{
	case ELLMNPCTestContextPreset::Excited:
		Dialogue->SetEmotionContext(TEXT("excited"), 0.9f, 0.8f, 0.9f);
		Label = LOCTEXT("ForwardN3ExcitedLabel", "Excited");
		break;
	case ELLMNPCTestContextPreset::RightHandOccupied:
		Dialogue->SetSceneStateActive(TEXT("right_hand_busy"), true);
		Label = LOCTEXT("ForwardN3RightBusyLabel", "Right Busy");
		break;
	case ELLMNPCTestContextPreset::Walking:
		Dialogue->SetSceneStateActive(TEXT("walking"), true);
		Label = LOCTEXT("ForwardN3WalkingLabel", "Walking");
		break;
	case ELLMNPCTestContextPreset::Neutral:
	default:
		break;
	}
	SetStatus(
		FText::Format(
			LOCTEXT("ForwardN3ContextApplied", "N3 context applied: {0}"),
			Label
		),
		false
	);
	return true;
}

bool SLLMNPCMotionTestConsole::PlaceN3TestTarget()
{
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	AActor* Owner = Motion ? Motion->GetOwner() : nullptr;
	UWorld* World = Motion ? Motion->GetWorld() : nullptr;
	if (!Motion || !Owner || !World || !GEditor || World != GEditor->PlayWorld)
	{
		SetStatus(
			LOCTEXT(
				"ForwardN3TargetUnavailable",
				"Start PIE and select an NPC before placing the N3 target"
			),
			true
		);
		return false;
	}

	AActor* TestTarget = N3TestTargetActor.Get();
	if (!TestTarget || TestTarget->GetWorld() != World)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TestTarget = World->SpawnActor<ATargetPoint>(
			Owner->GetActorLocation(),
			FRotator::ZeroRotator,
			SpawnParameters
		);
		N3TestTargetActor = TestTarget;
	}
	if (!TestTarget)
	{
		SetStatus(
			LOCTEXT(
				"ForwardN3TargetSpawnFailed",
				"Could not create the transient N3 test target"
			),
			true
		);
		return false;
	}

	FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Location =
		Owner->GetActorLocation() +
		Forward * TestTargetDistanceCm +
		FVector::UpVector * TestTargetHeightCm;
	TestTarget->SetActorLocation(
		Location,
		false,
		nullptr,
		ETeleportType::TeleportPhysics
	);
	TargetRef = TEXT("n3_test_target");
	Motion->RegisterTarget(TargetRef, TestTarget);
	if (ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent())
	{
		Dialogue->RegisterSceneTarget(
			TargetRef,
			TestTarget,
			TEXT("scene_target"),
			{ TEXT("test_target"), TEXT("location") },
			1.0f
		);
	}
	SetStatus(
		FText::Format(
			LOCTEXT(
				"ForwardN3TargetPlaced",
				"N3 target placed: distance {0}, height {1}"
			),
			FText::AsNumber(TestTargetDistanceCm),
			FText::AsNumber(TestTargetHeightCm)
		),
		false
	);
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
		AActor* ExecutionTarget = nullptr;
		if (
			Modifiers.TargetRef == TEXT("n3_test_target") &&
			N3TestTargetActor.IsValid()
		)
		{
			ExecutionTarget = N3TestTargetActor.Get();
		}
		else
		{
			ExecutionTarget =
				UGameplayStatics::GetPlayerPawn(Motion->GetWorld(), 0);
		}
		if (ExecutionTarget)
		{
			Motion->RegisterTarget(Modifiers.TargetRef, ExecutionTarget);
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
	Record.ResolvedReachScale = Debug.ResolvedReachScale;
	Record.ResolvedHeightScale = Debug.ResolvedHeightScale;
	Record.ResolvedGazeEngagement = Debug.ResolvedGazeEngagement;
	Record.ResolvedPalmOrientationWeight =
		Debug.ResolvedPalmOrientationWeight;
	Record.ResolvedFingerPoseWeight = Debug.ResolvedFingerPoseWeight;
	Record.ResolvedTorsoParticipation = Debug.ResolvedTorsoParticipation;
	Record.bResolvedMirror = Debug.bResolvedMirror;
	Record.ExecutionMovementMode = Debug.ExecutionMovementMode;
	Record.TargetDistanceCm = Debug.TargetDistanceCm;
	Record.TargetHeightRelativeCm = Debug.TargetHeightRelativeCm;
	Record.AvailableSpace = Debug.AvailableSpace;
	Record.bRightHandOccupied = Debug.bRightHandOccupied;
	Record.bLeftHandOccupied = Debug.bLeftHandOccupied;
	Record.ModifierResultCode = Debug.ModifierResultCode;
	Record.ModifierResolutionTrace = Debug.ModifierResolutionTrace;
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
		bOnlineMatrixRunning = false;
		bOnlineMatrixCompleted = false;
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
	const FLLMNPCForwardN7MatrixCase* MatrixCase =
		GetActiveOnlineMatrixCase();
	const FLLMNPCDialogueTurnResult& Turn = Dialogue.LastTurnResult;
	const FLLMNPCDialogueDebugState DialogueDebug = Dialogue.GetDebugState();
	const FLLMNPCSelectionContextSnapshot SelectionContext =
		Dialogue.GetSelectionContextSnapshot();
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
	Record.bResponseSchemaValid = Turn.bResponseSchemaValid;
	Record.RequestSchemaVersion = DialogueDebug.RequestSchemaVersion;
	Record.SourceCandidateCount = DialogueDebug.SourceCandidateCount;
	Record.OfferedCandidateCount = DialogueDebug.OfferedCandidateCount;
	Record.ExcludedCandidateCount = DialogueDebug.ExcludedCandidateCount;
	for (const FLLMNPCTemplateCandidate& Candidate : Dialogue.GetLastOfferedCandidates())
	{
		Record.OfferedCandidateIds.Add(Candidate.SelectionId);
	}
	Record.CandidateExclusions = Dialogue.GetLastCandidateExclusions();
	Record.ActiveStates = SelectionContext.ActiveStates;
	Record.ContextEmotion = SelectionContext.Emotion.PrimaryEmotion;
	for (const FLLMNPCSceneTargetContext& Target : SelectionContext.AvailableTargets)
	{
		Record.AvailableTargetRefs.Add(Target.TargetRef);
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
	if (MatrixCase)
	{
		Record.bMatrixCase = true;
		Record.MatrixCaseId = MatrixCase->CaseId;
		Record.ExpectedSelection = MatrixCase->ExpectedSelection;
		Record.ExpectedActionId = MatrixCase->ExpectedActionId;
		Record.ExpectedTargetRef = MatrixCase->ExpectedTargetRef;
		Record.bCheckExpectedMirror = MatrixCase->bCheckMirror;
		Record.bExpectedMirror = MatrixCase->bExpectedMirror;
		Record.bCheckExpectedStyle = MatrixCase->bCheckStyle;
		Record.ExpectedStyle = MatrixCase->ExpectedStyle;
		Record.ExpectedExclusionReasons = MatrixCase->AllowedExclusionReasons;
		Record.CoverageTags = MatrixCase->CoverageTags;

		FLLMNPCForwardN7ObservedSelection Observed;
		Observed.bStrictProviderIdentity = Record.bStrictProviderIdentity;
		Observed.bUsedLocalFallback = Record.bUsedLocalFallback;
		Observed.bResponseSchemaValid = Record.bResponseSchemaValid;
		Observed.OfferedCandidateIds = Record.OfferedCandidateIds;
		Observed.CandidateExclusions = Record.CandidateExclusions;
		Observed.SelectedActionId = Record.SelectedActionId;
		Observed.ResolvedTemplateId = Record.ResolvedTemplateId;
		Observed.bActionExecuted = Record.bActionExecuted;
		Observed.bBehaviorStarted = Record.bBehaviorStarted;
		Observed.ErrorCode = Record.ErrorCode;
		Observed.TargetRef = Record.TargetRef;
		Observed.ResolvedStyle = Record.ResolvedStyle;
		Observed.bResolvedMirror = Record.bResolvedMirror;
		Observed.ValidatorResult = Record.ValidatorResult;
		const FLLMNPCForwardN7CaseVerdict Verdict =
			LLMNPCForwardN7Evaluation::EvaluateCase(*MatrixCase, Observed);
		Record.bProviderPassed = Verdict.bProviderPassed;
		Record.bSchemaPassed = Verdict.bSchemaPassed;
		Record.bSelectionPassed = Verdict.bSelectionPassed;
		Record.bExecutionPassed = Verdict.bExecutionPassed;
		Record.bContextPassed = Verdict.bContextPassed;
		Record.bStylePassed = Verdict.bStylePassed;
		Record.bValidatorPassed = Verdict.bValidatorPassed;
		Record.FailureReason = Verdict.FailureReason;
		Record.bPassed = Verdict.bPassed;
	}
	else
	{
		Record.bPassed =
			Record.bStrictProviderIdentity &&
			Record.ErrorCode.IsNone() &&
			!Record.SelectedActionId.IsNone() &&
			!Record.ResolvedTemplateId.IsNone() &&
			(Record.bActionExecuted || Record.bBehaviorStarted);
	}

	bOnlineRunInFlight = false;
	ActiveOnlineRequestId.Invalidate();
	RestoreOnlineDialogueSettings();

	if (MatrixCase && bOnlineMatrixRunning)
	{
		Record.bPlaybackRequired =
			Record.bActionExecuted || Record.bBehaviorStarted;
		Record.PlaybackWaitResult = Record.bPlaybackRequired
			? TEXT("waiting_for_playback")
			: TEXT("observing_no_action");
		bOnlineMatrixWaitingForPlayback = true;
		OnlineMatrixPlaybackWaitStartedAt = FPlatformTime::Seconds();
		OnlineMatrixPlaybackIdleSince = 0.0;
		if (const ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
		{
			Record.bPlaybackObserved =
				IsOnlinePlaybackBusy(Motion->GetDebugState());
		}
		SetStatus(
			FText::Format(
				Record.bPlaybackRequired
					? LOCTEXT(
						"OnlineMatrixPlaybackWaiting",
						"N7-F selection complete for {0}; waiting for full playback and return to neutral"
					)
					: LOCTEXT(
						"OnlineMatrixNoActionWaiting",
						"N7-F selection complete for {0}; observing the required no-action interval"
					),
				FText::FromName(Record.MatrixCaseId)
			),
			!Record.bPassed
		);
		return;
	}

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
}

void SLLMNPCMotionTestConsole::PollOnlineMatrixPlayback(double CurrentTime)
{
	if (
		!bOnlineMatrixRunning ||
		!bOnlineMatrixWaitingForPlayback ||
		OnlineEvaluationRecords.IsEmpty()
	)
	{
		return;
	}

	FLLMNPCOnlineEvaluationRecord& Record = OnlineEvaluationRecords.Last();
	const FLLMNPCForwardN7MatrixCase* MatrixCase = GetActiveOnlineMatrixCase();
	if (!MatrixCase || Record.MatrixCaseId != MatrixCase->CaseId)
	{
		bOnlineMatrixWaitingForPlayback = false;
		bOnlineMatrixRunning = false;
		bOnlineMatrixCompleted = false;
		SetStatus(
			LOCTEXT("OnlineMatrixPlaybackStateMismatch", "N7-F playback state no longer matches the active case"),
			true
		);
		return;
	}

	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Motion)
	{
		CompleteOnlineMatrixCasePlayback(false, TEXT("motion_component_lost"));
		return;
	}

	const double Elapsed = FMath::Max(
		CurrentTime - OnlineMatrixPlaybackWaitStartedAt,
		0.0
	);
	const bool bPlaybackBusy = IsOnlinePlaybackBusy(Motion->GetDebugState());
	if (bPlaybackBusy)
	{
		Record.bPlaybackObserved = true;
		OnlineMatrixPlaybackIdleSince = 0.0;
	}
	else if (!Record.bPlaybackRequired || Record.bPlaybackObserved)
	{
		if (OnlineMatrixPlaybackIdleSince <= 0.0)
		{
			OnlineMatrixPlaybackIdleSince = CurrentTime;
		}
		const double RequiredIdleSeconds = Record.bPlaybackRequired
			? OnlineMatrixPlaybackIdleDwellSeconds
			: OnlineMatrixNoActionObservationSeconds;
		if (CurrentTime - OnlineMatrixPlaybackIdleSince >= RequiredIdleSeconds)
		{
			CompleteOnlineMatrixCasePlayback(
				true,
				Record.bPlaybackRequired ? TEXT("completed") : TEXT("not_required")
			);
			return;
		}
	}

	if (Elapsed >= OnlineMatrixPlaybackTimeoutSeconds)
	{
		Motion->StopAllMotions();
		CompleteOnlineMatrixCasePlayback(false, TEXT("timeout"));
	}
}

void SLLMNPCMotionTestConsole::CompleteOnlineMatrixCasePlayback(
	bool bPlaybackGatePassed,
	const FString& Result
)
{
	if (!bOnlineMatrixWaitingForPlayback || OnlineEvaluationRecords.IsEmpty())
	{
		return;
	}

	FLLMNPCOnlineEvaluationRecord& Record = OnlineEvaluationRecords.Last();
	Record.PlaybackWaitSeconds = static_cast<float>(FMath::Max(
		FPlatformTime::Seconds() - OnlineMatrixPlaybackWaitStartedAt,
		0.0
	));
	Record.PlaybackWaitResult = Result;
	Record.bPlaybackCompleted =
		Record.bPlaybackRequired && bPlaybackGatePassed;
	if (!bPlaybackGatePassed)
	{
		Record.bPassed = false;
		const FString PlaybackFailure = FString::Printf(
			TEXT("playback gate failed: %s"),
			*Result
		);
		Record.FailureReason = Record.FailureReason.IsEmpty()
			? PlaybackFailure
			: Record.FailureReason + TEXT("; ") + PlaybackFailure;
	}

	bOnlineMatrixWaitingForPlayback = false;
	OnlineMatrixPlaybackWaitStartedAt = 0.0;
	OnlineMatrixPlaybackIdleSince = 0.0;
	if (Record.bPassed)
	{
		++OnlineMatrixPassedCount;
	}
	else
	{
		++OnlineMatrixFailedCount;
	}

	++OnlineMatrixCaseIndex;
	if (OnlineMatrixCaseIndex >= OnlineMatrixCases.Num())
	{
		FinishOnlineMatrix();
		return;
	}

	OnlineMatrixNextCaseTime =
		FPlatformTime::Seconds() + OnlineMatrixInterCaseDelaySeconds;
	SetStatus(
		FText::Format(
			LOCTEXT(
				"OnlineMatrixProgress",
				"N7-F matrix: {0}/{1} fully observed, {2} passed, {3} failed"
			),
			FText::AsNumber(OnlineMatrixCaseIndex),
			FText::AsNumber(OnlineMatrixCases.Num()),
			FText::AsNumber(OnlineMatrixPassedCount),
			FText::AsNumber(OnlineMatrixFailedCount)
		),
		OnlineMatrixFailedCount > 0
	);
}

const FLLMNPCForwardN7MatrixCase*
SLLMNPCMotionTestConsole::GetActiveOnlineMatrixCase() const
{
	return bOnlineMatrixRunning &&
		OnlineMatrixCases.IsValidIndex(OnlineMatrixCaseIndex)
		? &OnlineMatrixCases[OnlineMatrixCaseIndex]
		: nullptr;
}

bool SLLMNPCMotionTestConsole::PrepareOnlineMatrixCase(
	const FLLMNPCForwardN7MatrixCase& TestCase
)
{
	ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent();
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	if (!Dialogue || !Motion)
	{
		SetStatus(
			LOCTEXT("OnlineMatrixTargetUnavailable", "The N7-F matrix lost its PIE NPC"),
			true
		);
		return false;
	}

	for (const FName State : {
		FName(TEXT("right_hand_busy")),
		FName(TEXT("right_hand_occupied")),
		FName(TEXT("left_hand_busy")),
		FName(TEXT("left_hand_occupied")),
		FName(TEXT("upper_body_busy")),
		FName(TEXT("upper_body_occupied")),
		FName(TEXT("walking")),
		FName(TEXT("running")),
		FName(TEXT("sprinting")),
		FName(TEXT("turning")),
		FName(TEXT("falling"))
	})
	{
		Dialogue->SetSceneStateActive(State, false);
	}
	for (const FName State : TestCase.ActiveStates)
	{
		Dialogue->SetSceneStateActive(State, true);
	}
	Dialogue->ResetEmotionContext();
	if (!TestCase.Emotion.IsNone())
	{
		Dialogue->SetEmotionContext(
			TestCase.Emotion,
			TestCase.EmotionIntensity,
			TestCase.EmotionValence,
			TestCase.EmotionArousal
		);
	}

	TestTargetDistanceCm = TestCase.TargetDistanceCm;
	TestTargetHeightCm = TestCase.TargetHeightCm;
	if (TestCase.bProvideTarget)
	{
		if (!PlaceN3TestTarget())
		{
			return false;
		}
	}
	else
	{
		for (const FString& TargetToRemove : {
			FString(TEXT("n3_test_target")),
			FString(TEXT("player")),
			FString(TEXT("player.main"))
		})
		{
			if (
				TargetToRemove == TEXT("n3_test_target") ||
				TestCase.bRequireNoAvailableTargets
			)
			{
				Dialogue->RegisterSceneTarget(
					TargetToRemove,
					nullptr,
					TEXT("scene_target"),
					{},
					0.0f
				);
			}
		}
		if (AActor* TestTarget = N3TestTargetActor.Get())
		{
			TestTarget->Destroy();
		}
		N3TestTargetActor.Reset();
		TargetRef = TestCase.bRequireNoAvailableTargets
			? FString()
			: FString(TEXT("player"));
	}

	OnlineInput = TestCase.NaturalLanguage;
	Motion->ResetMotionTestState();
	return true;
}

void SLLMNPCMotionTestConsole::StartOnlineMatrixCase()
{
	const FLLMNPCForwardN7MatrixCase* TestCase = GetActiveOnlineMatrixCase();
	if (!TestCase)
	{
		FinishOnlineMatrix();
		return;
	}
	if (!PrepareOnlineMatrixCase(*TestCase))
	{
		bOnlineMatrixRunning = false;
		bOnlineMatrixCompleted = false;
		return;
	}

	HandleRunOnlineEvaluation();
	if (!bOnlineRunInFlight)
	{
		bOnlineMatrixRunning = false;
		bOnlineMatrixCompleted = false;
		SetStatus(
			FText::Format(
				LOCTEXT("OnlineMatrixCaseStartFailed", "N7-F matrix could not start case {0}"),
				FText::FromName(TestCase->CaseId)
			),
			true
		);
	}
}

void SLLMNPCMotionTestConsole::FinishOnlineMatrix()
{
	bOnlineMatrixRunning = false;
	bOnlineMatrixCompleted = true;
	bOnlineMatrixCancelled = false;
	bOnlineMatrixWaitingForPlayback = false;
	OnlineMatrixNextCaseTime = 0.0;
	OnlineMatrixPlaybackWaitStartedAt = 0.0;
	OnlineMatrixPlaybackIdleSince = 0.0;
	ResetOnlineMatrixContext();
	HandleSaveReport();
	if (LastSavedReportPath.IsEmpty())
	{
		SetStatus(
			LOCTEXT("OnlineMatrixReportFailed", "N7-F matrix finished, but its report could not be saved"),
			true
		);
		return;
	}
	SetStatus(
		FText::Format(
			LOCTEXT(
				"OnlineMatrixComplete",
				"N7-F matrix complete: {0}/{1} passed. Report: {2}"
			),
			FText::AsNumber(OnlineMatrixPassedCount),
			FText::AsNumber(OnlineMatrixCases.Num()),
			FText::FromString(LastSavedReportPath)
		),
		OnlineMatrixFailedCount > 0
	);
}

void SLLMNPCMotionTestConsole::ResetOnlineMatrixContext()
{
	if (ULLMNPCDialogueComponent* Dialogue = GetSelectedDialogueComponent())
	{
		for (const FName State : {
			FName(TEXT("right_hand_busy")),
			FName(TEXT("right_hand_occupied")),
			FName(TEXT("left_hand_busy")),
			FName(TEXT("left_hand_occupied")),
			FName(TEXT("upper_body_busy")),
			FName(TEXT("upper_body_occupied")),
			FName(TEXT("walking")),
			FName(TEXT("running")),
			FName(TEXT("sprinting")),
			FName(TEXT("turning")),
			FName(TEXT("falling"))
		})
		{
			Dialogue->SetSceneStateActive(State, false);
		}
		Dialogue->ResetEmotionContext();
		Dialogue->RegisterSceneTarget(
			TEXT("n3_test_target"),
			nullptr,
			TEXT("scene_target"),
			{},
			0.0f
		);
		if (ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
		{
			if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(Motion->GetWorld(), 0))
			{
				Dialogue->RegisterSceneTarget(
					TEXT("player.main"),
					PlayerPawn,
					TEXT("player"),
					{TEXT("conversation_partner")},
					1.0f
				);
				Dialogue->RegisterTarget(TEXT("player"), PlayerPawn);
			}
		}
	}
	if (AActor* TestTarget = N3TestTargetActor.Get())
	{
		TestTarget->Destroy();
	}
	N3TestTargetActor.Reset();
	TargetRef = TEXT("player");
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
		if (bOnlineRunInFlight || bOnlineMatrixRunning)
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
	if (bOnlineRunInFlight || bOnlineMatrixRunning)
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

FReply SLLMNPCMotionTestConsole::HandleContextNeutral()
{
	ApplyContextPreset(ELLMNPCTestContextPreset::Neutral);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleContextExcited()
{
	ApplyContextPreset(ELLMNPCTestContextPreset::Excited);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleContextRightBusy()
{
	ApplyContextPreset(ELLMNPCTestContextPreset::RightHandOccupied);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleContextWalking()
{
	ApplyContextPreset(ELLMNPCTestContextPreset::Walking);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandlePlaceN3TestTarget()
{
	PlaceN3TestTarget();
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
	if (bOnlineMatrixWaitingForPlayback)
	{
		SetStatus(
			LOCTEXT("OnlinePlaybackStillRunning", "Wait for the current N7-F action to finish before starting another online selection"),
			true
		);
		return FReply::Handled();
	}
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
	const FLLMNPCForwardN7MatrixCase* MatrixCase =
		GetActiveOnlineMatrixCase();
	if (!MatrixCase || MatrixCase->bResetConversationBefore)
	{
		Dialogue->ResetConversation();
	}
	PreviousProviderKind = Dialogue->ProviderKind;
	PreviousProviderIdOverride = Dialogue->ProviderIdOverride;
	bPreviousLocalFallback = Dialogue->bEnableLocalCommandFallback;
	ActiveOnlineDialogue = Dialogue;
	bRestoreOnlineDialogueSettings = true;
	Dialogue->SetProviderKind(ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly);
	Dialogue->bEnableLocalCommandFallback = false;
	if (!MatrixCase || !MatrixCase->bRequireNoAvailableTargets)
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(Motion->GetWorld(), 0))
		{
			Dialogue->RegisterTarget(TEXT("player"), PlayerPawn);
		}
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
			MatrixCase
				? LOCTEXT(
					"OnlineMatrixRequestStarted",
					"N7-F case started: {0} ({1})"
				)
				: LOCTEXT(
					"OnlineRequestStarted",
					"Strict online request started: {1}"
				),
			MatrixCase
				? FText::FromName(MatrixCase->CaseId)
				: FText::GetEmpty(),
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

FReply SLLMNPCMotionTestConsole::HandleRunOnlineMatrix()
{
	if (bOnlineRunInFlight || bOnlineMatrixRunning)
	{
		SetStatus(LOCTEXT("OnlineMatrixAlreadyRunning", "An online run is already in flight"), true);
		return FReply::Handled();
	}
	if (!GetSelectedDialogueComponent() || !GetSelectedMotionComponent())
	{
		SetStatus(
			LOCTEXT("OnlineMatrixPIEUnavailable", "Start PIE and select a Manny NPC before running N7-F"),
			true
		);
		return FReply::Handled();
	}
	const FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!OnlineConfig.HasPassingConnectionForCurrentConfig())
	{
		SetStatus(
			LOCTEXT("OnlineMatrixConnectionMissing", "Load env.txt and pass Test Connection before running N7-F"),
			true
		);
		return FReply::Handled();
	}
	if (!Settings || !Settings->bAllowDirectProviderCallInEditorOnly)
	{
		SetStatus(
			LOCTEXT("OnlineMatrixDirectDisabled", "Direct Editor provider access is disabled"),
			true
		);
		return FReply::Handled();
	}
	if (Settings->RepeatSuppressionSeconds < OnlineMatrixMinimumRepeatSuppressionSeconds)
	{
		SetStatus(
			FText::Format(
				LOCTEXT(
					"OnlineMatrixRepeatWindowTooShort",
					"N7-F requires Repeat Suppression Seconds >= {0} so the repeat case remains protected after full playback"
				),
				FText::AsNumber(OnlineMatrixMinimumRepeatSuppressionSeconds)
			),
			true
		);
		return FReply::Handled();
	}

	ULLMNPCTemplateLibrarySubsystem* Library = GetTemplateLibrary();
	if (!Library)
	{
		SetStatus(LOCTEXT("OnlineMatrixLibraryUnavailable", "The PIE template library is unavailable"), true);
		return FReply::Handled();
	}
	Library->RefreshLibrary();
	const FLLMNPCForwardN7LibraryAudit Audit =
		LLMNPCForwardN7Evaluation::AuditMannyLibrary(*Library);
	if (!Audit.bPassed)
	{
		SetStatus(
			FText::FromString(FString::Printf(
				TEXT("N7-F library audit failed: %s"),
				*FString::Join(Audit.Errors, TEXT("; "))
			)),
			true
		);
		return FReply::Handled();
	}

	OnlineEvaluationRecords.Reset();
	OnlineMatrixCases = LLMNPCForwardN7Evaluation::BuildDefaultMatrix();
	OnlineMatrixCaseIndex = 0;
	OnlineMatrixPassedCount = 0;
	OnlineMatrixFailedCount = 0;
	bOnlineMatrixRunning = true;
	bOnlineMatrixCompleted = false;
	bOnlineMatrixCancelled = false;
	bOnlineMatrixWaitingForPlayback = false;
	OnlineMatrixHumanReview = TEXT("pending");
	OnlineMatrixHumanReviewedAtUtc = FDateTime();
	LastSavedReportPath.Reset();
	OnlineMatrixPlaybackWaitStartedAt = 0.0;
	OnlineMatrixPlaybackIdleSince = 0.0;
	OnlineMatrixNextCaseTime = FPlatformTime::Seconds();
	StartOnlineMatrixCase();
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleOnlineMatrixVisualPass()
{
	return ApplyOnlineMatrixHumanReview(TEXT("passed"));
}

FReply SLLMNPCMotionTestConsole::HandleOnlineMatrixVisualFail()
{
	return ApplyOnlineMatrixHumanReview(TEXT("failed"));
}

FReply SLLMNPCMotionTestConsole::ApplyOnlineMatrixHumanReview(
	const FString& Review
)
{
	if (!bOnlineMatrixCompleted || OnlineMatrixCases.IsEmpty())
	{
		SetStatus(
			LOCTEXT("OnlineMatrixHumanReviewUnavailable", "Complete the N7-F matrix before recording a human visual review"),
			true
		);
		return FReply::Handled();
	}
	if (Review == TEXT("passed") && OnlineMatrixFailedCount > 0)
	{
		SetStatus(
			LOCTEXT("OnlineMatrixHumanPassBlocked", "Human Visual Pass requires every machine-evaluated matrix case to pass"),
			true
		);
		return FReply::Handled();
	}

	OnlineMatrixHumanReview = Review;
	OnlineMatrixHumanReviewedAtUtc = FDateTime::UtcNow();
	for (FLLMNPCOnlineEvaluationRecord& Record : OnlineEvaluationRecords)
	{
		if (Record.bMatrixCase)
		{
			Record.VisualReview = Review;
		}
	}
	HandleSaveReport();
	if (LastSavedReportPath.IsEmpty())
	{
		SetStatus(
			LOCTEXT("OnlineMatrixHumanReviewSaveFailed", "The human-reviewed matrix report could not be saved"),
			true
		);
		return FReply::Handled();
	}

	SetStatus(
		FText::Format(
			LOCTEXT(
				"OnlineMatrixHumanReviewSaved",
				"N7-F Human Visual {0}. Reviewed report: {1}"
			),
			Review == TEXT("passed")
				? LOCTEXT("OnlineMatrixHumanReviewPassLabel", "Pass")
				: LOCTEXT("OnlineMatrixHumanReviewFailLabel", "Fail"),
			FText::FromString(LastSavedReportPath)
		),
		Review == TEXT("failed")
	);
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleCancelOnlineEvaluation()
{
	const bool bWasMatrixRunning = bOnlineMatrixRunning;
	if (ULLMNPCDialogueComponent* Dialogue = ActiveOnlineDialogue.Get())
	{
		if (bOnlineRunInFlight)
		{
			Dialogue->CancelActiveRequest();
		}
	}
	bOnlineRunInFlight = false;
	bOnlineMatrixRunning = false;
	bOnlineMatrixCompleted = false;
	bOnlineMatrixCancelled = bWasMatrixRunning;
	bOnlineMatrixWaitingForPlayback = false;
	OnlineMatrixPlaybackWaitStartedAt = 0.0;
	OnlineMatrixPlaybackIdleSince = 0.0;
	ActiveOnlineRequestId.Invalidate();
	RestoreOnlineDialogueSettings();
	if (bWasMatrixRunning)
	{
		if (ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent())
		{
			Motion->StopAllMotions();
		}
	}
	ResetOnlineMatrixContext();
	if (bWasMatrixRunning)
	{
		HandleSaveReport();
		if (LastSavedReportPath.IsEmpty())
		{
			SetStatus(
				LOCTEXT("OnlineMatrixPartialReportFailed", "N7-F matrix was cancelled and its partial report could not be saved"),
				true
			);
			return FReply::Handled();
		}
		SetStatus(
			FText::Format(
				LOCTEXT(
					"OnlineMatrixCancelled",
					"N7-F matrix cancelled after {0}/{1} cases. Partial report: {2}"
				),
				FText::AsNumber(OnlineMatrixCaseIndex),
				FText::AsNumber(OnlineMatrixCases.Num()),
				FText::FromString(LastSavedReportPath)
			),
			false
		);
	}
	else
	{
		SetStatus(LOCTEXT("OnlineEvaluationCancelled", "Online evaluation cancelled"), false);
	}
	return FReply::Handled();
}

FReply SLLMNPCMotionTestConsole::HandleSaveReport()
{
	LastSavedReportPath.Reset();
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.forward_n0_motion_test_report.v1"));
	Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(
		TEXT("human_review"),
		OnlineMatrixCases.IsEmpty()
			? FString(TEXT("pending"))
			: OnlineMatrixHumanReview
	);
	if (OnlineMatrixHumanReviewedAtUtc.GetTicks() > 0)
	{
		Root->SetStringField(
			TEXT("human_reviewed_at_utc"),
			OnlineMatrixHumanReviewedAtUtc.ToIso8601()
		);
		Root->SetStringField(TEXT("human_review_source"), TEXT("editor_user"));
	}

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
		Event->SetNumberField(
			TEXT("resolved_reach_scale"),
			Record.ResolvedReachScale
		);
		Event->SetNumberField(
			TEXT("resolved_height_scale"),
			Record.ResolvedHeightScale
		);
		Event->SetNumberField(
			TEXT("resolved_gaze_engagement"),
			Record.ResolvedGazeEngagement
		);
		Event->SetNumberField(
			TEXT("resolved_palm_orientation_weight"),
			Record.ResolvedPalmOrientationWeight
		);
		Event->SetNumberField(
			TEXT("resolved_finger_pose_weight"),
			Record.ResolvedFingerPoseWeight
		);
		Event->SetNumberField(
			TEXT("resolved_torso_participation"),
			Record.ResolvedTorsoParticipation
		);
		Event->SetBoolField(TEXT("resolved_mirror"), Record.bResolvedMirror);
		Event->SetStringField(
			TEXT("execution_movement_mode"),
			Record.ExecutionMovementMode
		);
		Event->SetNumberField(
			TEXT("target_distance_cm"),
			Record.TargetDistanceCm
		);
		Event->SetNumberField(
			TEXT("target_height_relative_cm"),
			Record.TargetHeightRelativeCm
		);
		Event->SetNumberField(
			TEXT("available_space"),
			Record.AvailableSpace
		);
		Event->SetBoolField(
			TEXT("right_hand_occupied"),
			Record.bRightHandOccupied
		);
		Event->SetBoolField(
			TEXT("left_hand_occupied"),
			Record.bLeftHandOccupied
		);
		Event->SetStringField(
			TEXT("modifier_result_code"),
			Record.ModifierResultCode.ToString()
		);
		Event->SetStringField(
			TEXT("modifier_resolution_trace"),
			Record.ModifierResolutionTrace
		);
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
		Event->SetBoolField(TEXT("response_schema_valid"), Record.bResponseSchemaValid);
		Event->SetStringField(TEXT("request_schema_version"), Record.RequestSchemaVersion);
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
		TArray<TSharedPtr<FJsonValue>> Exclusions;
		for (const FLLMNPCCandidateExclusion& Exclusion : Record.CandidateExclusions)
		{
			TSharedRef<FJsonObject> ExclusionObject = MakeShared<FJsonObject>();
			ExclusionObject->SetStringField(
				TEXT("selection_id"),
				Exclusion.SelectionId.ToString()
			);
			ExclusionObject->SetStringField(
				TEXT("reason"),
				Exclusion.Reason.ToString()
			);
			Exclusions.Add(MakeShared<FJsonValueObject>(ExclusionObject));
		}
		Event->SetArrayField(TEXT("candidate_exclusions"), Exclusions);
		Event->SetArrayField(TEXT("active_states"), NamesToJsonValues(Record.ActiveStates));
		Event->SetStringField(TEXT("context_emotion"), Record.ContextEmotion.ToString());
		Event->SetArrayField(
			TEXT("available_target_refs"),
			StringsToJsonValues(Record.AvailableTargetRefs)
		);
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
		Event->SetBoolField(TEXT("playback_required"), Record.bPlaybackRequired);
		Event->SetBoolField(TEXT("playback_observed"), Record.bPlaybackObserved);
		Event->SetBoolField(TEXT("playback_completed"), Record.bPlaybackCompleted);
		Event->SetNumberField(TEXT("playback_wait_seconds"), Record.PlaybackWaitSeconds);
		Event->SetStringField(TEXT("playback_wait_result"), Record.PlaybackWaitResult);
		Event->SetBoolField(TEXT("matrix_case"), Record.bMatrixCase);
		if (Record.bMatrixCase)
		{
			Event->SetStringField(
				TEXT("matrix_schema_version"),
				LLMNPCForwardN7Evaluation::GetMatrixSchemaVersion()
			);
			Event->SetStringField(TEXT("matrix_case_id"), Record.MatrixCaseId.ToString());
			Event->SetStringField(
				TEXT("expected_selection"),
				LLMNPCForwardN7Evaluation::ExpectedSelectionToString(
					Record.ExpectedSelection
				)
			);
			Event->SetStringField(TEXT("expected_action_id"), Record.ExpectedActionId.ToString());
			Event->SetStringField(TEXT("expected_target_ref"), Record.ExpectedTargetRef);
			Event->SetBoolField(TEXT("check_expected_mirror"), Record.bCheckExpectedMirror);
			Event->SetBoolField(TEXT("expected_mirror"), Record.bExpectedMirror);
			Event->SetBoolField(TEXT("check_expected_style"), Record.bCheckExpectedStyle);
			Event->SetStringField(TEXT("expected_style"), Record.ExpectedStyle.ToString());
			Event->SetArrayField(
				TEXT("expected_exclusion_reasons"),
				NamesToJsonValues(Record.ExpectedExclusionReasons)
			);
			Event->SetArrayField(TEXT("coverage_tags"), NamesToJsonValues(Record.CoverageTags));
			Event->SetBoolField(TEXT("provider_passed"), Record.bProviderPassed);
			Event->SetBoolField(TEXT("schema_passed"), Record.bSchemaPassed);
			Event->SetBoolField(TEXT("selection_passed"), Record.bSelectionPassed);
			Event->SetBoolField(TEXT("execution_passed"), Record.bExecutionPassed);
			Event->SetBoolField(TEXT("context_passed"), Record.bContextPassed);
			Event->SetBoolField(TEXT("style_passed"), Record.bStylePassed);
			Event->SetBoolField(TEXT("validator_passed"), Record.bValidatorPassed);
			Event->SetStringField(TEXT("failure_reason"), Record.FailureReason);
		}
		Event->SetStringField(TEXT("visual_review"), Record.VisualReview);
		if (Record.VisualReview != TEXT("pending") && OnlineMatrixHumanReviewedAtUtc.GetTicks() > 0)
		{
			Event->SetStringField(
				TEXT("visual_reviewed_at_utc"),
				OnlineMatrixHumanReviewedAtUtc.ToIso8601()
			);
			Event->SetStringField(TEXT("visual_review_source"), TEXT("editor_user"));
		}
		OnlineEvaluations.Add(MakeShared<FJsonValueObject>(Event));
	}
	Root->SetArrayField(TEXT("online_evaluations"), OnlineEvaluations);

	if (ULLMNPCTemplateLibrarySubsystem* Library = GetTemplateLibrary())
	{
		const FLLMNPCForwardN7LibraryAudit Audit =
			LLMNPCForwardN7Evaluation::AuditMannyLibrary(*Library);
		TSharedRef<FJsonObject> AuditObject = MakeShared<FJsonObject>();
		AuditObject->SetStringField(
			TEXT("schema_version"),
			LLMNPCForwardN7Evaluation::GetLibraryAuditSchemaVersion()
		);
		AuditObject->SetStringField(TEXT("skeleton_profile_id"), Audit.SkeletonProfileId.ToString());
		AuditObject->SetBoolField(TEXT("passed"), Audit.bPassed);
		AuditObject->SetNumberField(TEXT("public_action_count"), Audit.PublicActionCount);
		AuditObject->SetNumberField(TEXT("published_template_count"), Audit.PublishedTemplateCount);
		AuditObject->SetArrayField(TEXT("public_action_ids"), NamesToJsonValues(Audit.PublicActionIds));
		AuditObject->SetArrayField(TEXT("published_template_ids"), NamesToJsonValues(Audit.PublishedTemplateIds));
		AuditObject->SetArrayField(TEXT("missing_public_action_ids"), NamesToJsonValues(Audit.MissingPublicActionIds));
		AuditObject->SetArrayField(TEXT("unexpected_public_action_ids"), NamesToJsonValues(Audit.UnexpectedPublicActionIds));
		AuditObject->SetArrayField(TEXT("actions_without_manny_template"), NamesToJsonValues(Audit.ActionsWithoutMannyTemplate));
		AuditObject->SetArrayField(TEXT("non_manny_template_ids"), NamesToJsonValues(Audit.NonMannyTemplateIds));
		AuditObject->SetArrayField(TEXT("incomplete_candidate_ids"), NamesToJsonValues(Audit.IncompleteCandidateIds));
		AuditObject->SetBoolField(TEXT("wave_has_style_variants"), Audit.bWaveHasStyleVariants);
		AuditObject->SetBoolField(TEXT("clap_has_animation_asset"), Audit.bClapHasAnimationAsset);
		AuditObject->SetBoolField(TEXT("clap_has_procedural_variant"), Audit.bClapHasProceduralVariant);
		AuditObject->SetBoolField(TEXT("all_templates_published"), Audit.bAllTemplatesPublished);
		AuditObject->SetArrayField(TEXT("errors"), StringsToJsonValues(Audit.Errors));
		AuditObject->SetArrayField(TEXT("warnings"), StringsToJsonValues(Audit.Warnings));
		Root->SetObjectField(TEXT("forward_n7_library_audit"), AuditObject);
	}

	TSharedRef<FJsonObject> Matrix = MakeShared<FJsonObject>();
	Matrix->SetStringField(
		TEXT("schema_version"),
		LLMNPCForwardN7Evaluation::GetMatrixSchemaVersion()
	);
	Matrix->SetStringField(
		TEXT("status"),
		bOnlineMatrixRunning
			? TEXT("running")
			: (bOnlineMatrixCompleted
				? TEXT("complete")
				: (bOnlineMatrixCancelled ? TEXT("cancelled") : TEXT("not_run")))
	);
	Matrix->SetNumberField(TEXT("total_case_count"), OnlineMatrixCases.Num());
	int32 MatrixCompletedCount = 0;
	int32 MatrixPassedCount = 0;
	int32 MatrixSchemaSuccessCount = 0;
	int32 IllegalCandidateCount = 0;
	int32 UnnecessaryActionCount = 0;
	int32 MissedActionCount = 0;
	int32 TargetErrorCount = 0;
	int32 ValidatorRejectCount = 0;
	int32 ExecutionCompleteCount = 0;
	int32 PlaybackRequiredCount = 0;
	int32 PlaybackObservedCount = 0;
	int32 PlaybackCompletedCount = 0;
	int32 PlaybackTimeoutCount = 0;
	int32 TotalAttempts = 0;
	int64 TotalTokens = 0;
	int32 TokenSampleCount = 0;
	double LatencySum = 0.0;
	TArray<float> Latencies;
	TSet<FName> ExpectedCoverage;
	TSet<FName> PassedCoverage;
	TSet<FName> SelectedActions;
	TArray<FName> CompletedCaseIds;
	TArray<FName> FailedCaseIds;
	for (const FLLMNPCForwardN7MatrixCase& TestCase : OnlineMatrixCases)
	{
		for (const FName CoverageTag : TestCase.CoverageTags)
		{
			ExpectedCoverage.Add(CoverageTag);
		}
	}
	for (const FLLMNPCOnlineEvaluationRecord& Record : OnlineEvaluationRecords)
	{
		if (!Record.bMatrixCase)
		{
			continue;
		}
		++MatrixCompletedCount;
		MatrixPassedCount += Record.bPassed ? 1 : 0;
		MatrixSchemaSuccessCount += Record.bResponseSchemaValid ? 1 : 0;
		IllegalCandidateCount +=
			!Record.SelectedActionId.IsNone() &&
			!Record.OfferedCandidateIds.Contains(Record.SelectedActionId)
				? 1
				: 0;
		UnnecessaryActionCount +=
			Record.ExpectedSelection == ELLMNPCForwardN7ExpectedSelection::NoAction &&
			!Record.SelectedActionId.IsNone()
				? 1
				: 0;
		MissedActionCount +=
			Record.ExpectedSelection == ELLMNPCForwardN7ExpectedSelection::ExactAction &&
			Record.SelectedActionId.IsNone()
				? 1
				: 0;
		TargetErrorCount +=
			!Record.ExpectedTargetRef.IsEmpty() &&
			Record.TargetRef != Record.ExpectedTargetRef
				? 1
				: 0;
		ValidatorRejectCount +=
			Record.ExpectedSelection == ELLMNPCForwardN7ExpectedSelection::ExactAction &&
			!Record.bValidatorPassed
				? 1
				: 0;
		ExecutionCompleteCount += Record.bExecutionPassed ? 1 : 0;
		if (Record.bPlaybackRequired)
		{
			++PlaybackRequiredCount;
			PlaybackObservedCount += Record.bPlaybackObserved ? 1 : 0;
			PlaybackCompletedCount += Record.bPlaybackCompleted ? 1 : 0;
		}
		PlaybackTimeoutCount += Record.PlaybackWaitResult == TEXT("timeout") ? 1 : 0;
		TotalAttempts += Record.AttemptCount;
		if (Record.TotalTokens >= 0)
		{
			TotalTokens += Record.TotalTokens;
			++TokenSampleCount;
		}
		if (Record.TotalLatencySeconds >= 0.0f)
		{
			LatencySum += Record.TotalLatencySeconds;
			Latencies.Add(Record.TotalLatencySeconds);
		}
		CompletedCaseIds.Add(Record.MatrixCaseId);
		if (Record.bPassed)
		{
			for (const FName CoverageTag : Record.CoverageTags)
			{
				PassedCoverage.Add(CoverageTag);
			}
		}
		else
		{
			FailedCaseIds.Add(Record.MatrixCaseId);
		}
		if (!Record.SelectedActionId.IsNone())
		{
			SelectedActions.Add(Record.SelectedActionId);
		}
	}
	Latencies.Sort();
	const int32 P95Index = Latencies.IsEmpty()
		? INDEX_NONE
		: FMath::Clamp(
			FMath::CeilToInt(0.95f * Latencies.Num()) - 1,
			0,
			Latencies.Num() - 1
		);
	TArray<FName> MissingCoverage;
	for (const FName ExpectedTag : ExpectedCoverage)
	{
		if (!PassedCoverage.Contains(ExpectedTag))
		{
			MissingCoverage.Add(ExpectedTag);
		}
	}
	TArray<FName> ExpectedCoverageArray = ExpectedCoverage.Array();
	TArray<FName> PassedCoverageArray = PassedCoverage.Array();
	TArray<FName> SelectedActionArray = SelectedActions.Array();
	ExpectedCoverageArray.Sort(FNameLexicalLess());
	PassedCoverageArray.Sort(FNameLexicalLess());
	MissingCoverage.Sort(FNameLexicalLess());
	SelectedActionArray.Sort(FNameLexicalLess());
	CompletedCaseIds.Sort(FNameLexicalLess());
	FailedCaseIds.Sort(FNameLexicalLess());
	const double CompletedDenominator = FMath::Max(MatrixCompletedCount, 1);
	Matrix->SetNumberField(TEXT("completed_case_count"), MatrixCompletedCount);
	Matrix->SetNumberField(TEXT("passed_case_count"), MatrixPassedCount);
	Matrix->SetNumberField(TEXT("failed_case_count"), MatrixCompletedCount - MatrixPassedCount);
	Matrix->SetBoolField(
		TEXT("all_cases_passed"),
		bOnlineMatrixCompleted &&
		MatrixCompletedCount == OnlineMatrixCases.Num() &&
		MatrixPassedCount == MatrixCompletedCount
	);
	Matrix->SetNumberField(TEXT("pass_rate"), MatrixPassedCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("schema_success_rate"), MatrixSchemaSuccessCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("illegal_candidate_rate"), IllegalCandidateCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("unnecessary_action_rate"), UnnecessaryActionCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("missed_action_rate"), MissedActionCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("target_error_rate"), TargetErrorCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("validator_reject_rate"), ValidatorRejectCount / CompletedDenominator);
	Matrix->SetNumberField(TEXT("execution_complete_rate"), ExecutionCompleteCount / CompletedDenominator);
	const double PlaybackDenominator = FMath::Max(PlaybackRequiredCount, 1);
	Matrix->SetNumberField(TEXT("playback_required_case_count"), PlaybackRequiredCount);
	Matrix->SetNumberField(TEXT("playback_observed_case_count"), PlaybackObservedCount);
	Matrix->SetNumberField(TEXT("playback_completed_case_count"), PlaybackCompletedCount);
	Matrix->SetNumberField(TEXT("playback_timeout_count"), PlaybackTimeoutCount);
	Matrix->SetNumberField(TEXT("playback_observation_rate"), PlaybackObservedCount / PlaybackDenominator);
	Matrix->SetNumberField(TEXT("playback_completion_rate"), PlaybackCompletedCount / PlaybackDenominator);
	Matrix->SetNumberField(TEXT("distinct_selected_action_count"), SelectedActionArray.Num());
	Matrix->SetNumberField(
		TEXT("average_latency_seconds"),
		Latencies.IsEmpty() ? -1.0 : LatencySum / Latencies.Num()
	);
	Matrix->SetNumberField(
		TEXT("p95_latency_seconds"),
		P95Index == INDEX_NONE ? -1.0 : Latencies[P95Index]
	);
	Matrix->SetNumberField(TEXT("total_attempt_count"), TotalAttempts);
	Matrix->SetNumberField(TEXT("total_tokens"), TokenSampleCount > 0 ? TotalTokens : -1);
	Matrix->SetNumberField(TEXT("token_sample_count"), TokenSampleCount);
	Matrix->SetArrayField(TEXT("expected_coverage_tags"), NamesToJsonValues(ExpectedCoverageArray));
	Matrix->SetArrayField(TEXT("passed_coverage_tags"), NamesToJsonValues(PassedCoverageArray));
	Matrix->SetArrayField(TEXT("missing_coverage_tags"), NamesToJsonValues(MissingCoverage));
	Matrix->SetArrayField(TEXT("selected_action_ids"), NamesToJsonValues(SelectedActionArray));
	Matrix->SetArrayField(TEXT("completed_case_ids"), NamesToJsonValues(CompletedCaseIds));
	Matrix->SetArrayField(TEXT("failed_case_ids"), NamesToJsonValues(FailedCaseIds));
	Matrix->SetStringField(TEXT("human_review"), OnlineMatrixHumanReview);
	if (OnlineMatrixHumanReviewedAtUtc.GetTicks() > 0)
	{
		Matrix->SetStringField(
			TEXT("human_reviewed_at_utc"),
			OnlineMatrixHumanReviewedAtUtc.ToIso8601()
		);
		Matrix->SetStringField(TEXT("human_review_source"), TEXT("editor_user"));
	}
	Root->SetObjectField(TEXT("forward_n7_selection_matrix"), Matrix);

	FString Json;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, Json))
	{
		SetStatus(LOCTEXT("ReportSerializeFailed", "Motion test report serialization failed"), true);
		return FReply::Handled();
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		OnlineMatrixCases.IsEmpty()
			? TEXT("LLMNPCActionLayer/ForwardN0/Reports")
			: TEXT("LLMNPCActionLayer/ForwardN7/Reports")
	);
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Timestamp =
		FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString HumanReviewSuffix =
		OnlineMatrixHumanReview == TEXT("pending")
			? FString()
			: FString::Printf(TEXT("_human_%s"), *OnlineMatrixHumanReview);
	const FString ReportFilename = OnlineMatrixCases.IsEmpty()
		? FString::Printf(TEXT("motion_test_%s.json"), *Timestamp)
		: FString::Printf(
			TEXT("selection_matrix_%s%s.json"),
			*Timestamp,
			*HumanReviewSuffix
		);
	const FString ReportPath = FPaths::Combine(
		Directory,
		ReportFilename
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
	LastSavedReportPath = ReportPath;

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
