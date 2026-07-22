// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LLMNPCActionLayer : ModuleRules
{
	public LLMNPCActionLayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"AnimGraphRuntime",
				"AnimationCore",
				"UMG"
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AIModule",
				"HTTP",
				"Json",
				"JsonUtilities",
				"Projects",
				"Slate",
				"SlateCore"
			}
			);
	}
}
