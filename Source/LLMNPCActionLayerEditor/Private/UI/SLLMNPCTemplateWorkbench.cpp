#include "UI/SLLMNPCTemplateWorkbench.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformTime.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCSettings.h"
#include "Misc/MessageDialog.h"
#include "Misc/SecureHash.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Protocol/LLMNPCTurnRequestV3Adapter.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SLLMNPCTemplateWorkbench"

namespace
{
FString WorkbenchReviewStateToString(ELLMNPCTemplateReviewState State)
{
	switch (State)
	{
	case ELLMNPCTemplateReviewState::Draft:
		return TEXT("Draft");
	case ELLMNPCTemplateReviewState::Generated:
		return TEXT("Generated");
	case ELLMNPCTemplateReviewState::Previewed:
		return TEXT("Previewed");
	case ELLMNPCTemplateReviewState::HumanApproved:
		return TEXT("HumanApproved");
	case ELLMNPCTemplateReviewState::Published:
		return TEXT("Published");
	case ELLMNPCTemplateReviewState::Deprecated:
		return TEXT("Deprecated");
	case ELLMNPCTemplateReviewState::Rejected:
		return TEXT("Rejected");
	default:
		return TEXT("Unknown");
	}
}

FString WorkbenchJoinNames(const TArray<FName>& Names)
{
	TArray<FString> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(Name.ToString());
	}
	return FString::Join(Values, TEXT(", "));
}

FString WorkbenchJoinStrings(const TArray<FString>& Values)
{
	return FString::Join(Values, TEXT("; "));
}

FString WorkbenchItemKindToString(ELLMNPCTemplateWorkbenchItemKind Kind)
{
	return Kind == ELLMNPCTemplateWorkbenchItemKind::PublicAction
		? TEXT("Action")
		: TEXT("Template");
}

bool WorkbenchReadLatestRejectionFeedback(
	const ULLMNPCMotionTemplate& Template,
	FString& OutFeedback
)
{
	OutFeedback.Reset();
	TSharedPtr<FJsonObject> Provenance;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(
			Template.SourceProvenanceJson
		);
	if (
		!FJsonSerializer::Deserialize(Reader, Provenance) ||
		!Provenance.IsValid()
	)
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* HumanReview = nullptr;
	FString Event;
	return
		Provenance->TryGetObjectField(
			TEXT("human_review"),
			HumanReview
		) &&
		HumanReview &&
		HumanReview->IsValid() &&
		(*HumanReview)->TryGetStringField(TEXT("event"), Event) &&
		Event == TEXT("rejected") &&
		(*HumanReview)->TryGetStringField(
			TEXT("notes"),
			OutFeedback
		) &&
		!OutFeedback.TrimStartAndEnd().IsEmpty();
}

class SLLMNPCCatalogTableRow final
	: public SMultiColumnTableRow<TSharedPtr<FLLMNPCTemplateWorkbenchItem>>
{
public:
	SLATE_BEGIN_ARGS(SLLMNPCCatalogTableRow) {}
		SLATE_ARGUMENT(TSharedPtr<FLLMNPCTemplateWorkbenchItem>, Item)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<STableViewBase>& OwnerTable
	)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FLLMNPCTemplateWorkbenchItem>>::Construct(
			FSuperRowType::FArguments().Padding(FMargin(4.0f, 3.0f)),
			OwnerTable
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(
		const FName& ColumnName
	) override
	{
		FString Value;
		if (!Item.IsValid())
		{
			return SNew(STextBlock);
		}
		if (ColumnName == TEXT("Type"))
		{
			Value = WorkbenchItemKindToString(Item->Kind);
		}
		else if (ColumnName == TEXT("Id"))
		{
			Value = Item->LogicalId.ToString();
		}
		else if (ColumnName == TEXT("State"))
		{
			Value = WorkbenchReviewStateToString(Item->ReviewState);
		}
		else if (ColumnName == TEXT("Profile"))
		{
			Value = Item->Kind == ELLMNPCTemplateWorkbenchItemKind::PublicAction
				? TEXT("All Published Variants")
				: Item->SkeletonProfileId.ToString();
		}
		else if (ColumnName == TEXT("Version"))
		{
			Value = FString::Printf(
				TEXT("%s r%d"),
				*Item->Version,
				Item->Revision
			);
		}
		else if (ColumnName == TEXT("Description"))
		{
			Value = Item->Description;
		}
		return SNew(STextBlock)
			.Text(FText::FromString(Value))
			.ToolTipText(FText::FromString(Value));
	}

private:
	TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item;
};
}

void SLLMNPCTemplateWorkbench::Construct(const FArguments& InArgs)
{
	static_cast<void>(InArgs);
	ReviewerBox.Reset();
	ReviewNotesBox.Reset();
	AuthoringModelClient = MakeShared<FLLMNPCAuthoringModelClient>();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(10.0f)
		[
			SAssignNew(PageSwitcher, SWidgetSwitcher)
			.WidgetIndex_Lambda(
				[this]()
				{
					return static_cast<int32>(ActivePage);
				}
			)
			+ SWidgetSwitcher::Slot()
			[
				BuildLibraryPage()
			]
			+ SWidgetSwitcher::Slot()
			[
				BuildImportPage()
			]
			+ SWidgetSwitcher::Slot()
			[
				BuildGeneratePage()
			]
			+ SWidgetSwitcher::Slot()
			[
				BuildPreviewPage()
			]
			+ SWidgetSwitcher::Slot()
			[
				BuildQualityPage()
			]
			+ SWidgetSwitcher::Slot()
			[
				BuildReviewPage()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return StatusText; })
			.ColorAndOpacity(this, &SLLMNPCTemplateWorkbench::GetStatusColor)
		]
	];

	RefreshCatalog();
	RefreshPIEActors();
	UpdateSandboxSummary();
}

SLLMNPCTemplateWorkbench::~SLLMNPCTemplateWorkbench()
{
	CancelActiveSandboxPreview(TEXT("workbench_closed"));
	if (
		AuthoringModelClient.IsValid() &&
		ActiveAuthoringRequestId.IsValid()
	)
	{
		AuthoringModelClient->Cancel(ActiveAuthoringRequestId);
	}
}

void SLLMNPCTemplateWorkbench::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(ReferencedAssets);
}

FString SLLMNPCTemplateWorkbench::GetReferencerName() const
{
	return TEXT("SLLMNPCTemplateWorkbench");
}

