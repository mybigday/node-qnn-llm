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
  ({ Context, Embedding } = require(arch === 'arm64' ? `@fugood/${pkgName}` : `./packages/${pkgName}`));
  const nativeCreate = Context.create;
  Context.create = async (config, { dialog_type } = {}) =>
    nativeCreate.call(Context, selectDialogType(config, dialog_type));
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

const DIALOG_TYPES = ['basic', 'ssd-q1', 'lade', 'spd', 'multistream', 'eaglet', 'kv-share'];
// Dialog types that run two engines, and the roles Genie requires for them
const MULTI_ENGINE_ROLES = {
  spd: ['draft', 'target'],
  eaglet: ['draft', 'target'],
  'kv-share': ['primary', 'secondary'],
};

// Genie rejects a config carrying settings for any dialog type other than
// `dialog.type`, so a config may hold several and we keep only the chosen one.
const selectDialogType = (config, dialogType) => {
  const dialog = config?.dialog;
  if (!dialog) throw new Error('Config is not a LLM dialog config');
  const type = dialogType ?? dialog.type ?? 'basic';
  if (!DIALOG_TYPES.includes(type)) {
    throw new Error(`Unknown dialog type '${type}' (supported: ${DIALOG_TYPES.join(', ')})`);
  }
  if (type !== 'basic' && type !== dialog.type && !dialog[type]) {
    throw new Error(`Config does not provide settings for dialog type '${type}'`);
  }
  let { engine } = dialog;
  const roles = MULTI_ENGINE_ROLES[type];
  if (roles) {
    if (!Array.isArray(engine) || !roles.every(role => engine.some(e => e.role === role))) {
      throw new Error(`Dialog type '${type}' needs engines with roles: ${roles.join(', ')}`);
    }
  } else if (Array.isArray(engine)) {
    const main = engine.find(e => e.role === 'target' || e.role === 'primary');
    if (!main) throw new Error(`Config has no target or primary engine for dialog type '${type}'`);
    // a single-engine dialog must not carry `role`
    const { role, ...rest } = main;
    engine = rest;
  }
  const selected = { ...dialog, type, engine };
  DIALOG_TYPES.filter(t => t !== type).forEach(t => delete selected[t]);
  return { ...config, dialog: selected };
};

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
  dialog_type,
}) => {
  await Context.unpack(bundle_path, unpack_dir);
  const config = JSON.parse(await fs.readFile(path.join(unpack_dir, 'config.json'), 'utf8'));
  if (!config.dialog) throw new Error('Config is not a LLM dialog config');
  preProcessConfig(config, unpack_dir, n_threads);
  return await Context.create(config, { dialog_type });
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
