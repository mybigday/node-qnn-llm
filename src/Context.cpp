#include "Context.h"
#include "ContextHolder.h"
#include "LoadWorker.h"
#include "QueryWorker.h"
#include "ReleaseWorker.h"
#include "RestoreSessionWorker.h"
#include "SaveSessionWorker.h"
#include "UnpackWorker.h"
#include <stdexcept>
#include <string>

namespace {
Napi::Value Stringify(Napi::Env env, Napi::Value value) {
  Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
  return JSON.Get("stringify").As<Napi::Function>().Call({value});
}

std::string StopJson(Napi::Env env, Napi::Value stop) {
  return "{\"stop-sequence\": " +
         Stringify(env, stop).As<Napi::String>().Utf8Value() + "}";
}

void ReadSampler(Napi::Object obj, const char *temperature, const char *top_k,
                 const char *top_p, const char *seed, SamplerParams &sampler) {
  if (obj.Get(temperature).IsNumber())
    sampler.temperature = obj.Get(temperature).As<Napi::Number>().FloatValue();
  if (obj.Get(top_k).IsNumber())
    sampler.top_k = obj.Get(top_k).As<Napi::Number>().Int32Value();
  if (obj.Get(top_p).IsNumber())
    sampler.top_p = obj.Get(top_p).As<Napi::Number>().FloatValue();
  if (obj.Get(seed).IsNumber())
    sampler.seed = obj.Get(seed).As<Napi::Number>().Int32Value();
}

// Per-query values the dialog starts with, so a query that omits a param
// falls back to the config instead of whatever the previous query set.
QueryParams ConfigDefaults(Napi::Env env, Napi::Value config) {
  QueryParams defaults;
  if (!config.IsObject()) {
    return defaults;
  }
  Napi::Value dialog = config.As<Napi::Object>().Get("dialog");
  if (!dialog.IsObject()) {
    return defaults;
  }
  Napi::Object dialogObj = dialog.As<Napi::Object>();
  if (dialogObj.Get("stop-sequence").IsArray()) {
    defaults.stop_json = StopJson(env, dialogObj.Get("stop-sequence"));
  }
  if (dialogObj.Get("max-num-tokens").IsNumber()) {
    defaults.max_tokens =
        dialogObj.Get("max-num-tokens").As<Napi::Number>().Uint32Value();
  }
  if (dialogObj.Get("sampler").IsObject()) {
    ReadSampler(dialogObj.Get("sampler").As<Napi::Object>(), "temp", "top-k",
                "top-p", "seed", defaults.sampler);
  }
  Napi::Value context = dialogObj.Get("context");
  if (context.IsObject()) {
    Napi::Value grammar = context.As<Napi::Object>().Get("grammar");
    if (grammar.IsObject()) {
      Napi::Object grammarObj = grammar.As<Napi::Object>();
      // qualla only builds the init-time grammar when a backend is set
      if (grammarObj.Get("backend").IsString() &&
          grammarObj.Get("file").IsString()) {
        defaults.grammar.kind = grammarObj.Get("type").IsString()
            ? grammarObj.Get("type").As<Napi::String>().Utf8Value()
            : "json-schema";
        defaults.grammar.file =
            grammarObj.Get("file").As<Napi::String>().Utf8Value();
      }
    }
  }
  return defaults;
}
}  // namespace

Napi::FunctionReference Context::constructor;

Napi::Object Context::Init(Napi::Env env, Napi::Object &exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(
      env, "Context",
      {
          StaticMethod<&Context::Unpack>(
              "unpack", static_cast<napi_property_attributes>(
                           napi_writable | napi_configurable)),
          StaticMethod<&Context::Create>(
              "create", static_cast<napi_property_attributes>(
                            napi_writable | napi_configurable)),
          InstanceMethod<&Context::Query>(
              "query", static_cast<napi_property_attributes>(
                           napi_writable | napi_configurable)),
          InstanceMethod<&Context::SaveSession>(
              "save_session", static_cast<napi_property_attributes>(
                                  napi_writable | napi_configurable)),
          InstanceMethod<&Context::RestoreSession>(
              "restore_session", static_cast<napi_property_attributes>(
                                     napi_writable | napi_configurable)),
          InstanceMethod<&Context::Abort>(
              "abort", static_cast<napi_property_attributes>(
                           napi_writable | napi_configurable)),
          InstanceMethod<&Context::GetContextOccupancy>(
              "get_context_occupancy", static_cast<napi_property_attributes>(
                                            napi_writable | napi_configurable)),
          InstanceMethod<&Context::GetAppliedLoraAdapter>(
              "get_applied_lora_adapter", static_cast<napi_property_attributes>(
                                               napi_writable | napi_configurable)),
          InstanceMethod<&Context::ApplyLora>(
              "apply_lora", static_cast<napi_property_attributes>(
                                napi_writable | napi_configurable)),
          InstanceMethod<&Context::SetLoraStrength>(
              "set_lora_strength", static_cast<napi_property_attributes>(
                                        napi_writable | napi_configurable)),
          InstanceMethod<&Context::ReleaseLoraMemory>(
              "release_lora_memory", static_cast<napi_property_attributes>(
                                          napi_writable | napi_configurable)),
          InstanceMethod<&Context::Release>(
              "release", static_cast<napi_property_attributes>(
                             napi_writable | napi_configurable)),
      });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("Context", func);
  return exports;
}

