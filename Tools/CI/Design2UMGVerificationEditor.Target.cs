using UnrealBuildTool;

public class Design2UMGVerificationEditorTarget : TargetRules
{
    public Design2UMGVerificationEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        DisableUnityBuildForModules = new[] { "Design2UMGCore", "Design2UMGEditor" };
        ExtraModuleNames.Add("UnrealGame");
    }
}
