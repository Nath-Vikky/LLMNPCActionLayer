#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "LLMNPCSkeletonProfileCommandlet.generated.h"

UCLASS()
class LLMNPCACTIONLAYEREDITOR_API ULLMNPCSkeletonProfileCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	ULLMNPCSkeletonProfileCommandlet();

	virtual int32 Main(const FString& Params) override;
};
