/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: OpenALAudioManager.cpp 
/*---------------------------------------------------------------------------*/
/* EA Pacific                                                                */
/* Confidential Information	                                                 */
/* Copyright (C) 2001 - All Rights Reserved                                  */
/* DO NOT DISTRIBUTE                                                         */
/*---------------------------------------------------------------------------*/
/* Project:    RTS3                                                          */
/* File name:  OpenALAudioManager.cpp                                         */
/* Created:    Stephan Vedder, 3/9/2025s                              */
/* Desc:       This is the implementation for the OpenALAudioManager, which   */
/*						 interfaces with the Miles Sound System.                       */
/* Revision History:                                                         */
/*		3/9/2025 : Initial creation                                           */
/*---------------------------------------------------------------------------*/

#include "Lib/BaseType.h"
#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "OpenALAudioDevice/OpenALAudioStream.h"
#include "OpenALAudioCache.h"

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/AsciiString.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"

#include "GameClient/DebugDisplay.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"

#include "Common/file.h"

#include <AL/alext.h>

// GeneralsX @bugfix 14/06/2026 Self-heal for stuck "disallow speech" flag.
// Uninterruptible streamed speech (e.g. Generals Challenge enemy taunts) sets
// disallowSpeech=TRUE so a speaker doesn't talk over himself; it's cleared when
// the stream is detected stopped. But a finished finite-speech stream can linger
// "not stopped" for a long time (its drained source keeps getting restarted in
// OpenALAudioStream::update()), so the flag stays stuck and every subsequent
// taunt is rejected with AHSV_NoSound — the player hears only the first taunt.
// We record the frame the flag was set and force-clear it after longer than any
// real voice line can last, so a genuinely-playing taunt is never cut off but a
// stale flag can't silence the rest of the match.
static Int s_disallowSpeechSetFrame = 0;
// 30 logic frames/sec; 15s comfortably exceeds the longest taunt/EVA line.
static const Int DISALLOW_SPEECH_MAX_FRAMES = 30 * 15;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#ifdef _INTERNAL
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

enum { INFINITE_LOOP_COUNT = 1000000 };

#define LOAD_ALC_PROC(N) N = reinterpret_cast<decltype(N)>(alcGetProcAddress(m_alcDevice, #N))

static inline bool sourceIsStopped(ALuint source)
{
	ALenum state;
	alGetSourcei(source, AL_SOURCE_STATE, &state);
	
	return (state == AL_STOPPED);
}

//-------------------------------------------------------------------------------------------------
OpenALAudioManager::OpenALAudioManager() :
	m_providerCount(1),
	m_selectedProvider(PROVIDER_ERROR),
	m_selectedSpeakerType(0),
	m_lastProvider(PROVIDER_ERROR),
	m_alcDevice(NULL),
	m_alcContext(NULL),
	m_num2DSamples(0),
	m_num3DSamples(0),
	m_numStreams(0),
	m_binkAudio(NULL),
	m_pref3DProvider(AsciiString::TheEmptyString),
	m_prefSpeaker(AsciiString::TheEmptyString)
{
	m_audioCache = NEW OpenALAudioFileCache;
	m_provider3D[0].name = "Miles Fast 2D Positional Audio";
	m_provider3D[0].m_isValid = true;
}

//-------------------------------------------------------------------------------------------------
OpenALAudioManager::~OpenALAudioManager()
{
	DEBUG_ASSERTCRASH(m_binkAudio == NULL, ("Leaked a Bink handle. Chuybregts"));
	releaseHandleForBink();
	closeDevice();
	delete m_audioCache;

	DEBUG_ASSERTCRASH(this == TheAudio, ("Umm...\n"));
	TheAudio = NULL;
}

//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG) || defined(_INTERNAL)
AudioHandle OpenALAudioManager::addAudioEvent(const AudioEventRTS* eventToAdd)
{
	if (TheGlobalData->m_preloadReport) {
		if (!eventToAdd->getEventName().isEmpty()) {
			m_allEventsLoaded.insert(eventToAdd->getEventName());
		}
	}

	return AudioManager::addAudioEvent(eventToAdd);
}
#endif

