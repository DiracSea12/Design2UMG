const assert = require('node:assert/strict');
globalThis.__html__ = '';
globalThis.figma = {
  mixed: Symbol('mixed'),
  showUI() {},
  ui: {
    onmessage: null,
    postMessage() {},
  },
  getImageByHash(hash) {
    return {
      async getBytesAsync() {
        return new Uint8Array([1, 2, 3, 4, hash.length]);
      },
    };
  },
};

require('./code.js');
const api = globalThis.__design2umgExporterTestApi;
assert.ok(api, 'exporter test api missing');

const gradientPaint = {
  type: 'GRADIENT_LINEAR',
  visible: true,
  opacity: 0.8,
  blendMode: 'NORMAL',
  gradientTransform: [[1, 0, 0], [0, 1, 0]],
  gradientStops: [{ color: { r: 1, g: 0, b: 0, a: 1 }, position: 0 }],
  boundVariables: { color: { type: 'VARIABLE_ALIAS', id: 'var1' } },
};
const gradientOut = api.serializePaint(gradientPaint);
assert.ok(gradientOut.gradientTransform, 'gradientTransform missing');
assert.ok(Array.isArray(gradientOut.gradientHandlePositions), 'gradientHandlePositions missing');
assert.ok(gradientOut.boundVariables, 'paint boundVariables missing');

const imagePaint = {
  type: 'IMAGE',
  visible: true,
  opacity: 1,
  blendMode: 'LUMINOSITY',
  imageHash: 'img_hash',
  scaleMode: 'CROP',
  imageTransform: [[1, 0, 0.1], [0, 1, 0.2]],
  rotation: 15,
  scalingFactor: 0.75,
  filters: { exposure: 0.5 },
};
const imageOut = api.serializePaint(imagePaint);
assert.equal(imageOut.imageRef, 'img_hash');
assert.ok(imageOut.imageTransform, 'imageTransform missing');
assert.equal(imageOut.rotation, 15);
assert.equal(imageOut.scalingFactor, undefined);
assert.deepEqual(imageOut.filters, { exposure: 0.5 });

const tiledImageOut = api.serializePaint({
  type: 'IMAGE',
  visible: true,
  opacity: 1,
  blendMode: 'NORMAL',
  imageHash: 'tile_hash',
  scaleMode: 'TILE',
  scalingFactor: 0.75,
});
assert.equal(tiledImageOut.scalingFactor, 0.75);

const effectOut = api.serializeEffect({
  type: 'DROP_SHADOW',
  visible: true,
  blendMode: 'SCREEN',
  opacity: 0.5,
  radius: 4,
  offset: { x: 1, y: 2 },
  spread: 3,
  showShadowBehindNode: true,
  boundVariables: { radius: { type: 'VARIABLE_ALIAS', id: 'var2' } },
});
assert.equal(effectOut.blendMode, 'SCREEN');
assert.equal(effectOut.opacity, 0.5);
assert.equal(effectOut.showShadowBehindNode, true);
assert.ok(effectOut.boundVariables, 'effect boundVariables missing');

