#include "Providers/LLMNPCModelProviderRegistry.h"

#include "Misc/ScopeLock.h"

FLLMNPCModelProviderRegistry& FLLMNPCModelProviderRegistry::Get()
{
	static FLLMNPCModelProviderRegistry Registry;
	return Registry;
}

bool FLLMNPCModelProviderRegistry::RegisterProvider(
	FName ProviderId,
	const FLLMNPCModelProviderFactory& Factory,
	bool bReplaceExisting
)
{
	if (ProviderId.IsNone() || !Factory.IsBound())
	{
		return false;
	}

	FScopeLock Lock(&RegistryMutex);
	if (Factories.Contains(ProviderId) && !bReplaceExisting)
	{
		return false;
	}
	Factories.Add(ProviderId, Factory);
	return true;
}

bool FLLMNPCModelProviderRegistry::UnregisterProvider(FName ProviderId)
{
	FScopeLock Lock(&RegistryMutex);
	return Factories.Remove(ProviderId) > 0;
}

bool FLLMNPCModelProviderRegistry::IsProviderRegistered(FName ProviderId) const
{
	FScopeLock Lock(&RegistryMutex);
	return Factories.Contains(ProviderId);
}

TSharedPtr<ILLMNPCModelProvider> FLLMNPCModelProviderRegistry::CreateProvider(FName ProviderId) const
{
	FLLMNPCModelProviderFactory Factory;
	{
		FScopeLock Lock(&RegistryMutex);
		const FLLMNPCModelProviderFactory* FoundFactory = Factories.Find(ProviderId);
		if (!FoundFactory)
		{
			return nullptr;
		}
		Factory = *FoundFactory;
	}
	return Factory.Execute();
}

TArray<FName> FLLMNPCModelProviderRegistry::GetRegisteredProviderIds() const
{
	TArray<FName> ProviderIds;
	{
		FScopeLock Lock(&RegistryMutex);
		Factories.GetKeys(ProviderIds);
	}
	ProviderIds.Sort(FNameLexicalLess());
	return ProviderIds;
}
