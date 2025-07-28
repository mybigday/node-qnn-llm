#include "UnpackWorker.h"
#include "unpack.h"
#include <stdexcept>

UnpackWorker::UnpackWorker(Napi::Env env, std::string bundle_path, std::string unpack_dir)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
      bundle_path_(bundle_path), unpack_dir_(unpack_dir) {}

void UnpackWorker::Execute() {
  try {
    unpackModel(bundle_path_, unpack_dir_);
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void UnpackWorker::OnOK() {
  Resolve(Napi::AsyncWorker::Env().Undefined());
}

void UnpackWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