void SLLMNPCTemplateWorkbench::Tick(
	const FGeometry& AllottedGeometry,
	double InCurrentTime,
	float InDeltaTime
)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ActorRefreshAccumulator += InDeltaTime;
	if (
		(
			ActivePage == ELLMNPCTemplateWorkbenchPage::Preview ||
			ActivePage == ELLMNPCTemplateWorkbenchPage::Generate
		) &&
		ActorRefreshAccumulator >= 1.0f
	)
	{
		ActorRefreshAccumulator = 0.0f;
		RefreshPIEActors();
	}

	if (!ActiveSandboxClipId.IsEmpty())
	{
		ULLMNPCMotionComponent* Motion = ActiveSandboxMotion.Get();
		if (!Motion || !Motion->GetWorld() || !Motion->GetWorld()->IsGameWorld())
		{
			ActiveSandboxMotion.Reset();
			ActiveSandboxClipId.Reset();
			SandboxReport.Outcome = TEXT("preview_actor_lost");
			SandboxReport.ErrorCode =
				TEXT("LLMNPC_AUTHORING_SANDBOX_PREVIEW_ACTOR_LOST");
			SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
			SaveSandboxReport();
			UpdateSandboxSummary();
		}
		else if (Motion->IsMotionClipPendingOrActive(ActiveSandboxClipId))
		{
			const ULLMNPCSettings* Settings =
				GetDefault<ULLMNPCSettings>();
			const double WatchdogSeconds = Settings
				? Settings->AuthoringSandboxPreviewWatchdogSeconds
				: 8.0;
			if (
				SandboxPreviewStartedAtSeconds > 0.0 &&
				FPlatformTime::Seconds() -
					SandboxPreviewStartedAtSeconds >
					WatchdogSeconds
			)
			{
				SandboxReport.ErrorCode =
					TEXT("LLMNPC_AUTHORING_SANDBOX_PREVIEW_TIMEOUT");
				CancelActiveSandboxPreview(TEXT("preview_timeout"));
				SetStatus(
					LOCTEXT(
						"SandboxPreviewTimeout",
						"Sandbox preview exceeded its watchdog and was cancelled."
					),
					true
				);
			}
		}
		else if (SandboxReport.Outcome == TEXT("previewing"))
		{
			ActiveSandboxMotion.Reset();
			ActiveSandboxClipId.Reset();
			SandboxPreviewStartedAtSeconds = 0.0;
			SandboxReport.Outcome = TEXT("preview_completed");
			SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
			SaveSandboxReport();
			UpdateSandboxSummary();
		}
	}
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(10.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 14.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WorkbenchTitle", "LLM NPC Template Workbench"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Library,
					LOCTEXT("LibraryPage", "Library"),
					TEXT("Icons.FolderOpen")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Import,
					LOCTEXT("ImportPage", "Import"),
					TEXT("Icons.Import")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Generate,
					LOCTEXT("GeneratePage", "Generate"),
					TEXT("Icons.Plus")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Preview,
					LOCTEXT("PreviewPage", "Preview"),
					TEXT("Icons.Play")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Quality,
					LOCTEXT("QualityPage", "Quality"),
					TEXT("Icons.Check")
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildPageButton(
					ELLMNPCTemplateWorkbenchPage::Review,
					LOCTEXT("ReviewPage", "Review"),
					TEXT("Icons.Edit")
				)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SLLMNPCTemplateWorkbench::GetCatalogSummaryText)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("RefreshTooltip", "Reload catalog assets and diagnostics"))
				.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleRefresh)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Refresh"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(5.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("Refresh", "Refresh"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildPageButton(
	ELLMNPCTemplateWorkbenchPage Page,
	const FText& Label,
	const FName& IconName
)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.IsChecked_Lambda(
			[this, Page]()
			{
				return GetPageCheckState(Page);
			}
		)
		.OnCheckStateChanged_Lambda(
			[this, Page](ECheckBoxState State)
			{
				if (State == ECheckBoxState::Checked)
				{
					SetActivePage(Page);
				}
			}
		)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
					.Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(Label)
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildLibraryPage()
{
	return SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(0.64f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search IDs, descriptions, tags, and profiles"))
						.OnTextChanged(this, &SLLMNPCTemplateWorkbench::HandleSearchChanged)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.0f, 0.0f)
				[
					SNew(SCheckBox)
						.IsChecked(this, &SLLMNPCTemplateWorkbench::GetIncludeNonPublishedState)
						.OnCheckStateChanged(
							this,
							&SLLMNPCTemplateWorkbench::HandleIncludeNonPublishedChanged
						)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("IncludeDrafts", "Include non-Published"))
						]
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(
					ItemList,
					SListView<TSharedPtr<FLLMNPCTemplateWorkbenchItem>>
				)
				.ListItemsSource(&FilteredItems)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SLLMNPCTemplateWorkbench::GenerateItemRow)
				.OnSelectionChanged(
					this,
					&SLLMNPCTemplateWorkbench::HandleItemSelectionChanged
				)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(TEXT("Type"))
						.DefaultLabel(LOCTEXT("TypeColumn", "Type"))
						.FixedWidth(72.0f)
					+ SHeaderRow::Column(TEXT("Id"))
						.DefaultLabel(LOCTEXT("IdColumn", "Logical ID"))
						.FillWidth(0.23f)
					+ SHeaderRow::Column(TEXT("State"))
						.DefaultLabel(LOCTEXT("StateColumn", "State"))
						.FixedWidth(112.0f)
					+ SHeaderRow::Column(TEXT("Profile"))
						.DefaultLabel(LOCTEXT("ProfileColumn", "Skeleton / Variants"))
						.FillWidth(0.18f)
					+ SHeaderRow::Column(TEXT("Version"))
						.DefaultLabel(LOCTEXT("VersionColumn", "Version"))
						.FixedWidth(92.0f)
					+ SHeaderRow::Column(TEXT("Description"))
						.DefaultLabel(LOCTEXT("DescriptionColumn", "Model-facing Description"))
						.FillWidth(0.41f)
				)
			]
		]
		+ SSplitter::Slot()
		.Value(0.36f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeSectionHeader(LOCTEXT("SelectionHeader", "Selection"))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 8.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
						.Text(this, &SLLMNPCTemplateWorkbench::GetSelectedSummaryText)
						.AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
					.IsEnabled_Lambda([this]() { return SelectedItem.IsValid(); })
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleOpenAsset)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Edit"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("OpenAsset", "Open Asset"))
						]
					]
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildImportPage()
{
	FString DefaultDraftPath;
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (Plugin.IsValid())
	{
		DefaultDraftPath = FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/AuthoringExamples/DT_Clap_Manny_AnimationAsset_v1.json")
		);
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(
				LOCTEXT(
					"AnimationImportHeader",
					"Animation Asset Draft"
				)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("AnimationAssetLabel", "Animation Asset"),
				SNew(SObjectPropertyEntryBox)
					.AllowedClass(UAnimationAsset::StaticClass())
					.AllowClear(true)
					.ObjectPath_Lambda(
						[this]()
						{
							return SelectedAnimationAssetPath;
						}
					)
					.OnObjectChanged(
						this,
						&SLLMNPCTemplateWorkbench::HandleAnimationAssetChanged
					)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("AnimationDraftPathLabel", "Draft JSON"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(
						AnimationDraftPathBox,
						SEditableTextBox
					)
						.Text(FText::FromString(DefaultDraftPath))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
						.ToolTipText(
							LOCTEXT(
								"BrowseAnimationDraftTooltip",
								"Choose an Animation Template Draft JSON file"
							)
						)
						.OnClicked(
							this,
							&SLLMNPCTemplateWorkbench::HandleBrowseAnimationDraft
						)
						[
							SNew(SImage)
								.Image(
									FAppStyle::GetBrush(
										"Icons.FolderOpen"
									)
								)
						]
				]
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("AnimationDestinationLabel", "Destination"),
				SAssignNew(
					AnimationDestinationPathBox,
					SEditableTextBox
				)
					.Text(
						FText::FromString(
							TEXT("/Game/LLMNPCActionLayer/Authoring/Drafts")
						)
					)
			)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SBorder)
				.BorderImage(
					FAppStyle::GetBrush("ToolPanel.GroupBorder")
				)
				.Padding(10.0f)
				[
					SNew(STextBlock)
						.Text(
							this,
							&SLLMNPCTemplateWorkbench::GetAnimationImportSummaryText
						)
						.AutoWrapText(true)
				]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
				.IsEnabled(
					this,
					&SLLMNPCTemplateWorkbench::CanImportAnimationDraft
				)
				.OnClicked(
					this,
					&SLLMNPCTemplateWorkbench::HandleImportAnimationDraft
				)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SImage)
							.Image(
								FAppStyle::GetBrush("Icons.Import")
							)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"ImportAnimationDraft",
									"Import Generated Draft"
								)
							)
					]
				]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildGeneratePage()
{
	const FString DefaultIntent =
		TEXT("Express uncertainty with a natural bilateral shrug. Raise both shoulders together, ")
		TEXT("add subtle chest and arm participation, keep both hands relaxed and visible, ")
		TEXT("then return smoothly to neutral.");

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(
				LOCTEXT(
					"RecipeGenerateHeader",
					"Online Motion Recipe"
				)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT(
					"RecipeSkeletonProfileLabel",
					"Skeleton Profile"
				),
				SAssignNew(
					GenerateSkeletonCombo,
					SComboBox<
						TSharedPtr<
							FLLMNPCTemplateWorkbenchSkeletonOption
						>
					>
				)
					.OptionsSource(&SkeletonOptions)
					.OnGenerateWidget(
						this,
						&SLLMNPCTemplateWorkbench::GenerateSkeletonOption
					)
					.OnSelectionChanged(
						this,
						&SLLMNPCTemplateWorkbench::HandleSkeletonChanged
					)
					[
						SNew(STextBlock)
							.Text_Lambda(
								[this]()
								{
									return SelectedSkeleton.IsValid()
										? SelectedSkeleton->Label
										: LOCTEXT(
											"NoRecipeSkeleton",
											"No profile"
										);
								}
							)
					]
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT(
					"RecipeRequestSourceLabel",
					"Request Source"
				),
				SNew(STextBlock)
					.Text(
						this,
						&SLLMNPCTemplateWorkbench::
							GetRecipeRequestSourceText
					)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("RecipeIntentLabel", "Desired Action"),
				SNew(SBox)
					.HeightOverride(82.0f)
					[
						SAssignNew(
							RecipeIntentBox,
							SMultiLineEditableTextBox
						)
							.Text(FText::FromString(DefaultIntent))
							.AutoWrapText(true)
					]
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT(
					"RecipeDestinationLabel",
					"Draft Destination"
				),
				SAssignNew(
					RecipeDestinationPathBox,
					SEditableTextBox
				)
					.Text(
						FText::FromString(
							TEXT("/Game/LLMNPCActionLayer/Authoring/Drafts")
						)
					)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			MakeSectionHeader(
				LOCTEXT(
					"RuntimeSandboxHeader",
					"Authoring Runtime Sandbox"
				)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
					.IsChecked(
						this,
						&SLLMNPCTemplateWorkbench::
							GetSandboxEnabledState
					)
					.OnCheckStateChanged(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleSandboxEnabledChanged
					)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"EnableRuntimeSandbox",
									"Enable Sandbox"
								)
							)
					]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(12.0f, 0.0f, 8.0f, 0.0f)
			[
				MakeFormRow(
					LOCTEXT("SandboxActorLabel", "PIE Actor"),
					SAssignNew(
						SandboxActorCombo,
						SComboBox<
							TSharedPtr<
								FLLMNPCTemplateWorkbenchActorOption
							>
						>
					)
						.OptionsSource(&ActorOptions)
						.OnGenerateWidget(
							this,
							&SLLMNPCTemplateWorkbench::
								GenerateActorOption
						)
						.OnSelectionChanged(
							this,
							&SLLMNPCTemplateWorkbench::
								HandleActorChanged
						)
						[
							SNew(STextBlock)
								.Text_Lambda(
									[this]()
									{
										return SelectedActor.IsValid()
											? SelectedActor->Label
											: LOCTEXT(
												"NoSandboxPIEActor",
												"No PIE actor"
											);
									}
								)
						]
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanPreviewMotionRecipeInSandbox
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandlePreviewMotionRecipeInSandbox
					)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(
									FAppStyle::GetBrush("Icons.Play")
								)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(
									LOCTEXT(
										"PreviewRecipeSandbox",
										"Preflight and Preview"
									)
								)
						]
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanStopMotionRecipeSandbox
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleStopMotionRecipeSandbox
					)
					[
						SNew(SImage)
							.Image(
								FAppStyle::GetBrush("Icons.Stop")
							)
					]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 8.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanGenerateMotionRecipe
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleGenerateMotionRecipe
					)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(
									FAppStyle::GetBrush(
										"Icons.Plus"
									)
								)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(
									LOCTEXT(
										"GenerateRecipeOnline",
										"Generate Online"
									)
								)
						]
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanCancelMotionRecipeGeneration
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleCancelMotionRecipeGeneration
					)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"CancelRecipeGeneration",
									"Cancel"
								)
							)
					]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot()
			.Value(0.62f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 5.0f)
				[
					MakeSectionHeader(
						LOCTEXT(
							"GeneratedRecipeHeader",
							"Generated Recipe JSON"
						)
					)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(
						RecipeJsonBox,
						SMultiLineEditableTextBox
					)
						.AutoWrapText(false)
						.OnTextChanged(
							this,
							&SLLMNPCTemplateWorkbench::
								HandleRecipeJsonChanged
						)
				]
			]
			+ SSplitter::Slot()
			.Value(0.38f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 0.0f, 5.0f)
				[
					MakeSectionHeader(
						LOCTEXT(
							"RecipeEvidenceHeader",
							"Generation Evidence"
						)
					)
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.58f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(
						RecipeEvidenceBox,
						SMultiLineEditableTextBox
					)
						.IsReadOnly(true)
						.AutoWrapText(true)
						.Text(
							this,
							&SLLMNPCTemplateWorkbench::
								GetRecipeGenerationSummaryText
						)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 8.0f, 0.0f, 5.0f)
				[
					MakeSectionHeader(
						LOCTEXT(
							"SandboxEvidenceHeader",
							"Sandbox Evidence"
						)
					)
				]
				+ SVerticalBox::Slot()
				.FillHeight(0.42f)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(
						SandboxEvidenceBox,
						SMultiLineEditableTextBox
					)
						.IsReadOnly(true)
						.AutoWrapText(true)
						.Text(
							this,
							&SLLMNPCTemplateWorkbench::
								GetSandboxSummaryText
						)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(
					SandboxReviewNotesBox,
					SMultiLineEditableTextBox
				)
					.HintText(
						LOCTEXT(
							"SandboxReviewNotesHint",
							"Human PIE visual notes"
						)
					)
					.AutoWrapText(true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanRecordSandboxVisualReview
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleRecordSandboxVisualPass
					)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"SandboxVisualPass",
									"Visual Pass"
								)
							)
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanRecordSandboxVisualReview
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleRecordSandboxVisualFail
					)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"SandboxVisualFail",
									"Visual Fail"
								)
							)
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanCreateMotionRecipeDraft
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleCreateMotionRecipeDraft
					)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SImage)
							.Image(
								FAppStyle::GetBrush(
									"Icons.Import"
								)
							)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
							.Text(
								LOCTEXT(
									"CreateRecipeDraft",
									"Send to Generated Draft"
								)
							)
					]
				]
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildPreviewPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("CandidateCardHeader", "Turn Request v3 Candidate Card"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f)
		[
			MakeFormRow(
				LOCTEXT("SkeletonProfileLabel", "Skeleton Profile"),
				SAssignNew(
					SkeletonCombo,
					SComboBox<TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>>
				)
					.OptionsSource(&SkeletonOptions)
					.OnGenerateWidget(
						this,
						&SLLMNPCTemplateWorkbench::GenerateSkeletonOption
					)
					.OnSelectionChanged(
						this,
						&SLLMNPCTemplateWorkbench::HandleSkeletonChanged
					)
					[
						SNew(STextBlock)
							.Text_Lambda(
								[this]()
								{
									return SelectedSkeleton.IsValid()
										? SelectedSkeleton->Label
										: LOCTEXT("NoSkeleton", "No profile");
								}
							)
					]
			)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(CandidateCardBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(false)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeSectionHeader(LOCTEXT("PIEPreviewHeader", "PIE Preview"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				MakeFormRow(
					LOCTEXT("PreviewActorLabel", "Actor"),
					SAssignNew(
						ActorCombo,
						SComboBox<TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>>
					)
						.OptionsSource(&ActorOptions)
						.OnGenerateWidget(
							this,
							&SLLMNPCTemplateWorkbench::GenerateActorOption
						)
						.OnSelectionChanged(
							this,
							&SLLMNPCTemplateWorkbench::HandleActorChanged
						)
						[
							SNew(STextBlock)
								.Text_Lambda(
									[this]()
									{
										return SelectedActor.IsValid()
											? SelectedActor->Label
											: LOCTEXT("NoPIEActor", "No PIE actor");
									}
								)
						]
				)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(SButton)
					.ToolTipText(LOCTEXT("RefreshActorsTooltip", "Refresh PIE actors"))
					.OnClicked_Lambda(
						[this]()
						{
							RefreshPIEActors();
							return FReply::Handled();
						}
					)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Refresh"))
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
					.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanPreviewSelection)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandlePreview)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Play"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("PreviewTemplate", "Preview Selected"))
						]
					]
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildQualityPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("QualityHeader", "Catalog and Asset Quality"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 8.0f)
		[
			SAssignNew(QualityBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(false)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleValidate)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("ValidateSelection", "Validate Selection"))
					]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("ReconstructionPath", "Reconstruction Profile"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(ReconstructionPathBox, SEditableTextBox)
						.HintText(LOCTEXT("ReconstructionHint", "UEPI reconstruction_profile JSON"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
						.ToolTipText(LOCTEXT("BrowseReconstruction", "Choose reconstruction profile"))
						.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleBrowseReconstruction)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.FolderOpen"))
						]
				]
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("FullPosePath", "Full Pose Artifact"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(FullPosePathBox, SEditableTextBox)
						.HintText(LOCTEXT("FullPoseHint", "Optional full_pose_artifact JSON"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
						.ToolTipText(LOCTEXT("BrowseFullPose", "Choose full pose artifact"))
						.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleBrowseFullPose)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.FolderOpen"))
						]
				]
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
				.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanGenerateQualityReport)
				.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleGenerateQualityReport)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("GenerateQuality", "Generate Quality Report"))
				]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::BuildReviewPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("ReviewHeader", "Human Review and Publish"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("ReviewStateLabel", "Current State"),
				SNew(STextBlock)
					.Text(this, &SLLMNPCTemplateWorkbench::GetReviewStateText)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("DestinationLabel", "Publish Destination"),
				SNew(STextBlock)
					.Text(this, &SLLMNPCTemplateWorkbench::GetReviewDestinationText)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("ReviewerLabel", "Reviewer"),
				SAssignNew(ReviewerBox, SEditableTextBox)
					.HintText(LOCTEXT("ReviewerHint", "Reviewer identity"))
			)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			MakeFormRow(
				LOCTEXT("ReviewNotesLabel", "Review Notes"),
				SAssignNew(ReviewNotesBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("ReviewNotesHint", "Observed motion, context fit, and decision"))
					.AutoWrapText(true)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
					.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanMarkPreviewed)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleMarkPreviewed)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("MarkPreviewed", "Mark Previewed"))
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(6.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanApprove)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleApprove)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("Approve", "Approve"))
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(
						this,
						&SLLMNPCTemplateWorkbench::
							CanReviseOnline
					)
					.OnClicked(
						this,
						&SLLMNPCTemplateWorkbench::
							HandleReviseOnline
					)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(
									FAppStyle::GetBrush(
										"Icons.Refresh"
									)
								)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(
									LOCTEXT(
										"ReviseOnline",
										"Revise Online"
									)
								)
						]
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
					.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanReject)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandleReject)
					[
						SNew(STextBlock)
							.Text(LOCTEXT("Reject", "Reject"))
					]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
					.IsEnabled(this, &SLLMNPCTemplateWorkbench::CanPublish)
					.OnClicked(this, &SLLMNPCTemplateWorkbench::HandlePublish)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Save"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(5.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("Publish", "Publish Copy"))
						]
					]
			]
		];
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::MakeSectionHeader(
	const FText& Label
) const
{
	return SNew(STextBlock)
		.Text(Label)
		.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"));
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::MakeFormRow(
	const FText& Label,
	const TSharedRef<SWidget>& Control
) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
				.WidthOverride(170.0f)
				[
					SNew(STextBlock)
						.Text(Label)
				]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			Control
		];
}

