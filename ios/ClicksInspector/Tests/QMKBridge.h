#include <stddef.h>
#include <stdint.h>
void qmk_bridge_boot(void);
size_t qmk_bridge_send(const uint8_t *request, size_t length, uint8_t *response);
