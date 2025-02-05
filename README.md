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
