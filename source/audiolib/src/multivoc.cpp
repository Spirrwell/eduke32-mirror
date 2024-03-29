/*
Copyright (C) 1994-1995 Apogee Software, Ltd.
Copyright (C) 2015 EDuke32 developers
Copyright (C) 2015 Voidpoint, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
/**********************************************************************
   module: MULTIVOC.C

   author: James R. Dose
   date:   December 20, 1993

   Routines to provide multichannel digitized sound playback for
   Sound Blaster compatible sound cards.

   (c) Copyright 1993 James R. Dose.  All Rights Reserved.
**********************************************************************/

#include "multivoc.h"

#include "_multivc.h"
#include "baselayer.h"
#include "compat.h"
#include "drivers.h"
#include "fx_man.h"
#include "libasync_config.h"
#include "linklist.h"
#include "osd.h"
#include "pitch.h"
#include "pragmas.h"

#ifdef HAVE_XMP
# define BUILDING_STATIC
# include "libxmp-lite/xmp.h"

int MV_XMPInterpolation = XMP_INTERP_NEAREST;
#endif


static void MV_StopVoice(VoiceNode *voice, bool useCallBack = true);
static void MV_ServiceVoc(void);

static VoiceNode *MV_GetVoice(int handle);

static int MV_ReverbLevel;
static int MV_ReverbDelay;
static fix16_t MV_ReverbVolume;

Pan MV_PanTable[MV_NUMPANPOSITIONS][MV_MAXVOLUME + 1];

int MV_Installed;

int MV_BufferSize = MV_MIXBUFFERSIZE;
static int MV_BufferLength;

static int MV_NumberOfBuffers = MV_NUMBEROFBUFFERS;

int MV_MaxVoices = 1;
int MV_Channels = 1;
int MV_MixRate;
void *MV_InitDataPtr;

int MV_LazyAlloc = true;

#ifdef ASS_REVERSESTEREO
static int MV_ReverseStereo;
#endif

static int MV_BufferEmpty[MV_NUMBEROFBUFFERS];
char *MV_MixBuffer[(MV_NUMBEROFBUFFERS << 1) + 1];

VoiceNode *MV_Voices;
VoiceNode  VoiceList;
VoiceNode  VoicePool;

static int MV_MixPage;

static void (*MV_CallBackFunc)(intptr_t);

char *MV_MixDestination;
int MV_SampleSize = 1;
int MV_RightChannelOffset;

int MV_ErrorCode = MV_NotInstalled;

fix16_t MV_GlobalVolume = fix16_one;
fix16_t MV_VolumeSmoothFactor = fix16_one;

thread_local int MV_Locked;
char *MV_MusicBuffer;
static void (*MV_MusicCallback)(void);

static VoiceNode **MV_Handles;

static bool MV_Mix(VoiceNode * const voice, int const buffer)
{
    if (voice->task.valid())
    {
        if (!voice->task.ready())
            return true;

        auto result = voice->task.get();

        if (result != MV_Ok)
        {
            LOG_F(ERROR, "Error playing sound 0x%08" PRIxPTR ": %s", voice->callbackval, MV_ErrorString(result));
            return false;
        }
    }

    if (voice->length == 0 && voice->GetSound(voice) != KeepPlaying)
        return false;

    fix16_t const gv = MV_GlobalVolume;

    if (voice->priority == FX_MUSIC_PRIORITY)
        MV_GlobalVolume = fix16_one;

    int            length = MV_MIXBUFFERSIZE;
    uint32_t       bufsiz = voice->FixedPointBufferSize;
    uint32_t const rate   = voice->RateScale;

    MV_MixDestination = MV_MixBuffer[buffer];

    // Add this voice to the mix
    do
    {
        int            mixlen   = length;
        uint32_t const position = voice->position;
        uint32_t const voclen   = voice->length;

        // Check if the last sample in this buffer would be
        // beyond the length of the sample block
        if ((position + bufsiz) >= voclen)
        {
            if (position >= voclen - voice->channels)
            {
                if (voice->GetSound(voice) != KeepPlaying)
                {
                    MV_GlobalVolume = gv;
                    return false;
                }

                break;
            }

            mixlen = (voclen - position + rate - voice->channels) / rate;
        }

        voice->position = voice->mix(voice, mixlen);
        length -= mixlen;

        if (voice->position >= voclen - voice->channels)
        {
            // Get the next block of sound
            if (voice->GetSound(voice) != KeepPlaying)
            {
                MV_GlobalVolume = gv;
                return false;
            }

            // Get the position of the last sample in the buffer
            if (length > (voice->channels - 1))
                bufsiz = voice->RateScale * (length - voice->channels);
        }
    } while (length > 0);

    MV_GlobalVolume = gv;
    return true;
}

