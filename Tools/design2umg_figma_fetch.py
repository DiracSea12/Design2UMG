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


def collect_vector_nodes(node: dict, ids: list, root_id: str):
    if node.get("id") != root_id and node.get("type") in ("VECTOR", "LINE", "STAR", "ELLIPSE", "POLYGON", "BOOLEAN_OPERATION", "RECTANGLE"):
        ids.append(node["id"])
    for child in node.get("children") or []:
        if isinstance(child, dict):
            collect_vector_nodes(child, ids, root_id)


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
        outdir.mkdir(parents=True, exist_ok=True)
        images_dir = outdir / "images"
        images_dir.mkdir(exist_ok=True)

        sub = http_json(f"{API}/files/{key}/nodes?ids={urllib.parse.quote(node_id)}&geometry=paths", token)
        node_doc = (sub.get("nodes") or {}).get(node_id, {}).get("document")
        if node_doc is None:
            sys.exit(f"node {node_id} not returned")
        design = {
            "document": {"id": "0:0", "name": sub.get("name") or key, "type": "DOCUMENT", "children": [
                {"id": "0:1", "name": "Page 1", "type": "CANVAS", "children": [node_doc]}
            ]},
            "name": sub.get("name") or key,
            "version": sub.get("version"),
            "schemaVersion": sub.get("schemaVersion"),
            "components": sub.get("components") or {},
        }
        (outdir / "design.json").write_text(json.dumps(design), encoding="utf-8")

        refs: set = set()
        collect_image_refs(node_doc, refs)
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

        vec_ids: list = []
        collect_vector_nodes(node_doc, vec_ids, node_doc.get("id"))
        done = 0
        for start in range(0, len(vec_ids), 60):
            suite = vec_ids[start:start + 60]
            query = urllib.parse.urlencode({"ids": ",".join(suite), "format": "png", "scale": "2"})
            fills = http_json(f"{API}/images/{key}?{query}", token).get("images") or {}
            for vid in suite:
                url = fills.get(vid)
                if not url:
                    continue
                http_download(url, images_dir, f"vec_{vid.replace(':', '_').replace('/', '_')}.png")
                done += 1
        print(f"vector renders: {done}/{len(vec_ids)}")

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
