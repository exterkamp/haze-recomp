#ifndef HAZE_AUDIO_FILTER_H
#define HAZE_AUDIO_FILTER_H
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental whole-loop replacement for validated PIC04 2FA40 -> 2FA78.
 * Returns zero without mutation when the original loop must be used. The
 * caller supplies its actual generated continuation; no address registry or
 * audio completion protocol is replaced here. */
int haze_audio_filter_try_native(spu_context* ctx,
    void (*continuation)(spu_context*), int permitted);
#ifdef __cplusplus
}
#endif
#endif
