#pragma once

#include "CoreMinimal.h"
#include "Providers/LLMNPCModelProvider.h"

class LLMNPCACTIONLAYER_API FLLMNPCMockProvider final : public ILLMNPCModelProvider
{
public:
	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback Callback
	) override;

	virtual void CancelRequest(const FGuid& RequestId) override;

	virtual FName GetProviderId() const override { return TEXT("mock"); }
};
