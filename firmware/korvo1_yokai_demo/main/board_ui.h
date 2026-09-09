#pragma once

#include "esp_err.h"

#include "app_state.h"

/** Initialize the Korvo-1 RGB panel, GT1151 touch controller, and GSP bundle. */
esp_err_t board_ui_start(app_state_t *state);
