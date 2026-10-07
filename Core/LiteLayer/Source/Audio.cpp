#include "LiteInternal.h"

static bl_Status contextInfo(bl_Runtime* r, L_Audio* a, bl_HostAudioInfo* out)
{
    bl_HostAudioInfo info = {};
    bl_Status s;
    L_CALL(r, s, r->audio.getContextInfo(r->audio.userData, a->context, &info));
    if (s != BL_OK)
    {
        return L_FAIL(r, s, "Audio context query failed");
    }
    if ((unsigned)info.state > BL_AUDIO_INTERRUPTED || !isfinite(info.currentTime) ||
        info.currentTime < 0 || !isfinite(info.sampleRate) || info.sampleRate <= 0)
    {
        return L_FAIL(r, BL_HOST_ERROR, "Invalid host audio clock");
    }
    *out = info;
    return BL_OK;
}

static bl_Status ramp(bl_Runtime* r, L_Audio* a, bl_HostAudioNode node, double value,
                      const bl_RampOptions* o)
{
    if (!isfinite(value) || (o && ((unsigned)o->shape > BL_AUDIO_RAMP_LOGARITHMIC ||
                                   (o->duration.present && !isfinite(o->duration.value)))))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_HostAudioInfo info;
    L_TRY(contextInfo(r, a, &info));
    bl_Status s;
    double duration = a->rampDuration;
    if (o && o->duration.present && o->duration.value > duration)
    {
        duration = o->duration.value;
    }
    L_CALL(r, s, r->audio.cancelScheduledParameter(r->audio.userData, node, BL_AUDIO_GAIN, 0));
    if (s != BL_OK)
    {
        return L_FAIL(r, s, "Cancel audio parameter failed");
    }
    bl_AudioRampShape shape = o ? o->shape : BL_AUDIO_RAMP_LINEAR;
    if (shape == BL_AUDIO_RAMP_NONE || duration < .000001)
    {
        L_CALL(
            r, s,
            r->audio.setParameter(r->audio.userData, node, BL_AUDIO_GAIN, value, info.currentTime));
    }
    else
    {
        double from = 0;
        L_CALL(r, s, r->audio.getParameter(r->audio.userData, node, BL_AUDIO_GAIN, &from));
        if (s != BL_OK)
        {
            return L_FAIL(r, s, "Read audio parameter failed");
        }
        if (!isfinite(from))
        {
            from = 0;
        }
        float normalized[100] = {};
        float values[100];
        size_t count = 2;
        if (shape == BL_AUDIO_RAMP_LINEAR)
        {
            values[0] = (float)from;
            values[1] = (float)value;
        }
        else
        {
            count = 100;
            if (shape == BL_AUDIO_RAMP_EXPONENTIAL)
            {
                double increment = 1.0 / 99;
                double x = increment;
                for (unsigned i = 1; i < 100; ++i)
                {
                    normalized[i] = (float)exp(-11.512925464970227 * (1 - x));
                    x += increment;
                }
            }
            else
            {
                double increment = .01;
                double x = increment;
                for (unsigned i = 0; i < 100; ++i)
                {
                    normalized[i] = (float)(1 + log10(x) / log10(100.0));
                    x += increment;
                }
            }
            double range = fabs(value - from);
            for (unsigned i = 0; i < 100; ++i)
            {
                values[i] = (float)(value > from ? from + range * normalized[i]
                                                 : from - range * (1.0 - normalized[99 - i]));
            }
        }
        L_CALL(r, s,
               r->audio.setParameterCurve(r->audio.userData, node, BL_AUDIO_GAIN, {values, count},
                                          info.currentTime, duration));
    }
    return s == BL_OK ? BL_OK : L_FAIL(r, s, "Audio parameter scheduling failed");
}

static void releaseAudioNode(bl_Runtime* r, bl_HostAudioNode node)
{
    if (!node)
    {
        return;
    }
    l_enterHost(r);
    r->audio.releaseNode(r->audio.userData, node);
    l_leaveHost(r);
}

