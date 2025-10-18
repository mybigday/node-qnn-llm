#include "ContextHolder.h"
#include "EmbeddingsHolder.h"
#include <napi.h>

class ReleaseWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  ReleaseWorker(Napi::Env env, ContextHolder *context);
  ReleaseWorker(Napi::Env env, EmbeddingsHolder *embedding);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  ContextHolder *_context = NULL;
  EmbeddingsHolder *_embedding = NULL;
};
