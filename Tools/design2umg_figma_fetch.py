#!/usr/bin/env python3
"""Fetch Figma designs into local export dirs for Design2UMG validation.

Stages (token required, reads FIGMA_PAT env):
  probe   KEY                 -- depth=2 structure probe (canvas/frame overview)
  analyze KEY                 -- full fetch, report fontFamily/weight/italic/size diversity
  build   KEY NODE_ID OUTDIR  -- build a FigmaExporter-shaped export dir (design.json + images/ + reference.png)

A built dir dropped into the plugin's FigmaExports/ container (or any
--figma-export-root container) is discovered as local+source cases by the
next suite run with zero tooling changes. Community files must be
duplicated into your own drafts first (the REST API cannot read them
directly); files you own are readable by key.

Quota notes: the render endpoint (/v1/images at scale=2) is far more
expensive than reads on starter-tier tokens - a handful of builds can
exhaust a token for days. Vector nodes without fills are sometimes
refused server-side (returned as no render), which is a Figma-side
limitation, not a pipeline error.
"""
import json
import copy
import os
import sys
import urllib.parse
import urllib.request
from collections import Counter
from pathlib import Path

from design2umg_verify_lib.net_guard import build_guarded_opener

API = "https://api.figma.com/v1"

_REMOTE_HTTP = build_guarded_opener()


def http_json(url: str, token: str):
    req = urllib.request.Request(url, headers={"X-Figma-Token": token})
    with _REMOTE_HTTP.open(req, timeout=120) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_download(url: str, output_dir: Path, filename: str) -> Path:
    if Path(filename).name != filename or filename in ("", ".", ".."):
        raise ValueError(f"Refusing unsafe download file name: {filename}")
    out_path = output_dir / filename
    out_path.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url)
    with _REMOTE_HTTP.open(req, timeout=300) as resp, out_path.open("wb") as fh:
        while True:
            chunk = resp.read(1 << 16)
            if not chunk:
                break
            fh.write(chunk)
    return out_path


def get_token() -> str:
    token = os.environ.get("FIGMA_PAT", "").strip()
    if not token:
        sys.exit("FIGMA_PAT env var not set")
    return token


def walk_text_styles(node: dict, fonts: Counter, weights: Counter, italics: Counter, sizes: Counter):
    style = node.get("style")
    if isinstance(style, dict) and style.get("fontFamily"):
        fonts[f"{style.get('fontFamily')} / {style.get('fontPostScriptName') or '-'}"] += 1
        weights[str(style.get("fontWeight"))] += 1
        if style.get("italic"):
            italics[str(style.get("fontFamily"))] += 1
        sizes[str(style.get("fontSize"))] += 1
    for child in node.get("children") or []:
        if isinstance(child, dict):
            walk_text_styles(child, fonts, weights, italics, sizes)


def collect_image_refs(node: dict, refs: set):
    for fill in node.get("fills") or []:
        if isinstance(fill, dict) and fill.get("type") == "IMAGE" and fill.get("imageRef"):
            refs.add(fill["imageRef"])
    for fill in node.get("strokes") or []:
        if isinstance(fill, dict) and fill.get("type") == "IMAGE" and fill.get("imageRef"):
            refs.add(fill["imageRef"])
    for child in node.get("children") or []:
        if isinstance(child, dict):
            collect_image_refs(child, refs)


def walk_nodes(node):
    yield node
    for child in node.get("children") or []:
        yield from walk_nodes(child)


def walk_actions(actions):
    for action in actions or []:
        if not isinstance(action, dict):
            continue
        yield action
        for block in action.get("conditionalBlocks") or []:
            yield from walk_actions(block.get("actions"))


