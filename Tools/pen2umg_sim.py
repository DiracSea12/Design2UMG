"""
pen2umg_sim.py - Simulates Design2UMG conversion pipeline in Python.
Outputs widget tree with coordinates for comparison with UE Designer.

Usage:
  python pen2umg_sim.py <pen_file> [--frame <frame_id>] [--screenshot <png>]
"""
import json, sys, copy, re, argparse
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def parse_dim(v):
    if v is None: return ('fit', 0)
    if isinstance(v, (int, float)): return ('fixed', float(v))
    if isinstance(v, str):
        if v == 'fill_container': return ('fill', 0)
        if v == 'fit_content': return ('fit', 0)
        m = re.match(r'fill_container\((\d+\.?\d*)\)', v)
        if m: return ('fill', float(m.group(1)))
        m = re.match(r'fit_content\((\d+\.?\d*)\)', v)
        if m: return ('fit', float(m.group(1)))
        try: return ('fixed', float(v))
        except: return ('fit', 0)
    if isinstance(v, dict):
        return (v.get('type', 'fit'), float(v.get('value', 0) or 0))
    return ('fit', 0)


class PenDoc:
    def __init__(self, data):
        self.variables = data.get('variables', {})
        self.themes = data.get('themes', [])
        self.reusables = {}
        self.pages = data.get('children', [])
        for page in self.pages:
            self._collect_reusables(page)

    def _collect_reusables(self, node):
        if node.get('reusable'):
            self.reusables[node['id']] = node
        for c in node.get('children', []):
            self._collect_reusables(c)

    def find_node(self, tid, node=None):
        if node is None:
            for page in self.pages:
                r = self.find_node(tid, page)
                if r: return r
            return None
        if node.get('id') == tid: return node
        for c in node.get('children', []):
            r = self.find_node(tid, c)
            if r: return r
        return None

    def expand_ref(self, node, depth=0):
        if depth > 10 or not node.get('ref'):
            return node
        source = self.reusables.get(node['ref'])
        if not source: return node
        exp = copy.deepcopy(source)
        # Instance overrides
        if node.get('width') is not None: exp['width'] = node['width']
        if node.get('height') is not None: exp['height'] = node['height']
        if node.get('name'): exp['name'] = node['name']
        if node.get('x') is not None: exp['x'] = node['x']
        if node.get('y') is not None: exp['y'] = node['y']
        if node.get('fill') is not None: exp['fill'] = node['fill']
        # Descendant overrides
        for did, dov in node.get('descendants', {}).items():
            self._apply_descendant(exp, did, dov)
        # Recurse children refs
        exp['children'] = [self.expand_ref(c, depth+1) if c.get('ref') or c.get('type') == 'ref' else c
                           for c in exp.get('children', [])]
        # Nested ref
        if exp.get('type') == 'ref' and exp.get('ref'):
            return self.expand_ref(exp, depth+1)
        return exp

    def _apply_descendant(self, node, tid, override):
        if node.get('id') == tid:
            for k, v in override.items():
                if k not in ('id', 'children'):
                    node[k] = v
            return True
        for c in node.get('children', []):
            if self._apply_descendant(c, tid, override): return True
        return False


def get_padding(node):
    p = node.get('padding', {})
    if isinstance(p, dict):
        return p.get('top', 0) or 0, p.get('right', 0) or 0, p.get('bottom', 0) or 0, p.get('left', 0) or 0
    if isinstance(p, (int, float)):
        return p, p, p, p
    return 0, 0, 0, 0


def estimate_text_size(node):
    fs = node.get('fontSize', 0) or 0
    content = node.get('content', '')
    w = len(content) * fs * 0.6
    h = fs * 1.5
    return w, h


def estimate_size(node, parent_w=0, parent_h=0, doc=None):
    """Estimate node's actual pixel size given parent dimensions."""
    # Expand ref first
    if doc and (node.get('type') == 'ref' or node.get('ref')):
        node = doc.expand_ref(node)

    wm, wv = parse_dim(node.get('width'))
    hm, hv = parse_dim(node.get('height'))

    # Fixed
    w = wv if wm == 'fixed' else 0
    h = hv if hm == 'fixed' else 0

    # Fill: use parent size (minus padding would be more accurate but approximate)
    if wm == 'fill':
        w = parent_w if parent_w > 0 else wv  # fallback
    if hm == 'fill':
        h = parent_h if parent_h > 0 else hv

    # Text leaf
    ntype = node.get('type', '')
    if ntype == 'text':
        tw, th = estimate_text_size(node)
        if w == 0: w = tw
        if h == 0: h = th
        return w, h

    if ntype == 'icon_font':
        s = node.get('fontSize', 16) or 16
        if w == 0: w = s
        if h == 0: h = s
        return w, h

    # Fit: estimate from children
    if (wm == 'fit' and w == 0) or (hm == 'fit' and h == 0):
        cw, ch = estimate_content_size(node, w or parent_w, h or parent_h, doc)
        if wm == 'fit' and w == 0: w = cw
        if hm == 'fit' and h == 0: h = ch

    return w, h


