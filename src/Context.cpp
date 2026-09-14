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
          InstanceMethod<&Context::SetStopWords>(
              "set_stop_words", static_cast<napi_property_attributes>(
                                    napi_writable | napi_configurable)),
          InstanceMethod<&Context::ApplySamplerConfig>(
              "apply_sampler_config", static_cast<napi_property_attributes>(
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
  auto worker = new LoadWorker(env, config_json);
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
  std::string prompt = info[0].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[1].As<Napi::Function>();
  auto worker = new QueryWorker(env, prompt, _context, callback);
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

void Context::SetStopWords(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function stringify = JSON.Get("stringify").As<Napi::Function>();
  std::string config_json = "";
  if (info[0].IsArray()) {
    config_json = "{\"stop-sequence\": " +
                  stringify.Call({info[0]}).As<Napi::String>().Utf8Value() +
                  "}";
  } else if (info[0].IsNull() || info[0].IsUndefined()) {
    config_json = "{}";
  } else {
    Napi::Error::New(env, "Invalid argument").ThrowAsJavaScriptException();
    return;
  }
  try {
    _context->set_stop_words(config_json);
  } catch (const std::runtime_error &e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
  }
}

void Context::ApplySamplerConfig(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return;
  }
  Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function stringify = JSON.Get("stringify").As<Napi::Function>();
  std::string config_json =
      stringify.Call({info[0]}).As<Napi::String>().Utf8Value();
  try {
    _context->apply_sampler_config(config_json);
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
