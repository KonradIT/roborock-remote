#pragma once
#include <Arduino.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

// HAWK authentication helpers used by the Roborock cloud API client.
namespace ApiUtils {

inline String base64Encode(const uint8_t* input, size_t len) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, input, len);
    uint8_t* buf = new uint8_t[olen + 1];
    mbedtls_base64_encode(buf, olen + 1, &olen, input, len);
    buf[olen] = '\0';
    String result((char*)buf);
    delete[] buf;
    return result;
}

inline String hmacSha256Base64(const String& key, const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    return base64Encode(hash, 32);
}

// URL-safe base64 nonce for HAWK authentication.
inline String generateNonce() {
    uint8_t raw[6];
    for (int i = 0; i < 6; i++) raw[i] = (uint8_t)(esp_random() & 0xFF);
    String b64 = base64Encode(raw, 6);
    b64.replace("+", "-");
    b64.replace("/", "_");
    b64.replace("=", "");
    return b64;
}

} // namespace ApiUtils
