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

let Context;
try {
  ({ Context } = require('./dist/node-qnn-llm.node'));
} catch {
  Context = new Proxy({}, {
    get: () => {
      throw new Error('Unsupported platform');
    }
  });
}

const exists = async (path) => {
  try {
    await fs.stat(path);
    return true;
  } catch {
    return false;
  }
};

Context.load = async ({
  bundle_path,
  unpack_dir,
  n_threads,
}) => {
  await Context.unpack(bundle_path, unpack_dir);
  const config = JSON.parse(await fs.readFile(path.join(unpack_dir, 'config.json'), 'utf8'));
  if (config.dialog.engine.backend.type === 'QnnHtp') {
    config.dialog.engine.backend.extensions = getHtpConfigFilePath();
    config.dialog.engine.backend.QnnHtp['use-mmap'] = process.platform !== 'win32';
  }
  config.dialog.tokenizer.path = path.join(unpack_dir, config.dialog.tokenizer.path);
  if (config.dialog.engine.model.type === 'binary') {
    const bins = config.dialog.engine.model.binary['ctx-bins'];
    config.dialog.engine.model.binary['ctx-bins'] = bins.map(bin => path.join(unpack_dir, bin));
  } else {
    const bin = config.dialog.engine.library['model-bin'];
    config.dialog.engine.library['model-bin'] = path.join(unpack_dir, bin);
  }
  if (n_threads && n_threads > 0) config.dialog.engine['n-threads'] = n_threads;
  return await Context.create(config);
};

module.exports = {
  SentenceCode,
  Context,
  getHtpConfigFilePath,
};