void SLLMNPCTemplateWorkbench::RefreshCatalog(
	const FString& PreferredAssetPath
)
{
	const FString PreviousPath = PreferredAssetPath.IsEmpty() && SelectedItem.IsValid()
		? SelectedItem->AssetPath
		: PreferredAssetPath;
	ReferencedAssets.Reset();
	AllItems.Reset();
	FilteredItems.Reset();
	SelectedItem.Reset();
	SkeletonOptions.Reset();
	SelectedSkeleton.Reset();
	CatalogIndex.Reset();

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<ULLMNPCMotionTemplate*> Templates;
	TArray<ULLMNPCPublicActionDefinition*> Definitions;
	TArray<ULLMNPCSkeletonProfile*> Profiles;

	auto LoadAssets = [&AssetRegistryModule, this](
		const UClass* AssetClass,
		auto&& AddAsset
	)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(AssetClass->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssets(Filter, Assets);
		Assets.Sort(
			[](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			}
		);
		for (const FAssetData& AssetData : Assets)
		{
			if (UObject* Asset = AssetData.GetAsset())
			{
				ReferencedAssets.Add(Asset);
				AddAsset(Asset);
			}
		}
	};

	LoadAssets(
		ULLMNPCMotionTemplate::StaticClass(),
		[&Templates](UObject* Asset)
		{
			if (ULLMNPCMotionTemplate* Template = Cast<ULLMNPCMotionTemplate>(Asset))
			{
				Templates.Add(Template);
			}
		}
	);
	LoadAssets(
		ULLMNPCPublicActionDefinition::StaticClass(),
		[&Definitions](UObject* Asset)
		{
			if (ULLMNPCPublicActionDefinition* Definition =
				Cast<ULLMNPCPublicActionDefinition>(Asset))
			{
				Definitions.Add(Definition);
			}
		}
	);
	LoadAssets(
		ULLMNPCSkeletonProfile::StaticClass(),
		[&Profiles](UObject* Asset)
		{
			if (ULLMNPCSkeletonProfile* Profile = Cast<ULLMNPCSkeletonProfile>(Asset))
			{
				Profiles.Add(Profile);
			}
		}
	);

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
	if (Vocabulary)
	{
		ReferencedAssets.AddUnique(Vocabulary);
	}
	TSet<FName> ProfileIds;
	for (ULLMNPCSkeletonProfile* Profile : Profiles)
	{
		if (!Profile || Profile->ProfileId.IsNone())
		{
			continue;
		}
		ProfileIds.Add(Profile->ProfileId);
		TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option =
			MakeShared<FLLMNPCTemplateWorkbenchSkeletonOption>();
		Option->ProfileId = Profile->ProfileId;
		Option->Label = FText::FromName(Profile->ProfileId);
		SkeletonOptions.Add(MoveTemp(Option));
	}
	SkeletonOptions.Sort(
		[](const TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>& A,
			const TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>& B)
		{
			return A->ProfileId.LexicalLess(B->ProfileId);
		}
	);
	for (const TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>& Option : SkeletonOptions)
	{
		if (Option->ProfileId.ToString().Contains(TEXT("manny"), ESearchCase::IgnoreCase))
		{
			SelectedSkeleton = Option;
			break;
		}
	}
	if (!SelectedSkeleton.IsValid() && !SkeletonOptions.IsEmpty())
	{
		SelectedSkeleton = SkeletonOptions[0];
	}
	if (SkeletonCombo)
	{
		SkeletonCombo->RefreshOptions();
		SkeletonCombo->SetSelectedItem(SelectedSkeleton);
	}
	if (GenerateSkeletonCombo)
	{
		GenerateSkeletonCombo->RefreshOptions();
		GenerateSkeletonCombo->SetSelectedItem(SelectedSkeleton);
	}

	CatalogIndex.Build(Templates, Definitions, Vocabulary, ProfileIds);

	for (ULLMNPCPublicActionDefinition* Definition : Definitions)
	{
		if (!Definition)
		{
			continue;
		}
		TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item =
			MakeShared<FLLMNPCTemplateWorkbenchItem>();
		Item->Kind = ELLMNPCTemplateWorkbenchItemKind::PublicAction;
		Item->Asset = Definition;
		Item->AssetPath = Definition->GetPathName();
		Item->LogicalId = Definition->PublicActionId;
		Item->PublicActionId = Definition->PublicActionId;
		Item->DisplayName = Definition->DisplayName.ToString();
		Item->Description = Definition->SelectionSummary;
		Item->Version = Definition->SemanticVersion;
		Item->Revision = Definition->DefinitionRevision;
		Item->ReviewState = Definition->ReviewState;
		Item->SearchText = FString::Printf(
			TEXT("%s %s %s %s %s %s"),
			*Item->LogicalId.ToString(),
			*Item->DisplayName,
			*Item->Description,
			*WorkbenchJoinStrings(Definition->SuitableWhen),
			*WorkbenchJoinStrings(Definition->AvoidWhen),
			*WorkbenchJoinStrings(Definition->SearchKeywords)
		).ToLower();
		AllItems.Add(MoveTemp(Item));
	}
	for (ULLMNPCMotionTemplate* Template : Templates)
	{
		if (!Template)
		{
			continue;
		}
		TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item =
			MakeShared<FLLMNPCTemplateWorkbenchItem>();
		Item->Kind = ELLMNPCTemplateWorkbenchItemKind::MotionTemplate;
		Item->Asset = Template;
		Item->AssetPath = Template->GetPathName();
		Item->LogicalId = Template->Metadata.TemplateId;
		Item->PublicActionId = Template->Metadata.PublicActionId;
		Item->SkeletonProfileId = Template->Metadata.SkeletonProfileId;
		Item->DisplayName = Template->Metadata.DisplayName.ToString();
		Item->Description = Template->Metadata.VisualDescription;
		Item->Version = Template->Metadata.SemanticVersion;
		Item->Revision = Template->Metadata.CatalogRevision;
		Item->ReviewState = Template->Metadata.ReviewState;
		Item->SearchText = FString::Printf(
			TEXT("%s %s %s %s %s %s %s %s"),
			*Item->LogicalId.ToString(),
			*Item->PublicActionId.ToString(),
			*Item->DisplayName,
			*Item->Description,
			*Item->SkeletonProfileId.ToString(),
			*WorkbenchJoinNames(Template->Metadata.IntentTags),
			*WorkbenchJoinNames(Template->Metadata.BodyRegionTags),
			*WorkbenchJoinNames(Template->Metadata.SemanticEffectTags)
		).ToLower();
		AllItems.Add(MoveTemp(Item));
	}
	AllItems.Sort(
		[](const TSharedPtr<FLLMNPCTemplateWorkbenchItem>& A,
			const TSharedPtr<FLLMNPCTemplateWorkbenchItem>& B)
		{
			if (A->Kind != B->Kind)
			{
				return A->Kind < B->Kind;
			}
			return A->LogicalId.LexicalLess(B->LogicalId);
		}
	);

	ApplyFilters();
	if (!PreviousPath.IsEmpty())
	{
		for (const TSharedPtr<FLLMNPCTemplateWorkbenchItem>& Item : FilteredItems)
		{
			if (Item->AssetPath == PreviousPath)
			{
				SelectedItem = Item;
				break;
			}
		}
	}
	if (!SelectedItem.IsValid() && !FilteredItems.IsEmpty())
	{
		SelectedItem = FilteredItems[0];
	}
	if (ItemList)
	{
		ItemList->RequestListRefresh();
		ItemList->SetSelection(SelectedItem);
	}
	RefreshDerivedText();
	SetStatus(
		FText::Format(
			LOCTEXT("CatalogRefreshed", "Catalog refreshed: {0} Published templates, {1} Public Actions"),
			FText::AsNumber(CatalogIndex.GetTemplateCount()),
			FText::AsNumber(CatalogIndex.GetPublicActionCount())
		),
		!CatalogIndex.GetDiagnostics().IsEmpty()
	);
}

void SLLMNPCTemplateWorkbench::ApplyFilters()
{
	const FString Needle = SearchQuery.TrimStartAndEnd().ToLower();
	FilteredItems.Reset();
	for (const TSharedPtr<FLLMNPCTemplateWorkbenchItem>& Item : AllItems)
	{
		if (
			!Item.IsValid() ||
			(!bIncludeNonPublished &&
				Item->ReviewState != ELLMNPCTemplateReviewState::Published) ||
			(!Needle.IsEmpty() && !Item->SearchText.Contains(Needle))
		)
		{
			continue;
		}
		FilteredItems.Add(Item);
	}
	if (ItemList)
	{
		ItemList->RequestListRefresh();
	}
	if (SelectedItem.IsValid() && !FilteredItems.Contains(SelectedItem))
	{
		SelectedItem = FilteredItems.IsEmpty() ? nullptr : FilteredItems[0];
		if (ItemList)
		{
			ItemList->SetSelection(SelectedItem);
		}
		RefreshDerivedText();
	}
}

void SLLMNPCTemplateWorkbench::RefreshDerivedText()
{
	CandidateCardJson = TEXT("{\n  \"candidate\": null,\n  \"reason\": \"Select a Published Public Action or template with a compatible variant.\"\n}");
	const FName PublicActionId = GetSelectedPublicActionId();
	if (
		!PublicActionId.IsNone() &&
		SelectedSkeleton.IsValid()
	)
	{
		FLLMNPCTemplateCandidate Candidate;
		if (CatalogIndex.BuildRuntimeCandidate(
			PublicActionId,
			SelectedSkeleton->ProfileId,
			Candidate
		))
		{
			CandidateCardJson =
				FLLMNPCTurnRequestV3Adapter::BuildCandidateCardPreviewJson(Candidate);
		}
	}
	if (CandidateCardBox)
	{
		CandidateCardBox->SetText(FText::FromString(CandidateCardJson));
	}

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Catalog Hash: %s"), *CatalogIndex.GetCatalogHash()));
	Lines.Add(FString::Printf(
		TEXT("Published: %d templates, %d Public Actions"),
		CatalogIndex.GetTemplateCount(),
		CatalogIndex.GetPublicActionCount()
	));
	Lines.Add(TEXT(""));

	bool bValid = false;
	FString Error = TEXT("No selection");
	if (ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		bValid = Template->ValidateTemplate(Error);
		Lines.Add(FString::Printf(
			TEXT("Template: %s"),
			*Template->Metadata.TemplateId.ToString()
		));
		Lines.Add(FString::Printf(
			TEXT("Validation: %s%s"),
			bValid ? TEXT("PASS") : TEXT("FAIL - "),
			bValid ? TEXT("") : *Error
		));
		Lines.Add(FString::Printf(
			TEXT("Description Gate: %s"),
			Template->Metadata.VisualDescription.TrimStartAndEnd().IsEmpty()
				? TEXT("FAIL")
				: TEXT("PASS")
		));
		Lines.Add(FString::Printf(
			TEXT("Catalog Content Hash: %s"),
			*Template->Metadata.CatalogContentHash
		));
		Lines.Add(TEXT(""));
		Lines.Add(TEXT("Quality Report:"));
		Lines.Add(Template->ValidationReportJson.TrimStartAndEnd().IsEmpty()
			? TEXT("<missing>")
			: Template->ValidationReportJson);
	}
	else if (ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
		ULLMNPCActionVocabulary* Vocabulary =
			Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
		bValid = Definition->ValidateDefinition(Vocabulary, Error);
		Lines.Add(FString::Printf(
			TEXT("Public Action: %s"),
			*Definition->PublicActionId.ToString()
		));
		Lines.Add(FString::Printf(
			TEXT("Validation: %s%s"),
			bValid ? TEXT("PASS") : TEXT("FAIL - "),
			bValid ? TEXT("") : *Error
		));
		Lines.Add(FString::Printf(
			TEXT("Description Gate: %s"),
			Definition->SelectionSummary.TrimStartAndEnd().IsEmpty()
				? TEXT("FAIL")
				: TEXT("PASS")
		));
		Lines.Add(FString::Printf(TEXT("Content Hash: %s"), *Definition->ContentHash));
	}

	Lines.Add(TEXT(""));
	Lines.Add(FString::Printf(
		TEXT("Catalog Diagnostics: %d"),
		CatalogIndex.GetDiagnostics().Num()
	));
	for (const FLLMNPCCatalogDiagnostic& Diagnostic : CatalogIndex.GetDiagnostics())
	{
		Lines.Add(FString::Printf(
			TEXT("- %s | %s | %s | %s"),
			*Diagnostic.Code.ToString(),
			*Diagnostic.AssetPath,
			*Diagnostic.FieldPath,
			*Diagnostic.Message
		));
	}
	QualityText = FString::Join(Lines, TEXT("\n"));
	if (QualityBox)
	{
		QualityBox->SetText(FText::FromString(QualityText));
	}
}

void SLLMNPCTemplateWorkbench::RefreshPIEActors()
{
	const FString PreviousName =
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
			TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option =
				MakeShared<FLLMNPCTemplateWorkbenchActorOption>();
			Option->MotionComponent = Motion;
			Option->Label = FText::FromString(Motion->GetOwner()->GetName());
			ActorOptions.Add(MoveTemp(Option));
		}
	}
	ActorOptions.Sort(
		[](const TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>& A,
			const TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>& B)
		{
			return A->Label.ToString() < B->Label.ToString();
		}
	);
	for (const TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>& Option : ActorOptions)
	{
		if (
			Option->MotionComponent.IsValid() &&
			Option->MotionComponent->GetOwner() &&
			Option->MotionComponent->GetOwner()->GetName() == PreviousName
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
	if (SandboxActorCombo)
	{
		SandboxActorCombo->RefreshOptions();
		SandboxActorCombo->SetSelectedItem(SelectedActor);
	}
}

void SLLMNPCTemplateWorkbench::SetActivePage(
	ELLMNPCTemplateWorkbenchPage Page
)
{
	ActivePage = Page;
	if (
		Page == ELLMNPCTemplateWorkbenchPage::Preview ||
		Page == ELLMNPCTemplateWorkbenchPage::Generate
	)
	{
		RefreshPIEActors();
	}
	RefreshDerivedText();
}

void SLLMNPCTemplateWorkbench::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusError = bError;
}

ULLMNPCMotionTemplate* SLLMNPCTemplateWorkbench::GetSelectedTemplate() const
{
	return SelectedItem.IsValid()
		? Cast<ULLMNPCMotionTemplate>(SelectedItem->Asset.Get())
		: nullptr;
}

