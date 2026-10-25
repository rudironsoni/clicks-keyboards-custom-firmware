#include <stddef.h>
#include <stdint.h>
void qmk_bridge_boot(void);
size_t qmk_bridge_send(const uint8_t *request, size_t length, uint8_t *response);
void qmk_bridge_key(uint8_t row, uint8_t column, uint8_t pressed);
void qmk_bridge_ticks(uint32_t milliseconds);
void qmk_bridge_report(uint8_t report[10]);
void qmk_bridge_clear_reports(void);
size_t qmk_bridge_report_count(void);
void qmk_bridge_report_at(size_t index, uint8_t report[10]);
void qmk_bridge_layer(uint8_t layer);
