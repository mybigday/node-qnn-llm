# node-qnn-llm

Qualcomm AI Direct libGenie binding for Node.js

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
