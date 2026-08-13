/* On-device AEAD microbench for DTLS cipher evidence.
 *
 * IDF v6 / Mbed TLS 4.x: public mbedtls/gcm.h & chachapoly.h are gone; use PSA.
 *
 * Live DTLS AEAD is in-place on ssl->in_buf/out_buf (see mbedtls_ssl_encrypt_buf),
 * NOT on the app buffer passed to mbedtls_ssl_write/read. esp_peer already moves
 * those record buffers to INTERNAL+DMA, so the caller's send/recv buffer can stay
 * in PSRAM without affecting HW GCM.
 *
 * This bench calls psa_aead_* directly on the allocated plain/cipher buffers, so
 * those buffers model the SSL record buffers, not the app payload:
 * - INTERNAL+DMA  ≈ live path after esp_peer fix (expect fast AES-GCM)
 * - plain PSRAM   ≈ old EXTERNAL_MEM_ALLOC ssl record bufs (expect AES≈ChaCha)
 * SPIRAM|DMA is EDMA-PSRAM — not the same as EXTERNAL_MEM_ALLOC ssl bufs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "sdkconfig.h"
#include "psa/crypto.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S31)
#include "soc/soc_caps.h"
#endif
#include "cipher_bench.h"

static const char *TAG = "CIPHER_BENCH";

/* Match typical SCTP/DTLS application-data record size on this stack. */
#define RECORD_SIZE 1200
#define AAD_SIZE    13 /* DTLS-like AAD length */
#define TAG_SIZE    16
#define ITERS       2000

typedef enum {
    BENCH_MEM_INTERNAL_DMA = 0,
    BENCH_MEM_PSRAM,
} bench_mem_t;

static void fill_random(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

static const char *mem_kind(const void *p)
{
    if (p == NULL) {
        return "null";
    }
#if defined(CONFIG_SPIRAM)
    if (esp_ptr_external_ram(p)) {
        return "PSRAM";
    }
#endif
    if (esp_ptr_internal(p)) {
        return "INTERNAL";
    }
    return "OTHER";
}

static void log_buf(const char *name, const void *p)
{
    ESP_LOGI(TAG, "  %s: %s dma=%d", name, mem_kind(p), p ? (int)esp_ptr_dma_capable(p) : 0);
}

static void log_platform(void)
{
#if defined(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
    ESP_LOGI(TAG, "CPU freq config=%d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#endif
#if defined(CONFIG_SPIRAM_USE_MALLOC) && defined(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
    ESP_LOGI(TAG, "SPIRAM_USE_MALLOC=1 ALWAYSINTERNAL=%d (calloc>%dB -> PSRAM)",
             CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#elif defined(CONFIG_SPIRAM_USE_MALLOC)
    ESP_LOGI(TAG, "SPIRAM_USE_MALLOC=1");
#endif
#if defined(CONFIG_MBEDTLS_HARDWARE_AES)
    ESP_LOGI(TAG, "CONFIG_MBEDTLS_HARDWARE_AES=%d", CONFIG_MBEDTLS_HARDWARE_AES);
#else
    ESP_LOGI(TAG, "CONFIG_MBEDTLS_HARDWARE_AES=0");
#endif
#if defined(CONFIG_MBEDTLS_HARDWARE_GCM)
    ESP_LOGI(TAG, "CONFIG_MBEDTLS_HARDWARE_GCM=%d (needs DMA-capable input/output/AAD)",
             CONFIG_MBEDTLS_HARDWARE_GCM);
#elif defined(SOC_AES_SUPPORT_GCM)
    ESP_LOGI(TAG, "SOC_AES_SUPPORT_GCM=1 but HARDWARE_GCM off");
#else
    ESP_LOGI(TAG, "SOC_AES_SUPPORT_GCM=0 (AES-GCM mostly software on this SoC)");
#endif
#if defined(CONFIG_MBEDTLS_CHACHAPOLY_C)
    ESP_LOGI(TAG, "CONFIG_MBEDTLS_CHACHAPOLY_C=1 (ChaCha20-Poly1305 is always software)");
#else
    ESP_LOGI(TAG, "CONFIG_MBEDTLS_CHACHAPOLY_C=0");
#endif
    ESP_LOGI(TAG, "API=PSA one-shot aead (matches IDF v6 DTLS); setkey/setup often per call");
    ESP_LOGI(TAG, "Record size=%d B, iters=%d (enc+dec each)", RECORD_SIZE, ITERS);
}

static void report_mbps(const char *name, int64_t us, size_t bytes_total)
{
    if (us <= 0) {
        ESP_LOGI(TAG, "%s: invalid timing", name);
        return;
    }
    double mbps = (double)bytes_total / (double)us;
    ESP_LOGI(TAG, "%s: %lld us total, %.2f MB/s (%.0f us/pkt)",
             name, (long long)us, mbps, (double)us / (double)ITERS);
}

static int ensure_psa(void)
{
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS && status != PSA_ERROR_BAD_STATE) {
        ESP_LOGE(TAG, "psa_crypto_init fail %d", (int)status);
        return (int)status;
    }
    return 0;
}

static int import_aead_key(psa_key_id_t *key_id, psa_key_type_t type, psa_algorithm_t alg,
                           const uint8_t *key, size_t key_len, psa_key_usage_t usage)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, usage);
    psa_set_key_algorithm(&attr, alg);
    psa_set_key_type(&attr, type);
    psa_set_key_bits(&attr, key_len * 8);
    psa_status_t status = psa_import_key(&attr, key, key_len, key_id);
    psa_reset_key_attributes(&attr);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key fail %d", (int)status);
        return (int)status;
    }
    return 0;
}