def estimate_content_size(node, avail_w, avail_h, doc=None):
    children = node.get('children', [])
    if not children: return 0, 0
    layout = node.get('layout')
    pt, pr, pb, pl = get_padding(node)
    gap = node.get('gap', 0) or 0
    inner_w = avail_w - pl - pr
    inner_h = avail_h - pt - pb

    total_w, total_h, max_w, max_h = 0, 0, 0, 0
    for c in children:
        if not c.get('enabled', True) if 'enabled' in c else True:
            continue
        cw, ch = estimate_size(c, inner_w, inner_h, doc)
        if layout == 'horizontal':
            total_w += cw; max_h = max(max_h, ch)
        elif layout == 'vertical':
            max_w = max(max_w, cw); total_h += ch
        else:
            cx, cy = c.get('x', 0) or 0, c.get('y', 0) or 0
            max_w = max(max_w, cx + cw)
            max_h = max(max_h, cy + ch)

    n = len(children)
    tg = gap * (n - 1) if n > 1 else 0
    if layout == 'horizontal':
        return total_w + tg + pl + pr, max_h + pt + pb
    elif layout == 'vertical':
        return max_w + pl + pr, total_h + tg + pt + pb
    return max_w + pl + pr, max_h + pt + pb


class WidgetNode:
    """Simulated UMG widget node."""
    __slots__ = ('name', 'wtype', 'x', 'y', 'w', 'h', 'anchor_min', 'anchor_max',
                 'offset_min', 'offset_max', 'halign', 'valign', 'children', 'issues')

    def __init__(self, name='', wtype=''):
        self.name = name; self.wtype = wtype
        self.x = self.y = self.w = self.h = 0
        self.anchor_min = (0, 0); self.anchor_max = (0, 0)
        self.offset_min = (0, 0); self.offset_max = (0, 0)
        self.halign = 'Fill'; self.valign = 'Fill'
        self.children = []; self.issues = []


def convert_node(pen_node, doc, parent_w, parent_h, parent_layout=None):
    """Convert a pen node to a simulated UMG WidgetNode."""
    # Expand ref
    if pen_node.get('type') == 'ref' or pen_node.get('ref'):
        pen_node = doc.expand_ref(pen_node)

    ntype = pen_node.get('type', 'frame')
    name = pen_node.get('name', '?')
    w_est, h_est = estimate_size(pen_node, parent_w, parent_h, doc)

    wn = WidgetNode(name, ntype)
    wn.x = pen_node.get('x', 0) or 0
    wn.y = pen_node.get('y', 0) or 0
    wn.w = w_est
    wn.h = h_est

    # Check for issues
    wm, wv = parse_dim(pen_node.get('width'))
    hm, hv = parse_dim(pen_node.get('height'))
    if wn.w == 0 and wm != 'fill':
        wn.issues.append(f'width=0 (mode={wm})')
    if wn.h == 0 and hm != 'fill':
        wn.issues.append(f'height=0 (mode={hm})')

    # Calculate anchors for CanvasPanel children (absolute layout)
    if parent_layout is None or parent_layout == 'none':
        if parent_w > 0 and parent_h > 0:
            # Normalized position
            nx = wn.x / parent_w
            ny = wn.y / parent_h
            nw = wn.w / parent_w if wn.w > 0 else 0
            nh = wn.h / parent_h if wn.h > 0 else 0
            # Simple anchor calculation (mirrors LayoutCalculator)
            wn.anchor_min = (round(nx, 4), round(ny, 4))
            wn.anchor_max = (round(nx + nw, 4), round(ny + nh, 4))

    # Recurse children
    layout = pen_node.get('layout')
    pt, pr, pb, pl = get_padding(pen_node)
    inner_w = wn.w - pl - pr if wn.w > 0 else parent_w
    inner_h = wn.h - pt - pb if wn.h > 0 else parent_h

    for c in pen_node.get('children', []):
        child_wn = convert_node(c, doc, inner_w, inner_h, layout)
        wn.children.append(child_wn)

    return wn


