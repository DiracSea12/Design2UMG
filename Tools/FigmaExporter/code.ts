/// <reference types="@figma/plugin-typings" />
// Design2UMG Figma Exporter - Plugin Code
// Traverses selected frame, serializes to REST API compatible JSON, exports images

// ── Types ──

interface RestColor { r: number; g: number; b: number; a: number }
interface RestRect { x: number; y: number; width: number; height: number }
type RestTransform = [[number, number, number], [number, number, number]];
interface RestGradientStop { color: RestColor; position: number }
interface RestPaint {
  [key: string]: any;
  type: Paint['type'];
  visible?: boolean;
  opacity?: number;
  blendMode?: string;
  boundVariables?: SolidPaint['boundVariables'];
  color?: RestColor;
  gradientTransform?: RestTransform;
  gradientHandlePositions?: Array<{ x: number; y: number }>;
  gradientStops?: RestGradientStop[];
  imageRef?: string | null;
  scaleMode?: ImagePaint['scaleMode'];
  rotation?: number;
  filters?: ImageFilters;
  scalingFactor?: number;
  imageTransform?: RestTransform;
}
interface RestEffect {
  [key: string]: any;
  type: Effect['type'];
  visible?: boolean;
  blendMode?: string;
  opacity?: number;
  radius?: number;
  offset?: Vector;
  color?: RestColor;
  spread?: number;
  showShadowBehindNode?: boolean;
  boundVariables?: DropShadowEffect['boundVariables'];
}
interface RestVectorPath { path: string; windingRule: VectorPath['windingRule'] }
interface RestComponentPropertyReferences {
  visible?: string;
  characters?: string;
  mainComponent?: string;
}
interface RestNode {
  [key: string]: any;
  id: string;
  name: string;
  type: string;
  visible?: boolean;
  absoluteBoundingBox?: RestRect;
  absoluteRenderBounds?: RestRect | null;
  relativeTransform?: number[][];
  rotation?: number;
  fills?: RestPaint[];
  strokes?: RestPaint[];
  effects?: RestEffect[];
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
  componentSetKey?: string;
  mainComponentKey?: string;
  componentProperties?: ComponentProperties;
  variantProperties?: NonNullable<InstanceNode['variantProperties']>;
  overrides?: InstanceNode['overrides'];
  exposedInstances?: Array<{ id: string; name: string; type: string }>;
  isExposedInstance?: boolean;
  style?: Record<string, any>;
  characters?: string;
  characterStyleOverrides?: number[];
  styleOverrideTable?: Record<string, Record<string, any>>;
  textAutoResize?: string;
  children?: RestNode[];
  constraints?: { vertical: string; horizontal: string };
  size?: { x: number; y: number };
  fillGeometry?: RestVectorPath[];
  strokeGeometry?: RestVectorPath[];
  isMask?: boolean;
  maskType?: string;
  boundVariables?: Record<string, any>;
  componentPropertyReferences?: RestComponentPropertyReferences;
  explicitVariableModes?: Record<string, any>;
  resolvedVariableModes?: Record<string, any>;
  textTruncation?: string;
  maxLines?: number | null;
  interactions?: readonly Reaction[];
}

interface RestExportNodeEnvelope { document?: RestNode | null }
interface RestExportResponse {
  document?: RestNode | null;
  nodes?: Record<string, RestExportNodeEnvelope | null>;
}

// ── Helpers ──

