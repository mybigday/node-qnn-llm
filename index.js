const fs = require('fs/promises');
const path = require('path');

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
  const { Context } = require('./dist/node-qnn-llm.node');

  Context.load = async ({
    bundlePath,
    unpackDir,
    n_threads: nThreads,
  }) => {
    if (!(await fs.exists(path.join(unpackDir, 'config.json')))) {
      await Context.unpack(bundlePath, unpackDir);
    }
    const config = JSON.parse(await fs.readFile(path.join(unpackDir, 'config.json'), 'utf8'));
    // Hard patch config.json
    if (config.dialog.engine.backend.type === 'QnnHtp') {
      config.dialog.engine.backend.extensions = getHtpConfigFilePath();
      config.dialog.engine.backend.QnnHtp['use-mmap'] = false;
    }
    config.dialog.tokenizer.path = path.join(unpackDir, config.dialog.tokenizer.path);
    if (config.dialog.engine.model.type === 'binary') {
      const bins = config.dialog.engine.model.binary['ctx-bins'];
      config.dialog.engine.model.binary['ctx-bins'] = bins.map(bin => path.join(unpackDir, bin));
    } else {
      const bin = config.dialog.engine.library['model-bin'];
      config.dialog.engine.library['model-bin'] = path.join(unpackDir, bin);
    }
    if (nThreads && nThreads > 0) config.dialog.engine['n-threads'] = nThreads;
    return await Context.create(config);
  };

  module.exports = {
    SentenceCode,
    Context,
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
