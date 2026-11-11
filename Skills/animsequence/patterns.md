---
name: animsequence-patterns
description: Common patterns for animation work (listing animations for a skeleton, getting bone pose at time, adding curves, managing and configuring notifies) plus the CRITICAL RULES checklist for creation, notify class paths, asset path formats, time/frame units, and bone axes.
---

# Animation Sequence Patterns

## Common Patterns

### Listing Animations for a Skeleton

```python
import unreal

# Find all animations for a specific skeleton without loading them
skeleton_path = "/Game/Characters/Mannequin/Mesh/SK_Mannequin_Skeleton"
ar = unreal.AssetRegistryHelpers.get_asset_registry()
anims = [a for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'AnimSequence'))
         if skeleton_path in str(a.get_tag_value('Skeleton'))]
for anim in anims:
    print(anim.asset_name, anim.package_name)
```

### Getting Bone Pose at Time

```python
import unreal

# Get full skeleton pose at 0.5 seconds in world space
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
    unreal.load_asset("/Game/Animations/Run"), 0.5, unreal.AnimPoseEvaluationOptions()
)
for bone_name in unreal.AnimPoseExtensions.get_bone_names(pose):
    transform = unreal.AnimPoseExtensions.get_bone_pose(pose, bone_name, unreal.AnimPoseSpaces.WORLD)
    print(f"{bone_name}:")
    print(f"  Location: {transform.translation}")
    print(f"  Rotation: {transform.rotation}")
```

### Adding Animation Curves

```python
import unreal

anim = unreal.load_asset("/Game/Animations/Facial")
lib = unreal.AnimationLibrary
# Add a float curve and mark its skeleton metadata as a morph target
lib.add_curve(anim, "jaw_open")
skeleton = anim.get_editor_property("skeleton")
lib.add_curve_meta_data(skeleton, "jaw_open")
lib.set_curve_meta_data_morph_target(skeleton, "jaw_open", True)
# Add keys to the curve
lib.add_float_curve_key(anim, "jaw_open", 0.0, 0.0)
lib.add_float_curve_key(anim, "jaw_open", 0.5, 1.0)
```

### Managing Animation Notifies

**IMPORTANT**: Always use the full `anim_path` from animation discovery results!

**Display Name Behavior:**
- **Instant notifies with custom names**: Use base `/Script/Engine.AnimNotify` class - creates "skeleton notifies" that display your custom name in the editor timeline
- **Instant notifies without names**: Creates skeleton notify (base AnimNotify is abstract in UE5.7+), editor displays "Notify"
- **State notifies**: MUST use a concrete subclass (NOT the abstract base class). Custom names are stored in `NotifyName` but the editor displays the class name

**⚠️ COMMON MISTAKES:**
- ❌ Using `/Script/Engine.AnimNotifyState` - **FAILS** (abstract class, cannot instantiate)
- ❌ Using custom notify class + custom name - editor shows class name, not your custom name
- ✅ Use `/Script/Engine.AnimNotify` with a custom name for named instant notifies (creates skeleton notify)
- ✅ Use concrete subclasses like `/Script/Engine.AnimNotify_PlaySound` for functional notifies
- ✅ Use concrete subclasses like `/Script/Engine.AnimNotifyState_Trail` for state notifies

> ### 🛑 Notify indices are NOT stable — never cache them across mutations
>
> Notifies are stored **sorted by `trigger_time`**. The index returned by `add_notify`,
> and any index you read from `list_notifies`, is only valid until the next mutation. The moment you add
> another notify or change a `trigger_time`, **every index can shift**, so a cached `idx` now points at a
> *different* notify (or none).
>
> - ❌ `idx = add_notify(...)` then later `set_notify_trigger_time(path, idx, ...)` / `remove_notify(path, idx)` — `idx` may be stale.
> - ✅ Re-fetch `list_notifies(path)` and **look the notify up by name** (or by name+time) immediately before each operation.
>
> This single gotcha is the most common source of "I edited the wrong notify" bugs. Helper below.

