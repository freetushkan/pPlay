#include <openssl/ui.h>

/*
 * Some OpenOrbis toolchains ship libcurl built against OpenSSL expecting
 * UI_OpenSSL(), but the OpenSSL archive in the SDK may not provide it.
 * Provide a tiny fallback that matches historical behavior.
 */
UI_METHOD *UI_OpenSSL(void)
{
    return UI_null();
}
