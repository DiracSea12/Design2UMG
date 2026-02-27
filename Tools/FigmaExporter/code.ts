// Design2UMG Figma Exporter - Plugin Code
// Traverses selected frame, serializes to REST API compatible JSON, exports images

// ── Types ──

interface RestColor { r: number; g: number; b: number; a: number }
interface RestRect { x: number; y: number; width: number; height: number }
interface RestNode {
  id: string;
  name: string;
  type: string;
  visible: boolean;
  absoluteBoundingBox?: RestRect;
  absoluteRenderBounds?: RestRect | null;
  relativeTransform?: number[][];
  rotation?: number;
  fills?: any[];
  strokes?: any[];
  effects?: any[];
  cornerRadius?: number;
  rectangleCornerRadii?: number[];
  opacity?: number;
  blendMode?: string;
  clipsContent?: boolean;
  layoutMode?: string;
  primaryAxisSizingMode?: string;
  counterAxisSizingMode?: string;
  primaryAxisAlignItems?: string;
  counterAxisAlignItems?: string;
  paddingLeft?: number;
  paddingRight?: number;
  paddingTop?: number;
  paddingBottom?: number;
  itemSpacing?: number;
  layoutAlign?: string;
  layoutGrow?: number;
  layoutSizingHorizontal?: string;
  layoutSizingVertical?: string;
  individualStrokeWeights?: { top: number; right: number; bottom: number; left: number };
  strokeWeight?: number;
  strokeAlign?: string;
  componentId?: string;
  style?: Record<string, any>;
  characters?: string;
  textAutoResize?: string;
  children?: RestNode[];
  constraints?: { vertical: string; horizontal: string };
  size?: { x: number; y: number };
  fillGeometry?: any[];
  strokeGeometry?: any[];
  isMask?: boolean;
}

// ── Helpers ──

function colorToRest(c: RGB | RGBA, opacity = 1): RestColor {
  return { r: c.r, g: c.g, b: c.b, a: 'a' in c ? c.a * opacity : opacity };
}

function rectToRest(bb: { x: number; y: number; width: number; height: number }): RestRect {
  return { x: bb.x, y: bb.y, width: bb.width, height: bb.height };
}

function mapBlendMode(bm: string): string {
  return bm.replace(/_/g, '-');
}

// ── Fill Serialization ──

function serializePaint(paint: Paint): any {
  const out: any = {
    type: paint.type,
    visible: paint.visible !== false,
    opacity: paint.opacity ?? 1,
    blendMode: mapBlendMode(paint.blendMode || 'NORMAL'),
  };

  if (paint.type === 'SOLID' && 'color' in paint) {
    out.color = colorToRest(paint.color, paint.opacity);
  }

  if (paint.type === 'GRADIENT_LINEAR' || paint.type === 'GRADIENT_RADIAL' ||
      paint.type === 'GRADIENT_ANGULAR' || paint.type === 'GRADIENT_DIAMOND') {
    const gp = paint as GradientPaint;
    out.gradientHandlePositions = gp.gradientTransform
      ? [{ x: gp.gradientTransform[0][2], y: gp.gradientTransform[1][2] }]
      : [];
    out.gradientStops = gp.gradientStops.map(s => ({
      color: colorToRest(s.color),
      position: s.position,
    }));
  }

  if (paint.type === 'IMAGE' && 'imageHash' in paint) {
    // Plugin API: imageHash → REST API: imageRef
    out.imageRef = (paint as any).imageHash || '';
    out.scaleMode = (paint as any).scaleMode || 'FILL';
  }

  return out;
}

// ── Effect Serialization ──

function serializeEffect(effect: Effect): any {
  const out: any = {
    type: effect.type,
    visible: effect.visible !== false,
  };
  if ('radius' in effect) out.radius = effect.radius;
  if ('offset' in effect) out.offset = effect.offset;
  if ('color' in effect) out.color = colorToRest((effect as any).color);
  if ('spread' in effect) out.spread = (effect as any).spread;
  return out;
}

