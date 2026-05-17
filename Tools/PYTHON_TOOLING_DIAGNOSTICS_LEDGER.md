# Python Tooling Diagnostics Ledger

Phase24 section 5 review summary for the maintainer-side Python tooling.

This file is intentionally short-lived and decision-focused. It records what was
reviewed and what contract was accepted in this phase, without freezing exact
line numbers that would go stale after routine script edits.

## Scope Reviewed

- `Tools/design2umg_auto_verify_batch.py`
- `Tools/design2umg_rebuild_admissible_best.py`
- `Tools/design2umg_verify_lib/run_history.py`
- Embedded editor-driver patterns inside `Tools/design2umg_auto_verify.py`

## Decisions

### Output contracts

- `design2umg_auto_verify_batch.py`
  - Contract stdout is retained for dry-run path reporting, per-case `[CASE]` /
    `[BLOCKED]` lines, and final `[SUMMARY] report=...`.
  - Non-contract error reporting is allowed on stderr.

- `design2umg_rebuild_admissible_best.py`
  - The JSON manifest summary stays on stdout because downstream tooling treats
    it as the command contract.

- `design2umg_verify_lib/run_history.py`
  - Historical-best path/source prints are diagnostics, not command contract.
  - They are routed to stderr so they cannot corrupt wrapper stdout parsing.

### Broad exception handlers

- No `except: pass` pattern was accepted in the reviewed host-side tooling.
- Best-effort handlers that only protect optional discovery/probe behavior are
  retained with evidence.
- The embedded editor-driver `_get_world()` fallback in
  `design2umg_auto_verify.py` remains a retained best-effort runtime probe.

### Named constants and readability

- `design2umg_auto_verify_batch.py`
  - `EDITOR_GRACEFUL_EXIT_DEADLINE_SEC` names the editor post-exit wait window.

- `design2umg_rebuild_admissible_best.py`
  - Phase24 added a contract docstring to
    `rebuild_historical_best_manifest()` because the cache/manifest behavior is
    non-obvious and reused by downstream validation.

- `design2umg_verify_lib/run_history.py`
  - Phase24 added a contract docstring to
    `write_historical_best_validation()` and moved diagnostic prints to stderr.

## Dependency conclusion

- No standalone `requirements.txt` or `pyproject.toml` was required for this
  batch.
- Pillow remains the only documented external runtime dependency for the
  maintainer-side visual validation tooling.
- All other reviewed imports are stdlib or repo-local modules.

## Follow-up boundary

- This ledger is a phase-local review artifact, not a generated source of truth.
- If a future phase materially changes stdout contracts, broad exception
  handling, or external dependencies in these scripts, refresh this file rather
  than treating it as evergreen evidence.
