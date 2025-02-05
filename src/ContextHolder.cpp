#include "ContextHolder.h"
#include <stdexcept>

const char *Genie_Status_ToString(Genie_Status_t status) {
  switch (status) {
  case GENIE_STATUS_SUCCESS:
    return "GENIE_STATUS_SUCCESS";
  case GENIE_STATUS_ERROR_GENERAL:
    return "GENIE_STATUS_ERROR_GENERAL";
  case GENIE_STATUS_ERROR_INVALID_ARGUMENT:
    return "GENIE_STATUS_ERROR_INVALID_ARGUMENT";
  case GENIE_STATUS_ERROR_MEM_ALLOC:
    return "GENIE_STATUS_ERROR_MEM_ALLOC";
  case GENIE_STATUS_ERROR_INVALID_CONFIG:
    return "GENIE_STATUS_ERROR_INVALID_CONFIG";
  case GENIE_STATUS_ERROR_INVALID_HANDLE:
    return "GENIE_STATUS_ERROR_INVALID_HANDLE";
  case GENIE_STATUS_ERROR_QUERY_FAILED:
    return "GENIE_STATUS_ERROR_QUERY_FAILED";
  case GENIE_STATUS_ERROR_JSON_FORMAT:
    return "GENIE_STATUS_ERROR_JSON_FORMAT";
  case GENIE_STATUS_ERROR_JSON_SCHEMA:
    return "GENIE_STATUS_ERROR_JSON_SCHEMA";
  case GENIE_STATUS_ERROR_JSON_VALUE:
    return "GENIE_STATUS_ERROR_JSON_VALUE";
  default:
    return "UNKNOWN_GENIE_STATUS";
  }
}

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

void ContextHolder::query(std::string prompt,
                          const GenieDialog_SentenceCode_t sentenceCode,
                          const CompletionCallback &callback) {
  if (busying) {
    throw std::runtime_error("Context is busy");
  }
  std::string query = prompt;
  Genie_Status_t status;
  if (full_context.empty()) {
    full_context = prompt;
  } else {
    if (prompt.find(full_context) == 0) {
      query = prompt.substr(full_context.length());
    } else {
      full_context = prompt;
      status = GenieDialog_reset(dialog);
      if (status != GENIE_STATUS_SUCCESS) {
        throw std::runtime_error(Genie_Status_ToString(status));
      }
    }
  }
  busying = true;
  this->callback = std::move(callback);
  status =
      GenieDialog_query(dialog, query.c_str(), sentenceCode, on_response, this);
  busying = false;
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
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
