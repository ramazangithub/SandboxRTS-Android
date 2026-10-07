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

/*
** SDL3GameEngine.cpp
**
** Linux implementation of GameEngine using SDL3 for windowing/input.
**
** TheSuperHackers @feature CnC_Generals_Linux 07/02/2026
** Provides SDL3-based input and window management for Linux builds.
** Based on fighter19 reference implementation.
*/

#ifndef _WIN32

#include "SDL3GameEngine.h"
#include "OpenALAudioManager.h"
#include "SDL3Device/GameClient/SDL3Mouse.h"
#include "SDL3Device/GameClient/SDL3Keyboard.h"
#include "GameClient/View.h"
#include "GameClient/Mouse.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DWebBrowser.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "GameClient/Display.h"
#if defined(__ANDROID__)
Bool AndroidHud_HandleTap(Int x, Int y);   // InGameUI.cpp (r009 touch HUD)
void AndroidHud_SelectOnScreen();          // InGameUI.cpp (r011 double tap)
Bool AndroidHud_DoubleTapGround(Int x, Int y);   // InGameUI.cpp (r022 aggressive move)
Bool AndroidHud_HandleLongPress(Int x, Int y);   // InGameUI.cpp (r022 group slots)
Bool AndroidHud_FaceDrag(Int x, Int y);          // InGameUI.cpp (r026 formation front)
void AndroidHud_FaceEnd(Int x, Int y, Bool cancel);
Bool AndroidHud_MiniDown(Int x, Int y);          // InGameUI.cpp (r028 minimap drag)
Bool AndroidHud_MiniDrag(Int x, Int y);
void AndroidHud_MiniUp();
#else
static inline Bool AndroidHud_MiniDown(Int, Int) { return FALSE; }
static inline Bool AndroidHud_MiniDrag(Int, Int) { return FALSE; }
static inline void AndroidHud_MiniUp() {}
static inline Bool AndroidHud_HandleTap(Int, Int) { return FALSE; }
static inline void AndroidHud_SelectOnScreen() {}
static inline Bool AndroidHud_DoubleTapGround(Int, Int) { return FALSE; }
static inline Bool AndroidHud_HandleLongPress(Int, Int) { return FALSE; }
static inline Bool AndroidHud_FaceDrag(Int, Int) { return FALSE; }
static inline void AndroidHud_FaceEnd(Int, Int, Bool) {}
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__ANDROID__)
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// Extern globals for input devices (set by GameClient)
extern Mouse *TheMouse;
extern Keyboard *TheKeyboard;
extern GameWindowManager *TheWindowManager;

// GeneralsX @android FadiLabib 07/07/2026 - Touch-first platforms. The touch->mouse
// gesture translator and background render pause below are SHARED between iOS and
// Android: both are pure SDL (finger events in, synthetic mouse events out), and both
// platforms have the same constraints (single-tap must hover before clicking;
// presenting while the ANativeWindow / Metal drawable is gone is fatal). The system
// originated on iOS and was adopted by Android unchanged, then evolved here for both
// (pan/pinch mode-locking, DPI-scaled thresholds). Full design doc:
// docs/port/TOUCH_CONTROLS.md — tuning changes alter BOTH platforms' feel.
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
#define GX_TOUCH_UI 1
#else
#define GX_TOUCH_UI 0
#endif

#if GX_TOUCH_UI
#include <atomic>

// ---------------------------------------------------------------------------
// iOS app lifecycle
//
// iOS suspends the process when the app leaves the foreground. Any GPU work
// submitted around suspension stalls on drawable acquisition (MoltenVK waits
// out a timeout per present), which surfaces as multi-second input hangs right
// after resuming. SDL warns that lifecycle events can arrive outside the
// normal poll cycle, so they are captured in an event watcher that fires
// immediately on the delivering thread; the engine update loop checks the
// flag and skips simulation + rendering while backgrounded.
// ---------------------------------------------------------------------------
// Two independent reasons to halt the render/sim loop on iOS:
//  - BACKGROUNDED (home / switched away): the process is about to be suspended.
//  - INACTIVE (multitasking switcher open, Control Center, a notification
//    banner): iOS snapshots the window and owns the CAMetalLayer drawable during
//    this window — and crucially, opening the app switcher fires resign-active
//    WITHOUT a full background transition.
// Acquiring a Metal drawable during EITHER state fights iOS for the layer; across
// repeated suspend/switcher cycles MoltenVK is driven into an unrecoverable
// surface state and the app crashes (the reported "crashes after backgrounding /
// multitasking a few times"). Pause whenever either is set.
static std::atomic<bool> s_appBackgrounded{false};
static std::atomic<bool> s_appInactive{false};

static inline bool iosShouldPauseRendering()
{
	return s_appBackgrounded.load() || s_appInactive.load();
}

static bool SDLCALL iosLifecycleWatcher(void *userdata, SDL_Event *event)
{
	switch (event->type) {
		case SDL_EVENT_WILL_ENTER_BACKGROUND:
		case SDL_EVENT_DID_ENTER_BACKGROUND:
			s_appBackgrounded.store(true);
			break;
		case SDL_EVENT_DID_ENTER_FOREGROUND:
			s_appBackgrounded.store(false);
			break;
		// Resign/become active. On iOS, SDL maps applicationWillResignActive ->
		// window focus lost and applicationDidBecomeActive -> window focus gained.
		// Stay paused until fully active again (focus regained), which arrives
		// after DID_ENTER_FOREGROUND.
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			s_appInactive.store(true);
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			s_appInactive.store(false);
			break;
		default:
			break;
	}
	return true;
}

// ---------------------------------------------------------------------------
// iOS touch -> mouse gesture translation
//
// SDL's automatic touch-mouse synthesis is disabled on iOS (SDL3Main.cpp sets
// SDL_HINT_TOUCH_MOUSE_EVENTS=0); every mouse event the game sees on iOS is
// synthesized here, through the same SDL3Mouse::addSDLEvent path real mice use.
//
// Gestures (matching the game's stock control scheme, which is LMB-centric):
//   1 finger tap/drag     -> left button click / drag (select, command, drag-box)
//   1 finger long-press   -> right button click (deselect), if finger stays put
//   2 finger drag         -> right-button drag at the centroid (camera scroll)
//   2 finger pinch        -> mouse wheel (camera zoom)
// ---------------------------------------------------------------------------
namespace {

struct TouchState {
	enum Phase {
		IDLE,        // no fingers tracked
		PENDING,     // finger1 down, gesture identity not yet known, nothing sent
		DRAGGING,    // finger1 drag in progress, LMB held
		LONGPRESSED, // long-press fired (RMB click sent), swallow until lift
		TWO_PENDING, // two fingers down, pan-vs-pinch not yet decided, nothing sent
		PAN,         // two-finger camera pan, RMB held (pinch ignored)
		PINCH        // two-finger zoom, wheel ticks only (pan ignored)
	};

