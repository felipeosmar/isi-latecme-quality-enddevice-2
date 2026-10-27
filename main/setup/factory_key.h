/**
 * @file factory_key.h
 * @brief Factory provisioning shared secret (placeholder)
 *
 * Vendored copy of the shared-repo placeholder key. This key is used by
 * later setup-mode steps (SET/COMMIT) to authenticate/decrypt provisioning
 * payloads sent over ESP-NOW during factory setup.
 *
 * IMPORTANT: this is a placeholder (all-zero) key. The real production key
 * is swapped in at build time before this firmware ships — never commit the
 * real key to source control.
 */

#ifndef FACTORY_KEY_H
#define FACTORY_KEY_H

#include <stdint.h>

static const uint8_t FACTORY_KEY[16] = {0};

#endif /* FACTORY_KEY_H */
