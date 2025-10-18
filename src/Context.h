#pragma once

#include "ContextHolder.h"
#include <napi.h>

class Context : public Napi::ObjectWrap<Context> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object &exports);

  static inline Napi::Object New(Napi::External<ContextHolder> context) {
    return constructor.New({context});
  }

  Context(const Napi::CallbackInfo &info);
  ~Context();

protected:
  // Context.unpack(bundle_path: string, unpack_dir: string): Promise<void>
  static Napi::Value Unpack(const Napi::CallbackInfo &info);
  // Context.create(config_json: object): Promise<Context>
  static Napi::Value Create(const Napi::CallbackInfo &info);
  // context.set_stop_words(stop_words: string[]): void
  void SetStopWords(const Napi::CallbackInfo &info);
  // context.apply_sampler_config(config_json: object): void
  void ApplySamplerConfig(const Napi::CallbackInfo &info);
  // context.save_session(filename: string): void
  Napi::Value SaveSession(const Napi::CallbackInfo &info);
  // context.restore_session(filename: string): void
  Napi::Value RestoreSession(const Napi::CallbackInfo &info);
  // context.abort(): void
  void Abort(const Napi::CallbackInfo &info);
  // context.query(prompt: string, callback: (result: string) => void):
  // Promise<string>
  Napi::Value Query(const Napi::CallbackInfo &info);
  // context.release(): Promise<void>
  Napi::Value Release(const Napi::CallbackInfo &info);

  void releaseContext();

private:
  static Napi::FunctionReference constructor;
  ContextHolder *_context = NULL;
};