	Phase phase = IDLE;
	SDL_FingerID finger1 = 0;
	SDL_FingerID finger2 = 0;
	float downX = 0.0f, downY = 0.0f;   // finger1 down position (window points)
	float lastX = 0.0f, lastY = 0.0f;   // finger1 latest position
	float panX = 0.0f, panY = 0.0f;     // pan centroid
	float pinchDist = 0.0f;             // finger distance at last wheel step
	float twoCx0 = 0.0f, twoCy0 = 0.0f; // centroid at second-finger down (px)
	float twoDist0 = 0.0f;              // finger distance at second-finger down (px)
	Uint64 downTicks = 0;
	bool thresholdCrossed = false;      // finger1 passed the drag threshold, LMB not yet committed
	Uint64 thresholdCrossedTicks = 0;   // when it crossed, for the second-finger grace window
	bool oneFingerPan = false;             // r007: PAN driven by finger1 only
	Uint64 lastTapTicks = 0;               // commit time of the previous clean tap
	float lastTapX = 0.0f, lastTapY = 0.0f; // position of the previous clean tap
	float panLastX = 0.0f, panLastY = 0.0f;   // previous pan centroid (per-event delta)
	float panAccumX = 0.0f, panAccumY = 0.0f; // finger delta accrued since last frame flush
	float velX = 0.0f, velY = 0.0f;           // r010: smoothed per-frame pan offset
	Uint64 panStartTicks = 0;                // r012: pan ease-in
	float catchX = 0.0f, catchY = 0.0f;       // r015: travel made before the pan engaged
	bool gliding = false;                     // r010: inertia after lift (RMB still held)
	float glideX = 0.0f, glideY = 0.0f;
	float f1x = 0.0f, f1y = 0.0f, f2x = 0.0f, f2y = 0.0f; // normalized per finger
	float rot0 = 0.0f;        // r011: finger-vector angle at second-finger down
	float rotLast = 0.0f;     // r011: last applied finger-vector angle
	float rotAccum = 0.0f;    // r011: rotation accrued before the deadzone opens
	bool rotActive = false;   // r011: two-finger twist rotates the camera
	float wvX = 0.0f, wvY = 0.0f; // r011: smoothed world-space camera velocity per frame
};

TouchState s_touch;

const Uint64 LONG_PRESS_MS = 600;
// GeneralsX @android FadiLabib 07/07/2026 - Two fingers rarely touch down in the
// same SDL event: finger1's own motion can cross the drag threshold a few ms
// before finger2's FINGER_DOWN is delivered, prematurely committing a real LMB
// click (deselects the unit / drops a rally point) right before the gesture is
// recognized as a two-finger pan. Deferring the LMB commit by this long gives a
// same-gesture second finger time to land and redirect straight into the
// two-finger path, which never sends a click.
const Uint64 SECOND_FINGER_GRACE_MS = 30; // r015: was 60 - first swipe felt stuck
// GeneralsX @android FadiLabib 07/07/2026 - Touch double-tap -> double-click. A
// tap landing within this window AND near the previous tap emits clicks=2 on the
// button event, so the engine's double-click path fires (e.g. double-click a unit
// to select every same-type unit on screen). Matches SDL's default 500 ms
// multi-click window; the position slop reuses the gesture threshold.
const Uint64 DOUBLE_TAP_MS = 500;
// GeneralsX @android FadiLabib 07/07/2026 - 3% per tick (was 6%): with the
// pan/pinch mode lock below, zoom no longer fights camera pan, and the finer
// step doubles the wheel-tick rate for a smoother zoom feel.
const float PINCH_STEP_RATIO = 0.03f;  // 3% distance change per wheel tick

// GeneralsX @android FadiLabib 07/07/2026 - Two-finger pan speed. The engine's
// RMB scroll is a velocity joystick (scroll speed grows with cursor distance from
// the anchor, integrated every frame), so a full-screen finger swipe pins it at
// max speed and it keeps scrolling while the fingers stay displaced. We instead
// feed it only THIS frame's finger delta as the offset, so it behaves like a 1:1
// drag: the camera moves with the fingers and stops when they stop. PAN_GAIN
// scales that delta — 1.0 tracks the fingers; lower = slower camera.
// r009: user wants "swipe right->left = camera goes right" and a faster camera.
// r011: direct camera drag. 1.0 = the ground sticks to the finger; >1 = faster.
// r014: standard mobile feel - the ground sticks to the finger (1:1), a flick
// coasts with its own average speed and decays smoothly.
const float PAN_GAIN = 1.0f;
const float GLIDE_START = 1.0f;   // fraction of the release world velocity
const float GLIDE_DECAY = 0.93f;  // per-frame decay (~0.5 s coast)
const float ROTATE_SIGN = 1.0f;   // camera yaw follows the finger twist
const float ROTATE_DEADZONE = 0.10f; // rad of twist before rotation engages
static bool s_hudLongPress = false;  // r022: long press consumed by a HUD slot
const Uint64 TAP_MAX_MS = 450;    // longer presses are not taps (no flick double-taps)
const float PAN_MAX_FRAC = 0.30f; // r014: only clamps real spikes (lost frames), never normal swipes
// r010: inertia - after lift the camera keeps ~10% of the swipe momentum

// GeneralsX @android FadiLabib 07/07/2026 - Edge-hold scroll. A pure 1:1 drag can
// only move the camera as far as the fingers can travel, so it stops dead when
// they hit the screen edge — you can't cross a map bigger than one swipe. While
// the pan centroid sits within EDGE_MARGIN_FRAC of an edge, add a steady offset
// in that direction so the engine's RMB joystick keeps scrolling (a slow, fixed
// velocity — this is the knob if it feels too fast/slow), letting you traverse
// the whole map by parking your fingers at the edge. Normal drags never reach it.
// The 3D view is full-screen height but the control bar is painted over the
// bottom strip, so fingers stop at the play area and never reach a 6% bottom
// band. Trigger the bottom edge higher up, above the control bar.
// The engine's RMB scroll speed is proportional to the cursor's offset from the
// anchor measured in INTERNAL display pixels, and internal res now tracks the
// panel (see SDL3Main's -xres/-yres). A fixed pixel offset therefore scrolls at
// wildly different speeds per resolution (5px was fine at the old 4:3 internal
// res, ~5% of keyboard-scroll speed at native res). Express it as a fraction of
// window width so the feel is resolution-independent. This is the speed knob.

// GeneralsX @android FadiLabib 07/07/2026 - Gesture thresholds in PHYSICAL size,
// not pixels. The old fixed 8 px is ~0.7 mm on a Tab S7+ (2800x1752 @ ~274 ppi):
// normal fingertip jitter crosses it, so taps kept committing as accidental
// drag-boxes. 3 mm is a comfortable "deliberate movement" distance on every
// tested panel. SDL_GetDisplayContentScale is 1.0 at the 160 dpi Android/desktop
// baseline, so px/mm = 160 * scale / 25.4. Falls back to the old 8 px minimum if
// the display query fails (returns 0).
const float GESTURE_THRESHOLD_MM = 2.0f; // r015: was 3 - first swipe felt stuck

float gestureThresholdPx(SDL_Window *window)
{
	static float cached = 0.0f;
	if (cached > 0.0f) {
		return cached;
	}
	float scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
	if (scale <= 0.0f) {
		scale = 1.0f;
	}
	const float pxPerMm = (160.0f * scale) / 25.4f;
	float px = GESTURE_THRESHOLD_MM * pxPerMm;
	if (px < 8.0f) {
		px = 8.0f;  // never more sensitive than the old fixed value
	}
	cached = px;
	return cached;
}

void sendSyntheticMouse(SDL3Mouse *mouse, SDL_Window *window, Uint32 type,
                        float x, float y, Uint8 button = 0, float wheelY = 0.0f,
                        Uint8 clicks = 1)
{
	// The windowID must be valid: SDL3Mouse::scaleMouseCoordinates() looks the
	// window up by id to map window points into the game's internal resolution,
	// and silently skips scaling when the lookup fails.
	const SDL_WindowID windowID = SDL_GetWindowID(window);

	SDL_Event ev;
	SDL_zero(ev);
	ev.type = type;
	switch (type) {
		case SDL_EVENT_MOUSE_MOTION:
			ev.motion.windowID = windowID;
			ev.motion.x = x;
			ev.motion.y = y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			ev.button.windowID = windowID;
			ev.button.button = button;
			ev.button.down = (type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			ev.button.clicks = clicks;
			ev.button.x = x;
			ev.button.y = y;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			ev.wheel.windowID = windowID;
			ev.wheel.x = 0.0f;
			ev.wheel.y = wheelY;
			ev.wheel.mouse_x = x;
			ev.wheel.mouse_y = y;
			break;
	}
	mouse->addSDLEvent(&ev);
}

// GeneralsX @android FadiLabib 07/07/2026 - Two fingers no longer commit to a
// combined pan+pinch immediately. The old code held RMB (camera scroll) AND
// emitted wheel ticks (zoom) from the same two fingers at once, so a camera pan
// leaked spurious zoom steps (the finger distance always wobbles) and a pinch
// dragged the camera (the centroid always drifts) — the reported "pan feels
// wrong" and "zoom is jumpy". Desktop can't scroll and zoom simultaneously
// either, so we classify first: whichever crosses the movement threshold first
// — centroid travel (pan) or finger-distance change (pinch) — locks the gesture
// mode until a finger lifts. The loser is ignored for the rest of the gesture.
float wrapAngle(float a)
{
	while (a > 3.14159265f) a -= 6.2831853f;
	while (a < -3.14159265f) a += 6.2831853f;
	return a;
}

float fingerAngle(int winW, int winH)
{
	return SDL_atan2f((s_touch.f2y - s_touch.f1y) * (float)winH, (s_touch.f2x - s_touch.f1x) * (float)winW);
}

// r011: move the camera look-at point by a world-space delta (no mouse buttons involved,
// so nothing can be read as a right-click / deselect / UI close).
void touchMoveCameraWorld(float wx, float wy)
{
	if (!TheTacticalView || (wx == 0.0f && wy == 0.0f)) {
		return;
	}
	Coord3D p = TheTacticalView->getPosition();
	p.x += wx;
	p.y += wy;
	p.z = 0.0f;  // ground target: W3DView::lookAt skips its ray cast
	TheTacticalView->userLookAt(&p);
}

// r011: finger moved by (sdx, sdy) window points -> world delta that keeps the ground
// under the finger (x PAN_GAIN). Measured around the screen centre so the speed does
// not explode near the horizon. Returns the world delta applied.
void touchScrollScreen(SDL_Window *window, float sdx, float sdy, float *outWx, float *outWy)
{
	*outWx = 0.0f;
	*outWy = 0.0f;
	if (!TheTacticalView || !TheDisplay || (sdx == 0.0f && sdy == 0.0f)) {
		return;
	}
	int ww = 0, wh = 0;
	SDL_GetWindowSize(window, &ww, &wh);
	if (ww <= 0 || wh <= 0) {
		return;
	}
	const float dW = (float)TheDisplay->getWidth();
	const float dH = (float)TheDisplay->getHeight();
	const float ddx = sdx * dW / (float)ww * PAN_GAIN;
	const float ddy = sdy * dH / (float)wh * PAN_GAIN;
	// probe with an enlarged vector for sub-pixel precision, kept on screen
	const float len = SDL_fabsf(ddx) + SDL_fabsf(ddy);
	float k = 8.0f;
	if (len * k > dH * 0.3f) {
		k = (dH * 0.3f) / len;
	}
	ICoord2D c;
	c.x = (Int)(dW * 0.5f);
	c.y = (Int)(dH * 0.5f);
	ICoord2D e;
	e.x = c.x + (Int)(ddx * k);
	e.y = c.y + (Int)(ddy * k);
	if (e.x == c.x && e.y == c.y) {
		return;
	}
	Coord3D wc, we;
	TheTacticalView->screenToTerrain(&c, &wc);
	TheTacticalView->screenToTerrain(&e, &we);
	const float wx = (wc.x - we.x) / k;
	const float wy = (wc.y - we.y) / k;
	touchMoveCameraWorld(wx, wy);
	*outWx = wx;
	*outWy = wy;
}

void beginTwoPending(int winW, int winH)
{
	s_touch.twoCx0 = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
	s_touch.twoCy0 = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
	const float dx = (s_touch.f1x - s_touch.f2x) * (float)winW;
	const float dy = (s_touch.f1y - s_touch.f2y) * (float)winH;
	s_touch.twoDist0 = SDL_sqrtf(dx * dx + dy * dy);
	s_touch.rot0 = fingerAngle(winW, winH);
	s_touch.rotActive = false;
	s_touch.phase = TouchState::TWO_PENDING;
}

void beginPan(SDL3Mouse *mouse, SDL_Window *window, int winW, int winH)
{
	(void)mouse;
	(void)window;
	s_touch.panX = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
	s_touch.panY = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
	s_touch.panLastX = s_touch.panX;
	s_touch.panLastY = s_touch.panY;
	s_touch.panAccumX = 0.0f;
	s_touch.panAccumY = 0.0f;
	s_touch.wvX = s_touch.wvY = 0.0f;
	s_touch.rotLast = fingerAngle(winW, winH);
	s_touch.rotAccum = wrapAngle(s_touch.rotLast - s_touch.rot0);
	s_touch.oneFingerPan = false;
	s_touch.panStartTicks = SDL_GetTicks();
	s_touch.catchX = s_touch.catchY = 0.0f;
	s_touch.phase = TouchState::PAN;
}

void beginOneFingerPan(SDL3Mouse *mouse, SDL_Window *window)
{
	(void)mouse;
	(void)window;
	s_touch.panX = s_touch.downX;
	s_touch.panY = s_touch.downY;
	s_touch.panLastX = s_touch.lastX;
	s_touch.panLastY = s_touch.lastY;
	// r012: the threshold/grace travel is NOT applied in one frame any more (that
	// was the jump/teleport on the first swipe); the pan starts from here.
	s_touch.panAccumX = 0.0f;
	s_touch.panAccumY = 0.0f;
	// r015: ...but it is not thrown away either (that felt like heavy friction):
	// it is caught up smoothly over the next few frames.
	s_touch.catchX = s_touch.lastX - s_touch.downX;
	s_touch.catchY = s_touch.lastY - s_touch.downY;
	s_touch.panStartTicks = SDL_GetTicks();
	if (!s_touch.gliding) s_touch.wvX = s_touch.wvY = 0.0f;
	s_touch.rotActive = false;
	s_touch.finger2 = (SDL_FingerID)~(SDL_FingerID)0;
	s_touch.oneFingerPan = true;
	s_touch.phase = TouchState::PAN;
}

void handleTouchEvent(SDL3Mouse *mouse, SDL_Window *window, const SDL_Event &event)
{
	int winW = 0, winH = 0;
	SDL_GetWindowSize(window, &winW, &winH);
	const float px = event.tfinger.x * (float)winW;
	const float py = event.tfinger.y * (float)winH;

	switch (event.type) {
	case SDL_EVENT_FINGER_DOWN:
		if (s_touch.gliding) {
			// r010: finger stops the glide (catch the camera)
			s_touch.gliding = false;
		}
		if (s_touch.phase == TouchState::IDLE) {
			// Defer all BUTTON output: a finger landing could become a tap, a
			// drag-box, a long-press, or the first finger of a camera pan. A
			// premature LMB down+up is a real click to the game (e.g. it sets a
			// rally point when a production building is selected).
			s_touch.finger1 = event.tfinger.fingerID;
			s_touch.phase = TouchState::PENDING;
			s_touch.downX = s_touch.lastX = px;
			s_touch.downY = s_touch.lastY = py;
			s_touch.f1x = event.tfinger.x;
			s_touch.f1y = event.tfinger.y;
			s_touch.downTicks = SDL_GetTicks();
			s_touch.thresholdCrossed = false;
			{
				const float msx = (TheDisplay && winW > 0) ? (float)TheDisplay->getWidth() / (float)winW : 1.0f;
				const float msy = (TheDisplay && winH > 0) ? (float)TheDisplay->getHeight() / (float)winH : 1.0f;
				AndroidHud_MiniDown((Int)(px * msx), (Int)(py * msy)); // r028
			}
			// Move the cursor to the touch point NOW (motion clicks nothing, so the
			// deferred-tap protection is intact). This lets the GUI process hover
			// over the next frame(s) before the tap commits — hover-driven widgets
			// (e.g. the Generals Challenge general buttons, which are checkboxes
			// that ignore a click unless WIN_STATE_HILITED was set by a prior
			// mouse-enter) then accept the click. Real mice hover before clicking;
			// without this, a synthetic tap teleports + clicks in one instant and
			// the widget is never hilited, so only the default/first item responds.
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, px, py);
		}
		else if (s_touch.phase == TouchState::PENDING) {
			// Second finger before the first committed to anything: a two-finger
			// gesture (pan or pinch — decided by whichever moves first), no
			// left-click ever happened.
			s_touch.finger2 = event.tfinger.fingerID;
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
			beginTwoPending(winW, winH);
		}
		else if (s_touch.phase == TouchState::DRAGGING) {
			// Second finger during a live drag: finish the drag-box, then decide
			// pan vs pinch.
			s_touch.finger2 = event.tfinger.fingerID;
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
			                   s_touch.lastX, s_touch.lastY, SDL_BUTTON_LEFT);
			beginTwoPending(winW, winH);
		}
		// LONGPRESSED / TWO_PENDING / PAN / PINCH with extra fingers: ignored
		break;

	case SDL_EVENT_FINGER_MOTION:
		if (event.tfinger.fingerID == s_touch.finger1) {
			s_touch.f1x = event.tfinger.x;
			s_touch.f1y = event.tfinger.y;
			s_touch.lastX = px;
			s_touch.lastY = py;
			if (s_touch.phase == TouchState::PENDING || s_touch.phase == TouchState::LONGPRESSED) {
				// r028: finger held on the minimap -> the camera follows it (no drag box, no pan)
				const float msx = (TheDisplay && winW > 0) ? (float)TheDisplay->getWidth() / (float)winW : 1.0f;
				const float msy = (TheDisplay && winH > 0) ? (float)TheDisplay->getHeight() / (float)winH : 1.0f;
				if (AndroidHud_MiniDrag((Int)(px * msx), (Int)(py * msy)))
					break;
			}
		} else if ((s_touch.phase == TouchState::TWO_PENDING ||
		            s_touch.phase == TouchState::PAN ||
		            s_touch.phase == TouchState::PINCH) &&
		           event.tfinger.fingerID == s_touch.finger2) {
			s_touch.f2x = event.tfinger.x;
			s_touch.f2y = event.tfinger.y;
		} else {
			break;
		}

		if (s_touch.phase == TouchState::PENDING && event.tfinger.fingerID == s_touch.finger1) {
			const float moved = SDL_fabsf(px - s_touch.downX) + SDL_fabsf(py - s_touch.downY);
			if (moved >= gestureThresholdPx(window) && !s_touch.thresholdCrossed) {
				// Threshold crossed, but don't commit the LMB click yet — a second
				// finger landing in the next few ms (see SECOND_FINGER_GRACE_MS)
				// means this was actually a two-finger gesture starting, not a drag.
				s_touch.thresholdCrossed = true;
				s_touch.thresholdCrossedTicks = SDL_GetTicks();
			}
		}
		else if (s_touch.phase == TouchState::LONGPRESSED && event.tfinger.fingerID == s_touch.finger1) {
			if (s_hudLongPress) {
				// r026: HUD owns this press (formation front drag) - no box, no mouse
				int fw = 0, fh = 0;
				SDL_GetWindowSize(window, &fw, &fh);
				const float fsx = (TheDisplay && fw > 0) ? (float)TheDisplay->getWidth() / (float)fw : 1.0f;
				const float fsy = (TheDisplay && fh > 0) ? (float)TheDisplay->getHeight() / (float)fh : 1.0f;
				AndroidHud_FaceDrag((Int)(px * fsx), (Int)(py * fsy));
			} else {
			// r007: long-press armed the box (LMB already down) - dragging grows it
			const float moved = SDL_fabsf(px - s_touch.downX) + SDL_fabsf(py - s_touch.downY);
			if (moved >= gestureThresholdPx(window)) {
				sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, px, py);
				s_touch.phase = TouchState::DRAGGING;
			}
			}
		}
		else if (s_touch.phase == TouchState::DRAGGING && event.tfinger.fingerID == s_touch.finger1) {
			sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, px, py);
		}
		else if (s_touch.phase == TouchState::PAN && s_touch.oneFingerPan) {
			if (event.tfinger.fingerID == s_touch.finger1) {
				s_touch.panAccumX += px - s_touch.panLastX;
				s_touch.panAccumY += py - s_touch.panLastY;
				s_touch.panLastX = px;
				s_touch.panLastY = py;
			}
		}
		else if (s_touch.phase == TouchState::TWO_PENDING) {
			// Classify: pan (centroid travel) vs pinch (distance change) —
			// whichever crosses the threshold first wins the whole gesture.
			const float cx = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
			const float cy = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
			const float dx = (s_touch.f1x - s_touch.f2x) * (float)winW;
			const float dy = (s_touch.f1y - s_touch.f2y) * (float)winH;
			const float dist = SDL_sqrtf(dx * dx + dy * dy);
			const float threshold = gestureThresholdPx(window);

			const float centroidMoved = SDL_fabsf(cx - s_touch.twoCx0) + SDL_fabsf(cy - s_touch.twoCy0);
			const float distChanged = SDL_fabsf(dist - s_touch.twoDist0);

			const float twist = SDL_fabsf(wrapAngle(fingerAngle(winW, winH) - s_touch.rot0));
			const float arc = twist * dist * 0.5f;  // px travelled along the circle

			if (arc >= threshold && arc > centroidMoved && arc > distChanged) {
				// r011: two-finger twist -> rotate (pan rides along)
				beginPan(mouse, window, winW, winH);
				s_touch.rotActive = true;
			}
			else if (distChanged >= threshold && distChanged > centroidMoved) {
				// Pinch wins: zoom only, camera never moves.
				s_touch.pinchDist = dist;
				s_touch.phase = TouchState::PINCH;
			}
			else if (centroidMoved >= threshold) {
				// Pan wins: RMB camera scroll only, zoom never fires.
				beginPan(mouse, window, winW, winH);
			}
		}
		else if (s_touch.phase == TouchState::PAN) {
			// Accumulate finger travel; the actual motion (anchor + delta) is emitted
			// once per engine frame in updateTouchLongPress so the scroll speed is
			// independent of the touch sampling rate.
			const float cx = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
			const float cy = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
			s_touch.panAccumX += cx - s_touch.panLastX;
			s_touch.panAccumY += cy - s_touch.panLastY;
			s_touch.panLastX = cx;
			s_touch.panLastY = cy;
			// r011: two-finger twist rotates the camera
			const float ang = fingerAngle(winW, winH);
			const float dAng = wrapAngle(ang - s_touch.rotLast);
			s_touch.rotLast = ang;
			if (!s_touch.rotActive) {
				s_touch.rotAccum += dAng;
				if (SDL_fabsf(s_touch.rotAccum) >= ROTATE_DEADZONE) {
					s_touch.rotActive = true;
				}
			} else if (TheTacticalView && dAng != 0.0f) {
				TheTacticalView->userSetAngle(TheTacticalView->getAngle() + ROTATE_SIGN * dAng);
			}
		}
		else if (s_touch.phase == TouchState::PINCH) {
			const float cx = (s_touch.f1x + s_touch.f2x) * 0.5f * (float)winW;
			const float cy = (s_touch.f1y + s_touch.f2y) * 0.5f * (float)winH;
			const float dx = (s_touch.f1x - s_touch.f2x) * (float)winW;
			const float dy = (s_touch.f1y - s_touch.f2y) * (float)winH;
			const float dist = SDL_sqrtf(dx * dx + dy * dy);
			if (s_touch.pinchDist > 1.0f) {
				const float ratio = dist / s_touch.pinchDist;
				if (ratio > 1.0f + PINCH_STEP_RATIO) {
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, cx, cy);
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_WHEEL, cx, cy, 0, 1.0f);
					s_touch.pinchDist = dist;
				} else if (ratio < 1.0f - PINCH_STEP_RATIO) {
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, cx, cy);
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_WHEEL, cx, cy, 0, -1.0f);
					s_touch.pinchDist = dist;
				}
			}
		}
		break;

	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_CANCELED:
		if (event.tfinger.fingerID == s_touch.finger1)
			AndroidHud_MiniUp(); // r028 (the tap path still sees whether the finger slid)
		if (event.tfinger.fingerID != s_touch.finger1 &&
		    !((s_touch.phase == TouchState::TWO_PENDING ||
		       s_touch.phase == TouchState::PAN ||
		       s_touch.phase == TouchState::PINCH) &&
		      event.tfinger.fingerID == s_touch.finger2)) {
			break;
		}
		switch (s_touch.phase) {
			case TouchState::PENDING:
				// A CANCELED touch (incoming call, notification shade, palm
				// rejection) must not become a committed tap — that would be a
				// phantom select/command/rally-point click at the cancel point.
				if (event.type == SDL_EVENT_FINGER_CANCELED) {
					break;
				}
				// Clean tap: deliver the full click at the exact press position. A
				// second tap within DOUBLE_TAP_MS and near the first carries clicks=2
				// so the engine sees a double-click (select-all-same-type units).
				{
					const Uint64 now = SDL_GetTicks();
					const float slop = gestureThresholdPx(window) * 2.0f;
					// r011: a fast flick lifts before the pan engages. It moved / lasted too
					// long to be a tap -> apply it as a camera scroll, never as a (double) tap.
					const float upMoved = SDL_fabsf(px - s_touch.downX) + SDL_fabsf(py - s_touch.downY);
					if (s_touch.thresholdCrossed || upMoved >= gestureThresholdPx(window) ||
					    (now - s_touch.downTicks) > TAP_MAX_MS) {
						s_touch.lastTapTicks = 0;
						if (upMoved >= gestureThresholdPx(window)) {
							float wx = 0.0f, wy = 0.0f;
							touchScrollScreen(window, px - s_touch.downX, py - s_touch.downY, &wx, &wy);
							const float frames = SDL_max(1.0f, (float)(now - s_touch.downTicks) / 16.7f);
							s_touch.glideX = wx / frames * GLIDE_START;
							s_touch.glideY = wy / frames * GLIDE_START;
							s_touch.gliding = true;
						}
						break;
					}
					const bool dbl =
						s_touch.lastTapTicks != 0 &&
						(now - s_touch.lastTapTicks) <= DOUBLE_TAP_MS &&
						SDL_fabsf(s_touch.downX - s_touch.lastTapX) <= slop &&
						SDL_fabsf(s_touch.downY - s_touch.lastTapY) <= slop;
					int hudWinW = 0, hudWinH = 0;
					SDL_GetWindowSize(window, &hudWinW, &hudWinH);
					const float hudSX = (TheDisplay && hudWinW > 0) ? (float)TheDisplay->getWidth() / (float)hudWinW : 1.0f;
					const float hudSY = (TheDisplay && hudWinH > 0) ? (float)TheDisplay->getHeight() / (float)hudWinH : 1.0f;
					if (AndroidHud_HandleTap((Int)(s_touch.downX * hudSX), (Int)(s_touch.downY * hudSY))) {
						// r009: siege / deselect HUD button consumed the tap
						s_touch.lastTapTicks = 0;
						break;
					}
					if (dbl && TheMessageStream) {
						// r022: double tap on the ground with units selected = aggressive move;
						// otherwise (r011) select own units visible on screen only
						if (!AndroidHud_DoubleTapGround((Int)(s_touch.downX * hudSX), (Int)(s_touch.downY * hudSY)))
							AndroidHud_SelectOnScreen();
					} else {
						sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, s_touch.downX, s_touch.downY);
						sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_DOWN,
						                   s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT, 0.0f, 1);
						sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
						                   s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT, 0.0f, 1);
					}
					// Reset after a double so a triple-tap doesn't chain; otherwise
					// anchor this tap for the next one.
					s_touch.lastTapTicks = dbl ? 0 : now;
					s_touch.lastTapX = s_touch.downX;
					s_touch.lastTapY = s_touch.downY;
				}
				break;
			case TouchState::DRAGGING:
				sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP, px, py, SDL_BUTTON_LEFT);
				break;
			case TouchState::LONGPRESSED:
				if (s_hudLongPress) {
					s_hudLongPress = false;
					int uw = 0, uh = 0;
					SDL_GetWindowSize(window, &uw, &uh);
					const float usx = (TheDisplay && uw > 0) ? (float)TheDisplay->getWidth() / (float)uw : 1.0f;
					const float usy = (TheDisplay && uh > 0) ? (float)TheDisplay->getHeight() / (float)uh : 1.0f;
					AndroidHud_FaceEnd((Int)(px * usx), (Int)(py * usy), event.type == SDL_EVENT_FINGER_CANCELED);
				}
				else
					sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_UP,
					                   s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT);
				break;
			case TouchState::PAN:
				// r011: no mouse buttons are held during a pan any more; just coast.
				if (event.type != SDL_EVENT_FINGER_CANCELED &&
				    (SDL_fabsf(s_touch.wvX) + SDL_fabsf(s_touch.wvY)) > 0.05f) {
					s_touch.gliding = true;
					s_touch.glideX = s_touch.wvX * GLIDE_START;
					s_touch.glideY = s_touch.wvY * GLIDE_START;
				}
				s_touch.wvX = s_touch.wvY = 0.0f;
				s_touch.rotActive = false;
				break;
			// TWO_PENDING / PINCH hold no buttons — nothing to release.
			default:
				break;
		}
		s_touch.phase = TouchState::IDLE;
		break;
	}
}

