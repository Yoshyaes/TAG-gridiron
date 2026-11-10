using UnrealBuildTool;

public class TAGGridiron : ModuleRules
{
	public TAGGridiron(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"UMG", "Slate", "SlateCore", "CinematicCamera", "Niagara", "IKRig", "AnimationCore", "MassSpawner", "MassEntity", "MassCommon", "MassCore",
			"PoseSearch", "BlendStack", "Chooser", "AnimGraphRuntime", "AnimationWarpingRuntime", "MetaHumanCharacterPalette", "MassRepresentation", "ProceduralMeshComponent", "ApplicationCore"
		});

		// The simulation core (Sim/) is plain standard C++ shared with the headless calibration tools.
		// Relax a couple of Unreal-specific warning-as-error rules for that portable code.
		ShadowVariableWarningLevel = WarningLevel.Warning;
		bEnableExceptions = false;
		PublicIncludePaths.Add(ModuleDirectory);
	}
}