void MV_PlayVoice(VoiceNode *voice)
{
    MV_Lock();
    LL::SortedInsert(&VoiceList, voice, &VoiceNode::priority);
    voice->PannedVolume = voice->GoalVolume;
    voice->Paused.store(false, std::memory_order_release);
    MV_Unlock();
}

static void MV_FreeHandle(VoiceNode* voice)
{
    if (voice->handle < MV_MINVOICEHANDLE)
        return;

    MV_Handles[voice->handle - MV_MINVOICEHANDLE] = nullptr;
    voice->handle = 0;
    voice->length = 0;
    voice->sound = nullptr;
    voice->wavetype = FMT_UNKNOWN;
    LL::Move(voice, &VoicePool);
}

static void MV_CleanupVoice(VoiceNode* voice, bool useCallBack = true)
{
    if (useCallBack && MV_CallBackFunc)
        MV_CallBackFunc(voice->callbackval);

    switch (voice->wavetype)
    {
#ifdef HAVE_VORBIS
        case FMT_VORBIS: MV_ReleaseVorbisVoice(voice); break;
#endif
#ifdef HAVE_FLAC
        case FMT_FLAC:   MV_ReleaseFLACVoice(voice); break;
#endif
        case FMT_XA:     MV_ReleaseXAVoice(voice); break;
#ifdef HAVE_XMP
        case FMT_XMP:    MV_ReleaseXMPVoice(voice); break;
#endif
        default:
            // these are in the default case of this switch instead of down below because the functions above only zero them if MV_LazyAlloc is false
            voice->rawdataptr = nullptr;
            voice->rawdatasiz = 0;
            break;
    }
}

void MV_StopVoice(VoiceNode *voice, bool useCallBack)
{
    MV_CleanupVoice(voice, useCallBack);
    MV_Lock();
    // move the voice from the play list to the free list
    MV_FreeHandle(voice);
    MV_Unlock();
}

/*---------------------------------------------------------------------
   JBF: no synchronisation happens inside MV_ServiceVoc nor the
        supporting functions it calls. This would cause a deadlock
        between the mixer thread in the driver vs the nested
        locking in the user-space functions of MultiVoc. The call
        to MV_ServiceVoc is synchronised in the driver.
---------------------------------------------------------------------*/
static void MV_ServiceVoc(void)
{
    // Toggle which buffer we'll mix next
    ++MV_MixPage;
    MV_MixPage &= MV_NumberOfBuffers-1;

    if (MV_ReverbLevel == 0)
    {
        if (!MV_BufferEmpty[MV_MixPage])
        {
            Bmemset(MV_MixBuffer[MV_MixPage], 0, MV_BufferSize);
            MV_BufferEmpty[MV_MixPage] = TRUE;
        }
    }
    else
    {
        char const *const __restrict end    = MV_MixBuffer[0] + MV_BufferLength;
        char *            __restrict dest   = MV_MixBuffer[MV_MixPage];
        char const *      __restrict source = MV_MixBuffer[MV_MixPage] - MV_ReverbDelay;

        if (source < MV_MixBuffer[0])
            source += MV_BufferLength;

        int length = MV_BufferSize;

        do
        {
            int const count = (source + length > end) ? (end - source) : length;

            MV_Reverb<int16_t>(source, dest, MV_ReverbVolume, count >> 1);

            // if we go through the loop again, it means that we've wrapped around the buffer
            source  = MV_MixBuffer[0];
            dest   += count;
            length -= count;
        } while (length > 0);
    }

    VoiceNode *MusicVoice = nullptr;

    if (VoiceList.next && VoiceList.next != &VoiceList)
    {
        auto voice = VoiceList.next;
        VoiceNode *next;

        do
        {
            next = voice->next;

            if (voice->Paused.load(std::memory_order_acquire))
                continue;

            if (voice->priority == FX_MUSIC_PRIORITY)
            {
                MusicVoice = voice;
                continue;
            }

            MV_BufferEmpty[ MV_MixPage ] = FALSE;

            // Is this voice done?
            if (!MV_Mix(voice, MV_MixPage))
            {
                MV_CleanupVoice(voice);
                MV_FreeHandle(voice);
            }
        }
        while ((voice = next) != &VoiceList);
    }

    Bmemcpy(MV_MixBuffer[MV_MixPage+MV_NumberOfBuffers], MV_MixBuffer[MV_MixPage], MV_BufferSize);

    if (MV_MusicCallback)
    {
        MV_MusicCallback();
        int16_t * __restrict source = (int16_t*)MV_MusicBuffer;
        int16_t * __restrict dest = (int16_t*)MV_MixBuffer[MV_MixPage+MV_NumberOfBuffers];
        for (int32_t i = 0; i < MV_BufferSize>>1; i++, dest++)
            *dest = clamp(*dest + *source++,INT16_MIN, INT16_MAX);
    }

    if (MusicVoice && !MV_Mix(MusicVoice, MV_MixPage + MV_NumberOfBuffers))
    {
        MV_CleanupVoice(MusicVoice);
        MV_FreeHandle(MusicVoice);
    }
}

