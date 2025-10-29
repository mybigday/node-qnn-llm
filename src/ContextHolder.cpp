#include "ContextHolder.h"
#include "utils.h"
#include <stdexcept>

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
    busying = false;
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
  busying = true;
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
  busying = false;
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
  busying = true;
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
  busying = false;
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

void ContextHolder::reset() {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  Genie_Status_t status = GenieDialog_reset(dialog);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}
