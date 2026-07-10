#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LLMNPCSettings.generated.h"

class UAnimInstance;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="LLM NPC Motion Layer"))
class LLMNPCACTIONLAYER_API ULLMNPCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULLMNPCSettings();

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	FString ProviderEndpoint = TEXT("http://localhost:8787/npc/motion-plan");

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	FString ApiKeyEnvironmentVariable = TEXT("OPENAI_API_KEY");

	UPROPERTY(Config, EditAnywhere, Category="LLM API")
	bool bAllowDirectProviderCallInEditorOnly = false;

	UPROPERTY(Config, EditAnywhere, Category="Runtime", meta=(ClampMin="1.0", ClampMax="60.0"))
	float RequestTimeoutSeconds = 8.0f;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Post Process")
	TSoftClassPtr<UAnimInstance> DefaultPostProcessAnimClass;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TArray<FString> MotionTemplateScanPaths;

	UPROPERTY(Config, EditAnywhere, Category="Runtime|Templates")
	TArray<FString> SkeletonProfileScanPaths;
};
