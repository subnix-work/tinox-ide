/* libtinoxkde: C ABI between the Tinox IDE and Qt / KDE Frameworks.
 *
 * Placeholder until the ABI specification (docs/abi.md) lands: only the
 * version query exists so the Tinox side can prove that it links the bridge.
 */
#ifndef TINOXKDE_H
#define TINOXKDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define TKDE_EXPORT
#else
#define TKDE_EXPORT __attribute__((visibility("default")))
#endif

/** Version of the C ABI this library implements; 0 = pre-specification stub. */
#define TKDE_ABI_VERSION 0

TKDE_EXPORT int64_t tkde_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TINOXKDE_H */
