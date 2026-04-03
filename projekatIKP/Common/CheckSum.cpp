#pragma once
#include <string>
#include <cstdint>
#include "Protocol.h"
#include "Message.h"

// ----------------------
// CRC32 implementacija
// ----------------------
inline uint32_t crc32(const std::string& data) {
    static uint32_t table[256];
    static bool initialized = false;

    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1)
                    c = 0xEDB88320U ^ (c >> 1);
                else
                    c >>= 1;
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xFFFFFFFFU;
    for (unsigned char b : data) {
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

// ----------------------
// SHA-256 implementacija
// ----------------------
// Minimalna implementacija bez biblioteka
#include <array>
#include <vector>

inline uint32_t sha256_to_uint32(const std::string& data) {
    // Ovo je minimalna implementacija SHA-256 (digest 32 bajta)
    // Za jednostavnost, vraćamo prvih 4 bajta kao uint32_t
    // (nije kriptografski idealno, ali dovoljno za checksum)

    // konstante
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); };
    auto ch = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); };
    auto maj = [](uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); };
    auto sigma0 = [&](uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); };
    auto sigma1 = [&](uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); };
    auto delta0 = [&](uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); };
    auto delta1 = [&](uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); };

    // inicijalni hash
    uint32_t H[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    // priprema poruke
    std::vector<uint8_t> msg(data.begin(), data.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    uint64_t bitlen = (uint64_t)data.size() * 8;
    for (int i = 7; i >= 0; i--) msg.push_back((bitlen >> (i * 8)) & 0xFF);

    // obrada blokova
    for (size_t offset = 0; offset < msg.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[offset + 4 * i] << 24) | (msg[offset + 4 * i + 1] << 16) |
                (msg[offset + 4 * i + 2] << 8) | (msg[offset + 4 * i + 3]);
        }
        for (int i = 16; i < 64; i++) {
            w[i] = delta1(w[i - 2]) + w[i - 7] + delta0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }

    // vrati prvih 4 bajta kao uint32_t
    return H[0];
}

// ----------------------
// Wrapper funkcija
// ----------------------
inline uint32_t calculateChecksum(const std::string& payload, ChecksumType type) {
    switch (type) {
    case ChecksumType::NONE:
        return 0;
    case ChecksumType::SUM: {
        uint32_t sum = 0;
        for (unsigned char c : payload) sum += c;
        return sum;
    }
    case ChecksumType::CRC32:
        return crc32(payload);
    case ChecksumType::SHA256:
        return sha256_to_uint32(payload);
    default:
        return 0;
    }
}