using UnrealBuildTool;

public class OculusHMD : ModuleRules {
    public OculusHMD(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "HeadMountedDisplay",
        });
    }
}