static bl_Status disposeSource(bl_Runtime* r, L_Source* s)
{
    if (s->record.disposed)
    {
        return BL_OK;
    }
    bl_Status status;
    if (s->input)
    {
        L_CALL(r, status, r->audio.disconnect(r->audio.userData, s->input));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Source input disconnect failed");
        }
    }
    if (s->gain)
    {
        L_CALL(r, status, r->audio.disconnect(r->audio.userData, s->gain));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Source gain disconnect failed");
        }
        releaseAudioNode(r, s->gain);
    }
    s->input = s->gain = NULL;
    l_free(r, (void*)s->name.data);
    s->name = {};
    l_unpin(&s->engine->record);
    s->record.disposed = true;
    return BL_OK;
}

static void sourceCleanup(bl_Runtime* r, L_Record* p)
{
    p->cleanupStatus = disposeSource(r, (L_Source*)p);
}

bl_Status l_audioDispose(bl_Runtime* r, L_Audio* a)
{
    if (a->record.disposed)
    {
        return BL_OK;
    }
    bl_Status status;
    for (size_t i = 0; i < r->count; ++i)
    {
        L_Record* p = r->records[i];
        if (p && p->kind == L_SOURCE && !p->disposed && ((L_Source*)p)->engine == a)
        {
            L_TRY(disposeSource(r, (L_Source*)p));
        }
    }
    if (a->bus)
    {
        L_CALL(r, status, r->audio.disconnect(r->audio.userData, a->bus));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Audio bus disconnect failed");
        }
        releaseAudioNode(r, a->bus);
        a->bus = NULL;
    }
    if (a->master)
    {
        L_CALL(r, status, r->audio.disconnect(r->audio.userData, a->master));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Audio master disconnect failed");
        }
        releaseAudioNode(r, a->master);
        a->master = NULL;
    }
    if (!a->offline && a->context)
    {
        L_CALL(r, status, r->audio.closeContext(r->audio.userData, a->context));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Audio context close failed");
        }
    }
    a->context = NULL;
    a->record.disposed = true;
    return BL_OK;
}

static void audioCleanup(bl_Runtime* r, L_Record* p)
{
    p->cleanupStatus = l_audioDispose(r, (L_Audio*)p);
}

