#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "LLMNPCChatWidget.generated.h"

class UButton;
class UEditableTextBox;
class ULLMNPCDialogueComponent;
class UScrollBox;
class UTextBlock;
class UVerticalBox;

UCLASS(BlueprintType, Blueprintable)
class LLMNPCACTIONLAYER_API ULLMNPCChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC|Dialogue UI")
	void BindDialogueComponent(ULLMNPCDialogueComponent* InDialogueComponent);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Dialogue UI")
	ULLMNPCDialogueComponent* GetDialogueComponent() const { return DialogueComponent; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCDialogueComponent> DialogueComponent;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> MessageHistory;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MessageList;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> InputBox;

	UPROPERTY(Transient)
	TObjectPtr<UButton> SendButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LastActionText;

	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleMessageAdded(const FLLMNPCConversationMessage& Message);

	UFUNCTION()
	void HandleStateChanged(ELLMNPCDialogueState NewState);

	UFUNCTION()
	void HandleTurnCompleted(const FLLMNPCDialogueTurnResult& Result);

	void AddMessageRow(const FLLMNPCConversationMessage& Message);
	void RefreshHistory();
	void SubmitCurrentText();
	void UnbindDialogueComponent();
};
