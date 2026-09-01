# Sync every TTF/OTF in the plugin's Fonts/ folder into
# /Game/Design2UMG/Fonts composite font assets, driven purely by filename
# convention (idempotent upserts):
#   {Family}-{Typeface}.ttf  -> asset {Family}, typeface {Typeface}
#   {Family}-{Typeface}Italic.ttf -> typeface "{Typeface} Italic"
#   {Family}.ttf (no other {Family}-* files) -> typeface Regular
# Typeface values containing spaces are quoted in the console command, so
# names like "SemiBold Italic" register correctly (FParse::Value stops at
# unquoted whitespace).
#
# Run: UnrealEditor-Cmd <uproject> -ExecutePythonScript=this.py
#      (override the source folder with DESIGN2UMG_FONT_DIR env if needed)
import os
import unreal
from pathlib import Path

FONTS_DIR = Path(os.environ.get(
    'DESIGN2UMG_FONT_DIR',
    Path(__file__).resolve().parent.parent / 'Fonts',
))
WEIGHTS = {'Regular', 'Medium', 'SemiBold', 'Bold', 'ExtraBold', 'Light', 'Thin', 'Black'}


def parse(stem, has_suffixed_siblings):
    if '-' not in stem:
        # bare file: legacy ambiguous naming - only map to Regular when no
        # suffixed sibling disambiguates the family
        return stem, 'Regular', has_suffixed_siblings
    family, last = stem.rsplit('-', 1)
    if last == 'Italic':
        return family, 'Italic', False
    if last.endswith('Italic') and last[:-6] in WEIGHTS:
        return family, last[:-6] + ' Italic', False
    if last in WEIGHTS:
        return family, last, False
    return stem, 'Regular', False  # suffix is part of the family name (HuXiaoBo-NanShen)


files = sorted(p for p in FONTS_DIR.iterdir() if p.suffix.lower() in ('.ttf', '.otf'))
stems = {p.stem for p in files}
registered = 0
for p in files:
    family = p.stem.rsplit('-', 1)[0] if '-' in p.stem else p.stem
    has_siblings = any(q != p.stem and q.startswith(family + '-') for q in stems)
    family, typeface, skip = parse(p.stem, has_siblings)
    if skip:
        print(f'skip legacy bare file: {p.name}')
        continue
    cmd = (f'Design2UMG.BuildFontFamily Font=/Game/Design2UMG/Fonts/{family} '
           f'Typeface="{typeface}" Ttf="{p.as_posix()}"')
    unreal.SystemLibrary.execute_console_command(None, cmd)
    registered += 1
print(f'design2umg_font_sync: {registered} entries processed from {len(files)} files in {FONTS_DIR}')
unreal.SystemLibrary.quit_editor()
