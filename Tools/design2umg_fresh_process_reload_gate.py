"""Fresh-process reload gate for saved WidgetBlueprint assets.

ValidationProtocol.md §7.1 requires a fresh UnrealEditor process reload before a
saved asset counts as structural evidence. This tool turns that requirement into
a fail-closed gate:

1. Write one AutomationRender asset spec per target asset (force_reload_package).
2. Drive a single fresh UnrealEditor process through the shared editor driver.
3. Wait for the real process exit, then audit the fresh log for the two known
   save/reload corruption signatures:
   - CreateExport outer-load failures (EN + zh-CN localized forms)
   - WidgetBlueprintCompiler variable-GUID ensures
4. Require every spec to report success and a widget-tree node count at or above
   the requested floor, with result files newer than the gate started.

Exit code 0 means the gate passed. Anything else fails closed.
"""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

from design2umg_verify_lib.editor_driver_writer import write_editor_driver_script
from design2umg_verify_lib.json_io import load_json
from design2umg_verify_lib.structure_comparison import compare_structure_snapshots, structure_comparison_counts_match
from design2umg_auto_verify import compare_image_paths


OUTER_LOAD_FAILURE_MARKERS = (
    "CreateExport: Failed to load Outer",
    "外部容器加载失败",
)
GUID_ENSURE_MARKERS = (
    "was deleted but still has a GUID",
    "was added but did not get a GUID",
)


def normalize_object_path(raw: str) -> str:
    normalized = raw.strip().replace("\\", "/")
    while normalized.endswith("/"):
        normalized = normalized[:-1]
    if "." not in normalized.rsplit("/", 1)[-1]:
        raise SystemExit(f"Asset path must include the object name after a dot: {raw}")
    return normalized


def build_asset_spec(object_path: str, render_png_path: Path, result_json_path: Path) -> dict:
    return {
        "mode": "asset",
        "source_path": object_path,
        "node_id": "",
        "output_path": f"/Game/Design2UMG/Automation/FreshReloadGate/{render_png_path.parent.name}",
        "render_png_path": str(render_png_path),
        "result_json_path": str(result_json_path),
        "force_reload_package": True,
    }


def audit_log_for_corruption_lines(log_text: str) -> list[str]:
    hits = []
    for line in log_text.splitlines():
        if "LoadErrors:" not in line and any(marker in line for marker in GUID_ENSURE_MARKERS):
            hits.append(line.strip())
            continue
        if "LoadErrors:" in line and any(marker in line for marker in OUTER_LOAD_FAILURE_MARKERS):
            hits.append(line.strip())
    return hits


def audit_log_for_corruption(log_path: Path) -> list[str]:
    try:
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return [f"gate log unreadable: {log_path} ({exc})"]
    return audit_log_for_corruption_lines(log_text)


def read_result_summary(result_path: Path, started_at: float) -> tuple[dict | None, list[str]]:
    errors: list[str] = []
    if not result_path.is_file():
        return None, [f"result json missing: {result_path}"]
    if result_path.stat().st_mtime < started_at:
        errors.append(
            f"result json is stale (mtime {result_path.stat().st_mtime:.0f} < gate start {started_at:.0f}): {result_path}"
        )
    try:
        payload = json.loads(result_path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, ValueError) as exc:
        return None, errors + [f"result json unreadable: {result_path} ({exc})"]
    if not isinstance(payload, dict):
        return None, errors + [f"result json is not an object: {result_path}"]
    return payload, errors


def parse_asset_floors(raw: str) -> dict[int, int]:
    floors: dict[int, int] = {}
    for part in raw.replace(";", ",").split(","):
        token = part.strip()
        if not token:
            continue
        index, separator, count_text = token.partition("=")
        try:
            floor = int(count_text)
        except ValueError as exc:
            raise SystemExit(f"Invalid --asset-floors entry {token!r}: count must be an integer") from exc
        if not separator or floor < 1:
            raise SystemExit(f"Invalid --asset-floors entry {token!r}: expected 'INDEX=COUNT' with COUNT >= 1")
        try:
            asset_index = int(index)
        except ValueError as exc:
            raise SystemExit(f"Invalid --asset-floors entry {token!r}: index must be an integer") from exc
        if asset_index < 0 or asset_index in floors:
            raise SystemExit(f"Invalid --asset-floors index {asset_index}: indices must be non-negative and unique")
        floors[asset_index] = floor
    return floors


