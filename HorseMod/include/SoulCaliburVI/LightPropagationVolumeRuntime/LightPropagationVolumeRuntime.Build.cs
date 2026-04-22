using UnrealBuildTool;

public class LightPropagationVolumeRuntime : ModuleRules {
    public LightPropagationVolumeRuntime(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Renderer",
        });
    }
}
