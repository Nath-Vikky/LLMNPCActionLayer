#include "Providers/LLMNPCProviderSession.h"

#include "Misc/ScopeLock.h"

namespace
{
FCriticalSection SessionOverridesMutex;
TMap<FName, FLLMNPCProviderSessionOverrides> SessionOverrides;
}

void FLLMNPCProviderSession::SetSessionOverrides(
	FName ProviderId,
	const FLLMNPCProviderSessionOverrides& Overrides
)
{
#if WITH_EDITOR
	if (ProviderId.IsNone())
	{
		return;
	}

	FLLMNPCProviderSessionOverrides CleanOverrides = Overrides;
	CleanOverrides.BaseUrl = CleanOverrides.BaseUrl.TrimStartAndEnd();
	CleanOverrides.Model = CleanOverrides.Model.TrimStartAndEnd();
	CleanOverrides.NonSecretConfigHash = CleanOverrides.NonSecretConfigHash.TrimStartAndEnd();

	FScopeLock Lock(&SessionOverridesMutex);
	if (CleanOverrides.IsValid())
	{
		SessionOverrides.Add(ProviderId, MoveTemp(CleanOverrides));
	}
	else
	{
		SessionOverrides.Remove(ProviderId);
	}
#else
	static_cast<void>(ProviderId);
	static_cast<void>(Overrides);
#endif
}

void FLLMNPCProviderSession::ClearSessionOverrides(FName ProviderId)
{
	FScopeLock Lock(&SessionOverridesMutex);
	SessionOverrides.Remove(ProviderId);
}

void FLLMNPCProviderSession::ClearAllSessionOverrides()
{
	FScopeLock Lock(&SessionOverridesMutex);
	SessionOverrides.Reset();
}

bool FLLMNPCProviderSession::GetSessionOverrides(
	FName ProviderId,
	FLLMNPCProviderSessionOverrides& OutOverrides
)
{
	OutOverrides = FLLMNPCProviderSessionOverrides();
#if WITH_EDITOR
	FScopeLock Lock(&SessionOverridesMutex);
	if (const FLLMNPCProviderSessionOverrides* Found = SessionOverrides.Find(ProviderId))
	{
		OutOverrides = *Found;
		return OutOverrides.IsValid();
	}
#else
	static_cast<void>(ProviderId);
#endif
	return false;
}