ULLMNPCPublicActionDefinition*
SLLMNPCTemplateWorkbench::GetSelectedDefinition() const
{
	return SelectedItem.IsValid()
		? Cast<ULLMNPCPublicActionDefinition>(SelectedItem->Asset.Get())
		: nullptr;
}

ULLMNPCMotionTemplate* SLLMNPCTemplateWorkbench::ResolvePreviewTemplate() const
{
	if (ULLMNPCMotionTemplate* SelectedTemplate = GetSelectedTemplate())
	{
		if (
			!SelectedSkeleton.IsValid() ||
			SelectedTemplate->SupportsSkeletonProfile(SelectedSkeleton->ProfileId)
		)
		{
			return SelectedTemplate;
		}
		return nullptr;
	}
	if (!SelectedSkeleton.IsValid())
	{
		return nullptr;
	}
	const TArray<FName>* Variants = CatalogIndex.FindVariants(GetSelectedPublicActionId());
	if (!Variants)
	{
		return nullptr;
	}
	for (const FName VariantId : *Variants)
	{
		const ULLMNPCMotionTemplate* Variant = CatalogIndex.FindTemplate(VariantId);
		if (Variant && Variant->SupportsSkeletonProfile(SelectedSkeleton->ProfileId))
		{
			return const_cast<ULLMNPCMotionTemplate*>(Variant);
		}
	}
	return nullptr;
}

ULLMNPCMotionComponent*
SLLMNPCTemplateWorkbench::GetSelectedMotionComponent() const
{
	return SelectedActor.IsValid() ? SelectedActor->MotionComponent.Get() : nullptr;
}

ULLMNPCSkeletonProfile*
SLLMNPCTemplateWorkbench::GetSelectedSkeletonProfile() const
{
	if (!SelectedSkeleton.IsValid())
	{
		return nullptr;
	}
	for (UObject* Asset : ReferencedAssets)
	{
		if (
			ULLMNPCSkeletonProfile* Profile =
				Cast<ULLMNPCSkeletonProfile>(Asset)
		)
		{
			if (Profile->ProfileId == SelectedSkeleton->ProfileId)
			{
				return Profile;
			}
		}
	}
	return nullptr;
}

FName SLLMNPCTemplateWorkbench::GetSelectedPublicActionId() const
{
	return SelectedItem.IsValid() ? SelectedItem->PublicActionId : NAME_None;
}

FText SLLMNPCTemplateWorkbench::GetSelectedSummaryText() const
{
	if (!SelectedItem.IsValid())
	{
		return LOCTEXT("NoSelection", "No asset selected.");
	}
	if (const ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		return FText::FromString(FString::Printf(
			TEXT("%s\n\nAsset\n%s\n\nPublic Action\n%s\n\nState\n%s\n\nSkeleton Profile\n%s\n\nVariant\n%s\n\nStyles\n%s\n\nBody Regions\n%s\n\nSemantic Effects\n%s\n\nTarget Categories\n%s\n\nRequired Channels\n%s\n\nModel-facing Description\n%s"),
			*SelectedItem->DisplayName,
			*SelectedItem->AssetPath,
			*Template->Metadata.PublicActionId.ToString(),
			*WorkbenchReviewStateToString(Template->Metadata.ReviewState),
			*Template->Metadata.SkeletonProfileId.ToString(),
			*Template->Metadata.VariantId.ToString(),
			*WorkbenchJoinNames(Template->Metadata.VariantStyleTags),
			*WorkbenchJoinNames(Template->Metadata.BodyRegionTags),
			*WorkbenchJoinNames(Template->Metadata.SemanticEffectTags),
			*WorkbenchJoinNames(Template->Metadata.TargetCategoryTags),
			*WorkbenchJoinNames(Template->Metadata.RequiredChannels),
			*Template->Metadata.VisualDescription
		));
	}
	if (const ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		const TArray<FName>* Variants =
			CatalogIndex.FindVariants(Definition->PublicActionId);
		return FText::FromString(FString::Printf(
			TEXT("%s\n\nAsset\n%s\n\nState\n%s\n\nPublished Runtime Variants\n%d\n\nGesture Family\n%s\n\nDefault Style\n%s\n\nSemantic Effects\n%s\n\nTarget Categories\n%s\n\nSuitable When\n%s\n\nAvoid When\n%s\n\nSelection Summary\n%s"),
			*SelectedItem->DisplayName,
			*SelectedItem->AssetPath,
			*WorkbenchReviewStateToString(Definition->ReviewState),
			Variants ? Variants->Num() : 0,
			*Definition->GestureFamily.ToString(),
			*Definition->DefaultStyle.ToString(),
			*WorkbenchJoinNames(Definition->SemanticEffectTags),
			*WorkbenchJoinNames(Definition->TargetCategoryTags),
			*WorkbenchJoinStrings(Definition->SuitableWhen),
			*WorkbenchJoinStrings(Definition->AvoidWhen),
			*Definition->SelectionSummary
		));
	}
	return FText::FromString(SelectedItem->AssetPath);
}

FText SLLMNPCTemplateWorkbench::GetCatalogSummaryText() const
{
	return FText::Format(
		LOCTEXT("CatalogSummary", "{0} templates | {1} actions | {2} diagnostics"),
		FText::AsNumber(CatalogIndex.GetTemplateCount()),
		FText::AsNumber(CatalogIndex.GetPublicActionCount()),
		FText::AsNumber(CatalogIndex.GetDiagnostics().Num())
	);
}

FText SLLMNPCTemplateWorkbench::GetAnimationImportSummaryText() const
{
	if (SelectedAnimationAssetPath.IsEmpty())
	{
		return LOCTEXT(
			"NoAnimationAssetSelected",
			"No Animation Asset selected."
		);
	}
	UAnimationAsset* AnimationAsset =
		LoadObject<UAnimationAsset>(
			nullptr,
			*SelectedAnimationAssetPath
		);
	if (!AnimationAsset)
	{
		return FText::FromString(FString::Printf(
			TEXT("Asset\n%s\n\nStatus\nUnavailable"),
			*SelectedAnimationAssetPath
		));
	}
	const UAnimSequenceBase* Sequence =
		Cast<UAnimSequenceBase>(AnimationAsset);
	return FText::FromString(FString::Printf(
		TEXT("Asset\n%s\n\nClass\n%s\n\nSkeleton\n%s\n\nDuration\n%.3f seconds\n\nRoot Motion\n%s"),
		*AnimationAsset->GetPathName(),
		*AnimationAsset->GetClass()->GetName(),
		AnimationAsset->GetSkeleton()
			? *AnimationAsset->GetSkeleton()->GetPathName()
			: TEXT("<missing>"),
		AnimationAsset->GetPlayLength(),
		Sequence && Sequence->HasRootMotion()
			? TEXT("Present")
			: TEXT("Absent")
	));
}

FText SLLMNPCTemplateWorkbench::GetRecipeGenerationSummaryText() const
{
	if (bAuthoringRequestPending)
	{
		return LOCTEXT(
			"RecipeGenerationPending",
			"Online Authoring request is running."
		);
	}
	if (!RecipeGenerationSummary.IsEmpty())
	{
		return FText::FromString(RecipeGenerationSummary);
	}
	return LOCTEXT(
		"RecipeGenerationNotRun",
		"No Authoring request has completed."
	);
}

FText SLLMNPCTemplateWorkbench::GetRecipeRequestSourceText() const
{
	if (PendingRecipeRequestContext.IsRegeneration())
	{
		return FText::FromString(FString::Printf(
			TEXT("%s | %s"),
			LLMNPCMotionRecipeAuthoring::
				RegenerationTriggerSource,
			*PendingRecipeRequestContext.SourceTemplateId.ToString()
		));
	}
	return FText::FromString(
		LLMNPCMotionRecipeAuthoring::ManualTriggerSource
	);
}

FText SLLMNPCTemplateWorkbench::GetSandboxSummaryText() const
{
	return SandboxSummary.IsEmpty()
		? LOCTEXT(
			"SandboxNotRun",
			"No Sandbox Preflight has completed."
		)
		: FText::FromString(SandboxSummary);
}

FText SLLMNPCTemplateWorkbench::GetReviewStateText() const
{
	return SelectedItem.IsValid()
		? FText::FromString(WorkbenchReviewStateToString(SelectedItem->ReviewState))
		: LOCTEXT("NoReviewSelection", "No selection");
}

FText SLLMNPCTemplateWorkbench::GetReviewDestinationText() const
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || !SelectedItem.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(
		SelectedItem->Kind == ELLMNPCTemplateWorkbenchItemKind::PublicAction
			? Settings->ProjectPublishedPublicActionPath
			: Settings->ProjectPublishedTemplatePath
	);
}

FSlateColor SLLMNPCTemplateWorkbench::GetStatusColor() const
{
	return FSlateColor(
		bStatusError
			? FLinearColor(0.95f, 0.25f, 0.2f)
			: FLinearColor(0.3f, 0.8f, 0.45f)
	);
}

ECheckBoxState SLLMNPCTemplateWorkbench::GetPageCheckState(
	ELLMNPCTemplateWorkbenchPage Page
) const
{
	return ActivePage == Page
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

ECheckBoxState SLLMNPCTemplateWorkbench::GetIncludeNonPublishedState() const
{
	return bIncludeNonPublished
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

ECheckBoxState SLLMNPCTemplateWorkbench::GetSandboxEnabledState() const
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	return
		FLLMNPCAuthoringSandbox::IsBuildAvailable() &&
		Settings &&
		Settings->bEnableAuthoringRuntimeSandbox
			? ECheckBoxState::Checked
			: ECheckBoxState::Unchecked;
}

bool SLLMNPCTemplateWorkbench::CanPreviewSelection() const
{
	return ResolvePreviewTemplate() && GetSelectedMotionComponent();
}

bool SLLMNPCTemplateWorkbench::CanImportAnimationDraft() const
{
	return
		!SelectedAnimationAssetPath.IsEmpty() &&
		AnimationDraftPathBox.IsValid() &&
		FPaths::FileExists(
			AnimationDraftPathBox->GetText().ToString()
		) &&
		AnimationDestinationPathBox.IsValid() &&
		!AnimationDestinationPathBox->GetText()
			.ToString()
			.TrimStartAndEnd()
			.IsEmpty();
}

bool SLLMNPCTemplateWorkbench::CanGenerateMotionRecipe() const
{
	return
		!bAuthoringRequestPending &&
		AuthoringModelClient.IsValid() &&
		SelectedSkeleton.IsValid() &&
		RecipeIntentBox.IsValid() &&
		!RecipeIntentBox->GetText()
			.ToString()
			.TrimStartAndEnd()
			.IsEmpty();
}

bool SLLMNPCTemplateWorkbench::
CanCancelMotionRecipeGeneration() const
{
	return
		bAuthoringRequestPending &&
		ActiveAuthoringRequestId.IsValid() &&
		AuthoringModelClient.IsValid();
}

bool SLLMNPCTemplateWorkbench::
CanPreviewMotionRecipeInSandbox() const
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	return
		FLLMNPCAuthoringSandbox::IsBuildAvailable() &&
		Settings &&
		Settings->bEnableAuthoringRuntimeSandbox &&
		!bAuthoringRequestPending &&
		LastAuthoringResult.bSuccess &&
		!LastRecipeResponse.bUnsupported &&
		SelectedSkeleton.IsValid() &&
		RecipeJsonBox.IsValid() &&
		!RecipeJsonBox->GetText()
			.ToString()
			.TrimStartAndEnd()
			.IsEmpty() &&
		GetSelectedMotionComponent() &&
		GetSelectedSkeletonProfile();
}

bool SLLMNPCTemplateWorkbench::
CanStopMotionRecipeSandbox() const
{
	const ULLMNPCMotionComponent* Motion =
		ActiveSandboxMotion.Get();
	return
		Motion &&
		!ActiveSandboxClipId.IsEmpty() &&
		Motion->IsMotionClipPendingOrActive(
			ActiveSandboxClipId
		);
}

bool SLLMNPCTemplateWorkbench::
CanRecordSandboxVisualReview() const
{
	return
		LastSandboxPreflight.bPassed &&
		SandboxReport.bTransientPlanSubmitted &&
		SandboxReport.bDraftRecordSaved &&
		SandboxReport.RequestId.IsValid() &&
		!CanStopMotionRecipeSandbox() &&
		(
			SandboxReport.Outcome == TEXT("preview_completed") ||
			SandboxReport.Outcome == TEXT("visual_pass") ||
			SandboxReport.Outcome == TEXT("visual_fail")
		);
}

bool SLLMNPCTemplateWorkbench::CanCreateMotionRecipeDraft() const
{
	return
		!bAuthoringRequestPending &&
		LastAuthoringResult.bSuccess &&
		!LastRecipeResponse.bUnsupported &&
		!LastRecipeResponse.RecipeJson.IsEmpty() &&
		RecipeJsonBox.IsValid() &&
		!RecipeJsonBox->GetText()
			.ToString()
			.TrimStartAndEnd()
			.IsEmpty() &&
		RecipeDestinationPathBox.IsValid() &&
		!RecipeDestinationPathBox->GetText()
			.ToString()
			.TrimStartAndEnd()
			.IsEmpty() &&
		LastSandboxPreflight.bPassed &&
		SandboxReport.bTransientPlanSubmitted &&
		SandboxReport.bDraftRecordSaved &&
		SandboxReport.HumanVisualDecision == TEXT("pass");
}

bool SLLMNPCTemplateWorkbench::CanGenerateQualityReport() const
{
	const ULLMNPCMotionTemplate* Template = GetSelectedTemplate();
	return
		!bAuthoringRequestPending &&
		Template &&
		!Template->IsPublished() &&
		(
			Template->Kind == ELLMNPCTemplateKind::AnimationAsset ||
			!Template->Metadata.SourceRecipeHash
				.TrimStartAndEnd()
				.IsEmpty() ||
			(
				ReconstructionPathBox.IsValid() &&
				!ReconstructionPathBox->GetText()
					.ToString()
					.TrimStartAndEnd()
					.IsEmpty()
			)
		);
}