#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::audioDebugDisplay(DebugDisplayInterface* dd, void*, FILE* fp)
{
	std::list<PlayingAudio*>::iterator it;

	static char buffer[128] = { 0 };
	if (buffer[0] == 0) {
        strncpy(buffer, alGetString(AL_VERSION), sizeof(buffer));
	}

	Coord3D lookPos;
	TheTacticalView->getPosition(&lookPos);
	lookPos.z = TheTerrainLogic->getGroundHeight(lookPos.x, lookPos.y);
	const Coord3D* mikePos = TheAudio->getListenerPosition();
	Coord3D distanceVector = TheTacticalView->get3DCameraPosition();
	distanceVector.sub(mikePos);

	Int now = TheGameLogic->getFrame();
	static Int lastCheck = now;
	//const Int frames = 60;
	static Int latency = 0;
	static Int worstLatency = 0;

	if (dd)
	{
		dd->printf("OpenAL version: %s    ", buffer);
		dd->printf("Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
		dd->printf("Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No"));
		dd->printf("3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No"));
		dd->printf("Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No"));
		dd->printf("Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No"));
		dd->printf("Channels Available: ");
		dd->printf("%d Sounds    ", m_sound->getAvailableSamples());

		dd->printf("%d 3D Sounds\n", m_sound->getAvailable3DSamples());
		dd->printf("Volume: ");
		dd->printf("Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f));
		dd->printf("3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f));
		dd->printf("Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f));
		dd->printf("Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f));
		dd->printf("Current 3D Provider: %s    ",

			TheAudio->getProviderName(m_selectedProvider).str());
		dd->printf("Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		dd->printf("Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n",
			(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z);
		dd->printf("Camera distance from microphone: %d -- Zoom Volume: %d%%\n",
			(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume() * 100.0f));
		dd->printf("Worst latency: %d -- Current latency: %d\n", worstLatency, latency);

		dd->printf("-----------------------------------------------------------\n");
		dd->printf("Playing Audio\n");
	}
	if (fp)
	{
		fprintf(fp, "Miles Sound System version: %s    ", buffer);
		fprintf(fp, "Memory Usage : %d/%d\n", m_audioCache->getCurrentlyUsedSize(), m_audioCache->getMaxSize());
		fprintf(fp, "Sound: %s    ", (isOn(AudioAffect_Sound) ? "Yes" : "No"));
		fprintf(fp, "3DSound: %s    ", (isOn(AudioAffect_Sound3D) ? "Yes" : "No"));
		fprintf(fp, "Speech: %s    ", (isOn(AudioAffect_Speech) ? "Yes" : "No"));
		fprintf(fp, "Music: %s\n", (isOn(AudioAffect_Music) ? "Yes" : "No"));
		fprintf(fp, "Channels Available: ");
		fprintf(fp, "%d Sounds    ", m_sound->getAvailableSamples());
		fprintf(fp, "%d 3D Sounds\n", m_sound->getAvailable3DSamples());
		fprintf(fp, "Volume: ");
		fprintf(fp, "Sound: %d    ", REAL_TO_INT(m_soundVolume * 100.0f));
		fprintf(fp, "3DSound: %d    ", REAL_TO_INT(m_sound3DVolume * 100.0f));
		fprintf(fp, "Speech: %d    ", REAL_TO_INT(m_speechVolume * 100.0f));
		fprintf(fp, "Music: %d\n", REAL_TO_INT(m_musicVolume * 100.0f));
		fprintf(fp, "Current 3D Provider: %s    ", TheAudio->getProviderName(m_selectedProvider).str());
		fprintf(fp, "Current Speaker Type: %s\n", TheAudio->translateUnsignedIntToSpeakerType(TheAudio->getSpeakerType()).str());

		fprintf(fp, "Looking at: (%d,%d,%d) -- Microphone at: (%d,%d,%d)\n",
			(Int)lookPos.x, (Int)lookPos.y, (Int)lookPos.z, (Int)mikePos->x, (Int)mikePos->y, (Int)mikePos->z);
		fprintf(fp, "Camera distance from microphone: %d -- Zoom Volume: %d%%\n",
			(Int)distanceVector.length(), (Int)(TheAudio->getZoomVolume() * 100.0f));

		fprintf(fp, "-----------------------------------------------------------\n");
		fprintf(fp, "Playing Audio\n");
	}

	PlayingAudio* playing = NULL;
	Int channel;
	Int channelCount;
	Real volume = 0.0f;
	AsciiString filenameNoSlashes;

	const Int maxChannels = 64;
	PlayingAudio* playingArray[maxChannels] = { NULL };

	// 2-D Sounds
	if (dd)
	{
		dd->printf("-----------------------------------------------------Sounds\n");
		channelCount = TheAudio->getNum2DSamples();
		channel = 1;
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[channel] = playing;
			channel++;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i) {
			playing = playingArray[i];
			if (!playing) {
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-20s - (%s) Volume: %d (2D)\n", i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
			playingArray[i] = NULL;
		}
	}
	if (fp)
	{
		fprintf(fp, "-----------------------------------------------------Sounds\n");
		channelCount = TheAudio->getNum2DSamples();
		channel = 1;
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it)
		{
			playing = *it;
			if (!playing)
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			fprintf(fp, "%2d: %-20s - (%s) Volume: %d (2D)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}
		for (int i = channel; i <= channelCount; ++i)
		{
			fprintf(fp, "%d: Silence\n", i);
		}
	}

	const Coord3D* microphonePos = TheAudio->getListenerPosition();

	// Now 3D Sounds
	if (dd)
	{
		dd->printf("--------------------------------------------------3D Sounds\n");
		channelCount = TheAudio->getNum3DSamples();
		channel = 1;
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}

			playingArray[channel] = playing;
			channel++;
		}

		for (Int i = 1; i <= maxChannels && i <= channelCount; ++i)
		{
			playing = playingArray[i];
			if (!playing)
			{
				dd->printf("%d: Silence\n", i);
				continue;
			}

			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);
			Real dist = -1.0f;
			const Coord3D* pos = playing->m_audioEventRTS->getPosition();
			char distStr[32];
			if (pos)
			{
				Coord3D vector = *microphonePos;
				vector.sub(pos);
				dist = vector.length();
				sprintf(distStr, "%d", REAL_TO_INT(dist));
			}
			else
			{
				sprintf(distStr, "???");
			}
			char str[32];
			switch (playing->m_audioEventRTS->getOwnerType())
			{
			case OT_Positional:
				sprintf(str, "(3D)");
				break;
			case OT_Object:
				sprintf(str, "(3DObj)");
				break;
			case OT_Drawable:
				sprintf(str, "(3DDraw)");
				break;
			case OT_Dead:
				sprintf(str, "(3DDead)");
				break;

			}

			dd->printf("%2d: %-20s - (%s) Volume: %d, Dist: %s, %s\n",
				i, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume), distStr, str);
			playingArray[i] = NULL;
		}
	}
	if (fp)
	{
		fprintf(fp, "--------------------------------------------------3D Sounds\n");
		channelCount = TheAudio->getNum3DSamples();
		channel = 1;
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it)
		{
			playing = *it;
			if (!playing)
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);
			fprintf(fp, "%2d: %-24s - (%s) Volume: %d \n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}

		for (int i = channel; i <= channelCount; ++i)
		{
			fprintf(fp, "%2d: Silence\n", i);
		}
	}

	// Now Streams
	if (dd)
	{
		dd->printf("----------------------------------------------------Streams\n");
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (!playing) {
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;


			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			dd->printf("%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}

		for (int i = channel; i <= channelCount; ++i) {
			dd->printf("%2d: Silence\n", i);
		}
		dd->printf("===========================================================\n");
	}
	if (fp)
	{
		fprintf(fp, "----------------------------------------------------Streams\n");
		channelCount = TheAudio->getNumStreams();
		channel = 1;
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it)
		{
			playing = *it;
			if (!playing)
			{
				continue;
			}
			filenameNoSlashes = playing->m_audioEventRTS->getFilename();
			filenameNoSlashes = filenameNoSlashes.reverseFind('\\') + 1;

			// Calculate Sample volume
			volume = 100.0f;
			volume *= getEffectiveVolume(playing->m_audioEventRTS);

			fprintf(fp, "%2d: %-24s - (%s)  Volume: %d (Stream)\n", channel++, playing->m_audioEventRTS->getEventName().str(), filenameNoSlashes.str(), REAL_TO_INT(volume));
		}

		for (int i = channel; i <= channelCount; ++i)
		{
			fprintf(fp, "%2d: Silence\n", i);
		}
		fprintf(fp, "===========================================================\n");
	}
}
#endif

#ifdef AL_EXT_debug
// Debug callback for OpenAL errors - requires ALC_EXT_debug extension (OpenAL-soft >= 1.23 compiled with debug support)
static void AL_APIENTRY debugCallbackAL(ALenum source, ALenum type, ALuint id,
	ALenum severity, ALsizei length, const ALchar* message, void* userParam ) noexcept
{
	switch (severity)
	{
	case AL_DEBUG_SEVERITY_HIGH_EXT:
		DEBUG_LOG(("OpenAL Error: %s\n", message));
		break;
	case AL_DEBUG_SEVERITY_MEDIUM_EXT:
		DEBUG_LOG(("OpenAL Warning: %s\n", message));
		break;
	case AL_DEBUG_SEVERITY_LOW_EXT:
		DEBUG_LOG(("OpenAL Info: %s\n", message));
		break;
	default:
		DEBUG_LOG(("OpenAL Message: %s\n", message));
		break;
	}

}
#endif // AL_EXT_debug

ALenum OpenALAudioManager::getALFormat(uint8_t channels, uint8_t bitsPerSample)
{
	if (channels == 1 && bitsPerSample == 8)
		return AL_FORMAT_MONO8;
	if (channels == 1 && bitsPerSample == 16)
		return AL_FORMAT_MONO16;
	if (channels == 1 && bitsPerSample == 32)
		return AL_FORMAT_MONO_FLOAT32;
	if (channels == 2 && bitsPerSample == 8)
		return AL_FORMAT_STEREO8;
	if (channels == 2 && bitsPerSample == 16)
		return AL_FORMAT_STEREO16;
	if (channels == 2 && bitsPerSample == 32)
		return AL_FORMAT_STEREO_FLOAT32;

	DEBUG_LOG(("Unknown OpenAL format: %i channels, %i bits per sample", channels, bitsPerSample));
	return AL_FORMAT_MONO8;
}


//=================================================================================================
// r027: procedural map ambience (no asset files). Stereo 44.1 kHz stream on its own OpenAL source.
// Layers: gusty wind + whistle + sand, volcano rumble + deep booms, day cicadas + hawk cries,
// night crickets + cooling-rock cracks, rare animals (howl / jackal yips), rare rockfall / thunder.
// Ducks when a fight is visible on screen.
//=================================================================================================
#include <cstring>
#include <cmath>
#include <cstdint>
extern float g_gxAmbNight; // 0 day .. 1 night (written by the day/night cycle)
extern int g_gxAmbOwnLost;  // r029: bumped by the HUD whenever one of our vehicles dies
extern float g_gxEngIdle, g_gxEngMove; // r030: nearby vehicles standing / driving (weighted by distance to camera)
namespace {
const int GXA_RATE = 44100;
const int GXA_FRAMES = 4096;
const int GXA_NBUF = 4;
const float GXA_PI = 3.14159265f;
ALuint s_gxaSrc = 0;
ALuint s_gxaBuf[GXA_NBUF];
bool s_gxaOk = false, s_gxaFailed = false, s_gxaRunning = false;
UnsignedInt s_gxaCombatFrame = 0;
// r029: battle intensity inputs (filled by playAudioEvent, consumed by the synth)
float s_gxaHeatAdd = 0.0f;
int s_gxaDistReq = 0;
bool s_gxaRingReq = false;
float s_gxaMuffle = 0.0f;
UnsignedInt s_gxaLastDist = 0, s_gxaLastRing = 0;
int16_t s_gxaPcm[GXA_FRAMES * 2];

struct GxRng { uint32_t s; GxRng() : s(0x9E3779B9u) {}
	inline uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	inline float f() { return (u() >> 8) * (1.0f / 16777216.0f); }       // 0..1
	inline float n() { return f() * 2.0f - 1.0f; } };                      // -1..1
GxRng g_r;

struct GxSvf { float lo, bp;
	inline void run(float in, float f, float q) { float hp = in - lo - q * bp; bp += f * hp; lo += f * bp; } };
inline float gxSvfF(float hz) { float x = 2.0f * sinf(GXA_PI * hz / GXA_RATE); return x > 0.9f ? 0.9f : x; }
struct GxLp { float y; inline float run(float x, float a) { y += a * (x - y); return y; } };
inline float gxLpA(float hz) { return 1.0f - expf(-2.0f * GXA_PI * hz / GXA_RATE); }

// one-shot voices
enum { V_NONE, V_HAWK, V_HOWL, V_YIP, V_BOOM, V_ROCK, V_THUNDER, V_CRACK,
	V_DISTANT, V_RING, V_LOSS, V_RADIO, V_BEAT }; // >= V_DISTANT: battle layer (not ducked)
struct GxVoice { int type; float t, dur, pan, ph, ph2, gain; int n; GxSvf f; GxLp l; };
const int GXA_NV = 20;
GxVoice s_v[GXA_NV];

struct GxState {
	// wind
	float gust, gustTarget, gustTimer, windPan, windPanT; GxSvf wbL, wbR, whis; GxLp sandHp;
	// rumble
	GxLp r1, r2; float rumLfo;
	// cicadas / crickets
	GxSvf cic; float cicPh, cicEnv, cicTimer, cicOn; float crPh[3], crT[3], crPan[3];
	// schedulers (seconds)
	float tBoom, tBird, tAnimal, tRare, tCrack;
	float duck, night;
	// r029 battle layer
	float heat, nature, drone, dph1, dph2, dph3, radioT, beatT; GxLp dlp; int lastLost;
	// r030 engine hum
	float eIdle, eMove, eph1, eph2, eph3, eph4; GxLp eroll;
	GxState() { memset(this, 0, sizeof(*this)); nature = 1.0f; radioT = 5.0f; lastLost = -1; gust = 0.4f; gustTarget = 0.5f; tBoom = 40; tBird = 20; tAnimal = 70; tRare = 50; tCrack = 2; duck = 1; for (int i = 0; i < 3; ++i) { crPan[i] = -0.7f + 0.7f * i; crT[i] = 0.17f * i; } }
} s_a;

GxVoice *gxaStart(int type, float dur, float gain)
{
	for (int i = 0; i < GXA_NV; ++i)
		if (s_v[i].type == V_NONE)
		{
			GxVoice &v = s_v[i];
			memset(&v, 0, sizeof(v));
			v.type = type; v.dur = dur; v.gain = gain; v.pan = g_r.n() * 0.9f; v.n = 0;
			return &v;
		}
	return nullptr;
}

inline float gxaEnv(float t, float a, float d) { if (t < a) return t / a; float x = 1.0f - (t - a) / d; return x > 0 ? x * x : 0; }

// one voice sample (mono), returns value; advances time
float gxaVoice(GxVoice &v)
{
	const float dt = 1.0f / GXA_RATE;
	float out = 0.0f;
	const float t = v.t;
	switch (v.type)
	{
	case V_HAWK: { // two descending screeches
		float tt = t < 1.1f ? t : t - 1.1f;
		float hz = 3000.0f - 1400.0f * (tt / 0.9f) + 120.0f * sinf(2 * GXA_PI * 9.0f * tt);
		v.ph += 2 * GXA_PI * hz * dt;
		float e = (tt < 0.9f) ? gxaEnv(tt, 0.06f, 0.84f) : 0.0f;
		if (t > 1.1f) e *= 0.6f;
		out = (sinf(v.ph) + 0.3f * sinf(2.0f * v.ph) + 0.15f * g_r.n()) * e;
		out = v.l.run(out, gxLpA(2500.0f)); // distance
	} break;
	case V_HOWL: { // distant wolf / dog howl
		float x = t / v.dur;
		float hz = 330.0f + 260.0f * sinf(GXA_PI * (x < 0.4f ? x / 0.4f * 0.5f : 0.5f + (x - 0.4f) / 0.6f * 0.35f)) + 6.0f * sinf(2 * GXA_PI * 5.5f * t);
		v.ph += 2 * GXA_PI * hz * dt;
		float e = gxaEnv(t, 0.4f, v.dur - 0.4f);
		out = (sinf(v.ph) + 0.35f * sinf(2.0f * v.ph) + 0.12f * sinf(3.0f * v.ph)) * e;
		out = v.l.run(out, gxLpA(1200.0f));
	} break;
	case V_YIP: { // jackal: 6 short rising yips
		float seg = 0.22f; int k = (int)(t / seg); float tt = t - k * seg;
		float hz = 800.0f + 700.0f * (tt / 0.14f) + 40.0f * k;
		v.ph += 2 * GXA_PI * hz * dt;
		float e = (tt < 0.14f) ? gxaEnv(tt, 0.015f, 0.125f) : 0.0f;
		out = (sinf(v.ph) + 0.25f * sinf(2.0f * v.ph)) * e;
		out = v.l.run(out, gxLpA(1800.0f));
	} break;
	case V_BOOM: { // deep volcano boom
		float hz = 38.0f - 10.0f * (t / v.dur);
		v.ph += 2 * GXA_PI * hz * dt;
		float e = gxaEnv(t, 0.08f, v.dur - 0.08f);
		float nz = v.l.run(g_r.n(), gxLpA(90.0f)) * 3.0f;
		out = (sinf(v.ph) * 0.8f + nz) * e;
	} break;
	case V_ROCK: { // rockfall: clattering bursts + low tumble
		if (g_r.f() < 0.0009f * (1.0f - t / v.dur)) v.ph2 = 1.0f;
		v.ph2 *= 0.9985f;
		v.f.run(g_r.n() * v.ph2, gxSvfF(900.0f + 600.0f * g_r.f()), 0.7f);
		float tumble = v.l.run(g_r.n(), gxLpA(140.0f)) * 2.5f * gxaEnv(t, 0.3f, v.dur - 0.3f);
		out = v.f.bp * 1.4f + tumble;
	} break;
	case V_THUNDER: {
		float e = gxaEnv(t, 0.25f, v.dur - 0.25f);
		float crack = (t < 0.6f && g_r.f() < 0.02f) ? g_r.n() * 2.0f : 0.0f;
		out = (v.l.run(g_r.n() + crack, gxLpA(260.0f)) * 3.2f) * e * (0.7f + 0.3f * sinf(2 * GXA_PI * 3.0f * t));
	} break;
	case V_CRACK: { // cooling stone click
		float e = expf(-t * 900.0f);
		v.f.run(g_r.n(), gxSvfF(2600.0f), 0.5f);
		out = v.f.bp * e * 2.0f;
	} break;
	case V_DISTANT: { // far gun / explosion: delayed muffled thump + echo off the rocks
		const float tt = t - v.ph2;
		if (tt < 0.0f) break;
		const float e = expf(-tt * 5.0f);
		const float e2 = (tt > 0.35f) ? expf(-(tt - 0.35f) * 4.0f) * 0.35f : 0.0f;
		const float nz = v.l.run(g_r.n(), gxLpA(160.0f));
		v.ph += 2 * GXA_PI * 48.0f * dt;
		out = (nz * 4.0f + sinf(v.ph) * 0.6f) * (e + e2);
	} break;
	case V_RING: { // ear ringing after a close blast
		v.ph += 2 * GXA_PI * 3150.0f * dt;
		v.ph2 += 2 * GXA_PI * 3190.0f * dt;
		const float att = t < 0.05f ? t / 0.05f : 1.0f;
		out = (sinf(v.ph) + sinf(v.ph2)) * 0.5f * expf(-t * 1.4f) * att;
	} break;
	case V_LOSS: { // our vehicle died: heavy metal clang + low drop
		v.ph += 2 * GXA_PI * 187.0f * dt;
		v.ph2 += 2 * GXA_PI * (70.0f - 30.0f * (t / v.dur)) * dt;
		const float clang = (sinf(v.ph) + 0.6f * sinf(v.ph * 2.21f) + 0.4f * sinf(v.ph * 3.93f)) * expf(-t * 3.0f);
		out = clang + sinf(v.ph2) * 0.9f * gxaEnv(t, 0.02f, v.dur - 0.02f);
	} break;
	case V_RADIO: { // garbled radio chatter: buzzy syllables through a narrow band + squelch
		const bool sq = (t < 0.07f || t > v.dur - 0.07f);
		const int syl = (int)(t * 7.0f);
		const float st = t * 7.0f - (float)syl;
		const uint32_t h = (uint32_t)syl * 2654435761u + (uint32_t)v.n;
		const bool on = ((h >> 13) & 3u) != 0u && !sq;
		const float hz = 115.0f + (float)((h >> 7) & 31u);
		v.ph += hz * dt;
		if (v.ph > 1.0f) v.ph -= 1.0f;
		const float env = on ? sinf(GXA_PI * st) : 0.0f;
		v.f.run((v.ph * 2.0f - 1.0f) * env + g_r.n() * 0.15f, gxSvfF(1400.0f + 500.0f * sinf(GXA_PI * st)), 0.35f);
		out = v.f.bp * 1.6f + g_r.n() * ((sq ? 0.5f : 0.0f) + 0.04f);
	} break;
	case V_BEAT: { // heavy double pulse when the fight is at its peak
		const float tt = t < 0.25f ? t : t - 0.25f;
		v.ph += 2 * GXA_PI * (50.0f - 15.0f * tt) * dt;
		out = sinf(v.ph) * expf(-tt * 18.0f) * (t < 0.25f ? 1.0f : 0.7f);
	} break;
	default: break;
	}
	v.t += dt;
	if (v.t >= v.dur) v.type = V_NONE;
	return out * v.gain;
}

void gxaSchedule(float dt)
{
	GxState &a = s_a;
	const float day = 1.0f - a.night;
	a.tBoom -= dt; a.tBird -= dt; a.tAnimal -= dt; a.tRare -= dt; a.tCrack -= dt;
	if (a.tBoom <= 0) { gxaStart(V_BOOM, 2.5f + 2.0f * g_r.f(), 0.55f); a.tBoom = 60.0f + 120.0f * g_r.f(); }
	// r029: battle intensity
	a.heat += s_gxaHeatAdd; s_gxaHeatAdd = 0.0f;
	if (a.heat > 1.0f) a.heat = 1.0f;
	a.heat -= dt * 0.07f;
	if (a.heat < 0.0f) a.heat = 0.0f;
	while (s_gxaDistReq > 0)
	{
		--s_gxaDistReq;
		GxVoice *v = gxaStart(V_DISTANT, 2.4f, 0.16f);
		if (v) v->ph2 = 0.25f + 1.1f * g_r.f(); // sound travels: far shots arrive late
	}
	if (s_gxaRingReq) { s_gxaRingReq = false; gxaStart(V_RING, 3.2f, 0.09f); s_gxaMuffle = 1.0f; }
	if (a.lastLost < 0) a.lastLost = g_gxAmbOwnLost;
	if (g_gxAmbOwnLost != a.lastLost)
	{
		a.lastLost = g_gxAmbOwnLost;
		gxaStart(V_LOSS, 1.6f, 0.16f);
		if (g_r.f() < 0.6f) a.radioT = 0.8f;
	}
	a.radioT -= dt;
	if (a.radioT <= 0)
	{
		if (a.heat > 0.3f) { GxVoice *v = gxaStart(V_RADIO, 1.2f + 1.6f * g_r.f(), 0.10f); if (v) v->n = (int)(g_r.u() & 0xFFFFu); }
		a.radioT = 9.0f + 15.0f * g_r.f();
	}
	a.beatT -= dt;
	if (a.heat > 0.6f && a.beatT <= 0) { gxaStart(V_BEAT, 0.6f, 0.22f * (a.heat - 0.4f)); a.beatT = 0.95f; }
	if (a.tBird <= 0) { if (day > 0.4f && a.nature > 0.7f) gxaStart(V_HAWK, 2.0f, 0.10f * day); a.tBird = 45.0f + 75.0f * g_r.f(); }
	if (a.tAnimal <= 0)
	{
		if (a.nature < 0.7f) {}
		else if (a.night > 0.5f) gxaStart(g_r.f() < 0.6f ? V_HOWL : V_YIP, 2.6f + 1.5f * g_r.f(), 0.08f);
		else if (g_r.f() < 0.4f) gxaStart(V_YIP, 1.4f, 0.05f);
		a.tAnimal = 120.0f + 180.0f * g_r.f();
	}
	if (a.tRare <= 0) { gxaStart(g_r.f() < 0.5f ? V_ROCK : V_THUNDER, 3.0f + 2.5f * g_r.f(), 0.22f); a.tRare = 60.0f + 60.0f * g_r.f(); }
	if (a.tCrack <= 0) { gxaStart(V_CRACK, 0.03f, 0.10f * (0.3f + 0.7f * a.night)); a.tCrack = (1.5f + 6.0f * g_r.f()) / (0.3f + a.night); }
}

void gxaFill(int16_t *pcm, int frames, float duckTarget)
{
	GxState &a = s_a;
	const float nightTarget = g_gxAmbNight < 0 ? 0 : (g_gxAmbNight > 1 ? 1 : g_gxAmbNight);
	gxaSchedule((float)frames / GXA_RATE);
	const float fW1 = 0, fWh = 0; (void)fW1; (void)fWh;
	const float aSand = gxLpA(3000.0f), aR1 = gxLpA(55.0f), aR2 = gxLpA(45.0f);
	const float fCic = gxSvfF(5200.0f);
	for (int i = 0; i < frames; ++i)
	{
		const float dt = 1.0f / GXA_RATE;
		a.night += (nightTarget - a.night) * 0.00002f;
		a.duck += (duckTarget - a.duck) * 0.00006f;
		const float night = a.night, day = 1.0f - night;
		// r029: nature hushes fast when shooting starts, comes back slowly (~10 s) after
		const float natT = a.heat > 0.22f ? 0.0f : 1.0f;
		a.nature += (natT - a.nature) * (natT < a.nature ? 0.00005f : 0.0000025f);
		a.drone += (a.heat - a.drone) * 0.00002f;
		float dr = 0.0f;
		if (a.drone > 0.01f)
		{
			// low tension bed: detuned saws, filter opens with intensity (no melody)
			a.dph1 += 55.0f * dt; if (a.dph1 > 1.0f) a.dph1 -= 1.0f;
			a.dph2 += 55.6f * dt; if (a.dph2 > 1.0f) a.dph2 -= 1.0f;
			a.dph3 += 82.4f * dt; if (a.dph3 > 1.0f) a.dph3 -= 1.0f;
			const float sw = (a.dph1 * 2.0f - 1.0f) + (a.dph2 * 2.0f - 1.0f) + 0.6f * (a.dph3 * 2.0f - 1.0f);
			dr = a.dlp.run(sw, gxLpA(140.0f + 260.0f * a.drone)) * 0.09f * a.drone;
		}
		// --- gusts
		a.gustTimer -= dt;
		if (a.gustTimer <= 0) { float r = g_r.f(); a.gustTarget = r < 0.25f ? 0.05f + 0.1f * g_r.f() : 0.3f + 0.7f * g_r.f() * g_r.f() + 0.25f * r; a.gustTimer = 2.0f + 6.0f * g_r.f(); a.windPanT = g_r.n() * 0.6f; }
		a.gust += (a.gustTarget * (night > 0.5f ? 0.7f : 1.0f) - a.gust) * 0.00004f;
		a.windPan += (a.windPanT - a.windPan) * 0.00002f;
		const float g = a.gust;
		const float wf = gxSvfF(180.0f + 700.0f * g);
		a.wbL.run(g_r.n(), wf, 1.1f);
		a.wbR.run(g_r.n(), wf, 1.1f);
		float wind = 0.55f * (0.12f + g * g);
		a.whis.run(g_r.n(), gxSvfF(650.0f + 950.0f * g), 0.06f);
		float whistle = a.whis.bp * 0.05f * g * g * g;
		float white = g_r.n();
		float sand = (white - a.sandHp.run(white, aSand)) * 0.10f * g * g * (0.6f + 0.4f * g_r.f());
		float L = a.wbL.bp * wind * (1.0f - 0.4f * a.windPan) + whistle * (1.0f - a.windPan) + sand;
		float Rr = a.wbR.bp * wind * (1.0f + 0.4f * a.windPan) + whistle * (1.0f + a.windPan) + sand * 0.9f;
		// --- volcano rumble
		a.rumLfo += dt * 0.07f;
		float rum = a.r2.run(a.r1.run(g_r.n(), aR1), aR2) * 2.2f * (0.75f + 0.25f * sinf(2 * GXA_PI * a.rumLfo));
		L += rum; Rr += rum;
		// --- cicadas (day, swelling choruses)
		if (day > 0.05f)
		{
			a.cicTimer -= dt;
			if (a.cicTimer <= 0) { a.cicOn = a.cicOn > 0.5f ? 0.0f : 1.0f; a.cicTimer = a.cicOn > 0.5f ? 5.0f + 7.0f * g_r.f() : 4.0f + 10.0f * g_r.f(); }
			a.cicEnv += (a.cicOn - a.cicEnv) * 0.00003f;
			a.cicPh += dt * 47.0f; if (a.cicPh > 1) a.cicPh -= 1;
			float am = sinf(GXA_PI * a.cicPh); am *= am; am *= am;
			a.cic.run(g_r.n(), fCic, 0.12f);
			float c = a.cic.bp * am * a.cicEnv * 0.035f * day * a.nature;
			L += c * 0.8f; Rr += c;
		}
		// --- crickets (night)
		if (night > 0.05f)
			for (int k = 0; k < 3; ++k)
			{
				a.crT[k] += dt; if (a.crT[k] > 0.55f + 0.07f * k) a.crT[k] = 0;
				const float t = a.crT[k];
				const int pulse = (int)(t / 0.035f);
				const float pt = t - pulse * 0.035f;
				float e = (pulse < 3 && pt < 0.022f) ? sinf(GXA_PI * pt / 0.022f) : 0.0f;
				a.crPh[k] += 2 * GXA_PI * (4300.0f + 180.0f * k) * dt; if (a.crPh[k] > 2 * GXA_PI) a.crPh[k] -= 2 * GXA_PI;
				float c = sinf(a.crPh[k]) * e * 0.022f * night * a.nature;
				L += c * (1.0f - a.crPan[k]); Rr += c * (1.0f + a.crPan[k]);
			}
		// --- one-shots
		float FL = 0.0f, FR = 0.0f;
		for (int v = 0; v < GXA_NV; ++v)
			if (s_v[v].type != V_NONE)
			{
				const float pan = s_v[v].pan;
				const bool fx = s_v[v].type >= V_DISTANT;
				const float o = gxaVoice(s_v[v]);
				if (fx) { FL += o * (1.0f - pan); FR += o * (1.0f + pan); }
				else { L += o * (1.0f - pan); Rr += o * (1.0f + pan); }
			}
		L *= a.duck * 0.9f; Rr *= a.duck * 0.9f;
		// r030: modern electric drive - soft mains-like hum standing, motor whine rising when driving
		{
			const float ti = g_gxEngIdle < 0 ? 0 : (g_gxEngIdle > 6 ? 6 : g_gxEngIdle);
			const float tm = g_gxEngMove < 0 ? 0 : (g_gxEngMove > 6 ? 6 : g_gxEngMove);
			a.eIdle += (ti - a.eIdle) * 0.00005f;
			a.eMove += (tm - a.eMove) * 0.00005f;
			const float tot = a.eIdle + a.eMove;
			if (tot > 0.01f)
			{
				const float load = a.eMove / (tot + 0.001f);
				const float wh = 520.0f + 900.0f * load;
				const float TP = 2.0f * GXA_PI;
				a.eph1 += TP * wh * dt; if (a.eph1 > TP) a.eph1 -= TP;
				a.eph2 += TP * (wh * 1.5f + 3.0f) * dt; if (a.eph2 > TP) a.eph2 -= TP;
				a.eph3 += TP * 100.0f * dt; if (a.eph3 > TP) a.eph3 -= TP;
				a.eph4 += TP * 150.4f * dt; if (a.eph4 > TP) a.eph4 -= TP;
				const float hum = sinf(a.eph3) * 0.5f + sinf(a.eph4) * 0.3f;
				const float whine = sinf(a.eph1) * 0.25f + sinf(a.eph2) * 0.07f;
				const float roll = a.eroll.run(g_r.n(), gxLpA(220.0f)) * 3.0f * load;
				const float amt = sqrtf(tot > 4.0f ? 4.0f : tot) * 0.5f;
				const float eg = (hum * 0.05f + whine * 0.035f * (0.4f + 0.6f * load) + roll * 0.05f) * amt;
				FL += eg; FR += eg;
			}
		}
		L += FL + dr; Rr += FR + dr; // battle layer is never ducked
		L = L > 1.0f ? 1.0f : (L < -1.0f ? -1.0f : L);
		Rr = Rr > 1.0f ? 1.0f : (Rr < -1.0f ? -1.0f : Rr);
		pcm[2 * i] = (int16_t)(L * 32000.0f);
		pcm[2 * i + 1] = (int16_t)(Rr * 32000.0f);
	}
}

void gxaQueue(ALuint b, float duck)
{
	gxaFill(s_gxaPcm, GXA_FRAMES, duck);
	alBufferData(b, AL_FORMAT_STEREO16, s_gxaPcm, (ALsizei)sizeof(s_gxaPcm), GXA_RATE);
	alSourceQueueBuffers(s_gxaSrc, 1, &b);
}

void gxaShutdown()
{
	if (!s_gxaOk) return;
	alSourceStop(s_gxaSrc);
	alSourcei(s_gxaSrc, AL_BUFFER, 0);
	alDeleteSources(1, &s_gxaSrc);
	alDeleteBuffers(GXA_NBUF, s_gxaBuf);
	s_gxaOk = false; s_gxaRunning = false;
	if (s_gxaMuffle > 0.0f) { s_gxaMuffle = 0.0f; alListenerf(AL_GAIN, 1.0f); }
}

void gxaUpdate(Real volume)
{
	const bool want = TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused() && volume > 0.001f;
	if (!s_gxaOk)
	{
		if (!want || s_gxaFailed) return;
		alGetError();
		alGenSources(1, &s_gxaSrc);
		if (alGetError() != AL_NO_ERROR) { s_gxaFailed = true; return; }
		alGenBuffers(GXA_NBUF, s_gxaBuf);
		if (alGetError() != AL_NO_ERROR) { alDeleteSources(1, &s_gxaSrc); s_gxaFailed = true; return; }
		alSourcei(s_gxaSrc, AL_SOURCE_RELATIVE, AL_TRUE);
		alSource3f(s_gxaSrc, AL_POSITION, 0.0f, 0.0f, 0.0f);
		alSourcef(s_gxaSrc, AL_ROLLOFF_FACTOR, 0.0f);
		s_gxaOk = true;
	}
	if (!want)
	{
		if (s_gxaMuffle > 0.0f) { s_gxaMuffle = 0.0f; alListenerf(AL_GAIN, 1.0f); }
		if (s_gxaRunning) { alSourceStop(s_gxaSrc); alSourcei(s_gxaSrc, AL_BUFFER, 0); s_gxaRunning = false; }
		return;
	}
	// duck while a fight is visible (weapon / explosion sounds started on screen in the last ~3 s)
	const UnsignedInt fr = TheGameLogic->getFrame();
	const float duck = (s_gxaCombatFrame != 0 && fr >= s_gxaCombatFrame && fr - s_gxaCombatFrame < 90) ? 0.4f : 1.0f;
	alSourcef(s_gxaSrc, AL_GAIN, volume * 0.8f);
	if (s_gxaMuffle > 0.0f)
	{
		// r029: the whole world goes dull for a moment after a close blast
		s_gxaMuffle -= 0.012f;
		if (s_gxaMuffle < 0.0f) s_gxaMuffle = 0.0f;
		alListenerf(AL_GAIN, 1.0f - 0.55f * s_gxaMuffle);
	}
	if (!s_gxaRunning)
	{
		for (int i = 0; i < GXA_NBUF; ++i) gxaQueue(s_gxaBuf[i], duck);
		alSourcePlay(s_gxaSrc);
		s_gxaRunning = true;
		return;
	}
	ALint done = 0;
	alGetSourcei(s_gxaSrc, AL_BUFFERS_PROCESSED, &done);
	while (done-- > 0)
	{
		ALuint b = 0;
		alSourceUnqueueBuffers(s_gxaSrc, 1, &b);
		gxaQueue(b, duck);
	}
	ALint st = 0;
	alGetSourcei(s_gxaSrc, AL_SOURCE_STATE, &st);
	if (st != AL_PLAYING) alSourcePlay(s_gxaSrc); // underrun recovery
}

void gxaNoteSound(AudioEventRTS *event, bool onScreen, Real centerDist)
{
	if (!TheGameLogic) return;
	AsciiString n = event->getEventName();
	n.toLower();
	const char *s = n.str();
	if (!s) return;
	const bool boom = strstr(s, "explo") || strstr(s, "impact") || strstr(s, "death");
	const bool shot = strstr(s, "weapon") || strstr(s, "cannon") || strstr(s, "gun") || strstr(s, "shot") || strstr(s, "fire");
	if (!boom && !shot) return;
	const UnsignedInt fr = TheGameLogic->getFrame();
	if (onScreen)
	{
		s_gxaCombatFrame = fr;
		s_gxaHeatAdd += boom ? 0.10f : 0.06f;
		if (boom && centerDist < 140.0f && (fr < s_gxaLastRing || fr - s_gxaLastRing > 150))
		{
			s_gxaLastRing = fr;
			s_gxaRingReq = true;
		}
	}
	else
	{
		s_gxaHeatAdd += 0.025f;
		if (fr < s_gxaLastDist || fr - s_gxaLastDist >= 8)
		{
			s_gxaLastDist = fr;
			if (s_gxaDistReq < 4) ++s_gxaDistReq;
		}
	}
}

// r029: small random pitch spread so repeated shots never sound identical
Real gxaPitchJitter() { return 0.965f + 0.07f * g_r.f(); }

// r027: real distance fade for positional sounds. OpenAL's inverse-clamped model never reaches
// zero, so looping sounds (lava!) kept playing at ~20% forever after the camera left.
Real gxaDistFade(AudioEventRTS *event, const Coord3D *p, const Coord3D &listener)
{
	const AudioEventInfo *info = event ? event->getAudioEventInfo() : nullptr;
	if (!info || !p || (info->m_type & ST_GLOBAL) || info->m_maxDistance <= 1.0f)
		return 1.0f;
	const Real dx = p->x - listener.x, dy = p->y - listener.y;
	const Real d = sqrtf(dx * dx + dy * dy);
	const Real maxD = info->m_maxDistance;
	Real start = maxD * 0.55f;
	if (start < info->m_minDistance) start = info->m_minDistance;
	if (start >= maxD) start = maxD * 0.8f;
	if (d >= maxD) return 0.0f;
	if (d <= start) return 1.0f;
	const Real k = (maxD - d) / (maxD - start);
	return k * k;
}
} // namespace

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::init()
{
	AudioManager::init();
#ifdef INTENSE_DEBUG
	DEBUG_LOG(("Sound has temporarily been disabled in debug builds only. jkmcd\n"));
	// for now, _DEBUG builds only should have no sound. ask jkmcd or srj about this.
	return;
#endif

	// We should now know how many samples we want to load
	openDevice();
	m_audioCache->setMaxSize(getAudioSettings()->m_maxCacheSize);
	alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::postProcessLoad()
{
	AudioManager::postProcessLoad();
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::reset()
{
#if defined(_DEBUG) || defined(_INTERNAL)
	dumpAllAssetsUsed();
	m_allEventsLoaded.clear();
#endif

	AudioManager::reset();   // clears m_disallowSpeech
	s_disallowSpeechSetFrame = 0;  // GeneralsX @bugfix 14/06/2026 clear stale backstop frame across new game/map
	stopAllAudioImmediately();
	removeAllAudioRequests();
	// This must come after stopAllAudioImmediately() and removeAllAudioRequests(), to ensure that
	// sounds pointing to the temporary AudioEventInfo handles are deleted before their info is deleted
	removeLevelSpecificAudioEventInfos();
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::update()
{
	AudioManager::update();
	setDeviceListenerPosition();
	processRequestList();
	processPlayingList();
	processFadingList();
	processStoppedList();
	gxaUpdate(m_soundVolume); // r027 map ambience
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::stopAudio(AudioAffect which)
{
	// All we really need to do is:
	// 1) Remove the EOS callback.
	// 2) Stop the sample, (so that when we later unload it, bad stuff doesn't happen)
	// 3) Set the status to stopped, so that when we next process the playing list, we will 
	//		correctly clean up the sample.


	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourceStop(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourceStop(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
			if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo()) {
				if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
					if (!BitIsSet(which, AudioAffect_Music)) {
						continue;
					}
				}
				else {
					if (!BitIsSet(which, AudioAffect_Speech)) {
						continue;
					}
				}
				// GeneralsX @bugfix BenderAI 11/03/2026 - streams use m_stream->stop(), not alSourceStop(m_source) which is always 0
				if (playing->m_stream) {
					playing->m_stream->stop();
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::pauseAudio(AudioAffect which)
{
	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourceStop(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourceStop(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
			if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo()) {
				if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
					if (!BitIsSet(which, AudioAffect_Music)) {
						continue;
					}
				}
				else {
					if (!BitIsSet(which, AudioAffect_Speech)) {
						continue;
					}
				}

				// GeneralsX @bugfix BenderAI 11/03/2026 - streams use m_stream->pause(), not alSourcePause(m_source) which is always 0
				if (playing->m_stream) {
					playing->m_stream->pause();
				}
			}
		}
	}

	//Get rid of PLAY audio requests when pausing audio.
	std::list<AudioRequest*>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); /* empty */)
	{
		AudioRequest* req = (*ait);
		if (req && req->m_request == AR_Play)
		{
			deleteInstance(req);
			ait = m_audioRequests.erase(ait);
		}
		else
		{
			ait++;
		}
	}
}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::resumeAudio(AudioAffect which)
{
	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	if (BitIsSet(which, AudioAffect_Sound)) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourcePlay(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Sound3D)) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing) {
				alSourcePlay(playing->m_source);
			}
		}
	}

	if (BitIsSet(which, AudioAffect_Speech | AudioAffect_Music)) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
			if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getAudioEventInfo()) {
				if (playing->m_audioEventRTS->getAudioEventInfo()->m_soundType == AT_Music) {
					if (!BitIsSet(which, AudioAffect_Music)) {
						continue;
					}
				}
				else {
					if (!BitIsSet(which, AudioAffect_Speech)) {
						continue;
					}
				}
				alSourcePlay(playing->m_stream->getSource());
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::pauseAmbient(Bool shouldPause)
{

}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::playAudioEvent(AudioEventRTS* event)
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("OPENAL (%d) - Processing play request: %d (%s)", TheGameLogic->getFrame(), event->getPlayingHandle(), event->getEventName().str()));
#endif
	const AudioEventInfo* info = event->getAudioEventInfo();
	if (!info) {
		return;
	}

	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing = NULL;

	AudioHandle handleToKill = event->getHandleToKill();

	if (event->isPositionalAudio())
	{
		// r030: stock vehicle engine / move loops sound like a vacuum cleaner -> replaced by the synth hum
		AsciiString en = event->getEventName();
		en.toLower();
		const char *es = en.str();
		if (es && !strstr(es, "voice") &&
		    (strstr(es, "moveloop") || strstr(es, "movestart") || strstr(es, "engine") || strstr(es, "idle") || strstr(es, "tread")))
			return;
	}
	AsciiString fileToPlay = event->getFilename();
	if (event->isPositionalAudio() && event->getCurrentPosition() && TheTacticalView)
	{
		// r029: battle intensity / distant echo / ear ringing
		const Coord3D *gp = event->getCurrentPosition();
		if (gp && TheTacticalView)
		{
			const Coord3D &vc = TheTacticalView->getPosition();
			const Real ddx = gp->x - vc.x, ddy = gp->y - vc.y;
			gxaNoteSound(event, isOnScreen(gp), sqrtf(ddx * ddx + ddy * ddy));
		}
	}
	PlayingAudio* audio = allocatePlayingAudio();
	switch (info->m_soundType)
	{
	case AT_Music:
	case AT_Streaming:
	{
#ifdef INTENSIVE_AUDIO_DEBUG
		DEBUG_LOG(("- Stream\n"));
#endif

		if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
			stopAllSpeech();
		}

		Real curVolume = 1.0;
		if (info->m_soundType == AT_Music) {
			curVolume = m_musicVolume;
		}
		else {
			curVolume = m_speechVolume;
		}
		curVolume *= event->getVolume();

		Bool foundSoundToReplace = false;
		if (handleToKill) {
			for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
				playing = (*it);
				if (!playing) {
					continue;
				}

				if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
				{
					//Release this streaming channel immediately because we are going to play another sound in it's place.
					releasePlayingAudio(playing);
					m_playingStreams.erase(it);
					foundSoundToReplace = true;
					break;
				}
			}
		}

		File* file = TheFileSystem->openFile(fileToPlay.str());
		if (!file) {
			DEBUG_LOG(("Failed to open file: %s\n", fileToPlay.str()));
			releasePlayingAudio(audio);
			return;
		}

		FFmpegFile* ffmpegFile = NEW FFmpegFile();
		if (!ffmpegFile->open(file))
		{
			DEBUG_LOG(("Failed to open FFmpeg file: %s\n", fileToPlay.str()));
			releasePlayingAudio(audio);
			return;
		}

		OpenALAudioStream* stream;
		if (!handleToKill || foundSoundToReplace) {
			stream = new OpenALAudioStream;
			// When we need more data ask FFmpeg for more data.
			stream->setRequireDataCallback([ffmpegFile, stream]() -> bool {
				ffmpegFile->decodePacket();
				// GeneralsX @bugfix 14/06/2026 Report TRUE end-of-file so a finished one-shot
				// speech (taunt) stops being restarted and can reach a stable AL_STOPPED. Keys
				// on real EOF, not a bare decode-error, so long briefings/music and transient
				// underruns are unaffected.
				return !ffmpegFile->isAtEof();
				});
			
			// When we receive a frame from FFmpeg, send it to OpenAL.
			ffmpegFile->setFrameCallback([stream](AVFrame* frame, int stream_idx, int stream_type, void* user_data) {
				if (stream_type != AVMEDIA_TYPE_AUDIO) {
					return;
				}

				DEBUG_LOG(("Received audio frame\n"));

				AVSampleFormat sampleFmt = static_cast<AVSampleFormat>(frame->format);
				const int bytesPerSample = av_get_bytes_per_sample(sampleFmt);
				ALenum format = OpenALAudioManager::getALFormat(frame->ch_layout.nb_channels, bytesPerSample * 8);
				const int frameSize =
					av_samples_get_buffer_size(NULL, frame->ch_layout.nb_channels, frame->nb_samples, sampleFmt, 1);
				uint8_t* frameData = frame->data[0];

				// We need to interleave the samples if the format is planar
				if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format))) {
					uint8_t* audioBuffer = static_cast<uint8_t*>(av_malloc(frameSize));

					// Write the samples into our audio buffer
					for (int sample_idx = 0; sample_idx < frame->nb_samples; sample_idx++)
					{
						int byte_offset = sample_idx * bytesPerSample;
						for (int channel_idx = 0; channel_idx < frame->ch_layout.nb_channels; channel_idx++)
						{
							uint8_t* dst = &audioBuffer[byte_offset * frame->ch_layout.nb_channels + channel_idx * bytesPerSample];
							uint8_t* src = &frame->data[channel_idx][byte_offset];
							memcpy(dst, src, bytesPerSample);
						}
					}
					stream->bufferData(audioBuffer, frameSize, format, frame->sample_rate);
					av_freep(&audioBuffer);
				}
				else
					stream->bufferData(frameData, frameSize, format, frame->sample_rate);
			});
		}
		else {
			stream = NULL;
		}

		// Put this on here, so that the audio event RTS will be cleaned up regardless.
		audio->m_audioEventRTS = event;
		audio->m_stream = stream;
		audio->m_ffmpegFile = ffmpegFile;
		audio->m_type = PAT_Stream;

		if (stream) {
			if ((info->m_soundType == AT_Streaming) && event->getUninterruptible()) {
				setDisallowSpeech(TRUE);
				s_disallowSpeechSetFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
			}
			// AIL_set_stream_volume_pan(stream, curVolume, 0.5f);
			playStream(event, stream);
			m_playingStreams.push_back(audio);
			audio = NULL;
		}
		break;
	}

	case AT_SoundEffect:
	{
#ifdef INTENSIVE_AUDIO_DEBUG
		DEBUG_LOG(("- Sound"));
#endif


		if (event->isPositionalAudio()) {
			// Sounds that are non-global are positional 3-D sounds. Deal with them accordingly
#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" Positional"));
#endif
			Bool foundSoundToReplace = false;
			if (handleToKill)
			{
				for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
					playing = (*it);
					if (!playing) {
						continue;
					}

					if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
					{
						//Release this 3D sound channel immediately because we are going to play another sound in it's place.
						releasePlayingAudio(playing);
						m_playing3DSounds.erase(it);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			ALuint source;
			if (!handleToKill || foundSoundToReplace)
			{
				alGenSources(1, &source);

			}
			else
			{
				source = 0;
			}
			// Push it onto the list of playing things
			audio->m_audioEventRTS = event;
			audio->m_source = source;
			audio->m_bufferHandle = 0;
			audio->m_type = PAT_3DSample;
			m_playing3DSounds.push_back(audio);

			if (source) {
				audio->m_bufferHandle = playSample3D(event, audio);
				m_sound->notifyOf3DSampleStart();
			}

			if (!audio->m_bufferHandle)
			{
				m_playing3DSounds.pop_back();
#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Killed (no handles available)\n"));
#endif
			}
			else
			{
				audio = NULL;
#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Playing.\n"));
#endif
			}
		}
		else
		{
			// UI sounds are always 2-D. All other sounds should be Positional
			// Unit acknowledgement, etc, falls into the UI category of sound.
			Bool foundSoundToReplace = false;
			if (handleToKill) {
				for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
					playing = (*it);
					if (!playing) {
						continue;
					}

					if (playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handleToKill)
					{
						//Release this 2D sound channel immediately because we are going to play another sound in it's place.
						releasePlayingAudio(playing);
						m_playingSounds.erase(it);
						foundSoundToReplace = true;
						break;
					}
				}
			}

			ALuint source;
			if (!handleToKill || foundSoundToReplace)
			{
				alGenSources(1, &source);
			}
			else
			{
				source = 0;
			}

			// Push it onto the list of playing things
			audio->m_audioEventRTS = event;
			audio->m_source = source;
			audio->m_bufferHandle = 0;
			audio->m_type = PAT_Sample;
			m_playingSounds.push_back(audio);

			if (source) {
				audio->m_bufferHandle = playSample(event, audio);
				m_sound->notifyOf2DSampleStart();
			}

			if (!audio->m_bufferHandle) {
#ifdef INTENSIVE_AUDIO_DEBUG
				DEBUG_LOG((" Killed (no handles available)\n"));
#endif
				m_playingSounds.pop_back();
			}
			else {
				audio = NULL;
			}

#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" Playing.\n"));
#endif
		}
		break;
	}
	}

	// If we were able to successfully play audio, then we set it to NULL above. (And it will be freed
	// later. However, if audio is non-NULL at this point, then it must be freed.
	if (audio) {
		releasePlayingAudio(audio);
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::stopAudioEvent(AudioHandle handle)
{
#ifdef INTENSIVE_AUDIO_DEBUG
	DEBUG_LOG(("OPENAL (%d) - Processing stop request: %d\n", TheGameLogic->getFrame(), handle));
#endif

	std::list<PlayingAudio*>::iterator it;
	if (handle == AHSV_StopTheMusic || handle == AHSV_StopTheMusicFade) {
		// for music, just find the currently playing music stream and kill it.
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			PlayingAudio* audio = (*it);
			if (!audio) {
				continue;
			}

			// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
			const AudioEventInfo* stopInfo = (audio->m_audioEventRTS ? audio->m_audioEventRTS->getAudioEventInfo() : nullptr);
			if (stopInfo && stopInfo->m_soundType == AT_Music)
			{
				if (handle == AHSV_StopTheMusicFade)
				{
					m_fadingAudio.push_back(audio);
				}
				else
				{
					//m_stoppedAudio.push_back(audio);
					releasePlayingAudio(audio);
				}
				m_playingStreams.erase(it);
				break;
			}
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		PlayingAudio* audio = (*it);
		if (!audio || !audio->m_audioEventRTS) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			// found it
			audio->m_requestStop = true;
			notifyOfAudioCompletion((UnsignedInt)(audio->m_source), PAT_Stream);
			break;
		}
	}

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		PlayingAudio* audio = (*it);
		if (!audio || !audio->m_audioEventRTS) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
			audio->m_requestStop = true;
			break;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		PlayingAudio* audio = (*it);
		if (!audio || !audio->m_audioEventRTS) {
			continue;
		}

		if (audio->m_audioEventRTS->getPlayingHandle() == handle) {
#ifdef INTENSIVE_AUDIO_DEBUG
			DEBUG_LOG((" (%s)\n", audio->m_audioEventRTS->getEventName()));
#endif
			audio->m_requestStop = true;
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::killAudioEventImmediately(AudioHandle audioEvent)
{
	//First look for it in the request list.
	std::list<AudioRequest*>::iterator ait;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ait++)
	{
		AudioRequest* req = (*ait);
		if (req && req->m_request == AR_Play && req->m_handleToInteractOn == audioEvent)
		{
			deleteInstance(req);
			ait = m_audioRequests.erase(ait);
			return;
		}
	}

	//Look for matching 3D sound to kill
	std::list<PlayingAudio*>::iterator it;
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); it++)
	{
		PlayingAudio* audio = (*it);
		if (!audio)
		{
			continue;
		}

		if (audio->m_audioEventRTS && audio->m_audioEventRTS->getPlayingHandle() == audioEvent)
		{
			releasePlayingAudio(audio);
			m_playing3DSounds.erase(it);
			return;
		}
	}

	//Look for matching 2D sound to kill
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); it++)
	{
		PlayingAudio* audio = (*it);
		if (!audio)
		{
			continue;
		}

		if (audio->m_audioEventRTS && audio->m_audioEventRTS->getPlayingHandle() == audioEvent)
		{
			releasePlayingAudio(audio);
			m_playingSounds.erase(it);
			return;
		}
	}

	//Look for matching steaming sound to kill
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); it++)
	{
		PlayingAudio* audio = (*it);
		if (!audio)
		{
			continue;
		}

		if (audio->m_audioEventRTS && audio->m_audioEventRTS->getPlayingHandle() == audioEvent)
		{
			releasePlayingAudio(audio);
			m_playingStreams.erase(it);
			return;
		}
	}

}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::pauseAudioEvent(AudioHandle handle)
{
	// pause audio
}

