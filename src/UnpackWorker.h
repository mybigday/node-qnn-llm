#pragma once

#include <string>
#include <napi.h>

class UnpackWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  UnpackWorker(Napi::Env env, std::string bundle_path, std::string unpack_dir);
  void Execute();
  void OnOK();
  void OnError(const Napi::Error &e);

private:
  std::string bundle_path_;
  std::string unpack_dir_;
};
