#pragma once
#include "ch32x035.h"
typedef struct { uint16_t GPIO_Pin; unsigned GPIO_Mode, GPIO_Speed; } GPIO_InitTypeDef;
enum { GPIO_Mode_IPD = 0x28, GPIO_Mode_Out_PP = 0x10, GPIO_Speed_50MHz = 3 };
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
void GPIO_ResetBits(GPIO_TypeDef *port, uint16_t pins);
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins);
uint16_t GPIO_ReadInputData(GPIO_TypeDef *port);
