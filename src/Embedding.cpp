#include "Embedding.h"
#include "EmbeddingsHolder.h"
#include "EmbeddingQueryWorker.h"
#include "ReleaseWorker.h"
#include <stdexcept>
#include <string>

Napi::FunctionReference Embedding::constructor;

Napi::Object Embedding::Init(Napi::Env env, Napi::Object &exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(
      env, "Embedding",
      {
          InstanceMethod<&Embedding::Query>(
              "query", static_cast<napi_property_attributes>(
                           napi_writable | napi_configurable)),
          InstanceMethod<&Embedding::Release>(
              "release", static_cast<napi_property_attributes>(
                             napi_writable | napi_configurable)),
      });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("Embedding", func);
  return exports;
}

Embedding::Embedding(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Embedding>(info) {
  Napi::HandleScope scope(info.Env());
  _embedding = info[0].As<Napi::External<EmbeddingsHolder>>().Data();
}

Embedding::~Embedding() {
  if (_embedding) {
    delete _embedding;
  }
}

Napi::Value Embedding::Query(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_embedding == NULL) {
    Napi::Error::New(env, "Embedding is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  std::string prompt = info[0].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[1].As<Napi::Function>();
  auto worker = new EmbeddingQueryWorker(env, prompt, _embedding, callback);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Embedding::Release(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_embedding == NULL) {
    Napi::Error::New(env, "Embedding is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto worker = new ReleaseWorker(env, _embedding);
  worker->Queue();
  return worker->Promise();
}