// Called once per engine frame (not just per touch event): a perfectly
// stationary finger produces no SDL events, so the long-press timer must be
// polled from the frame loop or it would never fire.
void updateTouchLongPress(SDL3Mouse *mouse, SDL_Window *window)
{
	if (s_touch.gliding) {
		if (s_touch.phase == TouchState::PAN) {
			s_touch.gliding = false;
			s_touch.wvX = s_touch.glideX;
			s_touch.wvY = s_touch.glideY;
		} else if (s_touch.phase != TouchState::IDLE && s_touch.phase != TouchState::PENDING) {
			s_touch.gliding = false;
		} else {
			touchMoveCameraWorld(s_touch.glideX, s_touch.glideY);
			s_touch.glideX *= GLIDE_DECAY;
			s_touch.glideY *= GLIDE_DECAY;
			if (SDL_fabsf(s_touch.glideX) + SDL_fabsf(s_touch.glideY) < 0.05f) {
				s_touch.gliding = false;
			}
		}
	}
	if (s_touch.phase == TouchState::PENDING &&
	    (SDL_GetTicks() - s_touch.downTicks) >= LONG_PRESS_MS) {
		{
			// r022: long press on a group slot = store the current selection there
			int lw = 0, lh = 0;
			SDL_GetWindowSize(window, &lw, &lh);
			const float lsx = (TheDisplay && lw > 0) ? (float)TheDisplay->getWidth() / (float)lw : 1.0f;
			const float lsy = (TheDisplay && lh > 0) ? (float)TheDisplay->getHeight() / (float)lh : 1.0f;
			if (AndroidHud_HandleLongPress((Int)(s_touch.downX * lsx), (Int)(s_touch.downY * lsy))) {
				s_touch.phase = TouchState::LONGPRESSED;
				s_hudLongPress = true;
				return;
			}
		}
		// r007: long-press arms the selection box: LMB goes down at the press
		// point, the following drag grows the green box, lift selects.
		sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_MOTION, s_touch.downX, s_touch.downY);
		sendSyntheticMouse(mouse, window, SDL_EVENT_MOUSE_BUTTON_DOWN,
		                   s_touch.downX, s_touch.downY, SDL_BUTTON_LEFT);
		s_touch.phase = TouchState::LONGPRESSED;
	}

	// Threshold was crossed but the LMB commit was deferred (see
	// SECOND_FINGER_GRACE_MS above) — if the grace window has passed with no
	// second finger, this really is a single-finger drag: commit it now.
	if (s_touch.phase == TouchState::PENDING && s_touch.thresholdCrossed &&
	    (SDL_GetTicks() - s_touch.thresholdCrossedTicks) >= SECOND_FINGER_GRACE_MS) {
		// r007: a plain 1-finger drag moves the camera (box select = long-press + drag)
		beginOneFingerPan(mouse, window);
	}

	// Flush one frame's worth of pan travel as an RMB-scroll offset from the fixed
	// anchor, then clear it. Sending anchor+delta (delta==0 when the fingers are
	// still) makes the engine's velocity-joystick scroll behave like a 1:1 drag and
	// stop the instant the fingers stop.
	if (s_touch.phase == TouchState::PAN) {
		// r011: direct camera drag, once per frame with all finger motion since last frame
		float sdx = s_touch.panAccumX;
		float sdy = s_touch.panAccumY;

		int winW = 0, winH = 0;
		SDL_GetWindowSize(window, &winW, &winH);
		// r012: no edge auto-scroll on touch. r014: no ease-in; spike clamp only.
		// r015: feed the pre-pan travel in, half of what is left each frame.
		if (s_touch.catchX != 0.0f || s_touch.catchY != 0.0f) {
			float tx = s_touch.catchX * 0.5f, ty = s_touch.catchY * 0.5f;
			if (SDL_fabsf(s_touch.catchX) + SDL_fabsf(s_touch.catchY) < 1.0f) { tx = s_touch.catchX; ty = s_touch.catchY; }
			sdx += tx; sdy += ty;
			s_touch.catchX -= tx; s_touch.catchY -= ty;
		}
		const float maxD = PAN_MAX_FRAC * (float)(winW > winH ? winW : winH);
		if (sdx > maxD) sdx = maxD; else if (sdx < -maxD) sdx = -maxD;
		if (sdy > maxD) sdy = maxD; else if (sdy < -maxD) sdy = -maxD;

		float wx = 0.0f, wy = 0.0f;
		touchScrollScreen(window, sdx, sdy, &wx, &wy);
		s_touch.wvX = 0.5f * s_touch.wvX + 0.5f * wx;
		s_touch.wvY = 0.5f * s_touch.wvY + 0.5f * wy;
		s_touch.panAccumX = 0.0f;
		s_touch.panAccumY = 0.0f;
	}
}

} // anonymous namespace
#endif // GX_TOUCH_UI

