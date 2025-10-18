#pragma once

#include "GenieEmbedding.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

class EmbeddingsHolder {
  using EmbeddingsCallback =
      std::function<void(std::vector<float>)>;

public:
  EmbeddingsHolder(std::string config_json);
  ~EmbeddingsHolder();
  void release();
  std::string query(std::string prompt, const EmbeddingsCallback &callback);

protected:
  static void on_embeddings(const uint32_t* dimensions,
                            const uint32_t rank,
                            const float* embeddingBuffer,
                            const void* userData);

private:
  std::atomic<bool> busying = false;
  GenieEmbedding_Handle_t embedding = NULL;
  GenieEmbeddingConfig_Handle_t config = NULL;
  GenieProfile_Handle_t profile = NULL;
  EmbeddingsCallback callback = nullptr;
};