#pragma once

#include "EmbeddingsHolder.h"
#include <napi.h>

class Embedding : public Napi::ObjectWrap<Embedding> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object &exports);

  static inline Napi::Object New(Napi::External<EmbeddingsHolder> embedding) {
    return constructor.New({embedding});
  }

  Embedding(const Napi::CallbackInfo &info);
  ~Embedding();

protected:
  // embedding.query(prompt: string, callback: (result: vector<float>) => void): Promise<string>
  Napi::Value Query(const Napi::CallbackInfo &info);
  // embedding.release(): Promise<void>
  Napi::Value Release(const Napi::CallbackInfo &info);

private:
  static Napi::FunctionReference constructor;
  EmbeddingsHolder *_embedding = NULL;
};
