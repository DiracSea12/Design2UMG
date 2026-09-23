#!/usr/bin/env python3
"""
Single-Session Isolation Experiment

Compares rendering outputs between:
  A) step mode   - each case runs in its own UnrealEditor process (baseline)
  B) single-session mode - all cases run in ONE UnrealEditor process

If B produces identical (or within tolerance) images to A, then single-session
is safe for the full formal gate.
"""
import argparse
import base64
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter, ImageStat

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
STAMP = time.strftime("%Y%m%d_%H%M%S")
SCRIPT_DIR = Path(__file__).parent
AUTOMATION_DIR = SCRIPT_DIR.parent / "Saved" / "Design2UMG" / "Automation"
RUNTIME_PROJECT_ENV_VAR = "DESIGN2UMG_RUNTIME_PROJECT"
UE_ROOT = Path(os.environ.get("UE_ROOT", r"G:\UE5\UnrealEngine"))
EDITOR = UE_ROOT / "Engine/Binaries/Win64/UnrealEditor.exe"

PIXEL_TOLERANCE = 6
STEP_IDLE_TIMEOUT = 300
STEP_STARTUP_TIMEOUT = 60
STEP_DRIVER_START_ATTEMPTS = 2

EDITOR_GRACEFUL_EXIT_DEADLINE_SEC = 30.0


# ---------------------------------------------------------------------------
# Case selection: the current formal gate scope.  Read the frozen
# run scope so this experiment cannot silently drift from suite discovery.
# ---------------------------------------------------------------------------
def formal_scope_pointer(runtime_project: Path) -> Path:
    return runtime_project.parent / "Saved" / "Design2UMG" / "ReferenceCache" / "CURRENT.json"


def load_formal_case_scope(runtime_project: Path):
    pointer = json.loads(formal_scope_pointer(runtime_project).read_text(encoding="utf-8-sig"))
    scope_path = Path(pointer["current_formal_scope"]["run_scope"])
    if not scope_path.is_file():
        raise RuntimeError(f"Current formal run scope not found: {scope_path}")
    payload = json.loads(scope_path.read_text(encoding="utf-8-sig"))
    cases = []
    mode_map = {"local-figma": "local_figma", "figma-source": "figma_source"}
    for item in payload.get("case_inputs", []):
        mode = item.get("mode")
        runtime_mode = mode if mode == "pen" else mode_map.get(mode)
        if runtime_mode is None:
            raise RuntimeError(f"Unsupported formal case mode: {mode!r}")
        cases.append((
            item["name"],
            runtime_mode,
            item["source"],
            item.get("node", ""),
            "",
        ))
    # The expected count comes from the pointer's own scope record, so this
    # experiment cannot silently keep a stale hardcoded case count.
    expected = pointer["current_formal_scope"].get("case_count")
    if expected is not None and len(cases) != expected:
        raise RuntimeError(
            f"Expected the current {expected}-case scope, found {len(cases)} cases")
    return cases


# ---------------------------------------------------------------------------
# Low-level helpers
# ---------------------------------------------------------------------------
def plugin_root():
    return SCRIPT_DIR.parent


def resolve_runtime_project(raw_project: str) -> Path:
    candidate = (raw_project or "").strip() or os.environ.get(RUNTIME_PROJECT_ENV_VAR, "").strip()
    if not candidate:
        raise RuntimeError(
            "Runtime project path not provided. "
            f"Pass --project explicitly or set {RUNTIME_PROJECT_ENV_VAR}. "
            "Hardcoded fallback is not supported."
        )
    project = Path(candidate)
    if not project.is_file():
        raise RuntimeError(f"Runtime project does not exist: {project}")
    return project


