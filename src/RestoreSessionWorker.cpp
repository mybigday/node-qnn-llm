#include "RestoreSessionWorker.h"
#include "Context.h"
#include <stdexcept>

RestoreSessionWorker::RestoreSessionWorker(Napi::Env env, std::string filename,
                                           ContextHolder *context)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), filename_(filename),
      _context(context) {}

void RestoreSessionWorker::Execute() {
  try {
    _context->restore(filename_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void RestoreSessionWorker::OnOK() {
  Resolve(Napi::AsyncWorker::Env().Undefined());
}

void RestoreSessionWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