bl_Status bl_createAudioEngineAsync(bl_Runtime* r, const bl_AudioEngineOptions* o,
                                    bl_AudioEngine* out)
{
    L_TRY(l_check(r));
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_AudioService* s = &r->audio;
    if (!r->hasAudio || !s->getContextInfo || !s->getDestination || !s->createGain || !s->connect ||
        !s->disconnect || !s->setParameter || !s->getParameter || !s->cancelScheduledParameter ||
        !s->setParameterCurve || !s->releaseNode || !s->resumeContext || !s->closeContext ||
        ((!o || !o->audioContext) && !s->createContext))
    {
        return L_FAIL(r, BL_AUDIO_UNAVAILABLE, "No complete audio graph service");
    }
    double volume = o && o->volume.present ? o->volume.value : 1;
    double duration = o && o->parameterRampDuration.present ? o->parameterRampDuration.value : .01;
    double retry =
        o && o->resumeOnPauseRetryInterval.present ? o->resumeOnPauseRetryInterval.value : 1000;
    if (!isfinite(volume) || !isfinite(duration) || !isfinite(retry) || retry < 0 ||
        (o && ((unsigned)o->resumeOnInteraction > BL_BOOL_TRUE ||
               (unsigned)o->resumeOnPause > BL_BOOL_TRUE)))
    {
        return BL_INVALID_ARGUMENT;
    }
    L_NEW(r, L_AUDIO, 20, audioCleanup, L_Audio, a);
    a->volume = volume;
    a->rampDuration = duration < 0 ? 0 : duration;
    a->retryInterval = retry;
    a->resumeInteraction = !o || o->resumeOnInteraction != BL_BOOL_FALSE;
    a->resumePause = !o || o->resumeOnPause != BL_BOOL_FALSE;
    bool created = !o || !o->audioContext;
    bl_Status status = BL_OK;
    if (created)
    {
        L_CALL(r, status, s->createContext(s->userData, &a->context));
    }
    else
    {
        a->context = o->audioContext;
    }
    if (status == BL_OK && !a->context)
    {
        status = BL_HOST_ERROR;
    }
    bl_HostAudioInfo info = {};
    bl_HostAudioNode destination = NULL;
    if (status == BL_OK)
    {
        status = contextInfo(r, a, &info);
        a->offline = info.offline;
        a->started = info.state == BL_AUDIO_RUNNING;
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->getDestination(s->userData, a->context, &destination));
        if (status == BL_OK && !destination)
        {
            status = BL_HOST_ERROR;
        }
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->createGain(s->userData, a->context, &a->master));
        if (status == BL_OK && !a->master)
        {
            status = BL_HOST_ERROR;
        }
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->createGain(s->userData, a->context, &a->bus));
        if (status == BL_OK && !a->bus)
        {
            status = BL_HOST_ERROR;
        }
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->connect(s->userData, a->master, destination));
    }
    if (status == BL_OK)
    {
        status = ramp(r, a, a->master, volume, NULL);
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->setParameter(s->userData, a->bus, BL_AUDIO_GAIN, 1, info.currentTime));
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, s->connect(s->userData, a->bus, a->master));
    }
    if (status != BL_OK)
    {
        if (a->master)
        {
            bl_Status ignored;
            L_CALL(r, ignored, s->disconnect(s->userData, a->master));
            (void)ignored;
            releaseAudioNode(r, a->master);
        }
        if (a->bus)
        {
            bl_Status ignored;
            L_CALL(r, ignored, s->disconnect(s->userData, a->bus));
            (void)ignored;
            releaseAudioNode(r, a->bus);
        }
        if (created && a->context && !a->offline)
        {
            bl_Status ignored;
            L_CALL(r, ignored, s->closeContext(s->userData, a->context));
            (void)ignored;
        }
        a->master = a->bus = NULL;
        a->context = NULL;
        a->record.disposed = true;
        return L_FAIL(r, status, "Audio graph creation failed");
    }
    *out = {r, a->record.id};
    return BL_OK;
}

bl_Status bl_unlockAudioEngineAsync(bl_AudioEngine h)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    bl_Runtime* r = h._runtime;
    bl_HostAudioInfo info;
    L_TRY(contextInfo(r, a, &info));
    if (info.offline)
    {
        return BL_OK;
    }
    if (info.state != BL_AUDIO_RUNNING)
    {
        bl_Status status;
        L_CALL(r, status, r->audio.resumeContext(r->audio.userData, a->context));
        if (status != BL_OK)
        {
            return L_FAIL(r, status, "Audio resume failed");
        }
        L_TRY(contextInfo(r, a, &info));
    }
    if (info.state != BL_AUDIO_RUNNING)
    {
        return L_FAIL(r, BL_AUDIO_SUSPENDED, "Audio remains suspended");
    }
    a->started = true;
    return BL_OK;
}

bl_Status bl_getAudioEngineInfo(bl_AudioEngine h, bl_AudioEngineInfo* out)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_AudioEngineInfo info;
    L_TRY(contextInfo(h._runtime, a, &info.context));
    info.volume = a->volume;
    *out = info;
    return BL_OK;
}

bl_Status bl_getAudioContext(bl_AudioEngine h, bl_HostAudioContext* out,
                             const bl_AudioService** service)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    if (!out || !service)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = a->context;
    *service = &h._runtime->audio;
    return BL_OK;
}

bl_Status bl_setMasterVolume(bl_AudioEngine h, double value, const bl_RampOptions* o)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    L_TRY(ramp(h._runtime, a, a->master, value, o));
    a->volume = value;
    return BL_OK;
}

bl_Status bl_getMasterVolume(bl_AudioEngine h, double* out)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    if (!out)
    {
        return BL_INVALID_ARGUMENT;
    }
    *out = a->volume;
    return BL_OK;
}

