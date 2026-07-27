#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Engine/DeveloperSettings.h"
#include "LLMNPCSettings.generated.h"

class UAnimInstance;
class ULLMNPCActionVocabulary;
class UUserWidget;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="LLM NPC Motion Layer"))
class LLMNPCACTIONLAYER_API ULLMNPCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULLMNPCSettings();

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	FString ProviderEndpoint = TEXT("http://localhost:8787/npc/motion-plan");

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider")
	ELLMNPCModelProviderKind DefaultModelProvider = ELLMNPCModelProviderKind::Mock;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider")
	FName DefaultProviderId = NAME_None;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider")
	FString BackendProxyEndpoint = TEXT("http://localhost:8787/v1/npc/turn");

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider")
	FString DeepSeekBaseUrl = TEXT("https://api.deepseek.com");

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider")
	FString DeepSeekModel = TEXT("deepseek-v4-flash");

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	FString ApiKeyEnvironmentVariable = TEXT("DEEPSEEK_API_KEY");

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	bool bAllowDirectProviderCallInEditorOnly = false;

	UPROPERTY(Config, EditAnywhere, Category="Authoring|Sandbox")
	bool bEnableAuthoringRuntimeSandbox = false;

	UPROPERTY(Config, EditAnywhere, Category="Authoring|Sandbox", meta=(ClampMin="2.0", ClampMax="120.0"))
	float AuthoringSandboxRequestTimeoutSeconds = 30.0f;

	UPROPERTY(Config, EditAnywhere, Category="Authoring|Sandbox", meta=(ClampMin="4.0", ClampMax="30.0"))
	float AuthoringSandboxPreviewWatchdogSeconds = 8.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime", meta=(ClampMin="1.0", ClampMax="60.0"))
	float RequestTimeoutSeconds = 8.0f;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider", meta=(ClampMin="2.0", ClampMax="300.0"))
	float DialogueRequestWatchdogSeconds = 30.0f;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider", meta=(ClampMin="0", ClampMax="5"))
	int32 MaxProviderRetries = 2;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Provider", meta=(ClampMin="0.05", ClampMax="5.0"))
	float ProviderRetryBaseDelaySeconds = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|DeepSeek", meta=(ClampMin="0.0", ClampMax="2.0"))
	float DeepSeekTemperature = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|DeepSeek", meta=(ClampMin="128", ClampMax="8192"))
	int32 DeepSeekMaxTokens = 1200;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|DeepSeek", meta=(MultiLine="true"))
	FString DeepSeekSystemPrompt;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Selection")
	FString SelectionPromptVersion = TEXT("llmnpc.selection_prompt.v3");

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Selection", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxContextCandidates = 8;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Selection", meta=(ClampMin="0.0", ClampMax="30.0"))
	float RepeatSuppressionSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Selection", meta=(ClampMin="8", ClampMax="2048"))
	int32 MaxSelectionAnalyticsEvents = 128;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|Selection")
	bool bEnableLocalSelectionTelemetry = false;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior")
	bool bEnableNavigationIntents = true;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior")
	bool bSpawnDefaultAIController = true;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="50.0", ClampMax="500.0"))
	float NavigationDefaultAcceptanceRadiusCm = 150.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="25.0", ClampMax="250.0"))
	float NavigationMinAcceptanceRadiusCm = 50.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="100.0", ClampMax="1000.0"))
	float NavigationMaxAcceptanceRadiusCm = 500.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="1.0", ClampMax="120.0"))
	float BehaviorPlanTimeoutSeconds = 20.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="1.0", ClampMax="120.0"))
	float NavigationMoveTimeoutSeconds = 15.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="0.1", ClampMax="10.0"))
	float TargetFacingTimeoutSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="30.0", ClampMax="1080.0"))
	float TargetFacingTurnRateDegreesPerSecond = 360.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="1.0", ClampMax="45.0"))
	float TargetFacingToleranceDegrees = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Behavior", meta=(ClampMin="0.01", ClampMax="0.25"))
	float BehaviorTickIntervalSeconds = 0.05f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Post Process")
	TSoftClassPtr<UAnimInstance> DefaultPostProcessAnimClass;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TArray<FString> MotionTemplateScanPaths;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TArray<FString> PublicActionDefinitionScanPaths;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TSoftObjectPtr<ULLMNPCActionVocabulary> ActionVocabulary;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	FName CanonicalModelLanguage = TEXT("en");

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates", meta=(ClampMin="256", ClampMax="32768"))
	int32 CandidateTokenBudget = 4096;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	FString ProjectPublishedTemplatePath =
		TEXT("/Game/LLMNPCActionLayer/MotionTemplates/Published");

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	FString ProjectPublishedPublicActionPath =
		TEXT("/Game/LLMNPCActionLayer/PublicActions/Published");

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TArray<FString> SkeletonProfileScanPaths;

	UPROPERTY(Config, EditAnywhere, Category="Dialogue|UI")
	TSoftClassPtr<UUserWidget> DefaultChatWidgetClass;
};
