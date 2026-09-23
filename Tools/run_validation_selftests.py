"""Run the UE-free verifier contracts. No user design corpus is required."""
from pathlib import Path
import py_compile
import subprocess
import sys


def main() -> int:
    tools = Path(__file__).resolve().parent
    tests = sorted(tools.glob("design2umg_*_selftest.py"))
    if not tests:
        raise RuntimeError("No verifier selftests found")
    for path in sorted(tools.glob("*.py")) + sorted((tools / "design2umg_verify_lib").glob("*.py")):
        py_compile.compile(str(path), doraise=True)
    for path in tests:
        print(f"[RUN] {path.name}", flush=True)
        result = subprocess.run([sys.executable, str(path)], cwd=tools.parent)
        if result.returncode:
            return result.returncode
    print(f"[PASS] {len(tests)} verifier selftest suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