//-------------------------------------------------------------------------------------------------
ALuint OpenALAudioManager::loadBufferForRead(AudioEventRTS* eventToLoadFrom)
{
	return m_audioCache->getBufferForFile(OpenFileInfo(eventToLoadFrom));
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::closeBuffer(ALuint bufferToClose)
{
	m_audioCache->closeBuffer(bufferToClose);
}


//-------------------------------------------------------------------------------------------------
PlayingAudio* OpenALAudioManager::allocatePlayingAudio(void)
{
	PlayingAudio* aud = NEW PlayingAudio;	// poolify
	return aud;
}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::releaseOpenALHandles(PlayingAudio* release)
{
	if (release->m_source)
	{
		alDeleteSources(1, &release->m_source);
		release->m_source = 0;
	}
	if (release->m_stream)
	{
		delete release->m_stream;
		release->m_stream = NULL;
	}
	if (release->m_ffmpegFile)
	{
		delete release->m_ffmpegFile;
		release->m_ffmpegFile = NULL;
	}

	release->m_type = PAT_INVALID;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::releasePlayingAudio(PlayingAudio* release)
{
	// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null getAudioEventInfo() return
	const AudioEventInfo* releaseInfo = (release->m_audioEventRTS ? release->m_audioEventRTS->getAudioEventInfo() : nullptr);
	if (releaseInfo && releaseInfo->m_soundType == AT_SoundEffect) {
		if (release->m_type == PAT_Sample) {
			if (release->m_source) {
				m_sound->notifyOf2DSampleCompletion();
			}
		}
		else {
			if (release->m_source) {
				m_sound->notifyOf3DSampleCompletion();
			}
		}
	}
	releaseOpenALHandles(release);	// forces stop of this audio
	closeBuffer(release->m_bufferHandle);
	if (release->m_cleanupAudioEventRTS) {
		releaseAudioEventRTS(release->m_audioEventRTS);
	}
	delete release;
	release = NULL;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::stopAllAudioImmediately(void)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		playing = *it;
		if (!playing) {
			continue;
		}

		releasePlayingAudio(playing);
		it = m_playingSounds.erase(it);
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		playing = *it;
		if (!playing) {
			continue;
		}

		releasePlayingAudio(playing);
		it = m_playing3DSounds.erase(it);
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);
		if (!playing) {
			continue;
		}

		releasePlayingAudio(playing);
		it = m_playingStreams.erase(it);
	}

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); ) {
		playing = (*it);
		if (!playing) {
			continue;
		}

		releasePlayingAudio(playing);
		it = m_fadingAudio.erase(it);
	}

	//std::list<HAUDIO>::iterator hit;
	//for (hit = m_audioForcePlayed.begin(); hit != m_audioForcePlayed.end(); ++hit) {
	//	if (*hit) {
	//		AIL_quick_unload(*hit);
	//	}
	//}

	//m_audioForcePlayed.clear();
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::freeAllOpenALHandles(void)
{
	// First, we need to ensure that we don't have any sample handles open. To that end, we must stop
	// all of our currently playing audio.
	stopAllAudioImmediately();

	m_num2DSamples = 0;
	m_num3DSamples = 0;
	m_numStreams = 0;
}

