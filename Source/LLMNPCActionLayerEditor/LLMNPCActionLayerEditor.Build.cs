using UnrealBuildTool;

public class LLMNPCActionLayerEditor : ModuleRules
{
	public LLMNPCActionLayerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"LLMNPCActionLayer"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimGraph",
				"AnimGraphRuntime",
				"BlueprintGraph",
				"GraphEditor",
				"KismetCompiler",
				"UnrealEd"
			}
		);
	}
}
