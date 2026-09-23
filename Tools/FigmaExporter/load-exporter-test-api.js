const fs = require('node:fs');
const path = require('node:path');

// Evaluate the production bundle in function scope; expose internals only to tests.
module.exports = function loadExporterTestApi() {
  const source = fs.readFileSync(path.join(__dirname, 'code.js'), 'utf8');
  return new Function(`${source}\nreturn {
    attachRenderFallbackMetadata, indexRestNodes, makeRenderFallbackKey,
    serializePaint, serializeEffect, buildTextStyle, serializeNode,
    unwrapRestExportResponse, collectReferencedComponents,
    collectComponentDependencies, collectComponentDependencyImages,
    collectImages, exportReferencePng, shouldExportNodeRenderFallback,
  };`)();
};
