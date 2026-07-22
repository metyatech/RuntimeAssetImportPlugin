import unreal


SMOKE_MAP = "/Game/Smoke/SmokeMap"


def create_smoke_map() -> None:
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_editor is None:
        raise RuntimeError("LevelEditorSubsystem is unavailable")
    if not level_editor.new_level(SMOKE_MAP):
        raise RuntimeError(f"Failed to create smoke map: {SMOKE_MAP}")
    if not level_editor.save_current_level():
        raise RuntimeError(f"Failed to save smoke map: {SMOKE_MAP}")
    if not unreal.EditorAssetLibrary.does_asset_exist(SMOKE_MAP):
        raise RuntimeError(f"Smoke map asset does not exist after save: {SMOKE_MAP}")
    unreal.log(f"Created smoke map: {SMOKE_MAP}")


create_smoke_map()
