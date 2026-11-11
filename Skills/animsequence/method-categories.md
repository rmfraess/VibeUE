---
name: animsequence-method-categories
description: Categorised table of every AnimSequenceService method by domain (Discovery, Creation, Helpers, Properties, Bone Tracks, Poses, Curves, Notifies, Notify Tracks, Sync Markers, Root Motion, Additive, Compression, Export, Editor).
---

# AnimSequenceService Method Categories

## Method Categories

| Category | Methods |
|----------|---------|
| Discovery | `list_anim_sequences`, `get_anim_sequence_info` |
| **Creation** | `create_from_pose`, `create_anim_sequence` |
| Properties | `get_animation_frame_rate` |
| Bone Tracks | `get_animated_bones` |
| Poses | `get_root_motion_at_time`, `get_total_root_motion` |
| Curves | `list_curves`, `get_curve_info`, `get_curve_keyframes` |
| Notifies | `list_notifies`, `get_notify_info`, `add_notify`, `remove_notify`, `set_notify_trigger_time`, `set_notify_duration`, `set_notify_track`, `set_notify_name`, `set_notify_color`, `set_notify_trigger_chance`, `set_notify_trigger_on_server`, `set_notify_trigger_on_follower`, `set_notify_trigger_weight_threshold`, `set_notify_lod_filter` |
| Notify Tracks | `remove_notify_track` |
| Sync Markers | `remove_sync_marker`, `set_sync_marker_time` |
| Compression | `get_compression_info`, `compress_animation` |
| Export | `export_animation_to_json` |

Native replacements: frame count: `unreal.AnimationLibrary.get_num_frames(anim)` (keys = frames + 1); frame rate: `anim.get_editor_property('controller').set_frame_rate(unreal.FrameRate(fps, 1))`; curve keys: build a float identifier with `unreal.AnimationCurveIdentifierExtensions.get_curve_identifier(skeleton, name, unreal.RawCurveTrackTypes.RCT_FLOAT)` and pass it and a list of `unreal.RichCurveKey` to the controller's curve-key setter; source files: `anim.get_editor_property('asset_import_data').extract_filenames()`.

`AnimPoseExtensions` for poses, `AnimationLibrary` for curves/properties/root motion/additive settings, `AssetToolset.FindAssets` for discovery, and `EditorAppToolset.OpenEditorForAsset` for opening.

All methods are static. Most use time in seconds (not frames).
