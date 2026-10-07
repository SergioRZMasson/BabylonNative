#ifndef BL_AUDIO_INTERNAL_H
#define BL_AUDIO_INTERNAL_H

#include "RuntimeInternal.h"

struct L_Audio
{
    L_Record record;
    bl_HostAudioContext context;
    bl_HostAudioNode master;
    bl_HostAudioNode bus;
    double volume;
    double rampDuration;
    double retryInterval;
    double lastPoll;
    double nextRetry;
    bool offline;
    bool resumeInteraction;
    bool resumePause;
    bool started;
    bool hasPoll;
    bool paused;
};

struct L_Source
{
    L_Record record;
    L_Audio* engine;
    bl_HostAudioNode input;
    bl_HostAudioNode gain;
    bl_String name;
    double volume;
};

static_assert(__is_trivial(L_Audio) && __is_standard_layout(L_Audio), "Audio storage must be POD");
static_assert(__is_trivial(L_Source) && __is_standard_layout(L_Source),
              "Source storage must be POD");

bl_Status l_audioDispose(bl_Runtime* r, L_Audio* a);

#endif
