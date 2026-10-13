#include "ContextHolder.h"
#include <napi.h>

class LoadWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  LoadWorker(Napi::Env env, std::string config_json, QueryParams defaults,
             std::string dialog_type);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string config_json_;
  QueryParams defaults_;
  std::string dialog_type_;
  ContextHolder *_context = NULL;
};