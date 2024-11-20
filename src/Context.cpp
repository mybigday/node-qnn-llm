#include "Context.h"
#include "utils.h"
#include <string>

/* LoadWorker */

class LoadWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  LoadWorker(Napi::Env env, std::string config_json)
      : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
        config_json_(config_json) {}

protected:
  void Execute() {
    Genie_Status_t status;
    status = GenieDialogConfig_createFromJson(config_json_.c_str(),
                                              &_context->config);
    if (status != GENIE_STATUS_SUCCESS) {
      SetError(Genie_Status_ToString(status));
      return;
    }
    status = GenieDialog_create(_context->config, &_context->dialog);
    if (status != GENIE_STATUS_SUCCESS) {
      SetError(Genie_Status_ToString(status));
      return;
    }
  }

  void OnOK() {
    Resolve(Context::New(Napi::External<ContextHolder>::New(
        Napi::AsyncWorker::Env(), _context)));
  }

  void OnError(const Napi::Error &e) {
    Reject(e.Value());
    if (_context->config) {
      GenieDialogConfig_free(_context->config);
    }
    if (_context->dialog) {
      GenieDialog_free(_context->dialog);
    }
    delete _context;
    _context = NULL;
  }

private:
  std::string config_json_;
  ContextHolder *_context = new ContextHolder();
};

/* QueryWorker */

class QueryWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  QueryWorker(Napi::Env env, std::string prompt, GenieDialog_Handle_t dialog,
              Napi::Function callback)
      : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), prompt_(prompt),
        dialog_(dialog) {
    _tsfn = Napi::ThreadSafeFunction::New(env, callback, "QueryWorkerCallback", 0, 1);
  }

  ~QueryWorker() {
    _tsfn.Release();
  }

protected:
  void Execute() {
    Genie_Status_t status =
        GenieDialog_query(dialog_, prompt_.c_str(),
                          GENIE_DIALOG_SENTENCE_COMPLETE, on_response, this);
    if (status != GENIE_STATUS_SUCCESS) {
      SetError(Genie_Status_ToString(status));
      return;
    }
  }

  void OnOK() {
    Resolve(Napi::AsyncWorker::Env().Undefined());
  }

  void OnError(const Napi::Error &e) {
    Reject(e.Value());
  }

  static void on_response(const char *response,
                         const GenieDialog_SentenceCode_t sentenceCode,
                         const void *userData) {
    auto self = static_cast<QueryWorker *>(const_cast<void *>(userData));
    if (self->_tsfn.Acquire() != napi_ok) {
      printf("callback function is not available\n");
      return;
    }
    self->_tsfn.BlockingCall(response, [sentenceCode](Napi::Env env, Napi::Function callback, const char* value) {
      Napi::HandleScope scope(env);
      callback.Call({Napi::String::New(env, value), Napi::Number::New(env, sentenceCode)});
    });
  }

private:
  std::string prompt_;
  GenieDialog_Handle_t dialog_;
  Napi::ThreadSafeFunction _tsfn;
};

/* Context */

Napi::FunctionReference Context::constructor;

Napi::Object Context::Init(Napi::Env env, Napi::Object &exports) {
  Napi::HandleScope scope(env);
  Napi::Function func =
      DefineClass(env, "Context",
                  {
                      StaticMethod<&Context::Create>(
                          "create", static_cast<napi_property_attributes>(
                                        napi_writable | napi_configurable)),
                      InstanceMethod<&Context::Query>(
                          "query", static_cast<napi_property_attributes>(
                                       napi_writable | napi_configurable)),
                      InstanceMethod<&Context::Abort>(
                          "abort", static_cast<napi_property_attributes>(
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
    GenieDialog_free(_context->dialog);
    GenieDialogConfig_free(_context->config);
    delete _context;
  }
}

Napi::Value Context::Create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function stringify = JSON.Get("stringify").As<Napi::Function>();
  std::string config_json = stringify.Call({info[0]}).As<Napi::String>().Utf8Value();
  auto worker = new LoadWorker(env, config_json);
  worker->Queue();
  return worker->Promise();
}

Napi::Value Context::Query(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_context == NULL || _context->dialog == NULL) {
    Napi::Error::New(env, "Context is not initialized").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  std::string prompt = info[0].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[1].As<Napi::Function>();
  auto worker = new QueryWorker(env, prompt, _context->dialog, callback);
  worker->Queue();
  return worker->Promise();
}

void Context::Abort(const Napi::CallbackInfo &info) {
  if (_context) {
    GenieDialog_query(_context->dialog, "", GENIE_DIALOG_SENTENCE_ABORT,
                      NULL, NULL);
  }
}

void Context::Release(const Napi::CallbackInfo &info) {
  if (_context) {
    GenieDialog_free(_context->dialog);
    GenieDialogConfig_free(_context->config);
    delete _context;
    _context = NULL;
  }
}
