#include "calculator_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *op_ascii(calc_op_t op)
{
    switch (op) {
    case CALC_OP_ADD: return "+";
    case CALC_OP_SUB: return "-";
    case CALC_OP_MUL: return "*";
    case CALC_OP_DIV: return "/";
    default: return "";
    }
}

void calculator_format_double(double value, char *out, uint32_t out_len)
{
    if (!out || out_len == 0) return;
    if (!isfinite(value)) {
        snprintf(out, out_len, "エラー");
        return;
    }
    if (fabs(value) < 5e-13) value = 0.0;
    snprintf(out, out_len, "%.12g", value);
    if (strcmp(out, "-0") == 0) snprintf(out, out_len, "0");
}

static double input_value(const calculator_engine_t *c)
{
    return strtod(c->input, NULL);
}

static void set_display_from_input(calculator_engine_t *c)
{
    snprintf(c->display, sizeof(c->display), "%s", c->input);
}

static void set_input_from_double(calculator_engine_t *c, double v)
{
    calculator_format_double(v, c->input, sizeof(c->input));
    snprintf(c->display, sizeof(c->display), "%s", c->input);
}

static bool apply(calc_op_t op, double a, double b, double *out)
{
    switch (op) {
    case CALC_OP_ADD: *out = a + b; break;
    case CALC_OP_SUB: *out = a - b; break;
    case CALC_OP_MUL: *out = a * b; break;
    case CALC_OP_DIV:
        if (fabs(b) < 1e-15) return false;
        *out = a / b;
        break;
    default:
        *out = b;
        break;
    }
    return isfinite(*out);
}

static void push_history(calculator_engine_t *c,
                         double a, calc_op_t op, double b, double result)
{
    if (op == CALC_OP_NONE) return;

    if (c->history_count == 8) {
        memmove(&c->history[0], &c->history[1],
                7 * sizeof(c->history[0]));
        c->history_count = 7;
    }

    calculator_history_t *h = &c->history[c->history_count++];
    char aa[24], bb[24];
    calculator_format_double(a, aa, sizeof(aa));
    calculator_format_double(b, bb, sizeof(bb));
    snprintf(h->expression, sizeof(h->expression), "%s %s %s",
             aa, op_ascii(op), bb);
    calculator_format_double(result, h->result, sizeof(h->result));
}

static void enter_error(calculator_engine_t *c)
{
    c->state = CALC_STATE_ERROR;
    c->pending_op = CALC_OP_NONE;
    c->has_last_equals = false;
    snprintf(c->input, sizeof(c->input), "0");
    snprintf(c->display, sizeof(c->display), "エラー");
}

void calculator_init(calculator_engine_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->state = CALC_STATE_INPUT_A;
    snprintf(c->input, sizeof(c->input), "0");
    snprintf(c->display, sizeof(c->display), "0");
}

void calculator_press_digit(calculator_engine_t *c, int digit)
{
    if (!c || digit < 0 || digit > 9) return;
    if (c->state == CALC_STATE_ERROR || c->state == CALC_STATE_RESULT) {
        calc_state_t old = c->state;
        uint8_t hist_count = c->history_count;
        calculator_history_t hist[8];
        memcpy(hist, c->history, sizeof(hist));
        calculator_init(c);
        c->history_count = hist_count;
        memcpy(c->history, hist, sizeof(hist));
        (void)old;
    }
    if (c->state == CALC_STATE_OPERATOR) {
        snprintf(c->input, sizeof(c->input), "0");
        c->state = CALC_STATE_INPUT_B;
    }

    size_t len = strlen(c->input);
    if (len >= 15) return;

    if (strcmp(c->input, "0") == 0) {
        c->input[0] = (char)('0' + digit);
        c->input[1] = '\0';
    } else if (strcmp(c->input, "-0") == 0) {
        c->input[1] = (char)('0' + digit);
        c->input[2] = '\0';
    } else {
        c->input[len] = (char)('0' + digit);
        c->input[len+1] = '\0';
    }
    set_display_from_input(c);
}

