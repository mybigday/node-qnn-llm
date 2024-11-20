#pragma once

#include "GenieDialog.h"
#include <napi.h>

struct ContextHolder {
  GenieDialogConfig_Handle_t config = NULL;
  GenieDialog_Handle_t dialog = NULL;
};

class Context : public Napi::ObjectWrap<Context> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object &exports);

  static inline Napi::Object New(Napi::External<ContextHolder> context) {
    return constructor.New({context});
  }

  Context(const Napi::CallbackInfo &info);
  ~Context();

protected:
  // Context.create(config_json: object): Promise<Context>
  static Napi::Value Create(const Napi::CallbackInfo &info);
  // context.query(prompt: string, callback: (result: string) => void): Promise<void>
  Napi::Value Query(const Napi::CallbackInfo &info);
  // context.abort(): void
  void Abort(const Napi::CallbackInfo &info);
  // context.release(): void
  void Release(const Napi::CallbackInfo &info);

private:
  static Napi::FunctionReference constructor;
  ContextHolder *_context = NULL;
};
