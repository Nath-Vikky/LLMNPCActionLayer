#pragma once

#include "CoreMinimal.h"

class ULLMNPCSettings;

enum class ELLMNPCCredentialSource : uint8
{
	Missing,
	EditorSession,
	Environment
};

class LLMNPCACTIONLAYER_API FLLMNPCProviderCredentials
{
public:
	static void SetSessionSecret(FName ProviderId, const FString& Secret);
	static void ClearSessionSecret(FName ProviderId);
	static void ClearAllSessionSecrets();
	static bool HasSessionSecret(FName ProviderId);

	static bool ResolveDeepSeekApiKey(
		const ULLMNPCSettings& Settings,
		FString& OutApiKey,
		ELLMNPCCredentialSource& OutSource
	);

	static FName DeepSeekProviderId();
};
