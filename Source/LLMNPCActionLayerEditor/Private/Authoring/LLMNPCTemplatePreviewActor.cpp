#include "Authoring/LLMNPCTemplatePreviewActor.h"

#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionTypes.h"
#include "Templates/LLMNPCMotionTemplate.h"

ALLMNPCTemplatePreviewActor::ALLMNPCTemplatePreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(Root);
	MotionComponent = CreateDefaultSubobject<ULLMNPCMotionComponent>(TEXT("MotionComponent"));
}

void ALLMNPCTemplatePreviewActor::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPreviewOnBeginPlay && PreviewTemplate)
	{
		PreviewNow();
	}
}

bool ALLMNPCTemplatePreviewActor::PreviewNow()
{
	LastPreviewError.Reset();
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		LastPreviewError = TEXT("LLMNPC_AUTHORING_PREVIEW_REQUIRES_PIE");
		return false;
	}
	if (!PreviewTemplate || !MotionComponent)
	{
		LastPreviewError = TEXT("LLMNPC_AUTHORING_PREVIEW_INPUT_MISSING");
		return false;
	}
	if (PreviewTemplate->Metadata.bRequiresTarget)
	{
		AActor* PreviewTarget =
			UGameplayStatics::GetPlayerPawn(this, 0);
		if (!IsValid(PreviewTarget) || PreviewTarget == this)
		{
			LastPreviewError =
				TEXT("LLMNPC_AUTHORING_PREVIEW_TARGET_UNAVAILABLE");
			return false;
		}
		MotionComponent->RegisterTarget(
			TEXT("authoring_preview_target"),
			PreviewTarget
		);
	}

	FLLMMotionPlan Plan;
	if (!ULLMNPCTemplateAuthoringSubsystem::CompileTemplateForPreview(
		*PreviewTemplate,
		Plan,
		LastPreviewError
	))
	{
		return false;
	}
	if (!MotionComponent->SubmitCompiledTemplatePlan(MoveTemp(Plan)))
	{
		LastPreviewError = MotionComponent->LastValidationError;
		return false;
	}
	return true;
}
