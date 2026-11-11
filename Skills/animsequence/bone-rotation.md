---
name: animsequence-bone-rotation
description: Bone Rotation Axis Discovery - why bone local coordinate systems are non-intuitive, and the required discovery workflow (reference pose values, analyze existing animations, incremental axis testing) before writing rotation animation code.
---

# Bone Rotation Axis Discovery

> **⚠️ CRITICAL WARNING**: Bone local coordinate systems in UE5 are **non-intuitive**!
> 
> - Roll, Pitch, and Yaw meanings depend on how each bone is oriented in the skeleton
> - What looks like "arm up" might be Roll on one skeleton and Pitch on another
> - Different skeletons (Mannequin, MetaHuman, custom) have different bone orientations
> - **NEVER assume** axis mappings - always discover them first!

## Why This Matters

Common mistake: Assuming Pitch rotates an arm up/down (like airplane pitch). In reality:
- Bones have local coordinate systems based on their orientation in the skeleton
- A bone pointing along X-axis will rotate differently than one pointing along Y-axis
- The only reliable way to know is to **test and observe**

## Discovery Workflow (Required Before Rotation Animations)

**Step 1: Get reference pose values** - Find the "rest" rotation for target bones:

```python
import unreal

skeleton_path = "/Game/Path/To/Your/Skeleton"
target_bones = ["upperarm_r", "lowerarm_r", "hand_r"]  # Bones you want to animate
pose = unreal.AnimPoseExtensions.get_reference_pose(unreal.load_asset(skeleton_path))
for bone_name in target_bones:
    if bone_name not in map(str, unreal.AnimPoseExtensions.get_bone_names(pose)):
        raise ValueError(f"Bone not found in reference pose: {bone_name}")
    euler = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, bone_name).rotation.rotator()
    print(f"{bone_name}: Roll={euler.roll:.1f}, Pitch={euler.pitch:.1f}, Yaw={euler.yaw:.1f}")
```

**Step 2: Analyze existing animations** - See what axes change during desired movement:

```python
# Find an animation that does what you want (e.g., arm raise, wave)
options = unreal.AnimPoseEvaluationOptions()
idle_pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(unreal.load_asset("/Game/Anims/Idle"), 0.0, options)
action_pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(unreal.load_asset("/Game/Anims/ArmRaise"), 0.5, options)

# Compare local-space euler angles for target bones
for bone_name in target_bones:
    if bone_name in map(str, unreal.AnimPoseExtensions.get_bone_names(idle_pose)) and bone_name in map(str, unreal.AnimPoseExtensions.get_bone_names(action_pose)):
        idle_e = unreal.AnimPoseExtensions.get_bone_pose(idle_pose, bone_name).rotation.rotator()
        action_e = unreal.AnimPoseExtensions.get_bone_pose(action_pose, bone_name).rotation.rotator()
        print(f"{bone_name}:")
        print(f"  Roll:  {idle_e.roll:.1f} -> {action_e.roll:.1f} (delta: {action_e.roll - idle_e.roll:.1f})")
        print(f"  Pitch: {idle_e.pitch:.1f} -> {action_e.pitch:.1f} (delta: {action_e.pitch - idle_e.pitch:.1f})")
        print(f"  Yaw:   {idle_e.yaw:.1f} -> {action_e.yaw:.1f} (delta: {action_e.yaw - idle_e.yaw:.1f})")
```

**Step 3: Test incrementally** - Change ONE axis at a time to confirm its effect:

```python
# Create test animation varying only one axis
# Example: Test if Roll controls arm elevation
ref_roll, ref_pitch, ref_yaw = 2.7, -37.8, 0.2  # From reference pose

test_keyframes = [
    (0.0, ref_roll, ref_pitch, ref_yaw),        # Start at reference
    (0.5, ref_roll - 45, ref_pitch, ref_yaw),   # Change Roll only
    (1.0, ref_roll - 90, ref_pitch, ref_yaw),   # Change Roll more
]
# Create animation, preview, observe which direction the bone moves
```

## Key Insight

The axis that controls a particular movement varies by:
- **Skeleton source** (Mannequin vs MetaHuman vs custom)
- **Bone orientation** in the skeleton hierarchy
- **Local vs component space** transforms

**Always run the discovery workflow** before writing rotation animation code. Document your findings for each skeleton you work with.
