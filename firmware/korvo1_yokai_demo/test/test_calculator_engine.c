#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "calculator_engine.h"

static void test_basic_addition(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 1 + 2 = 3 */
    calculator_press_digit(&c, 1);
    assert(strcmp(calculator_display(&c), "1") == 0);
    assert(!calculator_is_all_clear(&c));

    calculator_press_operator(&c, CALC_OP_ADD);
    calculator_press_digit(&c, 2);
    assert(strcmp(calculator_display(&c), "2") == 0);

    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "3") == 0);
    assert(c.history_count == 1);
    assert(strcmp(c.history[0].expression, "1 + 2") == 0);
    assert(strcmp(c.history[0].result, "3") == 0);
}

static void test_decimals_and_precision(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 0.1 + 0.2 = 0.3 */
    calculator_press_digit(&c, 0);
    calculator_press_decimal(&c);
    calculator_press_digit(&c, 1);
    assert(strcmp(calculator_display(&c), "0.1") == 0);

    calculator_press_operator(&c, CALC_OP_ADD);
    calculator_press_digit(&c, 0);
    calculator_press_decimal(&c);
    calculator_press_digit(&c, 2);
    calculator_press_equals(&c);

    assert(strcmp(calculator_display(&c), "0.3") == 0);
}

static void test_operator_replacement(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 5 + (replace with *) 3 = 15 */
    calculator_press_digit(&c, 5);
    calculator_press_operator(&c, CALC_OP_ADD);
    calculator_press_operator(&c, CALC_OP_MUL);
    calculator_press_digit(&c, 3);
    calculator_press_equals(&c);

    assert(strcmp(calculator_display(&c), "15") == 0);
}

static void test_chained_operations(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 10 - 2 * 3 = 24 (left-to-right sequential evaluation) */
    calculator_press_digit(&c, 1);
    calculator_press_digit(&c, 0);
    calculator_press_operator(&c, CALC_OP_SUB);
    calculator_press_digit(&c, 2);
    calculator_press_operator(&c, CALC_OP_MUL); /* commits 10 - 2 = 8 */
    assert(strcmp(calculator_display(&c), "8") == 0);

    calculator_press_digit(&c, 3);
    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "24") == 0);
    assert(c.history_count == 2);
}

static void test_repeat_equals(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 5 + 2 = 7, = 9, = 11 */
    calculator_press_digit(&c, 5);
    calculator_press_operator(&c, CALC_OP_ADD);
    calculator_press_digit(&c, 2);
    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "7") == 0);

    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "9") == 0);

    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "11") == 0);
}

static void test_divide_by_zero(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 7 / 0 = エラー */
    calculator_press_digit(&c, 7);
    calculator_press_operator(&c, CALC_OP_DIV);
    calculator_press_digit(&c, 0);
    calculator_press_equals(&c);

    assert(strcmp(calculator_display(&c), "エラー") == 0);

    /* Clear resets from error */
    calculator_press_clear(&c);
    assert(strcmp(calculator_display(&c), "0") == 0);
    assert(calculator_is_all_clear(&c));
}

static void test_sign_and_percent(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* 50 +/- -> -50 */
    calculator_press_digit(&c, 5);
    calculator_press_digit(&c, 0);
    calculator_press_sign(&c);
    assert(strcmp(calculator_display(&c), "-50") == 0);
    calculator_press_sign(&c);
    assert(strcmp(calculator_display(&c), "50") == 0);

    /* 50 % -> 0.5 */
    calculator_press_percent(&c);
    assert(strcmp(calculator_display(&c), "0.5") == 0);

    /* 200 + 10 % = 200 + 20 = 220 */
    calculator_init(&c);
    calculator_press_digit(&c, 2);
    calculator_press_digit(&c, 0);
    calculator_press_digit(&c, 0);
    calculator_press_operator(&c, CALC_OP_ADD);
    calculator_press_digit(&c, 1);
    calculator_press_digit(&c, 0);
    calculator_press_percent(&c);
    assert(strcmp(calculator_display(&c), "20") == 0);
    calculator_press_equals(&c);
    assert(strcmp(calculator_display(&c), "220") == 0);
}

static void test_history_limit(void)
{
    calculator_engine_t c;
    calculator_init(&c);

    /* Perform 10 operations, history must cap at 8 */
    for (int i = 1; i <= 10; i++) {
        calculator_press_digit(&c, i % 10);
        calculator_press_operator(&c, CALC_OP_ADD);
        calculator_press_digit(&c, 1);
        calculator_press_equals(&c);
    }

    assert(c.history_count == 8);
    /* Latest item should be (10 % 10) + 1 = 0 + 1 = 1 */
    assert(strcmp(c.history[7].result, "1") == 0);

    calculator_clear_history(&c);
    assert(c.history_count == 0);
}

int main(void)
{
    test_basic_addition();
    test_decimals_and_precision();
    test_operator_replacement();
    test_chained_operations();
    test_repeat_equals();
    test_divide_by_zero();
    test_sign_and_percent();
    test_history_limit();

    printf("All calculator_engine tests PASS\n");
    return 0;
}
