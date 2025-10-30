const fs = require('fs/promises');
const { existsSync } = require('fs');
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
let Embedding;
try {
  const { platform, arch } = process;
  const pkgName = `node-qnn-llm-${platform}-${arch}`;
  ({ Context, Embedding } = require(arch === 'arm64' ? pkgName : `./packages/${pkgName}`));
} catch {
  Context = new Proxy({}, {
    get: () => {
      throw new Error('Unsupported platform or failed to load native module');
    }
  });
  Embedding = new Proxy({}, {
    get: () => {
      throw new Error('Unsupported platform or failed to load native module');
    }
  });
}

const preProcessEngine = (engine, dir, n_threads) => {
  if (engine.backend.type === 'QnnHtp') {
    const extPath = path.join(dir, engine.backend.extensions);
    if (!existsSync(extPath)) {
      engine.backend.extensions = getHtpConfigFilePath();
    } else {
      engine.backend.extensions = extPath;
    }
    if (process.platform === 'win32') {
      engine.backend.QnnHtp['use-mmap'] = false;
    }
  }
  if (engine.model.type === 'binary') {
    const bins = engine.model.binary['ctx-bins'];
    engine.model.binary['ctx-bins'] = bins.map(bin => path.join(dir, bin));
  } else {
    const bin = engine.library['model-bin'];
    engine.library['model-bin'] = path.join(dir, bin);
  }
  if (n_threads && n_threads > 0) engine['n-threads'] = n_threads;
};

const preProcessConfig = (config, dir, n_threads) => {
  const model = config.dialog || config.embedding || config['text-generator'] || config['text-encoder'] || config['image-encoder'];
  if (!model) throw new Error('Invalid config');
  if (model.embedding && model.embedding.type === 'lut') {
    model.embedding['lut-path'] = path.join(dir, model.embedding['lut-path']);
  }
  if (model.lut) {
    model.lut['lut-path'] = path.join(dir, model.lut['lut-path']);
  }
  if (model.tokenizer) {
    model.tokenizer.path = path.join(dir, model.tokenizer.path);
  }
  if (Array.isArray(model.engine)) {
    model.engine.forEach(engine => preProcessEngine(engine, dir, n_threads));
  } else if (model.engine) {
    preProcessEngine(model.engine, dir, n_threads);
  }
};

Context.load = async ({
  bundle_path,
  unpack_dir,
  n_threads,
}) => {
  await Context.unpack(bundle_path, unpack_dir);
  const config = JSON.parse(await fs.readFile(path.join(unpack_dir, 'config.json'), 'utf8'));
  if (!config.dialog) throw new Error('Config is not a LLM dialog config');
  preProcessConfig(config, unpack_dir, n_threads);
  return await Context.create(config);
};

Embedding.load = async ({
  bundle_path,
  unpack_dir,
  n_threads,
}) => {
  await Context.unpack(bundle_path, unpack_dir);
  const config = JSON.parse(await fs.readFile(path.join(unpack_dir, 'config.json'), 'utf8'));
  if (!config.embedding) throw new Error('Config is not an embedding config');
  preProcessConfig(config, unpack_dir, n_threads);
  return await Embedding.create(config);
};

module.exports = {
  SentenceCode,
  Context,
  Embedding,
  getHtpConfigFilePath,
};