Context::Context(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<Context>(info) {
  Napi::HandleScope scope(info.Env());
  _context = info[0].As<Napi::External<ContextHolder>>().Data();
}

Context::~Context() {
  if (_context) {
    delete _context;
  }
}

Napi::Value Context::Unpack(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  std::string bundle_path = info[0].As<Napi::String>().Utf8Value();
  std::string unpack_dir = info[1].As<Napi::String>().Utf8Value();
  auto worker = new UnpackWorker(env, bundle_path, unpack_dir);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Context::Create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function stringify = JSON.Get("stringify").As<Napi::Function>();
  std::string config_json =
      stringify.Call({info[0]}).As<Napi::String>().Utf8Value();
  std::string dialog_type = "basic";
  if (info[0].IsObject()) {
    Napi::Value dialog = info[0].As<Napi::Object>().Get("dialog");
    if (dialog.IsObject() && dialog.As<Napi::Object>().Get("type").IsString()) {
      dialog_type = dialog.As<Napi::Object>().Get("type").As<Napi::String>().Utf8Value();
    }
  }
  auto worker = new LoadWorker(env, config_json, ConfigDefaults(env, info[0]),
                               dialog_type);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Context::Query(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  if (!info[0].IsObject() ||
      !info[0].As<Napi::Object>().Get("prompt").IsString()) {
    Napi::Error::New(env, "params.prompt must be a string")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  Napi::Object obj = info[0].As<Napi::Object>();
  QueryParams params = _context->defaults();
  params.prompt = obj.Get("prompt").As<Napi::String>().Utf8Value();

  Napi::Value stop = obj.Get("stop");
  if (stop.IsArray()) {
    params.stop_json = StopJson(env, stop);
  } else if (!stop.IsUndefined() && !stop.IsNull()) {
    Napi::Error::New(env, "params.stop must be an array of strings")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // Like llama.rn: n_predict < 0 means no limit.
  Napi::Value n_predict = obj.Get("n_predict");
  if (n_predict.IsNumber()) {
    double n = n_predict.As<Napi::Number>().DoubleValue();
    params.max_tokens = n < 0 || n >= UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(n);
  } else if (!n_predict.IsUndefined() && !n_predict.IsNull()) {
    Napi::Error::New(env, "params.n_predict must be a number")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  ReadSampler(obj, "temperature", "top_k", "top_p", "seed", params.sampler);

  // Same precedence as llama.rn: grammar > json_schema; regex is Genie-only.
  Napi::Value grammar = obj.Get("grammar");
  Napi::Value json_schema = obj.Get("json_schema");
  Napi::Value regex = obj.Get("regex");
  if (grammar.IsString()) {
    params.grammar = {"ebnf", grammar.As<Napi::String>().Utf8Value(), ""};
  } else if (json_schema.IsString()) {
    params.grammar = {"json-schema", json_schema.As<Napi::String>().Utf8Value(), ""};
  } else if (json_schema.IsObject()) {
    params.grammar = {"json-schema",
                      Stringify(env, json_schema).As<Napi::String>().Utf8Value(), ""};
  } else if (regex.IsString()) {
    params.grammar = {"regex", regex.As<Napi::String>().Utf8Value(), ""};
  }

  Napi::Function callback = info[1].IsFunction()
      ? info[1].As<Napi::Function>()
      : Napi::Function::New(env, [](const Napi::CallbackInfo &) {});
  auto worker = new QueryWorker(env, std::move(params), _context, callback);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Context::SaveSession(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  std::string filename = info[0].As<Napi::String>().Utf8Value();
  auto worker = new SaveSessionWorker(env, filename, _context);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Context::RestoreSession(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  std::string filename = info[0].As<Napi::String>().Utf8Value();
  auto worker = new RestoreSessionWorker(env, filename, _context);
  worker->Queue();
  return worker->Promise();
}

void Context::Abort(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  try {
    _context->abort();
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
}

Napi::Value Context::GetContextOccupancy(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  try {
    return Napi::Number::New(env, _context->get_context_occupancy());
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Undefined();
  }
}

Napi::Value Context::GetAppliedLoraAdapter(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  try {
    return Napi::String::New(env, _context->get_applied_lora_adapter());
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Undefined();
  }
}

void Context::ApplyLora(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  std::string engine = info[0].As<Napi::String>().Utf8Value();
  std::string lora_adapter_name = info[1].As<Napi::String>().Utf8Value();
  try {
    _context->apply_lora(engine, lora_adapter_name);
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
}

void Context::SetLoraStrength(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  std::string engine = info[0].As<Napi::String>().Utf8Value();
  Napi::Object lora_strength_obj = info[1].As<Napi::Object>();
  LoraStrengthMap lora_strength_map;
  Napi::Array keys = lora_strength_obj.GetPropertyNames();
  for (uint32_t i = 0; i < keys.Length(); i++) {
    std::string tensor_name = keys.Get(i).As<Napi::String>().Utf8Value();
    float alpha = lora_strength_obj.Get(tensor_name).As<Napi::Number>().FloatValue();
    lora_strength_map[tensor_name] = alpha;
  }
  try {
    _context->set_lora_strength(engine, lora_strength_map);
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
}

void Context::ReleaseLoraMemory(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  std::string engine = info[0].As<Napi::String>().Utf8Value();
  std::string lora_adapter_name = info[1].As<Napi::String>().Utf8Value();
  try {
    _context->release_lora_memory(engine, lora_adapter_name);
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
}

Napi::Value Context::Release(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto worker = new ReleaseWorker(env, _context);
  _context = NULL;
  worker->Queue();
  return worker->Promise();
}
