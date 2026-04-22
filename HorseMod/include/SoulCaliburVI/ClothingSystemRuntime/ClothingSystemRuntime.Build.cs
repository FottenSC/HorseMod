using UnrealBuildTool;

public class ClothingSystemRuntime : ModuleRules {
    public ClothingSystemRuntime(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "ClothingSystemRuntimeInterface",
            "Core",
            "CoreUObject",
            "Engine",
        });
    }
}
