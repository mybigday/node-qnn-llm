#include "QueryWorker.h"
#include "Context.h"
#include <stdexcept>

QueryWorker::QueryWorker(Napi::Env env, QueryParams params,
                         ContextHolder *context, Napi::Function callback)
    : Napi::AsyncWorker(env), Napi::Promise::Deferred(env),
      params_(std::move(params)), _context(context) {
  _tsfn =
      Napi::ThreadSafeFunction::New(env, callback, "QueryWorkerCallback", 0, 1);
}

void QueryWorker::Execute() {
  try {
    result_ = _context->query(
        params_,
        [this](const char *response,
               const GenieDialog_SentenceCode_t sentenceCode) {
          if (response) {
            text_ += response;
          }
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

namespace {
// Finds the most recent dialog query stat in Genie's profile JSON:
// { components: [{ events: [{ "num-prompt-tokens": { value }, ... }] }] }
Napi::Object FindQueryEvent(Napi::Value profile) {
  Napi::Object found;
  if (!profile.IsObject()) {
    return found;
  }
  Napi::Value components = profile.As<Napi::Object>().Get("components");
  if (!components.IsArray()) {
    return found;
  }
  Napi::Array componentList = components.As<Napi::Array>();
  for (uint32_t i = 0; i < componentList.Length(); i++) {
    Napi::Value component = componentList.Get(i);
    if (!component.IsObject()) {
      continue;
    }
    Napi::Value events = component.As<Napi::Object>().Get("events");
    if (!events.IsArray()) {
      continue;
    }
    Napi::Array eventList = events.As<Napi::Array>();
    for (uint32_t j = 0; j < eventList.Length(); j++) {
      Napi::Value event = eventList.Get(j);
      if (event.IsObject() &&
          event.As<Napi::Object>().Has("num-generated-tokens")) {
        found = event.As<Napi::Object>();
      }
    }
  }
  return found;
}

double EventValue(Napi::Object event, const char *name) {
  if (event.IsEmpty() || !event.Has(name)) {
    return 0;
  }
  Napi::Value entry = event.Get(name);
  if (!entry.IsObject()) {
    return 0;
  }
  Napi::Value value = entry.As<Napi::Object>().Get("value");
  return value.IsNumber() ? value.As<Napi::Number>().DoubleValue() : 0;
}
}  // namespace

void QueryWorker::OnOK() {
  // Pending callbacks still run; an unreleased TSFN keeps the event loop alive.
  _tsfn.Release();
  Napi::Env env = Napi::AsyncWorker::Env();
  Napi::HandleScope scope(env);
  Napi::Value profile = env.Undefined();
  if (!result_.profile_json.empty()) {
    Napi::Object JSON = env.Global().Get("JSON").As<Napi::Object>();
    Napi::Function parse = JSON.Get("parse").As<Napi::Function>();
    profile = parse.Call({Napi::String::New(env, result_.profile_json)});
  }
  Napi::Object event = FindQueryEvent(profile);

  double prompt_n = EventValue(event, "num-prompt-tokens");
  double prompt_ms = EventValue(event, "time-to-first-token") / 1000.0;
  double predicted_n = result_.num_generated >= 0
                           ? static_cast<double>(result_.num_generated)
                           : EventValue(event, "num-generated-tokens");
  double predicted_ms = EventValue(event, "token-generation-time") / 1000.0;

  Napi::Object timings = Napi::Object::New(env);
  timings.Set("prompt_n", prompt_n);
  timings.Set("prompt_ms", prompt_ms);
  timings.Set("prompt_per_token_ms", prompt_n > 0 ? prompt_ms / prompt_n : 0);
  timings.Set("prompt_per_second", EventValue(event, "prompt-processing-rate"));
  timings.Set("predicted_n", predicted_n);
  timings.Set("predicted_ms", predicted_ms);
  timings.Set("predicted_per_token_ms",
              predicted_n > 0 ? predicted_ms / predicted_n : 0);
  timings.Set("predicted_per_second",
              EventValue(event, "token-generation-rate"));

  Napi::Object result = Napi::Object::New(env);
  result.Set("text", text_);
  result.Set("tokens_evaluated", prompt_n);
  result.Set("tokens_predicted", predicted_n);
  result.Set("interrupted", result_.interrupted);
  result.Set("timings", timings);
  result.Set("profile", profile);
  Resolve(result);
}

void QueryWorker::OnError(const Napi::Error &e) {
  _tsfn.Release();
  Reject(e.Value());
}