namespace {

Bool DecodeNextUtf8Codepoint(const char* text, size_t length, size_t& offset, UnsignedInt& outCodepoint)
{
	outCodepoint = 0;
	if (!text || offset >= length) {
		return false;
	}

	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first == 0) {
		return false;
	}

	if (first < 0x80) {
		outCodepoint = first;
		offset += 1;
		return true;
	}

	if ((first & 0xE0) == 0xC0 && offset + 1 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		if ((second & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x1F) << 6) | (second & 0x3F);
			offset += 2;
			return true;
		}
	}

	if ((first & 0xF0) == 0xE0 && offset + 2 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
			offset += 3;
			return true;
		}
	}

	if ((first & 0xF8) == 0xF0 && offset + 3 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		const unsigned char fourth = static_cast<unsigned char>(text[offset + 3]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80 && (fourth & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
			offset += 4;
			return true;
		}
	}

	// Invalid UTF-8 sequence: skip one byte and keep processing.
	offset += 1;
	return false;
}

}

/**
 * Constructor: Initialize SDL3 game engine state
 */
SDL3GameEngine::SDL3GameEngine()
	: GameEngine(),
	  m_SDLWindow(nullptr),
	  m_IsInitialized(false),
	  m_IsActive(false),
	  m_IsTextInputActive(false),
	  m_TextInputFocusWindow(nullptr)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::SDL3GameEngine() created\n");
}

