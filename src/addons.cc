#include "Context.h"
#include <napi.h>

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports = Context::Init(env, exports);
  return exports;
}

NODE_API_MODULE(addons, Init)
