#include "UI/SLLMNPCTemplateWorkbench.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCSettings.h"
#include "Misc/MessageDialog.h"
#include "Protocol/LLMNPCTurnRequestV3Adapter.h"
#include "ScopedTransaction.h"
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
		ActivePage == ELLMNPCTemplateWorkbenchPage::Preview &&
		ActorRefreshAccumulator >= 1.0f
	)
	{
		ActorRefreshAccumulator = 0.0f;
		RefreshPIEActors();
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
}

void SLLMNPCTemplateWorkbench::SetActivePage(
	ELLMNPCTemplateWorkbenchPage Page
)
{
	ActivePage = Page;
	if (Page == ELLMNPCTemplateWorkbenchPage::Preview)
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

bool SLLMNPCTemplateWorkbench::CanPreviewSelection() const
{
	return ResolvePreviewTemplate() && GetSelectedMotionComponent();
}

bool SLLMNPCTemplateWorkbench::CanGenerateQualityReport() const
{
	const ULLMNPCMotionTemplate* Template = GetSelectedTemplate();
	return
		Template &&
		!Template->IsPublished() &&
		ReconstructionPathBox.IsValid() &&
		!ReconstructionPathBox->GetText().ToString().TrimStartAndEnd().IsEmpty();
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

void SLLMNPCTemplateWorkbench::HandleIncludeNonPublishedChanged(
	ECheckBoxState State
)
{
	bIncludeNonPublished = State == ECheckBoxState::Checked;
	ApplyFilters();
}

void SLLMNPCTemplateWorkbench::HandleSkeletonChanged(
	TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	SelectedSkeleton = Option;
	RefreshDerivedText();
}

void SLLMNPCTemplateWorkbench::HandleActorChanged(
	TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	SelectedActor = Option;
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

#undef LOCTEXT_NAMESPACE