bl_Status bl_createSoundSourceAsync(bl_AudioEngine h, bl_HostAudioNode input,
                                    const bl_SoundSourceOptions* o, bl_AudioInputSource* out)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    bl_Runtime* r = h._runtime;
    bl_String name = o ? o->name : bl_String{};
    double volume = o && o->volume.present ? o->volume.value : 1;
    if (!input || !out || !l_string(name) || !isfinite(volume) ||
        (o && (unsigned)o->outBusAutoDefault > BL_BOOL_TRUE))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_String copy = l_copyString(r, name);
    if (!copy.data)
    {
        return BL_OUT_OF_MEMORY;
    }
    L_Record* record;
    bl_Status status = l_record(r, sizeof(L_Source), L_SOURCE, 30, sourceCleanup, &record);
    if (status != BL_OK)
    {
        l_free(r, (void*)copy.data);
        return status;
    }
    L_Source* s = (L_Source*)record;
    s->engine = a;
    l_pin(&a->record);
    s->name = copy;
    s->volume = volume;
    L_CALL(r, status, r->audio.createGain(r->audio.userData, a->context, &s->gain));
    if (status == BL_OK && !s->gain)
    {
        status = BL_HOST_ERROR;
    }
    if (status == BL_OK)
    {
        status = ramp(r, a, s->gain, volume, NULL);
    }
    if (status == BL_OK && (!o || o->outBusAutoDefault != BL_BOOL_FALSE))
    {
        L_CALL(r, status, r->audio.connect(r->audio.userData, s->gain, a->bus));
    }
    if (status == BL_OK)
    {
        L_CALL(r, status, r->audio.connect(r->audio.userData, input, s->gain));
    }
    if (status != BL_OK)
    {
        disposeSource(r, s);
        return L_FAIL(r, status, "Audio source routing failed");
    }
    s->input = input;
    *out = {r, s->record.id};
    return BL_OK;
}

bl_Status bl_setSoundSourceVolume(bl_AudioInputSource h, double value, const bl_RampOptions* o)
{
    L_GET(h, L_SOURCE, L_Source, s);
    L_TRY(ramp(h._runtime, s->engine, s->gain, value, o));
    s->volume = value;
    return BL_OK;
}

bl_Status bl_disposeSoundSource(bl_AudioInputSource h)
{
    L_RETIRED(h, L_SOURCE, L_Source, s);
    return s ? disposeSource(h._runtime, s) : BL_OK;
}

bl_Status bl_disposeAudioEngine(bl_AudioEngine h)
{
    L_RETIRED(h, L_AUDIO, L_Audio, a);
    return a ? l_audioDispose(h._runtime, a) : BL_OK;
}

bl_Status bl_audioUserGesture(bl_AudioEngine h)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    if (a->offline || !a->resumeInteraction)
    {
        return BL_OK;
    }
    return bl_unlockAudioEngineAsync(h);
}

bl_Status bl_pollAudioEngine(bl_AudioEngine h, double time)
{
    L_GET(h, L_AUDIO, L_Audio, a);
    if (!isfinite(time) || time < 0 || (a->hasPoll && time < a->lastPoll))
    {
        return BL_INVALID_ARGUMENT;
    }
    bl_HostAudioInfo info;
    L_TRY(contextInfo(h._runtime, a, &info));
    bool paused = !info.offline &&
                  (info.state == BL_AUDIO_STATE_SUSPENDED || info.state == BL_AUDIO_INTERRUPTED) &&
                  a->started && a->resumePause;
    if (info.state == BL_AUDIO_RUNNING)
    {
        a->started = true;
    }
    if (paused && !a->paused)
    {
        a->nextRetry = time + a->retryInterval;
    }
    a->lastPoll = time;
    a->hasPoll = true;
    a->paused = paused;
    if (paused && time >= a->nextRetry)
    {
        a->nextRetry = time + a->retryInterval;
        return bl_unlockAudioEngineAsync(h);
    }
    return BL_OK;
}
