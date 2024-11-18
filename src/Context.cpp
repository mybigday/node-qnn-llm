#include "Context.h"
#include <string>

/* LoadWorker */

class LoadWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  LoadWorker(Napi::Env env, std::string config_json)
      : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), config_json_(config_json) {}

protected:
  void Execute() {
    Genie_Status_t status;
    status = GenieDialogConfig_createFromJson(config_json_.c_str(), &_context->config);
    if (status != GENIE_STATUS_SUCCESS) {
      SetError(Genie_Status_toString(status));
      return;
    }
    status = GenieDialog_create(&_context->dialog, _context->config);
    if (status != GENIE_STATUS_SUCCESS) {
      SetError(Genie_Status_toString(status));
      return;
    }
  }

  void OnOK() {
    Resolve(Context::New(Napi::External<ContextHolder>::New(
        Napi::AsyncWorker::Env(), _context)));
  }

  void OnError(const Napi::Error &e) { Reject(e.Value()); }

private:
  std::string config_json_;
  ContextHolder _context;
};

/* Context */

Napi::FunctionReference Context::constructor;

Napi::Object Context::Init(Napi::Env env, Napi::Object &exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(env, "Context", {
    StaticMethod("load", &Context::Load),
    InstanceMethod("query", &Context::Query),
  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("Context", func);
  return exports;
}


Context::Context(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Context>(info) {
  Napi::HandleScope scope(info.Env());
  _context = info[0].As<Napi::External<ContextHolder>>().Data();
  assert(_context);
}

Context::~Context() {
  if (_context) {
    GenieDialog_free(_context->dialog);
    GenieDialogConfig_free(_context->config);
    delete _context;
  }
}

Napi::Value Context::Load(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(env);
  new LoadWorker(env, info[0].As<Napi::String>().Utf8Value())
      .Queue();
  return deferred.Promise();
}

Napi::Value Context::Query(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (_is_querying) {
    Napi::Error::New(env, "Context is busying").ThrowAsJavaScriptException();
  }
  _is_querying = true;
  const char *prompt = info[0].As<Napi::String>().Utf8Value().c_str();
  Napi::Function callback = info[1].As<Napi::Function>();
  _callback = Napi::Persistent(callback);
  Genie_Status_t status = GenieDialog_query(_context->dialog, prompt, GENIE_DIALOG_SENTENCE_COMPLETE, on_response, this);
  if (status != GENIE_STATUS_SUCCESS) {
    Napi::Error::New(env, Genie_Status_toString(status)).ThrowAsJavaScriptException();
  }
}

void Context::on_response(const char *response, const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData) {
  Context *context = static_cast<Context *>(userData);
  Napi::Env env = context->Env();
  Napi::HandleScope scope(env);
  Napi::Function callback = context->_callback.Value();
  callback.Call({Napi::String::New(env, response)});
  if (sentenceCode == GENIE_DIALOG_SENTENCE_COMPLETE) {
    context->_is_querying = false;
  }
}
