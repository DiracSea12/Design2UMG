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

const loadExporterTestApi = require('./load-exporter-test-api');
const api = loadExporterTestApi();
assert.ok(api, 'exporter test api missing');
(async () => {
  const pluginTimes = [{ trigger: { type: 'AFTER_TIMEOUT', timeout: 975 }, actions: [{
    type: 'NODE', navigation: 'NAVIGATE', destinationId: 'target',
    transition: { type: 'SMART_ANIMATE', duration: 0.3, easing: { type: 'LINEAR' } },
    conditionalBlocks: [{ actions: [{ type: 'NODE', transition: { duration: 0.5 } }] }],
  }] }];
  const restTimes = api.pluginReactionsToRest(pluginTimes);
  assert.equal(restTimes[0].actions[0].transition.duration, 300);
  assert.equal(restTimes[0].actions[0].conditionalBlocks[0].actions[0].transition.duration, 500);
  assert.equal(restTimes[0].trigger.timeout, 975000);
  assert.equal(pluginTimes[0].actions[0].transition.duration, 0.3, 'producer input remains unchanged');
  assert.equal(pluginTimes[0].trigger.timeout, 975, 'producer timeout remains unchanged');
  const mirroredAction = { type: 'NODE', navigation: 'NAVIGATE', destinationId: 'destination',
    transition: { type: 'SMART_ANIMATE', duration: 0.30000001192092896 } };
  const mirroredReaction = { trigger: { type: 'AFTER_TIMEOUT', timeout: 0.0010000000474974513 }, actions: [mirroredAction] };
  Object.defineProperty(mirroredReaction, 'action', { get() { return this.actions[0]; } });
  const rawMirroredReaction = api.copySourceValue(mirroredReaction);
  const normalizedMirroredReaction = api.pluginReactionsToRest([mirroredReaction])[0];
  assert.equal(Object.hasOwn(normalizedMirroredReaction, 'action'), false,
    'normalized interactions use the canonical actions list, without a source-seconds mirror');
  assert.equal(normalizedMirroredReaction.actions[0].transition.duration, mirroredAction.transition.duration * 1000);
  assert.equal(normalizedMirroredReaction.trigger.timeout, mirroredReaction.trigger.timeout * 1000);
  assert.equal(rawMirroredReaction.action.transition.duration, 0.30000001192092896,
    'the raw source snapshot preserves the current API mirror and seconds');
  assert.equal(mirroredReaction.action.transition.duration, 0.30000001192092896, 'the live source is unchanged');
  const pluginTimer = [{ trigger: { type: 'AFTER_TIMEOUT', timeout: 1 }, actions: [] },
    { trigger: { type: 'MOUSE_ENTER', delay: 0.2, deprecatedVersion: false }, actions: [] }];
  const originalTimer = JSON.stringify(pluginTimer);
  const normalizedTimer = api.pluginReactionsToRest(pluginTimer);
  assert.equal(normalizedTimer[0].trigger.timeout, 1000, 'verified Plugin second becomes REST milliseconds');
  assert.deepEqual(normalizedTimer[1], pluginTimer[1], 'unverified delay and other trigger fields are retained unchanged');
  assert.equal(JSON.stringify(pluginTimer), originalTimer, 'normalization never mutates the source reactions');
  assert.equal(Object.hasOwn(globalThis, '__design2umgExporterTestApi'), false,
    'exporter internals must stay outside the production global object');

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
  assert.deepEqual(gradientOut.gradientHandlePositions, [{ x: 0, y: 0.5 }, { x: 1, y: 0.5 }, { x: 0, y: 1 }]);
  const radialOut = api.serializePaint({ ...gradientPaint, type: 'GRADIENT_RADIAL' });
  assert.deepEqual(radialOut.gradientHandlePositions, [{ x: 0.5, y: 0.5 }, { x: 1, y: 0.5 }, { x: 0.5, y: 1 }],
    'radial control points start at the center of gradient space');

  const translucentSolid = api.serializePaint({
    type: 'SOLID', color: { r: 0.2, g: 0.4, b: 0.6 }, opacity: 0.5,
  });
  assert.deepEqual(translucentSolid.color, { r: 0.2, g: 0.4, b: 0.6, a: 1 });
  assert.equal(translucentSolid.opacity, 0.5, 'paint opacity is applied separately from color alpha');
  const solidWithColorAlpha = api.serializePaint({
    type: 'SOLID', color: { r: 0.2, g: 0.4, b: 0.6, a: 0.8 }, opacity: 0.5,
  });
  assert.equal(solidWithColorAlpha.color.a, 0.8, 'an independent color alpha is retained');
  assert.equal(solidWithColorAlpha.opacity, 0.5);

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
  assert.equal(imageOut.scalingFactor, 0.75, 'all readable paint parameters survive independently of scale mode');
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
  const officialText = {
    id: textNode.id, name: textNode.name, type: textNode.type, characters: 'Hello',
    textTruncation: 'ENDING', maxLines: 2,
    boundVariables: { characters: { type: 'VARIABLE_ALIAS', id: 'var3' } },
    componentPropertyReferences: { characters: 'prop1' },
    explicitVariableModes: { '1': '2:3' }, resolvedVariableModes: { '1': '2:4' },
    style: { fontFamily: 'Inter', fontStyle: 'Regular', fontSize: 14, textCase: 'UPPER',
      textDecoration: 'UNDERLINE', paragraphSpacing: 8, paragraphIndent: 4 },
  };
  textNode.exportAsync = async () => ({ document: structuredClone(officialText) });
  const textOut = await api.exportOfficialRestNode(textNode);
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
  const italicTextOut = await api.exportOfficialRestNode({
    ...textNode,
    fontName: { ...textNode.fontName, style: 'Italic' },
    exportAsync: async () => ({ document: { ...structuredClone(officialText),
      style: { ...officialText.style, fontStyle: 'Italic' } } }),
  });
  assert.equal(italicTextOut.style.fontStyle, 'Italic');
  assert.equal(italicTextOut.characterStyleOverrides, undefined);
  assert.equal(italicTextOut.design2umgPluginData.fontName.style, 'Italic');
  assert.deepEqual(textOut.design2umgPluginData.boundVariables, textNode.boundVariables);
  assert.deepEqual(textOut.design2umgPluginData.componentPropertyReferences, textNode.componentPropertyReferences);
  assert.deepEqual(textOut.design2umgPluginData.explicitVariableModes, textNode.explicitVariableModes);
  assert.deepEqual(textOut.design2umgPluginData.resolvedVariableModes, textNode.resolvedVariableModes);

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
  maskNode.exportAsync = async () => ({ document: { id: maskNode.id, type: maskNode.type, isMask: true, maskType: 'ALPHA' } });
  const maskOut = await api.exportOfficialRestNode(maskNode);
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
  vectorNode.exportAsync = async () => ({ document: { id: vectorNode.id, type: vectorNode.type, fills: [] } });
  const vectorOut = await api.exportOfficialRestNode(vectorNode);
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
      if (options.format === 'JSON_REST_V1') return { document: { id: this.id, type: this.type, children: [] } };
      throw new Error(`unexpected export format: ${options.format}`);
    },
  };
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
  Object.defineProperty(variantComponentRaw, 'componentPropertyDefinitions', {
    get() { throw new Error('variant component property definitions should not be read'); },
  });
  variantComponentRaw.exportAsync = async () => ({ document: { id: variantComponentRaw.id, type: 'COMPONENT', children: [] } });
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

  const variantRest = await api.exportOfficialRestNode(variantComponent);
  assert.equal(Object.hasOwn(variantRest.design2umgPluginData, 'componentPropertyDefinitions'), false,
    'the non-applicable variant getter is not read or exported');
  assert.equal(variantRest.design2umgExportDiagnostics.some(entry => entry.field === 'componentPropertyDefinitions'), false,
    'non-applicable source fields are not reported as missing');
  assert.deepEqual(componentSets.set1.componentPropertyDefinitions, componentSet.componentPropertyDefinitions,
    'the actual parent component set retains the property definitions');
  const unavailableDefinition = { id: 'unavailable-definition', name: 'Unavailable definition', type: 'COMPONENT', children: [],
    get componentPropertyDefinitions() { throw new Error('Source definitions unavailable'); },
    async exportAsync() { return { document: { id: this.id, type: this.type, children: [] } }; } };
  const unavailableRest = await api.exportOfficialRestNode(unavailableDefinition);
  assert.ok(unavailableRest.design2umgExportDiagnostics.some(entry => entry.field === 'componentPropertyDefinitions'
    && entry.message === 'Source definitions unavailable'), 'an applicable getter that fails still reports the real missing data');
  const restChild = { id: vectorNode.id, type: 'VECTOR', fills: [{ type: 'SOLID' }], size: { x: 32, y: 32 } };
  const restRoot = { id: 'geometry-root', type: 'FRAME', children: [restChild] };
  const official = await api.exportOfficialRestNode({
    id: restRoot.id, type: 'FRAME', fillGeometry: [], strokeGeometry: [], children: [{
      ...vectorNode, strokeGeometry: [{ data: 'M1 1 L9 1 L9 2 Z', windingRule: 'EVENODD' }],
    }],
    exportAsync: async () => ({ document: restRoot }),
  });
  assert.equal(official, restRoot, 'official REST fields should not be replaced by manual serialization');
  assert.deepEqual(official.fillGeometry, []);
  assert.deepEqual(official.strokeGeometry, []);
  assert.deepEqual(restChild.fillGeometry, [{ path: vectorNode.fillGeometry[0].data, windingRule: 'NONZERO' }]);
  assert.deepEqual(restChild.strokeGeometry, [{ path: 'M1 1 L9 1 L9 2 Z', windingRule: 'EVENODD' }]);
  assert.deepEqual(restChild.size, { x: 32, y: 32 });
  const sourceMatrix = [[0, -1, 90], [1, 0, 30]];
  const localNode = { id: 'local-geometry', type: 'RECTANGLE', width: 80, height: 40,
    relativeTransform: sourceMatrix, absoluteTransform: sourceMatrix, parent: { type: 'PAGE' },
    async exportAsync() { return { document: { id: this.id, type: this.type,
      absoluteBoundingBox: { x: 50, y: 30, width: 40, height: 80 }, opacity: 0.4 } }; },
  };
  const localRest = await api.exportOfficialRestNode(localNode);
  assert.deepEqual(localRest.size, { x: 80, y: 40 }, 'local size comes from the real SceneNode, not rotated bounds');
  assert.deepEqual(localRest.relativeTransform, sourceMatrix, 'real local transform survives official export');
  assert.notEqual(localRest.relativeTransform, sourceMatrix, 'exported transform does not alias mutable source data');
  assert.equal(localRest.opacity, 0.4, 'other official fields are not replaced');
  const groupMatrix = [[-1, 0, 72], [0, 1, 56]];
  const group = { id: 'group', type: 'GROUP', width: 30, height: 20, parent: { type: 'PAGE' },
    relativeTransform: groupMatrix, absoluteTransform: groupMatrix, children: [] };
  const child = { id: 'child', type: 'RECTANGLE', width: 10, height: 4, parent: group,
    relativeTransform: [[-1, 0, 70], [0, 1, 59]], absoluteTransform: [[-1, 0, 70], [0, 1, 59]], children: [] };
  group.children.push(child);
  group.exportAsync = async () => ({ document: { id: group.id, type: group.type,
    relativeTransform: groupMatrix, children: [{ id: child.id, type: child.type,
      relativeTransform: [[1, 0, 2], [0, 1, 3]] }] } });
  const officialGroup = await api.exportOfficialRestNode(group);
  assert.deepEqual(officialGroup.children[0].relativeTransform, [[1, 0, 2], [0, 1, 3]],
    'official parent-local geometry is not overwritten by plugin coordinates that skip the group');
  group.exportAsync = async () => ({ document: { id: group.id, type: group.type,
    relativeTransform: groupMatrix, children: [{ id: child.id, type: child.type }] } });
  const derivedGroup = await api.exportOfficialRestNode(group);
  assert.deepEqual(derivedGroup.children[0].relativeTransform, [[1, 0, 2], [0, 1, 3]],
    'missing REST geometry is derived from actual child and parent absolute transforms');
  const fullEffect = { type: 'GLASS', visible: true, refraction: 0.7, depth: 12,
    lightIntensity: 0.4, lightAngle: 25, dispersion: 0.3, saturation: 0.8,
    nested: { parameters: [0, false, { value: 'keep' }] } };
  assert.deepEqual(api.serializeEffect(fullEffect), fullEffect, 'effect parameters are data, not a field whitelist');
  const noise = { type: 'NOISE', noiseType: 'DUOTONE', noiseSize: 2, density: 0.6,
    color: { r: 0.1, g: 0.2, b: 0.3, a: 0.4 }, secondaryColor: { r: 1, g: 1, b: 1, a: 1 }, visible: false };
  assert.deepEqual(api.serializeEffect(noise), noise);
  const sourcePrototype = {
    get pluginOnlyLayoutData() { return { customSpacing: 17, tokens: [false, null] }; },
    get layoutGrids() { return [{ pattern: 'ROWS', count: Infinity, sectionSize: 60 },
      { pattern: 'COLUMNS', count: 12, offset: 22.5 }]; },
    get animationStyles() { throw new Error('Motion account gate'); },
    get animations() { return { TRANSLATION_X: { baseValue: { type: 'FLOAT', value: 1 },
      timelineDuration: 0.6, tracks: [{ id: 'track', keyframeOperation: 'SET', keyframes: [
        { id: 'key', timelinePosition: 0.3, value: { type: 'FLOAT', value: 20 }, easing: { type: 'HOLD' } },
      ] }] } }; },
    get manualKeyframeTracks() { return {}; },
    get timelines() { return [{ id: 'timeline', duration: 0.6 }]; },
  };
  const live = Object.assign(Object.create(sourcePrototype), {
    id: 'host-node', name: 'Host node', type: 'FRAME', children: [], reactions: pluginTimer,
    fills: [imagePaint], effects: [fullEffect], exportAsync: async () => ({ document: {
      id: 'host-node', type: 'FRAME', interactions: [], officialOnly: { keep: true },
      layoutGrids: [{ pattern: 'ROWS', count: -1, sectionSize: 60 }, { pattern: 'COLUMNS', count: 12, offset: 22.5 }],
    } }),
  });
  const complete = await api.exportOfficialRestNode(live);
  assert.deepEqual(complete.interactions, normalizedTimer, 'live reactions repair incomplete official interactions');
  assert.deepEqual(complete.officialOnly, { keep: true }, 'official data survives source enrichment');
  assert.deepEqual(JSON.parse(JSON.stringify(complete.design2umgPluginData.layoutGrids)), [
    { pattern: 'ROWS', count: { type: 'NON_FINITE_NUMBER', value: 'Infinity' }, sectionSize: 60 },
    { pattern: 'COLUMNS', count: 12, offset: 22.5 },
  ], 'documented Auto count remains reversible while the complete raw grid field and finite values survive JSON');
  assert.deepEqual(complete.layoutGrids, [{ pattern: 'ROWS', count: -1, sectionSize: 60 },
    { pattern: 'COLUMNS', count: 12, offset: 22.5 }], 'official grid data remains unchanged');
  assert.equal(complete.design2umgExportDiagnostics.some(entry => entry.field === 'layoutGrids'), false);
  assert.deepEqual(api.copySourceValue([Infinity, -Infinity, NaN, 12]), [
    { type: 'NON_FINITE_NUMBER', value: 'Infinity' }, { type: 'NON_FINITE_NUMBER', value: '-Infinity' },
    { type: 'NON_FINITE_NUMBER', value: 'NaN' }, 12,
  ], 'non-finite source numbers retain their exact numeric kind');
  assert.deepEqual(complete.effects, [fullEffect]);
  assert.deepEqual(complete.design2umgPluginData.pluginOnlyLayoutData, sourcePrototype.pluginOnlyLayoutData,
    'non-enumerable host getters are retained without JSON.stringify on the node');
  assert.deepEqual(complete.animations, sourcePrototype.animations);
  assert.equal(complete.timelines[0].duration, 0.6, 'Motion time stays in source seconds');
  assert.equal(complete.design2umgMotionTimeUnit, 'SECONDS');
  assert.equal(Object.hasOwn(complete, 'animationStyles'), false, 'unreadable Motion is not reported as empty');
  assert.ok(complete.design2umgExportDiagnostics.some(entry => entry.field === 'animationStyles'
    && /Motion account gate/.test(entry.message)));
  assert.equal(live.reactions[0].trigger.timeout, 1, 'source values are never mutated');
  await assert.rejects(api.exportOfficialRestNode({ id: 'missing-root', type: 'FRAME', children: [
    { id: 'missing-child', name: 'Hidden content', type: 'RECTANGLE', visible: false,
      exportAsync: async () => { throw new Error('child source unavailable'); } },
  ], exportAsync: async () => ({ document: { id: 'missing-root', type: 'FRAME', children: [] } }) }), error => {
    assert.match(error.message, /REST child missing-child/);
    assert.match(error.message, /child source unavailable/);
    assert.ok(error.message.includes('sourceChildren=[{"id":"missing-child","name":"Hidden content","type":"RECTANGLE","visible":false}]'),
      'the error identifies the exact source child, including hidden content');
    assert.ok(error.message.includes('restChildren=[]'), 'the error records the exact official child list');
    return true;
  }, 'missing tree nodes remain a hard failure with both source and official evidence');
  const subtreeCalls = [];
  const nestedSource = { id: 'nested-source', name: 'Nested source', type: 'RECTANGLE', visible: true,
    fills: [{ type: 'IMAGE', imageHash: 'subtree-image', scaleMode: 'FILL' }], effects: [fullEffect],
    async exportAsync(options) {
      assert.equal(options.format, 'JSON_REST_V1'); subtreeCalls.push(this.id);
      return { document: { id: this.id, type: this.type, officialField: 'nested data' } };
    } };
  const hiddenSource = { id: 'hidden-source', name: 'Hidden source', type: 'INSTANCE', visible: false,
    children: [nestedSource], async exportAsync(options) {
      assert.equal(options.format, 'JSON_REST_V1'); subtreeCalls.push(this.id);
      return { document: { id: this.id, type: this.type, children: [] } };
    } };
  const keptSource = { id: 'kept-source', name: 'Kept source', type: 'RECTANGLE', visible: true,
    async exportAsync() { throw new Error('an existing official child must not be re-exported'); } };
  const keptOfficial = { id: keptSource.id, type: keptSource.type, officialField: 'kept data' };
  const partialSource = { id: 'partial-root', name: 'Partial root', type: 'FRAME', visible: true,
    children: [hiddenSource, keptSource], async exportAsync() {
      return { document: { id: this.id, type: this.type, children: [keptOfficial] } };
    } };
  const repairedTree = await api.exportOfficialRestNode(partialSource);
  assert.deepEqual(subtreeCalls, ['hidden-source', 'nested-source'], 'only missing official subtrees are requested');
  assert.deepEqual(repairedTree.children.map(child => child.id), ['hidden-source', 'kept-source'],
    'the complete tree follows real source order');
  assert.equal(repairedTree.children[1], keptOfficial, 'existing official fields and node data are retained');
  const repairedHidden = repairedTree.children[0];
  assert.equal(repairedHidden.visible, false, 'hidden source subtrees are retained');
  assert.equal(repairedHidden.design2umgPluginData.visible, false);
  assert.equal(repairedHidden.children[0].officialField, 'nested data');
  assert.deepEqual(repairedHidden.children[0].effects, [fullEffect], 'recovered descendants receive the same complete source fields');
  const repairedResources = new Map();
  await api.collectComponentDependencyImages({ definitions: [], errors: [] }, repairedResources, [repairedTree]);
  assert.ok(repairedResources.has('subtree-image'), 'recovered source resources join the existing resource collection');
  hiddenSource.exportAsync = async () => ({ document: { id: hiddenSource.id, type: 'FRAME' } });
  await assert.rejects(api.exportOfficialRestNode(partialSource), /source identity hidden-source.*INSTANCE.*FRAME/,
    'a separately requested child still must match the exact source type and identity');
  const polygon = { id: 'polygon', name: 'Polygon', type: 'POLYGON', visible: true, pointCount: 5,
    fillGeometry: [{ data: 'M0 0 L5 0 L2 5 Z', windingRule: 'NONZERO' }],
    async exportAsync() { return { document: { id: this.id, type: 'REGULAR_POLYGON', officialField: 'polygon data' } }; } };
  const officialPolygon = await api.exportOfficialRestNode(polygon);
  assert.equal(officialPolygon.type, 'REGULAR_POLYGON', 'the official REST polygon type is preserved');
  assert.equal(officialPolygon.design2umgPluginData.type, 'POLYGON', 'the distinct Plugin source type is preserved');
  assert.equal(officialPolygon.design2umgPluginData.pointCount, 5);
  assert.deepEqual(officialPolygon.fillGeometry, [{ path: 'M0 0 L5 0 L2 5 Z', windingRule: 'NONZERO' }]);
  const polygonFrame = { id: 'polygon-frame', name: 'Polygon frame', type: 'FRAME', visible: true, children: [polygon],
    async exportAsync() { return { document: { id: this.id, type: this.type,
      children: [{ id: polygon.id, type: 'REGULAR_POLYGON' }] } }; } };
  const polygonTree = await api.exportOfficialRestNode(polygonFrame);
  assert.equal(polygonTree.children[0].type, 'REGULAR_POLYGON', 'existing official children use the same producer type mapping');
  assert.equal(polygonTree.children[0].design2umgPluginData.type, 'POLYGON');
  await assert.rejects(api.exportOfficialRestNode({ ...polygon,
    exportAsync: async () => ({ document: { id: 'different-polygon', type: 'REGULAR_POLYGON' } }),
  }), /source identity polygon.*different-polygon/, 'a valid producer type mapping never relaxes node identity');
  await assert.rejects(api.exportOfficialRestNode({ ...polygonFrame,
    exportAsync: async () => ({ document: { id: polygonFrame.id, type: 'FRAME', children: [{ id: polygon.id, type: 'ELLIPSE' }] } }),
  }), /changed source identity polygon.*POLYGON.*ELLIPSE/, 'unrelated types remain a hard failure');
  const sourceVariables = {
    v1: { id: 'v1', variableCollectionId: 'collection', valuesByMode: { day: { type: 'VARIABLE_ALIAS', id: 'v2' } } },
    v2: { id: 'v2', variableCollectionId: 'collection', valuesByMode: { day: 12 }, getPluginData() {} },
  };
  const reads = [];
  figma.variables = {
    getVariableByIdAsync: async id => { reads.push(id); return sourceVariables[id]; },
    getVariableCollectionByIdAsync: async id => ({ id, modes: [{ modeId: 'day', name: 'Day' }] }),
  };
  const styleReads = [];
  const readSourceStyle = async id => {
    styleReads.push(id);
    return id === '58915:1197' ? null : { id, type: 'PAINT', paints: [{ type: 'IMAGE', imageHash: 'style-image' }] };
  };
  figma.getStyleByIdAsync = readSourceStyle;
  const boundTree = { id: 'bound-node', type: 'FRAME',
    styles: { fill: '58915:1197' }, styleOverrideTable: { 1: { inheritFillStyleId: '58915:1197' } },
    design2umgPluginData: { fillStyleId: 'paint-style', boundVariables: { width: { type: 'VARIABLE_ALIAS', id: 'v1' } } },
    children: [{ id: 'bound-text', type: 'TEXT', design2umgPluginData: { fillStyleId: 'character-style' } }],
  };
  const boundTreeBefore = JSON.stringify(boundTree);
  const definitions = await api.collectSourceDataDependencies([boundTree]);
  assert.deepEqual(styleReads, ['paint-style', 'character-style'], 'only actual Plugin source style ids are queried, including descendants');
  assert.equal(JSON.stringify(boundTree), boundTreeBefore, 'official REST style references remain intact');
  figma.getStyleByIdAsync = async () => null;
  await assert.rejects(api.collectSourceDataDependencies([boundTree]), /Source styles paint-style is unavailable/,
    'a genuine source Plugin style that cannot be read still fails');
  figma.getStyleByIdAsync = readSourceStyle;
  assert.deepEqual(reads, ['v1', 'v2'], 'bound variable alias closure reads every definition once');
  assert.deepEqual(definitions.variables.v2.valuesByMode, { day: 12 }, 'PluginDataMixin does not reduce variables to node identities');
  assert.equal(definitions.variableCollections.collection.modes[0].modeId, 'day');
  assert.equal(definitions.styles['paint-style'].paints[0].imageHash, 'style-image');
  const resourceBundle = { definitions: [], instances: {}, errors: [] };
  const nestedImages = new Map();
  await api.collectComponentDependencyImages(resourceBundle, nestedImages, [{ id: 'text-images', type: 'TEXT',
    styleOverrideTable: { 1: { fills: [{ type: 'IMAGE', imageRef: 'character-image' }] } }, sourceData: definitions,
  }]);
  assert.ok(nestedImages.has('character-image') && nestedImages.has('style-image'), 'character and style-definition images are required resources');
  await assert.rejects(api.collectComponentDependencyImages(resourceBundle, new Map(), [{ id: 'video', type: 'FRAME',
    fills: [{ type: 'VIDEO', videoHash: 'movie' }],
  }]), /Video resource movie cannot be read/, 'unreadable source media is never silently omitted');
  const images = new Map();
  await api.collectImages({
    type: 'RECTANGLE',
    id: 'node1',
    name: 'Node1',
    fills: [{ type: 'IMAGE', imageHash: 'fill_hash' }],
    strokes: [{ type: 'IMAGE', imageHash: 'stroke_hash' }],
  }, images, () => {});
  assert.ok(images.has('fill_hash'), 'fill image not collected');
  assert.ok(images.has('stroke_hash'), 'stroke image not collected');

  const riskyRestNode = await api.exportOfficialRestNode(riskyLeafNode);
  await api.collectImages(riskyLeafNode, images, () => {});
  assert.ok(images.has('img_hash_risky'), 'original image not collected');
  assert.equal(images.has('render_202_5356'), false, 'node render is not exported');
  assert.equal(riskyRestNode.design2umgRenderFallbackImageRef, undefined, 'node render metadata is not added');
  await api.collectImages({ type: 'VECTOR', id: 'vector', name: 'Vector', fills: [],
    exportAsync: async () => { throw new Error('Vector PNG export must not run'); },
  }, images, () => {});
  assert.equal(images.has('vec_vector'), false, 'vector paint stays in source geometry instead of an auxiliary PNG');

  const reference = await api.exportReferencePng(riskyLeafNode, 2);
  assert.deepEqual(Array.from(reference), [9, 8, 7, 6], 'reference PNG should come from the selected root');

  const messages = [];
  const reactions = [{ trigger: { type: 'ON_CLICK' }, actions: [{ type: 'NODE', navigation: 'NAVIGATE', destinationId: 'target' }] }];
  const selected = {
    id: 'export-root', name: 'Export root', type: 'FRAME', visible: true, children: [], fills: [], reactions,
    async exportAsync(options) {
      if (options.format === 'JSON_REST_V1') throw new Error('official export fixture failure');
      return new Uint8Array([1, 2, 3]);
    },
  };
  const target = {
    id: 'target', name: 'Target', type: 'FRAME', visible: true, children: [], fills: [],
    async exportAsync() {
      return { document: { id: this.id, name: this.name, type: this.type, children: [], fills: [] } };
    },
  };
  const originalGetNodeByIdAsync = figma.getNodeByIdAsync;
  figma.getNodeByIdAsync = async id => id === target.id ? target : null;
  figma.root = { name: 'Test document' };
  figma.currentPage = { id: 'page', name: 'Page', selection: [selected] };
  figma.ui.postMessage = message => {
    messages.push(message);
    if (message.sequence !== undefined) queueMicrotask(() => figma.ui.onmessage({ type: 'export-ack', sequence: message.sequence }));
  };
  const originalWarn = console.warn;
  console.warn = () => {};
  try {
    await figma.ui.onmessage({ type: 'export', scale: 1 });
    const exported = messages.find(message => message.type === 'export-start');
    assert.equal(exported, undefined, 'failed official tree never creates an incomplete manual package');
    assert.ok(messages.some(message => message.type === 'error' && /official export fixture failure/.test(message.text)));

    messages.length = 0;
    selected.exportAsync = async () => ({ document: { id: selected.id, type: selected.type, children: [] } });
    selected.reactions = [{ trigger: reactions[0].trigger, action: reactions[0].actions[0] }];
    await figma.ui.onmessage({ type: 'export', scale: 1 });
    assert.ok(messages.some(message => message.type === 'error'), 'unsupported interaction representation reports failure');
    assert.equal(messages.some(message => message.type === 'export-start'), false, 'unsupported interactions are never silently discarded');
    selected.reactions = reactions;
    const transferReference = new Uint8Array([1, 2, 3]);
    const transferImage = new Uint8Array([4, 5, 6]);
    const originalTransferGetImage = figma.getImageByHash;
    figma.getImageByHash = () => ({ getBytesAsync: async () => transferImage });
    selected.fills = [{ type: 'IMAGE', imageHash: 'transfer-image', scaleMode: 'FILL' }];
    selected.exportAsync = async options => options.format === 'JSON_REST_V1'
      ? { document: { id: selected.id, type: selected.type, children: [] } } : transferReference;
    figma.getNodeByIdAsync = async id => id === selected.id ? selected : id === target.id ? target : null;
    figma.currentPage.selection = [];
    messages.length = 0;
    await figma.ui.onmessage({ type: 'export', rootNodeId: selected.id, scale: 1 });
    const transferDone = messages.find(message => message.type === 'export-complete');
    assert.ok(transferDone, 'root node ID works without selecting or changing the design');
    assert.equal(messages.find(message => message.type === 'export-bytes').data, transferReference,
      'reference bytes are passed as the native Uint8Array');
    assert.equal(messages.filter(message => message.type === 'export-bytes')[1].data, transferImage,
      'image bytes are not expanded into number arrays');
    const transferredJson = messages.filter(message => message.type === 'export-text').map(message => message.data).join('');
    const controls = new Map();
    let capturedZip;
    const uiAcks = [];
    const uiRuntime = { Uint8Array, Uint32Array, DataView, TextEncoder,
      Blob: class { constructor(parts) { this.parts = parts; } },
      URL: { createObjectURL(zip) { capturedZip = zip; return 'blob:test'; }, revokeObjectURL() {} },
      parent: { postMessage(message) { uiAcks.push(message.pluginMessage); } }, document: {
        getElementById(id) { if (!controls.has(id)) controls.set(id, { style: {} }); return controls.get(id); },
        createElement() { return { click() {} }; },
      } };
    const uiHtml = require('node:fs').readFileSync(require('node:path').join(__dirname, 'ui.html'), 'utf8');
    require('node:vm').runInNewContext(uiHtml.match(/<script>([\s\S]*?)<\/script>/)[1], uiRuntime);
    for (const message of messages) uiRuntime.onmessage({ data: { pluginMessage: message } });
    assert.ok(capturedZip.parts.includes(transferReference) && capturedZip.parts.includes(transferImage),
      'UI ZIP construction uses the received byte buffers without copying them again');
    const archiveBytes = Buffer.from(await new Blob(capturedZip.parts).arrayBuffer());
    const extracted = new Map();
    for (let offset = 0; archiveBytes.readUInt32LE(offset) === 0x04034b50;) {
      const size = archiveBytes.readUInt32LE(offset + 18), nameLength = archiveBytes.readUInt16LE(offset + 26);
      const start = offset + 30 + nameLength;
      extracted.set(archiveBytes.toString('utf8', offset + 30, start), archiveBytes.subarray(start, start + size));
      offset = start + size;
    }
    assert.deepEqual(extracted.get('reference.png'), Buffer.from(transferReference));
    assert.deepEqual(extracted.get('images/transfer-image.png'), Buffer.from(transferImage));
    assert.equal(extracted.get('design.json').toString('utf8'), transferredJson, 'the download retains all design data');
    assert.equal(uiAcks.some(message => message.error), false);

    const chunkLength = 1024 * 1024;
    const prefix = '{"data":"';
    const largeJson = prefix + 'x'.repeat(chunkLength - prefix.length - 1) + '😀é中' + 'x'.repeat(chunkLength) + '"}';
    const largeImage = new Uint8Array(chunkLength + 17);
    for (let index = 0; index < largeImage.length; index++) largeImage[index] = index % 251;
    const prepared = { warning: '', json: largeJson, reference: transferReference,
      images: new Map([['large-image', largeImage], ['small-image', transferImage]]) };
    const streamed = [];
    let releaseChunk;
    let firstChunk;
    const firstChunkReady = new Promise(resolve => { firstChunk = resolve; });
    const transfer = api.transferExport(prepared, async message => {
      streamed.push(message);
      if (message.type === 'export-text' && !releaseChunk) {
        firstChunk();
        await new Promise(resolve => { releaseChunk = resolve; });
      }
      uiRuntime.onmessage({ data: { pluginMessage: { ...message, sequence: streamed.length } } });
    });
    await firstChunkReady;
    assert.equal(streamed.filter(message => message.type === 'export-text').length, 1,
      'the next chunk waits for receiver confirmation');
    assert.equal(streamed.some(message => message.type === 'export-file-end'), false);
    releaseChunk();
    await transfer;
    const textChunks = streamed.filter(message => message.type === 'export-text');
    assert.equal(textChunks[0].data.length, chunkLength - 1, 'a UTF-16 pair is never split between UI encoding calls');
    assert.ok(textChunks.length > 1 && textChunks.every(message => message.data.length <= chunkLength));
    const binaryChunks = streamed.filter(message => message.type === 'export-bytes');
    assert.ok(binaryChunks.every(message => message.data instanceof Uint8Array && message.data.length <= chunkLength));
    assert.equal(binaryChunks.filter(message => message.data.buffer === largeImage.buffer).length, 2,
      'binary chunks view the existing bytes instead of copying the whole asset');
    assert.equal(prepared.json, '');
    assert.equal(prepared.reference.length, 0);
    assert.equal(prepared.images.size, 0, 'acknowledged source payloads are released');
    const streamedArchive = Buffer.from(await new Blob(capturedZip.parts).arrayBuffer());
    const streamedFiles = new Map();
    for (let offset = 0; streamedArchive.readUInt32LE(offset) === 0x04034b50;) {
      const size = streamedArchive.readUInt32LE(offset + 18), nameLength = streamedArchive.readUInt16LE(offset + 26);
      const start = offset + 30 + nameLength;
      const bytes = streamedArchive.subarray(start, start + size);
      assert.equal(streamedArchive.readUInt32LE(offset + 14), require('node:zlib').crc32(bytes),
        'incremental ZIP CRC matches the independent native implementation');
      streamedFiles.set(streamedArchive.toString('utf8', offset + 30, start), bytes);
      offset = start + size;
    }
    assert.equal(streamedFiles.get('design.json').toString('utf8'), largeJson);
    assert.deepEqual(streamedFiles.get('reference.png'), Buffer.from(transferReference));
    assert.deepEqual(streamedFiles.get('images/large-image.png'), Buffer.from(largeImage));
    assert.deepEqual(streamedFiles.get('images/small-image.png'), Buffer.from(transferImage));
    capturedZip = undefined;
    uiAcks.length = 0;
    for (const message of [
      { type:'export-start', fileCount:1, warning:'' },
      { type:'export-file', name:'design.json', kind:'text', length:2 },
      { type:'export-text', data:'x' }, { type:'export-file-end' },
    ]) uiRuntime.onmessage({ data:{ pluginMessage:{ ...message, sequence:uiAcks.length + 1 } } });
    assert.ok(uiAcks.at(-1).error.includes('incomplete'), 'truncated files are rejected before a download');
    assert.equal(capturedZip, undefined);
    const acceptMessages = figma.ui.postMessage;
    figma.ui.postMessage = message => {
      messages.push(message);
      if (message.sequence !== undefined) queueMicrotask(() => figma.ui.onmessage({ type:'export-ack',
        sequence:message.sequence, error:message.type === 'export-text' ? 'receiver rejected chunk' : undefined }));
    };
    messages.length = 0;
    await figma.ui.onmessage({ type:'export', rootNodeId:selected.id, scale:1 });
    assert.ok(messages.some(message => message.type === 'error' && /receiver rejected chunk/.test(message.text)),
      'UI rejection terminates the real export handler');
    assert.equal(messages.some(message => message.type === 'export-complete'), false);
    figma.ui.postMessage = acceptMessages;
    figma.getImageByHash = originalTransferGetImage;
    figma.currentPage.selection = [selected];

    selected.type = 'COMPONENT_SET';
    messages.length = 0;
    await figma.ui.onmessage({ type: 'export', rootNodeId: selected.id, scale: 1 });
    assert.ok(messages.some(message => message.type === 'export-complete'), 'component sets use the complete export transfer');
    const setPayload = JSON.parse(messages.filter(message => message.type === 'export-text').map(message => message.data).join(''));
    assert.equal(setPayload.document.children[0].children[0].type, 'COMPONENT_SET', 'component-set source type remains intact');
    selected.type = 'FRAME';

    selected.exportAsync = async options => options.format === 'JSON_REST_V1'
      ? { document: { id: selected.id, name: selected.name, type: selected.type, fills: [{ type: 'IMAGE', imageRef: 'missing' }] } }
      : new Uint8Array([1, 2, 3]);
    selected.fills = [{ type: 'IMAGE', imageHash: 'missing' }];
    const originalGetImage = figma.getImageByHash;
    try {
      for (const image of [null, { async getBytesAsync() { throw new Error('image fixture failure'); } }]) {
        messages.length = 0;
        figma.getImageByHash = () => image;
        await figma.ui.onmessage({ type: 'export', scale: 1 });
        assert.ok(messages.some(message => message.type === 'error'), 'missing required image reports failure');
        assert.equal(messages.some(message => message.type === 'export-start'), false, 'incomplete images never report completion');
      }
    } finally {
      figma.getImageByHash = originalGetImage;
    }
  } finally {
    figma.getNodeByIdAsync = originalGetNodeByIdAsync;
    console.warn = originalWarn;
  }
  console.log('FigmaExporter smoke test passed');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
