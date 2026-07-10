#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LLMNPCTemplatePreviewActor.generated.h"

class ULLMNPCMotionComponent;
class ULLMNPCMotionTemplate;
class USkeletalMeshComponent;

UCLASS(BlueprintType)
class LLMNPCACTIONLAYEREDITOR_API ALLMNPCTemplatePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ALLMNPCTemplatePreviewActor();

	virtual void BeginPlay() override;
	virtual bool IsEditorOnly() const override { return true; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category="LLM NPC|Authoring Preview")
	bool PreviewNow();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Authoring Preview")
	TObjectPtr<USkeletalMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Authoring Preview")
	TObjectPtr<ULLMNPCMotionComponent> MotionComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Authoring Preview")
	TObjectPtr<ULLMNPCMotionTemplate> PreviewTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Authoring Preview")
	bool bAutoPreviewOnBeginPlay = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Authoring Preview")
	FString LastPreviewError;
};