bool SLLMNPCTemplateWorkbench::CanMarkPreviewed() const
{
	if (const ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		return Template->Metadata.ReviewState == ELLMNPCTemplateReviewState::Generated;
	}
	if (const ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		return
			Definition->ReviewState == ELLMNPCTemplateReviewState::Draft ||
			Definition->ReviewState == ELLMNPCTemplateReviewState::Generated;
	}
	return false;
}

bool SLLMNPCTemplateWorkbench::CanApprove() const
{
	return SelectedItem.IsValid() &&
		SelectedItem->ReviewState == ELLMNPCTemplateReviewState::Previewed;
}

bool SLLMNPCTemplateWorkbench::CanReject() const
{
	return SelectedItem.IsValid() &&
		SelectedItem->ReviewState != ELLMNPCTemplateReviewState::Published &&
		SelectedItem->ReviewState != ELLMNPCTemplateReviewState::Rejected;
}

bool SLLMNPCTemplateWorkbench::CanReviseOnline() const
{
	const ULLMNPCMotionTemplate* Template = GetSelectedTemplate();
	return
		!bAuthoringRequestPending &&
		Template &&
		Template->Kind ==
			ELLMNPCTemplateKind::ProceduralMotion &&
		!Template->Metadata.SourceRecipeHash.IsEmpty() &&
		Template->Metadata.PublicActionId ==
			TEXT("gesture.shrug") &&
		Template->Metadata.SkeletonProfileId ==
			TEXT("ue5_manny.v1") &&
		!Template->IsPublished() &&
		Template->Metadata.ReviewState !=
			ELLMNPCTemplateReviewState::Deprecated;
}

bool SLLMNPCTemplateWorkbench::CanPublish() const
{
	return SelectedItem.IsValid() &&
		SelectedItem->ReviewState == ELLMNPCTemplateReviewState::HumanApproved;
}