def load_suite_assets(run_root: Path) -> dict[str, dict]:
    """Use the recorded discovered set, not directory count or a built-in corpus."""
    run_root = run_root.resolve()
    scope = load_json(run_root / "run_scope.json")
    names = scope.get("case_names") if isinstance(scope, dict) else None
    if (not isinstance(names, list) or not names or any(not isinstance(name, str) or not name for name in names)
            or len(names) != len(set(names))):
        raise RuntimeError("Bulk scope must contain a nonempty, unique case set")
    assets = {}
    for name in names:
        case_dir = (run_root / name).resolve()
        if case_dir.parent != run_root:
            raise RuntimeError(f"Invalid case directory in suite scope: {name}")
        result, errors = read_result_summary(case_dir / "ue_render_result.json", 0)
        if errors or result.get("success") is not True:
            raise RuntimeError(f"Bulk asset result unavailable for {name}: {errors}")
        asset = result.get("generated_asset_path")
        structure = result.get("asset_structure")
        if (not isinstance(asset, str) or not asset or not isinstance(structure, dict)
                or type(structure.get("total_nodes")) is not int or structure["total_nodes"] < 1):
            raise RuntimeError(f"Invalid or duplicate suite asset for {name}")
        result["render_png_path"] = str(case_dir / "candidate.png")
        if not Path(result["render_png_path"]).is_file():
            raise RuntimeError(f"Bulk candidate PNG missing for {name}")
        asset = normalize_object_path(asset)
        if asset in assets:
            raise RuntimeError(f"Duplicate suite asset: {asset}")
        assets[asset] = result
    return assets


def evaluate_results(
    result_paths: dict[Path, str],
    min_total_nodes: int,
    started_at: float,
    asset_floors: dict[int, int] | None = None,
    baselines: dict[str, dict] | None = None,
) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    summaries: list[str] = []
    for asset_index, (result_path, expected_asset) in enumerate(result_paths.items()):
        payload, errors = read_result_summary(result_path, started_at)
        if errors:
            failures.extend(errors)
            continue
        assert payload is not None
        label = expected_asset
        if payload.get("success") is not True:
            failures.append(f"{label}: AutomationRender reported failure: {payload.get('error_message') or '<no error message>'}")
            continue
        if payload.get("generated_asset_path") != expected_asset:
            failures.append(f"{label}: result belongs to a different asset: {payload.get('generated_asset_path')!r}")
            continue
        if payload.get("force_reload_package_requested") is not True:
            failures.append(f"{label}: result does not confirm the requested package reload")
            continue
        structure = payload.get("asset_structure")
        total_nodes = structure.get("total_nodes") if isinstance(structure, dict) else None
        if type(total_nodes) is not int:
            failures.append(f"{label}: result json carries no asset_structure.total_nodes")
            continue
        floor = (asset_floors or {}).get(asset_index, min_total_nodes)
        if total_nodes < floor:
            failures.append(
                f"{label}: widget tree collapsed below the guarded floor ({total_nodes} < {floor})"
            )
            continue
        if baselines is not None:
            baseline = baselines[expected_asset]
            comparison = compare_structure_snapshots(baseline["asset_structure"], structure)
            if not structure_comparison_counts_match(comparison):
                failures.append(f"{label}: reloaded structure or text differs from the suite asset")
                continue
            png_path = result_path.with_suffix(".png")
            if not png_path.is_file() or png_path.stat().st_mtime < started_at:
                failures.append(f"{label}: fresh reload PNG missing or stale")
                continue
            try:
                pixels = compare_image_paths(Path(baseline["render_png_path"]), png_path, 0)
            except OSError as exc:
                failures.append(f"{label}: reload PNG unreadable: {exc}")
                continue
            if not pixels or not pixels["size_match"] or pixels["changed_ratio"] != 0:
                failures.append(f"{label}: reloaded pixels differ from the suite candidate")
                continue
        summaries.append(f"{label}: total_nodes={total_nodes}")
    return failures, summaries


