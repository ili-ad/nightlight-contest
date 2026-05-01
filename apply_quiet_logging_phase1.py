from pathlib import Path
import sys

ROOT = Path.cwd()
APP = ROOT / "src" / "App.cpp"
SCENE = ROOT / "src" / "scenes" / "AnthuriumScene.cpp"


def read(path: Path) -> str:
    if not path.exists():
        raise FileNotFoundError(f"Missing expected file: {path}")
    return path.read_text(encoding="utf-8")


def write_if_changed(path: Path, text: str) -> bool:
    old = read(path)
    if old == text:
        return False
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def ensure_after(text: str, anchor: str, insertion: str) -> str:
    if insertion.strip() in text:
        return text
    idx = text.find(anchor)
    if idx == -1:
        raise RuntimeError(f"Could not find anchor:\n{anchor}")
    idx += len(anchor)
    return text[:idx] + insertion + text[idx:]


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count == 0:
        # Already patched or source drift. Do not silently corrupt.
        if new.strip() in text:
            return text
        raise RuntimeError(f"Could not find exact block to replace:\n{old[:300]}...")
    if count > 1:
        raise RuntimeError(f"Block matched {count} times; refusing ambiguous replacement:\n{old[:300]}...")
    return text.replace(old, new, 1)


def patch_app() -> bool:
    text = read(APP)

    text = ensure_after(
        text,
        '#include "topology/LayoutMap.h"\n',
        '\n// Production default: quiet serial telemetry. Set to 1 for bench debugging.\n'
        '#ifndef NIGHTLIGHT_ENABLE_TELEMETRY\n'
        '#define NIGHTLIGHT_ENABLE_TELEMETRY 0\n'
        '#endif\n'
    )

    start_old = (
        'void App::maybePrintAnthuriumTelemetry(const StableTrack& track, uint32_t nowMs) {\n'
        '  constexpr uint32_t kTelemetryIntervalMs = 500;\n'
    )
    start_new = (
        'void App::maybePrintAnthuriumTelemetry(const StableTrack& track, uint32_t nowMs) {\n'
        '#if !NIGHTLIGHT_ENABLE_TELEMETRY\n'
        '  (void)track;\n'
        '  (void)nowMs;\n'
        '  return;\n'
        '#else\n'
        '  constexpr uint32_t kTelemetryIntervalMs = 500;\n'
    )
    if '#if !NIGHTLIGHT_ENABLE_TELEMETRY' not in text:
        text = replace_once(text, start_old, start_new)
        end_old = '  Serial.print(" phase=");\n  Serial.println(phaseName(track.phase));\n}\n'
        end_new = '  Serial.print(" phase=");\n  Serial.println(phaseName(track.phase));\n#endif\n}\n'
        text = replace_once(text, end_old, end_new)

    return write_if_changed(APP, text)


def patch_scene() -> bool:
    text = read(SCENE)

    text = ensure_after(
        text,
        '#include <math.h>\n',
        '\n// Production default: keep the Anthurium scene quiet.\n'
        '// Set these to 1 while bench-testing the native/projection comparison.\n'
        '#ifndef ANTHURIUM_ENABLE_SHADOW44_SUMMARY\n'
        '#define ANTHURIUM_ENABLE_SHADOW44_SUMMARY 0\n'
        '#endif\n'
        '#ifndef ANTHURIUM_ENABLE_SHADOW44_DUMPS\n'
        '#define ANTHURIUM_ENABLE_SHADOW44_DUMPS 0\n'
        '#endif\n'
    )

    summary_start_old = (
        'void AnthuriumScene::maybeLogProjectionComparison(const ColorF* projected, const ColorF* native, uint32_t nowMs) {\n'
        '  constexpr uint32_t kCompareLogMs = 1000;\n'
    )
    summary_start_new = (
        'void AnthuriumScene::maybeLogProjectionComparison(const ColorF* projected, const ColorF* native, uint32_t nowMs) {\n'
        '#if !ANTHURIUM_ENABLE_SHADOW44_SUMMARY\n'
        '  (void)projected;\n'
        '  (void)native;\n'
        '  (void)nowMs;\n'
        '  return;\n'
        '#else\n'
        '  constexpr uint32_t kCompareLogMs = 1000;\n'
    )
    if '#if !ANTHURIUM_ENABLE_SHADOW44_SUMMARY' not in text:
        text = replace_once(text, summary_start_old, summary_start_new)
        summary_end_old = '  Serial.print(" dMax="); Serial.println(diffMax, 3);\n}\n\nvoid AnthuriumScene::maybeDumpProjectionArrays'
        summary_end_new = '  Serial.print(" dMax="); Serial.println(diffMax, 3);\n#endif\n}\n\nvoid AnthuriumScene::maybeDumpProjectionArrays'
        text = replace_once(text, summary_end_old, summary_end_new)

    dump_start_old = (
        'void AnthuriumScene::maybeDumpProjectionArrays(const ColorF* projected, const ColorF* native, uint32_t nowMs) {\n'
        '  constexpr uint32_t kDumpMs = 5000;\n'
    )
    dump_start_new = (
        'void AnthuriumScene::maybeDumpProjectionArrays(const ColorF* projected, const ColorF* native, uint32_t nowMs) {\n'
        '#if !ANTHURIUM_ENABLE_SHADOW44_DUMPS\n'
        '  (void)projected;\n'
        '  (void)native;\n'
        '  (void)nowMs;\n'
        '  return;\n'
        '#else\n'
        '  constexpr uint32_t kDumpMs = 5000;\n'
    )
    if '#if !ANTHURIUM_ENABLE_SHADOW44_DUMPS' not in text:
        text = replace_once(text, dump_start_old, dump_start_new)
        dump_end_old = '  Serial.println();\n}\n\nfloat AnthuriumScene::sampleTorusField'
        dump_end_new = '  Serial.println();\n#endif\n}\n\nfloat AnthuriumScene::sampleTorusField'
        text = replace_once(text, dump_end_old, dump_end_new)

    return write_if_changed(SCENE, text)


def main() -> int:
    try:
        changed = []
        if patch_app():
            changed.append(str(APP))
        if patch_scene():
            changed.append(str(SCENE))
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if changed:
        print("Applied quiet logging phase 1 edits:")
        for path in changed:
            print(f"  - {path}")
    else:
        print("No changes needed; quiet logging phase 1 edits already present.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
