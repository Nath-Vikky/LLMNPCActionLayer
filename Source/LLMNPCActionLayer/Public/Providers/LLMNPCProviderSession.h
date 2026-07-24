#pragma once

#include "CoreMinimal.h"

struct LLMNPCACTIONLAYER_API FLLMNPCProviderSessionOverrides
{
	FString BaseUrl;
	FString Model;
	FString NonSecretConfigHash;

	bool IsValid() const
	{
		return !BaseUrl.TrimStartAndEnd().IsEmpty() &&
			!Model.TrimStartAndEnd().IsEmpty();
	}
};

class LLMNPCACTIONLAYER_API FLLMNPCProviderSession
{
public:
	static void SetSessionOverrides(
		FName ProviderId,
		const FLLMNPCProviderSessionOverrides& Overrides
	);
	static void ClearSessionOverrides(FName ProviderId);
	static void ClearAllSessionOverrides();
	static bool GetSessionOverrides(
		FName ProviderId,
		FLLMNPCProviderSessionOverrides& OutOverrides
	);
};
