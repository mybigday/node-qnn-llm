#include "SaveSessionWorker.h"
#include "Context.h"
#include <stdexcept>

SaveSessionWorker::SaveSessionWorker(Napi::Env env, std::string filename,
                                     ContextHolder *context)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), filename_(filename),
      _context(context) {}

void SaveSessionWorker::Execute() {
  try {
    _context->save(filename_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void SaveSessionWorker::OnOK() {
  Resolve(Napi::AsyncWorker::Env().Undefined());
}

void SaveSessionWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
