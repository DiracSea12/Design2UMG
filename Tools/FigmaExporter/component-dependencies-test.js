const assert = require('node:assert/strict');
globalThis.__html__ = '';
globalThis.figma = { mixed: Symbol('mixed'), showUI() {}, ui: {} };
const loadExporterTestApi = require('./load-exporter-test-api');
const api = loadExporterTestApi();

function recordExport(messages) {
  return message => {
    messages.push(message);
    if (message.sequence !== undefined) queueMicrotask(() => figma.ui.onmessage({ type: 'export-ack', sequence: message.sequence }));
  };
}
function exportJson(messages) {
  return messages.filter(message => message.type === 'export-text').map(message => message.data).join('');
}

function node(id, type, children = []) {
  return { id, name: 'Same name', type, visible: true, children,
    ...(type === 'TEXT' ? { getStyledTextSegments() { return []; } } : {}) };
}
function component(id, key, children = []) {
  const result = { ...node(id, 'COMPONENT', children), key };
  result.exportAsync = async () => ({ nodes: { [id]: { document: {
    id, name: result.name, type: result.type, interactions: result.reactions, children: children.map(child => ({
      id: child.id, name: child.name, type: child.type, visible: child.visible,
    })),
  } } } });
  return result;
}
function instance(id, main, children = []) {
  return { ...node(id, 'INSTANCE', children), getMainComponentAsync: async () => main };
}

