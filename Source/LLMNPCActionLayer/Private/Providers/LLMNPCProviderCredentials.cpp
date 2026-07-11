#include "Providers/LLMNPCProviderCredentials.h"

#include "LLMNPCSettings.h"
#include "Misc/ScopeLock.h"

namespace
{
FCriticalSection SessionSecretsMutex;
TMap<FName, FString> SessionSecrets;
const FName DeepSeekId(TEXT("deepseek_direct_editor"));
}

void FLLMNPCProviderCredentials::SetSessionSecret(FName ProviderId, const FString& Secret)
{
#if WITH_EDITOR
	if (ProviderId.IsNone())
	{
		return;
	}
	FScopeLock Lock(&SessionSecretsMutex);
	const FString CleanSecret = Secret.TrimStartAndEnd();
	if (CleanSecret.IsEmpty())
	{
		SessionSecrets.Remove(ProviderId);
	}
	else
	{
		SessionSecrets.Add(ProviderId, CleanSecret);
	}
#else
	static_cast<void>(ProviderId);
	static_cast<void>(Secret);
#endif
}

void FLLMNPCProviderCredentials::ClearSessionSecret(FName ProviderId)
{
	FScopeLock Lock(&SessionSecretsMutex);
	SessionSecrets.Remove(ProviderId);
}

void FLLMNPCProviderCredentials::ClearAllSessionSecrets()
{
	FScopeLock Lock(&SessionSecretsMutex);
	SessionSecrets.Reset();
}

bool FLLMNPCProviderCredentials::HasSessionSecret(FName ProviderId)
{
	FScopeLock Lock(&SessionSecretsMutex);
	return SessionSecrets.Contains(ProviderId);
}

bool FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(
	const ULLMNPCSettings& Settings,
	FString& OutApiKey,
	ELLMNPCCredentialSource& OutSource
)
{
	OutApiKey.Reset();
	OutSource = ELLMNPCCredentialSource::Missing;
#if WITH_EDITOR
	{
		FScopeLock Lock(&SessionSecretsMutex);
		if (const FString* SessionSecret = SessionSecrets.Find(DeepSeekId))
		{
			OutApiKey = *SessionSecret;
			OutSource = ELLMNPCCredentialSource::EditorSession;
			return true;
		}
	}
#endif

	const FString EnvironmentVariable = Settings.ApiKeyEnvironmentVariable.TrimStartAndEnd();
	if (!EnvironmentVariable.IsEmpty())
	{
		OutApiKey = FPlatformMisc::GetEnvironmentVariable(*EnvironmentVariable).TrimStartAndEnd();
		if (!OutApiKey.IsEmpty())
		{
			OutSource = ELLMNPCCredentialSource::Environment;
			return true;
		}
	}
	return false;
}

FName FLLMNPCProviderCredentials::DeepSeekProviderId()
{
	return DeepSeekId;
}
