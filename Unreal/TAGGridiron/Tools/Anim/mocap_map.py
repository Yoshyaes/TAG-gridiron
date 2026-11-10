"""Which mocap-pack clip feeds each TAG clip, and how it is cut.

Every key is a game clip name (A_TAG_<key>, see FAthleteAssets in Render/Athlete.h). Values (see
mocap_retarget.build):
  src           clip name in the pack manifest (one performer; __P01 / __P02 for the men of a group; a missing
                version suffix resolves to the lowest version, see mocap_retarget.resolve_clip)
  start, end    source seconds to use (whole clip when absent)
  fit           target seconds (time scale), when the real tempo has to meet a game timing contract
  mode          'inplace' (default; the sim moves and turns the actor), 'inplace_pos' (turns stay in the clip),
                'anchor' (the clip carries the travel from its first frame: falls, dives, get-ups)
  mirror        build the other side (the game's _M variants come from MIRRORED)
  loop          blend frames so the clip wraps seamlessly (the game loops *_Loop clips)
  yaw           extra turn about up, degrees, positive to the left
  lift          metres added to the pelvis height
  mirror_of     this key is the mirrored build of another key (its spec with mirror flipped)

The pack's direction codes (D1..D8) are compass bearings around the man: D1 straight ahead, D3 his right, D5 behind,
D7 his left (D2/D4/D6/D8 the diagonals). In the Tackle_Neu / D_Win / O_Win sets P01 is the ball carrier (hands
together on the ball) and P02 the tackler; in Tackle_Dive_Low_from_D5 the diver is P01. Neu tackles end about where
the hit lands, D_Win drives the carrier back, O_Win lets him fall forward over the tackler. Cut points below come from
Tools/Anim/mocap_probe.py (hip heights, hand speeds, distance between the two men).
"""

