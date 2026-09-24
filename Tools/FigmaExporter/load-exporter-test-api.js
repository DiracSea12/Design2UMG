// Expose the production bundle's Node test exports; no dynamic evaluation.
module.exports = function loadExporterTestApi() {
  return require('./code.js');
};
