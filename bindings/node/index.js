'use strict';

const path = require('path');

const addonPath = path.join(__dirname, 'build', 'carbon.node');

try {
  module.exports = require(addonPath);
} catch (error) {
  error.message = `Unable to load CarbonC Node binding at ${addonPath}. Run "bash build.sh" in bindings/node first.\n${error.message}`;
  throw error;
}
