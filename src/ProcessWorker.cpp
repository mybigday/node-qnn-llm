#include "ProcessWorker.h"
#include "Context.h"
#include <stdexcept>

ProcessWorker::ProcessWorker(Napi::Env env, std::string prompt,
                         ContextHolder *context)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), prompt_(prompt),
      _context(context) {}

void ProcessWorker::Execute() {
  try {
    _context->process(prompt_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void ProcessWorker::OnOK() {
  Resolve(Napi::AsyncWorker::Env().Undefined());
}

void ProcessWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