static VoiceNode *MV_GetVoice(int handle)
{
    if (handle < MV_MINVOICEHANDLE || handle > MV_MaxVoices)
    {
        LOG_F(WARNING, "No voice found for handle 0x%08x", handle);
        return nullptr;
    }

    if (MV_Handles[handle - MV_MINVOICEHANDLE] != nullptr)
        return MV_Handles[handle - MV_MINVOICEHANDLE];

    MV_SetErrorCode(MV_VoiceNotFound);
    return nullptr;
}

VoiceNode *MV_BeginService(int handle)
{
    if (!MV_Installed)
        return nullptr;

    auto voice = MV_GetVoice(handle);

    if (voice == nullptr)
    {
        MV_SetErrorCode(MV_VoiceNotFound);
        return nullptr;
    }

    if (voice->task.valid() && !voice->task.ready())
        voice->task.wait();

    MV_Lock();

    return voice;
}

static inline void MV_EndService(void) { MV_Unlock(); }

int MV_VoicePlaying(int handle)
{
    Bassert(handle <= MV_MaxVoices);
    auto voice = MV_Handles[handle - MV_MINVOICEHANDLE];
    return MV_Installed && voice != nullptr && !voice->Paused.load(std::memory_order_relaxed);
}

int MV_KillAllVoices(bool useCallBack)
{
    if (!MV_Installed)
        return MV_Error;

    MV_Lock();

    if (&VoiceList == VoiceList.next)
    {
        MV_Unlock();
        return MV_Ok;
    }

    auto voice = VoiceList.prev;

    // Remove all the voices from the list
    while (voice != &VoiceList)
    {
        if (voice->priority == MV_MUSIC_PRIORITY)
        {
            voice = voice->prev;
            continue;
        }

        MV_Kill(voice->handle, useCallBack);
        voice = VoiceList.prev;
    }

    MV_Unlock();

    return MV_Ok;
}

int MV_Kill(int handle, bool useCallBack)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    MV_StopVoice(voice, useCallBack);
    MV_EndService();

    return MV_Ok;
}

int MV_VoicesPlaying(void)
{
    if (!MV_Installed)
        return 0;

    MV_Lock();

    int NumVoices = 0;

    for (auto voice = VoiceList.next; voice != &VoiceList; voice = voice->next)
        NumVoices++;

    MV_Unlock();

    return NumVoices;
}

static inline VoiceNode *MV_GetLowestPriorityVoice(void)
{
    auto voice = VoiceList.next;

    // find the voice with the lowest priority and volume
    for (auto node = voice; node != &VoiceList; node = node->next)
    {
        if (node->priority < voice->priority
            || (node->priority == voice->priority && node->PannedVolume.Left < voice->PannedVolume.Left && node->PannedVolume.Right < voice->PannedVolume.Right))
            voice = node;
    }

    return voice;
}