//-------------------------------------------------------------------------------------------------
//HSAMPLE OpenALAudioManager::getFirst2DSample(AudioEventRTS* event)
//{
//	if (m_availableSamples.begin() != m_availableSamples.end()) {
//		HSAMPLE retSample = *m_availableSamples.begin();
//		m_availableSamples.erase(m_availableSamples.begin());
//		return (retSample);
//	}
//
//	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle
//
//	return NULL;
//}
//
////-------------------------------------------------------------------------------------------------
//PlayingAudio* OpenALAudioManager::getFirst3DSample(AudioEventRTS* event)
//{
//	if (m_available3DSamples.begin() != m_available3DSamples.end()) {
//		H3DSAMPLE retSample = *m_available3DSamples.begin();
//		m_available3DSamples.erase(m_available3DSamples.begin());
//		return (retSample);
//	}
//
//	// Find the first sample of lower priority than my augmented priority that is interruptable and take its handle
//	return NULL;
//}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::adjustPlayingVolume(PlayingAudio* audio)
{
	// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null m_audioEventRTS
	if (!audio->m_audioEventRTS)
		return;
	Real desiredVolume = audio->m_audioEventRTS->getVolume() * audio->m_audioEventRTS->getVolumeShift();
	if (audio->m_type == PAT_Sample) {
		alSourcef(audio->m_source, AL_GAIN, m_soundVolume * desiredVolume);

	}
	else if (audio->m_type == PAT_3DSample) {
		alSourcef(audio->m_source, AL_GAIN, m_sound3DVolume * desiredVolume);

	}
	else if (audio->m_type == PAT_Stream) {
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (audio->m_audioEventRTS ? audio->m_audioEventRTS->getAudioEventInfo() : nullptr);
		if (info && info->m_soundType == AT_Music) {
			alSourcef(audio->m_stream->getSource(), AL_GAIN, m_musicVolume * desiredVolume);
		}
		else {
			alSourcef(audio->m_stream->getSource(), AL_GAIN, m_speechVolume * desiredVolume);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::stopAllSpeech(void)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);
		if (!playing) {
			++it;
			continue;
		}

		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing->m_audioEventRTS ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr);
		if (info && info->m_soundType == AT_Streaming) {
			releasePlayingAudio(playing);
			it = m_playingStreams.erase(it);
		}
		else {
			++it;
		}
	}

}

