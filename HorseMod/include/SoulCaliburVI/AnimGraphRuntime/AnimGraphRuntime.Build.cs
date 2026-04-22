using UnrealBuildTool;

public class AnimGraphRuntime : ModuleRules {
    public AnimGraphRuntime(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "AnimationCore",
            "Core",
            "CoreUObject",
            "Engine",
        });
    }
}
