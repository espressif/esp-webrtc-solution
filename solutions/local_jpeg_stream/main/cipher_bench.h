#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Run AES-GCM vs ChaCha20-Poly1305 microbench; prints MB/s + measure %%. */
int cipher_bench_run(void);

#ifdef __cplusplus
}
#endif
