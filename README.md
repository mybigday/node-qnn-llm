# node-qnn-llm

Qualcomm AI Direct libGenie binding for Node.js

## Configure

### Windows

- Env `PATH` should able found
  - Libraries from `<sdk-root>/lib/aarch64-windows-msvc`
    - `QnnSystem.dll`
    - `QnnHtp.dll`
    - `QnnHtpPrepare.dll`
    - `QnnHtpV*Stub.dll`
  - And `libQnnHtpV*Skel.so`, `libqnnhtpv73.cat` from `lib/hexagon-v*/unsigned`

### Linux

- Env `ADSP_LIBRARY_PATH` should able found `libQnnHtpV*Skel.so`
- Env `LD_LIBRARY_PATH` should able found libraries from `<sdk-root>/lib/aarch64-oe-linux-gcc11.2`
  - `libQnnSystem.so`
  - `libQnnHtp.so`
  - `libQnnHtpPrepare.so`
  - `libQnnHtpV*Stub.so`

## Usage

```javascript
import { Context, SentenceCode } from 'node-qnn-llm';

const context = await Context.create(/* Genie config object */);
// Or load bundled
// const context = await Context.load({ bundle_path: 'path/to/bundle', unpack_dir: 'path/to/store/unpacked', n_thread?: Number })

await context.query('Hello, world!', (result, sentenceCode) => {
  console.log(result);
});

await context.save_session('path/to/session-directory');

await context.restore_session('path/to/session-directory');

await context.set_stop_words(['stop_word1', 'stop_word2']);

await context.apply_sampler_config({
  /* Genie sampler config */
});

await context.release();
```

## Bundled File

To easier to deploy model, we announced packed file struct.

- Constant entry config path.
- Auto resolve file path.
- Patch config on load.

You can quickly pack your model files use [pack.py](pack.py).

Usage: `./pack.py path/to/config.json`

## License

MIT

---

<p align="center">
  <a href="https://bricks.tools">
    <img width="90px" src="https://avatars.githubusercontent.com/u/17320237?s=200&v=4">
  </a>
  <p align="center">
    Built and maintained by <a href="https://bricks.tools">BRICKS</a>.
  </p>
</p>
