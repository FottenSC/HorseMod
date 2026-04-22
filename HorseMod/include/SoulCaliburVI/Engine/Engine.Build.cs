using UnrealBuildTool;

public class Engine : ModuleRules {
    public Engine(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[] {
            "AIModule",
            "ClothingSystemRuntimeInterface",
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "PacketHandler",
            "Slate",
            "SlateCore",
            "UMG",
        });
    }
}