/**
 * Destructor: Cleanup SDL3 resources
 */
SDL3GameEngine::~SDL3GameEngine()
{
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}

	if (m_IsInitialized) {
		// Window cleanup is done in reset/shutdown
	}
	fprintf(stderr, "DEBUG: SDL3GameEngine::~SDL3GameEngine() destroyed\n");
}

/**
 * From GameEngine: init() - initialize subsystems
 * 
 * GeneralsX @bugfix felipebraz 16/02/2026
 * Simplified to follow fighter19 pattern - SDL3/Vulkan initialized in SDL3Main.cpp
 * before GameEngine is created. This init() only delegates to parent GameEngine::init().
 * ApplicationHWnd and TheSDL3Window are already set by main() before this is called.
 */
void SDL3GameEngine::init(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::init() starting\n");

	if (TheGlobalData && TheGlobalData->m_headless) {
		// GeneralsX @bugfix Copilot 17/05/2026 Allow headless replay path to initialize engine subsystems without an SDL window.
		fprintf(stderr, "INFO: SDL3GameEngine::init() headless mode - skipping SDL window binding\n");
		m_SDLWindow = nullptr;
		m_IsInitialized = true;
		m_IsActive = true;
		GameEngine::init();
		return;
	}

	// Verify window was created by SDL3Main.cpp
	extern SDL_Window* TheSDL3Window;
	extern HWND ApplicationHWnd;
	
	if (!TheSDL3Window || !ApplicationHWnd) {
		fprintf(stderr, "FATAL: SDL3 window not initialized before GameEngine::init()\n");
		fprintf(stderr, "FATAL: TheSDL3Window=%p, ApplicationHWnd=%p\n", TheSDL3Window, ApplicationHWnd);
		return;
	}

	// Store window reference locally
	m_SDLWindow = TheSDL3Window;
	m_IsInitialized = true;
	m_IsActive = true;

#if GX_TOUCH_UI
	// Lifecycle events can fire outside the poll cycle on iOS; catch them
	// immediately so rendering halts before the process is suspended.
	SDL_AddEventWatch(iosLifecycleWatcher, nullptr);
#endif

	fprintf(stderr, "INFO: SDL3GameEngine using pre-initialized window\n");

	// Call parent init to initialize game subsystems
	GameEngine::init();
}

