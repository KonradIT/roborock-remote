#pragma once
#include <Arduino.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>

// Shared crypto primitives and binary helpers for the Roborock V1 protocol.
// Header-only; all functions are inline to avoid duplicate symbols.
namespace RoborockCrypto {

constexpr const char* SALT = "TXdfu$jyZ#TZHsg4";

inline void md5Raw(const uint8_t* input, size_t len, uint8_t out[16]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, input, len);
    mbedtls_md_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

inline String md5Hex(const String& input) {
    uint8_t hash[16];
    md5Raw((const uint8_t*)input.c_str(), input.length(), hash);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[32] = '\0';
    return String(hex);
}

// Roborock-specific timestamp obfuscation for key derivation.
inline void encodeTimestamp(uint32_t ts, char out[9]) {
    char hex[9];
    snprintf(hex, sizeof(hex), "%08x", ts);
    static const int order[] = {5, 6, 3, 7, 1, 2, 0, 4};
    for (int i = 0; i < 8; i++) out[i] = hex[order[i]];
    out[8] = '\0';
}

// Derive the per-message AES key from timestamp + local_key + salt.
inline void deriveToken(uint32_t ts, const char* localKey, uint8_t token[16]) {
    char tsEnc[9];
    encodeTimestamp(ts, tsEnc);
    String combined = String(tsEnc) + String(localKey) + String(SALT);
    md5Raw((const uint8_t*)combined.c_str(), combined.length(), token);
}

inline void aesEcbEncrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

inline void aesEcbDecrypt(const uint8_t* in, size_t len, const uint8_t key[16], uint8_t* out) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, 128);
    for (size_t i = 0; i < len; i += 16)
        mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in + i, out + i);
    mbedtls_aes_free(&ctx);
}

inline uint32_t calcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFF;
}

// Big-endian serialization helpers.
inline void     writeBE16(uint8_t* d, uint16_t v) { d[0] = v >> 8; d[1] = v; }
inline void     writeBE32(uint8_t* d, uint32_t v) { d[0] = v >> 24; d[1] = v >> 16; d[2] = v >> 8; d[3] = v; }
inline uint16_t readBE16(const uint8_t* s) { return (s[0] << 8) | s[1]; }
inline uint32_t readBE32(const uint8_t* s) { return ((uint32_t)s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]; }

} // namespace RoborockCrypto