const textNode = {
  id: '1:1',
  name: 'Text',
  type: 'TEXT',
  visible: true,
  absoluteBoundingBox: { x: 0, y: 0, width: 100, height: 20 },
  fills: [],
  strokes: [],
  effects: [],
  constraints: { vertical: 'MIN', horizontal: 'MIN' },
  width: 100,
  height: 20,
  opacity: 1,
  blendMode: 'NORMAL',
  characters: 'Hello',
  fontName: { family: 'Inter', style: 'Regular' },
  fontWeight: 400,
  fontSize: 14,
  textAlignHorizontal: 'LEFT',
  textAlignVertical: 'TOP',
  textCase: 'UPPER',
  textDecoration: 'UNDERLINE',
  letterSpacing: { unit: 'PIXELS', value: 1 },
  paragraphSpacing: 8,
  paragraphIndent: 4,
  lineHeight: { unit: 'PIXELS', value: 18 },
  textAutoResize: 'HEIGHT',
  textTruncation: 'ENDING',
  maxLines: 2,
  boundVariables: { characters: { type: 'VARIABLE_ALIAS', id: 'var3' } },
  componentPropertyReferences: { characters: 'prop1' },
  explicitVariableModes: { '1': '2:3' },
  resolvedVariableModes: { '1': '2:4' },
  getStyledTextSegments() { return [{ ...this, start: 0, end: this.characters.length }]; },
};
const textOut = api.serializeNode(textNode);
assert.equal(textOut.maskType, undefined);
assert.equal(textOut.textTruncation, 'ENDING');
assert.equal(textOut.maxLines, 2);
assert.ok(textOut.boundVariables, 'node boundVariables missing');
assert.ok(textOut.componentPropertyReferences, 'componentPropertyReferences missing');
assert.ok(textOut.explicitVariableModes, 'explicitVariableModes missing');
assert.ok(textOut.resolvedVariableModes, 'resolvedVariableModes missing');
assert.equal(textOut.style.textCase, 'UPPER');
assert.equal(textOut.style.textDecoration, 'UNDERLINE');
assert.equal(textOut.style.paragraphSpacing, 8);
assert.equal(textOut.style.paragraphIndent, 4);

const maskNode = {
  id: '1:2',
  name: 'Mask',
  type: 'RECTANGLE',
  visible: true,
  isMask: true,
  maskType: 'ALPHA',
  absoluteBoundingBox: { x: 0, y: 0, width: 10, height: 10 },
  fills: [],
  strokes: [],
  effects: [],
  constraints: { vertical: 'MIN', horizontal: 'MIN' },
  width: 10,
  height: 10,
  opacity: 1,
  blendMode: 'NORMAL',
};
const maskOut = api.serializeNode(maskNode);
assert.equal(maskOut.maskType, 'ALPHA');

const vectorNode = {
  id: '9:9',
  name: 'Vector',
  type: 'VECTOR',
  visible: true,
  absoluteBoundingBox: { x: 0, y: 0, width: 32, height: 32 },
  fills: [{ type: 'SOLID', visible: true, color: { r: 1, g: 0, b: 0, a: 1 } }],
  strokes: [],
  effects: [],
  constraints: { vertical: 'MIN', horizontal: 'MIN' },
  width: 32,
  height: 32,
  opacity: 1,
  blendMode: 'NORMAL',
  fillGeometry: [{ data: 'M0 0 L10 0 L10 10 Z', windingRule: 'NONZERO' }],
};
const vectorOut = api.serializeNode(vectorNode);
assert.ok(Array.isArray(vectorOut.fills) && vectorOut.fills.length === 1, 'vector fills should preserve source fills only');
assert.equal(vectorOut.fills[0].type, 'SOLID');

const riskyLeafNode = {
  id: '202:5356',
  name: 'Image',
  type: 'FRAME',
  visible: true,
  absoluteBoundingBox: { x: 0, y: 0, width: 160, height: 96 },
  fills: [{
    type: 'IMAGE',
    visible: true,
    opacity: 1,
    blendMode: 'LUMINOSITY',
    imageHash: 'img_hash_risky',
    scaleMode: 'FIT',
    filters: { contrast: 0.25 },
  }],
  strokes: [],
  effects: [],
  constraints: { vertical: 'MIN', horizontal: 'MIN' },
  width: 160,
  height: 96,
  opacity: 1,
  blendMode: 'NORMAL',
  children: [],
  reactions: [],
  async exportAsync(options) {
    if (options.format === 'PNG') {
      return new Uint8Array([9, 8, 7, 6]);
    }
    throw new Error(`unexpected export format: ${options.format}`);
  },
};
assert.equal(api.shouldExportNodeRenderFallback(riskyLeafNode).use, true, 'risky leaf should use render fallback');

