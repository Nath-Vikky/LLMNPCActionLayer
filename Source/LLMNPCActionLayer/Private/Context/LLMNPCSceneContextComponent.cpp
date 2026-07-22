#include "Context/LLMNPCSceneContextComponent.h"

#include "GameFramework/Actor.h"

void ULLMNPCSceneContextComponent::RegisterSceneTarget(
	const FString& TargetRef,
	AActor* TargetActor,
	FName Category,
	const TArray<FName>& SemanticTags,
	float Salience
)
{
	const FString CleanRef = TargetRef.TrimStartAndEnd();
	if (CleanRef.IsEmpty())
	{
		return;
	}

	FLLMNPCSceneTargetRegistration* Registration = RegisteredTargets.FindByPredicate(
		[&CleanRef](const FLLMNPCSceneTargetRegistration& Candidate)
		{
			return Candidate.TargetRef == CleanRef;
		}
	);
	if (!TargetActor)
	{
		if (Registration)
		{
			RegisteredTargets.RemoveAll(
				[&CleanRef](const FLLMNPCSceneTargetRegistration& Candidate)
				{
					return Candidate.TargetRef == CleanRef;
				}
			);
		}
		return;
	}

	if (!Registration)
	{
		Registration = &RegisteredTargets.AddDefaulted_GetRef();
		Registration->TargetRef = CleanRef;
	}
	Registration->TargetActor = TargetActor;
	Registration->Category = Category.IsNone() ? FName(TEXT("generic")) : Category;
	Registration->SemanticTags = SemanticTags;
	Registration->Salience = FMath::Clamp(Salience, 0.0f, 1.0f);
	Registration->bAvailable = true;
}

void ULLMNPCSceneContextComponent::UnregisterSceneTarget(const FString& TargetRef)
{
	const FString CleanRef = TargetRef.TrimStartAndEnd();
	RegisteredTargets.RemoveAll(
		[&CleanRef](const FLLMNPCSceneTargetRegistration& Candidate)
		{
			return Candidate.TargetRef == CleanRef;
		}
	);
}

void ULLMNPCSceneContextComponent::SetTargetAvailable(const FString& TargetRef, bool bAvailable)
{
	const FString CleanRef = TargetRef.TrimStartAndEnd();
	if (FLLMNPCSceneTargetRegistration* Registration = RegisteredTargets.FindByPredicate(
		[&CleanRef](const FLLMNPCSceneTargetRegistration& Candidate)
		{
			return Candidate.TargetRef == CleanRef;
		}))
	{
		Registration->bAvailable = bAvailable;
	}
}

void ULLMNPCSceneContextComponent::SetStateActive(FName State, bool bActive)
{
	if (State.IsNone())
	{
		return;
	}
	if (bActive)
	{
		ActiveStates.AddUnique(State);
	}
	else
	{
		ActiveStates.Remove(State);
	}
}

FLLMNPCSelectionContextSnapshot ULLMNPCSceneContextComponent::AppendToSnapshot(
	FLLMNPCSelectionContextSnapshot Snapshot
) const
{
	Snapshot.ActiveStates = ActiveStates;
	Snapshot.AvailableTargets.Reset();
	for (const FLLMNPCSceneTargetRegistration& Registration : RegisteredTargets)
	{
		if (!Registration.bAvailable || !IsValid(Registration.TargetActor))
		{
			continue;
		}
		FLLMNPCSceneTargetContext& Target = Snapshot.AvailableTargets.AddDefaulted_GetRef();
		Target.TargetRef = Registration.TargetRef;
		Target.Category = Registration.Category;
		Target.SemanticTags = Registration.SemanticTags;
		Target.Salience = Registration.Salience;
	}
	Snapshot.AvailableTargets.Sort(
		[](const FLLMNPCSceneTargetContext& A, const FLLMNPCSceneTargetContext& B)
		{
			if (!FMath::IsNearlyEqual(A.Salience, B.Salience))
			{
				return A.Salience > B.Salience;
			}
			return A.TargetRef < B.TargetRef;
		}
	);
	Snapshot.ActiveStates.Sort(FNameLexicalLess());
	return Snapshot;
}

bool ULLMNPCSceneContextComponent::IsTargetAvailable(const FString& TargetRef) const
{
	return IsValid(ResolveSceneTarget(TargetRef));
}

AActor* ULLMNPCSceneContextComponent::ResolveSceneTarget(const FString& TargetRef) const
{
	const FString CleanRef = TargetRef.TrimStartAndEnd();
	const FLLMNPCSceneTargetRegistration* Registration = RegisteredTargets.FindByPredicate(
		[&CleanRef](const FLLMNPCSceneTargetRegistration& Candidate)
		{
			return Candidate.TargetRef == CleanRef;
		}
	);
	return Registration && Registration->bAvailable && IsValid(Registration->TargetActor)
		? Registration->TargetActor.Get()
		: nullptr;
}
