#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Widgets/SCompoundWidget.h"

class FLLMNPCDeepSeekProvider;
class SEditableTextBox;

struct FLLMNPCProviderOption
{
	ELLMNPCModelProviderKind Kind = ELLMNPCModelProviderKind::Mock;
	FText Label;
};

class SLLMNPCProviderSettings final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLLMNPCProviderSettings) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SLLMNPCProviderSettings() override;

private:
	TArray<TSharedPtr<FLLMNPCProviderOption>> ProviderOptions;
	TSharedPtr<FLLMNPCProviderOption> SelectedProvider;
	TSharedPtr<SEditableTextBox> ApiKeyTextBox;
	TSharedPtr<FLLMNPCDeepSeekProvider> ActiveTestProvider;
	FGuid ActiveTestRequestId;

	FString BackendEndpoint;
	FString DeepSeekBaseUrl;
	FString DeepSeekModel;
	FString ApiKeyEnvironmentVariable;
	FString PendingApiKey;
	float Temperature = 0.2f;
	float TimeoutSeconds = 8.0f;
	int32 MaxTokens = 1200;
	int32 MaxRetries = 2;
	bool bAllowDirectInEditor = false;
	bool bTestInFlight = false;
	bool bStatusError = false;
	FText StatusText;

	TSharedRef<SWidget> MakeSectionHeader(const FText& Text) const;
	TSharedRef<SWidget> MakeFormRow(const FText& Label, const TSharedRef<SWidget>& Control) const;
	TSharedRef<SWidget> GenerateProviderOption(TSharedPtr<FLLMNPCProviderOption> Option) const;
	FText GetSelectedProviderText() const;
	FText GetCredentialSourceText() const;
	FSlateColor GetStatusColor() const;
	bool CanTestDeepSeek() const;

	void HandleProviderChanged(TSharedPtr<FLLMNPCProviderOption> Option, ESelectInfo::Type SelectInfo);
	FReply HandleApplySettings();
	FReply HandleApplyToSelectedNPCs();
	FReply HandleClearSessionKey();
	FReply HandleTestDeepSeek();
	bool ApplySettings(bool bSaveConfig);
	void HandleTestCompleted(const struct FLLMNPCModelTurnResult& Result);
	void SetStatus(const FText& Text, bool bError);
};
