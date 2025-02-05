#include "ContextHolder.h"
#include <napi.h>

class QueryWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  QueryWorker(Napi::Env env, std::string prompt, ContextHolder *context,
              Napi::Function callback);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string prompt_;
  ContextHolder *_context;
  Napi::ThreadSafeFunction _tsfn;
};