static int bench_aead(const char *name_enc, const char *name_dec,
                      psa_key_type_t key_type, psa_algorithm_t alg,
                      const uint8_t *key, size_t key_len,
                      const uint8_t *nonce, size_t nonce_len,
                      const uint8_t *aad, uint8_t *plain, uint8_t *cipher_tag, uint8_t *out)
{
    psa_key_id_t key_enc = 0;
    psa_key_id_t key_dec = 0;
    psa_status_t status;
    int64_t t0, t1;
    size_t bytes = (size_t)RECORD_SIZE * ITERS;
    size_t out_len = 0;
    int ret;

    ret = import_aead_key(&key_enc, key_type, alg, key, key_len, PSA_KEY_USAGE_ENCRYPT);
    if (ret != 0) {
        return ret;
    }
    ret = import_aead_key(&key_dec, key_type, alg, key, key_len, PSA_KEY_USAGE_DECRYPT);
    if (ret != 0) {
        psa_destroy_key(key_enc);
        return ret;
    }

    /* Warm one call so first-iter init is outside the timed window. */
    status = psa_aead_encrypt(key_enc, alg, nonce, nonce_len, aad, AAD_SIZE,
                              plain, RECORD_SIZE, cipher_tag, RECORD_SIZE + TAG_SIZE, &out_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "%s warmup fail %d", name_enc, (int)status);
        ret = (int)status;
        goto done;
    }

    t0 = esp_timer_get_time();
    for (int i = 0; i < ITERS; i++) {
        status = psa_aead_encrypt(key_enc, alg, nonce, nonce_len, aad, AAD_SIZE,
                                  plain, RECORD_SIZE, cipher_tag, RECORD_SIZE + TAG_SIZE, &out_len);
        if (status != PSA_SUCCESS || out_len != RECORD_SIZE + TAG_SIZE) {
            ESP_LOGE(TAG, "%s fail %d out_len=%u", name_enc, (int)status, (unsigned)out_len);
            ret = (int)status;
            goto done;
        }
    }
    t1 = esp_timer_get_time();
    report_mbps(name_enc, t1 - t0, bytes);

    status = psa_aead_decrypt(key_dec, alg, nonce, nonce_len, aad, AAD_SIZE,
                              cipher_tag, RECORD_SIZE + TAG_SIZE, out, RECORD_SIZE, &out_len);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "%s warmup fail %d", name_dec, (int)status);
        ret = (int)status;
        goto done;
    }

    t0 = esp_timer_get_time();
    for (int i = 0; i < ITERS; i++) {
        status = psa_aead_decrypt(key_dec, alg, nonce, nonce_len, aad, AAD_SIZE,
                                  cipher_tag, RECORD_SIZE + TAG_SIZE, out, RECORD_SIZE, &out_len);
        if (status != PSA_SUCCESS || out_len != RECORD_SIZE) {
            ESP_LOGE(TAG, "%s fail %d out_len=%u", name_dec, (int)status, (unsigned)out_len);
            ret = (int)status;
            goto done;
        }
    }
    t1 = esp_timer_get_time();
    report_mbps(name_dec, t1 - t0, bytes);

    ret = 0;

done:
    psa_destroy_key(key_enc);
    psa_destroy_key(key_dec);
    return ret;
}

