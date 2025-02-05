#include "Context.h"
#include "ContextHolder.h"
#include "LoadWorker.h"
#include "QueryWorker.h"
#include "ReleaseWorker.h"
#include "RestoreSessionWorker.h"
#include "SaveSessionWorker.h"
#include <stdexcept>
#include <string>

Napi::FunctionReference Context::constructor;

Napi::Object Context::Init(Napi::Env env, Napi::Object &exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(
      env, "Context",
      {
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

Napi::Value Context::Release(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL) {
    Napi::Error::New(env, "Context is not initialized")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  auto worker = new ReleaseWorker(env, _context);
  worker->Queue();
  return worker->Promise();
}
