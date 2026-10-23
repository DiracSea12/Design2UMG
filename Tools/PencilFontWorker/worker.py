"""Import-time font conversion. No Unreal or system-Python runtime dependency."""

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import re

from font_policy import MAX_FONT_BYTES


def require_font_size(size, error):
    if size > MAX_FONT_BYTES:
        raise ValueError(error)


def require_absolute_paths(paths, error):
    if any(not path.is_absolute() for path in paths):
        raise ValueError(error)


def number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Font coordinates must be finite numbers.")
    return value


def name_instance(font, axes):
    """Describe the actual static location, including non-named axis values."""
    names = font["name"]
    family = names.getDebugName(16) or names.getDebugName(1)
    if not family:
        raise ValueError("The source font has no family name.")
    location = " ".join(f"{tag}={value:g}" for tag, value in sorted(axes.items()))
    prefix = names.getDebugName(25) or names.getDebugName(6) or family
    prefix = re.sub(r"[^A-Za-z0-9_.-]", "", prefix) or "Font"
    digest = hashlib.sha256(location.encode("utf-8")).hexdigest()[:16]
    values = {2: location, 17: location, 4: f"{family} {location}", 6: f"{prefix[:46]}-{digest}"}
    for name_id, text in values.items():
        for record in list(names.names):
            if record.nameID == name_id:
                names.setName(text, name_id, record.platformID, record.platEncID, record.langID)
        names.setName(text, name_id, 3, 1, 0x409)
    # Family records 1/16 remain the source font's own identity, not a library alias.


def read_font(path):
    from fontTools.ttLib import TTFont

    source = Path(path)
    require_absolute_paths((source,), "Font input path must be absolute.")
    require_font_size(source.stat().st_size,
                      f"Source font exceeds the {MAX_FONT_BYTES // (1024 * 1024)} MiB font limit.")
    data = source.read_bytes()
    if data[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"wOF2"):
        raise ValueError("Expected a TTF, OpenType, or WOFF2 font.")
    return data, TTFont(io.BytesIO(data), recalcTimestamp=False)


def inspect(request):
    data, font = read_font(request["input_path"])
    names = font["name"]
    result = {
        "success": True,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "family": names.getDebugName(16) or names.getDebugName(1) or "",
        "postscript": names.getDebugName(6) or "",
        "weight": int(font["OS/2"].usWeightClass),
        "italic": bool(font["OS/2"].fsSelection & 1),
        "variable": "fvar" in font,
    }
    if "fvar" in font:
        for axis in font["fvar"].axes:
            if axis.axisTag == "wght":
                result.update(weight_min=axis.minValue, weight_max=axis.maxValue)
    return result


def convert(request):
    from fontTools.misc.fixedTools import floatToFixedToFloat
    from fontTools.varLib.instancer import instantiateVariableFont

    source = Path(request["input_path"])
    destination = Path(request["output_path"])
    require_absolute_paths((source, destination), "Font input and output paths must be absolute.")
    if source.resolve() == destination.resolve() or destination.exists():
        raise ValueError("Font output must be a new file, separate from the source.")
    data, font = read_font(source)
    weight = number(request["weight"])
    overrides = request.get("axes", {})
    if not isinstance(overrides, dict):
        raise ValueError("Font axes must be an object.")
    axes = {}
    if "fvar" in font:
        definitions = {axis.axisTag: axis for axis in font["fvar"].axes}
        if overrides.keys() - definitions.keys():
            raise ValueError("Requested axis is absent from the font's fvar table.")
        if "wght" in overrides and number(overrides["wght"]) != weight:
            raise ValueError("The wght override conflicts with the requested weight.")
        for tag, definition in definitions.items():
            value = overrides.get(tag, weight if tag == "wght" else definition.defaultValue)
            axes[tag] = floatToFixedToFloat(max(definition.minValue, min(definition.maxValue, number(value))), 16)
        font = instantiateVariableFont(font, axes, inplace=True, updateFontNames=False)
        name_instance(font, axes)
    elif overrides:
        raise ValueError("A static font has no variation axes.")
    if axes or font.flavor == "woff2":
        font.flavor = None
        output = io.BytesIO()
        font.save(output)
        data_out = output.getvalue()
    else:
        data_out = data  # Static sfnt fonts keep their original bytes and names.
    size_error = f"Font conversion did not produce a static font within the {MAX_FONT_BYTES // (1024 * 1024)} MiB font limit."
    if "fvar" in font:
        raise ValueError(size_error)
    require_font_size(len(data_out), size_error)
    actual_weight = int(font["OS/2"].usWeightClass)
    with destination.open("xb") as stream:
        stream.write(data_out)
    return {
        "success": True,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "output_sha256": hashlib.sha256(data_out).hexdigest(),
        "weight": actual_weight,
        "axes": axes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--response", required=True, type=Path)
    args = parser.parse_args()
    if not args.response.is_absolute():
        return 2
    try:
        require_absolute_paths((args.request, args.response), "Request and response paths must be absolute.")
        request = json.loads(args.request.read_text(encoding="utf-8"))
        action = request.get("action", "convert")
        if action == "inspect":
            reply = inspect(request)
        elif action == "convert":
            reply = convert(request)
        else:
            raise ValueError("Unknown font action.")
    except Exception as error:
        reply = {"success": False, "error": str(error)}
    try:
        with args.response.open("x", encoding="utf-8") as stream:
            json.dump(reply, stream, ensure_ascii=False)
    except OSError:
        return 2
    return 0 if reply["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
