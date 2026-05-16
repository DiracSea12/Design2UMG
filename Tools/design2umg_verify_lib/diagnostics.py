import json
import sys
from pathlib import Path


class EvidenceReadError(RuntimeError):
    def __init__(self, reason: str, path: Path, detail: str = ""):
        self.reason = reason
        self.path = Path(path)
        self.detail = detail
        message = f"{reason}: {self.path}"
        if detail:
            message += f" ({detail})"
        super().__init__(message)


def diagnose_json_load_error(path: Path, error: Exception) -> EvidenceReadError:
    reason = "json_parse_failed" if isinstance(error, json.JSONDecodeError) else "json_read_failed"
    return EvidenceReadError(reason, path, str(error))


def emit_diagnostic(level: str, reason: str, path: Path | str = "", detail: str = "") -> None:
    payload = {
        "level": level,
        "reason": reason,
    }
    if path:
        payload["path"] = str(path)
    if detail:
        payload["detail"] = detail
    print(f"[DIAGNOSTIC] {json.dumps(payload, ensure_ascii=False, sort_keys=True)}", file=sys.stderr)
