#pragma once

#include "GenieCommon.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

const char *Genie_Status_ToString(Genie_Status_t status);
void alloc_json_data(size_t size, const char** data);

#ifdef __cplusplus
}
#endif // __cplusplus