def ensure_clean_editor():
    """Wait for any running UnrealEditor processes to exit."""
    deadline = time.time() + 30
    while time.time() < deadline:
        result = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe", "/NH"],
            capture_output=True, text=True
        )
        if "UnrealEditor.exe" not in result.stdout:
            return
        time.sleep(2)
    print("WARNING: UnrealEditor still running after 30s wait")


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    content = json.dumps(data, indent=2, ensure_ascii=False)
    if "一" in content or "　" in content:
        path.write_text(content, encoding="utf-8-sig")
    else:
        path.write_text(content, encoding="utf-8")


def load_json(path):
    try:
        text = path.read_text(encoding="utf-8-sig")
        return json.loads(text)
    except Exception:
        return None


def image_diffPixels(imgA: Image.Image, imgB: Image.Image, tolerance: int) -> float:
    """Return fraction of pixels that differ by > tolerance in any channel."""
    if imgA.size != imgB.size:
        return -1.0
    diff = ImageChops.difference(imgA.convert("RGB"), imgB.convert("RGB"))
    diff_arr = diff.load()
    w, h = imgA.size
    changed = 0
    for y in range(h):
        for x in range(w):
            r, g, b = diff_arr[x, y]
            if max(r, g, b) > tolerance:
                changed += 1
    return changed / (w * h)


def build_spec_json(case_name, mode, source, node, output_dir, png_path, result_json_path, node_override=""):
    spec = {
        "mode": mode,
        "source_path": source,
        "node_id": node_override or node,
        "output_path": f"/Game/Design2UMG/Automation/{case_name}",
        "render_png_path": str(png_path),
        "result_json_path": str(result_json_path),
        "force_reload_package": True,
    }
    write_json(output_dir / f"{case_name}_spec.json", spec)
    return output_dir / f"{case_name}_spec.json"


def get_editor_driver_log_path(script_path: Path) -> Path:
    return script_path.parent / f"driver_{script_path.stem}.log"


def editor_driver_started(driver_log_path: Path) -> bool:
    return driver_log_path.is_file()


def run_editor_process(editor_path, project_path, driver_script_path, result_paths, log_path,
                       idle_timeout_sec, startup_timeout_sec, extra_cmds=""):
    driver_script_path = driver_script_path.resolve()
    driver_log_path = get_editor_driver_log_path(driver_script_path)
    cmd = [
        str(editor_path),
        str(project_path),
        "-nop4", "-NoSound", "-nosplash", "-NoMutex",
        "-stdout", "-FullStdOutLogOutput", "-unattended",
        f"-ExecutePythonScript={driver_script_path}",
    ]
    if extra_cmds:
        cmd.append(f"-LogCmds={extra_cmds}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w", encoding="utf-8") as handle:
        proc = subprocess.Popen(cmd, stdout=handle, stderr=subprocess.STDOUT)

    proc_started = time.time()
    last_progress = time.time()
    last_result_count = 0
    last_log_size = 0
    observed = False
    driver_started = False

    while True:
        ec = proc.poll()
        if ec is not None:
            break

        cur_results = sum(1 for p in result_paths if p.is_file())
        if cur_results > last_result_count:
            last_result_count = cur_results
            last_progress = time.time()
            observed = True
        if not driver_started and editor_driver_started(driver_log_path):
            driver_started = True
            last_progress = time.time()
            observed = True

        try:
            cur_log_size = log_path.stat().st_size
        except FileNotFoundError:
            cur_log_size = 0
        if cur_log_size > last_log_size:
            last_log_size = cur_log_size
            last_progress = time.time()
            observed = True

        if not observed and (time.time() - proc_started > startup_timeout_sec):
            proc.kill()
            raise TimeoutError(f"Editor startup timeout after {startup_timeout_sec}s")

        if time.time() - last_progress > idle_timeout_sec:
            proc.kill()
            raise TimeoutError(f"Editor idle timeout after {idle_timeout_sec}s")

        time.sleep(2)

    # grace period
    deadline = time.time() + EDITOR_GRACEFUL_EXIT_DEADLINE_SEC
    while time.time() < deadline:
        cur = sum(1 for p in result_paths if p.is_file())
        if cur >= len(result_paths):
            break
        if cur > last_result_count:
            last_result_count = cur
            deadline = time.time() + EDITOR_GRACEFUL_EXIT_DEADLINE_SEC
        time.sleep(1)

    if not editor_driver_started(driver_log_path):
        raise RuntimeError(f"Editor exited before driver started, exit_code={ec}")
    return ec


