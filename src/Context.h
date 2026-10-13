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
  // context.get_context_occupancy(): number
  Napi::Value GetContextOccupancy(const Napi::CallbackInfo &info);
  // context.get_applied_lora_adapter(): string
  Napi::Value GetAppliedLoraAdapter(const Napi::CallbackInfo &info);
  // context.apply_lora(engine: string, lora_adapter_name: string): void
  void ApplyLora(const Napi::CallbackInfo &info);
  // context.set_lora_strength(engine: string, lora_strength: Record<string, number>): void
  void SetLoraStrength(const Napi::CallbackInfo &info);
  // context.release_lora_memory(engine: string, lora_adapter_name: string): void
  void ReleaseLoraMemory(const Napi::CallbackInfo &info);
  // context.save_session(filename: string): void
  Napi::Value SaveSession(const Napi::CallbackInfo &info);
  // context.restore_session(filename: string): void
  Napi::Value RestoreSession(const Napi::CallbackInfo &info);
  // context.abort(): void
  void Abort(const Napi::CallbackInfo &info);
  // context.query(params: { prompt, stop?, n_predict?, temperature?, top_k?, top_p?, seed?,
  //   grammar?, json_schema?, regex? }, callback?: (text, sentenceCode) => void):
  // Promise<{ text, tokens_evaluated, tokens_predicted, interrupted, timings, profile }>
  Napi::Value Query(const Napi::CallbackInfo &info);
  // context.release(): Promise<void>
  Napi::Value Release(const Napi::CallbackInfo &info);

  void releaseContext();

private:
  static Napi::FunctionReference constructor;
  ContextHolder *_context = NULL;
};