// ── Text Style Aggregation ──

function buildTextStyle(node: TextNode): Record<string, any> {
  const style: Record<string, any> = {};
  style.fontFamily = node.fontName !== figma.mixed ? (node.fontName as FontName).family : 'Inter';
  style.fontPostScriptName = '';
  style.fontWeight = node.fontWeight !== figma.mixed ? node.fontWeight : 400;
  style.fontSize = node.fontSize !== figma.mixed ? node.fontSize : 14;
  style.textAlignHorizontal = node.textAlignHorizontal || 'LEFT';
  style.textAlignVertical = node.textAlignVertical || 'TOP';
  style.letterSpacing = node.letterSpacing !== figma.mixed
    ? (node.letterSpacing as LetterSpacing).value || 0 : 0;
  if (node.lineHeight !== figma.mixed) {
    const lh = node.lineHeight as LineHeight;
    style.lineHeightUnit = lh.unit === 'PIXELS' ? 'PIXELS' : lh.unit === 'PERCENT' ? 'FONT_SIZE_%' : 'AUTO';
    style.lineHeightPx = lh.unit === 'PIXELS' ? lh.value : 0;
  } else {
    style.lineHeightUnit = 'AUTO';
    style.lineHeightPx = 0;
  }
  return style;
}

// ── Node Serialization ──

