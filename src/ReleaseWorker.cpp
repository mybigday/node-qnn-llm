#include "ReleaseWorker.h"
#include "Context.h"
#include <stdexcept>

ReleaseWorker::ReleaseWorker(Napi::Env env, ContextHolder *context)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), _context(context) {}

void ReleaseWorker::Execute() {
  try {
    _context->release();
    delete _context;
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void ReleaseWorker::OnOK() { Resolve(Napi::AsyncWorker::Env().Undefined()); }

void ReleaseWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
