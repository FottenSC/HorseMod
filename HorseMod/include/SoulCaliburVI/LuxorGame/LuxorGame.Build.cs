using UnrealBuildTool;

public class LuxorGame : ModuleRules {
    public LuxorGame(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "AnimGraphRuntime",
            "Core",
            "CoreUObject",
            "CriWareRuntime",
            "Engine",
            "InputCore",
            "LuxorSessionUtil",
            "MediaAssets",
            "ProceduralMeshComponent",
            "SlateCore",
            "UMG",
            "UMGUtil",
        });
    }
}
