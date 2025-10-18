#include "EmbeddingQueryWorker.h"
#include "EmbeddingsHolder.h"
#include <stdexcept>
#include <memory>

EmbeddingQueryWorker::EmbeddingQueryWorker(Napi::Env env, std::string prompt,
                         EmbeddingsHolder *embedding, Napi::Function callback)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), prompt_(prompt),
      _embedding(embedding) {
  _tsfn =
      Napi::ThreadSafeFunction::New(env, callback, "EmbeddingQueryWorkerCallback", 0, 1);
}

void EmbeddingQueryWorker::Execute() {
  try {
    profile_json_ = _embedding->query(
        prompt_,
        [this](std::vector<float> embedding) {
          std::unique_ptr<std::vector<float>> embedding_ptr = std::make_unique<std::vector<float>>(embedding);
          this->_tsfn.NonBlockingCall(embedding_ptr.release(), [this](Napi::Env env, Napi::Function callback, std::vector<float>* embedding_ptr) {
            Napi::HandleScope scope(env);
            Napi::Float32Array array = Napi::Float32Array::New(env, embedding_ptr->size());
            std::copy(embedding_ptr->begin(), embedding_ptr->end(), array.Data());
            delete embedding_ptr;
            callback.Call({array});
          });
        });
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void EmbeddingQueryWorker::OnOK() {
  if (!profile_json_.empty()) {
    Napi::Env env = Napi::AsyncWorker::Env();
    Napi::HandleScope scope(env);
    Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
    Napi::Function parse = JSON.Get("parse").As<Napi::Function>();
    Napi::Value result = parse.Call({Napi::String::New(env, profile_json_)});
    Resolve(result);
  } else {
    Resolve(Napi::AsyncWorker::Env().Undefined());
  }
}

void EmbeddingQueryWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
