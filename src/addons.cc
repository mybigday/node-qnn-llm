#include "Context.h"
#include "Embedding.h"
#include <napi.h>

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports = Context::Init(env, exports);
  exports = Embedding::Init(env, exports);
  return exports;
}

NODE_API_MODULE(addons, Init)
