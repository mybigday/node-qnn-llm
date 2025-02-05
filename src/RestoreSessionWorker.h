#include "ContextHolder.h"
#include <napi.h>

class RestoreSessionWorker : public Napi::AsyncWorker,
                             public Napi::Promise::Deferred {
public:
  RestoreSessionWorker(Napi::Env env, std::string filename,
                       ContextHolder *context);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string filename_;
  ContextHolder *_context;
};