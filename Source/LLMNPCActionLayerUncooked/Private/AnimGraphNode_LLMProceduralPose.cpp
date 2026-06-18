#include "AnimGraphNode_LLMProceduralPose.h"

#define LOCTEXT_NAMESPACE "AnimGraphNode_LLMProceduralPose"

FText UAnimGraphNode_LLMProceduralPose::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("LLMProceduralPoseTitle", "LLM Procedural Pose");
}

FText UAnimGraphNode_LLMProceduralPose::GetTooltipText() const
{
	return LOCTEXT(
		"LLMProceduralPoseTooltip",
		"Applies validated LLM MotionClip procedural motion to a skeletal pose."
	);
}

FString UAnimGraphNode_LLMProceduralPose::GetNodeCategory() const
{
	return TEXT("LLM NPC");
}

FText UAnimGraphNode_LLMProceduralPose::GetControllerDescription() const
{
	return LOCTEXT("LLMProceduralPoseDescription", "LLM Procedural Pose");
}

#undef LOCTEXT_NAMESPACE
