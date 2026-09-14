#include "ContextHolder.h"
#include "utils.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
// Clears `busying` on every exit path (normal return or exception unwinding)
// so a concurrent release() waiting on this flag is never left spinning
// forever, and so `busying` stays true for the entire duration a query()/
// process() call keeps touching the dialog/profile handles.
class BusyGuard {
public:
  explicit BusyGuard(std::atomic<bool> &flag) : flag_(flag) { flag_ = true; }
  ~BusyGuard() { flag_ = false; }
  BusyGuard(const BusyGuard &) = delete;
  BusyGuard &operator=(const BusyGuard &) = delete;

private:
  std::atomic<bool> &flag_;
};
}  // namespace

ContextHolder::ContextHolder(std::string config_json) {
  Genie_Status_t status;
  status = GenieProfile_create(NULL, &profile);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieDialogConfig_createFromJson(config_json.c_str(), &config);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieDialogConfig_bindProfiler(config, profile);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieDialogConfig_free(config);
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieDialog_create(config, &dialog);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieDialogConfig_free(config);
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

ContextHolder::~ContextHolder() {
  try {
    release();
  } catch (const std::runtime_error &e) {
    // nothing to do
  }
}

void ContextHolder::release() {
  if (busying) {
    Genie_Status_t status =
        GenieDialog_signal(dialog, GENIE_DIALOG_ACTION_ABORT);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error(Genie_Status_ToString(status));
    }
    // Wait for the in-flight query()/process() call to actually return from
    // GenieDialog_query before freeing the dialog out from under it. That
    // call clears `busying` itself once GenieDialog_query unblocks with the
    // abort status; setting it here instead would let this thread free the
    // dialog while the other thread is still inside GenieDialog_query.
    while (busying) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (dialog) {
    Genie_Status_t status = GenieDialog_free(dialog);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error(Genie_Status_ToString(status));
    }
    dialog = NULL;
  }
  if (config) {
    GenieDialogConfig_free(config);
    config = NULL;
  }
  if (profile) {
    GenieProfile_free(profile);
    profile = NULL;
  }
}

void ContextHolder::process(std::string prompt) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  std::string query = prompt;
  Genie_Status_t status;
  GenieDialog_SentenceCode_t sentenceCode = GENIE_DIALOG_SENTENCE_COMPLETE;
  if (!full_context.empty()) {
    sentenceCode = GENIE_DIALOG_SENTENCE_REWIND;
  }
  BusyGuard guard(busying);
  this->callback = std::move(callback);
  status = GenieDialog_query(dialog, query.c_str(), sentenceCode, process_callback, this);
  if (status != GENIE_STATUS_SUCCESS && status != GENIE_STATUS_WARNING_ABORTED) {
    // retry normal query
    if (prompt.find(full_context) == 0) {
      query = prompt.substr(full_context.length());
    } else {
      status = GenieDialog_reset(dialog);
      if (status != GENIE_STATUS_SUCCESS) {
        throw std::runtime_error(Genie_Status_ToString(status));
      }
    }
    status = GenieDialog_query(dialog, query.c_str(), sentenceCode, process_callback, this);
  }
  if (status != GENIE_STATUS_SUCCESS && status != GENIE_STATUS_WARNING_ABORTED) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  full_context = prompt;
}

void ContextHolder::process_callback(const char *response,
                               const GenieDialog_SentenceCode_t sentenceCode,
                               const void *userData) {
  ContextHolder *self = (ContextHolder *)userData;
  GenieDialog_signal(self->dialog, GENIE_DIALOG_ACTION_ABORT);
}

std::string ContextHolder::query(std::string prompt,
                          const CompletionCallback &callback) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  std::string query = prompt;
  Genie_Status_t status;
  GenieDialog_SentenceCode_t sentenceCode = GENIE_DIALOG_SENTENCE_COMPLETE;
  if (!full_context.empty()) {
    sentenceCode = GENIE_DIALOG_SENTENCE_REWIND;
  }
  BusyGuard guard(busying);
  this->callback = std::move(callback);
  status = GenieDialog_query(dialog, query.c_str(), sentenceCode, on_response, this);
  if (status != GENIE_STATUS_SUCCESS && status != GENIE_STATUS_WARNING_ABORTED) {
    // retry normal query
    if (prompt.find(full_context) == 0) {
      query = prompt.substr(full_context.length());
    } else {
      status = GenieDialog_reset(dialog);
      if (status != GENIE_STATUS_SUCCESS) {
        throw std::runtime_error(Genie_Status_ToString(status));
      }
    }
    status = GenieDialog_query(dialog, query.c_str(), sentenceCode, on_response, this);
  }
  if (status != GENIE_STATUS_SUCCESS && status != GENIE_STATUS_WARNING_ABORTED) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  full_context = prompt;
  const char* profile_json = nullptr;
  GenieProfile_getJsonData(profile, alloc_json_data, &profile_json);
  std::string profile_json_str(profile_json);
  free((char*)profile_json);
  return profile_json_str;
}

void ContextHolder::abort() {
  Genie_Status_t status = GenieDialog_signal(dialog, GENIE_DIALOG_ACTION_ABORT);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::save(std::string filename) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_save(dialog, filename.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::restore(std::string filename) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_restore(dialog, filename.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::set_stop_words(std::string stop_words_json) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status =
      GenieDialog_setStopSequence(dialog, stop_words_json.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::apply_sampler_config(std::string config_json) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  GenieSampler_Handle_t sampler = NULL;
  Genie_Status_t status = GenieDialog_getSampler(dialog, &sampler);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  GenieSamplerConfig_Handle_t config = NULL;
  status = GenieSamplerConfig_createFromJson(config_json.c_str(), &config);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieSampler_applyConfig(sampler, config);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::on_response(const char *response,
                                const GenieDialog_SentenceCode_t sentenceCode,
                                const void *userData) {
  ContextHolder *context = (ContextHolder *)userData;
  if (response) {
    context->full_context += response;
  }
  if (context->callback) {
    context->callback(response, sentenceCode);
  }
}

void ContextHolder::apply_lora(const std::string &engine, const std::string &lora_adapter_name) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_applyLora(dialog, engine.c_str(), lora_adapter_name.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

void ContextHolder::set_lora_strength(const std::string &engine, const LoraStrengthMap &lora_strength_map) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  for (const auto &[tensor_name, alpha] : lora_strength_map) {
    Genie_Status_t status = GenieDialog_setLoraStrength(dialog, engine.c_str(), tensor_name.c_str(), alpha);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error(Genie_Status_ToString(status));
    }
  }
}

void ContextHolder::release_lora_memory(const std::string &engine, const std::string &lora_adapter_name) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_releaseLoraMemory(dialog, engine.c_str(), lora_adapter_name.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

uint32_t ContextHolder::get_context_occupancy() {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_DataType_t dataType;
  Genie_Value_t value;
  Genie_Status_t status = GenieDialog_getValue(
      dialog, GENIE_DIALOG_PARAM_CONTEXT_OCCUPANCY, nullptr, &dataType, &value);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  return value.uint32Value;
}

std::string ContextHolder::get_applied_lora_adapter() {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_DataType_t dataType;
  Genie_Value_t value;
  value.stringValue = nullptr;
  Genie_Status_t status = GenieDialog_getValue(
      dialog, GENIE_DIALOG_PARAM_APPLIED_LORA_ADAPTER, alloc_json_data, &dataType, &value);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  std::string result = value.stringValue ? value.stringValue : "";
  free((void *)value.stringValue);
  return result;
}

void ContextHolder::reset() {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_reset(dialog);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}
