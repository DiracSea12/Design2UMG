// Design2UMG Figma Exporter - Plugin Code
// Traverses selected frame, serializes to REST API compatible JSON, exports images

// ── Types ──

interface RestColor { r: number; g: number; b: number; a: number }
interface RestRect { x: number; y: number; width: number; height: number }
type RestTransform = [[number, number, number], [number, number, number]];
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
  gradientTransform?: RestTransform;
  imageTransform?: RestTransform;
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
  counterAxisSpacing?: number;
  counterAxisAlignContent?: string;
  layoutWrap?: string;
  layoutPositioning?: string;
  itemReverseZIndex?: boolean;
  strokesIncludedInLayout?: boolean;
  minWidth?: number;
  maxWidth?: number;
  minHeight?: number;
  maxHeight?: number;
  gridRowCount?: number;
  gridColumnCount?: number;
  gridRowGap?: number;
  gridColumnGap?: number;
  gridRowsSizing?: string;
  gridColumnsSizing?: string;
  gridChildHorizontalAlign?: string;
  gridChildVerticalAlign?: string;
  gridRowSpan?: number;
  gridColumnSpan?: number;
  gridColumnAnchorIndex?: number;
  gridRowAnchorIndex?: number;
  layoutAlign?: string;
  layoutGrow?: number;
  layoutSizingHorizontal?: string;
  layoutSizingVertical?: string;
  individualStrokeWeights?: { top: number; right: number; bottom: number; left: number };
  strokeWeight?: number;
  strokeAlign?: string;
  componentId?: string;
  componentSetId?: string;
  mainComponentKey?: string;
  componentProperties?: Record<string, any>;
  variantProperties?: Record<string, any>;
  overrides?: any[];
  exposedInstances?: Array<{ id: string; name: string; type: string }>;
  isExposedInstance?: boolean;
  style?: Record<string, any>;
  characters?: string;
  textAutoResize?: string;
  children?: RestNode[];
  constraints?: { vertical: string; horizontal: string };
  size?: { x: number; y: number };
  fillGeometry?: any[];
  strokeGeometry?: any[];
  isMask?: boolean;
  maskType?: string;
  boundVariables?: Record<string, any>;
  componentPropertyReferences?: Record<string, any>;
  explicitVariableModes?: Record<string, any>;
  resolvedVariableModes?: Record<string, any>;
  textTruncation?: string;
  maxLines?: number | null;
  design2umgRenderFallbackImageRef?: string;
  design2umgRenderFallbackScope?: string;
  design2umgRenderFallbackSource?: string;
  design2umgRenderFallbackReason?: string;
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

function toRestTransform(transform: Transform | number[][] | undefined): RestTransform | undefined {
  if (!transform || transform.length !== 2) return undefined;
  const row0 = transform[0];
  const row1 = transform[1];
  if (row0.length !== 3 || row1.length !== 3) return undefined;
  return [
    [row0[0], row0[1], row0[2]],
    [row1[0], row1[1], row1[2]],
  ];
}

function invertTransform(transform: RestTransform): RestTransform | null {
  const [[a, b, tx], [c, d, ty]] = transform;
  const det = a * d - b * c;
  if (Math.abs(det) < 1e-6) return null;
  return [
    [d / det, -b / det, (b * ty - d * tx) / det],
    [-c / det, a / det, (c * tx - a * ty) / det],
  ];
}

function applyTransform(transform: RestTransform, x: number, y: number): { x: number; y: number } {
  return {
    x: transform[0][0] * x + transform[0][1] * y + transform[0][2],
    y: transform[1][0] * x + transform[1][1] * y + transform[1][2],
  };
}

function gradientTransformToHandlePositions(transform: RestTransform): Array<{ x: number; y: number }> {
  const inverse = invertTransform(transform);
  if (!inverse) return [];
  return [
    applyTransform(inverse, 0, 0.5),
    applyTransform(inverse, 1, 0.5),
    applyTransform(inverse, 0, 1),
  ];
}

