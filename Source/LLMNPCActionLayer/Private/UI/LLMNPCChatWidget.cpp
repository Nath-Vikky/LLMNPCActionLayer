#include "UI/LLMNPCChatWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCDialogueComponent.h"
#include "Styling/CoreStyle.h"

namespace
{
FSlateFontInfo MakeFont(int32 Size, bool bBold = false)
{
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(
		bBold ? TEXT("Bold") : TEXT("Regular"),
		Size
	);
	return Font;
}

FText StateLabel(ELLMNPCDialogueState State)
{
	switch (State)
	{
	case ELLMNPCDialogueState::Sending:
		return FText::FromString(TEXT("Sending"));
	case ELLMNPCDialogueState::Receiving:
		return FText::FromString(TEXT("Receiving"));
	case ELLMNPCDialogueState::Parsing:
		return FText::FromString(TEXT("Parsing"));
	case ELLMNPCDialogueState::Validating:
		return FText::FromString(TEXT("Validating"));
	case ELLMNPCDialogueState::Executing:
		return FText::FromString(TEXT("Executing"));
	case ELLMNPCDialogueState::Failed:
		return FText::FromString(TEXT("Failed"));
	case ELLMNPCDialogueState::Cancelled:
		return FText::FromString(TEXT("Cancelled"));
	case ELLMNPCDialogueState::TimedOut:
		return FText::FromString(TEXT("Timed out"));
	case ELLMNPCDialogueState::Idle:
	default:
		return FText::FromString(TEXT("Ready"));
	}
}
}

void ULLMNPCChatWidget::BindDialogueComponent(ULLMNPCDialogueComponent* InDialogueComponent)
{
	if (DialogueComponent == InDialogueComponent)
	{
		return;
	}

	UnbindDialogueComponent();
	DialogueComponent = InDialogueComponent;
	if (DialogueComponent)
	{
		DialogueComponent->OnMessageAdded.AddDynamic(this, &ULLMNPCChatWidget::HandleMessageAdded);
		DialogueComponent->OnStateChanged.AddDynamic(this, &ULLMNPCChatWidget::HandleStateChanged);
		DialogueComponent->OnTurnCompleted.AddDynamic(this, &ULLMNPCChatWidget::HandleTurnCompleted);
		RefreshHistory();
		HandleStateChanged(DialogueComponent->GetDialogueState());
	}
}

TSharedRef<SWidget> ULLMNPCChatWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}

	USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSize"));
	RootSize->SetWidthOverride(520.0f);
	RootSize->SetHeightOverride(560.0f);
	WidgetTree->RootWidget = RootSize;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.035f, 0.045f, 0.055f, 0.97f));
	Panel->SetPadding(FMargin(16.0f));
	RootSize->AddChild(Panel);

	UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Layout"));
	Panel->AddChild(Layout);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Header"));
	UVerticalBoxSlot* HeaderSlot = Layout->AddChildToVerticalBox(Header);
	HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("LLM NPC")));
	Title->SetFont(MakeFont(18, true));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 0.98f)));
	UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Status"));
	StatusText->SetText(FText::FromString(TEXT("Ready")));
	StatusText->SetFont(MakeFont(12));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.36f, 0.82f, 0.63f)));
	UHorizontalBoxSlot* StatusSlot = Header->AddChildToHorizontalBox(StatusText);
	StatusSlot->SetHorizontalAlignment(HAlign_Right);
	StatusSlot->SetVerticalAlignment(VAlign_Center);

	MessageHistory = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ScrollBox_MessageHistory"));
	MessageHistory->SetScrollBarVisibility(ESlateVisibility::Visible);
	MessageList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MessageList"));
	MessageHistory->AddChild(MessageList);
	UVerticalBoxSlot* HistorySlot = Layout->AddChildToVerticalBox(MessageHistory);
	HistorySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HistorySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	LastActionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TextBlock_LastAction"));
	LastActionText->SetText(FText::FromString(TEXT("Action: none")));
	LastActionText->SetFont(MakeFont(11));
	LastActionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.65f, 0.72f)));
	UVerticalBoxSlot* ActionSlot = Layout->AddChildToVerticalBox(LastActionText);
	ActionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	UHorizontalBox* Composer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Composer"));
	Layout->AddChildToVerticalBox(Composer);

	InputBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("EditableTextBox_Input"));
	InputBox->SetHintText(FText::FromString(TEXT("Message")));
	InputBox->WidgetStyle.SetFont(MakeFont(14));
	InputBox->OnTextCommitted.AddDynamic(this, &ULLMNPCChatWidget::HandleInputCommitted);
	UHorizontalBoxSlot* InputSlot = Composer->AddChildToHorizontalBox(InputBox);
	InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	InputSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));

	SendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Send"));
	SendButton->OnClicked.AddDynamic(this, &ULLMNPCChatWidget::HandleSendClicked);
	UTextBlock* SendLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SendLabel"));
	SendLabel->SetText(FText::FromString(TEXT("Send")));
	SendLabel->SetFont(MakeFont(13, true));
	SendLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.04f, 0.05f, 0.06f)));
	SendButton->AddChild(SendLabel);
	UHorizontalBoxSlot* SendSlot = Composer->AddChildToHorizontalBox(SendButton);
	SendSlot->SetVerticalAlignment(VAlign_Fill);

	return Super::RebuildWidget();
}

void ULLMNPCChatWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshHistory();
	if (DialogueComponent)
	{
		HandleStateChanged(DialogueComponent->GetDialogueState());
	}
}

void ULLMNPCChatWidget::NativeDestruct()
{
	UnbindDialogueComponent();
	Super::NativeDestruct();
}

void ULLMNPCChatWidget::HandleSendClicked()
{
	SubmitCurrentText();
}

void ULLMNPCChatWidget::HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	static_cast<void>(Text);
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitCurrentText();
	}
}

void ULLMNPCChatWidget::HandleMessageAdded(const FLLMNPCConversationMessage& Message)
{
	AddMessageRow(Message);
	if (MessageHistory)
	{
		MessageHistory->ScrollToEnd();
	}
}

void ULLMNPCChatWidget::HandleStateChanged(ELLMNPCDialogueState NewState)
{
	if (StatusText)
	{
		StatusText->SetText(StateLabel(NewState));
		const bool bError =
			NewState == ELLMNPCDialogueState::Failed ||
			NewState == ELLMNPCDialogueState::TimedOut;
		StatusText->SetColorAndOpacity(FSlateColor(
			bError
				? FLinearColor(0.95f, 0.36f, 0.32f)
				: FLinearColor(0.36f, 0.82f, 0.63f)
		));
	}

	const bool bCanSend =
		NewState == ELLMNPCDialogueState::Idle ||
		NewState == ELLMNPCDialogueState::Failed ||
		NewState == ELLMNPCDialogueState::Cancelled ||
		NewState == ELLMNPCDialogueState::TimedOut;
	if (SendButton)
	{
		SendButton->SetIsEnabled(bCanSend);
	}
	if (InputBox)
	{
		InputBox->SetIsEnabled(bCanSend);
	}
}

void ULLMNPCChatWidget::HandleTurnCompleted(const FLLMNPCDialogueTurnResult& Result)
{
	if (LastActionText)
	{
		const FName DisplayAction = !Result.ResolvedTemplateId.IsNone()
			? Result.ResolvedTemplateId
			: Result.SelectedActionId;
		LastActionText->SetText(FText::FromString(FString::Printf(
			TEXT("Action: %s"),
			DisplayAction.IsNone() ? TEXT("none") : *DisplayAction.ToString()
		)));
	}

	if (Result.bUsedLocalFallback && StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("Offline fallback")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.72f, 0.28f)));
	}
}

void ULLMNPCChatWidget::AddMessageRow(const FLLMNPCConversationMessage& Message)
{
	if (!MessageList || Message.Content.IsEmpty())
	{
		return;
	}

	const bool bPlayer = Message.Role == ELLMNPCDialogueRole::Player;
	UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>();
	Row->SetText(FText::FromString(FString::Printf(
		TEXT("%s: %s"),
		bPlayer ? TEXT("You") : TEXT("NPC"),
		*Message.Content
	)));
	Row->SetAutoWrapText(true);
	Row->SetFont(MakeFont(13, bPlayer));
	Row->SetColorAndOpacity(FSlateColor(
		bPlayer
			? FLinearColor(0.46f, 0.78f, 1.0f)
			: FLinearColor(0.92f, 0.94f, 0.96f)
	));
	UVerticalBoxSlot* RowSlot = MessageList->AddChildToVerticalBox(Row);
	RowSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 7.0f));
}

void ULLMNPCChatWidget::RefreshHistory()
{
	if (!MessageList)
	{
		return;
	}
	MessageList->ClearChildren();
	if (!DialogueComponent || !DialogueComponent->GetConversationSession())
	{
		return;
	}

	for (const FLLMNPCConversationMessage& Message :
		DialogueComponent->GetConversationSession()->GetMessages())
	{
		AddMessageRow(Message);
	}
}

void ULLMNPCChatWidget::SubmitCurrentText()
{
	if (!DialogueComponent || !InputBox)
	{
		return;
	}

	const FString Message = InputBox->GetText().ToString().TrimStartAndEnd();
	if (Message.IsEmpty())
	{
		return;
	}

	if (DialogueComponent->SendPlayerMessage(Message))
	{
		InputBox->SetText(FText::GetEmpty());
	}
}

void ULLMNPCChatWidget::UnbindDialogueComponent()
{
	if (DialogueComponent)
	{
		DialogueComponent->OnMessageAdded.RemoveDynamic(this, &ULLMNPCChatWidget::HandleMessageAdded);
		DialogueComponent->OnStateChanged.RemoveDynamic(this, &ULLMNPCChatWidget::HandleStateChanged);
		DialogueComponent->OnTurnCompleted.RemoveDynamic(this, &ULLMNPCChatWidget::HandleTurnCompleted);
	}
	DialogueComponent = nullptr;
}
