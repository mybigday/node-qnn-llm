# node-qnn-llm

QNN LLM binding for Node.js

## Usage

```javascript
import { Context, SentenceCode } from 'node-qnn-llm';

const context = await Context.create(/* Genie config */);

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