//-------------------------------------------------------------------------------------------------
//void OpenALAudioManager::initFilters(HSAMPLE sample, const AudioEventRTS* event)
//{
//	// set the sample volume
//	Real volume = event->getVolume() * event->getVolumeShift() * m_soundVolume;
//	AIL_set_sample_volume_pan(sample, volume, 0.5f);
//
//	// pitch shift
//	Real pitchShift = event->getPitchShift();
//	if (pitchShift == 0.0f) {
//		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()));
//	}
//	else {
//		AIL_set_sample_playback_rate(sample, REAL_TO_INT(AIL_sample_playback_rate(sample) * pitchShift));
//	}
//
//	// set up delay filter, if applicable
//	if (event->getDelay() > 0.0f) {
//		Real value;
//		value = event->getDelay();
//		AIL_set_sample_processor(sample, DP_FILTER, m_delayFilter);
//		AIL_set_filter_sample_preference(sample, "Mono Delay Time", &value);
//
//		value = 0.0;
//		AIL_set_filter_sample_preference(sample, "Mono Delay", &value);
//		AIL_set_filter_sample_preference(sample, "Mono Delay Mix", &value);
//	}
//}

//-------------------------------------------------------------------------------------------------
//void OpenALAudioManager::initFilters3D(H3DSAMPLE sample, const AudioEventRTS* event, const Coord3D* pos)
//{
//	// set the sample volume
//	Real volume = event->getVolume() * event->getVolumeShift() * m_sound3DVolume;
//	AIL_set_3D_sample_volume(sample, volume);
//
//	// pitch shift
//	Real pitchShift = event->getPitchShift();
//	if (pitchShift == 0.0f) {
//		DEBUG_CRASH(("Invalid Pitch shift in sound: '%s'", event->getEventName().str()));
//	}
//	else {
//		AIL_set_3D_sample_playback_rate(sample, REAL_TO_INT(AIL_3D_sample_playback_rate(sample) * pitchShift));
//	}
//
//	// Low pass filter
//	if (event->getAudioEventInfo()->m_lowPassFreq > 0 && !isOnScreen(pos)) {
//		AIL_set_3D_sample_occlusion(sample, 1.0f - event->getAudioEventInfo()->m_lowPassFreq);
//	}
//}

//-------------------------------------------------------------------------------------------------
AsciiString OpenALAudioManager::nextMusicTrack(void)
{
	AsciiString trackName;
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing && playing->m_audioEventRTS) ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr;
		if (info && info->m_soundType == AT_Music) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = nextTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);

	return trackName;
}

