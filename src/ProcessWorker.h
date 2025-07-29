#include "ContextHolder.h"
#include <napi.h>

class ProcessWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  ProcessWorker(Napi::Env env, std::string prompt, ContextHolder *context);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string prompt_;
  ContextHolder *_context;
};