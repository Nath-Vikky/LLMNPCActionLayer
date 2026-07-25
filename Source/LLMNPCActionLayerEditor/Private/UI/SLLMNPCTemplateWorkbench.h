#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SListViewBase;
class SMultiLineEditableTextBox;
class SSearchBox;
class SWidgetSwitcher;
class ULLMNPCMotionComponent;
class ULLMNPCMotionTemplate;
class ULLMNPCPublicActionDefinition;

template<typename OptionType>
class SComboBox;

template<typename ItemType>
class SListView;

enum class ELLMNPCTemplateWorkbenchPage : uint8
{
	Library,
	Preview,
	Quality,
	Review
};

enum class ELLMNPCTemplateWorkbenchItemKind : uint8
{
	PublicAction,
	MotionTemplate
};

struct FLLMNPCTemplateWorkbenchItem
{
	ELLMNPCTemplateWorkbenchItemKind Kind =
		ELLMNPCTemplateWorkbenchItemKind::MotionTemplate;
	TWeakObjectPtr<UObject> Asset;
	FString AssetPath;
	FString SearchText;
	FName LogicalId = NAME_None;
	FName PublicActionId = NAME_None;
	FName SkeletonProfileId = NAME_None;
	FString DisplayName;
	FString Description;
	FString Version;
	int32 Revision = 0;
	ELLMNPCTemplateReviewState ReviewState = ELLMNPCTemplateReviewState::Draft;
};

struct FLLMNPCTemplateWorkbenchSkeletonOption
{
	FName ProfileId = NAME_None;
	FText Label;
};

struct FLLMNPCTemplateWorkbenchActorOption
{
	TWeakObjectPtr<ULLMNPCMotionComponent> MotionComponent;
	FText Label;
};

class SLLMNPCTemplateWorkbench final : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SLLMNPCTemplateWorkbench) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLLMNPCTemplateWorkbench() override = default;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	virtual void Tick(
		const FGeometry& AllottedGeometry,
		double InCurrentTime,
		float InDeltaTime
	) override;

private:
	ELLMNPCTemplateWorkbenchPage ActivePage =
		ELLMNPCTemplateWorkbenchPage::Library;
	bool bIncludeNonPublished = false;
	bool bStatusError = false;
	float ActorRefreshAccumulator = 0.0f;
	FString SearchQuery;
	FText StatusText;
	FString CandidateCardJson;
	FString QualityText;

	TArray<UObject*> ReferencedAssets;
	TArray<TSharedPtr<FLLMNPCTemplateWorkbenchItem>> AllItems;
	TArray<TSharedPtr<FLLMNPCTemplateWorkbenchItem>> FilteredItems;
	TSharedPtr<FLLMNPCTemplateWorkbenchItem> SelectedItem;
	TSharedPtr<SListView<TSharedPtr<FLLMNPCTemplateWorkbenchItem>>> ItemList;

	TArray<TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>> SkeletonOptions;
	TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> SelectedSkeleton;
	TSharedPtr<SComboBox<TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption>>> SkeletonCombo;

	TArray<TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>> ActorOptions;
	TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> SelectedActor;
	TSharedPtr<SComboBox<TSharedPtr<FLLMNPCTemplateWorkbenchActorOption>>> ActorCombo;

	TSharedPtr<SWidgetSwitcher> PageSwitcher;
	TSharedPtr<SMultiLineEditableTextBox> CandidateCardBox;
	TSharedPtr<SMultiLineEditableTextBox> QualityBox;
	TSharedPtr<SEditableTextBox> ReconstructionPathBox;
	TSharedPtr<SEditableTextBox> FullPosePathBox;
	TSharedPtr<SEditableTextBox> ReviewerBox;
	TSharedPtr<SMultiLineEditableTextBox> ReviewNotesBox;

	FLLMNPCTemplateSearchIndex CatalogIndex;

	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildLibraryPage();
	TSharedRef<SWidget> BuildPreviewPage();
	TSharedRef<SWidget> BuildQualityPage();
	TSharedRef<SWidget> BuildReviewPage();
	TSharedRef<SWidget> BuildPageButton(
		ELLMNPCTemplateWorkbenchPage Page,
		const FText& Label,
		const FName& IconName
	);
	TSharedRef<SWidget> MakeSectionHeader(const FText& Label) const;
	TSharedRef<SWidget> MakeFormRow(
		const FText& Label,
		const TSharedRef<SWidget>& Control
	) const;

	void RefreshCatalog(const FString& PreferredAssetPath = FString());
	void ApplyFilters();
	void RefreshDerivedText();
	void RefreshPIEActors();
	void SetActivePage(ELLMNPCTemplateWorkbenchPage Page);
	void SetStatus(const FText& Text, bool bError);

	ULLMNPCMotionTemplate* GetSelectedTemplate() const;
	ULLMNPCPublicActionDefinition* GetSelectedDefinition() const;
	ULLMNPCMotionTemplate* ResolvePreviewTemplate() const;
	ULLMNPCMotionComponent* GetSelectedMotionComponent() const;
	FName GetSelectedPublicActionId() const;

	FText GetSelectedSummaryText() const;
	FText GetCatalogSummaryText() const;
	FText GetReviewStateText() const;
	FText GetReviewDestinationText() const;
	FSlateColor GetStatusColor() const;
	ECheckBoxState GetPageCheckState(ELLMNPCTemplateWorkbenchPage Page) const;
	ECheckBoxState GetIncludeNonPublishedState() const;
	bool CanPreviewSelection() const;
	bool CanGenerateQualityReport() const;
	bool CanMarkPreviewed() const;
	bool CanApprove() const;
	bool CanReject() const;
	bool CanPublish() const;

	TSharedRef<ITableRow> GenerateItemRow(
		TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable
	);
	TSharedRef<SWidget> GenerateSkeletonOption(
		TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option
	) const;
	TSharedRef<SWidget> GenerateActorOption(
		TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option
	) const;
	void HandleItemSelectionChanged(
		TSharedPtr<FLLMNPCTemplateWorkbenchItem> Item,
		ESelectInfo::Type SelectInfo
	);
	void HandleSearchChanged(const FText& Text);
	void HandleIncludeNonPublishedChanged(ECheckBoxState State);
	void HandleSkeletonChanged(
		TSharedPtr<FLLMNPCTemplateWorkbenchSkeletonOption> Option,
		ESelectInfo::Type SelectInfo
	);
	void HandleActorChanged(
		TSharedPtr<FLLMNPCTemplateWorkbenchActorOption> Option,
		ESelectInfo::Type SelectInfo
	);

	FReply HandleRefresh();
	FReply HandleOpenAsset();
	FReply HandlePreview();
	FReply HandleValidate();
	FReply HandleGenerateQualityReport();
	FReply HandleBrowseReconstruction();
	FReply HandleBrowseFullPose();
	FReply HandleMarkPreviewed();
	FReply HandleApprove();
	FReply HandleReject();
	FReply HandlePublish();
};
