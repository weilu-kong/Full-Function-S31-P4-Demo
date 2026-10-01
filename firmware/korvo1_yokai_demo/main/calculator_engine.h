#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CALC_STATE_INPUT_A = 0,
    CALC_STATE_OPERATOR,
    CALC_STATE_INPUT_B,
    CALC_STATE_RESULT,
    CALC_STATE_ERROR,
} calc_state_t;

typedef enum {
    CALC_OP_NONE = 0,
    CALC_OP_ADD,
    CALC_OP_SUB,
    CALC_OP_MUL,
    CALC_OP_DIV,
} calc_op_t;

typedef struct {
    char expression[64];
    char result[32];
} calculator_history_t;

typedef struct {
    calc_state_t state;
    calc_op_t pending_op;
    double accumulator;
    calc_op_t last_op;
    double last_operand;
    bool has_last_equals;

    char input[32];
    char display[32];

    calculator_history_t history[8];
    uint8_t history_count;
} calculator_engine_t;

void calculator_init(calculator_engine_t *c);
void calculator_press_digit(calculator_engine_t *c, int digit);
void calculator_press_decimal(calculator_engine_t *c);
void calculator_press_operator(calculator_engine_t *c, calc_op_t op);
void calculator_press_equals(calculator_engine_t *c);
void calculator_press_clear(calculator_engine_t *c);
void calculator_press_sign(calculator_engine_t *c);
void calculator_press_percent(calculator_engine_t *c);
const char *calculator_display(const calculator_engine_t *c);
bool calculator_is_all_clear(const calculator_engine_t *c);
void calculator_clear_history(calculator_engine_t *c);
void calculator_format_double(double value, char *out, uint32_t out_len);

#ifdef __cplusplus
}
#endif