static inline void MV_FinishAllocation(VoiceNode* voice, uint32_t const allocsize)
{
    if (voice->rawdataptr != nullptr && voice->rawdatasiz == allocsize)
        return;
    else if (voice->rawdataptr != nullptr && voice->wavetype >= FMT_VORBIS)
    {
        // this is sort of a hack... wavetypes less than FMT_VORBIS never do their own allocations, so don't bother trying to free them
        ALIGNED_FREE_AND_NULL(voice->rawdataptr);
    }

    voice->rawdatasiz = allocsize;
    voice->rawdataptr = Xaligned_alloc(16, allocsize);
    Bmemset(voice->rawdataptr, 0, allocsize);
}

VoiceNode *MV_AllocVoice(int priority, uint32_t allocsize /* = 0 */)
{
    MV_Lock();

    // Check if we have any free voices
    if (LL::Empty(&VoicePool))
    {
        auto voice = MV_GetLowestPriorityVoice();

        if (voice != &VoiceList && voice->priority <= priority && voice->handle >= MV_MINVOICEHANDLE && FX_SoundValidAndActive(voice->handle))
            MV_Kill(voice->handle);

        if (LL::Empty(&VoicePool))
        {
            // No free voices
            MV_Unlock();
            return nullptr;
        }
    }

    auto voice = VoicePool.next;
    LL::Remove(voice);

    int handle = MV_MINVOICEHANDLE;

    // Find a free voice handle
    do
    {
        if (++handle > MV_MaxVoices)
            handle = MV_MINVOICEHANDLE;
    } while (MV_Handles[handle - MV_MINVOICEHANDLE] != nullptr);
    MV_Handles[handle - MV_MINVOICEHANDLE] = voice;

    voice->length = 0;
    voice->BlockLength = 0;
    voice->handle = handle;
    voice->next = voice->prev = nullptr;
    MV_Unlock();

    if (allocsize)
        MV_FinishAllocation(voice, allocsize);

    return voice;
}

int MV_VoiceAvailable(int priority)
{
    // Check if we have any free voices
    if (!LL::Empty(&VoicePool))
        return TRUE;

    MV_Lock();
    auto const voice = MV_GetLowestPriorityVoice();
    MV_Unlock();

    return (voice == &VoiceList || voice->priority > priority) ? FALSE : TRUE;
}

void MV_SetVoicePitch(VoiceNode *voice, uint32_t rate, int pitchoffset)
{
    voice->SamplingRate = rate;
    voice->PitchScale   = PITCH_GetScale(pitchoffset);
    voice->RateScale    = divideu64((uint64_t)rate * voice->PitchScale, MV_MixRate);

    // Multiply by MV_MIXBUFFERSIZE - 1
    voice->FixedPointBufferSize = (voice->RateScale * MV_MIXBUFFERSIZE) -
                                  voice->RateScale;
}

int MV_SetPitch(int handle, int pitchoffset)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    MV_SetVoicePitch(voice, voice->SamplingRate, pitchoffset);
    MV_EndService();

    return MV_Ok;
}

int MV_SetFrequency(int handle, int frequency)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    MV_SetVoicePitch(voice, frequency, 0);
    MV_EndService();

    return MV_Ok;
}

int MV_GetFrequency(int handle, int *frequency)
{
    auto voice = MV_BeginService(handle);

    if (voice == NULL || !frequency)
        return MV_Error;

    if (voice->SamplingRate == 0)
        voice->GetSound(voice);

    *frequency = voice->SamplingRate;
    MV_EndService();

    return MV_Ok;
}

/*---------------------------------------------------------------------
   Function: MV_SetVoiceMixMode

   Selects which method should be used to mix the voice.

   16Bit        16Bit |  8Bit  16Bit  8Bit  16Bit |
   Mono         Ster  |  Mono  Mono   Ster  Ster  |  Mixer
   Out          Out   |  In    In     In    In    |
----------------------+---------------------------+-------------
    X                 |         X                 | MixMono<int16_t, int16_t>
    X                 |   X                       | MixMono<uint8_t, int16_t>
                 X    |         X                 | MixStereo<int16_t, int16_t>
                 X    |   X                       | MixStereo<uint8_t, int16_t>
----------------------+---------------------------+-------------
                 X    |                      X    | MixStereoStereo<int16_t, int16_t>
                 X    |                X          | MixStereoStereo<uint8_t, int16_t>
    X                 |                      X    | MixMonoStereo<int16_t, int16_t>
    X                 |                X          | MixMonoStereo<uint8_t, int16_t>
---------------------------------------------------------------------*/

