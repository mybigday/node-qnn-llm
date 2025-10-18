#include "EmbeddingsHolder.h"
#include "utils.h"
#include <stdexcept>

EmbeddingsHolder::EmbeddingsHolder(std::string config_json) {
  Genie_Status_t status;
  status = GenieProfile_create(NULL, &profile);
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieEmbeddingConfig_createFromJson(config_json.c_str(), &config);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieEmbeddingConfig_bindProfiler(config, profile);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieEmbeddingConfig_free(config);
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  status = GenieEmbedding_create(config, &embedding);
  if (status != GENIE_STATUS_SUCCESS) {
    GenieEmbeddingConfig_free(config);
    GenieProfile_free(profile);
    throw std::runtime_error(Genie_Status_ToString(status));
  }
}

EmbeddingsHolder::~EmbeddingsHolder() {
  try {
    release();
  } catch (const std::runtime_error &e) {
    // nothing to do
  }
}

void EmbeddingsHolder::release() {
  if (embedding) {
    Genie_Status_t status = GenieEmbedding_free(embedding);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error(Genie_Status_ToString(status));
    }
    embedding = NULL;
  }
  if (config) {
    GenieEmbeddingConfig_free(config);
    config = NULL;
  }
  if (profile) {
    GenieProfile_free(profile);
    profile = NULL;
  }
}

std::string EmbeddingsHolder::query(std::string prompt, const EmbeddingsCallback &callback) {
  if (busying) {
    throw std::runtime_error("Embedding context is busy");
  }
  busying = true;
  this->callback = std::move(callback);
  Genie_Status_t status = GenieEmbedding_generate(embedding, prompt.c_str(), on_embeddings, this);
  busying = false;
  if (status != GENIE_STATUS_SUCCESS) {
    throw std::runtime_error(Genie_Status_ToString(status));
  }
  const char* profile_json = nullptr;
  GenieProfile_getJsonData(profile, alloc_json_data, &profile_json);
  std::string profile_json_str(profile_json);
  free((char*)profile_json);
  return profile_json_str;
}

void EmbeddingsHolder::on_embeddings(const uint32_t* dimensions,
                                     const uint32_t rank,
                                     const float* embeddingBuffer,
                                     const void* userData) {
  EmbeddingsHolder *embeddingsHolder = (EmbeddingsHolder *)userData;
  if (embeddingsHolder->callback) {
    size_t size = 1;
    for (uint32_t i = 0; i < rank; i++) {
      size *= dimensions[i];
    }
    std::vector<float> embedding(embeddingBuffer, embeddingBuffer + size);
    embeddingsHolder->callback(embedding);
  }
}
