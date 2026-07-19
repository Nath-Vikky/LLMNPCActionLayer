#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Blueprint.h"
#include "LLMNPCMotionComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6BlueprintInputBindingTest,
	"LLMNPCActionLayer.Phase6.Input.BlueprintKeyBindingDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FLLMNPCPhase6BlueprintInputBindingTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UBlueprint* TestBlueprint = LoadObject<UBlueprint>(
		nullptr,
		TEXT("/Game/LLMNPC/Blueprints/BP_LLMNPC_Manny.BP_LLMNPC_Manny")
	);
	if (!TestBlueprint || !TestBlueprint->GeneratedClass)
	{
		AddWarning(TEXT("The demo Manny Blueprint is unavailable; input-binding assertions were skipped."));
		return true;
	}

	TestTrue(
		TEXT("The demo Manny Blueprint exposes a compiled raw-key input binding"),
		ULLMNPCMotionComponent::HasBlueprintKeyInputBindings(TestBlueprint->GeneratedClass)
	);
	TestFalse(
		TEXT("A native component class does not opt into Blueprint key input"),
		ULLMNPCMotionComponent::HasBlueprintKeyInputBindings(ULLMNPCMotionComponent::StaticClass())
	);
	return true;
}

#endif
