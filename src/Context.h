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
  // Context.load(config_json: string): Promise<Context>
  static Napi::Value Load(const Napi::CallbackInfo &info);
  // context.query(prompt: string, callback: (result: string) => void): void
  void Query(const Napi::CallbackInfo &info);

  static void on_response(const char *response,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData);

private:
  static Napi::FunctionReference constructor;
  ContextHolder *_context = NULL;
  Napi::FunctionReference _callback;
  bool _is_querying = false;
};
