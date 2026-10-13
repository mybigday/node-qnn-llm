#include "ContextHolder.h"
#include <napi.h>

class QueryWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  QueryWorker(Napi::Env env, QueryParams params, ContextHolder *context,
              Napi::Function callback);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  QueryParams params_;
  ContextHolder *_context;
  Napi::ThreadSafeFunction _tsfn;
  std::string text_;
  QueryResult result_;
};