void MV_SetVoiceMixMode(VoiceNode *voice)
{
    // stereo look-up table
    static constexpr decltype(voice->mix) mixslut[]
    = { MV_MixStereo<uint8_t, int16_t>,       MV_MixMono<uint8_t, int16_t>,       MV_MixStereo<int16_t, int16_t>,       MV_MixMono<int16_t, int16_t>,
        MV_MixStereoStereo<uint8_t, int16_t>, MV_MixMonoStereo<uint8_t, int16_t>, MV_MixStereoStereo<int16_t, int16_t>, MV_MixMonoStereo<int16_t, int16_t> };

    // corresponds to T_MONO, T_16BITSOURCE, and T_STEREOSOURCE
    voice->mix = mixslut[(MV_Channels == 1) | ((voice->bits == 16) << 1) | ((voice->channels == 2) << 2)];
}

void MV_SetVoiceVolume(VoiceNode *voice, int vol, int left, int right, fix16_t volume)
{
    if (MV_Channels == 1)
        left = right = vol;
#ifdef ASS_REVERSESTEREO
    else if (MV_ReverseStereo)
        swap(&left, &right);
#endif

    voice->GoalVolume = { fix16_smul(fix16_from_int(left), F16(1.f/MV_MAXTOTALVOLUME)), fix16_smul(fix16_from_int(right), F16(1.f/MV_MAXTOTALVOLUME)) };
    voice->volume = volume;

    MV_SetVoiceMixMode(voice);
}

int MV_PauseVoice(int handle, int pause)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    voice->Paused.store(pause, std::memory_order_release);
    MV_EndService();

    return MV_Ok;
}

int MV_GetPosition(int handle, int *position)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    switch (voice->wavetype)
    {
#ifdef HAVE_VORBIS
        case FMT_VORBIS: *position = MV_GetVorbisPosition(voice); break;
#endif
#ifdef HAVE_FLAC
        case FMT_FLAC:   *position = MV_GetFLACPosition(voice); break;
#endif
        case FMT_XA:     *position = MV_GetXAPosition(voice); break;
#ifdef HAVE_XMP
        case FMT_XMP:    *position = MV_GetXMPPosition(voice); break;
#endif
        default:         *position = (int)max<intptr_t>(0, (((intptr_t)voice->NextBlock + (intptr_t)voice->position - (intptr_t)voice->rawdataptr) >> 16) * ((voice->channels * voice->bits) >> 3)); break;
    }

    MV_EndService();

    return MV_Ok;
}

int MV_SetPosition(int handle, int position)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    switch (voice->wavetype)
    {
#ifdef HAVE_VORBIS
        case FMT_VORBIS: MV_SetVorbisPosition(voice, position); break;
#endif
#ifdef HAVE_FLAC
        case FMT_FLAC:   MV_SetFLACPosition(voice, position); break;
#endif
        case FMT_XA:     MV_SetXAPosition(voice, position); break;
#ifdef HAVE_XMP
        case FMT_XMP:    MV_SetXMPPosition(voice, position); break;
#endif
        default: break;
    }

    MV_EndService();

    return MV_Ok;
}

int MV_EndLooping(int handle)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    voice->Loop = {};

    MV_EndService();

    return MV_Ok;
}

int MV_SetPan(int handle, int vol, int left, int right)
{
    auto voice = MV_BeginService(handle);

    if (voice == nullptr)
        return MV_Error;

    MV_SetVoiceVolume(voice, vol, left, right, voice->volume);
    MV_EndService();
    return MV_Ok;
}

