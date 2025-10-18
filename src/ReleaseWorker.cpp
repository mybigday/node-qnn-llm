#include "ReleaseWorker.h"
#include "Context.h"
#include <stdexcept>

ReleaseWorker::ReleaseWorker(Napi::Env env, ContextHolder *context)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), _context(context) {}

ReleaseWorker::ReleaseWorker(Napi::Env env, EmbeddingsHolder *embedding)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), _embedding(embedding) {}

void ReleaseWorker::Execute() {
  try {
    if (_context) {
      _context->release();
      delete _context;
    }
    if (_embedding) {
      _embedding->release();
      delete _embedding;
    }
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void ReleaseWorker::OnOK() { Resolve(Napi::AsyncWorker::Env().Undefined()); }

void ReleaseWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