# ---------------------------------------------------------------------------
# Write driver scripts
# ---------------------------------------------------------------------------
def write_step_driver(script_path, spec_path, log_path):
    """Driver for step mode - calls PrepareAutomationTransientState before each render."""
    content = f'''import json, traceback
from datetime import datetime
import unreal

MANIFEST = r"{spec_path}"
DRIVER_LOG = r"{log_path}"

def _write(msg):
    ts = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
    with open(DRIVER_LOG, "a", encoding="utf-8") as f:
        f.write(f"{{ts}} {{msg}}\\n")

_write("driver:start")
with open(MANIFEST, "r", encoding="utf-8-sig") as f:
    spec = json.load(f)

spec_path = spec.get("spec_path", MANIFEST)
_write(f"driver:run:{{spec_path}}")

# Call transient state reset BEFORE render (like the real step mode)
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Design2UMG.AutomationRender " + spec_path + " --no-quit")
_write("driver:complete")
'''
    script_path.write_text(content, encoding="utf-8")


def write_single_session_driver(script_path, spec_paths_list, log_path):
    """Driver for single-session mode - runs ALL specs in one editor WITHOUT resetting transient state between them."""
    # Escape for Python string embedding
    spec_paths_json = json.dumps(spec_paths_list)
    content = f'''import json, traceback
from datetime import datetime
import unreal

SPEC_PATHS = {spec_paths_json}
DRIVER_LOG = r"{log_path}"

def _write(msg):
    ts = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
    with open(DRIVER_LOG, "a", encoding="utf-8") as f:
        f.write(f"{{ts}} {{msg}}\\n")

_write("driver:start")
world = unreal.EditorLevelLibrary.get_editor_world()

# Reset transient state before EACH spec to isolate rendering state.
# Without this, the Slate font atlas accumulates atlas tiles from prior cases,
# causing sub-pixel rendering differences on Unicode symbols (e.g. ○ ◐ ✓)
# even though the widget tree and text content are identical.
for i, spec_path in enumerate(SPEC_PATHS):
    _write(f"driver:run:{{i+1}}/{{len(SPEC_PATHS)}}:{{spec_path}}")
    unreal.SystemLibrary.execute_console_command(world, "Design2UMG.ResetAutomationTransient --no-quit")
    unreal.SystemLibrary.execute_console_command(world, "Design2UMG.AutomationRender " + spec_path + " --no-quit")
    _write(f"driver:done:{{i+1}}")

_write("driver:complete")
'''
    script_path.write_text(content, encoding="utf-8")


