using UnrealBuildTool;

public class OnlineSubsystemUtils : ModuleRules {
    public OnlineSubsystemUtils(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "OnlineSubsystem",
        });
    }
}
