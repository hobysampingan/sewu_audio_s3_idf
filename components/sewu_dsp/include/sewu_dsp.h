#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void sewu_dsp_init(void);
void sewu_dsp_update(void);
void sewu_dsp_process_frame(float *left, float *right);

#ifdef __cplusplus
}
#endif