//-------------------------------------------------------------------------------------------------
AsciiString OpenALAudioManager::prevMusicTrack(void)
{
	AsciiString trackName;
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing && playing->m_audioEventRTS) ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr;
		if (info && info->m_soundType == AT_Music) {
			trackName = playing->m_audioEventRTS->getEventName();
		}
	}

	// Stop currently playing music
	TheAudio->removeAudioEvent(AHSV_StopTheMusic);

	trackName = prevTrackName(trackName);
	AudioEventRTS newTrack(trackName);
	TheAudio->addAudioEvent(&newTrack);

	return trackName;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isMusicPlaying(void) const
{
	std::list<PlayingAudio*>::const_iterator it;
	PlayingAudio* playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing && playing->m_audioEventRTS) ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr;
		if (info && info->m_soundType == AT_Music) {
			return TRUE;
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::hasMusicTrackCompleted(const AsciiString& trackName, Int numberOfTimes) const
{
	std::list<PlayingAudio*>::const_iterator it;
	PlayingAudio* playing;
	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing && playing->m_audioEventRTS) ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr;
		if (info && info->m_soundType == AT_Music) {
			if (playing->m_audioEventRTS->getEventName() == trackName) {
				//if (INFINITE_LOOP_COUNT - AIL_stream_loop_count(playing->m_stream) >= numberOfTimes) {
				// return TRUE;
				//}
			}
		}
	}

	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::openDevice(void)
{
	if (!TheGlobalData->m_audioOn) {
		return;
	}

	// AIL_quick_startup should be replaced later with a call to actually pick which device to use, etc
	const AudioSettings* audioSettings = getAudioSettings();
	m_selectedSpeakerType = TheAudio->translateSpeakerTypeToUnsignedInt(m_prefSpeaker);

	enumerateDevices();

	m_alcDevice = alcOpenDevice(NULL);
	if (m_alcDevice == nullptr) {
		DEBUG_LOG(("Failed to open ALC device"));
		// if we couldn't initialize any devices, turn sound off (fail silently)
		setOn(false, AudioAffect_All);
		return;
	}

	ALCint attributes[] = { ALC_FREQUENCY, audioSettings->m_outputRate, 0 /* end-of-list */ };
	m_alcContext = alcCreateContext(m_alcDevice, attributes);
	if (m_alcContext == nullptr) {
		DEBUG_LOG(("Failed to create ALC context"));
		setOn(false, AudioAffect_All);
		return;
	}

	if (!alcMakeContextCurrent(m_alcContext)) {
		DEBUG_LOG(("Failed to make ALC context current"));
		setOn(false, AudioAffect_All);
		return;
	}

#ifdef AL_EXT_debug
	if (alcIsExtensionPresent(m_alcDevice, "ALC_EXT_debug")) {
		auto alDebugMessageCallbackEXT = LPALDEBUGMESSAGECALLBACKEXT{};
		LOAD_ALC_PROC(alDebugMessageCallbackEXT);
		alEnable(AL_DEBUG_OUTPUT_EXT);
		alDebugMessageCallbackEXT(debugCallbackAL, nullptr);
	}
#endif // AL_EXT_debug

	selectProvider(TheAudio->getProviderIndex(m_pref3DProvider));

	// Now that we're all done, update the cached variables so that everything is in sync.
	TheAudio->refreshCachedVariables();

	if (!isValidProvider()) {
		return;
	}

	initDelayFilter();
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::closeDevice(void)
{
	gxaShutdown(); // r027
	unselectProvider();
	alcMakeContextCurrent(nullptr);

	if (m_alcContext)
		alcDestroyContext(m_alcContext);

	if (m_alcDevice)
		alcCloseDevice(m_alcDevice);
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isCurrentlyPlaying(AudioHandle handle)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getPlayingHandle() == handle) {
			return true;
		}
	}

	// if something is requested, it is also considered playing
	std::list<AudioRequest*>::iterator ait;
	AudioRequest* req = NULL;
	for (ait = m_audioRequests.begin(); ait != m_audioRequests.end(); ++ait) {
		req = *ait;
		if (req && req->m_usePendingEvent && req->m_pendingEvent->getPlayingHandle() == handle) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::notifyOfAudioCompletion(UnsignedInt audioCompleted, UnsignedInt flags)
{
	PlayingAudio* playing = findPlayingAudioFrom(audioCompleted, flags);
	if (!playing) {
		DEBUG_CRASH(("Audio has completed playing, but we can't seem to find it. - jkmcd"));
		return;
	}

	// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
	if (!playing->m_audioEventRTS)
		return;
	const AudioEventInfo* completionInfo = playing->m_audioEventRTS->getAudioEventInfo();
	if (!completionInfo)
		return;

	if (getDisallowSpeech() && completionInfo->m_soundType == AT_Streaming) {
		setDisallowSpeech(FALSE);
	}

	if (completionInfo->m_control & AC_LOOP) {
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Attack) {
			playing->m_audioEventRTS->setNextPlayPortion(PP_Sound);
		}
		if (playing->m_audioEventRTS->getNextPlayPortion() == PP_Sound) {
			// First, decrease the loop count.
			playing->m_audioEventRTS->decreaseLoopCount();

			// Now, try to start the next loop
			if (startNextLoop(playing)) {
				return;
			}
		}
	}

	playing->m_audioEventRTS->advanceNextPlayPortion();
	if (playing->m_audioEventRTS->getNextPlayPortion() != PP_Done) {
		if (playing->m_type == PAT_Sample) {
			closeBuffer(playing->m_bufferHandle);	// close it so as not to leak it.
			playing->m_bufferHandle = playSample(playing->m_audioEventRTS, playing);

			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_bufferHandle) {
				return;
			}
		}
		else if (playing->m_type == PAT_3DSample) {
			closeBuffer(playing->m_bufferHandle);	// close it so as not to leak it.
			playing->m_bufferHandle = playSample3D(playing->m_audioEventRTS, playing);

			// If we don't have a file now, then we should drop to the stopped status so that 
			// We correctly close this handle.
			if (playing->m_bufferHandle) {
				return;
			}
		}
	}

	if (playing->m_type == PAT_Stream) {
		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		const AudioEventInfo* info = (playing->m_audioEventRTS ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr);
		if (info && info->m_soundType == AT_Music) {
			playStream(playing->m_audioEventRTS, playing->m_stream);

			return;
		}
	}
}

//-------------------------------------------------------------------------------------------------
PlayingAudio* OpenALAudioManager::findPlayingAudioFrom(ALuint source, UnsignedInt flags)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	if (flags == PAT_Sample) {
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_source == source) {
				return playing;
			}
		}
	}

	if (flags == PAT_3DSample) {
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			playing = *it;
			if (playing && playing->m_source == source) {
				return playing;
			}
		}
	}

	if (flags == PAT_Stream) {
		for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
			playing = *it;
			if (playing && playing->m_source == source) {
				return playing;
			}
		}
	}

	return NULL;
}


//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getProviderCount(void) const
{
	return m_providerCount;
}

