#include "QueryWorker.h"
#include "Context.h"
#include <stdexcept>

QueryWorker::QueryWorker(Napi::Env env, std::string prompt,
                         ContextHolder *context, Napi::Function callback)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env), prompt_(prompt),
      _context(context) {
  _tsfn =
      Napi::ThreadSafeFunction::New(env, callback, "QueryWorkerCallback", 0, 1);
}

void QueryWorker::Execute() {
  try {
    _context->query(
        prompt_, GENIE_DIALOG_SENTENCE_COMPLETE,
        [this](const char *response,
               const GenieDialog_SentenceCode_t sentenceCode) {
          char *value = response ? strdup(response) : NULL;
          this->_tsfn.NonBlockingCall(
              value, [sentenceCode](Napi::Env env, Napi::Function callback,
                                    char *response) {
                Napi::HandleScope scope(env);
                callback.Call({Napi::String::New(env, response),
                               Napi::Number::New(env, sentenceCode)});
                if (response) {
                  free(response);
                }
              });
        });
  } catch (const std::runtime_error &e) {
    SetError(e.what());
  }
}

void QueryWorker::OnOK() { Resolve(Napi::AsyncWorker::Env().Undefined()); }

void QueryWorker::OnError(const Napi::Error &e) { Reject(e.Value()); }