def run_gate(args: argparse.Namespace) -> int:
    editor_path = Path(args.editor)
    project_path = Path(args.project)
    if not editor_path.is_file():
        print(f"[FAIL] Editor not found: {editor_path}")
        return 2
    if not project_path.is_file():
        print(f"[FAIL] Project not found: {project_path}")
        return 2

    try:
        asset_floors = parse_asset_floors(args.asset_floors)
    except SystemExit as exc:
        print(f"[FAIL] {exc}")
        return 2

    try:
        baselines = load_suite_assets(Path(args.run_root)) if args.run_root else None
        object_paths = list(baselines) if baselines is not None else [
            normalize_object_path(raw) for raw in args.assets.split(",") if raw.strip()]
    except (SystemExit, RuntimeError) as exc:
        print(f"[FAIL] {exc}")
        return 2
    if not object_paths:
        print("[FAIL] No asset paths supplied.")
        return 2

    if args.min_total_nodes < 1 or any(index >= len(object_paths) for index in asset_floors):
        print("[FAIL] Node floors must be positive and their indices must refer to supplied assets.")
        return 2

    output_root = Path(args.output_root).resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    tag = args.tag
    spec_paths: list[Path] = []
    result_paths: dict[Path, str] = {}
    png_dir = output_root / "renders"
    png_dir.mkdir(parents=True, exist_ok=True)
    for index, object_path in enumerate(object_paths):
        asset_name = object_path.rsplit(".", 1)[-1]
        render_png_path = png_dir / f"{tag}-{index}-{asset_name}.png"
        result_json_path = png_dir / f"{tag}-{index}-{asset_name}.json"
        spec_path = output_root / f"{tag}-{index}-automation_asset_spec.json"
        spec_path.write_text(
            json.dumps(build_asset_spec(object_path, render_png_path, result_json_path), indent=2),
            encoding="utf-8",
        )
        spec_paths.append(spec_path)
        result_paths[result_json_path] = object_path

    driver_script_path = write_editor_driver_script(
        output_root / f"{tag}_editor_driver.py",
        output_root / f"{tag}_editor_driver_manifest.json",
        output_root / f"{tag}_editor_driver.log",
        {"spec_paths": [spec_path.as_posix() for spec_path in spec_paths]},
        "spec_paths",
    )
    gate_log_path = output_root / f"{tag}_unreal.log"

    command = [
        str(editor_path),
        str(project_path),
        "-nop4",
        "-NoSound",
        "-nosplash",
        "-NoMutex",
        "-stdout",
        "-FullStdOutLogOutput",
        "-unattended",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    started_at = time.time()
    print(f"[GATE] Launching fresh editor process for {len(spec_paths)} asset(s)...")
    with open(gate_log_path, "w", encoding="utf-8", errors="replace") as handle:
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
        try:
            exit_code = process.wait(timeout=args.deadline_sec)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print(f"[FAIL] Editor exceeded the {args.deadline_sec}s deadline and was killed.")
            return 3
    elapsed = time.time() - started_at
    print(f"[GATE] Editor exited with code {exit_code} after {elapsed:.0f}s.")

    failures: list[str] = []
    if exit_code != 0:
        failures.append(f"Editor exited with non-zero code {exit_code}; reload completion is not verified.")
    log_hits = audit_log_for_corruption(gate_log_path)
    if log_hits:
        failures.append(f"fresh-log corruption signatures ({len(log_hits)} hit(s)):")
        failures.extend(f"  {hit}" for hit in log_hits[:20])

    result_failures, summaries = evaluate_results(result_paths, args.min_total_nodes, started_at, asset_floors, baselines)
    failures.extend(result_failures)

    for summary in summaries:
        print(f"[OK] {summary}")

    if failures:
        print("[FAIL] Fresh-process reload gate FAILED:")
        for failure in failures:
            print(f"  {failure}")
        print(f"Gate log: {gate_log_path}")
        return 1

    print("[PASS] Fresh-process reload gate passed: clean reload, no corruption signatures, node floors held.")
    print(f"Gate log: {gate_log_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--project", required=True, help="Runtime .uproject path. No env fallback, fails closed.")
    parser.add_argument("--editor", required=True, help="UnrealEditor.exe path. No env fallback, fails closed.")
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--assets", default="", help="Comma-separated /Game/... WidgetBlueprint object paths.")
    inputs.add_argument("--run-root", default="", help="Completed suite run; reload every recorded asset and verify structure, text and pixels.")
    parser.add_argument("--output-root", required=True, help="Directory for specs, driver scripts, logs, and renders.")
    parser.add_argument("--tag", default="freshreload", help="Filename tag for generated gate artifacts.")
    parser.add_argument(
        "--min-total-nodes",
        type=int,
        default=1,
        help="Global guarded floor for asset_structure.total_nodes (default 1 = any non-empty tree).",
    )
    parser.add_argument(
        "--asset-floors",
        default="",
        help=(
            "Per-asset floors as 'INDEX=COUNT' pairs separated by commas or semicolons "
            "(e.g. '0=136,1=219'). Overrides --min-total-nodes for that asset index."
        ),
    )
    parser.add_argument("--deadline-sec", type=int, default=900, help="Hard deadline for the editor process.")
    return parser


def main(argv: list[str] | None = None) -> int:
    return run_gate(build_parser().parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
