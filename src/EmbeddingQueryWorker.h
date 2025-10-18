#include "EmbeddingsHolder.h"
#include <napi.h>

class EmbeddingQueryWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  EmbeddingQueryWorker(Napi::Env env, std::string prompt, EmbeddingsHolder *embedding,
              Napi::Function callback);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string prompt_;
  EmbeddingsHolder *_embedding;
  Napi::ThreadSafeFunction _tsfn;
  std::string profile_json_;
};