/**
 * From GameEngine: reset() - reset system to starting state
 */
void SDL3GameEngine::reset(void)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::reset()\n");
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}
	GameEngine::reset();
}

/**
 * From GameEngine: update() - per-frame update
 */
void SDL3GameEngine::update(void)
{
#if defined(__ANDROID__)
	{
		// GeneralsX @android r011: mobile render/visibility settings, forced once after
		// the INI load and before any map is built (the device GameData.ini shipped
		// DrawEntireTerrain=Yes / TerrainLOD=DISABLE / MaxCameraHeight=700 and no shroud).
		static bool s_mobileGlobalsDone = false;
		if (!s_mobileGlobalsDone && TheWritableGlobalData) {
			s_mobileGlobalsDone = true;
			TheWritableGlobalData->m_drawEntireTerrain = FALSE;
			TheWritableGlobalData->m_terrainLOD = TERRAIN_LOD_AUTOMATIC;
			if (TheWritableGlobalData->m_maxCameraHeight > 380.0f) {
				TheWritableGlobalData->m_maxCameraHeight = 380.0f;
			}
			TheWritableGlobalData->m_shroudOn = TRUE;   // fog of war back on
		}
	}
#endif
	pollSDL3Events();
#if GX_TOUCH_UI
	// Pause sim + render while backgrounded OR inactive (see iosLifecycleWatcher).
	// Acquiring a Metal drawable in these windows fights iOS for the layer and,
	// across repeated suspend/switcher cycles, crashes MoltenVK. Keep polling so
	// we still catch the resume events; just don't touch the GPU.
	if (iosShouldPauseRendering()) {
		SDL_Delay(50);
		return;
	}
#endif
	GameEngine::update();
}

