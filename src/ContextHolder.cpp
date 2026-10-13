#include "ContextHolder.h"
#include "utils.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
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

ContextHolder::ContextHolder(std::string config_json, QueryParams defaults,
                             std::string dialog_type)
    : defaults_(std::move(defaults)), dialog_type(std::move(dialog_type)) {
  applied_stop_json = defaults_.stop_json;
  applied_max_tokens = defaults_.max_tokens;
  applied_sampler = defaults_.sampler;
  applied_grammar = defaults_.grammar;
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

QueryResult ContextHolder::query(const QueryParams &params,
                                  const CompletionCallback &callback) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  BusyGuard guard(busying);
  apply_stop_words(params.stop_json);
  apply_max_tokens(params.max_tokens);
  apply_sampler(params.sampler);
  apply_grammar(params.grammar);
  const std::string &prompt = params.prompt;
  std::string query = prompt;
  Genie_Status_t status;
  GenieDialog_SentenceCode_t sentenceCode = GENIE_DIALOG_SENTENCE_COMPLETE;
  if (!full_context.empty()) {
    sentenceCode = GENIE_DIALOG_SENTENCE_REWIND;
  }
  this->callback = callback;
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

  QueryResult result;
  result.interrupted = status == GENIE_STATUS_WARNING_ABORTED;
  Genie_DataType_t dataType;
  Genie_Value_t value;
  if (GenieDialog_getValue(dialog, GENIE_DIALOG_PARAM_NUM_GENERATED, nullptr,
                           &dataType, &value) == GENIE_STATUS_SUCCESS) {
    result.num_generated = value.uint32Value;
  }
  const char* profile_json = nullptr;
  GenieProfile_getJsonData(profile, alloc_json_data, &profile_json);
  if (profile_json) {
    result.profile_json = profile_json;
    free((char*)profile_json);
  }
  return result;
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

void ContextHolder::apply_stop_words(const std::string &stop_json) {
  if (stop_json == applied_stop_json) {
    return;
  }
  Genie_Status_t status = GenieDialog_setStopSequence(dialog, stop_json.c_str());
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  applied_stop_json = stop_json;
}

void ContextHolder::apply_max_tokens(uint32_t max_tokens) {
  if (max_tokens == applied_max_tokens) {
    return;
  }
  Genie_Status_t status = GenieDialog_setMaxNumTokens(dialog, max_tokens);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  applied_max_tokens = max_tokens;
}

void ContextHolder::apply_sampler(const SamplerParams &sampler) {
  if (sampler == applied_sampler) {
    return;
  }
  GenieSampler_Handle_t handle = NULL;
  Genie_Status_t status = GenieDialog_getSampler(dialog, &handle);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  std::ostringstream json;
  json << std::setprecision(9) << "{\"sampler\":{\"version\":1"
       << ",\"temp\":" << sampler.temperature << ",\"top-k\":" << sampler.top_k
       << ",\"top-p\":" << sampler.top_p << ",\"seed\":" << sampler.seed
       << "}}";
  GenieSamplerConfig_Handle_t config = NULL;
  status = GenieSamplerConfig_createFromJson(json.str().c_str(), &config);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieSampler_applyConfig(handle, config);
  GenieSamplerConfig_free(config);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  applied_sampler = sampler;
}

void ContextHolder::apply_grammar(const GrammarParams &grammar) {
  if (grammar == applied_grammar) {
    return;
  }
  // Genie only reports GENIE_STATUS_ERROR_GENERAL (details go to stderr), so
  // catch the known unsupported case up front with a readable message.
  if (!grammar.kind.empty() && dialog_type != "basic" && dialog_type != "eaglet") {
    throw std::runtime_error("Dialog type '" + dialog_type +
                             "' does not support grammar (supported: basic, eaglet)");
  }
  Genie_Status_t status;
  if (grammar.kind.empty()) {
    status = GenieDialog_setGrammar(dialog, nullptr, nullptr);
  } else if (!grammar.file.empty()) {
    status = GenieDialog_setGrammar(dialog, grammar.kind.c_str(), grammar.file.c_str());
  } else {
    // GenieDialog_setGrammar only accepts a file path; it reads and compiles
    // the file before returning, so the temp file can be removed right after.
    static std::atomic<uint64_t> counter = 0;
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("node-qnn-llm-grammar-" + std::to_string(stamp) + "-" +
         std::to_string(counter++));
    {
      std::ofstream file(path, std::ios::binary);
      file << grammar.content;
      if (!file) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        throw std::runtime_error("Failed to write grammar file");
      }
    }
    status = GenieDialog_setGrammar(dialog, grammar.kind.c_str(), path.string().c_str());
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(std::string("Failed to set grammar (invalid ") +
                             grammar.kind + "?): " + Genie_Status_ToString(status));
  }
  applied_grammar = grammar;
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
