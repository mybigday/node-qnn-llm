
if (process.env.NODE_PLATFORM === 'win32' && process.env.NODE_ARCH === 'arm64') {
  module.exports = require('./dist/node-qnn-llm.node');
} else {
  module.exports = new Proxy({}, {
    get: () => {
      throw new Error('Unsupported platform');
    }
  });
}
