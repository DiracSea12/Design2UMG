const assert = require('node:assert/strict');
globalThis.__html__ = '';
globalThis.figma = { mixed: Symbol('mixed'), showUI() {}, ui: { postMessage() {} } };
const loadExporterTestApi = require('./load-exporter-test-api');
const { serializeNode } = loadExporterTestApi();
const normal = { fontName: { family: 'Example Sans', style: 'Regular' }, fontWeight: 400,
  fontStyle: 'REGULAR', fontSize: 18, textDecoration: 'NONE', textCase: 'ORIGINAL',
  lineHeight: { unit: 'PIXELS', value: 24 }, letterSpacing: { unit: 'PIXELS', value: 1 },
  fills: [{ type: 'SOLID', color: { r: 1, g: 0, b: 0 } }], hyperlink: null };
const special = { ...normal, fontName: { family: 'Example Serif', style: 'Bold Italic' },
  fontWeight: 700, fontStyle: 'ITALIC', fontSize: 30, textDecoration: 'UNDERLINE',
  lineHeight: { unit: 'PERCENT', value: 150 }, letterSpacing: { unit: 'PERCENT', value: 10 },
  fills: [{ type: 'SOLID', color: { r: 0, g: 0, b: 1 } }],
  hyperlink: { type: 'URL', value: 'https://example.com/' } };
const segments = [{ ...normal, start: 0, end: 1, characters: 'A' },
  { ...special, start: 1, end: 3, characters: '\uD83D\uDE00' },
  { ...normal, start: 3, end: 4, characters: 'B' }];
const node = { id: 'text', name: 'Mixed', type: 'TEXT', visible: true, characters: 'A\uD83D\uDE00B',
  fontName: figma.mixed, fontWeight: figma.mixed, fontSize: figma.mixed,
  textCase: 'ORIGINAL', textDecoration: figma.mixed, lineHeight: figma.mixed,
  letterSpacing: figma.mixed, fills: figma.mixed,
  getStyledTextSegments(fields) {
    for (const field of ['fontName', 'fontWeight', 'fontStyle', 'fontSize', 'fills', 'lineHeight', 'letterSpacing', 'textDecoration', 'hyperlink']) assert.ok(fields.includes(field), field);
    return segments;
  } };
const output = serializeNode(node);
assert.deepEqual(output.characterStyleOverrides, [1, 2, 2, 1], 'indices cover UTF-16 units and reuse equal styles');
assert.equal(output.style.fontFamily, 'Example Sans', 'base uses actual first style, not mixed defaults');
assert.equal(output.style.fontSize, 18);
const row = output.styleOverrideTable['2'];
assert.equal(row.fontFamily, 'Example Serif');
assert.equal(row.fontWeight, 700);
assert.equal(row.italic, true);
assert.equal(row.letterSpacing, 3, 'percentage spacing resolves to source pixels');
assert.equal(row.lineHeightPercentFontSize, 150);
assert.equal(row.textDecoration, 'UNDERLINE');
assert.deepEqual(row.hyperlink, { type: 'URL', url: 'https://example.com/' });
assert.equal(row.fills[0].color.b, 1);
const uniform = serializeNode({ ...node, ...normal, characters: 'A', getStyledTextSegments() { return [segments[0]]; } });
assert.equal(uniform.characterStyleOverrides, undefined, 'uniform text keeps its existing generation path');
console.log('Character style export tests passed.');
