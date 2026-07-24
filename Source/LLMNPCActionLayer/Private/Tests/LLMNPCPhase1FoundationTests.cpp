#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "LLMNPCPostProcessAnimInstance.h"
#include "LLMNPCSettings.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 Phase1FoundationTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCDefaultPostProcessAssetTest,
	"LLMNPCActionLayer.Phase1.Foundation.DefaultPostProcessAsset",
	Phase1FoundationTestFlags
)

bool FLLMNPCDefaultPostProcessAssetTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	TestNotNull(TEXT("Runtime settings are available"), Settings);
	if (!Settings)
	{
		return false;
	}

	UClass* PostProcessClass = Settings->DefaultPostProcessAnimClass.LoadSynchronous();
	TestNotNull(TEXT("The default post-process AnimBP class loads"), PostProcessClass);
	if (PostProcessClass)
	{
		TestTrue(
			TEXT("The default AnimBP uses ULLMNPCPostProcessAnimInstance"),
			PostProcessClass->IsChildOf(ULLMNPCPostProcessAnimInstance::StaticClass())
		);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCMannySkeletonProfileTest,
	"LLMNPCActionLayer.Phase1.Foundation.MannySkeletonProfile",
	Phase1FoundationTestFlags
)

bool FLLMNPCMannySkeletonProfileTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const ULLMNPCSkeletonProfile* Profile = LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
	TestNotNull(TEXT("The Manny skeleton profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FString ValidationError;
	TestTrue(TEXT("The Manny skeleton profile validates"), Profile->ValidateProfile(ValidationError));
	if (!ValidationError.IsEmpty())
	{
		AddError(ValidationError);
	}

	TestEqual(TEXT("The profile ID is stable"), Profile->ProfileId, FName(TEXT("ue5_manny.v1")));
	TestEqual(TEXT("head resolves to the Manny head bone"), Profile->FindBoneName(TEXT("head")), FName(TEXT("head")));
	TestEqual(
		TEXT("right hand resolves to hand_r"),
		Profile->FindBoneName(TEXT("hand_right")),
		FName(TEXT("hand_r"))
	);
	TestTrue(TEXT("The profile contains both arm IK chains"), Profile->IKChains.Num() == 2);
	const FLLMNPCPoseBoneBindings Bindings = Profile->BuildPoseBoneBindings();
	TestTrue(
		TEXT("The Manny right-arm pole direction reaches the animation thread"),
		Bindings.RightArmIKPoleDirectionCS.Equals(FVector::BackwardVector)
	);
	TestTrue(
		TEXT("The Manny left-arm pole direction reaches the animation thread"),
		Bindings.LeftArmIKPoleDirectionCS.Equals(FVector::ForwardVector)
	);
	TestTrue(
		TEXT("The Manny component-space forward direction reaches the animation thread"),
		Bindings.ComponentForwardDirectionCS.Equals(FVector::RightVector)
	);
	TestTrue(
		TEXT("The Manny component-space up direction reaches the animation thread"),
		Bindings.ComponentUpDirectionCS.Equals(FVector::UpVector)
	);
	TestEqual(
		TEXT("The Manny right-arm reach limit reaches the animation thread"),
		Bindings.RightArmIKMaxReachScale,
		0.98f
	);
	TestEqual(
		TEXT("The Manny left-arm reach limit reaches the animation thread"),
		Bindings.LeftArmIKMaxReachScale,
		0.98f
	);
	TestTrue(TEXT("The profile contains approved finger poses"), Profile->FingerPoses.Num() >= 2);
	return true;
}

#endif