function colorToRest(c: RGB | RGBA, opacity = 1): RestColor {
  return { r: c.r, g: c.g, b: c.b, a: 'a' in c ? c.a * opacity : opacity };
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

function multiplyTransforms(left: RestTransform, right: RestTransform): RestTransform {
  return [
    [left[0][0] * right[0][0] + left[0][1] * right[1][0],
      left[0][0] * right[0][1] + left[0][1] * right[1][1],
      left[0][0] * right[0][2] + left[0][1] * right[1][2] + left[0][2]],
    [left[1][0] * right[0][0] + left[1][1] * right[1][0],
      left[1][0] * right[0][1] + left[1][1] * right[1][1],
      left[1][0] * right[0][2] + left[1][1] * right[1][2] + left[1][2]],
  ];
}

function gradientTransformToHandlePositions(transform: RestTransform, type: GradientPaint['type']): Array<{ x: number; y: number }> {
  const inverse = invertTransform(transform);
  if (!inverse) return [];
  return [
    applyTransform(inverse, type === 'GRADIENT_LINEAR' ? 0 : 0.5, 0.5),
    applyTransform(inverse, 1, 0.5),
    applyTransform(inverse, type === 'GRADIENT_LINEAR' ? 0 : 0.5, 1),
  ];
}

function documentationLinksToRest(links: readonly DocumentationLink[] | undefined): Array<{ uri: string }> | undefined {
  if (!links || links.length === 0) return undefined;
  return links.map(link => ({ uri: link.uri }));
}

function setIfDefined<T extends object, K extends keyof T>(target: T, key: K, value: T[K] | null | undefined): void {
  if (value !== undefined && value !== null) {
    target[key] = value;
  }
}

function indexRestNodes(node: RestNode | null | undefined, out = new Map<string, RestNode>()): Map<string, RestNode> {
  if (!node) return out;
  out.set(node.id, node);
  for (const child of node.children || []) {
    indexRestNodes(child, out);
  }
  return out;
}

// ── Fill Serialization ──

function serializePaint(paint: Paint): RestPaint {
  // Paints and effects are data records, unlike SceneNode host objects.
  const out = copySourceValue(paint) as RestPaint;
  out.visible = paint.visible !== false;
  out.opacity = paint.opacity ?? 1;
  out.blendMode = mapBlendMode(paint.blendMode || 'NORMAL');
  if (paint.type === 'SOLID') out.color = colorToRest(paint.color);
  if ('gradientTransform' in paint) {
    const transform = toRestTransform(paint.gradientTransform);
    if (transform) out.gradientHandlePositions = gradientTransformToHandlePositions(transform, paint.type);
  }
  if ('gradientStops' in paint) {
    out.gradientStops = paint.gradientStops.map(stop => ({ ...copySourceValue(stop), color: colorToRest(stop.color) }));
  }
  if (paint.type === 'IMAGE') {
    out.imageRef = paint.imageHash;
    delete out.imageHash;
  }
  return out;
}

function serializeEffect(effect: Effect): RestEffect {
  const out = copySourceValue(effect) as RestEffect;
  out.visible = effect.visible !== false;
  if ('blendMode' in effect) out.blendMode = mapBlendMode(effect.blendMode);
  return out;
}

// ── Source Geometry ──

function copyGeometryPaths(node: SceneNode, out: RestNode): void {
  if ('fillGeometry' in node) {
    out.fillGeometry = node.fillGeometry.map(p => ({ path: p.data, windingRule: p.windingRule }));
  }
  if ('strokeGeometry' in node) {
    out.strokeGeometry = node.strokeGeometry.map(p => ({ path: p.data, windingRule: p.windingRule }));
  }
}

function retainRestGeometryPaths(node: SceneNode, restNodesById: Map<string, RestNode>): void {
  const out = restNodesById.get(node.id);
  if (out) {
    copyGeometryPaths(node, out);
    if ('width' in node && 'height' in node) out.size = { x: node.width, y: node.height };
    if (!out.relativeTransform && 'relativeTransform' in node) {
      const absolute = toRestTransform(node.absoluteTransform);
      if (!absolute) throw new Error(`Source absolute transform is unavailable for ${node.id}`);
      const parent = node.parent;
      if (parent && 'absoluteTransform' in parent) {
        const parentAbsolute = toRestTransform(parent.absoluteTransform);
        const inverse = parentAbsolute && invertTransform(parentAbsolute);
        if (!inverse) throw new Error(`Parent absolute transform is unavailable for ${node.id}`);
        out.relativeTransform = multiplyTransforms(inverse, absolute);
      } else {
        out.relativeTransform = absolute;
      }
    }
  }
  if ('children' in node) {
    for (const child of node.children) retainRestGeometryPaths(child, restNodesById);
  }
}

function findNodeById(node: unknown, targetId: string): RestNode | null {
  if (!node || typeof node !== 'object') return null;
  if ((node as Partial<RestNode>).id === targetId) return node as RestNode;
  const children = Array.isArray((node as Partial<RestNode>).children) ? (node as Partial<RestNode>).children! : [];
  for (const child of children) {
    const match = findNodeById(child, targetId);
    if (match) return match;
  }
  return null;
}

function unwrapRestExportResponse(response: unknown, targetId: string): RestNode | null {
  if (!response || typeof response !== 'object') return null;

  const directDocument = (response as RestExportResponse).document;
  if (directDocument && typeof directDocument === 'object') {
    const directMatch = findNodeById(directDocument, targetId);
    return directMatch || (directDocument as RestNode);
  }

  const nodes = (response as RestExportResponse).nodes;
  if (nodes && typeof nodes === 'object') {
    const candidateKeys = [targetId, targetId.replace(/:/g, '-')];
    for (const key of candidateKeys) {
      const entry = nodes[key];
      if (entry && typeof entry === 'object' && entry.document) {
        return entry.document;
      }
    }

    for (const entry of Object.values(nodes)) {
      if (entry && typeof entry === 'object' && entry.document) {
        const match = findNodeById(entry.document, targetId);
        if (match) return match;
      }
    }
  }

  return null;
}

/** Read data properties explicitly: JSON.stringify on a SceneNode drops host getters. */
function sourcePropertyNames(value: object): Set<string> {
  const names = new Set<string>();
  for (let current: object | null = value; current && current !== Object.prototype;
    current = Object.getPrototypeOf(current)) {
    for (const key of Object.getOwnPropertyNames(current)) {
      if (key !== 'constructor') names.add(key);
    }
  }
  return names;
}

function copySourceValue(value: any, ancestors = new Set<object>()): any {
  if (value === undefined || value === null || typeof value === 'string' || typeof value === 'boolean') return value;
  if (typeof value === 'number') {
    return Number.isFinite(value) ? value : { type: 'NON_FINITE_NUMBER', value: String(value) };
  }
  if (value === figma.mixed) return { type: 'MIXED' };
  if (typeof value !== 'object') throw new Error(`Source value is not JSON data (${typeof value})`);
  // Node references carry identity; the actual node trees are exported separately.
  if (typeof value.getPluginData === 'function' && 'parent' in value) {
    return { id: value.id, name: value.name, type: value.type };
  }
  if (ancestors.has(value)) throw new Error('Source data contains a circular reference');
  ancestors.add(value);
  try {
    if (Array.isArray(value)) return value.map(entry => copySourceValue(entry, ancestors));
    const result: Record<string, any> = {};
    for (const key of sourcePropertyNames(value)) {
      const entry = value[key];
      if (entry !== undefined && typeof entry !== 'function') result[key] = copySourceValue(entry, ancestors);
    }
    return result;
  } finally {
    ancestors.delete(value);
  }
}

const motionFields = ['animationStyles', 'animations', 'manualKeyframeTracks', 'timelines'] as const;
interface ExportDiagnostic { nodeId: string; field: string; message: string }

function capturePluginData(source: BaseNode, includeMotion = false): { data: Record<string, any>; diagnostics: ExportDiagnostic[] } {
  const raw: Record<string, any> = {};
  const diagnostics: ExportDiagnostic[] = [];
  const keys = sourcePropertyNames(source);
  // Motion properties may be present in the API contract but hidden by account gates.
  if (includeMotion) for (const field of motionFields) keys.add(field);
  for (const field of keys) {
    if (field === 'children' || field === 'parent') continue;
    if (field === 'componentPropertyDefinitions' && source.type === 'COMPONENT' && isVariantComponent(source)) continue;
    try {
      const value = (source as any)[field];
      if (typeof value === 'function') continue;
      if (value === undefined) {
        if ((motionFields as readonly string[]).includes(field)) {
          diagnostics.push({ nodeId: source.id, field, message: 'Motion API is unavailable; data was not read' });
        }
        continue;
      }
      raw[field] = copySourceValue(value);
    } catch (error) {
      diagnostics.push({ nodeId: source.id, field, message: error instanceof Error ? error.message : String(error) });
    }
  }
  return { data: raw, diagnostics };
}

function pluginNodeTypeToRest(type: SceneNode['type']): string {
  return type === 'POLYGON' ? 'REGULAR_POLYGON' : type;
}

async function mergePluginSourceNode(source: SceneNode, out: RestNode): Promise<void> {
  if (out.id !== source.id || out.type !== pluginNodeTypeToRest(source.type)) {
    throw new Error(`REST export changed source identity ${source.id} (${source.type}) to ${out.id} (${out.type})`);
  }
  const { data: raw, diagnostics } = capturePluginData(source, true);
  out.visible = source.visible;
  if (source.type === 'TEXT') {
    raw.styledTextSegments = copySourceValue(source.getStyledTextSegments([
      'fontSize', 'fontName', 'fontWeight', 'fontStyle', 'textDecoration', 'textDecorationStyle',
      'textDecorationOffset', 'textDecorationThickness', 'textDecorationColor', 'textDecorationSkipInk',
      'textCase', 'lineHeight', 'letterSpacing', 'fills', 'textStyleId', 'fillStyleId', 'listOptions',
      'listSpacing', 'indentation', 'paragraphIndent', 'paragraphSpacing', 'textWrapStyle', 'hyperlink',
      'openTypeFeatures', 'boundVariables', 'textStyleOverrides',
    ]));
  }
  out.design2umgPluginData = raw;
  if (diagnostics.length) out.design2umgExportDiagnostics = diagnostics;
  for (const field of motionFields) {
    if (Object.prototype.hasOwnProperty.call(raw, field)) out[field] = raw[field];
    else delete out[field];
  }
  out.design2umgMotionTimeUnit = 'SECONDS';
  if ('reactions' in source) out.interactions = pluginReactionsToRest(source.reactions);
  if ('fills' in source && source.fills !== figma.mixed) {
    out.fills = source.fills.map((paint, index) => ({ ...out.fills?.[index], ...serializePaint(paint) }));
  }
  if ('strokes' in source) {
    out.strokes = source.strokes.map((paint, index) => ({ ...out.strokes?.[index], ...serializePaint(paint) }));
  }
  if ('effects' in source) {
    out.effects = source.effects.map((effect, index) => ({ ...out.effects?.[index], ...serializeEffect(effect) }));
  }
  if ('children' in source) {
    const sourceChildren = Array.from(source.children);
    const officialChildren = out.children || [];
    const describe = (children: readonly { id: string; name?: string; type: string; visible?: boolean }[]) =>
      children.map(child => ({ id: child.id, name: child.name ?? null, type: child.type, visible: child.visible ?? null }));
    const context = `on ${source.id} (${source.name}); sourceChildren=${JSON.stringify(describe(sourceChildren))}; `
      + `restChildren=${JSON.stringify(describe(officialChildren))}`;
    const sourceIds = new Set(sourceChildren.map(child => child.id));
    const childrenById = new Map(officialChildren.map(child => [child.id, child]));
    if (childrenById.size !== officialChildren.length || officialChildren.some(child => !sourceIds.has(child.id))) {
      throw new Error(`REST export changed source children or order ${context}`);
    }
    const children: RestNode[] = [];
    for (const child of sourceChildren) {
      try {
        // Every missing subtree must come from that exact source child's official export.
        const document = childrenById.get(child.id) || await readOfficialRestNode(child);
        await mergePluginSourceNode(child, document);
        children.push(document);
      } catch (error) {
        throw new Error(`REST child ${child.id} (${child.name}) ${context}: ${error instanceof Error ? error.message : String(error)}`);
      }
    }
    if (JSON.stringify(children.map(child => child.id)) !== JSON.stringify(sourceChildren.map(child => child.id))) {
      throw new Error(`REST export changed source children or order ${context}`);
    }
    out.children = children;
  }
}

async function readOfficialRestNode(node: SceneNode): Promise<RestNode> {
  const response = await node.exportAsync({ format: 'JSON_REST_V1' });
  const document = unwrapRestExportResponse(response, node.id);
  if (!document || document.id !== node.id || document.type !== pluginNodeTypeToRest(node.type)) {
    throw new Error(`Official JSON did not contain source identity ${node.id} (${node.type}); `
      + `received ${document?.id ?? 'null'} (${document?.type ?? 'null'})`);
  }
  return document;
}

async function exportOfficialRestNode(node: SceneNode): Promise<RestNode> {
  try {
    const document = await readOfficialRestNode(node);
    await mergePluginSourceNode(node, document);
    const nodes = indexRestNodes(document);
    validatePrototypeInteractions(node, nodes);
    retainRestGeometryPaths(node, nodes);
    return document;
  } catch (error) {
    throw new Error(`JSON_REST_V1 ${node.id} (${node.name}): ${error instanceof Error ? error.message : String(error)}`);
  }
}

function pluginReactionsToRest(reactions: readonly Reaction[]): Reaction[] {
  if (!Array.isArray(reactions) || reactions.some(reaction => !Array.isArray(reaction.actions))) {
    throw new Error('Source interaction has no action list');
  }
  const result = copySourceValue(reactions) as Reaction[];
  function convert(actions: readonly any[]): void {
    for (const action of actions) {
      if (!action) continue;
      if (action.transition && typeof action.transition.duration === 'number') {
        action.transition.duration *= 1000;
      }
      for (const block of action.conditionalBlocks || []) convert(block.actions || []);
    }
  }
  for (const reaction of result) {
    // The normalized contract uses actions; the deprecated source mirror stays in the raw snapshot.
    delete reaction.action;
    if (reaction.trigger && reaction.trigger.type === 'AFTER_TIMEOUT') {
      (reaction.trigger as { timeout: number }).timeout *= 1000;
    }
    convert(reaction.actions || []);
  }
  return result;
}

function validatePrototypeInteractions(source: SceneNode, nodes: Map<string, RestNode>): void {
  function interactionJson(interactions: readonly any[]): string {
    function actions(entries: readonly any[]): void {
      for (const action of entries) {
        if (!action) continue;
        if (action.type === 'NODE') {
          if (typeof action.navigation !== 'string' || !action.navigation) {
            throw new Error(`NODE action on ${source.id} is missing required navigation`);
          }
        }
        for (const block of action.conditionalBlocks || []) actions(block.actions || []);
      }
    }
    for (const interaction of interactions) {
      if (interaction.trigger) actions(interaction.actions || []);
    }
    return JSON.stringify(interactions, (_key, value) => {
      if (!value || typeof value !== 'object' || Array.isArray(value)) return value;
      const ordered: Record<string, unknown> = {};
      for (const key of Object.keys(value).sort()) ordered[key] = value[key];
      return ordered;
    });
  }
  if ('reactions' in source && interactionJson(pluginReactionsToRest(source.reactions))
    !== interactionJson(nodes.get(source.id)?.interactions || [])) {
    throw new Error(`REST export lost prototype destinations or changed interaction data on ${source.id}`);
  }
  if ('children' in source) {
    for (const child of source.children) validatePrototypeInteractions(child, nodes);
  }
}

// ── Image Collection ──

interface ImageEntry { hash: string; bytes: Uint8Array }
interface RestComponentMeta {
  key: string;
  name: string;
  description: string;
  remote: boolean;
  componentSetId?: string;
  componentPropertyDefinitions?: ComponentPropertyDefinitions;
  variantGroupProperties?: ComponentSetNode['variantGroupProperties'];
  documentationLinks?: Array<{ uri: string }>;
  design2umgExportDiagnostics?: ExportDiagnostic[];
}

function isVariantComponent(component: ComponentNode): boolean {
  return !!(component.parent && component.parent.type === 'COMPONENT_SET');
}

function componentMetadata(node: ComponentNode | ComponentSetNode): RestComponentMeta {
  const metadata: RestComponentMeta = {
    key: node.key, name: node.name, description: node.description || '', remote: !!node.remote,
    documentationLinks: documentationLinksToRest(node.documentationLinks),
  };
  if (node.type === 'COMPONENT' && node.parent?.type === 'COMPONENT_SET') metadata.componentSetId = node.parent.id;
  const fields = node.type === 'COMPONENT_SET' ? ['componentPropertyDefinitions', 'variantGroupProperties']
    : isVariantComponent(node) ? [] : ['componentPropertyDefinitions'];
  const diagnostics: ExportDiagnostic[] = [];
  for (const field of fields) {
    try {
      (metadata as any)[field] = copySourceValue((node as any)[field]);
    } catch (error) {
      diagnostics.push({ nodeId: node.id, field, message: error instanceof Error ? error.message : String(error) });
    }
  }
  if (diagnostics.length) metadata.design2umgExportDiagnostics = diagnostics;
  return metadata;
}

function registerReferencedComponent(
  component: ComponentNode,
  components: Record<string, RestComponentMeta>,
  componentSets: Record<string, RestComponentMeta>
): void {
  components[component.id] = componentMetadata(component);
  if (component.parent?.type === 'COMPONENT_SET') {
    componentSets[component.parent.id] = componentMetadata(component.parent);
  }
}

function collectReferencedComponents(
  node: SceneNode,
  components: Record<string, RestComponentMeta>,
  componentSets: Record<string, RestComponentMeta>
): void {
  if (node.type === 'COMPONENT') {
    registerReferencedComponent(node, components, componentSets);
  } else if (node.type === 'COMPONENT_SET') {
    componentSets[node.id] = componentMetadata(node);
  } else if (node.type === 'INSTANCE' && node.mainComponent) {
    registerReferencedComponent(node.mainComponent, components, componentSets);
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
  onProgress: (text: string) => void
): Promise<void> {
  // Image fills
  if ('fills' in node && node.fills !== figma.mixed) {
    for (const fill of node.fills as readonly Paint[]) {
      if (fill.type === 'IMAGE' && 'imageHash' in fill) {
        const hash = fill.imageHash;
        if (hash && !images.has(hash)) {
          try {
            const img = figma.getImageByHash(hash);
            if (!img) throw new Error('Image is unavailable');
            const bytes = await img.getBytesAsync();
            if (bytes.length === 0) throw new Error('Image is empty');
            images.set(hash, bytes);
            onProgress(`Image: ${hash.substring(0, 8)}...`);
          } catch (e) {
            throw new Error(`Failed to get image ${hash}: ${e instanceof Error ? e.message : String(e)}`);
          }
        }
      }
    }
  }

  if ('strokes' in node) {
    for (const stroke of node.strokes || []) {
      if (stroke.type === 'IMAGE' && 'imageHash' in stroke) {
        const hash = stroke.imageHash;
        if (hash && !images.has(hash)) {
          try {
            const img = figma.getImageByHash(hash);
            if (!img) throw new Error('Image is unavailable');
            const bytes = await img.getBytesAsync();
            if (bytes.length === 0) throw new Error('Image is empty');
            images.set(hash, bytes);
            onProgress(`Stroke Image: ${hash.substring(0, 8)}...`);
          } catch (e) {
            throw new Error(`Failed to get stroke image ${hash}: ${e instanceof Error ? e.message : String(e)}`);
          }
        }
      }
    }
  }

  // Recurse children
  if ('children' in node) {
    for (const child of (node as any).children) {
      await collectImages(child, images, onProgress);
    }
  }
}

/** Export the selected root exactly as Figma renders it. This is the authoritative
 * reference image shipped beside the serialized design data. */
async function exportReferencePng(node: SceneNode, scale: number): Promise<Uint8Array> {
  if (!('exportAsync' in node)) {
    throw new Error('Selected node cannot be exported as a PNG reference.');
  }
  const bytes = await (node as any).exportAsync({
    format: 'PNG',
    constraint: { type: 'SCALE', value: scale },
  });
  if (!(bytes instanceof Uint8Array) || bytes.length === 0) {
    throw new Error('Figma returned an empty PNG reference.');
  }
  return bytes;
}

interface ComponentDependencyIdentity {
  id: string;
  key: string;
  componentSetId?: string;
  componentSetKey?: string;
}

interface ComponentDependencyBundle {
  schemaVersion: number;
  definitions: Array<ComponentDependencyIdentity & { document: RestNode }>;
  sourceNodes?: RestNode[];
  instances: Record<string, ComponentDependencyIdentity>;
  errors: Array<{ kind: string; nodeId: string; message: string }>;
}

interface RestDocumentNode {
  id: string;
  name: string;
  type: 'DOCUMENT';
  children: Array<{
    id: string;
    name: string;
    type: 'CANVAS';
    children: RestNode[];
  }>;
}

interface ExportDocumentPayload {
  document: RestDocumentNode;
  name: string;
  version: string;
  schemaVersion: number;
  design2umgPrototypeDurationUnit: 'MILLISECONDS';
  fileKey?: string;
  design2umgComponentDependencies: ComponentDependencyBundle;
  design2umgPrototypePages: RestNode[];
  components?: Record<string, RestComponentMeta>;
  componentSets?: Record<string, RestComponentMeta>;
}

interface ExportRequestMessage {
  type: string;
  scale?: number;
  rootNodeId?: string;
  sequence?: number;
  error?: string;
}

interface PreparedExport {
  warning: string;
  json: string;
  images: Map<string, Uint8Array>;
  reference: Uint8Array;
}

type ExportTransferMessage =
  | { type: 'export-start'; warning: string; fileCount: number }
  | { type: 'export-file'; name: string; kind: 'text' | 'binary'; length: number }
  | { type: 'export-text'; data: string }
  | { type: 'export-bytes'; data: Uint8Array }
  | { type: 'export-file-end' }
  | { type: 'export-complete' };

const exportChunkLength = 1024 * 1024;

async function transferExport(prepared: PreparedExport, send: (message: ExportTransferMessage) => Promise<void>): Promise<void> {
  await send({ type: 'export-start', warning: prepared.warning, fileCount: prepared.images.size + 2 });
  async function textFile(name: string, text: string): Promise<void> {
    await send({ type: 'export-file', name, kind: 'text', length: text.length });
    for (let start = 0; start < text.length;) {
      let end = Math.min(start + exportChunkLength, text.length);
      // Each UI TextEncoder call must receive both halves of a UTF-16 surrogate pair.
      if (end < text.length && text.charCodeAt(end - 1) >= 0xD800 && text.charCodeAt(end - 1) <= 0xDBFF
        && text.charCodeAt(end) >= 0xDC00 && text.charCodeAt(end) <= 0xDFFF) end--;
      await send({ type: 'export-text', data: text.slice(start, end) });
      start = end;
    }
    await send({ type: 'export-file-end' });
  }
  async function binaryFile(name: string, bytes: Uint8Array): Promise<void> {
    await send({ type: 'export-file', name, kind: 'binary', length: bytes.length });
    for (let start = 0; start < bytes.length; start += exportChunkLength) {
      await send({ type: 'export-bytes', data: bytes.length <= exportChunkLength
        ? bytes : bytes.subarray(start, Math.min(start + exportChunkLength, bytes.length)) });
    }
    await send({ type: 'export-file-end' });
  }
  await textFile('design.json', prepared.json);
  prepared.json = '';
  await binaryFile('reference.png', prepared.reference);
  prepared.reference = new Uint8Array(0);
  for (const [key, bytes] of prepared.images) {
    await binaryFile('images/' + key + '.png', bytes);
    prepared.images.delete(key);
  }
  await send({ type: 'export-complete' });
}

async function collectComponentDependencyImages(
  bundle: ComponentDependencyBundle, images: Map<string, Uint8Array>, pages: RestNode[] = []
): Promise<void> {
  const hashes = new Set<string>();
  function collect(value: any): void {
    if (!value || typeof value !== 'object') return;
    if (value.type === 'IMAGE') {
      const hash = value.imageRef ?? value.imageHash;
      if (typeof hash === 'string' && hash) hashes.add(hash);
    }
    if (value.type === 'VIDEO' && value.videoHash) {
      throw new Error(`Video resource ${value.videoHash} cannot be read by the current Plugin API`);
    }
    if (value.type === 'SHADER' && value.id) {
      throw new Error(`Shader resource ${value.id} has no readable shader source in the current Plugin API`);
    }
    for (const entry of Object.values(value)) collect(entry);
  }
  for (const definition of bundle.definitions) collect(definition.document);
  for (const node of bundle.sourceNodes || []) collect(node);
  for (const page of pages) collect(page);
  for (const hash of hashes) {
    if (images.has(hash)) continue;
    try {
      const image = figma.getImageByHash(hash);
      if (!image) throw new Error('Source image is unavailable');
      const bytes = await image.getBytesAsync();
      if (bytes.length === 0) throw new Error('Source image is empty');
      images.set(hash, bytes);
    } catch (error) {
      bundle.errors.push({ kind: 'component_image_unavailable', nodeId: hash,
        message: `${hash}: ${error instanceof Error ? error.message : String(error)}` });
    }
  }
}

// Component definitions are separate from the selected layout tree. Instance nodes
// retain their resolved appearance; definitions supply reusable defaults and dependencies.
async function collectComponentDependencies(
  root: SceneNode, pages: RestNode[] = [],
  components: Record<string, RestComponentMeta> = {}, componentSets: Record<string, RestComponentMeta> = {}
): Promise<ComponentDependencyBundle> {
  const result: ComponentDependencyBundle = {
    schemaVersion: 1, definitions: [], sourceNodes: [], instances: {}, errors: [],
  };
  const active = new Set<string>();
  const completed = new Set<string>();
  const runtimeIds = new Set<string>();
  const scanned = new Set<SceneNode>();
  const targets: Array<{ origin: SceneNode; destinationId: string; navigation: string; fileLocal: boolean }> = [];

  function collectTargets(node: SceneNode, fileLocal: boolean): void {
    if (scanned.has(node)) return;
    scanned.add(node);
    function actions(entries: readonly any[]): void {
      for (const action of entries) {
        if (!action) continue;
        for (const block of action.conditionalBlocks || []) actions(block.actions || []);
        if (action.type !== 'NODE') continue;
        const navigation = action.navigation;
        if (typeof navigation !== 'string' || !navigation) {
          throw new Error(`NODE action on ${node.id} is missing required navigation`);
        }
        if (!['CHANGE_TO', 'NAVIGATE', 'OVERLAY', 'SWAP', 'OPEN_OVERLAY', 'SWAP_OVERLAY'].includes(navigation)) continue;
        if (action.destinationId === null) continue;
        targets.push({ origin: node, destinationId: action.destinationId, navigation, fileLocal });
      }
    }
    if ('reactions' in node) {
      for (const reaction of node.reactions) {
        if (reaction.trigger) actions(reaction.actions || []);
      }
    }
    function paints(value: any): void {
      if (!value || typeof value !== 'object') return;
      if (value.type === 'PATTERN') {
        targets.push({ origin: node, destinationId: value.sourceNodeId, navigation: 'PATTERN', fileLocal });
      }
      for (const entry of Object.values(value)) paints(entry);
    }
    if ('fills' in node && node.fills !== figma.mixed) paints(node.fills);
    if ('strokes' in node) paints(node.strokes);
    if (node.type === 'TEXT' && typeof node.getStyledTextSegments === 'function') {
      paints(node.getStyledTextSegments(['fills']).map(segment => segment.fills));
    }
    for (const field of motionFields) {
      // Access restrictions are recorded by mergePluginSourceNode; they do not mean an empty animation.
      try { paints((node as any)[field]); } catch { /* unavailable Motion source */ }
    }
  }

  function identity(component: ComponentNode): ComponentDependencyIdentity {
    const set = component.parent?.type === 'COMPONENT_SET' ? component.parent : undefined;
    return { id: component.id, key: component.key, componentSetId: set?.id, componentSetKey: set?.key };
  }

  async function visitDefinition(component: ComponentNode): Promise<void> {
    const source = identity(component);
    // Unpublished local components still have a document-local node identity.
    const token = source.key || source.id;
    if (active.has(token)) {
      result.errors.push({ kind: 'component_cycle', nodeId: component.id,
        message: `Component dependency cycle at ${component.id} (${component.name})` });
      return;
    }
    if (completed.has(token)) return;
    active.add(token);
    try {
      if (!component.remote) registerReferencedComponent(component, components, componentSets);
      collectTargets(component, !component.remote);
      // Node ids are file-local. Remote definitions can contain the same node id
      // as another definition or the selected file, so resolve each tree separately.
      const instances: Record<string, ComponentDependencyIdentity> = {};
      await visitChildren(component, instances, !component.remote);
      const document = await exportOfficialRestNode(component);
      applyComponentIdentities(document, instances);
      result.definitions.push({ ...source, document });
    } catch (error) {
      result.errors.push({ kind: 'component_export_failed', nodeId: component.id,
        message: `${component.id} (${component.name}): ${error instanceof Error ? error.message : String(error)}` });
    } finally {
      active.delete(token);
      completed.add(token);
    }
  }

  async function visitChildren(node: SceneNode, instances: Record<string, ComponentDependencyIdentity>, fileLocal: boolean): Promise<void> {
    if ('children' in node) {
      for (const child of node.children) await visit(child, instances, fileLocal);
    }
  }

  async function visit(node: SceneNode, instances: Record<string, ComponentDependencyIdentity>, fileLocal: boolean): Promise<void> {
    if (instances === result.instances) runtimeIds.add(node.id);
    collectTargets(node, fileLocal);
    if (node.type === 'INSTANCE') {
      try {
        const main = await node.getMainComponentAsync();
        if (!main) throw new Error('Main component is missing or inaccessible');
        instances[node.id] = identity(main);
        await visitDefinition(main);
      } catch (error) {
        result.errors.push({ kind: 'component_unavailable', nodeId: node.id,
          message: `${node.id} (${node.name}): ${error instanceof Error ? error.message : String(error)}` });
      }
    } else if (node.type === 'COMPONENT') {
      await visitDefinition(node);
    }
    // Resolved instance children may contain a component swap absent from the main definition.
    await visitChildren(node, instances, fileLocal);
  }

  await visit(root, result.instances, true);
  for (let index = 0; index < targets.length; index++) {
    const { origin, destinationId, navigation, fileLocal } = targets[index];
    try {
      if (!destinationId) throw new Error(`${navigation} destination is missing`);
      if (fileLocal && navigation !== 'CHANGE_TO' && runtimeIds.has(destinationId)) continue;
      let target: BaseNode | null | undefined;
      if (navigation === 'CHANGE_TO') {
        let scope: BaseNode | null = origin;
        while (scope && scope.type !== 'INSTANCE' && scope.type !== 'COMPONENT_SET') scope = scope.parent;
        if (scope?.type === 'INSTANCE') {
          const main = await scope.getMainComponentAsync();
          if (!main || main.parent?.type !== 'COMPONENT_SET') {
            throw new Error(`CHANGE_TO destination ${destinationId} has no accessible source component set`);
          }
          scope = main.parent;
        }
        if (scope?.type === 'COMPONENT_SET') {
          target = scope.children.find(child => child.id === destinationId);
          if (!target) throw new Error(`CHANGE_TO destination ${destinationId} is unavailable in source component set ${scope.id}`);
        } else if (fileLocal && !scope) {
          target = await figma.getNodeByIdAsync(destinationId);
        } else {
          throw new Error(`CHANGE_TO destination ${destinationId} has no accessible source component set`);
        }
      } else if (fileLocal) {
        target = await figma.getNodeByIdAsync(destinationId);
      } else {
        throw new Error(`Remote ${navigation} destination ${destinationId} cannot be resolved in this file`);
      }
      if (!target || target.type === 'PAGE' || target.type === 'DOCUMENT'
        || (navigation !== 'PATTERN' && (navigation === 'CHANGE_TO' ? target.type !== 'COMPONENT'
          : target.type !== 'FRAME' && target.type !== 'INSTANCE' && target.type !== 'COMPONENT'))) {
        const actualTarget = target ? { id: target.id, type: target.type, name: target.name } : null;
        throw new Error(`${navigation} destination ${destinationId} is missing or has the wrong node type; `
          + `actualTarget=${JSON.stringify(actualTarget)}`);
      }
      if (target.type === 'COMPONENT' && (navigation === 'CHANGE_TO' || navigation === 'PATTERN')) {
        await visitDefinition(target);
      } else {
        await visit(target, result.instances, true);
        const document = await exportOfficialRestNode(target);
        applyComponentIdentities(document, result.instances);
        if (navigation === 'PATTERN') result.sourceNodes!.push(document);
        else pages.push(document);
      }
    } catch (error) {
      result.errors.push({ kind: 'prototype_destination_unavailable', nodeId: origin.id,
        message: `${origin.id}: ${error instanceof Error ? error.message : String(error)}` });
    }
  }
  return result;
}

function applyComponentIdentities(node: RestNode, identities: Record<string, ComponentDependencyIdentity>): void {
  const source = identities[node.id];
  if (node.type === 'INSTANCE' && source) {
    node.componentId = source.id;
    if (source.key) node.mainComponentKey = source.key;
    if (source.componentSetId) node.componentSetId = source.componentSetId;
    if (source.componentSetKey) node.componentSetKey = source.componentSetKey;
  }
  for (const child of node.children || []) applyComponentIdentities(child, identities);
}

/** Definitions used by source bindings are separate from the resolved layout values. */
async function collectSourceDataDependencies(trees: RestNode[]): Promise<Record<string, any>> {
  const result: Record<string, Record<string, any>> = { variables: {}, variableCollections: {}, styles: {} };
  const pending: Array<{ kind: string; id: string }> = [];
  const queued = new Set<string>();
  function enqueue(kind: string, id: unknown): void {
    if (typeof id !== 'string' || !id || queued.has(`${kind}:${id}`)) return;
    queued.add(`${kind}:${id}`);
    pending.push({ kind, id });
  }
  function scan(value: any): void {
    if (!value || typeof value !== 'object') return;
    if (value.type === 'VARIABLE_ALIAS') enqueue('variables', value.id);
    for (const [key, entry] of Object.entries(value)) {
      if (key === 'variableId') enqueue('variables', entry);
      if (key === 'variableCollectionId' || key === 'parentVariableCollectionId' || key === 'rootVariableCollectionId') {
        enqueue('variableCollections', entry);
      }
      if (key.endsWith('StyleId')) enqueue('styles', entry);
      if ((key === 'explicitVariableModes' || key === 'resolvedVariableModes') && entry && typeof entry === 'object') {
        for (const id of Object.keys(entry)) enqueue('variableCollections', id);
      }
      scan(entry);
    }
  }
  // REST style references use a different identity space from Plugin API style ids.
  for (const tree of trees) {
    for (const node of indexRestNodes(tree).values()) scan(node.design2umgPluginData);
  }
  for (let index = 0; index < pending.length; index++) {
    const { kind, id } = pending[index];
    const source = kind === 'variables' ? await figma.variables.getVariableByIdAsync(id)
      : kind === 'variableCollections' ? await figma.variables.getVariableCollectionByIdAsync(id)
      : await figma.getStyleByIdAsync(id);
    if (!source) throw new Error(`Source ${kind} ${id} is unavailable`);
    result[kind][id] = copySourceValue(source);
    scan(result[kind][id]);
  }
  return result;
}

// Source trees leave scope before the acknowledged UI transfer starts.
async function prepareExport(root: SceneNode, scale: number): Promise<PreparedExport> {
  let page: BaseNode | null = root.parent;
  while (page && page.type !== 'PAGE') page = page.parent;
  figma.ui.postMessage({ type: 'progress', percent: 10, text: 'Serializing nodes...' });
  const serialized = await exportOfficialRestNode(root);

  const components: Record<string, RestComponentMeta> = {};
  const componentSets: Record<string, RestComponentMeta> = {};
  collectReferencedComponents(root, components, componentSets);
  const prototypePages: RestNode[] = [];
  const componentDependencies = await collectComponentDependencies(root, prototypePages, components, componentSets);
  if (componentDependencies.errors.length > 0) {
    throw new Error(componentDependencies.errors.map(error => error.message).join('\n'));
  }
  applyComponentIdentities(serialized, componentDependencies.instances);

  const document: ExportDocumentPayload = {
    design2umgPrototypeDurationUnit: 'MILLISECONDS',
    document: {
      id: '0:0',
      name: figma.root.name,
      type: 'DOCUMENT',
      children: [{
        id: page?.id || figma.currentPage.id,
        name: page?.name || figma.currentPage.name,
        type: 'CANVAS',
        children: [serialized],
      }],
    },
    name: figma.root.name,
    version: '0',
    schemaVersion: 0,
    fileKey: figma.fileKey,
    design2umgComponentDependencies: componentDependencies,
    design2umgPrototypePages: prototypePages,
  };
  if (Object.keys(components).length > 0) {
    document.components = components;
  }
  if (Object.keys(componentSets).length > 0) {
    document.componentSets = componentSets;
  }

  const sourceTrees = [serialized, ...prototypePages, ...componentDependencies.definitions.map(entry => entry.document),
    ...(componentDependencies.sourceNodes || [])];
  const documentSource = capturePluginData(figma.root);
  const pageSource = page ? capturePluginData(page) : undefined;
  (document.document as any).design2umgPluginData = documentSource.data;
  if (pageSource) (document.document.children[0] as any).design2umgPluginData = pageSource.data;
  const sourceDependencies = await collectSourceDataDependencies(sourceTrees);
  (document as any).design2umgSourceDataDependencies = sourceDependencies;
  figma.ui.postMessage({ type: 'progress', percent: 30, text: 'Collecting images...' });
  const images = new Map<string, Uint8Array>();
  await collectImages(root, images, (text) => {
    figma.ui.postMessage({ type: 'progress', percent: 50, text });
  });
  await collectComponentDependencyImages(componentDependencies, images, [serialized, ...prototypePages,
    { id: 'source-data-dependencies', type: 'SOURCE_DATA', name: 'Source definitions', sourceDependencies }]);
  if (componentDependencies.errors.length > 0) {
    throw new Error(componentDependencies.errors.map(error => error.message).join('\n'));
  }

  const diagnostics: ExportDiagnostic[] = [...documentSource.diagnostics, ...(pageSource?.diagnostics || [])];
  for (const tree of sourceTrees) {
    for (const node of indexRestNodes(tree).values()) diagnostics.push(...(node.design2umgExportDiagnostics || []));
  }
  for (const metadata of [...Object.values(components), ...Object.values(componentSets)]) {
    diagnostics.push(...(metadata.design2umgExportDiagnostics || []));
  }
  const warning = diagnostics.length
    ? `${diagnostics.length} source API field(s) could not be read. See design2umgExportDiagnostics in design.json.` : '';
  (document as any).design2umgExportDiagnostics = diagnostics;
  figma.ui.postMessage({ type: 'progress', percent: 70, text: 'Rendering reference PNG...' });
  const reference = await exportReferencePng(root, scale);

  figma.ui.postMessage({ type: 'progress', percent: 80, text: 'Preparing download...' });

  return { warning, json: JSON.stringify(document, null, 2), images, reference };
}

// ── Main Export Flow ──
if (typeof figma !== 'undefined') {
  figma.showUI(__html__, { width: 400, height: 250 });
  let sequence = 0;
  let pending: { sequence: number; resolve: () => void; reject: (error: Error) => void } | undefined;
  const send = (message: ExportTransferMessage): Promise<void> => new Promise((resolve, reject) => {
    const current = ++sequence;
    pending = { sequence: current, resolve, reject };
    try {
      figma.ui.postMessage({ ...message, sequence: current });
    } catch (error) {
      pending = undefined;
      reject(error);
    }
  });

  figma.ui.onmessage = async (msg: ExportRequestMessage) => {
    if (msg.type === 'export-ack') {
      if (pending && pending.sequence === msg.sequence) {
        const completed = pending;
        pending = undefined;
        if (msg.error) completed.reject(new Error(msg.error));
        else completed.resolve();
      }
      return;
    }
    if (msg.type !== 'export') return;
    const scale = msg.scale || 2;
    try {
      const rootId = msg.rootNodeId?.trim();
      const root = rootId ? await figma.getNodeByIdAsync(rootId) : figma.currentPage.selection[0];
      if (!root || (root.type !== 'FRAME' && root.type !== 'COMPONENT_SET' && root.type !== 'COMPONENT' && root.type !== 'INSTANCE')) {
        throw new Error('Select a Frame, Component Set, Component, or Instance, or enter its root node ID.');
      }
      const prepared = await prepareExport(root, scale);
      await transferExport(prepared, send);
    } catch (e: any) {
      figma.ui.postMessage({ type: 'error', text: `Export failed: ${e.message || e}` });
    }
  };
}

// ── Node test exposure ──
// In the Figma sandbox `module` is undefined and this block is skipped; under
// Node (tests via load-exporter-test-api.js) the internals attach to the
// CommonJS export so tests can require them without dynamic evaluation.
declare const module: { exports?: Record<string, unknown> };
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    indexRestNodes,
    serializePaint,
    serializeEffect,
    exportOfficialRestNode,
    pluginReactionsToRest,
    copySourceValue,
    unwrapRestExportResponse,
    collectReferencedComponents,
    collectComponentDependencies,
    collectComponentDependencyImages,
    collectSourceDataDependencies,
    collectImages,
    exportReferencePng,
    transferExport,
  };
}