void calculator_press_decimal(calculator_engine_t *c)
{
    if (!c) return;
    if (c->state == CALC_STATE_ERROR || c->state == CALC_STATE_RESULT) {
        uint8_t hist_count = c->history_count;
        calculator_history_t hist[8];
        memcpy(hist, c->history, sizeof(hist));
        calculator_init(c);
        c->history_count = hist_count;
        memcpy(c->history, hist, sizeof(hist));
    }
    if (c->state == CALC_STATE_OPERATOR) {
        snprintf(c->input, sizeof(c->input), "0");
        c->state = CALC_STATE_INPUT_B;
    }
    if (strchr(c->input, '.')) return;
    size_t len = strlen(c->input);
    if (len + 1 >= sizeof(c->input)) return;
    c->input[len] = '.';
    c->input[len+1] = '\0';
    set_display_from_input(c);
}

void calculator_press_operator(calculator_engine_t *c, calc_op_t op)
{
    if (!c || op == CALC_OP_NONE || c->state == CALC_STATE_ERROR) return;

    double cur = input_value(c);

    if (c->state == CALC_STATE_INPUT_B && c->pending_op != CALC_OP_NONE) {
        double result;
        double a = c->accumulator;
        if (!apply(c->pending_op, a, cur, &result)) {
            enter_error(c);
            return;
        }
        push_history(c, a, c->pending_op, cur, result);
        c->accumulator = result;
        set_input_from_double(c, result);
    } else if (c->state == CALC_STATE_OPERATOR) {
        c->pending_op = op; /* operator replacement */
        return;
    } else {
        c->accumulator = cur;
    }

    c->pending_op = op;
    c->state = CALC_STATE_OPERATOR;
    c->has_last_equals = false;
}

void calculator_press_equals(calculator_engine_t *c)
{
    if (!c || c->state == CALC_STATE_ERROR) return;

    calc_op_t op = c->pending_op;
    double a = c->accumulator;
    double b = input_value(c);

    if (c->state == CALC_STATE_RESULT && c->has_last_equals) {
        op = c->last_op;
        a = input_value(c);
        b = c->last_operand;
    } else if (op == CALC_OP_NONE) {
        return;
    } else if (c->state == CALC_STATE_OPERATOR) {
        b = a;
    }

    double result;
    if (!apply(op, a, b, &result)) {
        enter_error(c);
        return;
    }

    push_history(c, a, op, b, result);
    c->last_op = op;
    c->last_operand = b;
    c->has_last_equals = true;
    c->pending_op = CALC_OP_NONE;
    c->accumulator = result;
    set_input_from_double(c, result);
    c->state = CALC_STATE_RESULT;
}

void calculator_press_clear(calculator_engine_t *c)
{
    if (!c) return;
    if (c->state == CALC_STATE_ERROR ||
        strcmp(c->input, "0") == 0 ||
        c->state == CALC_STATE_RESULT) {
        uint8_t hist_count = c->history_count;
        calculator_history_t hist[8];
        memcpy(hist, c->history, sizeof(hist));
        calculator_init(c);
        c->history_count = hist_count;
        memcpy(c->history, hist, sizeof(hist));
    } else {
        snprintf(c->input, sizeof(c->input), "0");
        snprintf(c->display, sizeof(c->display), "0");
        if (c->state == CALC_STATE_INPUT_B) c->state = CALC_STATE_OPERATOR;
    }
}

void calculator_press_sign(calculator_engine_t *c)
{
    if (!c || c->state == CALC_STATE_ERROR) return;
    double v = -input_value(c);
    set_input_from_double(c, v);
}

void calculator_press_percent(calculator_engine_t *c)
{
    if (!c || c->state == CALC_STATE_ERROR) return;
    double v = input_value(c);

    if (c->pending_op == CALC_OP_ADD || c->pending_op == CALC_OP_SUB) {
        v = c->accumulator * (v / 100.0);
    } else {
        v = v / 100.0;
    }
    set_input_from_double(c, v);
    if (c->state == CALC_STATE_OPERATOR) c->state = CALC_STATE_INPUT_B;
}

const char *calculator_display(const calculator_engine_t *c)
{
    return c ? c->display : "0";
}

bool calculator_is_all_clear(const calculator_engine_t *c)
{
    if (!c) return true;
    return c->state == CALC_STATE_INPUT_A &&
           c->pending_op == CALC_OP_NONE &&
           strcmp(c->input, "0") == 0;
}

void calculator_clear_history(calculator_engine_t *c)
{
    if (!c) return;
    memset(c->history, 0, sizeof(c->history));
    c->history_count = 0;
}
