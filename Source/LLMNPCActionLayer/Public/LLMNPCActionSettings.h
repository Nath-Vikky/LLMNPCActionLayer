#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LLMNPCActionSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="LLM NPC Action Layer"))
class LLMNPCACTIONLAYER_API ULLMNPCActionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="LLM")
	FString ProviderEndpoint = TEXT("http://localhost:8787/npc/action-plan");

	UPROPERTY(Config, EditAnywhere, Category="LLM")
	FString ModelName = TEXT("your-model-name");

	UPROPERTY(Config, EditAnywhere, Category="LLM")
	FString ApiKeyEnvironmentVariable = TEXT("OPENAI_API_KEY");

	UPROPERTY(Config, EditAnywhere, Category="LLM")
	bool bAllowDirectProviderCallInEditorOnly = false;

	UPROPERTY(Config, EditAnywhere, Category="Runtime", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxActionsPerPlan = 3;

	UPROPERTY(Config, EditAnywhere, Category="Runtime", meta=(ClampMin="1.0", ClampMax="60.0"))
	float RequestTimeoutSeconds = 8.0f;
};
