#pragma once

#include "CoreMinimal.h"
#include "LLMNPCProviderCapabilityTypes.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCProviderCapabilityProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Protocol")
	TArray<FString> SupportedTurnRequestSchemas = {
		TEXT("llmnpc.turn_request.v2")
	};

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Protocol")
	FString PreferredTurnRequestSchema = TEXT("llmnpc.turn_request.v2");

	bool SupportsTurnRequestSchema(const FString& SchemaVersion) const
	{
		return SupportedTurnRequestSchemas.Contains(SchemaVersion);
	}

	static FLLMNPCProviderCapabilityProfile V3()
	{
		FLLMNPCProviderCapabilityProfile Result;
		Result.SupportedTurnRequestSchemas = {
			TEXT("llmnpc.turn_request.v2"),
			TEXT("llmnpc.turn_request.v3")
		};
		Result.PreferredTurnRequestSchema = TEXT("llmnpc.turn_request.v3");
		return Result;
	}
};
