#include "hmac_utils.h"
#include "config.h"
#include "mbedtls/md.h"
#include <string.h>

static uint8_t s_key[HMAC_SECRET_MAX_LEN];
static size_t  s_key_len = 0;

static void ensure_key(void)
{
    if (s_key_len == 0) {
        const char *def = HMAC_SECRET_DEFAULT;
        size_t def_len = strlen(def);
        if (def_len > sizeof(s_key)) def_len = sizeof(s_key);
        memcpy(s_key, def, def_len);
        s_key_len = def_len;
    }
}

void hmac_set_key(const uint8_t *key, size_t key_len)
{
    if (key == NULL || key_len == 0) {
        const char *def = HMAC_SECRET_DEFAULT;
        key_len = strlen(def);
        if (key_len > sizeof(s_key)) key_len = sizeof(s_key);
        memcpy(s_key, def, key_len);
        s_key_len = key_len;
        return;
    }

    if (key_len > sizeof(s_key)) key_len = sizeof(s_key);
    memcpy(s_key, key, key_len);
    s_key_len = key_len;
}

void hmac_compute(const uint8_t *data, size_t data_len, uint8_t out_hmac[32])
{
    ensure_key();

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);

    mbedtls_md_hmac_starts(&ctx, s_key, s_key_len);
    mbedtls_md_hmac_update(&ctx, data, data_len);
    mbedtls_md_hmac_finish(&ctx, out_hmac);

    mbedtls_md_free(&ctx);
}

bool hmac_verify(const uint8_t *data, size_t data_len, const uint8_t expected_hmac[32])
{
    uint8_t computed[32];
    hmac_compute(data, data_len, computed);
    return memcmp(computed, expected_hmac, 32) == 0;
}
