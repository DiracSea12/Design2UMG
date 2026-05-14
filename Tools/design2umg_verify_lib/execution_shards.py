import os


def resolve_parallel_editor_count(requested: int, case_count: int) -> int:
    if case_count <= 1:
        return 1
    if requested > 0:
        return max(1, requested)

    cpu_count = os.cpu_count() or 1
    if case_count < 8:
        return 1
    # Keep auto-sharding conservative on shared projects; users can still force
    # higher values with --parallel-editors when the machine is stable.
    if cpu_count >= 12 and case_count >= 8:
        return 2
    return 1


def resolve_post_worker_count(requested: int, case_count: int) -> int:
    if case_count <= 1:
        return 1
    if requested > 0:
        return max(1, requested)

    cpu_count = os.cpu_count() or 1
    baseline = max(1, cpu_count // 2)
    return max(1, min(8, baseline, case_count))


def build_execution_shards(states: list[dict], shard_count: int) -> list[dict]:
    normalized_shard_count = max(1, min(shard_count, len(states)))
    shards = [
        {
            "id": index,
            "states": [],
            "spec_paths": [],
            "result_paths": [],
        }
        for index in range(normalized_shard_count)
    ]

    sorted_states = sorted(states, key=lambda item: len(item["spec_paths"]), reverse=True)
    for state in sorted_states:
        shard = min(shards, key=lambda item: len(item["spec_paths"]))
        shard["states"].append(state)
        shard["spec_paths"].extend(state["spec_paths"])
        shard["result_paths"].extend(step["result_json_path"] for step in state["steps"].values())

    return [shard for shard in shards if shard["states"]]
