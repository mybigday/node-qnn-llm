#include "ContextHolder.h"
#include <napi.h>

class ReleaseWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  ReleaseWorker(Napi::Env env, ContextHolder *context);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  ContextHolder *_context;
};