TSharedRef<ITableRow> SLLMNPCTemplateWorkbench::GenerateItemRow(
	TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable
)
{
	return SNew(SLLMNPCCatalogTableRow, OwnerTable)
		.Item(Item);
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::GenerateSkeletonOption(
	TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option
) const
{
	return SNew(STextBlock)
		.Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

TSharedRef<SWidget> SLLMNPCTemplateWorkbench::GenerateActorOption(
	TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option
) const
{
	return SNew(STextBlock)
		.Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

void SLLMNPCTemplateWorkbench::HandleItemSelectionChanged(
	TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	SelectedItem = Item;
	RefreshDerivedText();
}

void SLLMNPCTemplateWorkbench::HandleSearchChanged(const FText& Text)
{
	SearchQuery = Text.ToString();
	ApplyFilters();
}

void SLLMNPCTemplateWorkbench::HandleRecipeJsonChanged(
	const FText& Text
)
{
	static_cast<void>(Text);
	InvalidateSandboxPreflight(true);
}

void SLLMNPCTemplateWorkbench::HandleAnimationAssetChanged(
	const FAssetData& AssetData
)
{
	SelectedAnimationAssetPath = AssetData.IsValid()
		? AssetData.GetObjectPathString()
		: FString();
	SetStatus(
		SelectedAnimationAssetPath.IsEmpty()
			? LOCTEXT(
				"AnimationSelectionCleared",
				"Animation Asset selection cleared."
			)
			: FText::FromString(FString::Printf(
				TEXT("Selected Animation Asset: %s"),
				*SelectedAnimationAssetPath
			)),
		false
	);
}

void SLLMNPCTemplateWorkbench::HandleIncludeNonPublishedChanged(
	ECheckBoxState State
)
{
	bIncludeNonPublished = State == ECheckBoxState::Checked;
	ApplyFilters();
}

void SLLMNPCTemplateWorkbench::HandleSandboxEnabledChanged(
	ECheckBoxState State
)
{
	ULLMNPCSettings* Settings = GetMutableDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		SetStatus(
			LOCTEXT(
				"SandboxSettingsUnavailable",
				"Sandbox settings are unavailable."
			),
			true
		);
		return;
	}
	Settings->bEnableAuthoringRuntimeSandbox =
		State == ECheckBoxState::Checked;
	Settings->SaveConfig();
	if (!Settings->bEnableAuthoringRuntimeSandbox)
	{
		CancelActiveSandboxPreview(TEXT("sandbox_disabled"));
	}
	UpdateSandboxSummary();
	SetStatus(
		Settings->bEnableAuthoringRuntimeSandbox
			? LOCTEXT(
				"SandboxEnabled",
				"Authoring Runtime Sandbox enabled for local development."
			)
			: LOCTEXT(
				"SandboxDisabled",
				"Authoring Runtime Sandbox disabled. Runtime remains Published-only."
			),
		false
	);
}

void SLLMNPCTemplateWorkbench::HandleSkeletonChanged(
	TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	const FName PreviousProfile = SelectedSkeleton.IsValid()
		? SelectedSkeleton->ProfileId
		: NAME_None;
	SelectedSkeleton = Option;
	if (
		PreviousProfile !=
			(SelectedSkeleton.IsValid()
				? SelectedSkeleton->ProfileId
				: NAME_None)
	)
	{
		InvalidateSandboxPreflight(true);
	}
	RefreshDerivedText();
}

void SLLMNPCTemplateWorkbench::HandleActorChanged(
	TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	ULLMNPCMotionComponent* PreviousMotion =
		GetSelectedMotionComponent();
	SelectedActor = Option;
	if (PreviousMotion != GetSelectedMotionComponent())
	{
		InvalidateSandboxPreflight(true);
	}
}

FReply SLLMNPCTemplateWorkbench::HandleRefresh()
{
	RefreshCatalog();
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleOpenAsset()
{
	UObject* Asset = SelectedItem.IsValid() ? SelectedItem->Asset.Get() : nullptr;
	UAssetEditorSubsystem* AssetEditor =
		GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!Asset || !AssetEditor)
	{
		SetStatus(LOCTEXT("OpenAssetFailed", "Could not open the selected asset."), true);
		return FReply::Handled();
	}
	AssetEditor->OpenEditorForAsset(Asset);
	SetStatus(FText::FromString(FString::Printf(TEXT("Opened %s"), *Asset->GetPathName())), false);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleBrowseAnimationDraft()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !AnimationDraftPathBox.IsValid())
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const void* ParentWindow =
		FSlateApplication::Get()
			.FindBestParentWindowHandleForDialogs(nullptr);
	if (DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("Select Animation Template Draft"),
		FPaths::GetPath(
			AnimationDraftPathBox->GetText().ToString()
		),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files
	) && !Files.IsEmpty())
	{
		AnimationDraftPathBox->SetText(
			FText::FromString(Files[0])
		);
	}
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleImportAnimationDraft()
{
	UAnimationAsset* AnimationAsset =
		LoadObject<UAnimationAsset>(
			nullptr,
			*SelectedAnimationAssetPath
		);
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<
				ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (
		!AnimationAsset ||
		!Authoring ||
		!AnimationDraftPathBox.IsValid() ||
		!AnimationDestinationPathBox.IsValid()
	)
	{
		SetStatus(
			LOCTEXT(
				"AnimationImportUnavailable",
				"Animation Draft import inputs are unavailable."
			),
			true
		);
		return FReply::Handled();
	}

	const FLLMNPCAuthoringOperationResult Result =
		Authoring->ImportAnimationDraftFromFile(
			AnimationDraftPathBox->GetText().ToString(),
			AnimationAsset,
			AnimationDestinationPathBox->GetText().ToString()
		);
	if (Result.bSuccess && Result.TemplateAsset)
	{
		bIncludeNonPublished = true;
		const FString ImportedPath =
			Result.TemplateAsset->GetPathName();
		RefreshCatalog(ImportedPath);
		SetActivePage(ELLMNPCTemplateWorkbenchPage::Quality);
	}
	SetStatus(
		FText::FromString(Result.Message),
		!Result.bSuccess
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleGenerateMotionRecipe()
{
	if (
		!CanGenerateMotionRecipe() ||
		!SelectedSkeleton.IsValid()
	)
	{
		SetStatus(
			LOCTEXT(
				"RecipeGenerationInputsUnavailable",
				"Motion Recipe generation inputs are unavailable."
			),
			true
		);
		return FReply::Handled();
	}
	if (SelectedSkeleton->ProfileId != TEXT("ue5_manny.v1"))
	{
		SetStatus(
			LOCTEXT(
				"RecipeGenerationMannyOnly",
				"Online Motion Recipe generation is currently locked to ue5_manny.v1."
			),
			true
		);
		return FReply::Handled();
	}

	FLLMNPCOnlineTestConfigState OnlineConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	if (!OnlineConfig.IsLoaded())
	{
		OnlineConfig =
			FLLMNPCOnlineTestConfigLoader::LoadProjectConfig();
	}
	if (!OnlineConfig.HasPassingConnectionForCurrentConfig())
	{
		SetStatus(
			LOCTEXT(
				"RecipeGenerationConnectionGate",
				"Load env.txt and pass Test Connection for the current config before online authoring."
			),
			true
		);
		return FReply::Handled();
	}

	ULLMNPCSkeletonProfile* Profile = nullptr;
	TArray<const ULLMNPCMotionTemplate*> PublishedExamples;
	for (UObject* Asset : ReferencedAssets)
	{
		if (
			ULLMNPCSkeletonProfile* CandidateProfile =
				Cast<ULLMNPCSkeletonProfile>(Asset)
		)
		{
			if (
				CandidateProfile->ProfileId ==
				SelectedSkeleton->ProfileId
			)
			{
				Profile = CandidateProfile;
			}
		}
		else if (
			const ULLMNPCMotionTemplate* Template =
				Cast<ULLMNPCMotionTemplate>(Asset)
		)
		{
			if (Template->IsPublished())
			{
				PublishedExamples.Add(Template);
			}
		}
	}
	if (!Profile)
	{
		SetStatus(
			LOCTEXT(
				"RecipeGenerationProfileMissing",
				"The selected Skeleton Profile could not be loaded."
			),
			true
		);
		return FReply::Handled();
	}

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	const ULLMNPCControlManifest* GenerationManifest = nullptr;
	if (
		ULLMNPCMotionComponent* PreviewMotion =
			GetSelectedMotionComponent()
	)
	{
		if (
			const ULLMNPCSkeletonProfile* PreviewProfile =
				PreviewMotion->SkeletonProfile.LoadSynchronous()
		)
		{
			if (PreviewProfile->ProfileId == Profile->ProfileId)
			{
				GenerationManifest =
					PreviewMotion->ControlManifest;
			}
		}
	}
	const FLLMNPCSkeletonCapabilityBuildResult CapabilityResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*Profile,
			GenerationManifest,
			Capability
		);
	if (!CapabilityResult.bSucceeded)
	{
		SetStatus(
			FText::FromString(
				CapabilityResult.Errors.IsEmpty()
					? TEXT("Skeleton Capability build failed.")
					: CapabilityResult.Errors[0]
			),
			true
		);
		return FReply::Handled();
	}

	FString PromptError;
	FLLMNPCMotionRecipePromptPackage Prompt;
	if (!FLLMNPCMotionRecipeAuthoringPrompt::Build(
		RecipeIntentBox->GetText().ToString(),
		Capability,
		PublishedExamples,
		Prompt,
		PromptError,
		PendingRecipeRequestContext
	))
	{
		SetStatus(FText::FromString(PromptError), true);
		return FReply::Handled();
	}

	LastRecipePrompt = MoveTemp(Prompt);
	LastRecipeCapability = Capability;
	LastRecipeResponse =
		FLLMNPCMotionRecipeAuthoringResponse();
	LastAuthoringResult = FLLMNPCAuthoringJsonResult();
	CancelActiveSandboxPreview(TEXT("superseded"));
	LastSandboxPreflight =
		FLLMNPCAuthoringSandboxPreflightResult();
	SandboxReport = FLLMNPCOnlineSandboxReportRecord();
	SandboxReport.PromptVersion =
		LastRecipePrompt.PromptVersion;
	SandboxReport.PromptHash = LastRecipePrompt.PromptHash;
	SandboxReport.CapabilityHash =
		LastRecipePrompt.CapabilityHash;
	SandboxReport.RegistryVersion =
		LastRecipePrompt.RegistryVersion;
	SandboxReport.EndpointOrigin = OnlineConfig.EndpointOrigin;
	SandboxReport.NonSecretConfigHash =
		OnlineConfig.NonSecretConfigHash;
	SandboxReport.StartedAtUtc = FDateTime::UtcNow();
	SandboxReport.UpdatedAtUtc = SandboxReport.StartedAtUtc;
	SandboxReport.Outcome = TEXT("request_pending");
	RecipeGeneratedAtUtc = FDateTime();
	GenerationEndpointOrigin = OnlineConfig.EndpointOrigin;
	GenerationConfigHash = OnlineConfig.NonSecretConfigHash;
	RecipeGenerationSummary = FString::Printf(
		TEXT("Request: pending\nSource: %s\nProfile: %s\nCapability: %s\nRegistry: %s\nPrompt: %s\nExamples: %d"),
		*LastRecipePrompt.RequestContext.TriggerSource.ToString(),
		*SelectedSkeleton->ProfileId.ToString(),
		*LastRecipePrompt.CapabilityHash,
		*LastRecipePrompt.RegistryVersion,
		*LastRecipePrompt.PromptHash,
		LastRecipePrompt.SimilarTemplateCount
	);
	if (RecipeJsonBox)
	{
		RecipeJsonBox->SetText(FText::GetEmpty());
	}
	if (SandboxReviewNotesBox)
	{
		SandboxReviewNotesBox->SetText(FText::GetEmpty());
	}
	if (RecipeEvidenceBox)
	{
		RecipeEvidenceBox->SetText(
			GetRecipeGenerationSummaryText()
		);
	}

	FLLMNPCAuthoringJsonRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.SystemPrompt = LastRecipePrompt.SystemPrompt;
	Request.UserJson = LastRecipePrompt.UserJson;
	Request.Temperature = 0.1f;
	Request.MaxTokens = 1800;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	Request.TimeoutSeconds = Settings
		? Settings->AuthoringSandboxRequestTimeoutSeconds
		: 30.0f;
	ActiveAuthoringRequestId = Request.RequestId;
	SandboxReport.RequestId = Request.RequestId;
	bAuthoringRequestPending = true;
	SaveSandboxReport();
	UpdateSandboxSummary();
	SetStatus(
		LOCTEXT(
			"RecipeGenerationStarted",
			"Online Authoring request started."
		),
		false
	);

	const TWeakPtr<SLLMNPCTemplateWorkbench> WeakWorkbench =
		SharedThis(this);
	AuthoringModelClient->Send(
		Request,
		[WeakWorkbench](
			const FLLMNPCAuthoringJsonResult& Result
		)
		{
			const TSharedPtr<SLLMNPCTemplateWorkbench> Self =
				WeakWorkbench.Pin();
			if (
				!Self.IsValid() ||
				Result.RequestId != Self->ActiveAuthoringRequestId
			)
			{
				return;
			}
			Self->bAuthoringRequestPending = false;
			Self->ActiveAuthoringRequestId.Invalidate();
			Self->LastAuthoringResult = Result;
			FLLMNPCOnlineSandboxReport::ApplyAuthoringResult(
				Result,
				Self->SandboxReport
			);
			Self->RecipeGeneratedAtUtc = FDateTime::UtcNow();
			if (!Result.bSuccess)
			{
				Self->RecipeGenerationSummary = FString::Printf(
					TEXT("Request: failed\nError: %s\nHTTP: %d\nAttempts: %d\nLatency: %.3fs"),
					*Result.ErrorCode.ToString(),
					Result.HttpStatus,
					Result.AttemptCount,
					Result.TotalLatencySeconds
				);
				if (Self->RecipeEvidenceBox)
				{
					Self->RecipeEvidenceBox->SetText(
						Self->GetRecipeGenerationSummaryText()
					);
				}
				Self->SetStatus(
					FText::FromName(Result.ErrorCode),
					true
				);
				Self->SaveSandboxReport();
				Self->UpdateSandboxSummary();
				return;
			}

			FString ParseError;
			if (!FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
				Result.ResponseJson,
				Self->LastRecipeResponse,
				ParseError
			))
			{
				Self->LastAuthoringResult.bSuccess = false;
				Self->LastAuthoringResult.ErrorCode =
					TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_INVALID");
				Self->RecipeGenerationSummary = FString::Printf(
					TEXT("Request: rejected\nError: %s\nModel: %s\nHTTP: %d\nLatency: %.3fs"),
					*ParseError,
					*Result.ProviderModelId,
					Result.HttpStatus,
					Result.TotalLatencySeconds
				);
				if (Self->RecipeEvidenceBox)
				{
					Self->RecipeEvidenceBox->SetText(
						Self->GetRecipeGenerationSummaryText()
					);
				}
				Self->SetStatus(
					FText::FromString(ParseError),
					true
				);
				Self->SandboxReport.Outcome =
					TEXT("response_invalid");
				Self->SandboxReport.ErrorCode =
					TEXT("LLMNPC_RECIPE_AUTHORING_RESPONSE_INVALID");
				Self->SaveSandboxReport();
				Self->UpdateSandboxSummary();
				return;
			}
			if (Self->LastRecipeResponse.bUnsupported)
			{
				Self->LastAuthoringResult.bSuccess = false;
				Self->RecipeGenerationSummary = FString::Printf(
					TEXT("Request: unsupported\nModel: %s\nReason: %s\nLatency: %.3fs"),
					*Result.ProviderModelId,
					*Self->LastRecipeResponse.UnsupportedReason,
					Result.TotalLatencySeconds
				);
				if (Self->RecipeEvidenceBox)
				{
					Self->RecipeEvidenceBox->SetText(
						Self->GetRecipeGenerationSummaryText()
					);
				}
				Self->SetStatus(
					FText::FromString(
						Self->LastRecipeResponse.UnsupportedReason
					),
					true
				);
				Self->SandboxReport.Outcome = TEXT("unsupported");
				Self->SandboxReport.ErrorCode =
					TEXT("LLMNPC_RECIPE_AUTHORING_UNSUPPORTED");
				Self->SaveSandboxReport();
				Self->UpdateSandboxSummary();
				return;
			}

			if (Self->RecipeJsonBox)
			{
				Self->RecipeJsonBox->SetText(
					FText::FromString(
						Self->LastRecipeResponse.RecipeJson
					)
				);
			}
			FString RecipeValidationError;
			if (
				!FLLMNPCMotionRecipeAuthoringPrompt::
					ValidateRecipeForCapability(
						Self->LastRecipeResponse,
						Self->LastRecipeCapability,
						{TEXT("shoulder.shrug")},
						TEXT("express_uncertainty"),
						1,
						RecipeValidationError
					)
			)
			{
				Self->LastAuthoringResult.bSuccess = false;
				Self->LastAuthoringResult.ErrorCode =
					TEXT("LLMNPC_RECIPE_AUTHORING_RECIPE_REJECTED");
				Self->RecipeGenerationSummary = FString::Printf(
					TEXT("Request: rejected by UE Recipe Validator\nError: %s\nModel: %s\nHTTP: %d\nLatency: %.3fs\nCapability: %s\nRegistry: %s\nPrompt: %s"),
					*RecipeValidationError,
					*Result.ProviderModelId,
					Result.HttpStatus,
					Result.TotalLatencySeconds,
					*Self->LastRecipePrompt.CapabilityHash,
					*Self->LastRecipePrompt.RegistryVersion,
					*Self->LastRecipePrompt.PromptHash
				);
				if (Self->RecipeEvidenceBox)
				{
					Self->RecipeEvidenceBox->SetText(
						Self->GetRecipeGenerationSummaryText()
					);
				}
				Self->SetStatus(
					FText::FromString(RecipeValidationError),
					true
				);
				Self->SandboxReport.Outcome =
					TEXT("recipe_rejected");
				Self->SandboxReport.ErrorCode =
					TEXT("LLMNPC_RECIPE_AUTHORING_RECIPE_REJECTED");
				Self->SaveSandboxReport();
				Self->UpdateSandboxSummary();
				return;
			}
			Self->SandboxReport.Outcome =
				TEXT("recipe_accepted");
			Self->SandboxReport.ErrorCode = NAME_None;
			Self->RecipeGenerationSummary = FString::Printf(
				TEXT("Request: accepted\nProvider: %s\nModel: %s\nHTTP: %d\nAttempts: %d\nLatency: %.3fs\nTokens: %d / %d / %d\nCapability: %s\nRegistry: %s\nPrompt: %s"),
				*Result.ProviderId.ToString(),
				*Result.ProviderModelId,
				Result.HttpStatus,
				Result.AttemptCount,
				Result.TotalLatencySeconds,
				Result.PromptTokens,
				Result.CompletionTokens,
				Result.TotalTokens,
				*Self->LastRecipePrompt.CapabilityHash,
				*Self->LastRecipePrompt.RegistryVersion,
				*Self->LastRecipePrompt.PromptHash
			);
			if (Self->RecipeEvidenceBox)
			{
				Self->RecipeEvidenceBox->SetText(
					Self->GetRecipeGenerationSummaryText()
				);
			}
			Self->SetStatus(
				LOCTEXT(
					"RecipeGenerationAccepted",
					"Online Recipe passed the Authoring response contract."
				),
				false
			);
			Self->SaveSandboxReport();
			Self->UpdateSandboxSummary();
		}
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::
HandleCancelMotionRecipeGeneration()
{
	if (CanCancelMotionRecipeGeneration())
	{
		AuthoringModelClient->Cancel(ActiveAuthoringRequestId);
	}
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::
HandlePreviewMotionRecipeInSandbox()
{
	if (!CanPreviewMotionRecipeInSandbox())
	{
		SetStatus(
			LOCTEXT(
				"SandboxPreviewUnavailable",
				"Enable Sandbox, start PIE, select a Manny actor, and generate an accepted Recipe first."
			),
			true
		);
		return FReply::Handled();
	}

	ULLMNPCMotionComponent* Motion =
		GetSelectedMotionComponent();
	ULLMNPCSkeletonProfile* Profile =
		GetSelectedSkeletonProfile();
	ULLMNPCSkeletonProfile* ActorProfile =
		Motion ? Motion->SkeletonProfile.LoadSynchronous() : nullptr;
	if (
		!Motion ||
		!Profile ||
		!ActorProfile ||
		ActorProfile->ProfileId != Profile->ProfileId
	)
	{
		SandboxReport.Outcome = TEXT("preflight_rejected");
		SandboxReport.ErrorCode =
			TEXT("LLMNPC_AUTHORING_SANDBOX_ACTOR_PROFILE_MISMATCH");
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		SaveSandboxReport();
		UpdateSandboxSummary();
		SetStatus(
			LOCTEXT(
				"SandboxActorProfileMismatch",
				"The selected PIE actor does not use the selected Manny Skeleton Profile."
			),
			true
		);
		return FReply::Handled();
	}

	FLLMNPCAuthoringSandboxRequest Request;
	Request.RecipeJson =
		RecipeJsonBox->GetText().ToString();
	Request.SkeletonProfile = Profile;
	Request.ControlManifest = Motion->ControlManifest;
	LastSandboxPreflight =
		FLLMNPCAuthoringSandbox::RunFullPreflight(Request);
	if (
		LastSandboxPreflight.bPassed &&
		LastSandboxPreflight.CompiledMetadata.CapabilityHash !=
			LastRecipePrompt.CapabilityHash
	)
	{
		LastSandboxPreflight.bPassed = false;
		LastSandboxPreflight.Stage =
			ELLMNPCAuthoringSandboxStage::Rejected;
		LastSandboxPreflight.ErrorCode =
			TEXT("LLMNPC_AUTHORING_SANDBOX_CAPABILITY_STALE");
		LastSandboxPreflight.ErrorMessage =
			LastSandboxPreflight.ErrorCode.ToString();
		LastSandboxPreflight.TransientPlan = FLLMMotionPlan();
	}
	FLLMNPCOnlineSandboxReport::ApplyPreflightResult(
		LastSandboxPreflight,
		SandboxReport
	);
	if (!LastSandboxPreflight.bPassed)
	{
		SandboxReport.bTransientPlanSubmitted = false;
		SandboxReport.bDraftRecordSaved = false;
		SandboxReport.DraftRecordPath.Reset();
		SandboxReport.HumanVisualDecision =
			TEXT("not_recorded");
		SaveSandboxReport();
		UpdateSandboxSummary();
		SetStatus(
			FText::FromString(
				LastSandboxPreflight.ErrorMessage.IsEmpty()
					? LastSandboxPreflight.ErrorCode.ToString()
					: LastSandboxPreflight.ErrorMessage
			),
			true
		);
		return FReply::Handled();
	}

	CancelActiveSandboxPreview(TEXT("superseded"));
	ActiveSandboxClipId =
		LastSandboxPreflight.TransientPlan.Clip.ClipId;
	if (!Motion->SubmitAuthoringSandboxPlan(
		LastSandboxPreflight.TransientPlan
	))
	{
		ActiveSandboxClipId.Reset();
		SandboxReport.Outcome = TEXT("submit_rejected");
		SandboxReport.ErrorCode =
			FName(*Motion->LastValidationError);
		SandboxReport.bTransientPlanSubmitted = false;
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		SaveSandboxReport();
		UpdateSandboxSummary();
		SetStatus(
			FText::FromString(Motion->LastValidationError),
			true
		);
		return FReply::Handled();
	}

	ActiveSandboxMotion = Motion;
	SandboxPreviewStartedAtSeconds =
		FPlatformTime::Seconds();
	SandboxReport.bTransientPlanSubmitted = true;
	SandboxReport.HumanVisualDecision =
		TEXT("not_recorded");
	SandboxReport.HumanVisualNotes.Reset();

	FString DraftRecordError;
	if (!FLLMNPCOnlineSandboxReport::SaveDraftRecord(
		LastSandboxPreflight.CanonicalRecipeJson,
		SandboxReport,
		SandboxReport.DraftRecordPath,
		DraftRecordError
	))
	{
		Motion->CancelMotionClip(ActiveSandboxClipId);
		ActiveSandboxMotion.Reset();
		ActiveSandboxClipId.Reset();
		SandboxPreviewStartedAtSeconds = 0.0;
		SandboxReport.bTransientPlanSubmitted = false;
		SandboxReport.bDraftRecordSaved = false;
		SandboxReport.Outcome = TEXT("draft_record_failed");
		SandboxReport.ErrorCode = FName(*DraftRecordError);
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		SaveSandboxReport();
		UpdateSandboxSummary();
		SetStatus(FText::FromString(DraftRecordError), true);
		return FReply::Handled();
	}

	SandboxReport.bDraftRecordSaved = true;
	SandboxReport.Outcome = TEXT("previewing");
	SandboxReport.ErrorCode = NAME_None;
	SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
	SaveSandboxReport();
	UpdateSandboxSummary();
	SetStatus(
		LOCTEXT(
			"SandboxPreviewSubmitted",
			"Full Preflight passed. The transient Recipe is playing on the selected PIE actor."
		),
		false
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::
HandleStopMotionRecipeSandbox()
{
	if (CanStopMotionRecipeSandbox())
	{
		SandboxReport.ErrorCode =
			TEXT("LLMNPC_AUTHORING_SANDBOX_PREVIEW_CANCELLED");
		CancelActiveSandboxPreview(TEXT("preview_cancelled"));
		SetStatus(
			LOCTEXT(
				"SandboxPreviewCancelled",
				"Sandbox preview cancelled and its pose contribution was cleared."
			),
			false
		);
	}
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::
HandleRecordSandboxVisualPass()
{
	RecordSandboxVisualDecision(TEXT("pass"));
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::
HandleRecordSandboxVisualFail()
{
	RecordSandboxVisualDecision(TEXT("fail"));
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleCreateMotionRecipeDraft()
{
	if (
		!CanCreateMotionRecipeDraft() ||
		!SelectedSkeleton.IsValid()
	)
	{
		SetStatus(
			LOCTEXT(
				"RecipeDraftInputsUnavailable",
				"An accepted Recipe, completed Sandbox preview, Human Visual Pass, and Draft destination are required."
			),
			true
		);
		return FReply::Handled();
	}
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<
				ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (!Authoring)
	{
		SetStatus(
			LOCTEXT(
				"RecipeDraftAuthoringUnavailable",
				"Authoring subsystem is unavailable."
			),
			true
		);
		return FReply::Handled();
	}

	const FString RequestSuffix =
		LastAuthoringResult.RequestId
			.ToString(EGuidFormats::Digits)
			.Left(6)
			.ToLower();
	const FString RecipeSuffix =
		FMD5::HashAnsiString(
			*RecipeJsonBox->GetText().ToString()
		).Left(6).ToLower();
	const FString Suffix =
		RequestSuffix + RecipeSuffix;
	FLLMNPCMotionRecipeDraftCatalogSpec CatalogSpec;
	CatalogSpec.AssetName =
		FString::Printf(
			TEXT("MT_Shrug_Manny_Generated_%s"),
			*Suffix
		);
	CatalogSpec.TemplateId =
		FName(*FString::Printf(
			TEXT("gesture.shrug.manny.generated.%s"),
			*Suffix
		));
	CatalogSpec.PublicActionId = TEXT("gesture.shrug");
	CatalogSpec.PublicActionAssetName =
		TEXT("PA_Gesture_Shrug_Draft");
	CatalogSpec.SemanticVersion = TEXT("1.0.0");
	CatalogSpec.VariantId = TEXT("generated_recipe");
	CatalogSpec.DisplayName =
		LastRecipeResponse.CatalogDraft.DisplayName;
	CatalogSpec.SelectionSummary =
		LastRecipeResponse.CatalogDraft.SelectionSummary;
	CatalogSpec.VisualDescription =
		LastRecipeResponse.CatalogDraft.VisualDescription;
	CatalogSpec.SuitableWhen =
		LastRecipeResponse.CatalogDraft.SuitableWhen;
	CatalogSpec.AvoidWhen =
		LastRecipeResponse.CatalogDraft.AvoidWhen;
	CatalogSpec.IntentTags = {TEXT("express_uncertainty")};
	CatalogSpec.EmotionTags = {TEXT("uncertain")};
	CatalogSpec.VariantStyleTags = {
		TEXT("neutral"),
		TEXT("subtle"),
		TEXT("uncertain")
	};
	CatalogSpec.BodyRegionTags = {
		TEXT("shoulders"),
		TEXT("upper_torso"),
		TEXT("two_arms"),
		TEXT("two_hands"),
		TEXT("fingers")
	};
	CatalogSpec.SpatialRequirementTags = {
		TEXT("target_independent")
	};
	CatalogSpec.SemanticEffectTags = {
		TEXT("express_uncertainty"),
		TEXT("noncommittal")
	};
	CatalogSpec.GestureFamily = TEXT("shrug");
	CatalogSpec.DefaultStyle = TEXT("uncertain");
	CatalogSpec.SearchKeywords = {
		TEXT("shrug"),
		TEXT("uncertain"),
		TEXT("unsure"),
		TEXT("maybe"),
		TEXT("do not know")
	};
	CatalogSpec.bCanRunWhileMoving = true;
	CatalogSpec.Expressiveness = 0.6f;
	CatalogSpec.Energy = 0.42f;
	CatalogSpec.SocialIntensity = 0.48f;

	FLLMNPCMotionRecipeGenerationEvidence Evidence;
	Evidence.RequestId = LastAuthoringResult.RequestId;
	Evidence.ProviderId = LastAuthoringResult.ProviderId;
	Evidence.ProviderModelId =
		LastAuthoringResult.ProviderModelId;
	Evidence.EndpointOrigin = GenerationEndpointOrigin;
	Evidence.NonSecretConfigHash = GenerationConfigHash;
	Evidence.PromptVersion = LastRecipePrompt.PromptVersion;
	Evidence.PromptHash = LastRecipePrompt.PromptHash;
	Evidence.CapabilityHash = LastRecipePrompt.CapabilityHash;
	Evidence.RegistryVersion = LastRecipePrompt.RegistryVersion;
	Evidence.SystemPrompt = LastRecipePrompt.SystemPrompt;
	Evidence.UserJson = LastRecipePrompt.UserJson;
	Evidence.RecipeSchemaJson =
		LastRecipePrompt.RecipeSchemaJson;
	Evidence.CapabilityModelViewJson =
		LastRecipePrompt.CapabilityModelViewJson;
	Evidence.RawResponseJson =
		LastAuthoringResult.ResponseJson;
	Evidence.TriggerSource =
		LastRecipePrompt.RequestContext.TriggerSource;
	Evidence.SourceTemplateId =
		LastRecipePrompt.RequestContext.SourceTemplateId;
	Evidence.SourceRecipeHash =
		LastRecipePrompt.RequestContext.SourceRecipeHash;
	Evidence.ReviewFeedback =
		LastRecipePrompt.RequestContext.ReviewFeedback;
	Evidence.GeneratedAtUtc = RecipeGeneratedAtUtc;
	Evidence.HttpStatus = LastAuthoringResult.HttpStatus;
	Evidence.AttemptCount =
		LastAuthoringResult.AttemptCount;
	Evidence.TotalLatencySeconds =
		LastAuthoringResult.TotalLatencySeconds;
	Evidence.PromptTokens = LastAuthoringResult.PromptTokens;
	Evidence.CompletionTokens =
		LastAuthoringResult.CompletionTokens;
	Evidence.TotalTokens = LastAuthoringResult.TotalTokens;
	Evidence.CompiledRecipeHash =
		LastSandboxPreflight.CompiledMetadata.CompiledRecipeHash;
	Evidence.KinematicReportHash =
		LastSandboxPreflight.KinematicReport.ReportHash;

	const FScopedTransaction Transaction(
		LOCTEXT(
			"CreateRecipeDraftTransaction",
			"Create LLM NPC Motion Recipe Draft"
		)
	);
	const FLLMNPCAuthoringOperationResult Result =
		Authoring->CreateMotionRecipeDraft(
			RecipeJsonBox->GetText().ToString(),
			SelectedSkeleton->ProfileId,
			CatalogSpec,
			Evidence,
			RecipeDestinationPathBox->GetText().ToString()
		);
	if (Result.bSuccess && Result.TemplateAsset)
	{
		PendingRecipeRequestContext =
			FLLMNPCMotionRecipeRequestContext();
		SandboxReport.Outcome = TEXT("sent_to_generated_draft");
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		SaveSandboxReport();
		UpdateSandboxSummary();
		bIncludeNonPublished = true;
		RefreshCatalog(Result.TemplateAsset->GetPathName());
		SetActivePage(ELLMNPCTemplateWorkbenchPage::Quality);
	}
	SetStatus(
		FText::FromString(Result.Message),
		!Result.bSuccess
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandlePreview()
{
	ULLMNPCMotionTemplate* Template = ResolvePreviewTemplate();
	ULLMNPCMotionComponent* Motion = GetSelectedMotionComponent();
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (!Template || !Motion || !Motion->GetOwner() || !Authoring)
	{
		SetStatus(LOCTEXT("PreviewUnavailable", "PIE preview target or template is unavailable."), true);
		return FReply::Handled();
	}
	const FLLMNPCAuthoringOperationResult Result =
		Authoring->PreviewTemplateOnActor(Template, Motion->GetOwner());
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleValidate()
{
	RefreshDerivedText();
	FString Error;
	bool bValid = false;
	if (const ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		bValid = Template->ValidateTemplate(Error);
	}
	else if (const ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
		ULLMNPCActionVocabulary* Vocabulary =
			Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
		bValid = Definition->ValidateDefinition(Vocabulary, Error);
	}
	SetStatus(
		bValid
			? LOCTEXT("SelectionValid", "Selection validation passed.")
			: FText::FromString(Error.IsEmpty() ? TEXT("No selection.") : Error),
		!bValid
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleGenerateQualityReport()
{
	ULLMNPCMotionTemplate* Template = GetSelectedTemplate();
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (!Template || !Authoring || !ReconstructionPathBox.IsValid())
	{
		SetStatus(LOCTEXT("QualityUnavailable", "Quality report inputs are unavailable."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("GenerateQualityTransaction", "Generate LLM NPC Quality Report"));
	Template->Modify();
	const FLLMNPCAuthoringOperationResult Result = Authoring->GenerateQualityReport(
		Template,
		ReconstructionPathBox->GetText().ToString(),
		FullPosePathBox.IsValid() ? FullPosePathBox->GetText().ToString() : FString()
	);
	RefreshCatalog(Template->GetPathName());
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleBrowseReconstruction()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !ReconstructionPathBox.IsValid())
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const void* ParentWindow =
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("Select UEPI Reconstruction Profile"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files
	) && !Files.IsEmpty())
	{
		ReconstructionPathBox->SetText(FText::FromString(Files[0]));
	}
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleBrowseFullPose()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform || !FullPosePathBox.IsValid())
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const void* ParentWindow =
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("Select UEPI Full Pose Artifact"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		TEXT("JSON files (*.json)|*.json"),
		EFileDialogFlags::None,
		Files
	) && !Files.IsEmpty())
	{
		FullPosePathBox->SetText(FText::FromString(Files[0]));
	}
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleMarkPreviewed()
{
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	const FString Notes = ReviewNotesBox.IsValid()
		? ReviewNotesBox->GetText().ToString()
		: FString();
	if (!Authoring)
	{
		SetStatus(LOCTEXT("AuthoringUnavailable", "Authoring subsystem is unavailable."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("PreviewedTransaction", "Mark LLM NPC Asset Previewed"));
	FLLMNPCAuthoringOperationResult Result;
	if (ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		Template->Modify();
		Result = Authoring->MarkTemplatePreviewed(Template, Notes);
	}
	else if (ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		Definition->Modify();
		Result = Authoring->MarkPublicActionPreviewed(Definition, Notes);
	}
	RefreshCatalog(
		Result.TemplateAsset
			? Result.TemplateAsset->GetPathName()
			: Result.PublicActionAsset
				? Result.PublicActionAsset->GetPathName()
				: FString()
	);
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleApprove()
{
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	const FString Reviewer = ReviewerBox.IsValid()
		? ReviewerBox->GetText().ToString()
		: FString();
	const FString Notes = ReviewNotesBox.IsValid()
		? ReviewNotesBox->GetText().ToString()
		: FString();
	if (!Authoring)
	{
		SetStatus(LOCTEXT("ApproveUnavailable", "Authoring subsystem is unavailable."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("ApproveTransaction", "Approve LLM NPC Asset"));
	FLLMNPCAuthoringOperationResult Result;
	if (ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		Template->Modify();
		Result = Authoring->ApproveTemplate(Template, Reviewer, Notes);
	}
	else if (ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		Definition->Modify();
		Result = Authoring->ApprovePublicAction(Definition, Reviewer, Notes);
	}
	RefreshCatalog(
		Result.TemplateAsset
			? Result.TemplateAsset->GetPathName()
			: Result.PublicActionAsset
				? Result.PublicActionAsset->GetPathName()
				: FString()
	);
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleReject()
{
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	const FString Reviewer = ReviewerBox.IsValid()
		? ReviewerBox->GetText().ToString()
		: FString();
	const FString Notes = ReviewNotesBox.IsValid()
		? ReviewNotesBox->GetText().ToString()
		: FString();
	if (!Authoring)
	{
		SetStatus(LOCTEXT("RejectUnavailable", "Authoring subsystem is unavailable."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("RejectTransaction", "Reject LLM NPC Asset"));
	FLLMNPCAuthoringOperationResult Result;
	if (ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		Template->Modify();
		Result = Authoring->RejectTemplate(Template, Reviewer, Notes);
	}
	else if (ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		Definition->Modify();
		Result = Authoring->RejectPublicAction(Definition, Reviewer, Notes);
	}
	RefreshCatalog(
		Result.TemplateAsset
			? Result.TemplateAsset->GetPathName()
			: Result.PublicActionAsset
				? Result.PublicActionAsset->GetPathName()
				: FString()
	);
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandleReviseOnline()
{
	ULLMNPCMotionTemplate* Template = GetSelectedTemplate();
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<
				ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (!Template || !Authoring || !CanReviseOnline())
	{
		SetStatus(
			LOCTEXT(
				"ReviseOnlineUnavailable",
				"Only a non-Published Manny Shrug Motion Recipe Draft can start an online revision."
			),
			true
		);
		return FReply::Handled();
	}

	FString Feedback = ReviewNotesBox.IsValid()
		? ReviewNotesBox->GetText().ToString().TrimStartAndEnd()
		: FString();
	if (
		Template->Metadata.ReviewState !=
			ELLMNPCTemplateReviewState::Rejected
	)
	{
		const FString Reviewer = ReviewerBox.IsValid()
			? ReviewerBox->GetText()
				.ToString()
				.TrimStartAndEnd()
			: FString();
		if (Reviewer.IsEmpty() || Feedback.IsEmpty())
		{
			SetStatus(
				LOCTEXT(
					"ReviseOnlineReviewRequired",
					"Reviewer identity and concrete visual feedback are required before revision."
				),
				true
			);
			return FReply::Handled();
		}
		const FScopedTransaction Transaction(
			LOCTEXT(
				"RejectForRevisionTransaction",
				"Reject and Revise LLM NPC Motion Recipe"
			)
		);
		Template->Modify();
		const FLLMNPCAuthoringOperationResult Rejection =
			Authoring->RejectTemplate(
				Template,
				Reviewer,
				Feedback
			);
		if (!Rejection.bSuccess)
		{
			SetStatus(
				FText::FromString(Rejection.Message),
				true
			);
			return FReply::Handled();
		}
		RefreshCatalog(Template->GetPathName());
	}
	else if (
		!WorkbenchReadLatestRejectionFeedback(
			*Template,
			Feedback
		)
	)
	{
		SetStatus(
			LOCTEXT(
				"ReviseOnlineFeedbackMissing",
				"The Rejected Draft has no usable visual feedback."
			),
			true
		);
		return FReply::Handled();
	}

	if (!PrepareRejectedDraftRegeneration(*Template, Feedback))
	{
		return FReply::Handled();
	}
	SetStatus(
		LOCTEXT(
			"ReviseOnlinePrepared",
			"Rejected Draft lineage is locked. Generate Online will create a separate revision."
		),
		false
	);
	return FReply::Handled();
}

FReply SLLMNPCTemplateWorkbench::HandlePublish()
{
	if (FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT(
			"ConfirmPublish",
			"Create a new immutable Published copy in the configured project library?"
		)
	) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}
	ULLMNPCTemplateAuthoringSubsystem* Authoring =
		GEditor
			? GEditor->GetEditorSubsystem<ULLMNPCTemplateAuthoringSubsystem>()
			: nullptr;
	if (!Authoring)
	{
		SetStatus(LOCTEXT("PublishUnavailable", "Authoring subsystem is unavailable."), true);
		return FReply::Handled();
	}
	const FScopedTransaction Transaction(LOCTEXT("PublishTransaction", "Publish LLM NPC Asset"));
	FLLMNPCAuthoringOperationResult Result;
	if (ULLMNPCMotionTemplate* Template = GetSelectedTemplate())
	{
		Result = Authoring->PublishTemplate(Template);
	}
	else if (ULLMNPCPublicActionDefinition* Definition = GetSelectedDefinition())
	{
		Result = Authoring->PublishPublicAction(Definition);
	}
	RefreshCatalog(
		Result.TemplateAsset
			? Result.TemplateAsset->GetPathName()
			: Result.PublicActionAsset
				? Result.PublicActionAsset->GetPathName()
				: FString()
	);
	SetStatus(FText::FromString(Result.Message), !Result.bSuccess);
	return FReply::Handled();
}

bool SLLMNPCTemplateWorkbench::PrepareRejectedDraftRegeneration(
	ULLMNPCMotionTemplate& Template,
	const FString& ReviewFeedback
)
{
	const FString CleanFeedback =
		ReviewFeedback.TrimStartAndEnd();
	if (
		Template.Metadata.ReviewState !=
			ELLMNPCTemplateReviewState::Rejected ||
		Template.Metadata.TemplateId.IsNone() ||
		Template.Metadata.SourceRecipeHash.IsEmpty() ||
		Template.Metadata.SkeletonProfileId.IsNone() ||
		CleanFeedback.IsEmpty() ||
		CleanFeedback.Len() > 600
	)
	{
		SetStatus(
			LOCTEXT(
				"ReviseOnlineLineageInvalid",
				"Rejected Draft lineage or bounded visual feedback is invalid."
			),
			true
		);
		return false;
	}

	TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>
		SourceSkeleton;
	for (
		const TSharedPtr<
			FLLMNPCTemplateWorkbenchSkeletonOption>& Option :
			SkeletonOptions
	)
	{
		if (
			Option.IsValid() &&
			Option->ProfileId ==
				Template.Metadata.SkeletonProfileId
		)
		{
			SourceSkeleton = Option;
			break;
		}
	}
	if (!SourceSkeleton.IsValid())
	{
		SetStatus(
			LOCTEXT(
				"ReviseOnlineSkeletonMissing",
				"The rejected Draft Skeleton Profile is unavailable."
			),
			true
		);
		return false;
	}

	PendingRecipeRequestContext =
		FLLMNPCMotionRecipeRequestContext();
	PendingRecipeRequestContext.TriggerSource =
		LLMNPCMotionRecipeAuthoring::
			RegenerationTriggerSource;
	PendingRecipeRequestContext.SourceTemplateId =
		Template.Metadata.TemplateId;
	PendingRecipeRequestContext.SourceRecipeHash =
		Template.Metadata.SourceRecipeHash;
	PendingRecipeRequestContext.ReviewFeedback =
		CleanFeedback;
	SelectedSkeleton = SourceSkeleton;
	if (GenerateSkeletonCombo)
	{
		GenerateSkeletonCombo->SetSelectedItem(
			SourceSkeleton
		);
	}
	if (SkeletonCombo)
	{
		SkeletonCombo->SetSelectedItem(SourceSkeleton);
	}

	FString DesiredAction =
		Template.Metadata.Description.ToString()
			.TrimStartAndEnd();
	if (DesiredAction.IsEmpty())
	{
		DesiredAction =
			Template.Metadata.DisplayName.ToString()
				.TrimStartAndEnd();
	}
	if (RecipeIntentBox)
	{
		RecipeIntentBox->SetText(
			FText::FromString(DesiredAction.Left(600))
		);
	}

	CancelActiveSandboxPreview(TEXT("revision_started"));
	LastRecipePrompt = FLLMNPCMotionRecipePromptPackage();
	LastRecipeResponse =
		FLLMNPCMotionRecipeAuthoringResponse();
	LastAuthoringResult = FLLMNPCAuthoringJsonResult();
	LastSandboxPreflight =
		FLLMNPCAuthoringSandboxPreflightResult();
	SandboxReport = FLLMNPCOnlineSandboxReportRecord();
	RecipeGeneratedAtUtc = FDateTime();
	RecipeGenerationSummary = FString::Printf(
		TEXT("Revision source: %s\nParent Recipe: %s\nFeedback: %s"),
		*Template.Metadata.TemplateId.ToString(),
		*Template.Metadata.SourceRecipeHash,
		*CleanFeedback
	);
	if (RecipeJsonBox)
	{
		RecipeJsonBox->SetText(FText::GetEmpty());
	}
	if (RecipeEvidenceBox)
	{
		RecipeEvidenceBox->SetText(
			GetRecipeGenerationSummaryText()
		);
	}
	if (SandboxReviewNotesBox)
	{
		SandboxReviewNotesBox->SetText(
			FText::GetEmpty()
		);
	}
	UpdateSandboxSummary();
	SetActivePage(ELLMNPCTemplateWorkbenchPage::Generate);
	return true;
}

void SLLMNPCTemplateWorkbench::InvalidateSandboxPreflight(
	bool bCancelActivePreview
)
{
	const bool bHadSandboxState =
		LastSandboxPreflight.Stage !=
			ELLMNPCAuthoringSandboxStage::Idle ||
		SandboxReport.bTransientPlanSubmitted ||
		SandboxReport.bDraftRecordSaved ||
		SandboxReport.HumanVisualDecision != TEXT("not_recorded");
	if (bCancelActivePreview)
	{
		CancelActiveSandboxPreview(TEXT("recipe_or_target_changed"));
	}
	LastSandboxPreflight =
		FLLMNPCAuthoringSandboxPreflightResult();
	if (bHadSandboxState && SandboxReport.RequestId.IsValid())
	{
		SandboxReport.bPreflightPassed = false;
		SandboxReport.bTransientPlanSubmitted = false;
		SandboxReport.bDraftRecordSaved = false;
		SandboxReport.DraftRecordPath.Reset();
		SandboxReport.RecipeHash.Reset();
		SandboxReport.CompiledRecipeHash.Reset();
		SandboxReport.KinematicReportHash.Reset();
		SandboxReport.PreflightIssueCodes.Reset();
		SandboxReport.HumanVisualDecision =
			TEXT("not_recorded");
		SandboxReport.HumanVisualNotes.Reset();
		SandboxReport.Outcome =
			TEXT("recipe_or_target_changed");
		SandboxReport.ErrorCode =
			TEXT("LLMNPC_AUTHORING_SANDBOX_PREFLIGHT_STALE");
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		if (SandboxReviewNotesBox)
		{
			SandboxReviewNotesBox->SetText(FText::GetEmpty());
		}
		SaveSandboxReport();
	}
	UpdateSandboxSummary();
}

void SLLMNPCTemplateWorkbench::CancelActiveSandboxPreview(
	FName Outcome
)
{
	if (ActiveSandboxClipId.IsEmpty())
	{
		return;
	}
	if (ULLMNPCMotionComponent* Motion = ActiveSandboxMotion.Get())
	{
		Motion->CancelMotionClip(ActiveSandboxClipId);
	}
	ActiveSandboxMotion.Reset();
	ActiveSandboxClipId.Reset();
	SandboxPreviewStartedAtSeconds = 0.0;
	if (SandboxReport.RequestId.IsValid())
	{
		SandboxReport.Outcome = Outcome;
		SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
		SaveSandboxReport();
		UpdateSandboxSummary();
	}
}

void SLLMNPCTemplateWorkbench::UpdateSandboxSummary()
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const bool bEnabled =
		FLLMNPCAuthoringSandbox::IsBuildAvailable() &&
		Settings &&
		Settings->bEnableAuthoringRuntimeSandbox;
	if (!SandboxReport.RequestId.IsValid())
	{
		SandboxSummary = FString::Printf(
			TEXT("Mode: %s\nBuild Gate: %s\nStatus: Generate an online Recipe, then run Full Preflight in PIE."),
			bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
			FLLMNPCAuthoringSandbox::IsBuildAvailable()
				? TEXT("Development")
				: TEXT("Shipping Disabled")
		);
	}
	else
	{
		SandboxSummary = FString::Printf(
			TEXT("Mode: %s\nRequest: %s\nOutcome: %s\nPreflight: %s\nRecipe: %s\nCapability: %s\nKinematic: %s\nTransient Submit: %s\nDraft Record: %s\nHuman Visual: %s\nReport: %s%s"),
			bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
			*SandboxReport.RequestId.ToString(
				EGuidFormats::DigitsWithHyphensLower
			),
			*SandboxReport.Outcome.ToString(),
			SandboxReport.bPreflightPassed
				? TEXT("Passed")
				: TEXT("Not Passed"),
			SandboxReport.RecipeHash.IsEmpty()
				? TEXT("<none>")
				: *SandboxReport.RecipeHash,
			SandboxReport.CapabilityHash.IsEmpty()
				? TEXT("<none>")
				: *SandboxReport.CapabilityHash,
			SandboxReport.KinematicReportHash.IsEmpty()
				? TEXT("<none>")
				: *SandboxReport.KinematicReportHash,
			SandboxReport.bTransientPlanSubmitted
				? TEXT("Yes")
				: TEXT("No"),
			SandboxReport.bDraftRecordSaved
				? *SandboxReport.DraftRecordPath
				: TEXT("<none>"),
			*SandboxReport.HumanVisualDecision.ToString(),
			SandboxReportPath.IsEmpty()
				? TEXT("<not written>")
				: *SandboxReportPath,
			SandboxReport.ErrorCode.IsNone()
				? TEXT("")
				: *FString::Printf(
					TEXT("\nError: %s"),
					*SandboxReport.ErrorCode.ToString()
				)
		);
	}
	if (SandboxEvidenceBox)
	{
		SandboxEvidenceBox->SetText(
			FText::FromString(SandboxSummary)
		);
	}
}

void SLLMNPCTemplateWorkbench::SaveSandboxReport()
{
	if (!SandboxReport.RequestId.IsValid())
	{
		return;
	}
	FString Error;
	FString Path;
	if (FLLMNPCOnlineSandboxReport::Save(
		SandboxReport,
		Path,
		Error
	))
	{
		SandboxReportPath = MoveTemp(Path);
	}
	else
	{
		SandboxReportPath = FString::Printf(
			TEXT("<write failed: %s>"),
			*Error
		);
	}
}

void SLLMNPCTemplateWorkbench::RecordSandboxVisualDecision(
	FName Decision
)
{
	if (
		!CanRecordSandboxVisualReview() ||
		(Decision != TEXT("pass") && Decision != TEXT("fail"))
	)
	{
		SetStatus(
			LOCTEXT(
				"SandboxVisualReviewUnavailable",
				"Wait for a completed Sandbox preview before recording the human visual result."
			),
			true
		);
		return;
	}
	FString Notes = SandboxReviewNotesBox.IsValid()
		? SandboxReviewNotesBox->GetText().ToString().TrimStartAndEnd()
		: FString();
	if (Notes.Len() > 1000)
	{
		SetStatus(
			LOCTEXT(
				"SandboxVisualNotesTooLong",
				"Human visual notes must be 1000 characters or fewer."
			),
			true
		);
		return;
	}

	SandboxReport.HumanVisualDecision = Decision;
	SandboxReport.HumanVisualNotes = MoveTemp(Notes);
	SandboxReport.Outcome = Decision == TEXT("pass")
		? FName(TEXT("visual_pass"))
		: FName(TEXT("visual_fail"));
	SandboxReport.ErrorCode = NAME_None;
	SandboxReport.UpdatedAtUtc = FDateTime::UtcNow();
	SaveSandboxReport();
	UpdateSandboxSummary();
	SetStatus(
		Decision == TEXT("pass")
			? LOCTEXT(
				"SandboxVisualPassRecorded",
				"Human visual pass recorded. The Recipe can now be sent to a Generated Draft."
			)
			: LOCTEXT(
				"SandboxVisualFailRecorded",
				"Human visual failure recorded. Edit or regenerate the Recipe before creating a Draft."
			),
		Decision == TEXT("fail")
	);
}

#undef LOCTEXT_NAMESPACE
