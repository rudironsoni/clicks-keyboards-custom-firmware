#ifndef CK_V122_GATE_H
#define CK_V122_GATE_H
#include "machine.h"
void gate_access(ChgMachine *, uint32_t, int, bool, uint32_t);
bool gate_bus(ChgMachine *, uint32_t, int, bool, uint32_t *);
void gate_instruction(ChgMachine *, uint32_t);
void gate_trap(ChgMachine *, uint32_t, uint32_t, uint32_t, int);
void gate_mret(ChgMachine *);
void gate_csr(ChgMachine *, uint32_t, bool, uint32_t);
#endif
