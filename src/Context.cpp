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
    delete _context;
    _context = NULL;
  }

private:
  std::string config_json_;
  ContextHolder *_context = new ContextHolder();
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
  auto worker = new LoadWorker(env, stringify.Call({info[0]}).As<Napi::String>());
  worker->Queue();
  return worker->Promise();
}

void Context::Query(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_is_querying) {
    Napi::Error::New(env, "Context is busying").ThrowAsJavaScriptException();
    return;
  }
  _is_querying = true;
  const char *prompt = info[0].As<Napi::String>().Utf8Value().c_str();
  Napi::Function callback = info[1].As<Napi::Function>();
  _callback = Napi::Persistent(callback);
  Genie_Status_t status =
      GenieDialog_query(_context->dialog, prompt,
                        GENIE_DIALOG_SENTENCE_COMPLETE, on_response, this);
  if (status != GENIE_STATUS_SUCCESS) {
    _is_querying = false;
    Napi::Error::New(env, Genie_Status_ToString(status))
        .ThrowAsJavaScriptException();
  }
}

void Context::on_response(const char *response,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData) {
  Context *context = static_cast<Context *>(const_cast<void *>(userData));
  Napi::Env env = context->Env();
  Napi::HandleScope scope(env);
  Napi::Function callback = context->_callback.Value();
  callback.Call({Napi::String::New(env, response),
                 Napi::Number::New(env, sentenceCode)});
  if (sentenceCode == GENIE_DIALOG_SENTENCE_COMPLETE) {
    context->_is_querying = false;
  }
}