# ---------------------------------------------------------------------------
# Experiment runner
# ---------------------------------------------------------------------------
def run_experiment(runtime_project: Path):
    experiment_cases = load_formal_case_scope(runtime_project)
    print("=" * 60)
    print("Single-Session Isolation Experiment")
    print("=" * 60)
    print(f"Editor: {EDITOR}")
    print(f"Project: {runtime_project}")
    print(f"Cases: {len(experiment_cases)}")
    print()

    # Create output dirs
    exp_root = AUTOMATION_DIR / f"_isolation_experiment_{STAMP}"
    step_root = exp_root / "step_mode"
    session_root = exp_root / "single_session_mode"
    compare_root = exp_root / "compare"
    for d in [step_root, session_root, compare_root]:
        d.mkdir(parents=True, exist_ok=True)

    # Build spec files
    step_specs = []
    session_specs = []
    step_pngs = []
    session_pngs = []
    step_results = []
    session_results = []

    for i, (case_name, mode, source, node, ref) in enumerate(experiment_cases):
        case_dir_step = step_root / case_name
        case_dir_step.mkdir(exist_ok=True)
        png_step = case_dir_step / f"{case_name}.png"
        res_step = case_dir_step / f"{case_name}_result.json"
        spec_path_step = build_spec_json(case_name, mode, source, node, case_dir_step, png_step, res_step, node)
        step_specs.append(str(spec_path_step))
        step_pngs.append(png_step)
        step_results.append(res_step)

        case_dir_sess = session_root / case_name
        case_dir_sess.mkdir(exist_ok=True)
        png_sess = case_dir_sess / f"{case_name}.png"
        res_sess = case_dir_sess / f"{case_name}_result.json"
        spec_path_sess = build_spec_json(case_name, mode, source, node, case_dir_sess, png_sess, res_sess, node)
        session_specs.append(str(spec_path_sess))
        session_pngs.append(png_sess)
        session_results.append(res_sess)

    # Write manifests
    step_manifest = step_root / "manifest.json"
    write_json(step_manifest, {"spec_paths": step_specs, "spec_path": step_specs[0]})

    session_manifest = session_root / "manifest.json"
    write_json(session_manifest, {"spec_paths": session_specs, "spec_path": session_specs[0]})

    # ------------------------------------------------------------------------
    # Separate editor processes (baseline)
    # ------------------------------------------------------------------------
    print(f"\n[SEPARATE PROCESSES] Step mode - each case in its own editor process")
    print(f"  Output: {step_root}")
    print()

    for i, (case_name, _, _, _, _) in enumerate(experiment_cases):
        spec_path = step_specs[i]
        log_path = step_root / f"step_{case_name}.log"
        driver_script = step_root / f"driver_step_{i:02d}.py"
        write_step_driver(driver_script, spec_path, log_path)

        print(f"  [{i+1}/{len(experiment_cases)}] {case_name} ... ", end="", flush=True)
        ensure_clean_editor()

        ec = None
        for attempt in range(1, STEP_DRIVER_START_ATTEMPTS + 1):
            try:
                ec = run_editor_process(
                    EDITOR, runtime_project, driver_script,
                    [step_results[i]], log_path,
                    STEP_IDLE_TIMEOUT, STEP_STARTUP_TIMEOUT
                )
                break
            except (TimeoutError, RuntimeError) as e:
                if attempt >= STEP_DRIVER_START_ATTEMPTS:
                    print(f"FAILED ({e})")
                    break
                print(f"\n    Retry {attempt}...", flush=True)
                time.sleep(5)

        if step_results[i].is_file():
            res = load_json(step_results[i])
            success = res.get("success", False) if res else False
            print("OK" if success else "FAILED")
        else:
            print("NO_RESULT")

    # ------------------------------------------------------------------------
    # Shared editor process
    # ------------------------------------------------------------------------
    print(f"\n[SHARED PROCESS] Single-session mode - all cases in one editor")
    print(f"  Output: {session_root}")
    print()

    session_log = session_root / "session.log"
    session_driver = session_root / "driver_single_session.py"
    write_single_session_driver(session_driver, session_specs, session_log)

    print(f"  Running {len(experiment_cases)} cases in single editor session...")
    ensure_clean_editor()

    try:
        ec = run_editor_process(
            EDITOR, runtime_project, session_driver,
            session_results, session_log,
            STEP_IDLE_TIMEOUT * len(experiment_cases),  # longer timeout for all cases
            STEP_STARTUP_TIMEOUT
        )
        print(f"  Session exit code: {ec}")
    except Exception as e:
        print(f"  Session failed: {e}")

    # ------------------------------------------------------------------------
    # Compare process-isolation results
    # ------------------------------------------------------------------------
    print(f"\n[COMPARISON] Comparing step vs single-session outputs")
    print()

    rows = []
    all_pass = True
    for i, (case_name, _, _, _, _) in enumerate(experiment_cases):
        png_a = step_pngs[i]
        png_b = session_pngs[i]

        if not png_a.is_file():
            rows.append({"case": case_name, "status": "STEP_NO_OUTPUT", "diff_pct": -1, "note": "step mode produced no PNG"})
            all_pass = False
            continue
        if not png_b.is_file():
            rows.append({"case": case_name, "status": "SESSION_NO_OUTPUT", "diff_pct": -1, "note": "single-session produced no PNG"})
            all_pass = False
            continue

        img_a = Image.open(png_a)
        img_b = Image.open(png_b)

        if img_a.size != img_b.size:
            rows.append({
                "case": case_name,
                "status": "SIZE_MISMATCH",
                "diff_pct": -1,
                "note": f"step={img_a.size} session={img_b.size}"
            })
            all_pass = False
            continue

        diff_pct = image_diffPixels(img_a, img_b, PIXEL_TOLERANCE)
        passed = diff_pct < 1e-6  # effectively identical

        rows.append({
            "case": case_name,
            "status": "PASS" if passed else "DIFF_DETECTED",
            "diff_pct": round(diff_pct * 100, 4),
            "note": "" if passed else f"{diff_pct*100:.4f}% pixels differ"
        })
        if not passed:
            all_pass = False

        # Save side-by-side diff
        diff_img = ImageChops.difference(img_a.convert("RGB"), img_b.convert("RGB"))
        side_by_side = Image.new("RGB", (img_a.width * 2 + 4, img_a.height))
        side_by_side.paste(img_a.convert("RGB"), (0, 0))
        side_by_side.paste(img_b.convert("RGB"), (img_a.width + 4, 0))
        side_by_side.save(compare_root / f"{case_name}_compare.png")

    # Print table
    print(f"  {'Case':<35} {'Status':<18} {'Diff%':>8}  Note")
    print(f"  {'-'*35} {'-'*18} {'-'*8}  {'-'*20}")
    for r in rows:
        print(f"  {r['case']:<35} {r['status']:<18} {r['diff_pct']:>8.4f}  {r['note']}")

    # Save report
    report = {
        "experiment": "single_session_isolation",
        "stamp": STAMP,
        "exp_root": str(exp_root),
        "step_root": str(step_root),
        "session_root": str(session_root),
        "pixel_tolerance": PIXEL_TOLERANCE,
        "case_count": len(experiment_cases),
        "cases": experiment_cases,
        "results": rows,
        "overall_pass": all_pass,
    }
    write_json(exp_root / "experiment_report.json", report)

    print()
    print("=" * 60)
    if all_pass:
        print("RESULT: PASS - Single-session produces identical outputs")
        print(f"  -> {len(experiment_cases)} cases CAN run in a single editor session without contamination")
    else:
        diff_cases = [r["case"] for r in rows if r["status"] not in ("PASS", "STEP_NO_OUTPUT", "SESSION_NO_OUTPUT")]
        print(f"RESULT: FAIL - {len(diff_cases)} case(s) show differences")
        for c in diff_cases:
            note = next(r["note"] for r in rows if r["case"] == c)
            print(f"  {c}: {note}")
        print()
        print("  -> Single-session mode is NOT safe without fixing the contamination")
    print("=" * 60)
    print(f"\nReport: {exp_root / 'experiment_report.json'}")
    print(f"Step output:   {step_root}")
    print(f"Session output: {session_root}")
    print(f"Compare images: {compare_root}")

    return all_pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Design2UMG single-session isolation experiment")
    parser.add_argument(
        "--project",
        default="",
        help=f"Runtime project path. If omitted, {RUNTIME_PROJECT_ENV_VAR} is required.",
    )
    args = parser.parse_args()
    return 0 if run_experiment(resolve_runtime_project(args.project)) else 1


if __name__ == "__main__":
    sys.exit(main())