//-------------------------------------------------------------------------------------------------
AsciiString OpenALAudioManager::getProviderName(UnsignedInt providerNum) const
{
	if (isOn(AudioAffect_Sound3D) && providerNum < m_providerCount) {
		return m_provider3D[providerNum].name;
	}

	return AsciiString::TheEmptyString;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getProviderIndex(AsciiString providerName) const
{
	for (UnsignedInt i = 0; i < m_providerCount; ++i) {
		if (providerName == m_provider3D[i].name) {
			return i;
		}
	}

	return PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::selectProvider(UnsignedInt providerNdx)
{
	if (!isOn(AudioAffect_Sound3D))
	{
		return;
	}

	if (providerNdx == m_selectedProvider)
	{
		return;
	}

	if (isValidProvider())
	{
		freeAllOpenALHandles();
		unselectProvider();
	}

	/*LPDIRECTSOUND lpDirectSoundInfo;
	AIL_get_DirectSound_info(NULL, (void**)&lpDirectSoundInfo, NULL);
	Bool useDolby = FALSE;
	if (lpDirectSoundInfo)
	{
		DWORD speakerConfig;
		lpDirectSoundInfo->GetSpeakerConfig(&speakerConfig);
		switch (DSSPEAKER_CONFIG(speakerConfig))
		{
		case DSSPEAKER_DIRECTOUT:
			m_selectedSpeakerType = AIL_3D_2_SPEAKER;
			break;
		case DSSPEAKER_MONO:
			m_selectedSpeakerType = AIL_3D_2_SPEAKER;
			break;
		case DSSPEAKER_STEREO:
			m_selectedSpeakerType = AIL_3D_2_SPEAKER;
			break;
		case DSSPEAKER_HEADPHONE:
			m_selectedSpeakerType = AIL_3D_HEADPHONE;
			useDolby = TRUE;
			break;
		case DSSPEAKER_QUAD:
			m_selectedSpeakerType = AIL_3D_4_SPEAKER;
			useDolby = TRUE;
			break;
		case DSSPEAKER_SURROUND:
			m_selectedSpeakerType = AIL_3D_SURROUND;
			useDolby = TRUE;
			break;
		case DSSPEAKER_5POINT1:
			m_selectedSpeakerType = AIL_3D_51_SPEAKER;
			useDolby = TRUE;
			break;
		case DSSPEAKER_7POINT1:
			m_selectedSpeakerType = AIL_3D_71_SPEAKER;
			useDolby = TRUE;
			break;
		}
	}

	if (useDolby)
	{
		providerNdx = getProviderIndex("Dolby Surround");
	}
	else
	{
		providerNdx = getProviderIndex("Miles Fast 2D Positional Audio");
	}
	success = AIL_open_3D_provider(m_provider3D[providerNdx].id) == 0;*/

	//if (providerNdx < m_providerCount) 
	//{
	//	failed = AIL_open_3D_provider(m_provider3D[providerNdx].id);
	//}

	Bool success = FALSE;

	if (!success)
	{
		m_selectedProvider = PROVIDER_ERROR;
		// try to select a failsafe
		providerNdx = getProviderIndex("Miles Fast 2D Positional Audio");
		success = TRUE;
	}

	if (success)
	{
		m_selectedProvider = providerNdx;

		initSamplePools();

		createListener();
		setSpeakerType(m_selectedSpeakerType);
		if (TheVideoPlayer)
		{
			TheVideoPlayer->notifyVideoPlayerOfNewProvider(TRUE);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::unselectProvider(void)
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider())) {
		return;
	}

	if (TheVideoPlayer) {
		TheVideoPlayer->notifyVideoPlayerOfNewProvider(FALSE);
	}
	//AIL_close_3D_listener(m_listener);
	//m_listener = NULL;

	//AIL_close_3D_provider(m_provider3D[m_selectedProvider].id);
	m_lastProvider = m_selectedProvider;

	m_selectedProvider = PROVIDER_ERROR;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getSelectedProvider(void) const
{
	return m_selectedProvider;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::setSpeakerType(UnsignedInt speakerType)
{
	if (!isValidProvider()) {
		return;
	}

	//AIL_set_3D_speaker_type(m_provider3D[m_selectedProvider].id, speakerType);
	m_selectedSpeakerType = speakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getSpeakerType(void)
{
	if (!isValidProvider()) {
		return 0;
	}

	return m_selectedSpeakerType;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getNum2DSamples(void) const
{
	return m_num2DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getNum3DSamples(void) const
{
	return m_num3DSamples;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt OpenALAudioManager::getNumStreams(void) const
{
	return m_numStreams;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::doesViolateLimit(AudioEventRTS* event) const
{
	Int limit = event->getAudioEventInfo()->m_limit;
	if (limit == 0) {
		return false;
	}

	Int totalCount = 0;
	Int totalRequestCount = 0;

	std::list<PlayingAudio*>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	}
	else {
		// 3-D
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				if (totalCount == 0) {
					// This is the oldest audio of this type playing.
					event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				}
				++totalCount;
			}
		}
	}

	// Also check the request list in case we've requested to play this sound.
	std::list<AudioRequest*>::const_iterator arIt;
	for (arIt = m_audioRequests.begin(); arIt != m_audioRequests.end(); ++arIt) {
		AudioRequest* req = (*arIt);
		if (req == NULL) {
			continue;
		}
		if (req->m_usePendingEvent)
		{
			if (req->m_pendingEvent->getEventName() == event->getEventName())
			{
				totalRequestCount++;
				totalCount++;
			}
		}
	}

	//If our event is an interrupting type, then normally we would always add it. The exception is when we have requested
	//multiple sounds in the same frame and those requests violate the limit. Because we don't have any "old" sounds to
	//remove in the case of an interrupt, we need to catch it early and prevent the sound from being added if we already
	//reached the limit
	if (event->getAudioEventInfo()->m_control & AC_INTERRUPT)
	{
		if (totalRequestCount < limit)
		{
			Int totalPlayingCount = totalCount - totalRequestCount;
			if (totalRequestCount + totalPlayingCount < limit)
			{
				//We aren't exceeding the actual limit, then clear the kill handle.
				event->setHandleToKill(0);
				return false;
			}

			//We are exceeding the limit - the kill handle will kill the
			//oldest playing sound to enforce the actual limit.
			return false;
		}
	}

	if (totalCount < limit)
	{
		event->setHandleToKill(0);
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isPlayingAlready(AudioEventRTS* event) const
{
	std::list<PlayingAudio*>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	}
	else {
		// 3-D
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			if ((*it)->m_audioEventRTS->getEventName() == event->getEventName()) {
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isObjectPlayingVoice(UnsignedInt objID) const
{
	if (objID == 0) {
		return false;
	}

	std::list<PlayingAudio*>::const_iterator it;
	// 2-D
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
		const AudioEventInfo* info = (*it)->m_audioEventRTS->getAudioEventInfo();
		if (info && (*it)->m_audioEventRTS->getObjectID() == objID && (info->m_type & ST_VOICE)) {
			return true;
		}
	}

	// 3-D
	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
		const AudioEventInfo* info = (*it)->m_audioEventRTS->getAudioEventInfo();
		if (info && (*it)->m_audioEventRTS->getObjectID() == objID && (info->m_type & ST_VOICE)) {
			return true;
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
AudioEventRTS* OpenALAudioManager::findLowestPrioritySound(AudioEventRTS* event)
{
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST)
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return NULL;
	}
	AudioEventRTS* lowestPriorityEvent = NULL;
	AudioPriority lowestPriority;

	std::list<PlayingAudio*>::const_iterator it;
	if (event->isPositionalAudio())
	{
		//3D
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it)
		{
			AudioEventRTS* itEvent = (*it)->m_audioEventRTS;
			if (!itEvent) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			const AudioEventInfo* itInfo = itEvent->getAudioEventInfo();
			if (!itInfo) continue;
			AudioPriority itPriority = itInfo->m_priority;
			if (itPriority < priority)
			{
				if (!lowestPriorityEvent || lowestPriority > itPriority)
				{
					lowestPriorityEvent = itEvent;
					lowestPriority = itPriority;
					if (lowestPriority == AP_LOWEST)
					{
						return lowestPriorityEvent;
					}
				}
			}
		}
	}
	else
	{
		//2D
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it)
		{
			AudioEventRTS* itEvent = (*it)->m_audioEventRTS;
			if (!itEvent) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			const AudioEventInfo* itInfo = itEvent->getAudioEventInfo();
			if (!itInfo) continue;
			AudioPriority itPriority = itInfo->m_priority;
			if (itPriority < priority)
			{
				if (!lowestPriorityEvent || lowestPriority > itPriority)
				{
					lowestPriorityEvent = itEvent;
					lowestPriority = itPriority;
					if (lowestPriority == AP_LOWEST)
					{
						return lowestPriorityEvent;
					}
				}
			}
		}
	}
	return lowestPriorityEvent;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isPlayingLowerPriority(AudioEventRTS* event) const
{
	//We don't actually want to do anything to this CONST function. Remember, we're
	//just checking to see if there is a lower priority sound.
	AudioPriority priority = event->getAudioEventInfo()->m_priority;
	if (priority == AP_LOWEST)
	{
		//If the event we pass in is the lowest priority, don't bother checking because
		//there is nothing lower priority than lowest.
		return false;
	}
	std::list<PlayingAudio*>::const_iterator it;
	if (!event->isPositionalAudio()) {
		// 2-D
		for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			const AudioEventInfo* info = (*it)->m_audioEventRTS->getAudioEventInfo();
			if (info && info->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	}
	else {
		// 3-D
		for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
			if (!(*it)->m_audioEventRTS) continue; // GeneralsX @bugfix BenderAI 11/03/2026
			const AudioEventInfo* info = (*it)->m_audioEventRTS->getAudioEventInfo();
			if (info && info->m_priority < priority) {
				//event->setHandleToKill((*it)->m_audioEventRTS->getPlayingHandle());
				return true;
			}
		}
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::killLowestPrioritySoundImmediately(AudioEventRTS* event)
{
	//Actually, we want to kill the LOWEST PRIORITY SOUND, not the first "lower" priority
	//sound we find, because it could easily be 
	AudioEventRTS* lowestPriorityEvent = findLowestPrioritySound(event);
	if (lowestPriorityEvent)
	{
		std::list<PlayingAudio*>::iterator it;
		if (event->isPositionalAudio())
		{
			for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it)
			{
				PlayingAudio* playing = (*it);
				if (!playing)
				{
					continue;
				}

				if (playing->m_audioEventRTS && playing->m_audioEventRTS == lowestPriorityEvent)
				{
					//Release this 3D sound channel immediately because we are going to play another sound in it's place.
					releasePlayingAudio(playing);
					m_playing3DSounds.erase(it);
					return TRUE;
				}
			}
		}
		else
		{
			for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it)
			{
				PlayingAudio* playing = (*it);
				if (!playing)
				{
					continue;
				}

				if (playing->m_audioEventRTS && playing->m_audioEventRTS == lowestPriorityEvent)
				{
					//Release this 3D sound channel immediately because we are going to play another sound in it's place.
					releasePlayingAudio(playing);
					m_playing3DSounds.erase(it);
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::adjustVolumeOfPlayingAudio(AsciiString eventName, Real newVolume)
{
	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			alSourcef(playing->m_source, AL_GAIN, desiredVolume);
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			alSourcef(playing->m_source, AL_GAIN, desiredVolume);
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it) {
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName) {
			// Adjust it
			playing->m_audioEventRTS->setVolume(newVolume);
			Real desiredVolume = playing->m_audioEventRTS->getVolume() * playing->m_audioEventRTS->getVolumeShift();
			alSourcef(playing->m_source, AL_GAIN, desiredVolume);
		}
	}
}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::removePlayingAudio(AsciiString eventName)
{
	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName)
		{
			releasePlayingAudio(playing);
			it = m_playingSounds.erase(it);
		}
		else
		{
			it++;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName)
		{
			releasePlayingAudio(playing);
			it = m_playing3DSounds.erase(it);
		}
		else
		{
			it++;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getEventName() == eventName)
		{
			releasePlayingAudio(playing);
			it = m_playingStreams.erase(it);
		}
		else
		{
			it++;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::removeAllDisabledAudio()
{
	std::list<PlayingAudio*>::iterator it;

	PlayingAudio* playing = NULL;
	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getVolume() == 0.0f)
		{
			releasePlayingAudio(playing);
			it = m_playingSounds.erase(it);
		}
		else
		{
			it++;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getVolume() == 0.0f)
		{
			releasePlayingAudio(playing);
			it = m_playing3DSounds.erase(it);
		}
		else
		{
			it++;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); )
	{
		playing = *it;
		if (playing && playing->m_audioEventRTS && playing->m_audioEventRTS->getVolume() == 0.0f)
		{
			releasePlayingAudio(playing);
			it = m_playingStreams.erase(it);
		}
		else
		{
			it++;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::processRequestList(void)
{
	std::list<AudioRequest*>::iterator it;
	for (it = m_audioRequests.begin(); it != m_audioRequests.end(); /* empty */) {
		AudioRequest* req = (*it);
		if (req == NULL) {
			continue;
		}

		if (!shouldProcessRequestThisFrame(req)) {
			adjustRequest(req);
			++it;
			continue;
		}

		if (!req->m_requiresCheckForSample || checkForSample(req)) {
			processRequest(req);
		}
		deleteInstance(req);
		it = m_audioRequests.erase(it);
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::processPlayingList(void)
{
	// There are two types of processing we have to do here. 
	// 1. Move the item to the stopped list if it has become stopped.
	// 2. Update the position of the audio if it is positional
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); /* empty */) {
		playing = (*it);
		if (!playing)
		{
			it = m_playingSounds.erase(it);
			continue;
		}

		if (sourceIsStopped(playing->m_source))
		{
			// GeneralsX @bugfix BenderAI 09/05/2026 - Advance through Attack/Sound/Decay portions.
			// Miles used EOS callbacks; OpenAL requires polling. Without this call the Attack
			// (static/intro) portion plays but Sound (voice) is never started.
			if (!playing->m_requestStop)
				notifyOfAudioCompletion(playing->m_source, PAT_Sample);
			// If notifyOfAudioCompletion started the next portion the source is now playing;
			// only erase when it is still stopped (done or failed to start next portion).
			if (sourceIsStopped(playing->m_source))
			{
				//m_stoppedAudio.push_back(playing);
				releasePlayingAudio(playing);
				it = m_playingSounds.erase(it);
			}
			else
			{
				++it;
			}
		}
		else
		{
			if (m_volumeHasChanged)
			{
				adjustPlayingVolume(playing);
			}
			++it;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); )
	{
		playing = (*it);
		if (!playing)
		{
			it = m_playing3DSounds.erase(it);
			continue;
		}

		if (sourceIsStopped(playing->m_source))
		{
			// GeneralsX @bugfix BenderAI 09/05/2026 - Same fix as for 2D samples: advance
			// Attack→Sound→Decay before releasing. Miles used EOS callbacks; we must poll.
			if (!playing->m_requestStop)
				notifyOfAudioCompletion(playing->m_source, PAT_3DSample);
			if (sourceIsStopped(playing->m_source))
			{
				//m_stoppedAudio.push_back(playing);
				releasePlayingAudio(playing);
				it = m_playing3DSounds.erase(it);
			}
			else
			{
				++it;
			}
		}
		else
		{
			if (m_volumeHasChanged)
			{
				adjustPlayingVolume(playing);
			}

			const Coord3D* pos = getCurrentPositionFromEvent(playing->m_audioEventRTS);
			if (pos)
			{
				if (playing->m_audioEventRTS->isDead())
				{
					stopAudioEvent(playing->m_audioEventRTS->getPlayingHandle());
					it++;
					continue;
				}
				else
				{
					Real volForConsideration = getEffectiveVolume(playing->m_audioEventRTS);
					volForConsideration /= (m_sound3DVolume > 0.0f ? m_soundVolume : 1.0f);
					// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null getAudioEventInfo()
				const AudioEventInfo* pai = (playing->m_audioEventRTS ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr);
				Bool playAnyways = pai && (BitIsSet(pai->m_type, ST_GLOBAL) || pai->m_priority == AP_CRITICAL);
					if (volForConsideration < m_audioSettings->m_minVolume && !playAnyways)
					{
						// don't want to get an additional callback for this sample
						//AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
						//m_stoppedAudio.push_back(playing);
						releasePlayingAudio(playing);
						it = m_playing3DSounds.erase(it);
						continue;
					}
					else
					{
						Real x = pos->x;
						Real y = pos->y;
						Real z = pos->z;
						alSource3f(playing->m_source, AL_POSITION, x, y, z);
						// r027: fade to silence past MaxRange (keeps the sample alive so it returns when the camera does)
						alSourcef(playing->m_source, AL_GAIN, getEffectiveVolume(playing->m_audioEventRTS) * gxaDistFade(playing->m_audioEventRTS, pos, m_listenerPosition));
						// DEBUG_LOG(("Updating 3D sound position for %s to %f, %f, %f\n", playing->m_audioEventRTS->getEventName().str(), x, y, z));
					}
				}
			}
			else
			{
				//AIL_register_3D_EOS_callback(playing->m_3DSample, NULL);
				//m_stoppedAudio.push_back(playing);
				releasePlayingAudio(playing);
				it = m_playing3DSounds.erase(it);
				continue;
			}

			++it;
		}
	}

	for (it = m_playingStreams.begin(); it != m_playingStreams.end(); ) {
		playing = (*it);
		if (!playing)
		{
			it = m_playingStreams.erase(it);
			continue;
		}

		// GeneralsX @bugfix BenderAI 11/03/2026 - streams use m_stream->getSource(), not m_source (which is always 0).
		// Call update() first so buffer-underrun recovery (play()) runs before we check stopped state.
		if (m_volumeHasChanged)
		{
			adjustPlayingVolume(playing);
		}
		playing->m_stream->update();

		// After update(), if stream source is still stopped there is no more data — release it.
		if (playing->m_stream && sourceIsStopped(playing->m_stream->getSource()))
		{
			// If this was an uninterruptible speech stream, clear the disallow flag.
			const AudioEventInfo* streamInfo = (playing->m_audioEventRTS ? playing->m_audioEventRTS->getAudioEventInfo() : nullptr);
			if (streamInfo && streamInfo->m_soundType == AT_Streaming && getDisallowSpeech())
			{
				setDisallowSpeech(FALSE);
			}
			//m_stoppedAudio.push_back(playing);
			releasePlayingAudio(playing);
			it = m_playingStreams.erase(it);
		}
		else
		{
			++it;
		}
	}

	// GeneralsX @bugfix 14/06/2026 Backstop (belt-and-braces): the proper fix is the EOF
	// propagation in OpenALAudioStream (a finished one-shot now reaches AL_STOPPED so the
	// per-stream clear above fires the frame the audio ends). This timeout remains only as a
	// safety net in case AL_STOPPED detection ever fails on this iOS/OpenAL stack. A negative
	// delta means the logic frame counter reset under us (new game/map) — treat the recorded
	// frame as stale and clear too. Never cuts a genuinely-playing line (longer than any voice).
	if (getDisallowSpeech() && TheGameLogic) {
		const Int sinceSet = (Int)TheGameLogic->getFrame() - s_disallowSpeechSetFrame;
		if (sinceSet < 0 || sinceSet > DISALLOW_SPEECH_MAX_FRAMES) {
			setDisallowSpeech(FALSE);
		}
	}

	if (m_volumeHasChanged) {
		m_volumeHasChanged = false;
	}
}

//Patch for a rare bug (only on about 5% of in-studio machines suffer, and not all the time) .
//The actual mechanics of this problem are still elusive as of the date of this comment. 8/21/03
//but the cause is clear. Some cinematics do a radical change in the microphone position, which
//calls for a radical 3DSoundVolume adjustment. If this happens while a stereo stream is *ENDING*,
//low-level code gets caught in a tight loop. (Hangs) on some machines.
//To prevent this condition, we just suppress the updating of 3DSoundVolume while one of these
//is on the list. Since the music tracks play continuously, they never *END* during these cinematics.
//so we filter them out as, *NOT SENSITIVE*... we do want to update 3DSoundVolume during music, 
//which is almost all of the time.

Bool OpenALAudioManager::has3DSensitiveStreamsPlaying(void) const
{
	if (m_playingStreams.empty())
		return FALSE;

	for (std::list< PlayingAudio* >::const_iterator it = m_playingStreams.begin(); it != m_playingStreams.end(); ++it)
	{
		const PlayingAudio* playing = (*it);

		if (!playing)
			continue;

		// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null audioEventRTS/info
		if (!playing->m_audioEventRTS)
			continue;

		const AudioEventInfo* info = playing->m_audioEventRTS->getAudioEventInfo();
		if (!info)
			continue;

		if (info->m_soundType != AT_Music)
		{
			return TRUE;
		}

		if (playing->m_audioEventRTS->getEventName().startsWith("Game_") == FALSE)
		{
			return TRUE;
		}
	}

	return FALSE;

}


//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::processFadingList(void)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_fadingAudio.begin(); it != m_fadingAudio.end(); /* emtpy */) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_framesFaded >= getAudioSettings()->m_fadeAudioFrames) {
			playing->m_requestStop = true;
			//m_stoppedAudio.push_back(playing);
			releasePlayingAudio(playing);
			it = m_fadingAudio.erase(it);
			continue;
		}

		++playing->m_framesFaded;
		Real volume = getEffectiveVolume(playing->m_audioEventRTS);
		volume *= (1.0f - 1.0f * playing->m_framesFaded / getAudioSettings()->m_fadeAudioFrames);

		switch (playing->m_type)
		{
		case PAT_Sample:
		{
			alSourcef(playing->m_source, AL_GAIN, volume);
			break;
		}

		case PAT_3DSample:
		{
			alSourcef(playing->m_source, AL_GAIN, volume);
			break;
		}

		case PAT_Stream:
		{
			alSourcef(playing->m_stream->getSource(), AL_GAIN, volume);
			break;
		}

		}

		++it;
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::processStoppedList(void)
{
	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_stoppedAudio.begin(); it != m_stoppedAudio.end(); /* emtpy */) {
		playing = *it;
		if (playing) {
			releasePlayingAudio(playing);
		}
		it = m_stoppedAudio.erase(it);
	}
}


//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::shouldProcessRequestThisFrame(AudioRequest* req) const
{
	if (!req->m_usePendingEvent) {
		return true;
	}

	if (req->m_pendingEvent->getDelay() < MSEC_PER_LOGICFRAME_REAL) {
		return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::adjustRequest(AudioRequest* req)
{
	if (!req->m_usePendingEvent) {
		return;
	}

	req->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
	req->m_requiresCheckForSample = true;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::checkForSample(AudioRequest* req)
{
	if (!req->m_usePendingEvent) {
		return true;
	}

	if (req->m_pendingEvent->getAudioEventInfo() == NULL)
	{
		// Fill in event info
		getInfoForAudioEvent(req->m_pendingEvent);
	}

	if (req->m_pendingEvent->getAudioEventInfo()->m_type != AT_SoundEffect)
	{
		return true;
	}

	return m_sound->canPlayNow(req->m_pendingEvent);
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::setHardwareAccelerated(Bool accel)
{
	// Extends
	Bool retEarly = (accel == m_hardwareAccel);
	AudioManager::setHardwareAccelerated(accel);

	if (retEarly) {
		return;
	}

	if (m_hardwareAccel) {
		for (Int i = 0; i < MAX_HW_PROVIDERS; ++i) {
			UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[i]);
			TheAudio->selectProvider(providerNdx);
			if (getSelectedProvider() == providerNdx) {
				return;
			}
		}
	}

	// set it false
	AudioManager::setHardwareAccelerated(FALSE);
	UnsignedInt providerNdx = TheAudio->getProviderIndex(TheAudio->getAudioSettings()->m_preferred3DProvider[MAX_HW_PROVIDERS]);
	TheAudio->selectProvider(providerNdx);
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::setSpeakerSurround(Bool surround)
{
	// Extends
	Bool retEarly = (surround == m_surroundSpeakers);
	AudioManager::setSpeakerSurround(surround);

	if (retEarly) {
		return;
	}

	UnsignedInt speakerType;
	if (m_surroundSpeakers) {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType3D;
	}
	else {
		speakerType = TheAudio->getAudioSettings()->m_defaultSpeakerType2D;
	}

	TheAudio->setSpeakerType(speakerType);
}

//-------------------------------------------------------------------------------------------------
Real OpenALAudioManager::getFileLengthMS(AsciiString strToLoad) const
{
	if (strToLoad.isEmpty()) {
		return 0.0f;
	}
	float length = 0.0f;

#ifdef SAGE_USE_FFMPEG
	ALuint handle = m_audioCache->getBufferForFile(OpenFileInfo(&strToLoad));
	length = m_audioCache->getBufferLength(handle);
	m_audioCache->closeBuffer(handle);
#endif

	return length;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::closeAnySamplesUsingFile(const void* fileToClose)
{
	ALuint bufferHandle = (ALuint)(uintptr_t)fileToClose;
	if (!bufferHandle) {
		return;
	}

	std::list<PlayingAudio*>::iterator it;
	PlayingAudio* playing;

	for (it = m_playingSounds.begin(); it != m_playingSounds.end(); ) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_bufferHandle == bufferHandle) {
			releasePlayingAudio(playing);
			it = m_playingSounds.erase(it);
		}
		else {
			++it;
		}
	}

	for (it = m_playing3DSounds.begin(); it != m_playing3DSounds.end(); ) {
		playing = *it;
		if (!playing) {
			continue;
		}

		if (playing->m_bufferHandle == bufferHandle) {
			releasePlayingAudio(playing);
			it = m_playing3DSounds.erase(it);
		}
		else {
			++it;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::setDeviceListenerPosition(void)
{
	ALfloat listenerOri[] = { m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z, 0.0f, 0.0f, 1.0f };
	alListener3f(AL_POSITION, m_listenerPosition.x, m_listenerPosition.y, m_listenerPosition.z);
	alListenerfv(AL_ORIENTATION, listenerOri);
	DEBUG_LOG(("Listener Position: %f, %f, %f\n", m_listenerPosition.x, m_listenerPosition.y, m_listenerPosition.z));
}

//-------------------------------------------------------------------------------------------------
const Coord3D* OpenALAudioManager::getCurrentPositionFromEvent(AudioEventRTS* event)
{
	if (!event->isPositionalAudio()) {
		return NULL;
	}

	return event->getCurrentPosition();
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isOnScreen(const Coord3D* pos) const
{
	static ICoord2D dummy;
	// WorldToScreen will return True if the point is onscreen and false if it is offscreen.
	return TheTacticalView->worldToScreen(pos, &dummy);
}

//-------------------------------------------------------------------------------------------------
Real OpenALAudioManager::getEffectiveVolume(AudioEventRTS* event) const
{
	// GeneralsX @bugfix BenderAI 11/03/2026 - guard against null event or getAudioEventInfo()
	if (!event)
		return 0.0f;
	Real volume = 1.0f;
	volume *= (event->getVolume() * event->getVolumeShift());
	const AudioEventInfo* evInfo = event->getAudioEventInfo();
	if (evInfo && evInfo->m_soundType == AT_Music)
	{
		volume *= m_musicVolume;
	}
	else if (evInfo && evInfo->m_soundType == AT_Streaming)
	{
		volume *= m_speechVolume;
	}
	else
	{
		if (event->isPositionalAudio())
		{
			volume *= m_sound3DVolume;
		}
		else
		{
			volume *= m_soundVolume;
		}
	}

	return volume;
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::startNextLoop(PlayingAudio* looping)
{
	closeBuffer(looping->m_bufferHandle);
	looping->m_bufferHandle = 0;

	if (looping->m_requestStop) {
		return false;
	}

	if (looping->m_audioEventRTS->hasMoreLoops()) {
		// generate a new filename, and test to see whether we can play with it now
		looping->m_audioEventRTS->generateFilename();

		if (looping->m_audioEventRTS->getDelay() > MSEC_PER_LOGICFRAME_REAL) {
			// fake it out so that this sound appears done, but also so that it will not
			// delete the sound on completion (which would suck)
			looping->m_cleanupAudioEventRTS = false;
			looping->m_requestStop = true;

			AudioRequest* req = allocateAudioRequest(true);
			req->m_pendingEvent = looping->m_audioEventRTS;
			req->m_requiresCheckForSample = true;
			appendAudioRequest(req);
			return true;
		}

		if (looping->m_type == PAT_3DSample) {
			looping->m_bufferHandle = playSample3D(looping->m_audioEventRTS, looping);
		}
		else {
			looping->m_bufferHandle = playSample(looping->m_audioEventRTS, looping);
		}

		return looping->m_bufferHandle != 0;
	}
	return false;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::playStream(AudioEventRTS* event, OpenALAudioStream* stream)
{
	// Force it to the beginning
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		//alSourcei(stream->getSource(), AL_LOOPING, AL_TRUE);
	}

	stream->play();
	if (event->getAudioEventInfo()->m_soundType == AT_Music) {
		// Need to stop/fade out the old music here.
	}
}

//-------------------------------------------------------------------------------------------------
ALuint OpenALAudioManager::playSample(AudioEventRTS* event, PlayingAudio* audio)
{
	// Load the file in
	ALuint bufferHandle = loadBufferForRead(event);
	if (bufferHandle) {
		alSourcei(audio->m_source, AL_SOURCE_RELATIVE, AL_TRUE);
		alSourcei(audio->m_source, AL_BUFFER, (ALuint)(uintptr_t)bufferHandle);
		alSourcePlay(audio->m_source);
	}

	return bufferHandle;
}

//-------------------------------------------------------------------------------------------------
ALuint OpenALAudioManager::playSample3D(AudioEventRTS* event, PlayingAudio* sample3D)
{
	const Coord3D* pos = getCurrentPositionFromEvent(event);
	if (pos) {
		ALuint handle = loadBufferForRead(event);
		const AudioSettings* audioSettings = getAudioSettings();

		if (handle) {
			auto source = sample3D->m_source;
			Real x = pos->x;
			Real y = pos->y;
			Real z = pos->z;
			Real pitch = event->getPitchShift() != 0.0f ? event->getPitchShift() : 1.0f;
			pitch *= gxaPitchJitter(); // r029
			ALint channels = 0;
			alGetBufferi(handle, AL_CHANNELS, &channels);
			alSourcef(source, AL_PITCH, pitch);

			if (channels > 1) {
				// GeneralsX @bugfix Bender 09/05/2026 Fallback multichannel positional voice assets to direct playback instead of dropping them.
				alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
				alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
				alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
				alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
			#ifdef AL_DIRECT_CHANNELS_SOFT
				alSourcei(source, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);
			#endif
			#ifdef AL_SOURCE_SPATIALIZE_SOFT
				alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
			#endif
				DEBUG_LOG(("OpenAL positional fallback active for '%s' (%d channels)\n",
					event->getEventName().str(), channels));
			}
			else {
				alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
				// Set the position values of the sample here
				if (event->getAudioEventInfo()->m_type & ST_GLOBAL) {
					alSourcef(source, AL_REFERENCE_DISTANCE, audioSettings->m_globalMinRange);
					alSourcef(source, AL_MAX_DISTANCE, audioSettings->m_globalMaxRange);
				}
				else {
					alSourcef(source, AL_REFERENCE_DISTANCE, event->getAudioEventInfo()->m_minDistance);
					alSourcef(source, AL_MAX_DISTANCE, event->getAudioEventInfo()->m_maxDistance);
				}

				alSourcef(source, AL_ROLLOFF_FACTOR, 0.5f);
				alSource3f(source, AL_POSITION, x, y, z);
			}
			alSourcei(source, AL_BUFFER, handle);
			DEBUG_LOG(("Playing 3D sample '%s' at %f, %f, %f\n", event->getEventName().str(), x, y, z));

			// Start playback
			alSourcePlay(source);
		}
		return handle;
	}

	return 0;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::enumerateDevices(void)
{
	const ALCchar* devices = NULL;
	if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") == AL_TRUE) {
		devices = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
		if ((devices == nullptr || *devices == '\0') && alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE) {
			devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
		}
	}

	if (devices == nullptr) {
		DEBUG_LOG(("Enumerating OpenAL devices is not supported"));
		return;
	}

	const ALCchar* device = devices;
	const ALCchar* next = devices + 1;
	size_t len = 0;
	size_t idx = 0;
	while (device && *device != '\0' && next && *next != '\0' && idx < AL_MAX_PLAYBACK_DEVICES) {
		m_alDevicesList[idx++] = device;
		len = strlen(device);
		device += (len + 1);
		next += (len + 2);
	}

	m_alMaxDevicesIndex = idx;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::createListener(void)
{
	// OpenAL only has one listener
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::initDelayFilter(void)
{
}

//-------------------------------------------------------------------------------------------------
Bool OpenALAudioManager::isValidProvider(void)
{
	return (m_selectedProvider < m_providerCount);
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::initSamplePools(void)
{
	if (!(isOn(AudioAffect_Sound3D) && isValidProvider()))
	{
		return;
	}

	m_num2DSamples = getAudioSettings()->m_sampleCount2D;
	m_num3DSamples = getAudioSettings()->m_sampleCount3D;

	// Streams are basically free, so we can just allocate the appropriate number
	m_numStreams = getAudioSettings()->m_streamCount;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::processRequest(AudioRequest* req)
{
	switch (req->m_request)
	{
	case AR_Play:
	{
		playAudioEvent(req->m_pendingEvent);
		break;
	}
	case AR_Pause:
	{
		pauseAudioEvent(req->m_handleToInteractOn);
		break;
	}
	case AR_Stop:
	{
		stopAudioEvent(req->m_handleToInteractOn);
		break;
	}
	}
}

//-------------------------------------------------------------------------------------------------
void* OpenALAudioManager::getHandleForBink(void)
{
	if (!m_binkAudio) {
		DEBUG_LOG(("Creating Bink audio stream\n"));
		m_binkAudio = NEW OpenALAudioStream;
	}
	return m_binkAudio;
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::releaseHandleForBink(void)
{
	if (m_binkAudio) {
		DEBUG_LOG(("Releasing Bink audio stream\n"));
		delete m_binkAudio;
		m_binkAudio = NULL;
	}
}

//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS* eventToPlay)
{
	if (!eventToPlay->getAudioEventInfo()) {
		getInfoForAudioEvent(eventToPlay);
		if (!eventToPlay->getAudioEventInfo()) {
			DEBUG_CRASH(("No info for forced audio event '%s'\n", eventToPlay->getEventName().str()));
			return;
		}
	}

	switch (eventToPlay->getAudioEventInfo()->m_soundType)
	{
	case AT_Music:
		if (!isOn(AudioAffect_Music))
			return;
		break;
	case AT_SoundEffect:
		if (!isOn(AudioAffect_Sound) || !isOn(AudioAffect_Sound3D))
			return;
		break;
	case AT_Streaming:
		if (!isOn(AudioAffect_Speech))
			return;
		break;
	}

	// GeneralsX @bugfix BenderAI 11/03/2026 - heap-allocate so releasePlayingAudio can safely
	// delete it via m_cleanupAudioEventRTS. Stack allocation caused SIGSEGV in delete.
	AudioEventRTS* event = NEW AudioEventRTS(*eventToPlay);

	event->generateFilename();
	event->generatePlayInfo();

	std::list<std::pair<AsciiString, Real> >::iterator it;
	for (it = m_adjustedVolumes.begin(); it != m_adjustedVolumes.end(); ++it) {
		if (it->first == event->getEventName()) {
			event->setVolume(it->second);
			break;
		}
	}

	playAudioEvent(event);
}

#if defined(_DEBUG) || defined(_INTERNAL)
//-------------------------------------------------------------------------------------------------
void OpenALAudioManager::dumpAllAssetsUsed()
{
	if (!TheGlobalData->m_preloadReport) {
		return;
	}

	// Dump all the audio assets we've used.
	FILE* logfile = fopen("PreloadedAssets.txt", "a+");	//append to log
	if (!logfile)
		return;

	std::list<AsciiString> missingEvents;
	std::list<AsciiString> usedFiles;

	std::list<AsciiString>::iterator lit;

	fprintf(logfile, "\nAudio Asset Report - BEGIN\n");
	{
		SetAsciiStringIt it;
		std::vector<AsciiString>::iterator asIt;
		for (it = m_allEventsLoaded.begin(); it != m_allEventsLoaded.end(); ++it) {
			AsciiString astr = *it;
			AudioEventInfo* aei = findAudioEventInfo(astr);
			if (!aei) {
				missingEvents.push_back(astr);
				continue;
			}

			for (asIt = aei->m_attackSounds.begin(); asIt != aei->m_attackSounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_sounds.begin(); asIt != aei->m_sounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			for (asIt = aei->m_decaySounds.begin(); asIt != aei->m_decaySounds.end(); ++asIt) {
				usedFiles.push_back(*asIt);
			}

			if (!aei->m_filename.isEmpty()) {
				usedFiles.push_back(aei->m_filename);
			}
		}

		fprintf(logfile, "\nEvents Requested that are missing information - BEGIN\n");
		for (lit = missingEvents.begin(); lit != missingEvents.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nEvents Requested that are missing information - END\n");

		fprintf(logfile, "\nFiles Used - BEGIN\n");
		for (lit = usedFiles.begin(); lit != usedFiles.end(); ++lit) {
			fprintf(logfile, "%s\n", (*lit).str());
		}
		fprintf(logfile, "\nFiles Used - END\n");
	}
	fprintf(logfile, "\nAudio Asset Report - END\n");
	fclose(logfile);
	logfile = NULL;
}
#endif
