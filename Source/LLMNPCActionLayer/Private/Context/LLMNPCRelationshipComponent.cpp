#include "Context/LLMNPCRelationshipComponent.h"

void ULLMNPCRelationshipComponent::SetRelationship(
	const FString& InOtherActorRef,
	float InFamiliarity,
	float InTrust,
	float InAffinity,
	const TArray<FName>& InTags
)
{
	OtherActorRef = InOtherActorRef.TrimStartAndEnd();
	Familiarity = FMath::Clamp(InFamiliarity, 0.0f, 1.0f);
	Trust = FMath::Clamp(InTrust, -1.0f, 1.0f);
	Affinity = FMath::Clamp(InAffinity, -1.0f, 1.0f);
	RelationshipTags = InTags;
}

FLLMNPCRelationshipSnapshot ULLMNPCRelationshipComponent::GetRelationshipSnapshot() const
{
	FLLMNPCRelationshipSnapshot Snapshot;
	Snapshot.OtherActorRef = OtherActorRef;
	Snapshot.Familiarity = FMath::Clamp(Familiarity, 0.0f, 1.0f);
	Snapshot.Trust = FMath::Clamp(Trust, -1.0f, 1.0f);
	Snapshot.Affinity = FMath::Clamp(Affinity, -1.0f, 1.0f);
	Snapshot.RelationshipTags = RelationshipTags;
	return Snapshot;
}