int MV_Pan3D(int handle, int angle, int distance)
{
    if (distance < 0)
    {
        distance = -distance;
        angle += MV_NUMPANPOSITIONS / 2;
    }

    int const volume = MIX_VOLUME(distance);

    angle &= MV_MAXPANPOSITION;

    return MV_SetPan(handle, max(0, 255 - distance),
        MV_PanTable[angle][volume].left,
        MV_PanTable[angle][volume].right);
}

void MV_SetReverb(int reverb)
{
    MV_ReverbLevel = MIX_VOLUME(reverb);
    MV_ReverbVolume = fix16_smul(fix16_from_int(MV_ReverbLevel), F16(1.f/MV_MAXVOLUME));
}

int MV_GetMaxReverbDelay(void) { return MV_MIXBUFFERSIZE * MV_NumberOfBuffers; }
int MV_GetReverbDelay(void) { return tabledivide32(MV_ReverbDelay, MV_SampleSize); }

void MV_SetReverbDelay(int delay)
{
    MV_ReverbDelay = max(MV_MIXBUFFERSIZE, min(delay, MV_GetMaxReverbDelay())) * MV_SampleSize;
}

static int MV_SetMixMode(int numchannels)
{
    if (!MV_Installed)
        return MV_Error;

    MV_Channels = 1 + (numchannels == 2);
    MV_SampleSize = sizeof(int16_t) * MV_Channels;

    MV_BufferSize = MV_MIXBUFFERSIZE * MV_SampleSize;
    MV_NumberOfBuffers = tabledivide32(MV_TOTALBUFFERSIZE, MV_BufferSize);
    Bassert(isPow2(MV_NumberOfBuffers));
    MV_BufferLength = MV_TOTALBUFFERSIZE;

    MV_RightChannelOffset = MV_SampleSize >> 1;

    return MV_Ok;
}

static int MV_StartPlayback(void)
{
    // Initialize the buffers
    Bmemset(MV_MixBuffer[0], 0, MV_TOTALBUFFERSIZE << 1);

    for (int buffer = 0; buffer < MV_NumberOfBuffers; buffer++)
        MV_BufferEmpty[buffer] = TRUE;

    MV_MixPage = 1;

    if (SoundDriver_PCM_BeginPlayback(MV_MixBuffer[MV_NumberOfBuffers], MV_BufferSize, MV_NumberOfBuffers, MV_ServiceVoc) != MV_Ok)
        return MV_SetErrorCode(MV_DriverError);

    return MV_Ok;
}

static void MV_StopPlayback(void)
{
    SoundDriver_PCM_StopPlayback();

    // Make sure all callbacks are done.
    MV_Lock();

    for (VoiceNode *voice = VoiceList.next, *next; voice != &VoiceList; voice = next)
    {
        next = voice->next;
        MV_StopVoice(voice);
    }

    MV_Unlock();
}

static void MV_CalcPanTable(void)
{
    const int HalfAngle = MV_NUMPANPOSITIONS / 2;
    const int QuarterAngle = HalfAngle / 2;

    for (int distance = 0; distance <= MV_MAXVOLUME; distance++)
    {
        const int level = (255 * (MV_MAXVOLUME - distance)) / MV_MAXVOLUME;

        for (int angle = 0; angle <= QuarterAngle; angle++)
        {
            const int ramp = level - (level * angle) / QuarterAngle;

            MV_PanTable[angle][distance].left = ramp;
            MV_PanTable[angle][distance].right = level;

            MV_PanTable[HalfAngle - angle][distance].left = ramp;
            MV_PanTable[HalfAngle - angle][distance].right = level;

            MV_PanTable[HalfAngle + angle][distance].left = level;
            MV_PanTable[HalfAngle + angle][distance].right = ramp;

            MV_PanTable[MV_MAXPANPOSITION - angle][distance].left = level;
            MV_PanTable[MV_MAXPANPOSITION - angle][distance].right = ramp;
        }
    }
}

void MV_SetVolume(int volume) { MV_GlobalVolume = fix16_smul(fix16_from_int(volume), F16(1.f/MV_MAXTOTALVOLUME)); }

int MV_GetVolume(void) { return Blrintf(fix16_to_float(MV_GlobalVolume) * MV_MAXTOTALVOLUME); }

void MV_SetCallBack(void (*function)(intptr_t)) { MV_CallBackFunc = function; }

