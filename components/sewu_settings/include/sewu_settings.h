#pragma once

#include "sewu_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void sewu_settings_init(void);
void sewu_settings_update(void);

void sewu_settings_get_user_preset(int slot, sewu_eq_preset_t *out);
void sewu_settings_set_user_preset(int slot, const sewu_eq_preset_t *in);

#ifdef __cplusplus
}
#endif