static int bench_with_caps(bench_mem_t mem, const uint8_t *key16, const uint8_t *key32,
                           const uint8_t *iv, const uint8_t *aad_src)
{
    uint8_t *plain = NULL;
    uint8_t *cipher_tag = NULL;
    uint8_t *out = NULL;
    uint8_t *aad = NULL;
    uint32_t caps;
    int ret = -1;

    if (mem == BENCH_MEM_INTERNAL_DMA) {
        caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
        ESP_LOGI(TAG, "=== AEAD bufs = INTERNAL+DMA (models fixed ssl in/out_buf) ===");
    } else {
        /* Models pre-fix EXTERNAL_MEM_ALLOC ssl record buffers (not app send buf). */
        caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        ESP_LOGI(TAG, "=== AEAD bufs = PSRAM (models old ssl in/out_buf; not app payload) ===");
    }

    plain = heap_caps_calloc(1, RECORD_SIZE, caps);
    cipher_tag = heap_caps_calloc(1, RECORD_SIZE + TAG_SIZE, caps);
    out = heap_caps_calloc(1, RECORD_SIZE, caps);
    /* AAD: live DTLS builds this on the task stack; IDF patch can DMA-copy it. */
    aad = heap_caps_calloc(1, AAD_SIZE, caps);
    if (!plain || !cipher_tag || !out || !aad) {
        ESP_LOGE(TAG, "alloc failed caps=0x%lx plain=%p tag=%p out=%p aad=%p",
                 (unsigned long)caps, plain, cipher_tag, out, aad);
        goto done;
    }
    log_buf("plain", plain);
    log_buf("cipher", cipher_tag);
    log_buf("out", out);
    log_buf("aad", aad);
    if (mem == BENCH_MEM_INTERNAL_DMA &&
        (!esp_ptr_dma_capable(plain) || !esp_ptr_dma_capable(cipher_tag))) {
        ESP_LOGW(TAG, "INTERNAL alloc is not DMA-capable; AES HW path will miss");
    }
    if (mem == BENCH_MEM_PSRAM && !esp_ptr_external_ram(plain)) {
        ESP_LOGW(TAG, "requested PSRAM but plain is %s — SPIRAM heap empty?", mem_kind(plain));
    }

    memcpy(aad, aad_src, AAD_SIZE);
    fill_random(plain, RECORD_SIZE);

    ret = bench_aead("aes_gcm_enc", "aes_gcm_dec",
                     PSA_KEY_TYPE_AES, PSA_ALG_GCM,
                     key16, 16, iv, 12, aad, plain, cipher_tag, out);
    if (ret != 0) {
        goto done;
    }

#if defined(CONFIG_MBEDTLS_CHACHAPOLY_C)
    ret = bench_aead("chacha_enc", "chacha_dec",
                     PSA_KEY_TYPE_CHACHA20, PSA_ALG_CHACHA20_POLY1305,
                     key32, 32, iv, 12, aad, plain, cipher_tag, out);
#else
    ESP_LOGW(TAG, "ChaCha20-Poly1305 not compiled in; only AES-GCM measured");
#endif

done:
    heap_caps_free(plain);
    heap_caps_free(cipher_tag);
    heap_caps_free(out);
    heap_caps_free(aad);
    return ret;
}

int cipher_bench_run(void)
{
    uint8_t key16[16];
    uint8_t key32[32];
    uint8_t iv[12];
    uint8_t aad[AAD_SIZE];
    int ret;

    log_platform();

    if (ensure_psa() != 0) {
        return -1;
    }

    fill_random(key16, sizeof(key16));
    fill_random(key32, sizeof(key32));
    fill_random(iv, sizeof(iv));
    fill_random(aad, sizeof(aad));

    ESP_LOGI(TAG, "=== PSA one-shot on AEAD buffers (ssl record bufs, not app send) ===");

    /* Primary: matches live DTLS after esp_peer puts in_buf/out_buf in INTERNAL DMA. */
    ret = bench_with_caps(BENCH_MEM_INTERNAL_DMA, key16, key32, iv, aad);
    if (ret != 0) {
        return ret;
    }

#if defined(CONFIG_SPIRAM)
    /* Contrast only: old ssl record bufs in PSRAM. App payload in PSRAM is fine. */
    ret = bench_with_caps(BENCH_MEM_PSRAM, key16, key32, iv, aad);
#endif
    return ret;
}