MAP = {
    # ---------------------------------------------------------------- pre-snap stances (2-4 s loops)
    "Stance3pt_Loop": dict(src="Preplay_Fidgets_OL_3pt_Back_Foot_Reset_01__P01", end=2.9, loop=6),  # hips 0.71-0.74, head level with them
    "Stance4pt_Loop": dict(src="PrePlay_Def_4pt_cycle__P01", loop=6),
    "Stance2pt_Loop": dict(src="Stance_WR_ChadJohnson__P01", fit=2.4, loop=6),  # knees bent, hands down; a 1 s hold stretched to breathe
    "StanceRB_Loop": dict(src="FATIGUED_STANCE_P_HB_looping__P01", end=3.0, loop=6),  # hands on the thigh pads, hips 0.85
    "StanceLB_Loop": dict(src="Idle_PrePlay_LB__P01", loop=6),
    "StanceDB_Loop": dict(src="Pre_Play_DB_Idle_02__P01", end=3.0, loop=6),
    "StanceK_Loop": dict(src="Kicker_FG_loop_Default__P01", loop=6),
    "QB_Shotgun_Loop": dict(src="Preplay_19_Idle_QBSG_TomBrady__P01", end=3.0, loop=6),
    "StanceUnderCenter_Loop": dict(src="Preplay_19_Idle_QBUC_TomBrady__P01", end=3.0, loop=6),
    # ---------------------------------------------------------------- quarterback
    "QB_SnapCatch": dict(src="MADNMAY04_218_2_QB_ShotgunHandsUpCatchAndSet__P01", start=0.1, end=0.6),
    "QB_UnderCenterSnap": dict(src="QB_Drop_UC_3step_Quick__P01", end=0.5),
    "QB_Drop_Shotgun": dict(src="QB_Drop_SG_5step__P01", start=0.55, end=1.45),   # the quick steps of a gun drop, ~2.5 m/s (the sim stops the clip at its depth)
    "QB_Drop_UnderCenter": dict(src="QB_Drop_UC_5step_Big__P01", start=0.3, end=1.6),  # five step drop from the second step, ~3.9 m/s
    "QB_Pocket_Loop": dict(src="MG_QB_In_Pocket_BallHigh_Idle__P01", loop=6),
    # throws: the release (right hand's peak speed) is read from the json into FAthleteAssets' Release table
    "QB_Throw": dict(src="Pass_Set_Favre_Bullet_D1_footup__P01"),                              # release ~0.6 s
    "QB_Throw_Quick": dict(src="Pass_Set_Brees_Bullet_footup_D1__P01"),                        # ~0.5 s
    "QB_Throw_Deep": dict(src="MADNMAY04_0028_1_QB_setDeepPass_D8_standard__P01", end=1.5),    # ~0.6 s
    "QB_Throw_Fade": dict(src="Backfoot_Short_D2_Dec09_0793_1__P01", end=1.3),                 # ~0.5 s
    "QB_Throw_Sidearm": dict(src="Pass_Set_Elway_Bullet_D1_footup__P01", start=0.9, end=2.5),  # ~1.5 s
    "QB_Throw_Touch": dict(src="Pass_Set_Montana_Bullet_footup_D1__P01", start=0.9, end=2.5),  # ~1.5 s
    "QB_Throw_RunR": dict(src="QB_Pass_1429_6_Rollout_HeadRight_Throw_Short_D1__P01", end=1.3),  # ~0.5 s
    "QB_Throw_RunL": dict(src="QB_Pass_1409_5_Rollout_HeadLeft_Throw_Short_D1__P01"),           # ~0.5 s
    "QB_Handoff": dict(src="MADNMAY04_2302_1_QBHandoffD5_RH_RBStanceD1__P01", end=1.2),
    "QB_Pitch": dict(src="MADNMAY04_0248_1_QB_Shotgun_Hands_Up_Catch_Quick_Pitch_R_then_Set__P01", start=0.3, end=1.4),
    "QB_Kneel": dict(src="MADNMAY04_0270_1_QB_Under_Center_Quick_Kneel__P01", end=2.5),
    # ---------------------------------------------------------------- the line
    "Lineman_GetOff": dict(src="Madnoct04_0296_1_StartPlay_Transition_OL_Stance_3point_RunBlock_D1_Alt_1__P01", end=0.6),
    "Block_PassSet_Loop": dict(src="Dline_PassBlock_HeadsUp_Base_Loop__P01", end=1.6, loop=6),
    "Block_Engaged_Loop": dict(src="Dline_PassBlock_HeadsUp_Base_Loop__P01", start=1.6, end=3.2, loop=6),
    "Block_Drive_Loop": dict(src="RunBlock_GetEng_Force_D1_to_D1_RTShldr_OffWin_RT__P01", start=1.0, end=2.4, loop=6),
    "Block_DrivenBack_Loop": dict(src="RunBlock_GetEng_Force_D1_to_D1_RTShldr_OffWin_RT__P02", start=1.0, end=2.4, loop=6),
    "Rush_BullDrive_Loop": dict(src="RunBlock_GetEng_Force_D1_to_D1_RTShldr_DefWin__P02", start=0.25, end=1.05, loop=5),
    "Block_Anchor_Loop": dict(src="DL_Move_BullRush_Loss__P01", start=0.3, end=1.3, loop=6),
    "Block_Punch": dict(src="Block_Impact_D1_Stand_Loss_Big_SEP09_0036__P01", end=0.5),
    "Block_Beaten": dict(src="Dline_PassBlock_Stationary_to_Left_Win__P01", start=0.2, end=1.3),
    "Block_Finish": dict(src="Block_Impact_D1_Stand_Loss_Big_SEP09_0036__P01", end=1.1),
    "Rush_Bull": dict(src="DefMove_BullRush__P01", end=1.2),
    "Shed_Swim": dict(src="Dline_PassBlock_Stationary_to_Left_Win__P02", start=0.2, end=1.3),
    "Shed_Rip": dict(src="Dline_PassBlock_Stationary_to_Left_Win_Aggressive__P01", start=0.3, end=1.4, mirror=True),
    "Rush_Club": dict(src="Dline_PassBlock_Stationary_to_Left_Win_Aggressive__P01", start=0.3, end=1.4),
    "Rush_LongArm": dict(src="Dline_PassBlock_Disengage_Left_from_Stationary__P01", end=1.3),
    "Rush_DipRip": dict(src="Dline_PassBlock_Stationary_to_Left_Win__P02", start=0.1, end=1.3),
    # ---------------------------------------------------------------- tackles (the tackler, in place one-shots that
    # stay upright; the paired falls below take the men to the ground)
    "Tackle_Wrap": dict(src="Tackle_D_Win_12_D1_HeadLeft__P02", end=1.0),
    "Tackle_High": dict(src="Tackle_D_Win_12_D1_HeadLeft__P02", start=0.2, end=1.2),
    "Tackle_Low": dict(src="Tackle_O_Win_12_D1_HeadLeft__P02", end=0.9),
    "Tackle_Dive": dict(src="MADNJUN04_0249_1_1_BC_Running_D1_Defender_Dives_Medium_Tackles_From_D4_PMCD__P01", end=0.8),
    "Tackle_Shoulder": dict(src="Hitstick_D1_FBJAN07_0196_4__P02", end=1.2),
    "Tackle_Arm": dict(src="Tackle_D_Win_12_D3_HeadBehind__P02", end=0.5),
    "Tackle_Assist": dict(src="NonBC_Hit_Pile_Aggressive__P01", end=0.6),
    "Tackle_Miss": dict(src="Breakdown_Whiff__P01", end=1.05),
    "Tackled_Hit": dict(src="Tackle_D_Win_12_D1_HeadLeft__P01", end=0.6),
    "Tackled_FromBehind": dict(src="Tackle_Neu_12_D5_HeadLeft__P01", start=0.3, end=0.7),
    "Tackled_FromSide": dict(src="Tackle_Neu_12_D3_HeadLeft__P01", start=0.5, end=0.85),
    "Broken_Stumble": dict(src="NonBC_Stumble_Hit_D3__P01", end=1.2),
    # ---------------------------------------------------------------- catches (in place; the sim moves the actor)
    "Catch_High": dict(src="Catch_Backpedal_45_AboveHead__P01"),
    "Catch_Chest": dict(src="C5SWTRM2_CATCH_STAND_MID__P01"),
    "Catch_Low": dict(src="Catch_Stand_90_Knees__P01"),
    "Catch_OverShoulder": dict(src="Catch_Run_135_AboveHead_OverShldr_Exit0__P01"),
    "Catch_Sideline_L": dict(src="C5MCD1M2_Extended_45_waist_sideline__P01"),
    "Catch_Sideline_R": dict(src="C5MCD1M2_Extended_45_waist_sideline__P01", mirror=True),
    "Catch_Dive": dict(src="Catch_Run_Dive_90_Knees__P01", mode="anchor", start=0.3),  # straight dive, lands at 0.4 s, back on a knee by the end,
    "Catch_InStride": dict(src="Catch_Run_180_InStride_FBOCT08_0253__P01"),
    "Catch_HighPoint": dict(src="Catch_Stand_0_JumpHigh_Reach_Interception__P01", start=0.1, end=1.4),
    "Catch_OneHand": dict(src="Catch_Run_90_Helmet_BehindOneHand_Exit0__P01", end=1.3),
    "Catch_BackShoulder": dict(src="Catch_Run_180_Helmet_BackShoulder_Exit0__P01"),
    "Catch_Comeback": dict(src="Catch_Run_0_Chest_ReachLF_Turn180__P01", start=0.3, end=1.6),
    "Catch_Scoop": dict(src="Catch_Run_0_Ankles_FBOCT08_0239__P01"),
    # ---------------------------------------------------------------- ball carrier moves (in place)
    "Juke_L": dict(src="BC_JUKE_AGILE_LEFT__P01", start=0.2, end=1.2),
    "Juke_R": dict(src="BC_JUKE_AGILE_RIGHT__P01", start=0.2, end=1.2),
    "Spin_L": dict(src="BC_Agile_Spin_Left_Sloppy__P01", start=0.3, end=1.4, mode="inplace_pos"),
    "Spin_R": dict(src="BC_Big_Spin_Right__P01", end=1.1, mode="inplace_pos"),
    "StiffArm_L": dict(src="2014_Montage_RB_StiffArm__P01", end=1.4),
    "StiffArm_R": dict(src="2014_Montage_RB_StiffArm__P01", end=1.4, mirror=True),
    "Truck": dict(src="Brkn_Tackle_Truck_D1__P01", start=0.3, end=1.5),
    "Hurdle": dict(src="BC_Hurdle_Tap_Jog_T1__P01", end=1.28),
    # ---------------------------------------------------------------- get-ups (start lying along the actor axis:
    # face down with the head forward, face up with the head behind - see AAthlete::StartGetUp)
    "GetUp_FaceDown": dict(src="Getup_NonBC_15_Splat_D1_LowIntensity__P01", end=1.6, mode="anchor"),  # standing at 1.5 s, 0.3 m from where he lay,
    "GetUp_FaceUp": dict(src="Getup_InPlace_Backsplat_FBSEP15_7633_5__P01", start=0.3, end=2.9, mode="anchor"),
    "GetUp_Roll": dict(src="Getup_InPlace_SidesplatL_FBSEP15_7638_1_2__P01", start=0.4, end=3.35, mode="anchor", yaw=90),  # lies on the side with the head to the left; turned so the head is behind, like the keyed roll,
    "GetUp_Slow": dict(src="Getup_NonBC_15_Splat_D3_LowIntensity_Dejected__P01", end=2.1, mode="anchor"),
    "Injured_Down": dict(src="15_FLD_PLYR_Injury_Front_Knee__P01", end=3.5, mode="anchor"),
    # ---------------------------------------------------------------- kicking (foot contact = right foot's peak speed,
    # read from the json into FAthleteAssets::*Contact)
    "Kick_Kickoff": dict(src="madnoct04_0816_3_13_K_kick_off__P01", start=1.2, end=3.3),   # contact ~2.6 s source
    "Kick_FieldGoal": dict(src="FG_Kick_New__P01", start=0.4, end=2.2),                     # ~1.2 s
    "Kick_Punt": dict(src="Punt_Normal_Kick__P01", start=1.0, end=3.0),                     # ~2.0 s
    # ---------------------------------------------------------------- celebrations and idles (loops)
    "Celebrate_Loop": dict(src="Celeb_18_BC_TD_THilton_TwoFistPump__P03", start=0.5, end=4.5, loop=8),
    "Celebrate_Flex_Loop": dict(src="PostPlay_BC_Celeb_TD_Flex_Muscles_1__P01", end=4.6, loop=8),
    "Celebrate_PointUp_Loop": dict(src="PostPlay_BC_TD_Celeb_Point_to_sky__P01", start=0.5, end=4.9, loop=8),
    "Celebrate_Leap_Loop": dict(src="PostPlay_NonBC_15_Celeb_DoubleFistPump__P01", loop=8),
    "Celebrate_Shimmy_Loop": dict(src="2014_PC_Generic_SwagDance__P01", start=0.5, end=5.0, loop=8),
    "Celebrate_FirstDown_Loop": dict(src="PostPlay_NonBC_15_FirstDown_Celeb_Signal_v2", loop=8),
    "Idle_HandsHips_Loop": dict(src="Idle_NonBC_PostPlay_15_Neutral_var32_NoTrajMove__P01", end=4.0, loop=8),
    "Idle_HandsKnees_Loop": dict(src="Pre_Play_LB_Idle_Hands_on_Knee__P01", loop=8),
    "Def_Breakdown_Loop": dict(src="NonBC_DefenderBreakdown_Idle__P01", loop=4),
    # ---------------------------------------------------------------- paired tackles to the ground (anchor mode from
    # the hit; both men of the group in one shared space, the director places them from Tools/Anim/mocap_pairs.py)
    "PT_Drive_T": dict(src="Tackle_D_Win_12_D1_HeadLeft__P02", start=0.1, end=1.9, mode="anchor"),
    "PT_Drive_C": dict(src="Tackle_D_Win_12_D1_HeadLeft__P01", start=0.1, end=1.9, mode="anchor"),
    "PT_Over_T": dict(src="Tackle_O_Win_12_D1_HeadLeft__P02", start=0.5, end=1.8, mode="anchor"),
    "PT_Over_C": dict(src="Tackle_O_Win_12_D1_HeadLeft__P01", start=0.5, end=1.8, mode="anchor"),
    "PT_Behind_T": dict(src="Tackle_Neu_12_D5_HeadLeft__P02", start=0.4, end=1.7, mode="anchor"),
    "PT_Behind_C": dict(src="Tackle_Neu_12_D5_HeadLeft__P01", start=0.4, end=1.7, mode="anchor"),
    "PT_Side_T": dict(src="Tackle_Neu_12_D3_HeadLeft__P02", start=0.6, end=1.9, mode="anchor"),
    "PT_Side_C": dict(src="Tackle_Neu_12_D3_HeadLeft__P01", start=0.6, end=1.9, mode="anchor"),
    "PT_Dive_T": dict(src="Tackle_Dive_Low_from_D5__P01", start=0.2, end=1.1, mode="anchor"),
    "PT_Dive_C": dict(src="Tackle_Dive_Low_from_D5__P02", start=0.2, end=1.1, mode="anchor"),
    "PT_PileOn": dict(src="Madnnov04_0328_3_Late_hit_pile_on_Defender_sprint_D1_dives_over_pile_and_rolls__P01", start=0.3, end=1.5, mode="anchor"),
}