def print_widget_tree(wn, indent=0, max_depth=6):
    if indent > max_depth: return
    prefix = '  ' * indent
    issues = f' !! {"; ".join(wn.issues)}' if wn.issues else ''
    print(f'{prefix}{wn.name} ({wn.wtype}) @({wn.x:.0f},{wn.y:.0f}) {wn.w:.0f}x{wn.h:.0f}{issues}')
    for c in wn.children:
        print_widget_tree(c, indent + 1, max_depth)


def collect_issues(wn, path=''):
    issues = []
    full_path = f'{path}/{wn.name}' if path else wn.name
    for iss in wn.issues:
        issues.append((full_path, iss))
    for c in wn.children:
        issues.extend(collect_issues(c, full_path))
    return issues


def render_widget_tree(wn, frame_w, frame_h, output_path):
    """Render widget tree as a PNG layout diagram."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('PIL not available, skipping render')
        return

    scale = min(1.0, 800 / max(frame_w, 1), 800 / max(frame_h, 1))
    iw, ih = max(int(frame_w * scale), 1), max(int(frame_h * scale), 1)
    img = Image.new('RGB', (iw, ih), (20, 20, 20))
    draw = ImageDraw.Draw(img)

    COLORS = [(66,133,244),(234,67,53),(251,188,4),(52,168,83),
              (171,71,188),(255,112,67),(0,172,193),(124,179,66)]
    idx = [0]

    def draw_node(n, ox, oy):
        x = int((ox + n.x) * scale)
        y = int((oy + n.y) * scale)
        w = int(n.w * scale)
        h = int(n.h * scale)
        c = COLORS[idx[0] % len(COLORS)]
        idx[0] += 1
        if w > 1 and h > 1:
            draw.rectangle([x, y, x+w-1, y+h-1], outline=c, width=1)
            if n.issues:
                draw.rectangle([x+1, y+1, x+w-2, y+h-2], outline=(255,0,0), width=1)
        for child in n.children:
            draw_node(child, ox + n.x, oy + n.y)

    draw_node(wn, 0, 0)
    img.save(output_path)
    print(f'Layout rendered: {output_path} ({iw}x{ih})')


def main():
    parser = argparse.ArgumentParser(description='Simulate Design2UMG conversion')
    parser.add_argument('pen_file', help='Path to .pen file')
    parser.add_argument('--frame', default=None, help='Target frame ID (default: all target frames)')
    parser.add_argument('--screenshot', default=None, help='UE Designer screenshot for comparison')
    parser.add_argument('--render', action='store_true', help='Render layout diagram')
    parser.add_argument('--depth', type=int, default=6, help='Max tree depth to print')
    args = parser.parse_args()

    with open(args.pen_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    doc = PenDoc(data)
    print(f'Loaded: {len(doc.reusables)} reusables, {len(doc.variables)} variables')

    # Find target frames
    if args.frame:
        frames = [(args.frame, args.frame)]
    else:
        # Auto-detect: top-level non-reusable frames
        frames = []
        for page in doc.pages:
            for c in page.get('children', []):
                if not c.get('reusable') and c.get('type') == 'frame':
                    frames.append((c['id'], c.get('name', c['id'])))

    for fid, fname in frames:
        node = doc.find_node(fid)
        if not node:
            print(f'\n[{fname}] NOT FOUND')
            continue

        fw, fh = estimate_size(node, 1920, 1080, doc)
        print(f'\n{"="*60}')
        print(f'Frame: {fname} ({fid}) design={node.get("width")}x{node.get("height")} estimated={fw:.0f}x{fh:.0f}')
        print(f'Layout: {node.get("layout")}, Theme: {node.get("theme")}')
        print(f'{"="*60}')

        # Expand and convert
        expanded = doc.expand_ref(copy.deepcopy(node)) if node.get('ref') else copy.deepcopy(node)
        # Expand all child refs recursively
        def expand_all(n):
            n['children'] = [doc.expand_ref(c) if c.get('ref') or c.get('type') == 'ref' else c
                             for c in n.get('children', [])]
            for c in n.get('children', []):
                expand_all(c)
        expand_all(expanded)

        wn = convert_node(expanded, doc, fw, fh)
        print_widget_tree(wn, max_depth=args.depth)

        issues = collect_issues(wn)
        if issues:
            print(f'\n  Issues ({len(issues)}):')
            for path, iss in issues:
                print(f'    {path}: {iss}')

        if args.render:
            out = Path(args.pen_file).parent / f'sim_layout_{fname.replace(" ", "_")}.png'
            render_widget_tree(wn, fw, fh, str(out))

    # Screenshot comparison
    if args.screenshot:
        print(f'\nScreenshot comparison with: {args.screenshot}')
        print('(Visual diff not yet implemented - use rendered layout for manual comparison)')


if __name__ == '__main__':
    main()