#ifdef ASS_REVERSESTEREO
void MV_SetReverseStereo(int setting) { MV_ReverseStereo = setting; }
int MV_GetReverseStereo(void) { return MV_ReverseStereo; }
#endif

int MV_Init(int soundcard, int MixRate, int Voices, int numchannels, void *initdata)
{
    if (MV_Installed)
        MV_Shutdown();

    MV_SetErrorCode(MV_Ok);

    int const totalmem = Voices * sizeof(VoiceNode) + (MV_TOTALBUFFERSIZE * sizeof(int16_t)) + (MV_MIXBUFFERSIZE * numchannels * sizeof(int16_t));

    char *ptr = (char *) Xaligned_calloc(16, 1, totalmem);

    MV_Voices = (VoiceNode *)ptr;
    ptr += Voices * sizeof(VoiceNode);
    Bassert(Voices < MV_MAXVOICES);

    MV_MaxVoices = Voices;

    LL::Reset((VoiceNode*) &VoiceList);
    LL::Reset((VoiceNode*) &VoicePool);

    for (int index = 0; index < Voices; index++)
        LL::Insert(&VoicePool, &MV_Voices[index]);

    MV_Handles = (VoiceNode **)Xaligned_calloc(16, Voices, sizeof(intptr_t));
#ifdef ASS_REVERSESTEREO
    MV_SetReverseStereo(FALSE);
#endif

    ASS_PCMSoundDriver = soundcard;

    // Initialize the sound card

    if (SoundDriver_PCM_Init(&MixRate, &numchannels, initdata) != MV_Ok)
        MV_SetErrorCode(MV_DriverError);

    if (MV_ErrorCode != MV_Ok)
    {
        ALIGNED_FREE_AND_NULL(MV_Voices);

        return MV_Error;
    }

    MV_Installed    = TRUE;
    MV_InitDataPtr  = initdata;
    MV_CallBackFunc = nullptr;
    MV_ReverbLevel  = 0;
    MV_ReverbVolume = 0.f;

    // Set the sampling rate
    MV_MixRate = MixRate;

    // Set Mixer to play stereo digitized sound
    MV_SetMixMode(numchannels);
    MV_ReverbDelay = MV_BufferSize * 3;

    // Make sure we don't cross a physical page
    MV_MixBuffer[MV_NumberOfBuffers<<1] = ptr;
    for (int buffer = 0; buffer < MV_NumberOfBuffers<<1; buffer++)
    {
        MV_MixBuffer[buffer] = ptr;
        ptr += MV_BufferSize;
    }

    MV_MusicBuffer = ptr;

    // Calculate pan table
    MV_CalcPanTable();

    MV_VolumeSmoothFactor = fix16_from_float(1.f-powf(0.1f, 30.f/MixRate));

    // Start the playback engine
    if (MV_StartPlayback() != MV_Ok)
    {
        // Preserve error code while we shutdown.
        int status = MV_ErrorCode;
        MV_Shutdown();
        return MV_SetErrorCode(status);
    }

    return MV_Ok;
}

int MV_Shutdown(void)
{
    if (!MV_Installed)
        return MV_Ok;

    MV_KillAllVoices();

    MV_Installed = FALSE;

    // Stop the sound playback engine
    MV_StopPlayback();

    // Shutdown the sound card
    SoundDriver_PCM_Shutdown();

    // Free any voices we allocated
    ALIGNED_FREE_AND_NULL(MV_Voices);

    LL::Reset((VoiceNode*) &VoiceList);
    LL::Reset((VoiceNode*) &VoicePool);

    ALIGNED_FREE_AND_NULL(MV_Handles);

    MV_MaxVoices = 1;

    // Release the descriptor from our mix buffer
    for (int buffer = 0; buffer < MV_NUMBEROFBUFFERS<<1; buffer++)
        MV_MixBuffer[buffer] = nullptr;

    MV_SetErrorCode(MV_NotInstalled);

    return MV_Ok;
}

void MV_HookMusicRoutine(void(*callback)(void))
{
    MV_Lock();
    MV_MusicCallback = callback;
    MV_Unlock();
}

void MV_UnhookMusicRoutine(void)
{
    if (MV_MusicCallback)
    {
        MV_Lock();
        MV_MusicCallback = nullptr;
        MV_Unlock();
    }
}