```python
import unreal

def find_notify_index(anim_path, name):
    """Resolve a notify's CURRENT index by name. Call this right before every
    index-based operation — indices shift whenever notifies are added or retimed."""
    for n in unreal.AnimSequenceService.list_notifies(anim_path):
        if n.notify_name == name:
            return n.notify_index
    return -1

# Step 1: Find an animation and get the FULL asset path
results = unreal.AnimSequenceService.list_anim_sequences("/Game/Animations", "")
if not results:
    print("No animations found")
else:
    # CRITICAL: anim_path is the full asset path, not the containing folder
    anim_path = results[0].anim_path
    print(f"Using animation: {anim_path}")
    
    # List existing notifies
    notifies = unreal.AnimSequenceService.list_notifies(anim_path)
    print(f"Found {len(notifies)} existing notifies:")
    for n in notifies:
        print(f"  [{n.notify_index}] {n.notify_name} @ {n.trigger_time:.3f}s (state={n.is_state})")
    
    # Add an instant notify with custom name (displays "LeftFoot" in editor)
    idx = unreal.AnimSequenceService.add_notify(
        anim_path,
        "/Script/Engine.AnimNotify",  # Base class + custom name = skeleton notify
        0.25,                          # Trigger time in seconds
        "LeftFoot"                     # Custom name - displayed in editor
    )
    if idx >= 0:
        print(f"Created instant notify (returned index {idx} — may shift after edits)")
    
    # State notifies: use native AnimationLibrary.add_animation_notify_state_event
    # with a concrete AnimNotifyState subclass.
    
    # ⚠️ Re-resolve the index by name before EACH operation — it may have moved.
    # Modify notify timing (this re-sorts notifies, invalidating other indices)
    unreal.AnimSequenceService.set_notify_trigger_time(anim_path, find_notify_index(anim_path, "LeftFoot"), 0.5)
    
    # Modify notify track (visual row in editor)
    unreal.AnimSequenceService.set_notify_track(anim_path, find_notify_index(anim_path, "LeftFoot"), 1)
    
    # Rename a notify (for base AnimNotify, this also converts to skeleton notify for proper display)
    unreal.AnimSequenceService.set_notify_name(anim_path, find_notify_index(anim_path, "LeftFoot"), "RightFoot")
    
    # Get notify info (look up by the NEW name now)
    info = unreal.AnimSequenceService.get_notify_info(anim_path, find_notify_index(anim_path, "RightFoot"))
    if info:
        print(f"Notify: {info.notify_name} @ {info.trigger_time}s, track {info.track_index}")
    
    # Remove a notify
    unreal.AnimSequenceService.remove_notify(anim_path, find_notify_index(anim_path, "RightFoot"))
```

### Configuring Notify Behavior

```python
import unreal

anim_path = "/Game/Animations/MyAnim"

# Rename a notify (changes display name in editor)
# For base AnimNotify class, this also converts to skeleton notify for proper display
unreal.AnimSequenceService.set_notify_name(anim_path, 0, "Footstep_Left")

# Set editor display color using LinearColor object
orange = unreal.LinearColor(1.0, 0.5, 0.0, 1.0)
unreal.AnimSequenceService.set_notify_color(anim_path, 0, orange)

# Set trigger chance (0.0-1.0, for random triggering)
unreal.AnimSequenceService.set_notify_trigger_chance(anim_path, 0, 0.75)  # 75% chance

# Control triggering on dedicated server (default True)
unreal.AnimSequenceService.set_notify_trigger_on_server(anim_path, 0, False)  # Disable on server

# Control triggering on sync group followers (default True)
unreal.AnimSequenceService.set_notify_trigger_on_follower(anim_path, 0, False)

# Set minimum blend weight to trigger (0.0-1.0)
unreal.AnimSequenceService.set_notify_trigger_weight_threshold(anim_path, 0, 0.5)  # Only trigger at 50%+ weight

# Set LOD filtering ("NoFiltering" or "LOD" with level 0-3)
unreal.AnimSequenceService.set_notify_lod_filter(anim_path, 0, "LOD", 2)  # Only LOD 0-2

# Read all notify properties via get_notify_info
info = unreal.AnimSequenceService.get_notify_info(anim_path, 0)
if info:
    print(f"Name: {info.notify_name}")
    print(f"Trigger Chance: {info.trigger_chance}")
    print(f"Trigger On Server: {info.trigger_on_server}")
    print(f"Trigger On Follower: {info.trigger_on_follower}")
    print(f"Weight Threshold: {info.trigger_weight_threshold}")
    print(f"Filter Type: {info.notify_filter_type}")
    print(f"Filter LOD: {info.notify_filter_lod}")
```

