#include "Providers/LLMNPCMockProvider.h"

namespace
{
const FName MockNotConfiguredError(TEXT("LLMNPC_MOCK_NOT_CONFIGURED"));
}

void FLLMNPCMockProvider::SendTurn(
	const FLLMNPCModelTurnRequest& Request,
	FLLMNPCModelTurnCallback Callback
)
{
	FLLMNPCModelTurnResult Result;
	Result.RequestId = Request.RequestId;
	Result.ErrorCode = MockNotConfiguredError;

	if (Callback)
	{
		Callback(Result);
	}
}

void FLLMNPCMockProvider::CancelRequest(const FGuid& RequestId)
{
	static_cast<void>(RequestId);
}
