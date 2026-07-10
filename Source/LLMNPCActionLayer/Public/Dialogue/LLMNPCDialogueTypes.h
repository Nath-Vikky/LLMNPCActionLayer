#pragma once

#include "CoreMinimal.h"
#include "LLMNPCDialogueTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCModelProviderKind : uint8
{
	UseProjectSettings,
	Mock,
	BackendProxy,
	DeepSeekDirectEditorOnly
};

UENUM(BlueprintType)
enum class ELLMNPCDialogueRole : uint8
{
	Player,
	Assistant,
	System
};

UENUM(BlueprintType)
enum class ELLMNPCDialogueState : uint8
{
	Idle,
	Sending,
	Receiving,
	Parsing,
	Validating,
	Executing,
	Failed,
	Cancelled,
	TimedOut
};

USTRUCT(BlueprintType)
struct FLLMNPCConversationMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FGuid MessageId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	ELLMNPCDialogueRole Role = ELLMNPCDialogueRole::Player;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FString Content;
};

USTRUCT(BlueprintType)
struct FLLMNPCSelectedAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FName Decision = TEXT("none");

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FName TemplateId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FString TargetRef;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	float Amplitude = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	float SpeedScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	float DurationScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FName Style = TEXT("neutral");

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FName ReasonTag = NAME_None;
};

USTRUCT(BlueprintType)
struct FLLMNPCNavigationIntent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FName Decision = TEXT("none");

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FString TargetRef;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	float AcceptanceRadiusCm = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCModelTurnDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FString SchemaVersion;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FString AssistantText;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FLLMNPCSelectedAction Action;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model Turn")
	FLLMNPCNavigationIntent Locomotion;
};

USTRUCT(BlueprintType)
struct FLLMNPCDialogueTurnResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	bool bTextResponseReceived = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	bool bActionExecuted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	bool bUsedLocalFallback = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FString AssistantText;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FName SelectedActionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FName ResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue")
	FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct FLLMNPCDialogueDebugState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	ELLMNPCDialogueState State = ELLMNPCDialogueState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FGuid ActiveRequestId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FName ProviderId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FName LastSelectedActionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FName LastResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FName LastErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	FString LastErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Dialogue|Debug")
	int32 MessageCount = 0;
};
