#include "UI/SLLMNPCProviderSettings.h"

#include "Async/Async.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "LLMNPCSettings.h"
#include "Dialogue/LLMNPCDialogueComponent.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
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

#define LOCTEXT_NAMESPACE "SLLMNPCProviderSettings"

namespace
{
bool IsHttpUrl(const FString& Value)
{
	const FString Clean = Value.TrimStartAndEnd();
	return Clean.StartsWith(TEXT("http://")) || Clean.StartsWith(TEXT("https://"));
}

FText CredentialSourceLabel(ELLMNPCCredentialSource Source)
{
	switch (Source)
	{
	case ELLMNPCCredentialSource::EditorSession:
		return LOCTEXT("CredentialSession", "Editor session");
	case ELLMNPCCredentialSource::Environment:
		return LOCTEXT("CredentialEnvironment", "Environment variable");
	case ELLMNPCCredentialSource::Missing:
	default:
		return LOCTEXT("CredentialMissing", "Missing");
	}
}
}

void SLLMNPCProviderSettings::Construct(const FArguments& InArgs)
{
	static_cast<void>(InArgs);
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	BackendEndpoint = Settings->BackendProxyEndpoint;
	DeepSeekBaseUrl = Settings->DeepSeekBaseUrl;
	DeepSeekModel = Settings->DeepSeekModel;
	ApiKeyEnvironmentVariable = Settings->ApiKeyEnvironmentVariable;
	Temperature = Settings->DeepSeekTemperature;
	MaxTokens = Settings->DeepSeekMaxTokens;
	TimeoutSeconds = Settings->RequestTimeoutSeconds;
	MaxRetries = Settings->MaxProviderRetries;
	bAllowDirectInEditor = Settings->bAllowDirectProviderCallInEditorOnly;
	StatusText = LOCTEXT("ReadyStatus", "Ready");

	ProviderOptions = {
		MakeShared<FLLMNPCProviderOption>(FLLMNPCProviderOption{ ELLMNPCModelProviderKind::Mock, LOCTEXT("ProviderMock", "Mock") }),
		MakeShared<FLLMNPCProviderOption>(FLLMNPCProviderOption{ ELLMNPCModelProviderKind::BackendProxy, LOCTEXT("ProviderBackend", "Backend Proxy") }),
		MakeShared<FLLMNPCProviderOption>(FLLMNPCProviderOption{ ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly, LOCTEXT("ProviderDeepSeek", "DeepSeek Direct (Editor)") })
	};
	SelectedProvider = ProviderOptions[0];
	for (const TSharedPtr<FLLMNPCProviderOption>& Option : ProviderOptions)
	{
		if (Option->Kind == Settings->DefaultModelProvider)
		{
			SelectedProvider = Option;
			break;
		}
	}

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
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Title", "LLM NPC Provider"))
						.TextStyle(FAppStyle::Get(), "LargeText")
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SLLMNPCProviderSettings::GetCredentialSourceText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("ProviderSection", "Provider"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeFormRow(
						LOCTEXT("DefaultProvider", "Default Provider"),
						SNew(SComboBox<TSharedPtr<FLLMNPCProviderOption>>)
						.OptionsSource(&ProviderOptions)
						.InitiallySelectedItem(SelectedProvider)
						.OnGenerateWidget(this, &SLLMNPCProviderSettings::GenerateProviderOption)
						.OnSelectionChanged(this, &SLLMNPCProviderSettings::HandleProviderChanged)
						[
							SNew(STextBlock).Text(this, &SLLMNPCProviderSettings::GetSelectedProviderText)
						]
					)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("DeepSeekSection", "DeepSeek"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("BaseUrl", "Base URL"),
						SNew(SEditableTextBox)
						.Text(FText::FromString(DeepSeekBaseUrl))
						.OnTextChanged_Lambda([this](const FText& Value) { DeepSeekBaseUrl = Value.ToString(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("Model", "Model"),
						SNew(SEditableTextBox)
						.Text(FText::FromString(DeepSeekModel))
						.OnTextChanged_Lambda([this](const FText& Value) { DeepSeekModel = Value.ToString(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("ApiKey", "API Key"),
						SAssignNew(ApiKeyTextBox, SEditableTextBox)
						.IsPassword(true)
						.HintText(LOCTEXT("ApiKeyHint", "Session only"))
						.OnTextChanged_Lambda([this](const FText& Value) { PendingApiKey = Value.ToString(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("EnvironmentVariable", "Environment Variable"),
						SNew(SEditableTextBox)
						.Text(FText::FromString(ApiKeyEnvironmentVariable))
						.OnTextChanged_Lambda([this](const FText& Value) { ApiKeyEnvironmentVariable = Value.ToString(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("Temperature", "Temperature"),
						SNew(SNumericEntryBox<float>)
						.MinValue(0.0f).MaxValue(2.0f)
						.Value_Lambda([this]() { return TOptional<float>(Temperature); })
						.OnValueChanged_Lambda([this](float Value) { Temperature = Value; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("MaxTokens", "Max Tokens"),
						SNew(SNumericEntryBox<int32>)
						.MinValue(128).MaxValue(8192)
						.Value_Lambda([this]() { return TOptional<int32>(MaxTokens); })
						.OnValueChanged_Lambda([this](int32 Value) { MaxTokens = Value; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("AllowDirect", "Allow Direct In Editor"),
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bAllowDirectInEditor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAllowDirectInEditor = State == ECheckBoxState::Checked; }))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("BackendSection", "Backend Proxy"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("BackendEndpoint", "Endpoint"),
						SNew(SEditableTextBox)
						.Text(FText::FromString(BackendEndpoint))
						.OnTextChanged_Lambda([this](const FText& Value) { BackendEndpoint = Value.ToString(); }))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 8.0f)
				[
					MakeSectionHeader(LOCTEXT("RequestSection", "Request"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("Timeout", "Timeout Seconds"),
						SNew(SNumericEntryBox<float>)
						.MinValue(1.0f).MaxValue(60.0f)
						.Value_Lambda([this]() { return TOptional<float>(TimeoutSeconds); })
						.OnValueChanged_Lambda([this](float Value) { TimeoutSeconds = Value; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeFormRow(LOCTEXT("Retries", "Max Retries"),
						SNew(SNumericEntryBox<int32>)
						.MinValue(0).MaxValue(5)
						.Value_Lambda([this]() { return TOptional<int32>(MaxRetries); })
						.OnValueChanged_Lambda([this](int32 Value) { MaxRetries = Value; }))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 18.0f, 0.0f, 8.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FMargin(4.0f, 0.0f))
					+ SUniformGridPanel::Slot(0, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("ApplyTooltip", "Save non-secret provider settings."))
						.OnClicked(this, &SLLMNPCProviderSettings::HandleApplySettings)
						[
							SNew(STextBlock).Text(LOCTEXT("Apply", "Apply Settings")).Justification(ETextJustify::Center)
						]
					]
					+ SUniformGridPanel::Slot(1, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("SelectedTooltip", "Apply the selected provider to selected NPC actors."))
						.OnClicked(this, &SLLMNPCProviderSettings::HandleApplyToSelectedNPCs)
						[
							SNew(STextBlock).Text(LOCTEXT("ApplySelected", "Apply To Selected NPCs")).Justification(ETextJustify::Center)
						]
					]
					+ SUniformGridPanel::Slot(2, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("TestTooltip", "Send a structured no-action test turn to DeepSeek."))
						.IsEnabled(this, &SLLMNPCProviderSettings::CanTestDeepSeek)
						.OnClicked(this, &SLLMNPCProviderSettings::HandleTestDeepSeek)
						[
							SNew(STextBlock).Text(LOCTEXT("Test", "Test DeepSeek")).Justification(ETextJustify::Center)
						]
					]
					+ SUniformGridPanel::Slot(3, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("ClearTooltip", "Remove the DeepSeek key from this editor session."))
						.OnClicked(this, &SLLMNPCProviderSettings::HandleClearSessionKey)
						[
							SNew(STextBlock).Text(LOCTEXT("Clear", "Clear Session Key")).Justification(ETextJustify::Center)
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 12.0f, 4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return StatusText; })
					.ColorAndOpacity(this, &SLLMNPCProviderSettings::GetStatusColor)
				]
			]
		]
	];
}

SLLMNPCProviderSettings::~SLLMNPCProviderSettings()
{
	if (ActiveTestProvider && ActiveTestRequestId.IsValid())
	{
		ActiveTestProvider->CancelRequest(ActiveTestRequestId);
	}
	ActiveTestProvider.Reset();
	PendingApiKey.Reset();
}

TSharedRef<SWidget> SLLMNPCProviderSettings::MakeSectionHeader(const FText& Text) const
{
	return SNew(STextBlock).Text(Text).TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle");
}

TSharedRef<SWidget> SLLMNPCProviderSettings::MakeFormRow(const FText& Label, const TSharedRef<SWidget>& Control) const
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
			SNew(SBox).MaxDesiredWidth(620.0f)[Control]
		];
}

TSharedRef<SWidget> SLLMNPCProviderSettings::GenerateProviderOption(TSharedPtr<FLLMNPCProviderOption> Option) const
{
	return SNew(STextBlock).Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
}

FText SLLMNPCProviderSettings::GetSelectedProviderText() const
{
	return SelectedProvider.IsValid() ? SelectedProvider->Label : FText::GetEmpty();
}

FText SLLMNPCProviderSettings::GetCredentialSourceText() const
{
	FString ApiKey;
	ELLMNPCCredentialSource Source = ELLMNPCCredentialSource::Missing;
	FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(*GetDefault<ULLMNPCSettings>(), ApiKey, Source);
	ApiKey.Reset();
	return FText::Format(LOCTEXT("CredentialSourceFormat", "Credential: {0}"), CredentialSourceLabel(Source));
}

FSlateColor SLLMNPCProviderSettings::GetStatusColor() const
{
	return bStatusError
		? FSlateColor(FLinearColor(0.95f, 0.28f, 0.24f))
		: FSlateColor(FLinearColor(0.25f, 0.78f, 0.42f));
}

bool SLLMNPCProviderSettings::CanTestDeepSeek() const
{
	return !bTestInFlight && SelectedProvider.IsValid() &&
		SelectedProvider->Kind == ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly;
}

void SLLMNPCProviderSettings::HandleProviderChanged(
	TSharedPtr<FLLMNPCProviderOption> Option,
	ESelectInfo::Type SelectInfo
)
{
	static_cast<void>(SelectInfo);
	if (Option.IsValid())
	{
		SelectedProvider = MoveTemp(Option);
	}
}

FReply SLLMNPCProviderSettings::HandleApplySettings()
{
	if (ApplySettings(true))
	{
		SetStatus(LOCTEXT("AppliedStatus", "Settings applied"), false);
	}
	return FReply::Handled();
}

FReply SLLMNPCProviderSettings::HandleApplyToSelectedNPCs()
{
	if (!SelectedProvider.IsValid() || !GEditor)
	{
		SetStatus(LOCTEXT("NoProviderStatus", "Provider selection is invalid"), true);
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyProviderTransaction", "Apply LLM NPC Provider"));
	int32 UpdatedCount = 0;
	for (FSelectionIterator Iterator(*GEditor->GetSelectedActors()); Iterator; ++Iterator)
	{
		AActor* Actor = Cast<AActor>(*Iterator);
		ULLMNPCDialogueComponent* Dialogue = Actor ? Actor->FindComponentByClass<ULLMNPCDialogueComponent>() : nullptr;
		if (!Dialogue)
		{
			continue;
		}
		Dialogue->Modify();
		Dialogue->ProviderKind = SelectedProvider->Kind;
		Dialogue->MarkPackageDirty();
		++UpdatedCount;
	}
	SetStatus(
		UpdatedCount > 0
			? FText::Format(LOCTEXT("SelectedUpdated", "Updated {0} selected NPC(s)"), UpdatedCount)
			: LOCTEXT("NoSelectedNPC", "No selected actor has an LLM NPC Dialogue Component"),
		UpdatedCount == 0
	);
	return FReply::Handled();
}

FReply SLLMNPCProviderSettings::HandleClearSessionKey()
{
	FLLMNPCProviderCredentials::ClearSessionSecret(FLLMNPCProviderCredentials::DeepSeekProviderId());
	PendingApiKey.Reset();
	if (ApiKeyTextBox)
	{
		ApiKeyTextBox->SetText(FText::GetEmpty());
	}
	SetStatus(LOCTEXT("KeyCleared", "Session key cleared"), false);
	return FReply::Handled();
}

FReply SLLMNPCProviderSettings::HandleTestDeepSeek()
{
	if (!ApplySettings(true))
	{
		return FReply::Handled();
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	FString ApiKey;
	ELLMNPCCredentialSource Source = ELLMNPCCredentialSource::Missing;
	if (!FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(*Settings, ApiKey, Source))
	{
		SetStatus(LOCTEXT("MissingKeyStatus", "DeepSeek API key is missing"), true);
		return FReply::Handled();
	}
	ApiKey.Reset();
	static_cast<void>(Source);

	bTestInFlight = true;
	SetStatus(LOCTEXT("TestingStatus", "Testing DeepSeek connection..."), false);
	ActiveTestRequestId = FGuid::NewGuid();
	ActiveTestProvider = MakeShared<FLLMNPCDeepSeekProvider>();
	FLLMNPCModelTurnRequest Request;
	Request.RequestId = ActiveTestRequestId;
	Request.SessionId = FGuid::NewGuid();
	Request.NPCId = TEXT("provider_connection_test");
	Request.UserMessage = TEXT("connection test");
	Request.ContextJson = FString::Printf(
		TEXT("{\"schema_version\":\"llmnpc.turn_request.v2\",\"prompt_version\":\"%s\",")
		TEXT("\"request_id\":\"%s\",\"session_id\":\"%s\",\"npc_id\":\"provider_connection_test\",")
		TEXT("\"user_message\":\"Return a valid no-action llmnpc.model_turn.v1 object.\",")
		TEXT("\"context\":{},\"candidate_templates\":[]}"),
		*Settings->SelectionPromptVersion,
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*Request.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower)
	);

	const TWeakPtr<SLLMNPCProviderSettings> WeakPanel = SharedThis(this);
	ActiveTestProvider->SendTurn(
		Request,
		[WeakPanel](const FLLMNPCModelTurnResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakPanel, Result]()
			{
				if (const TSharedPtr<SLLMNPCProviderSettings> Pinned = WeakPanel.Pin())
				{
					Pinned->HandleTestCompleted(Result);
				}
			});
		}
	);
	return FReply::Handled();
}

bool SLLMNPCProviderSettings::ApplySettings(bool bSaveConfig)
{
	if (!SelectedProvider.IsValid())
	{
		SetStatus(LOCTEXT("InvalidProvider", "Provider selection is invalid"), true);
		return false;
	}
	DeepSeekBaseUrl = DeepSeekBaseUrl.TrimStartAndEnd();
	DeepSeekModel = DeepSeekModel.TrimStartAndEnd();
	BackendEndpoint = BackendEndpoint.TrimStartAndEnd();
	ApiKeyEnvironmentVariable = ApiKeyEnvironmentVariable.TrimStartAndEnd();
	if (SelectedProvider->Kind == ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly &&
		(!IsHttpUrl(DeepSeekBaseUrl) || DeepSeekModel.IsEmpty()))
	{
		SetStatus(LOCTEXT("InvalidDeepSeek", "DeepSeek Base URL or Model is invalid"), true);
		return false;
	}
	if (SelectedProvider->Kind == ELLMNPCModelProviderKind::BackendProxy && !IsHttpUrl(BackendEndpoint))
	{
		SetStatus(LOCTEXT("InvalidBackend", "Backend Proxy endpoint is invalid"), true);
		return false;
	}

	ULLMNPCSettings* Settings = GetMutableDefault<ULLMNPCSettings>();
	Settings->DefaultModelProvider = SelectedProvider->Kind;
	Settings->BackendProxyEndpoint = BackendEndpoint;
	Settings->DeepSeekBaseUrl = DeepSeekBaseUrl;
	Settings->DeepSeekModel = DeepSeekModel;
	Settings->ApiKeyEnvironmentVariable = ApiKeyEnvironmentVariable;
	Settings->DeepSeekTemperature = FMath::Clamp(Temperature, 0.0f, 2.0f);
	Settings->DeepSeekMaxTokens = FMath::Clamp(MaxTokens, 128, 8192);
	Settings->RequestTimeoutSeconds = FMath::Clamp(TimeoutSeconds, 1.0f, 60.0f);
	Settings->MaxProviderRetries = FMath::Clamp(MaxRetries, 0, 5);
	Settings->bAllowDirectProviderCallInEditorOnly = bAllowDirectInEditor;
	if (!PendingApiKey.TrimStartAndEnd().IsEmpty())
	{
		FLLMNPCProviderCredentials::SetSessionSecret(
			FLLMNPCProviderCredentials::DeepSeekProviderId(),
			PendingApiKey
		);
		PendingApiKey.Reset();
		if (ApiKeyTextBox)
		{
			ApiKeyTextBox->SetText(FText::GetEmpty());
		}
	}
	if (bSaveConfig && !Settings->TryUpdateDefaultConfigFile())
	{
		SetStatus(LOCTEXT("SaveFailed", "Failed to update the project config"), true);
		return false;
	}
	return true;
}

void SLLMNPCProviderSettings::HandleTestCompleted(const FLLMNPCModelTurnResult& Result)
{
	bTestInFlight = false;
	ActiveTestRequestId.Invalidate();
	ActiveTestProvider.Reset();
	if (Result.bSuccess)
	{
		SetStatus(
			FText::Format(LOCTEXT("ConnectedStatus", "DeepSeek connected (HTTP {0})"), Result.HttpStatus),
			false
		);
		return;
	}
	SetStatus(
		Result.HttpStatus > 0
			? FText::Format(
				LOCTEXT("TestHttpFailure", "DeepSeek test failed: HTTP {0} ({1})"),
				Result.HttpStatus,
				FText::FromName(Result.ErrorCode)
			)
			: FText::Format(
				LOCTEXT("TestFailure", "DeepSeek test failed: {0}"),
				FText::FromName(Result.ErrorCode)
			),
		true
	);
}

void SLLMNPCProviderSettings::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusError = bError;
}

#undef LOCTEXT_NAMESPACE
