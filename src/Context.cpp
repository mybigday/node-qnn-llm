#include "Context.h"
#include "utils.h"
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <functional>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

class ContextWorker {
public:
  ContextWorker() {
    thread = std::thread([this]() {
      std::function<void()> task;
      while (running) {
        task = nullptr;
        {
          std::unique_lock<std::mutex> lock(mutex_tx);
          cv_tx.wait(lock, [this]() { return !tasks.empty() || !running; });
          if (!running) {
            if (is_initialized) {
              GenieDialog_reset(dialog);
              GenieDialog_free(dialog);
              GenieDialogConfig_free(config);
            }
            break;
          }
          task = tasks.front();
          tasks.pop();
        }
        if (task) {
          task();
        }
        cv_rx.notify_one();
      }
    });
  }

  ~ContextWorker() {
    stop();
#ifdef _WIN32
      TerminateThread(thread.native_handle(), 1);
#else
    pthread_cancel(thread.native_handle());
#endif
    thread.join();
  }

  void stop() {
    running = false;
    cv_tx.notify_one();
  }

  void query(std::string prompt, const GenieDialog_SentenceCode_t sentenceCode,
             const GenieDialog_QueryCallback_t callback, const void *userData) {
    addTask([this, prompt, sentenceCode, callback, userData]() {
      error.clear();
      Genie_Status_t status = GenieDialog_query(dialog, prompt.c_str(),
                                                sentenceCode, callback,
                                                userData);
      if (status != GENIE_STATUS_SUCCESS) {
        error = Genie_Status_ToString(status);
      }
    });
    wait();
    if (!error.empty()) {
      throw std::runtime_error(error);
    }
  }

  void load(std::string config_json) {
    addTask([this, config_json]() {
      Genie_Status_t status;
      status = GenieDialogConfig_createFromJson(config_json.c_str(),
                                                &config);
      if (status != GENIE_STATUS_SUCCESS) {
        error = Genie_Status_ToString(status);
        return;
      }
      status = GenieDialog_create(config, &dialog);
      if (status != GENIE_STATUS_SUCCESS) {
        error = Genie_Status_ToString(status);
        return;
      }
      is_initialized = true;
    });
    wait();
    if (!is_initialized) {
      throw std::runtime_error(error);
    }
  }

protected:
  void addTask(std::function<void()> task) {
    std::unique_lock<std::mutex> lock(mutex_tx);
    tasks.push(task);
    cv_tx.notify_one();
  }

  void wait() {
    std::unique_lock<std::mutex> lock(mutex_rx);
    cv_rx.wait(lock, [this]() { return tasks.empty(); });
  }

private:
  GenieDialog_Handle_t dialog = NULL;
  GenieDialogConfig_Handle_t config = NULL;
  std::atomic<bool> running = true;
  std::thread thread;
  std::mutex mutex_tx;
  std::mutex mutex_rx;
  std::condition_variable cv_tx;
  std::condition_variable cv_rx;
  std::queue<std::function<void()>> tasks;
  std::string error;
  std::atomic<bool> is_initialized = false;
};

/* LoadWorker */

class LoadWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  LoadWorker(Napi::Env env, std::string config_json)
      : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
        config_json_(config_json) {}

protected:
  void Execute() {
    try {
      _context->load(config_json_);
    } catch (const std::runtime_error &e) {
      SetError(e.what());
      delete _context;
    }
  }

  void OnOK() {
    Resolve(Context::New(Napi::External<ContextWorker>::New(
        Napi::AsyncWorker::Env(), _context)));
  }

  void OnError(const Napi::Error &e) { Reject(e.Value()); }

private:
  std::string config_json_;
  ContextWorker *_context = new ContextWorker();
};

/* QueryWorker */

class QueryWorker : public Napi::AsyncWorker, public Napi::Promise::Deferred {
public:
  QueryWorker(Napi::Env env, std::string prompt, ContextWorker *context,
              Napi::Function callback)
      : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), prompt_(prompt),
        _context(context) {
    _tsfn = Napi::ThreadSafeFunction::New(env, callback, "QueryWorkerCallback",
                                          0, 1);
  }

  ~QueryWorker() { _tsfn.Release(); }

protected:
  void Execute() {
    try {
      _context->query(prompt_, GENIE_DIALOG_SENTENCE_COMPLETE, on_response,
                       this);
    } catch (const std::runtime_error &e) {
      SetError(e.what());
    }
  }

  void OnOK() { Resolve(Napi::AsyncWorker::Env().Undefined()); }

  void OnError(const Napi::Error &e) { Reject(e.Value()); }

  static void on_response(const char *response,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const void *userData) {
    auto self = static_cast<QueryWorker *>(const_cast<void *>(userData));
    self->_tsfn.BlockingCall(response, [sentenceCode](Napi::Env env,
                                                      Napi::Function callback,
                                                      const char *value) {
      Napi::HandleScope scope(env);
      callback.Call({value ? Napi::String::New(env, value) : env.Undefined(),
                     Napi::Number::New(env, sentenceCode)});
    });
  }

private:
  std::string prompt_;
  ContextWorker *_context;
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
  _context = info[0].As<Napi::External<ContextWorker>>().Data();
}

Context::~Context() {
  if (_context) {
    delete _context;
    _context = NULL;
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

void Context::Release(const Napi::CallbackInfo &info) {
  if (_context) {
    delete _context;
    _context = NULL;
  }
}
