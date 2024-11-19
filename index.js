const SentenceCode = {
  Complete: 0,
  Begin: 1,
  Continue: 2,
  End: 3,
  Abort: 4,
};

const getHtpConfigFilePath = () => {
  return require.resolve('./htp_backend_ext_config.json');
};

if (process.platform === 'win32' && process.arch === 'arm64') {
  const binding = require('./dist/node-qnn-llm.node');
  module.exports = {
    SentenceCode,
    Context: binding.Context,
    getHtpConfigFilePath,
  };
} else {
  module.exports = {
    SentenceCode,
    Context: new Proxy({}, {
      get: () => {
        throw new Error('Unsupported platform');
      }
    }),
    getHtpConfigFilePath,
  };
}
