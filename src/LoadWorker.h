#include "ContextHolder.h"
#include <napi.h>

class LoadWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  LoadWorker(Napi::Env env, std::string config_json);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string config_json_;
  ContextHolder *_context = NULL;
};