#pragma once
#include <cstdint>

#pragma pack(push, 1)
enum class ChecksumType {
    NONE = 0,   
    SUM = 1,    
    CRC32 = 2,  
    SHA256 = 3  
};


struct MessageHeader {
    int32_t client_id;
    int32_t request_type;
    int32_t payload_len;
    ChecksumType type;
    int32_t checksum;
};
#pragma pack(pop)
