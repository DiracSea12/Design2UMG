const assert = require('node:assert/strict');
globalThis.__html__ = '';
globalThis.figma = { mixed: Symbol('mixed'), showUI() {}, ui: {} };
const loadExporterTestApi = require('./load-exporter-test-api');
const api = loadExporterTestApi();

function node(id, type, children = []) {
  return { id, name: 'Same name', type, visible: true, children };
}
function component(id, key, children = []) {
  const result = { ...node(id, 'COMPONENT', children), key };
  result.exportAsync = async () => ({ nodes: { [id]: { document: {
    id, name: result.name, type: result.type, children: children.map(child => ({
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
  const leaf = component('2:1', 'leaf-key', [node('2:2', 'TEXT')]);
  const nested = instance('3:2', leaf);
  const parent = component('3:1', 'parent-key', [nested]);
  const other = component('4:1', 'other-key');
  const root = node('1:1', 'FRAME', [instance('1:2', parent), instance('1:3', parent), instance('1:4', other)]);
  const result = await api.collectComponentDependencies(root);
  assert.deepEqual(result.errors, []);
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
  const serializedVariant = api.serializeNode({ ...instance('7:2', variant), mainComponent: variant });
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
  console.log('Component dependency tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
