#include "LoadWorker.h"
#include "Context.h"
#include <stdexcept>

LoadWorker::LoadWorker(Napi::Env env, std::string config_json,
                       QueryParams defaults, std::string dialog_type)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
      config_json_(config_json), defaults_(std::move(defaults)),
      dialog_type_(std::move(dialog_type)) {}

void LoadWorker::Execute() {
  try {
    _context = new ContextHolder(config_json_, defaults_, dialog_type_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void LoadWorker::OnOK() {
  Resolve(Context::New(
      Napi::External<ContextHolder>::New(Napi::AsyncWorker::Env(), _context)));
}

void LoadWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
