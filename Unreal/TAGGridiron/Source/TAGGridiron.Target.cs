using UnrealBuildTool;

public class TAGGridironTarget : TargetRules
{
	public TAGGridironTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TAGGridiron");
	}
}