(async () => {
  assert.equal(typeof api.collectComponentDependencies, 'function', 'dependency collector is exported');
  const leafGeometry = node('2:3', 'VECTOR');
  leafGeometry.fillGeometry = [{ data: 'M0 0 L8 0 L8 8 Z', windingRule: 'NONZERO' }];
  const leaf = component('2:1', 'leaf-key', [node('2:2', 'TEXT'), leafGeometry]);
  const nested = instance('3:2', leaf);
  const parent = component('3:1', 'parent-key', [nested]);
  const other = component('4:1', 'other-key');
  const root = node('1:1', 'FRAME', [instance('1:2', parent), instance('1:3', parent), instance('1:4', other)]);
  const result = await api.collectComponentDependencies(root);
  assert.deepEqual(result.errors, []);
  assert.deepEqual(result.definitions[0].document.children[1].fillGeometry,
    [{ path: 'M0 0 L8 0 L8 8 Z', windingRule: 'NONZERO' }]);
  assert.deepEqual(result.definitions.map(entry => entry.key), ['leaf-key', 'parent-key', 'other-key']);
  assert.equal(result.definitions.filter(entry => entry.key === 'parent-key').length, 1);
  assert.equal(result.definitions[1].document.children[0].componentId, '2:1');
  assert.equal(result.definitions[1].document.children[0].mainComponentKey, 'leaf-key');
  assert.equal(result.instances['1:2'].key, 'parent-key');
  assert.equal(result.instances['1:3'].key, 'parent-key');
  assert.equal(result.instances['3:2'], undefined, 'definition-local ids do not leak into the selected file identity map');
  assert.equal(result.definitions.some(entry => entry.id === '1:1'), false, 'ordinary frame is not a component');
  assert.equal(result.definitions[1].document.type, 'COMPONENT');

  const missing = await api.collectComponentDependencies(node('5:1', 'FRAME', [instance('5:2', null)]));
  assert.equal(missing.errors.length, 1);
  assert.equal(missing.errors[0].nodeId, '5:2');
  assert.equal(missing.errors[0].kind, 'component_unavailable');

  const denied = instance('5:3', leaf);
  denied.getMainComponentAsync = async () => { throw new Error('source unavailable'); };
  const deniedResult = await api.collectComponentDependencies(denied);
  assert.match(deniedResult.errors[0].message, /source unavailable/);

  const cycle = component('6:1', 'cycle-key');
  cycle.children.push(instance('6:2', cycle));
  const cyclic = await api.collectComponentDependencies(instance('6:3', cycle));
  assert.ok(cyclic.errors.some(error => error.kind === 'component_cycle'));

  const variant = component('7:1', 'variant-key');
  variant.parent = { id: '7:0', key: 'set-key', type: 'COMPONENT_SET' };
  const variants = await api.collectComponentDependencies(instance('7:2', variant));
  assert.equal(variants.definitions[0].componentSetKey, 'set-key');
  assert.equal(variants.instances['7:2'].componentSetId, '7:0');
  const variantMessages = [];
  const variantInstance = { ...instance('7:2', variant), mainComponent: variant,
    async exportAsync(options) { return options.format === 'JSON_REST_V1'
      ? { document: { id: this.id, type: this.type, children: [] } } : new Uint8Array([1, 2, 3]); } };
  figma.root = { name: 'Variant document' };
  figma.currentPage = { id: 'variant-page', name: 'Variant page', selection: [variantInstance] };
  figma.ui.postMessage = recordExport(variantMessages);
  await figma.ui.onmessage({ type: 'export' });
  const variantDone = variantMessages.find(message => message.type === 'export-complete');
  assert.ok(variantDone, 'component identities are applied by the current official export entry point');
  const serializedVariant = JSON.parse(exportJson(variantMessages)).document.children[0].children[0];
  assert.equal(serializedVariant.componentSetId, '7:0');
  assert.equal(serializedVariant.componentSetKey, 'set-key', 'published set key is distinct from node locator');

  const hidden = node('8:2', 'TEXT'); hidden.visible = false;
  const hiddenMain = component('8:1', 'hidden-key', [hidden]);
  const hiddenResult = await api.collectComponentDependencies(instance('8:3', hiddenMain));
  assert.equal(hiddenResult.definitions[0].document.children[0].visible, false);

  const broken = component('9:1', 'broken-key');
  broken.exportAsync = async () => { throw new Error('serialization failed'); };
  const brokenResult = await api.collectComponentDependencies(instance('9:2', broken));
  assert.equal(brokenResult.definitions.length, 0);
  assert.ok(brokenResult.errors.some(error => error.kind === 'component_export_failed'));

  const swapped = component('10:1', 'swapped-key');
  const swappedResult = await api.collectComponentDependencies(instance('10:2', parent, [instance('10:3', swapped)]));
  assert.ok(swappedResult.definitions.some(entry => entry.key === 'swapped-key'), 'resolved instance swaps are dependencies');
  assert.equal(swappedResult.instances['10:3'].key, 'swapped-key');

  const local = component('11:1', '');
  const localResult = await api.collectComponentDependencies(node('11:0', 'FRAME', [instance('11:2', local), instance('11:3', local)]));
  assert.equal(localResult.definitions.length, 1, 'unpublished components deduplicate by node identity within the file');
  assert.equal(localResult.definitions[0].id, '11:1');

  const remoteLeaf = component('remote-leaf', 'remote-leaf-key');
  const remoteParent = component('remote-parent', 'remote-parent-key', [instance('collision', remoteLeaf)]);
  const localParent = component('local-parent', 'local-parent-key', [instance('collision', remoteParent)]);
  const collisions = await api.collectComponentDependencies(node('root', 'FRAME', [instance('collision', localParent)]));
  assert.equal(collisions.instances.collision.key, 'local-parent-key');
  assert.equal(collisions.definitions.find(entry => entry.key === 'local-parent-key').document.children[0].mainComponentKey, 'remote-parent-key');
  assert.equal(collisions.definitions.find(entry => entry.key === 'remote-parent-key').document.children[0].mainComponentKey, 'remote-leaf-key');

  const remoteState = component('remote-state', 'remote-state-key');
  const remoteStart = component('remote-start', 'remote-start-key');
  remoteState.remote = remoteStart.remote = true;
  const remoteSet = { id: 'remote-set', name: 'Remote set', type: 'COMPONENT_SET', key: 'remote-set-key',
    remote: true, children: [remoteStart, remoteState],
    get componentPropertyDefinitions() { throw new Error('Component set has existing errors'); } };
  remoteStart.parent = remoteState.parent = remoteSet;
  const remoteReaction = destinationId => ({ trigger: { type: 'ON_CLICK' },
    actions: [{ type: 'NODE', navigation: 'CHANGE_TO', destinationId }] });
  remoteStart.reactions = [remoteReaction(remoteState.id)];
  remoteState.reactions = [remoteReaction(remoteStart.id)];
  const misleadingLocalState = component(remoteState.id, 'different-local-key');
  const previousRemoteLookup = figma.getNodeByIdAsync;
  let remoteGlobalLookups = 0;
  figma.getNodeByIdAsync = async () => { remoteGlobalLookups++; return misleadingLocalState; };
  const remoteInstance = { ...instance('remote-instance', remoteStart), mainComponent: remoteStart,
    async exportAsync(options) {
      return options.format === 'JSON_REST_V1'
        ? { document: { id: this.id, type: this.type, children: [] } } : new Uint8Array([1, 2, 3]);
    } };
  const remoteClosure = await api.collectComponentDependencies(remoteInstance);
  assert.deepEqual(remoteClosure.errors, []);
  assert.deepEqual(new Set(remoteClosure.definitions.map(entry => entry.key)), new Set(['remote-start-key', 'remote-state-key']));
  assert.equal(remoteGlobalLookups, 0, 'remote state closure never resolves ids in the current file');
  const nestedStart = component('nested-start', 'nested-start-key');
  const nestedState = component('nested-state', 'nested-state-key');
  nestedStart.remote = nestedState.remote = true;
  const nestedSet = { id: 'nested-set', type: 'COMPONENT_SET', children: [nestedStart, nestedState] };
  nestedStart.parent = nestedState.parent = nestedSet;
  const nestedInstance = instance('nested-instance', nestedStart);
  nestedInstance.reactions = [remoteReaction(nestedState.id)];
  const outerVariant = component('outer-variant', 'outer-key', [nestedInstance]);
  outerVariant.remote = true;
  const outerSet = { id: 'outer-set', type: 'COMPONENT_SET',
    children: [outerVariant, component(nestedState.id, 'wrong-outer-key')] };
  outerVariant.parent = outerSet;
  nestedInstance.parent = outerVariant;
  const nestedClosure = await api.collectComponentDependencies(instance('outer-instance', outerVariant));
  assert.deepEqual(nestedClosure.errors, []);
  assert.ok(nestedClosure.definitions.some(entry => entry.key === nestedState.key),
    'nested instance CHANGE_TO resolves the target in its main component set, not the outer set');
  assert.equal(remoteGlobalLookups, 0, 'nested remote targets cannot resolve against a same-id local node');
  nestedInstance.getMainComponentAsync = async () => null;
  const missingNestedMain = await api.collectComponentDependencies(instance('outer-instance', outerVariant));
  assert.ok(missingNestedMain.errors.some(error => /no accessible source component set/.test(error.message)));
  assert.equal(remoteGlobalLookups, 0, 'an inaccessible nested main component does not fall back to another file');
  const remoteComponents = {}, remoteSets = {};
  api.collectReferencedComponents(remoteInstance, remoteComponents, remoteSets);
  assert.equal(remoteSets[remoteSet.id].componentPropertyDefinitions, undefined);
  assert.ok(remoteSets[remoteSet.id].design2umgExportDiagnostics.some(entry =>
    entry.nodeId === remoteSet.id && entry.field === 'componentPropertyDefinitions' && /existing errors/.test(entry.message)),
  'unreadable source definitions produce an explicit metadata diagnostic');
  const remoteMessages = [];
  figma.root = { name: 'Remote source document' };
  figma.currentPage = { id: 'remote-page', name: 'Remote page', selection: [remoteInstance] };
  figma.ui.postMessage = recordExport(remoteMessages);
  await figma.ui.onmessage({ type: 'export' });
  const remotePackage = remoteMessages.find(message => message.type === 'export-start');
  assert.ok(remotePackage && remotePackage.warning, 'unreadable optional definitions are reported to the user');
  assert.ok(JSON.parse(exportJson(remoteMessages)).design2umgExportDiagnostics.some(entry =>
    entry.nodeId === remoteSet.id && entry.field === 'componentPropertyDefinitions'),
  'metadata diagnostics also reach the exported package diagnostic list');
  remoteSet.children = [remoteStart];
  const missingRemoteState = await api.collectComponentDependencies(remoteInstance);
  assert.ok(missingRemoteState.errors.some(error => /unavailable in source component set remote-set/.test(error.message)));
  assert.equal(remoteGlobalLookups, 0, 'an inaccessible remote state cannot fall back to a same-id local component');
  remoteSet.children = [remoteStart, remoteState];
  remoteStart.reactions[0].actions[0].navigation = 'NAVIGATE';
  const remoteNavigation = await api.collectComponentDependencies(remoteInstance);
  assert.ok(remoteNavigation.errors.some(error => /Remote NAVIGATE destination/.test(error.message)),
    'remote page navigation keeps the existing file identity protection');
  assert.equal(remoteGlobalLookups, 0);
  figma.getNodeByIdAsync = previousRemoteLookup;

  const images = new Map();
  const imageBundle = { schemaVersion: 1, definitions: [{ id: '12:1', key: 'images', document: {
    id: '12:1', type: 'COMPONENT', children: [{ id: '12:2', type: 'RECTANGLE',
      fills: [{ type: 'IMAGE', imageRef: 'default-image' }],
      strokes: [{ type: 'IMAGE', imageRef: 'default-image' }, { type: 'IMAGE', imageRef: 'missing-image' }],
    }],
  } }], instances: {}, errors: [] };
  let imageReads = 0;
  figma.getImageByHash = hash => hash === 'missing-image' ? null : {
    getBytesAsync: async () => { imageReads++; return new Uint8Array([1, 2, 3]); },
  };
  await api.collectComponentDependencyImages(imageBundle, images);
  assert.equal(imageReads, 1, 'source image bytes are collected once across fills and strokes');
  assert.deepEqual(Array.from(images.get('default-image')), [1, 2, 3]);
  assert.equal(imageBundle.errors[0].kind, 'component_image_unavailable');
  assert.equal(imageBundle.errors[0].nodeId, 'missing-image');

  const state = component('state', 'state-key');
  const initial = component('initial', 'initial-key', [instance('private-state-instance', leaf)]);
  const reaction = (destinationId, navigation = 'CHANGE_TO') => ({ trigger: { type: 'ON_CLICK' },
    actions: [null, { type: 'NODE', navigation, destinationId }] });
  initial.reactions = [reaction('state')];
  state.reactions = [reaction('initial'), reaction('overlay', 'OVERLAY')];
  const overlay = node('overlay', 'FRAME', [instance('overlay-instance', other)]);
  overlay.reactions = [reaction('selected', 'NAVIGATE')];
  overlay.exportAsync = async () => ({ document: { id: overlay.id, type: 'FRAME', children: [
    { id: 'overlay-instance', type: 'INSTANCE', componentId: other.id }], interactions: [] } });
  const selected = node('selected', 'FRAME', [instance('selected-instance', initial)]);
  selected.reactions = [{ trigger: null, actions: reaction('absent').actions }];
  const lookup = new Map([initial, state, overlay, selected].map(entry => [entry.id, entry]));
  figma.getNodeByIdAsync = async id => lookup.get(id) || null;
  const pages = [];
  const metadata = {};
  const closure = await api.collectComponentDependencies(selected, pages, metadata);
  assert.deepEqual(closure.errors, []);
  assert.deepEqual(new Set(closure.definitions.map(entry => entry.id)), new Set([initial.id, state.id, leaf.id, other.id]));
  assert.deepEqual(Object.keys(closure.instances).sort(), ['overlay-instance', 'selected-instance']);
  assert.equal(pages.length, 1);
  assert.deepEqual(pages[0].interactions, overlay.reactions);
  assert.equal(metadata.state.key, 'state-key');
  assert.equal(pages[0].children[0].mainComponentKey, 'other-key');
  const menuDefinition = component('menu-definition', 'menu-key');
  const menuInstance = instance('menu-instance', menuDefinition);
  const screenComponent = component('screen-component', 'screen-key', [instance('screen-child', leaf)]);
  const navigationRoot = node('navigation-root', 'FRAME');
  navigationRoot.reactions = [reaction(menuInstance.id, 'OVERLAY')];
  menuInstance.reactions = [reaction(screenComponent.id, 'SWAP')];
  screenComponent.reactions = [reaction(navigationRoot.id, 'NAVIGATE')];
  menuInstance.exportAsync = async () => ({ document: { id: menuInstance.id, type: menuInstance.type,
    children: [], interactions: menuInstance.reactions } });
  const navigationLookup = new Map([navigationRoot, menuInstance, screenComponent].map(entry => [entry.id, entry]));
  figma.getNodeByIdAsync = async id => navigationLookup.get(id) || lookup.get(id) || null;
  const navigationPages = [];
  const navigationClosure = await api.collectComponentDependencies(navigationRoot, navigationPages);
  assert.deepEqual(navigationClosure.errors, []);
  assert.deepEqual(navigationPages.map(page => [page.id, page.type]), [
    [menuInstance.id, 'INSTANCE'], [screenComponent.id, 'COMPONENT'],
  ], 'instance and component destinations retain their source types as prototype pages');
  assert.equal(navigationPages[0].mainComponentKey, menuDefinition.key);
  assert.equal(navigationPages[1].children[0].mainComponentKey, leaf.key);
  assert.deepEqual(new Set(navigationClosure.definitions.map(entry => entry.id)),
    new Set([menuDefinition.id, screenComponent.id, leaf.id]), 'component destinations also retain their definition dependencies');
  navigationRoot.reactions = [reaction(menuInstance.id)];
  const invalidVariantTarget = await api.collectComponentDependencies(navigationRoot);
  assert.ok(invalidVariantTarget.errors.some(error => error.kind === 'prototype_destination_unavailable'
    && error.message.includes('CHANGE_TO') && error.message.includes('"type":"INSTANCE"')),
  'CHANGE_TO still requires a component variant');
  figma.getNodeByIdAsync = async id => lookup.get(id) || null;
  const runtimeComponent = await api.collectComponentDependencies(initial);
  assert.equal(runtimeComponent.instances['private-state-instance'].key, 'leaf-key');
  state.reactions = [reaction('absent')];
  const missingState = await api.collectComponentDependencies(selected);
  assert.ok(missingState.errors.some(error => error.kind === 'prototype_destination_unavailable' && /absent/.test(error.message)));
  const lost = component('lost', 'lost-key');
  lost.reactions = [reaction('state')];
  lost.exportAsync = async () => ({ document: { id: lost.id, type: 'COMPONENT', interactions: [reaction(null)] } });
  const lostResult = await api.collectComponentDependencies(instance('lost-instance', lost));
  assert.equal(lostResult.errors.some(error => error.kind === 'component_export_failed'), false,
    'missing official destinations are repaired from live reactions');
  assert.deepEqual(lostResult.definitions.find(entry => entry.id === lost.id).document.interactions, lost.reactions);
  const disconnected = node('disconnected', 'FRAME');
  disconnected.reactions = [reaction(null, 'NAVIGATE')];
  disconnected.exportAsync = async () => ({ document: { id: disconnected.id, type: disconnected.type, children: [] } });
  const previousDisconnectedLookup = figma.getNodeByIdAsync;
  let disconnectedLookups = 0;
  figma.getNodeByIdAsync = async () => { disconnectedLookups++; return null; };
  const disconnectedClosure = await api.collectComponentDependencies(disconnected);
  assert.deepEqual(disconnectedClosure.errors, [], 'an explicit null target is a representable source action without a dependency');
  assert.equal(disconnectedLookups, 0, 'null target actions never enter destination resolution');
  const disconnectedRest = await api.exportOfficialRestNode(disconnected);
  assert.deepEqual(disconnectedRest.interactions, disconnected.reactions, 'the full action, including destinationId:null, is retained');
  assert.deepEqual(disconnectedRest.design2umgPluginData.reactions, disconnected.reactions, 'the raw source null is retained too');
  disconnected.reactions[0].actions[1].destinationId = undefined;
  assert.ok((await api.collectComponentDependencies(disconnected)).errors.some(error => /NAVIGATE destination is missing/.test(error.message)),
    'an absent destination field remains a failure');
  disconnected.reactions[0].actions[1].destinationId = 'unavailable-destination';
  const unavailableTarget = await api.collectComponentDependencies(disconnected);
  assert.ok(unavailableTarget.errors.some(error => /unavailable-destination.*missing/.test(error.message)),
    'a nonempty destination that cannot be read remains a failure');
  assert.ok(unavailableTarget.errors[0].message.includes('actualTarget=null'), 'a failed lookup is distinguishable from a type rejection');
  disconnected.reactions[0].actions[1].navigation = 'OVERLAY';
  figma.getNodeByIdAsync = async () => ({ ...node('unavailable-destination', 'GROUP'), name: 'Actual target' });
  const wrongTargetType = await api.collectComponentDependencies(disconnected);
  assert.ok(wrongTargetType.errors[0].message.includes('actualTarget={"id":"unavailable-destination","type":"GROUP","name":"Actual target"}'),
    'a rejected overlay target reports its actual identity, type and name');
  figma.getNodeByIdAsync = previousDisconnectedLookup;
  const noNavigation = node('no-navigation', 'FRAME');
  noNavigation.reactions = [{ trigger: { type: 'ON_CLICK' }, actions: [{ type: 'NODE', destinationId: noNavigation.id }] }];
  await assert.rejects(api.collectComponentDependencies(noNavigation), /missing required navigation/);
  const lostNavigation = component('lost-navigation', 'lost-navigation-key');
  lostNavigation.reactions = [reaction('lost-navigation-instance', 'NAVIGATE')];
  lostNavigation.exportAsync = async () => ({ document: { id: lostNavigation.id, type: 'COMPONENT',
    interactions: [reaction('lost-navigation-instance', '')] } });
  const lostNavigationResult = await api.collectComponentDependencies(instance('lost-navigation-instance', lostNavigation));
  assert.deepEqual(lostNavigationResult.errors, [], 'source navigation is authoritative over a corrupt official field');
  const patternSource = node('pattern-source', 'RECTANGLE');
  patternSource.exportAsync = async () => ({ document: { id: patternSource.id, type: patternSource.type } });
  const patterned = node('pattern-root', 'FRAME');
  patterned.fills = [{ type: 'PATTERN', sourceNodeId: patternSource.id, tileType: 'RECTANGULAR',
    scalingFactor: 1, spacing: { x: 0, y: 0 }, horizontalAlignment: 'START' }];
  figma.getNodeByIdAsync = async id => id === patternSource.id ? patternSource : lookup.get(id) || null;
  const patternClosure = await api.collectComponentDependencies(patterned);
  assert.deepEqual(patternClosure.errors, []);
  assert.equal(patternClosure.sourceNodes[0].id, patternSource.id, 'pattern source nodes join the actual source dependency closure');
  const unknownNavigation = node('unknown-navigation', 'FRAME');
  unknownNavigation.reactions = [reaction('unresolved-target', 'UNKNOWN_NAVIGATION')];
  unknownNavigation.exportAsync = async () => ({ document: { id: unknownNavigation.id, type: 'FRAME', interactions: unknownNavigation.reactions } });
  assert.deepEqual((await api.collectComponentDependencies(unknownNavigation)).errors, []);
  assert.deepEqual((await api.exportOfficialRestNode(unknownNavigation)).interactions, unknownNavigation.reactions);
  const messages = [];
  figma.root = { name: 'Synthetic document' };
  let blockedRenderCalls = 0;
  const blockedVector = node('blocked-vector', 'VECTOR');
  blockedVector.exportAsync = async () => { blockedRenderCalls++; return new Uint8Array([1, 2, 3]); };
  const selectedMissing = node('selected-missing', 'FRAME', [instance('missing-instance', null), blockedVector]);
  selectedMissing.exportAsync = async options => {
    if (options.format === 'PNG') { blockedRenderCalls++; return new Uint8Array([1, 2, 3]); }
    return { document: { id: selectedMissing.id, type: 'FRAME',
      children: [{ id: 'missing-instance', type: 'INSTANCE' }, { id: blockedVector.id, type: 'VECTOR' }] } };
  };
  figma.currentPage = { selection: [selectedMissing] };
  figma.ui.postMessage = recordExport(messages);
  await figma.ui.onmessage({ type: 'export' });
  assert.ok(messages.some(message => message.type === 'error' && /Main component is missing/.test(message.text)));
  assert.equal(messages.some(message => message.type === 'export-start'), false);
  assert.equal(blockedRenderCalls, 0, 'known structural failures stop before vector or reference rendering');
  assert.equal(messages.some(message => message.type === 'progress' && message.percent >= 30), false,
    'known dependency errors stop before entering resource collection');
  const manual = node('manual', 'FRAME');
  manual.reactions = [reaction('manual-target', 'NAVIGATE')];
  manual.exportAsync = async options => {
    if (options.format === 'JSON_REST_V1') throw new Error('official export fixture failure');
    return new Uint8Array([1, 2, 3]);
  };
  const manualTarget = node('manual-target', 'FRAME');
  manualTarget.exportAsync = async () => ({ document: { id: manualTarget.id, type: 'FRAME', children: [] } });
  figma.getNodeByIdAsync = async id => id === manualTarget.id ? manualTarget : null;
  figma.currentPage = { id: 'page', name: 'Page', selection: [manual] };
  messages.length = 0;
  const originalWarn = console.warn;
  console.warn = () => {};
  try {
    await figma.ui.onmessage({ type: 'export' });
  } finally {
    console.warn = originalWarn;
  }
  const manualExport = messages.find(message => message.type === 'export-start');
  assert.equal(manualExport, undefined, 'official serialization failure does not silently select a partial serializer');
  assert.ok(messages.some(message => message.type === 'error' && /official export fixture failure/.test(message.text)));
  let officialInteractions = [];
  manual.reactions = [{ trigger: { type: 'MOUSE_DOWN' }, actions: [{ type: 'URL', url: 'https://example.com' }] }];
  manual.exportAsync = async options => options.format === 'JSON_REST_V1'
    ? { document: { id: manual.id, type: manual.type, children: [], interactions: officialInteractions } }
    : new Uint8Array([1, 2, 3]);
  console.warn = () => {};
  try {
    messages.length = 0;
    await figma.ui.onmessage({ type: 'export' });
    const mouseExport = messages.find(message => message.type === 'export-complete');
    assert.ok(mouseExport, 'source reactions restore missing non-NODE interaction in the official tree');
    assert.deepEqual(JSON.parse(exportJson(messages)).document.children[0].children[0].interactions, manual.reactions);
    manual.reactions = [{ trigger: { type: 'AFTER_TIMEOUT', timeout: 0.9750000238418579 }, actions: [{
      type: 'NODE', destinationId: manualTarget.id, navigation: 'OVERLAY', resetScrollPosition: false,
      transition: { type: 'DISSOLVE', duration: 0.30000001192092896, easing: { type: 'EASE_IN' } },
    }] }];
    officialInteractions = [{ trigger: { type: 'ON_CLICK' }, actions: manual.reactions[0].actions }];
    assert.deepEqual((await api.exportOfficialRestNode(manual)).interactions, api.pluginReactionsToRest(manual.reactions),
      'source trigger and timeout repair official interaction differences');
    officialInteractions = [{ actions: [{
      transition: { easing: { type: 'EASE_IN' }, duration: 0.30000001192092896, type: 'DISSOLVE' },
      resetScrollPosition: false, navigation: 'OVERLAY', destinationId: manualTarget.id, type: 'NODE',
    }], trigger: { timeout: 0.9750000238418579, type: 'AFTER_TIMEOUT' } }];
    const normalized = (await api.exportOfficialRestNode(manual)).interactions;
    assert.equal(normalized[0].actions[0].transition.duration, manual.reactions[0].actions[0].transition.duration * 1000,
      'Plugin-produced JSON transition seconds become REST milliseconds');
    assert.equal(normalized[0].trigger.timeout, manual.reactions[0].trigger.timeout * 1000,
      'Plugin-produced JSON timeout seconds become REST milliseconds');
    assert.equal(manual.reactions[0].trigger.timeout, 0.9750000238418579,
      'normalizing the official export leaves the live Plugin source unchanged');
  } finally {
    console.warn = originalWarn;
  }
  let referenceExportCalls = 0;
  manual.exportAsync = async options => {
    if (options.format === 'JSON_REST_V1') {
      return { document: { id: manual.id, type: manual.type, children: [], interactions: manual.reactions } };
    }
    referenceExportCalls++;
    return new Uint8Array([1, 2, 3]);
  };
  manualTarget.fills = [{ type: 'IMAGE', imageHash: 'missing-page-image' }];
  manualTarget.exportAsync = async () => ({ document: { id: manualTarget.id, type: manualTarget.type,
    children: [], fills: [{ type: 'IMAGE', imageRef: 'missing-page-image' }] } });
  const originalGetImageByHash = figma.getImageByHash;
  figma.getImageByHash = () => null;
  messages.length = 0;
  try {
    await figma.ui.onmessage({ type: 'export' });
    assert.ok(messages.some(message => message.type === 'error' && /missing-page-image: Source image is unavailable/.test(message.text)));
    assert.equal(messages.some(message => message.type === 'export-start'), false, 'missing dependency image never reports completion');
    assert.equal(referenceExportCalls, 0, 'missing dependency image fails before reference export');
  } finally {
    figma.getImageByHash = originalGetImageByHash;
  }
  console.log('Component dependency tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
