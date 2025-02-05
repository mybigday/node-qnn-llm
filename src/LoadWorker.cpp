#include "LoadWorker.h"
#include "Context.h"
#include <stdexcept>

LoadWorker::LoadWorker(Napi::Env env, std::string config_json)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
      config_json_(config_json) {}

void LoadWorker::Execute() {
  try {
    _context = new ContextHolder(config_json_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void LoadWorker::OnOK() {
  Resolve(Context::New(
      Napi::External<ContextHolder>::New(Napi::AsyncWorker::Env(), _context)));
}

void LoadWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