const safeContainerNode = {
  ...riskyLeafNode,
  id: '202:6000',
  children: [{ id: 'child1', visible: true }],
};
assert.equal(api.shouldExportNodeRenderFallback(safeContainerNode).use, false, 'container should not use full render fallback');

const restNode = api.unwrapRestExportResponse({
  document: {
    id: 'doc',
    type: 'DOCUMENT',
    children: [{ id: 'target', type: 'FRAME', name: 'Target', children: [] }],
  },
}, 'target');
assert.equal(restNode.id, 'target');

const componentSet = {
  id: 'set1',
  name: 'Button',
  type: 'COMPONENT_SET',
  description: '',
  remote: false,
  key: 'set-key',
  componentPropertyDefinitions: { Size: { type: 'VARIANT', defaultValue: 'S' } },
  variantGroupProperties: { Size: { values: ['S', 'M'] } },
  documentationLinks: [{ uri: 'https://example.com' }],
  children: [],
};
const component = {
  id: 'cmp1',
  name: 'Button / Primary',
  type: 'COMPONENT',
  description: '',
  remote: false,
  key: 'cmp-key',
  componentPropertyDefinitions: { Label: { type: 'TEXT', defaultValue: 'OK' } },
  documentationLinks: [],
  children: [],
};
const variantComponentRaw = {
  id: 'cmp2',
  name: 'Button / Active=True',
  type: 'COMPONENT',
  description: '',
  remote: false,
  key: 'cmp-key-variant',
  documentationLinks: [],
  parent: componentSet,
  children: [],
};
const variantComponent = new Proxy(variantComponentRaw, {
  get(target, prop, receiver) {
    if (prop === 'componentPropertyDefinitions') {
      throw new Error('variant component property definitions should not be read');
    }
    return Reflect.get(target, prop, receiver);
  },
});
const instance = {
  id: 'inst1',
  name: 'Button Instance',
  type: 'INSTANCE',
  mainComponent: variantComponent,
  children: [],
};
const components = {};
const componentSets = {};
api.collectReferencedComponents(instance, components, componentSets);
assert.equal(components.cmp2.componentPropertyDefinitions, undefined, 'variant component definitions should be omitted');
api.collectReferencedComponents(component, components, componentSets);
assert.ok(components.cmp1.componentPropertyDefinitions, 'component definitions missing');
assert.ok(componentSets.set1.variantGroupProperties, 'variant group properties missing');

(async () => {
  const images = new Map();
  await api.collectImages({
    type: 'RECTANGLE',
    id: 'node1',
    name: 'Node1',
    fills: [{ type: 'IMAGE', imageHash: 'fill_hash' }],
    strokes: [{ type: 'IMAGE', imageHash: 'stroke_hash' }],
  }, images, 2, () => {});
  assert.ok(images.has('fill_hash'), 'fill image not collected');
  assert.ok(images.has('stroke_hash'), 'stroke image not collected');

  const riskyRestNode = api.serializeNode(riskyLeafNode);
  const restNodesById = api.indexRestNodes(riskyRestNode);
  await api.collectImages(riskyLeafNode, images, 2, () => {}, restNodesById);
  const renderKey = api.makeRenderFallbackKey('202:5356');
  assert.ok(images.has(renderKey), 'render fallback image not collected');
  assert.equal(riskyRestNode.design2umgRenderFallbackImageRef, renderKey, 'render fallback image ref missing');
  assert.equal(riskyRestNode.design2umgRenderFallbackScope, 'full', 'render fallback scope missing');
  assert.equal(riskyRestNode.design2umgRenderFallbackSource, 'figma_render_png', 'render fallback source missing');
  assert.equal(riskyRestNode.design2umgRenderFallbackReason, 'image_filters+image_blend_luminosity', 'render fallback reason missing');

  const reference = await api.exportReferencePng(riskyLeafNode, 2);
  assert.deepEqual(Array.from(reference), [9, 8, 7, 6], 'reference PNG should come from the selected root');
  console.log('FigmaExporter smoke test passed');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