function cloneJsonSafe<T>(value: T): T | undefined {
  if (value === undefined) return undefined;
  try {
    return JSON.parse(JSON.stringify(value));
  } catch {
    return undefined;
  }
}

function documentationLinksToRest(links: readonly DocumentationLink[] | undefined): Array<{ uri: string }> | undefined {
  if (!links || links.length === 0) return undefined;
  return links.map(link => ({ uri: link.uri }));
}

function exposedInstancesToRest(instances: readonly BaseNode[] | undefined): Array<{ id: string; name: string; type: string }> | undefined {
  if (!instances || instances.length === 0) return undefined;
  return instances.map(instance => ({
    id: instance.id,
    name: (instance as any).name || instance.id,
    type: instance.type,
  }));
}

function setIfDefined<T extends object, K extends keyof any>(target: T, key: K, value: any): void {
  if (value !== undefined && value !== null) {
    (target as any)[key] = value;
  }
}

function makeRenderFallbackKey(nodeId: string): string {
  const sanitized = (nodeId || '')
    .replace(/[^a-zA-Z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
  return `render_${sanitized || 'node'}`;
}

function indexRestNodes(node: RestNode | null | undefined, out = new Map<string, RestNode>()): Map<string, RestNode> {
  if (!node) return out;
  out.set(node.id, node);
  for (const child of node.children || []) {
    indexRestNodes(child, out);
  }
  return out;
}

function hasVisibleChildNodes(node: SceneNode): boolean {
  if (!('children' in node)) return false;
  return (node.children || []).some(child => child.visible !== false);
}

function hasNodeReactions(node: SceneNode): boolean {
  return 'reactions' in node && Array.isArray((node as any).reactions) && (node as any).reactions.length > 0;
}

function isExcludedRenderFallbackNodeType(node: SceneNode): boolean {
  return node.type === 'TEXT'
    || node.type === 'INSTANCE'
    || node.type === 'COMPONENT'
    || node.type === 'COMPONENT_SET'
    || node.type === 'SECTION';
}

function getRenderFallbackReasons(node: SceneNode): string[] {
  if (!('fills' in node) || node.fills === figma.mixed) {
    return [];
  }

  const reasons = new Set<string>();
  for (const fill of node.fills as readonly Paint[]) {
    if (fill.type !== 'IMAGE' || fill.visible === false) {
      continue;
    }

    const imagePaint = fill as ImagePaint;
    if (imagePaint.filters && Object.keys(imagePaint.filters).length > 0) {
      reasons.add('image_filters');
    }

    const blendMode = String(imagePaint.blendMode || 'NORMAL').toUpperCase();
    if (blendMode !== 'NORMAL' && blendMode !== 'PASS_THROUGH') {
      reasons.add(`image_blend_${blendMode.toLowerCase()}`);
    }
  }

  return Array.from(reasons.values());
}

function shouldExportNodeRenderFallback(node: SceneNode): { use: boolean; reason: string } {
  if (isExcludedRenderFallbackNodeType(node)) {
    return { use: false, reason: '' };
  }
  if (hasVisibleChildNodes(node)) {
    return { use: false, reason: '' };
  }
  if (hasNodeReactions(node)) {
    return { use: false, reason: '' };
  }

  const reasons = getRenderFallbackReasons(node);
  if (reasons.length === 0) {
    return { use: false, reason: '' };
  }

  return {
    use: true,
    reason: reasons.join('+'),
  };
}

function attachRenderFallbackMetadata(restNode: RestNode | undefined, imageRef: string, reason: string): void {
  if (!restNode) return;
  restNode.design2umgRenderFallbackImageRef = imageRef;
  restNode.design2umgRenderFallbackScope = 'full';
  restNode.design2umgRenderFallbackSource = 'figma_render_png';
  restNode.design2umgRenderFallbackReason = reason;
}

// ── Fill Serialization ──

function serializePaint(paint: Paint): any {
  const out: any = {
    type: paint.type,
    visible: paint.visible !== false,
    opacity: paint.opacity ?? 1,
    blendMode: mapBlendMode(paint.blendMode || 'NORMAL'),
  };

  setIfDefined(out, 'boundVariables', cloneJsonSafe((paint as any).boundVariables));

  if (paint.type === 'SOLID' && 'color' in paint) {
    out.color = colorToRest(paint.color, paint.opacity);
  }

  if (paint.type === 'GRADIENT_LINEAR' || paint.type === 'GRADIENT_RADIAL' ||
      paint.type === 'GRADIENT_ANGULAR' || paint.type === 'GRADIENT_DIAMOND') {
    const gp = paint as GradientPaint;
    const gradientTransform = toRestTransform(gp.gradientTransform);
    if (gradientTransform) {
      out.gradientTransform = gradientTransform;
      out.gradientHandlePositions = gradientTransformToHandlePositions(gradientTransform);
    } else {
      out.gradientHandlePositions = [];
    }
    out.gradientStops = gp.gradientStops.map(s => ({
      color: colorToRest(s.color),
      position: s.position,
    }));
  }

  if (paint.type === 'IMAGE' && 'imageHash' in paint) {
    // Plugin API: imageHash → REST API: imageRef
    const imagePaint = paint as ImagePaint;
    out.imageRef = imagePaint.imageHash || '';
    out.scaleMode = imagePaint.scaleMode || 'FILL';
    setIfDefined(out, 'rotation', (imagePaint as any).rotation);
    setIfDefined(out, 'filters', cloneJsonSafe((imagePaint as any).filters));
    if (out.scaleMode === 'TILE') {
      setIfDefined(out, 'scalingFactor', (imagePaint as any).scalingFactor);
    }
    const imageTransform = toRestTransform(imagePaint.imageTransform);
    if (out.scaleMode === 'CROP' && imageTransform) {
      out.imageTransform = imageTransform;
    }
  }

  return out;
}

// ── Effect Serialization ──

function serializeEffect(effect: Effect): any {
  const out: any = {
    type: effect.type,
    visible: effect.visible !== false,
  };
  setIfDefined(out, 'blendMode', mapBlendMode((effect as any).blendMode || 'NORMAL'));
  setIfDefined(out, 'opacity', (effect as any).opacity);
  if ('radius' in effect) out.radius = effect.radius;
  if ('offset' in effect) out.offset = effect.offset;
  if ('color' in effect) out.color = colorToRest((effect as any).color);
  if ('spread' in effect) out.spread = (effect as any).spread;
  setIfDefined(out, 'showShadowBehindNode', (effect as any).showShadowBehindNode);
  setIfDefined(out, 'boundVariables', cloneJsonSafe((effect as any).boundVariables));
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
  style.textCase = node.textCase !== figma.mixed ? node.textCase : 'ORIGINAL';
  style.textDecoration = node.textDecoration !== figma.mixed ? node.textDecoration : 'NONE';
  style.letterSpacing = node.letterSpacing !== figma.mixed
    ? (node.letterSpacing as LetterSpacing).value || 0 : 0;
  style.paragraphSpacing = (node as any).paragraphSpacing ?? 0;
  style.paragraphIndent = (node as any).paragraphIndent ?? 0;
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
  if ((node as any).isMask && 'maskType' in node) {
    setIfDefined(out, 'maskType', (node as any).maskType);
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
  setIfDefined(out, 'boundVariables', cloneJsonSafe((node as any).boundVariables));
  setIfDefined(out, 'componentPropertyReferences', cloneJsonSafe((node as any).componentPropertyReferences));
  setIfDefined(out, 'explicitVariableModes', cloneJsonSafe((node as any).explicitVariableModes));
  setIfDefined(out, 'resolvedVariableModes', cloneJsonSafe((node as any).resolvedVariableModes));

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
    setIfDefined(out, 'counterAxisSpacing', n.counterAxisSpacing);
    setIfDefined(out, 'counterAxisAlignContent', n.counterAxisAlignContent);
    setIfDefined(out, 'layoutWrap', n.layoutWrap);
    setIfDefined(out, 'strokesIncludedInLayout', n.strokesIncludedInLayout);
    setIfDefined(out, 'itemReverseZIndex', n.itemReverseZIndex);
    setIfDefined(out, 'gridRowCount', n.gridRowCount);
    setIfDefined(out, 'gridColumnCount', n.gridColumnCount);
    setIfDefined(out, 'gridRowGap', n.gridRowGap);
    setIfDefined(out, 'gridColumnGap', n.gridColumnGap);
    setIfDefined(out, 'gridRowsSizing', n.gridRowsSizing);
    setIfDefined(out, 'gridColumnsSizing', n.gridColumnsSizing);
    setIfDefined(out, 'gridChildHorizontalAlign', n.gridChildHorizontalAlign);
    setIfDefined(out, 'gridChildVerticalAlign', n.gridChildVerticalAlign);
  }

  // Layout child properties
  if ('layoutAlign' in node) out.layoutAlign = (node as any).layoutAlign;
  if ('layoutGrow' in node) out.layoutGrow = (node as any).layoutGrow;
  if ('layoutSizingHorizontal' in node) out.layoutSizingHorizontal = (node as any).layoutSizingHorizontal;
  if ('layoutSizingVertical' in node) out.layoutSizingVertical = (node as any).layoutSizingVertical;
  if ('layoutPositioning' in node) out.layoutPositioning = (node as any).layoutPositioning;
  if ('minWidth' in node) setIfDefined(out, 'minWidth', (node as any).minWidth);
  if ('maxWidth' in node) setIfDefined(out, 'maxWidth', (node as any).maxWidth);
  if ('minHeight' in node) setIfDefined(out, 'minHeight', (node as any).minHeight);
  if ('maxHeight' in node) setIfDefined(out, 'maxHeight', (node as any).maxHeight);
  if ('gridRowSpan' in node) setIfDefined(out, 'gridRowSpan', (node as any).gridRowSpan);
  if ('gridColumnSpan' in node) setIfDefined(out, 'gridColumnSpan', (node as any).gridColumnSpan);
  if ('gridColumnAnchorIndex' in node) setIfDefined(out, 'gridColumnAnchorIndex', (node as any).gridColumnAnchorIndex);
  if ('gridRowAnchorIndex' in node) setIfDefined(out, 'gridRowAnchorIndex', (node as any).gridRowAnchorIndex);

  // Component reference
  if (node.type === 'INSTANCE') {
    const instanceNode = node as InstanceNode;
    if (instanceNode.mainComponent) {
      out.componentId = instanceNode.mainComponent.id;
      setIfDefined(out, 'mainComponentKey', (instanceNode.mainComponent as any).key);
      const componentSet = instanceNode.mainComponent.parent;
      if (componentSet && componentSet.type === 'COMPONENT_SET') {
        out.componentSetId = componentSet.id;
      }
    }
    setIfDefined(out, 'componentProperties', cloneJsonSafe((instanceNode as any).componentProperties));
    setIfDefined(out, 'variantProperties', cloneJsonSafe((instanceNode as any).variantProperties));
    setIfDefined(out, 'overrides', cloneJsonSafe((instanceNode as any).overrides));
    setIfDefined(out, 'exposedInstances', exposedInstancesToRest((instanceNode as any).exposedInstances));
    setIfDefined(out, 'isExposedInstance', (instanceNode as any).isExposedInstance);
  }

  // Text
  if (node.type === 'TEXT') {
    const tn = node as TextNode;
    out.characters = tn.characters;
    out.style = buildTextStyle(tn);
    out.textAutoResize = tn.textAutoResize || 'NONE';
    setIfDefined(out, 'textTruncation', (tn as any).textTruncation);
    setIfDefined(out, 'maxLines', (tn as any).maxLines);
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

function findNodeById(node: any, targetId: string): RestNode | null {
  if (!node || typeof node !== 'object') return null;
  if (node.id === targetId) return node as RestNode;
  const children = Array.isArray((node as any).children) ? (node as any).children : [];
  for (const child of children) {
    const match = findNodeById(child, targetId);
    if (match) return match;
  }
  return null;
}

function unwrapRestExportResponse(response: any, targetId: string): RestNode | null {
  if (!response || typeof response !== 'object') return null;

  const directDocument = (response as any).document;
  if (directDocument && typeof directDocument === 'object') {
    const directMatch = findNodeById(directDocument, targetId);
    return directMatch || (directDocument as RestNode);
  }

  const nodes = (response as any).nodes;
  if (nodes && typeof nodes === 'object') {
    const candidateKeys = [targetId, targetId.replace(/:/g, '-')];
    for (const key of candidateKeys) {
      const entry = (nodes as any)[key];
      if (entry && typeof entry === 'object' && (entry as any).document) {
        return (entry as any).document as RestNode;
      }
    }

    for (const entry of Object.values(nodes as Record<string, any>)) {
      if (entry && typeof entry === 'object' && (entry as any).document) {
        const match = findNodeById((entry as any).document, targetId);
        if (match) return match;
      }
    }
  }

  return null;
}

async function exportOfficialRestNode(node: SceneNode): Promise<RestNode | null> {
  if (!('exportAsync' in node)) return null;
  try {
    const response = await (node as any).exportAsync({ format: 'JSON_REST_V1' });
    return unwrapRestExportResponse(response, node.id);
  } catch (error) {
    console.warn(`JSON_REST_V1 export failed for ${node.name}:`, error);
    return null;
  }
}

// ── Image Collection ──

interface ImageEntry { hash: string; bytes: Uint8Array }
interface RestComponentMeta {
  key?: string;
  name: string;
  description: string;
  remote: boolean;
  componentSetId?: string;
  componentPropertyDefinitions?: Record<string, any>;
  variantGroupProperties?: Record<string, any>;
  documentationLinks?: Array<{ uri: string }>;
}

function isVariantComponent(component: ComponentNode): boolean {
  return !!(component.parent && component.parent.type === 'COMPONENT_SET');
}

function getExportableComponentPropertyDefinitions(component: ComponentNode): Record<string, any> | undefined {
  if (isVariantComponent(component)) {
    return undefined;
  }
  return cloneJsonSafe((component as any).componentPropertyDefinitions);
}

function collectReferencedComponents(
  node: SceneNode,
  components: Record<string, RestComponentMeta>,
  componentSets: Record<string, RestComponentMeta>
): void {
  const registerComponent = (component: ComponentNode) => {
    components[component.id] = {
      key: (component as any).key,
      name: component.name,
      description: component.description || '',
      remote: !!(component as any).remote,
      componentSetId: component.parent && component.parent.type === 'COMPONENT_SET' ? component.parent.id : undefined,
      componentPropertyDefinitions: getExportableComponentPropertyDefinitions(component),
      documentationLinks: documentationLinksToRest((component as any).documentationLinks),
    };

    if (component.parent && component.parent.type === 'COMPONENT_SET') {
      const componentSet = component.parent;
      componentSets[componentSet.id] = {
        key: (componentSet as any).key,
        name: componentSet.name,
        description: componentSet.description || '',
        remote: !!(componentSet as any).remote,
        componentPropertyDefinitions: cloneJsonSafe((componentSet as any).componentPropertyDefinitions),
        variantGroupProperties: cloneJsonSafe((componentSet as any).variantGroupProperties),
        documentationLinks: documentationLinksToRest((componentSet as any).documentationLinks),
      };
    }
  };

  if (node.type === 'COMPONENT') {
    registerComponent(node);
  } else if (node.type === 'COMPONENT_SET') {
    componentSets[node.id] = {
      key: (node as any).key,
      name: node.name,
      description: node.description || '',
      remote: !!(node as any).remote,
      componentPropertyDefinitions: cloneJsonSafe((node as any).componentPropertyDefinitions),
      variantGroupProperties: cloneJsonSafe((node as any).variantGroupProperties),
      documentationLinks: documentationLinksToRest((node as any).documentationLinks),
    };
  } else if (node.type === 'INSTANCE' && node.mainComponent) {
    registerComponent(node.mainComponent);
  }

  if ('children' in node) {
    for (const child of node.children) {
      collectReferencedComponents(child, components, componentSets);
    }
  }
}

async function collectImages(
  node: SceneNode,
  images: Map<string, Uint8Array>,
  scale: number,
  onProgress: (text: string) => void,
  restNodesById?: Map<string, RestNode>
): Promise<void> {
  const renderFallback = shouldExportNodeRenderFallback(node);
  if (renderFallback.use && 'exportAsync' in node) {
    const imageRef = makeRenderFallbackKey(node.id);
    if (!images.has(imageRef)) {
      try {
        const bytes = await (node as any).exportAsync({
          format: 'PNG',
          constraint: { type: 'SCALE', value: scale },
        });
        images.set(imageRef, bytes);
        onProgress(`Render: ${node.name}`);
      } catch (e) {
        console.warn(`Failed to export render fallback ${node.name}:`, e);
      }
    }

    if (images.has(imageRef)) {
      attachRenderFallbackMetadata(restNodesById?.get(node.id), imageRef, renderFallback.reason);
    }
  }

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

  if ('strokes' in node) {
    for (const stroke of ((node as any).strokes as readonly Paint[] | undefined) || []) {
      if (stroke.type === 'IMAGE' && 'imageHash' in stroke) {
        const hash = (stroke as any).imageHash as string;
        if (hash && !images.has(hash)) {
          try {
            const img = figma.getImageByHash(hash);
            if (img) {
              const bytes = await img.getBytesAsync();
              images.set(hash, bytes);
              onProgress(`Stroke Image: ${hash.substring(0, 8)}...`);
            }
          } catch (e) {
            console.warn(`Failed to get stroke image ${hash}:`, e);
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
      await collectImages(child, images, scale, onProgress, restNodesById);
    }
  }
}

const testApi = {
  attachRenderFallbackMetadata,
  indexRestNodes,
  makeRenderFallbackKey,
  serializePaint,
  serializeEffect,
  buildTextStyle,
  serializeNode,
  unwrapRestExportResponse,
  collectReferencedComponents,
  collectImages,
  shouldExportNodeRenderFallback,
};

if (typeof globalThis !== 'undefined') {
  (globalThis as any).__design2umgExporterTestApi = testApi;
}

// ── Main Export Flow ──
if (typeof figma !== 'undefined') {
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
      figma.ui.postMessage({ type: 'progress', percent: 10, text: 'Serializing nodes...' });
      const serialized = await exportOfficialRestNode(root) || serializeNode(root);
      if (!serialized) {
        figma.ui.postMessage({ type: 'error', text: 'Failed to serialize selected node.' });
        return;
      }

      const components: Record<string, RestComponentMeta> = {};
      const componentSets: Record<string, RestComponentMeta> = {};
      collectReferencedComponents(root, components, componentSets);

      const document: any = {
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
      if (Object.keys(components).length > 0) {
        document.components = components;
      }
      if (Object.keys(componentSets).length > 0) {
        document.componentSets = componentSets;
      }

      figma.ui.postMessage({ type: 'progress', percent: 30, text: 'Collecting images...' });
      const images = new Map<string, Uint8Array>();
      const restNodesById = indexRestNodes(serialized);
      await collectImages(root, images, scale, (text) => {
        figma.ui.postMessage({ type: 'progress', percent: 50, text });
      }, restNodesById);

      figma.ui.postMessage({ type: 'progress', percent: 80, text: 'Preparing download...' });

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
}
