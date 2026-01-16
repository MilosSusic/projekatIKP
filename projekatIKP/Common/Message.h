#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct MessageHeader {
    int32_t client_id;
    int32_t request_type;
    int32_t payload_len;
};
#pragma pack(pop)