## CRITICAL RULES

1. **Full Asset Path Required**: Use `package_name` from AssetData (e.g., `/Game/Folder/AssetName`), NOT `package_path` (which is just the folder)
2. **Skeleton Required**: All creation methods require a valid skeleton path
3. **Asset Path Format**: Use `/Game/` format, not file paths
4. **Time Units**: Most methods use seconds, not frames
5. **Frame Rate**: Default is 30 FPS if not specified
6. **Bone Names**: Must match exact bone names from skeleton
7. **Save Before Use**: Newly created animations are automatically saved
8. **Keyframe Times**: Must be within [0, duration] range
9. **Reference Pose**: `create_from_pose` uses skeleton's reference pose
10. **Notify Class Paths**: `add_notify` requires a FULL class path; native state-notify creation takes a concrete class:
   - Instant notify: `/Script/Engine.AnimNotify`
   - State notify: a concrete subclass such as `/Script/Engine.AnimNotifyState_Trail` (the base
     `/Script/Engine.AnimNotifyState` is abstract and is rejected)
   - Sound notify: `/Script/Engine.AnimNotify_PlaySound`
   - Custom: `/Script/YourModule.YourNotifyClass`
11. **Bone Rotation Axes Are Non-Intuitive**: Roll/Pitch/Yaw do NOT map predictably to world-space movement. Each bone's local coordinate system is different. **Always discover axis mappings** using native `AnimPoseExtensions.get_reference_pose` and `get_anim_pose_at_time` before creating rotation animations!
12. **Notify indices are unstable**: Notifies are sorted by `trigger_time`; any add/retime shifts indices. Re-fetch `list_notifies` and resolve by name before each index-based call. (See the notify gotcha box above.)
13. **`list_anim_sequences` does NOT report root motion**: it returns `root_motion=False` for every entry. For accurate root-motion status (and `raw_size`/`compressed_size`/`bone_track_count`), use `get_anim_sequence_info`, whose results expose `enable_root_motion`.
14. **Search-result types differ**: the Asset Registry query returns `AssetData` (`package_name` and `asset_name`); `list_anim_sequences` returns `AnimSequenceInfo` (`anim_path` and `anim_name`), and `AssetToolset.FindAssets` returns object-path strings. Filter out `None` service results before reading fields.
15. **Scope searches to a folder**: a project-wide wildcard (e.g. `*Run*` over all of `/Game`) can exceed the 30 s Python timeout. Pass a `search_path` folder to keep `list_anim_sequences` fast.
16. **Verify frame rate after creation**: `create_anim_sequence(frame_rate=...)` has been observed to come back at 30 FPS for a 60 FPS request — always read back `get_animation_frame_rate(anim_path)` and don't assume a non-30 rate stuck.
17. **Montages use `AnimMontageService`**: `AnimSequenceService` accepts `AnimSequence` assets only.
    Passing an `AnimMontage` returns the failure sentinel; call the corresponding
    `unreal.AnimMontageService` method where retained, or the corresponding native animation API.