def prepare_design(data: dict, node_id: str, file_key=None):
    nodes = {node["id"]: node for node in walk_nodes(data["document"])}
    if node_id not in nodes:
        raise ValueError(f"Selected node {node_id} is missing")
    if file_key and data.get("fileKey") and data["fileKey"] != file_key:
        raise ValueError("Response fileKey does not match the requested file")
    components = data.get("components") or {}
    sets = data.get("componentSets") or {}
    parent_sets = {child["id"]: node["id"] for node in nodes.values()
                   if node.get("type") == "COMPONENT_SET" for child in node.get("children") or []}
    design = copy.deepcopy({key: value for key, value in data.items() if key != "document"})
    design["document"] = copy.deepcopy(nodes[node_id])
    if file_key:
        design["fileKey"] = file_key
    bundle = {"schemaVersion": 1, "definitions": [], "instances": {}, "errors": []}
    design["design2umgComponentDependencies"] = bundle
    pages = []
    design["design2umgPrototypePages"] = pages
    definitions = {}
    runtime_ids = set()
    pending = []

    def add_runtime(node):
        runtime_ids.update(child["id"] for child in walk_nodes(node))
        pending.append((node, True))

    def require_component(component_id, origin):
        if component_id in definitions:
            return definitions[component_id]
        metadata = components.get(component_id) or {}
        component = nodes.get(component_id)
        if metadata.get("remote") or not component or component.get("type") != "COMPONENT":
            raise ValueError(f"Node {origin}: component {component_id} definition is missing or inaccessible")
        identity = {"id": component_id, "key": metadata.get("key") or ""}
        set_id = metadata.get("componentSetId") or parent_sets.get(component_id)
        if set_id:
            identity["componentSetId"] = set_id
            identity["componentSetKey"] = (sets.get(set_id) or {}).get("key") or ""
        document = copy.deepcopy(component)
        definitions[component_id] = identity
        bundle["definitions"].append({**identity, "document": document})
        pending.append((document, False))
        return identity

    add_runtime(design["document"])
    while pending:
        tree, runtime = pending.pop(0)
        for node in walk_nodes(tree):
            if node.get("type") == "COMPONENT":
                require_component(node["id"], node["id"])
            if node.get("type") == "INSTANCE":
                identity = require_component(node.get("componentId"), node["id"])
                for field, value in identity.items():
                    if field != "id" and value:
                        node["mainComponentKey" if field == "key" else field] = value
                if runtime:
                    bundle["instances"][node["id"]] = dict(identity)
            for interaction in node.get("interactions") or []:
                if not interaction.get("trigger"):
                    continue
                for action in walk_actions(interaction.get("actions")):
                    if action.get("type") != "NODE":
                        continue
                    navigation = action.get("navigation")
                    if not isinstance(navigation, str) or not navigation:
                        raise ValueError(f"Node {node['id']}: NODE action is missing required navigation")
                    if navigation not in ("CHANGE_TO", "NAVIGATE", "OVERLAY", "SWAP", "OPEN_OVERLAY", "SWAP_OVERLAY"):
                        continue
                    target_id = action.get("destinationId")
                    if not target_id:
                        raise ValueError(f"Node {node['id']}: {navigation} destination is missing")
                    if navigation == "CHANGE_TO":
                        require_component(target_id, node["id"])
                    elif target_id not in runtime_ids:
                        target = nodes.get(target_id)
                        if not target or target.get("type") != "FRAME":
                            raise ValueError(f"Node {node['id']}: {navigation} destination {target_id} is missing or is not a FRAME")
                        page = copy.deepcopy(target)
                        pages.append(page)
                        add_runtime(page)

    completed = set()
    active = set()
    documents = {entry["id"]: entry["document"] for entry in bundle["definitions"]}

    def check_structure(component_id):
        if component_id in active:
            raise ValueError(f"Component structure cycle at {component_id}")
        if component_id in completed:
            return
        active.add(component_id)
        for node in walk_nodes(documents[component_id]):
            if node.get("type") == "INSTANCE":
                check_structure(node["componentId"])
        active.remove(component_id)
        completed.add(component_id)

    for component_id in definitions:
        check_structure(component_id)
    return design


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    stage, key = sys.argv[1], sys.argv[2]
    token = get_token()

    if stage == "probe":
        data = http_json(f"{API}/files/{key}?depth=2", token)
        print("name:", data.get("name"))
        for page in (data.get("document") or {}).get("children") or []:
            frames = page.get("children") or []
            print(f"  canvas '{page.get('name')}': {len(frames)} top frames")
            for frame in frames[:12]:
                print(f"    {frame.get('id'):>16}  {frame.get('type'):<10} {frame.get('name')}")

    elif stage == "analyze":
        data = http_json(f"{API}/files/{key}", token)
        fonts, weights, italics, sizes = Counter(), Counter(), Counter(), Counter()
        walk_text_styles(data.get("document") or {}, fonts, weights, italics, sizes)
        print("name:", data.get("name"))
        print("== fontFamily / postScript ==")
        for name, count in fonts.most_common(25):
            print(f"  {count:>6}  {name}")
        print("== weights ==", dict(weights.most_common()))
        print("== italic families ==", dict(italics.most_common()))
        print("== distinct sizes ==", len(sizes))

    elif stage == "build":
        if len(sys.argv) < 5:
            sys.exit("build KEY NODE_ID OUTDIR")
        node_id, outdir = sys.argv[3], Path(sys.argv[4])
        data = http_json(f"{API}/files/{key}?geometry=paths", token)
        design = prepare_design(data, node_id, key)
        node_doc = design["document"]
        outdir.mkdir(parents=True, exist_ok=True)
        images_dir = outdir / "images"
        images_dir.mkdir(exist_ok=True)

        (outdir / "design.json").write_text(json.dumps(design), encoding="utf-8")

        refs: set = set()
        collect_image_refs(node_doc, refs)
        for entry in design["design2umgComponentDependencies"]["definitions"]:
            collect_image_refs(entry["document"], refs)
        for page in design["design2umgPrototypePages"]:
            collect_image_refs(page, refs)
        if refs:
            payload = http_json(f"{API}/files/{key}/images", token)
            mapping = (payload.get("meta") or {}).get("images") or payload.get("images") or {}
            done = 0
            for ref in refs:
                url = mapping.get(ref)
                if not url:
                    continue
                http_download(url, images_dir, f"{ref}.png")
                done += 1
            print(f"image fills: {done}/{len(refs)} downloaded")

        query = urllib.parse.urlencode({"ids": node_id, "format": "png", "scale": "2"})
        png = http_json(f"{API}/images/{key}?{query}", token).get("images", {}).get(node_id)
        if png:
            http_download(png, outdir, "reference.png")
            print("reference.png saved")
        print("export dir:", outdir)

    else:
        sys.exit(f"unknown stage {stage}")


if __name__ == "__main__":
    main()