function serializeNode(node: SceneNode): RestNode | null {
  if (!node.visible && node.type !== 'COMPONENT' && node.type !== 'INSTANCE') return null;

  const out: RestNode = {
    id: node.id,
    name: node.name,
    type: node.type,
    visible: node.visible,
  };

  // absoluteBoundingBox — Plugin API provides this directly
  if ('absoluteBoundingBox' in node && node.absoluteBoundingBox) {
    out.absoluteBoundingBox = rectToRest(node.absoluteBoundingBox);
  }

  // absoluteRenderBounds — includes stroke/effect expansion
  if ('absoluteRenderBounds' in node) {
    const rb = (node as any).absoluteRenderBounds;
    out.absoluteRenderBounds = rb ? rectToRest(rb) : null;
  }

  // relativeTransform — used for flip detection
  if ('relativeTransform' in node) {
    const t = (node as any).relativeTransform as Transform;
    out.relativeTransform = [[t[0][0], t[0][1], t[0][2]], [t[1][0], t[1][1], t[1][2]]];
  }

  // rotation
  if ('rotation' in node) {
    out.rotation = (node as any).rotation;
  }

  // isMask
  if ('isMask' in node) {
    out.isMask = (node as any).isMask;
  }

  // Size
  if ('width' in node) {
    out.size = { x: (node as any).width, y: (node as any).height };
  }

  // Fills & Strokes
  if ('fills' in node && node.fills !== figma.mixed) {
    out.fills = (node.fills as readonly Paint[]).map(serializePaint);
  }
  if ('strokes' in node) {
    out.strokes = (node.strokes as readonly Paint[]).map(serializePaint);
  }

  // Geometry paths (fillGeometry / strokeGeometry)
  if ('fillGeometry' in node) {
    const fg = (node as any).fillGeometry as VectorPaths;
    if (fg && fg.length > 0) {
      out.fillGeometry = fg.map((p: VectorPath) => ({ path: p.data, windingRule: p.windingRule }));
    }
  }
  if ('strokeGeometry' in node) {
    const sg = (node as any).strokeGeometry as VectorPaths;
    if (sg && sg.length > 0) {
      out.strokeGeometry = sg.map((p: VectorPath) => ({ path: p.data, windingRule: p.windingRule }));
    }
  }

  // Stroke weight
  if ('strokeWeight' in node) {
    out.strokeWeight = (node as any).strokeWeight !== figma.mixed ? (node as any).strokeWeight : 1;
    out.strokeAlign = (node as any).strokeAlign || 'INSIDE';
  }

  // Individual stroke weights → aggregate to object
  if ('strokeTopWeight' in node) {
    const n = node as any;
    out.individualStrokeWeights = {
      top: n.strokeTopWeight ?? 0,
      right: n.strokeRightWeight ?? 0,
      bottom: n.strokeBottomWeight ?? 0,
      left: n.strokeLeftWeight ?? 0,
    };
  }

  // Effects
  if ('effects' in node) {
    out.effects = (node.effects as readonly Effect[]).map(serializeEffect);
  }

  // Corner radius
  if ('cornerRadius' in node) {
    const cr = (node as any).cornerRadius;
    if (cr !== figma.mixed) out.cornerRadius = cr;
  }
  if ('topLeftRadius' in node) {
    const n = node as any;
    out.rectangleCornerRadii = [n.topLeftRadius, n.topRightRadius, n.bottomRightRadius, n.bottomLeftRadius];
  }

  // Opacity & blend
  if ('opacity' in node) out.opacity = node.opacity;
  if ('blendMode' in node) out.blendMode = mapBlendMode(node.blendMode);

  // Clips
  if ('clipsContent' in node) out.clipsContent = (node as any).clipsContent;

  // Constraints
  if ('constraints' in node) {
    const c = (node as any).constraints;
    out.constraints = { vertical: c.vertical, horizontal: c.horizontal };
  }

  // Auto Layout
  if ('layoutMode' in node && (node as any).layoutMode !== 'NONE') {
    const n = node as any;
    out.layoutMode = n.layoutMode;
    out.primaryAxisSizingMode = n.primaryAxisSizingMode;
    out.counterAxisSizingMode = n.counterAxisSizingMode;
    out.primaryAxisAlignItems = n.primaryAxisAlignItems;
    out.counterAxisAlignItems = n.counterAxisAlignItems;
    out.paddingLeft = n.paddingLeft;
    out.paddingRight = n.paddingRight;
    out.paddingTop = n.paddingTop;
    out.paddingBottom = n.paddingBottom;
    out.itemSpacing = n.itemSpacing;
  }

  // Layout child properties
  if ('layoutAlign' in node) out.layoutAlign = (node as any).layoutAlign;
  if ('layoutGrow' in node) out.layoutGrow = (node as any).layoutGrow;
  if ('layoutSizingHorizontal' in node) out.layoutSizingHorizontal = (node as any).layoutSizingHorizontal;
  if ('layoutSizingVertical' in node) out.layoutSizingVertical = (node as any).layoutSizingVertical;

  // Component reference
  if (node.type === 'INSTANCE' && node.mainComponent) {
    out.componentId = node.mainComponent.id;
  }

  // Text
  if (node.type === 'TEXT') {
    const tn = node as TextNode;
    out.characters = tn.characters;
    out.style = buildTextStyle(tn);
    out.textAutoResize = tn.textAutoResize || 'NONE';
  }

  // Vector nodes: add synthetic IMAGE fill pointing to vec_* export
  const vectorTypes = ['VECTOR', 'BOOLEAN_OPERATION', 'STAR', 'LINE', 'ELLIPSE', 'POLYGON'];
  if (vectorTypes.includes(node.type)) {
    const vecKey = `vec_${node.id.replace(/[:/]/g, '_')}`;
    if (!out.fills) out.fills = [];
    out.fills.push({
      type: 'IMAGE',
      visible: true,
      opacity: 1,
      blendMode: 'NORMAL',
      imageRef: vecKey,
      scaleMode: 'FILL',
    });
  }

  // Children
  if ('children' in node) {
    const children: RestNode[] = [];
    for (const child of (node as any).children) {
      const serialized = serializeNode(child);
      if (serialized) children.push(serialized);
    }
    out.children = children;
  }

  return out;
}

// ── Image Collection ──

interface ImageEntry { hash: string; bytes: Uint8Array }