/**
 * From GameEngine: execute() - main game loop
 */
void SDL3GameEngine::execute(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - entering main loop\n");
	GameEngine::execute();
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - exited main loop\n");
}

/**
 * From GameEngine: serviceWindowsOS() - native OS service
 * On Linux, process SDL3 events
 */
void SDL3GameEngine::serviceWindowsOS(void)
{
	pollSDL3Events();
}

/**
 * Check if game has OS focus
 */
Bool SDL3GameEngine::isActive(void)
{
	return m_IsActive;
}

/**
 * Set OS focus status
 */
void SDL3GameEngine::setIsActive(Bool isActive)
{
	m_IsActive = isActive;
}

/**
 * Poll and process SDL3 events
 * Handles keyboard, mouse, window, and quit events
 */
void SDL3GameEngine::pollSDL3Events(void)
{
	if (!m_SDLWindow) {
		return;
	}

	updateTextInputState();

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				m_IsActive = false;
				if (m_IsTextInputActive) {
					SDL_StopTextInput(m_SDLWindow);
					m_IsTextInputActive = false;
					m_TextInputFocusWindow = nullptr;
				}
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

#if GX_TOUCH_UI
			// App suspension/resume: mirror the desktop focus handling so audio
			// and mouse state pause cleanly (the render gate lives in update()).
			case SDL_EVENT_DID_ENTER_BACKGROUND:
				m_IsActive = false;
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

			case SDL_EVENT_DID_ENTER_FOREGROUND:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;
#endif

			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				if (TheMouse) {
					TheMouse->onCursorMovedInside();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				if (TheMouse) {
					TheMouse->onCursorMovedOutside();
				}
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				// Fighter19 pattern: direct addSDLEvent() call
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheKeyboard) {
					SDL3Keyboard* keyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
					if (keyboard) {
						keyboard->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_TEXT_INPUT:
				forwardTextInputEvent(event.text.text);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:
#if GX_TOUCH_UI
				// Belt-and-braces: drop SDL's own touch-synthesized mouse events.
				// The gesture translator owns all touch->mouse conversion; double
				// delivery would produce phantom second clicks.
				if (event.motion.which == SDL_TOUCH_MOUSEID) {
					break;
				}
#endif
				// Fighter19 pattern: direct addSDLEvent() call with raw SDL_Event
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheMouse) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						mouse->addSDLEvent(&event);
					}
				}
				break;

#if GX_TOUCH_UI
			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:
				if (TheMouse && m_SDLWindow) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						handleTouchEvent(mouse, m_SDLWindow, event);
					}
				}
				break;
#endif

			case SDL_EVENT_WINDOW_RESIZED:
				handleWindowEvent(event.window);
				break;

			default:
				// Ignore other events for now
				break;
		}

		updateTextInputState();
	}

#if GX_TOUCH_UI
	// Poll the long-press timer every frame; a stationary finger emits no events.
	if (TheMouse && m_SDLWindow) {
		SDL3Mouse* touchMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (touchMouse) {
			updateTouchLongPress(touchMouse, m_SDLWindow);
		}
	}
#endif
#if defined(__ANDROID__)
	{
		// GeneralsX @android r010: the engine is one heavy thread (~87% of a core).
		// Pin it to the fastest cores (2x Cortex-A76 on SD730G) and raise its
		// priority so the scheduler never parks it on a little core. Done once,
		// after start-up, so DXVK/audio worker threads keep their own masks.
		static int s_pinFrames = 0;
		if (s_pinFrames >= 0 && ++s_pinFrames > 120) {
			s_pinFrames = -1;
			const int ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
			long freq[32] = {0};
			long maxFreq = 0;
			for (int c = 0; c < ncpu && c < 32; ++c) {
				char path[96];
				snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
				if (FILE *fp = fopen(path, "r")) {
					if (fscanf(fp, "%ld", &freq[c]) != 1) freq[c] = 0;
					fclose(fp);
				}
				if (freq[c] > maxFreq) maxFreq = freq[c];
			}
			if (maxFreq > 0) {
				cpu_set_t set;
				CPU_ZERO(&set);
				int picked = 0;
				for (int c = 0; c < ncpu && c < 32; ++c) {
					if (freq[c] >= maxFreq * 9 / 10) { CPU_SET(c, &set); ++picked; }
				}
				const int rc = (picked > 0 && picked < ncpu) ? sched_setaffinity(0, sizeof(set), &set) : -2;
				fprintf(stderr, "INFO: r010 main thread -> %d big core(s) @%ld kHz (rc=%d)\n", picked, maxFreq, rc);
			}
			const int prc = setpriority(PRIO_PROCESS, (id_t)gettid(), -8);
			fprintf(stderr, "INFO: r010 main thread priority -8 (rc=%d)\n", prc);
		}
	}