# the game's mirrored variants (FAthleteAssets::MirrorOf): <name>_M is <name> built with mirror flipped
MIRRORED = ["Tackle_Wrap", "Tackle_High", "Tackle_Low", "Tackle_Dive", "Tackle_Shoulder", "Tackle_Arm", "Tackle_Assist",
            "Tackled_Hit", "Catch_High", "Catch_Chest", "Catch_Low", "Catch_OverShoulder", "Catch_Dive", "GetUp_FaceDown",
            "GetUp_FaceUp", "GetUp_Roll", "GetUp_Slow", "Injured_Down", "Truck", "Hurdle", "QB_Pitch", "QB_Handoff",
            "Celebrate_Loop", "Celebrate_Flex_Loop", "Celebrate_PointUp_Loop", "Celebrate_Leap_Loop",
            "Celebrate_FirstDown_Loop", "Celebrate_Shimmy_Loop", "Catch_OneHand", "Catch_BackShoulder", "Catch_Comeback",
            "Tackled_FromSide", "Tackle_Miss", "Broken_Stumble", "Rush_LongArm", "Rush_DipRip", "Block_Beaten", "Shed_Swim",
            "Shed_Rip", "Rush_Club", "PT_Side_T", "PT_Side_C"]
for _n in MIRRORED:
    MAP[_n + "_M"] = dict(mirror_of=_n)

# clips with no mocap counterpart in the pack stay hand keyed (Tools/Anim/clips*.py): the pass-rush spins and the
# spectators' cheer
KEEP_KEYED = ["Rush_Spin_L", "Rush_Spin_R", "Fan_Cheer_Loop"]


def resolve(name):
    spec = MAP[name]
    if "mirror_of" in spec:
        base = dict(MAP[spec["mirror_of"]])
        base["mirror"] = not base.get("mirror", False)
        return base
    return dict(spec)


def all_names():
    return [n for n in MAP]
