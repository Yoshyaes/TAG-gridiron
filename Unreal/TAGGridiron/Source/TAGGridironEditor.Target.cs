using UnrealBuildTool;

public class TAGGridironEditorTarget : TargetRules
{
	public TAGGridironEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TAGGridiron");
	}
}