#endif
}

// GeneralsX @bugfix felipebraz 01/04/2026 Enable SDL text input only while an entry gadget owns focus.
void SDL3GameEngine::updateTextInputState(void)
{
	if (!m_SDLWindow || !TheWindowManager) {
		return;
	}

	GameWindow* focusedWindow = TheWindowManager->winGetFocus();
	const Bool wantsTextInput =
		focusedWindow != nullptr && BitIsSet(focusedWindow->winGetStyle(), GWS_ENTRY_FIELD);

	if (wantsTextInput) {
		if (!m_IsTextInputActive) {
			if (SDL_StartTextInput(m_SDLWindow)) {
				m_IsTextInputActive = true;
			}
		}
		m_TextInputFocusWindow = focusedWindow;
	} else {
		if (m_IsTextInputActive) {
			SDL_StopTextInput(m_SDLWindow);
			m_IsTextInputActive = false;
		}
		m_TextInputFocusWindow = nullptr;
	}
}

// GeneralsX @bugfix felipebraz 01/04/2026 Forward SDL UTF-8 text input through existing GWM_IME_CHAR path.
void SDL3GameEngine::forwardTextInputEvent(const char* utf8Text)
{
	if (!utf8Text || !TheWindowManager) {
		return;
	}

	// GeneralsX @bugfix felipebraz 01/04/2026 Use tracked text-input focus window to keep SDL text delivery stable.
	GameWindow* targetWindow = m_TextInputFocusWindow;
	if (!targetWindow || !BitIsSet(targetWindow->winGetStyle(), GWS_ENTRY_FIELD)) {
		return;
	}

	const size_t textLength = strlen(utf8Text);
	size_t offset = 0;
	while (offset < textLength) {
		UnsignedInt codepoint = 0;
		if (!DecodeNextUtf8Codepoint(utf8Text, textLength, offset, codepoint)) {
			continue;
		}

		// GeneralsX @bugfix felipebraz 01/04/2026 Clamp IME char forwarding to BMP and reject UTF-16 surrogate range.
		if (codepoint == 0 || codepoint > 0x10FFFFU) {
			continue;
		}

		if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
			continue;
		}

		if (codepoint > 0xFFFFU) {
			continue;
		}

		const WideChar wideCharacter = static_cast<WideChar>(codepoint);
		TheWindowManager->winSendInputMsg(targetWindow, GWM_IME_CHAR, static_cast<WindowMsgData>(wideCharacter), 0);
	}
}

/**
 * Handle keyboard event -dispatch to Keyboard manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleKeyboardEvent(const SDL_KeyboardEvent& event)
{
	// Dispatch to SDL3Keyboard if available
	if (TheKeyboard) {
		SDL3Keyboard* sdlKeyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
		if (sdlKeyboard) {
			sdlKeyboard->addSDL3KeyEvent(event);
		}
	}
}

/**
 * Handle mouse motion event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseMotionEvent(const SDL_MouseMotionEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseMotionEvent(event);
		}
	}
}

/**
 * Handle mouse button event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseButtonEvent(const SDL_MouseButtonEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseButtonEvent(event);
		}
	}
}

/**
 * Handle mouse wheel event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseWheelEvent(const SDL_MouseWheelEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseWheelEvent(event);
		}
	}
}

/**
 * Handle window event (resize, etc.)
 */
void SDL3GameEngine::handleWindowEvent(const SDL_WindowEvent& event)
{
	// TODO: Phase 2 - Handle window resize, notify graphics subsystem
	// fprintf(stderr, "DEBUG: Window event (type=%d)\n", event.type);
}

/**
 * Factory Methods for GameEngine subsystems
 * TheSuperHackers @build felipebraz 13/02/2026
 * Implementations in .cpp to provide complete type definitions and avoid circular includes
 */

LocalFileSystem *SDL3GameEngine::createLocalFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createLocalFileSystem() -> StdLocalFileSystem\n");
	return NEW StdLocalFileSystem;
}

ArchiveFileSystem *SDL3GameEngine::createArchiveFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createArchiveFileSystem() -> StdBIGFileSystem\n");
	return NEW StdBIGFileSystem;
}

GameLogic *SDL3GameEngine::createGameLogic(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameLogic() -> W3DGameLogic\n");
	return NEW W3DGameLogic;
}

GameClient *SDL3GameEngine::createGameClient(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameClient() -> W3DGameClient\n");
	return NEW W3DGameClient;
}

ModuleFactory *SDL3GameEngine::createModuleFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createModuleFactory() -> W3DModuleFactory\n");
	return NEW W3DModuleFactory;
}

ThingFactory *SDL3GameEngine::createThingFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createThingFactory() -> W3DThingFactory\n");
	return NEW W3DThingFactory;
}

FunctionLexicon *SDL3GameEngine::createFunctionLexicon(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createFunctionLexicon() -> W3DFunctionLexicon\n");
	return NEW W3DFunctionLexicon;
}

// GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
Radar *SDL3GameEngine::createRadar(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy radar.
	// Upstream reference: Win32GameEngine headless factory behavior, TheSuperHackers/GeneralsGameCode
	// https://github.com/TheSuperHackers/GeneralsGameCode
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> RadarDummy (headless)\n");
		return NEW RadarDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> W3DRadar\n");
	return NEW W3DRadar;
}

// GeneralsX @bugfix Copilot 24/03/2026 Match upstream GameEngine pure-virtual signature after sync.
ParticleSystemManager* SDL3GameEngine::createParticleSystemManager(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy particle manager.
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> ParticleSystemManagerDummy (headless)\n");
		return NEW ParticleSystemManagerDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> W3DParticleSystemManager\n");
	return NEW W3DParticleSystemManager;
}

WebBrowser *SDL3GameEngine::createWebBrowser(void)
{
	// WebBrowser uses Windows COM (CComObject<W3DWebBrowser>)
	// Not available on Linux - return nullptr
	fprintf(stderr, "WARNING: WebBrowser not available on Linux platform\n");
	return nullptr;
}

/**
 * Factory method: AudioManager
 * Select audio backend based on compile flags
 * GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
 */
AudioManager *SDL3GameEngine::createAudioManager(Bool dummy)
{
	(void)dummy;
	fprintf(stderr, "INFO: SDL3GameEngine::createAudioManager()\n");

#ifdef SAGE_USE_OPENAL
	fprintf(stderr, "INFO: Creating OpenAL audio backend\n");
	return new OpenALAudioManager();
#else
	fprintf(stderr, "INFO: Audio backend not available (SAGE_USE_OPENAL not defined)\n");
	fprintf(stderr, "WARNING: Falls back to parent implementation or silent mode\n");
	return GameEngine::createAudioManager();  // Call parent (may return stub)
#endif
}

#endif // !_WIN32