MV_MusicRoutineBuffer MV_GetMusicRoutineBuffer()
{
    return MV_MusicRoutineBuffer{ MV_MusicBuffer, MV_BufferSize };
}

const char *loopStartTags[loopStartTagCount] = { "LOOP_START", "LOOPSTART", "LOOP" };
const char *loopEndTags[loopEndTagCount] = { "LOOP_END", "LOOPEND" };
const char *loopLengthTags[loopLengthTagCount] = { "LOOP_LENGTH", "LOOPLENGTH" };

const char *MV_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case MV_Error:
            return MV_ErrorString(MV_ErrorCode);
        case MV_Ok:
            return "Multivoc ok.";
        case MV_NotInstalled:
            return "Multivoc not installed.";
        case MV_DriverError:
            return SoundDriver_PCM_ErrorString(SoundDriver_PCM_GetError());
        case MV_NoVoices:
            return "No free voices available to Multivoc.";
        case MV_VoiceNotFound:
            return "No voice with matching handle found.";
        case MV_InvalidFile:
            return "Invalid file passed in to Multivoc.";
        default:
            return "Unknown Multivoc error code.";
    }
}

static playbackstatus MV_GetNextDemandFeedBlock(VoiceNode* voice)
{
    if (voice->BlockLength > 0)
    {
        voice->position -= voice->length;
        voice->sound += voice->length >> 16;
        voice->length = min(voice->BlockLength, 0x8000u);
        voice->BlockLength -= voice->length;
        voice->length <<= 16;

        return KeepPlaying;
    }

    if (voice->DemandFeed == NULL)
        return NoMoreData;

    voice->position = 0;
    (voice->DemandFeed)(&voice->sound, &voice->BlockLength, voice->rawdataptr);
    voice->length = min(voice->BlockLength, 0x8000u);
    voice->BlockLength -= voice->length;
    voice->length <<= 16;

    if (voice->length > 0 && voice->sound != NULL)
        return KeepPlaying;

    return NoMoreData;
}

int MV_StartDemandFeedPlayback(void (*function)(const char** ptr, uint32_t* length, void* userdata), int bitdepth, int channels, int rate,
    int pitchoffset, int vol, int left, int right, int priority, fix16_t volume, intptr_t callbackval, void* userdata)
{
    if (!MV_Installed)
        return MV_SetErrorCode(MV_NotInstalled);

    // Request a voice from the voice pool
    auto voice = MV_AllocVoice(priority);
    if (voice == nullptr)
        return MV_SetErrorCode(MV_NoVoices);

//    voice->wavetype = FMT_DEMANDFED;
    voice->bits = bitdepth;
    voice->channels = channels;
    voice->GetSound = MV_GetNextDemandFeedBlock;
    voice->DemandFeed = function;
    voice->position = 0;
    voice->sound  = nullptr;
    voice->length = 0;
    voice->priority = priority;
    voice->callbackval = callbackval;
    voice->rawdataptr = userdata;

    voice->Loop = {};

    MV_SetVoicePitch(voice, rate, pitchoffset);
    MV_SetVoiceMixMode(voice);
    MV_SetVoiceVolume(voice, vol, left, right, volume);
    MV_PlayVoice(voice);

    return voice->handle;
}

int MV_StartDemandFeedPlayback3D(void (*function)(const char** ptr, uint32_t* length, void* userdata), int bitdepth, int channels, int rate,
    int pitchoffset, int angle, int distance, int priority, fix16_t volume, intptr_t callbackval, void* userdata)
{
    if (!MV_Installed)
        return MV_SetErrorCode(MV_NotInstalled);

    if (distance < 0)
    {
        distance  = -distance;
        angle    += MV_NUMPANPOSITIONS / 2;
    }

    int const vol = MIX_VOLUME(distance);

    // Ensure angle is within 0 - 127
    angle &= MV_MAXPANPOSITION;

    return MV_StartDemandFeedPlayback(function, bitdepth, channels, rate, pitchoffset, max(0, 255 - distance),
        MV_PanTable[ angle ][ vol ].left, MV_PanTable[ angle ][ vol ].right, priority, volume, callbackval, userdata);
}
