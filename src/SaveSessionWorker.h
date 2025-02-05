#include "ContextHolder.h"
#include <napi.h>

class SaveSessionWorker : public Napi::AsyncWorker,
                          public Napi::Promise::Deferred {
public:
  SaveSessionWorker(Napi::Env env, std::string filename,
                    ContextHolder *context);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string filename_;
  ContextHolder *_context;
};