async function collectImages(
  node: SceneNode,
  images: Map<string, Uint8Array>,
  scale: number,
  onProgress: (text: string) => void
): Promise<void> {
  // Image fills
  if ('fills' in node && node.fills !== figma.mixed) {
    for (const fill of node.fills as readonly Paint[]) {
      if (fill.type === 'IMAGE' && 'imageHash' in fill) {
        const hash = (fill as any).imageHash as string;
        if (hash && !images.has(hash)) {
          try {
            const img = figma.getImageByHash(hash);
            if (img) {
              const bytes = await img.getBytesAsync();
              images.set(hash, bytes);
              onProgress(`Image: ${hash.substring(0, 8)}...`);
            }
          } catch (e) {
            console.warn(`Failed to get image ${hash}:`, e);
          }
        }
      }
    }
  }

  // Vector nodes → export as PNG
  const vectorTypes = ['VECTOR', 'BOOLEAN_OPERATION', 'STAR', 'LINE', 'ELLIPSE', 'POLYGON'];
  if (vectorTypes.includes(node.type) && 'exportAsync' in node) {
    const vecKey = `vec_${node.id.replace(/[:/]/g, '_')}`;
    if (!images.has(vecKey)) {
      try {
        const bytes = await (node as any).exportAsync({
          format: 'PNG',
          constraint: { type: 'SCALE', value: scale },
        });
        images.set(vecKey, bytes);
        onProgress(`Vector: ${node.name}`);
      } catch (e) {
        console.warn(`Failed to export vector ${node.name}:`, e);
      }
    }
  }

  // Recurse children
  if ('children' in node) {
    for (const child of (node as any).children) {
      await collectImages(child, images, scale, onProgress);
    }
  }
}

// ── Main Export Flow ──

figma.showUI(__html__, { width: 360, height: 200 });

figma.ui.onmessage = async (msg: any) => {
  if (msg.type !== 'export') return;

  const selection = figma.currentPage.selection;
  if (selection.length === 0) {
    figma.ui.postMessage({ type: 'error', text: 'No frame selected. Please select a frame first.' });
    return;
  }

  const root = selection[0];
  if (root.type !== 'FRAME' && root.type !== 'COMPONENT' && root.type !== 'INSTANCE') {
    figma.ui.postMessage({ type: 'error', text: 'Please select a Frame, Component, or Instance.' });
    return;
  }

  const scale = msg.scale || 2;

  try {
    // Step 1: Serialize node tree
    figma.ui.postMessage({ type: 'progress', percent: 10, text: 'Serializing nodes...' });
    const serialized = serializeNode(root);
    if (!serialized) {
      figma.ui.postMessage({ type: 'error', text: 'Failed to serialize selected node.' });
      return;
    }

    // Wrap in REST API document structure
    const document = {
      document: {
        id: '0:0',
        name: figma.root.name,
        type: 'DOCUMENT',
        children: [{
          id: figma.currentPage.id,
          name: figma.currentPage.name,
          type: 'CANVAS',
          children: [serialized],
        }],
      },
      name: figma.root.name,
      version: '0',
      schemaVersion: 0,
    };

    // Step 2: Collect images
    figma.ui.postMessage({ type: 'progress', percent: 30, text: 'Collecting images...' });
    const images = new Map<string, Uint8Array>();
    await collectImages(root, images, scale, (text) => {
      figma.ui.postMessage({ type: 'progress', percent: 50, text });
    });

    // Step 3: Send to UI for saving
    figma.ui.postMessage({ type: 'progress', percent: 80, text: 'Preparing download...' });

    // Convert images to transferable format
    const imageEntries: Array<{ name: string; data: number[] }> = [];
    for (const [key, bytes] of images) {
      imageEntries.push({ name: key, data: Array.from(bytes) });
    }

    figma.ui.postMessage({
      type: 'done',
      text: `Exported ${images.size} image(s). Saving...`,
      json: JSON.stringify(document, null, 2),
      images: imageEntries,
      exportPath: msg.exportPath,
    });

  } catch (e: any) {
    figma.ui.postMessage({ type: 'error', text: `Export failed: ${e.message || e}` });
  }
};
