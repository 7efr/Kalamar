// Native mouse aiming for Splatoon 3 (mouse_native.h). The mouse keeps turning the virtual six-axis controller of
// mouse_gyro.c (input.c input_mouse_gyro_read); while that controller is the sensor the game reads and
// TOTK_MOUSE_NATIVE is on, the three stages of the game's gyro processing that smooth a real controller's motion are
// switched off, so the camera takes every frame's counts whole in the frame that reads them, both axes, and the pitch
// turns by the yaw's own degrees per count over its whole range instead of along the game's pitch curve (4, below).
// A gamepad's gyro keeps all of them. Everything else stays the game's: its sensitivity, pitch limits, collisions, the
// camera modes, and what it sends to the other players.
//
// 1. The attitude filter (measured with TOTK_MGYRO_BENCH: an 8 degree pitch step of the controller moved the aim by
//    3%, 5% and 7% of its turn in the first three reads and 89% after nine). Once a frame the game's gyro processor
//    (main+0x2afaf6c, its work in main+0x2afb620) copies the new six-axis states into a ring and hands the cameras an
//    angular velocity and a direction matrix. The angular velocity is the newest state's; the matrix is a filtered
//    attitude: a state about two samples behind the newest picked by how well it continues the previous output, then
//    blended into the previous output carried forward by its angular velocity, with a weight that drops to 0.2 when
//    the picked state is far from that prediction and recovers by 0.2 a frame. Yaw is the angular velocity's part
//    about the matrix's up axis, so it was whole at once; pitch comes from the matrix itself. Two bytes of the game's
//    gyro tuning data choose the newest state's angular velocity and matrix instead (the first is set by default);
//    the gyro processor reads them every frame. While the mouse is the sensor the matrix byte is set, and the game's
//    value is put back when it is not.
// 2. The camera's yaw damping (the reversal easing of mouse_gyro.h mgyro_frame_yaw). The player camera
//    (main+0x28f4e20) multiplies each frame's yaw turn by a gain that drops, to 0.1 at least, when the turn follows
//    motion in the other direction and recovers over about ten frames, and keeps the part it cut to give back later.
//    The multiplication is replaced by a host call (recomp/patches-splatoon3-v1130.txt, set mouse_native): while the
//    mouse is the sensor the turn is taken whole and nothing is kept back; otherwise it multiplies as the game does.
// 3. The camera's pitch follow. The same camera maps the controller's pitch (plus the stick's) through its pitch curve
//    and limits to a target, then moves its pitch (the float at +0x16c, -1 down to 1 up) toward that target by a share
//    of the distance each frame: 0.3 plus or minus 0.08 times the motion sensitivity, less near the vertical, times a
//    gain that drops after motion the other way, as the yaw's (measured with the first stage off: 28% a frame, so a
//    step took seven frames to reach 90%). The share is a product the camera computes just before the move;
//    a second host call takes its place and makes it 1 while the mouse is the sensor, so the pitch is the target.
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "mouse_native.h"
#include "platform/platform.h"
#include "splat3_addr.h"

#if __has_include("patches.h")
#include "patches.h"
#endif

static volatile int32_t active;   // mouse_native_active: written on the game's six-axis reads, read by the hooks
static int supported = -1;    // the running build has the switch where its row says (checked once)
static volatile uint8_t *attitude_switch;
static uint8_t game_value;    // the switch's value before mouse_native set it
static float *pose_out;       // guest-visible scratch for the linear pitch: the camera pose's position and target

// The switch's address, once: the row's tuning data plus the byte offset of the gyro processor's load, which must be
// an ldrb, so a build whose code differs from the row is never written blind.
static int setup(void) {
    if (supported >= 0) return supported;
    supported = 0;
    const Splat3Addr *s3 = splat3_addr();
    if (!s3 || !s3->gyro_tuning || !s3->gyro_attitude_load || !ldr_count()) return 0;
    uint64_t base = ldr_module(0)->base, load = base + s3->gyro_attitude_load;
    uint32_t w = mem_guest_readable(load, 4) ? *(const uint32_t *)(uintptr_t)load : 0;
    if ((w & 0xffc00000u) != 0x39400000u) {   // ldrb wt, [xn, #imm]
        rt_log("mouse native: main does not have the expected gyro processor at main+%#llx; off",
               (unsigned long long)s3->gyro_attitude_load);
        return 0;
    }
    uint64_t a = base + s3->gyro_tuning + ((w >> 10) & 0xfff);
    if (!mem_guest_readable(a, 1)) return 0;
    attitude_switch = (volatile uint8_t *)(uintptr_t)a;
#ifdef RT_PATCH_PAGE
    if (s3->pitch_curve && s3->camera_pose) pose_out = mem_kernel_alloc(32, 16);   // linear pitch (below)
#endif
    supported = 1;
    return 1;
}

static int setting(void) {
    static unsigned seen = ~0u;
    static int on;
    unsigned v = cfg_version();
    if (v != seen) seen = v, on = cfg_bool("TOTK_MOUSE_NATIVE");
    return on;
}

void mouse_native_read(int mouse) {
    int want = mouse && setting() && setup();
    if (want == __atomic_load_n(&active, __ATOMIC_RELAXED)) return;
    if (want) {
        game_value = *attitude_switch;
        *attitude_switch = 1;
    } else {
        *attitude_switch = game_value;
    }
    __atomic_store_n(&active, want, __ATOMIC_RELAXED);
    rt_log("mouse native: %s", want ? "on: the mouse's turns reach the aim unsmoothed"
                                    : "off: the game's gyro smoothing is back");
}

int mouse_native_active(void) { return __atomic_load_n(&active, __ATOMIC_RELAXED); }

#ifdef RT_PATCH_PAGE
#define HOST_MOUSE_NATIVE_YAW (RT_PATCH_PAGE + 0xf60)
#define HOST_MOUSE_NATIVE_PITCH (RT_PATCH_PAGE + 0xf50)
#define CAMERA_KEPT_YAW 0x1770

static float sreg(const Cpu *c, int r) {
    uint32_t bits = (uint32_t)c->v[r];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void set_sreg(Cpu *c, int r, float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    c->v[r] = bits;   // a 32-bit write clears the rest of the vector register
}

// In place of the camera's `fmul s11, s12, s0` (main+0x28f6984): s12 the frame's yaw turn after the game's
// sensitivity, s0 the damping gain, s11 the turn the camera goes on with; x19 the camera, whose float at +0x1770 holds
// the part of earlier turns the damping kept back to give later.
static void yaw_turn(Cpu *c) {
    float turn = sreg(c, 12), out = turn * sreg(c, 0);
    if (mouse_native_active()) {
        out = turn;
        float zero = 0;
        uint64_t kept = c->x[19] + CAMERA_KEPT_YAW;
        if (mem_guest_readable(kept, sizeof zero)) memcpy((void *)(uintptr_t)kept, &zero, sizeof zero);
    }
    set_sreg(c, 11, out);
}

// In place of the camera's `fmul s1, s15, s2` (main+0x28f9ae0): the share of the distance to its pitch target the
// camera moves this frame, which the next instructions apply (pitch += (target - pitch) * s1).
static void pitch_share(Cpu *c) { set_sreg(c, 1, mouse_native_active() ? 1.0f : sreg(c, 15) * sreg(c, 2)); }

// ---- 4. Linear pitch. With the three stages off the pitch still comes from the controller's tilt through the game's
// own maps: the camera's pitch curve takes the tilt plus the stick's offset, in degrees, to a pitch from -1 (its
// lowest) to 1 (its highest), against a base that the Y recentre and pushes past a limit move; its camera pose places
// the camera and its target for that pitch; and the camera's later placement raises or lowers the target, part of it
// with a delay (measured: the target's height went on rising for three frames after the pitch turned back), so the
// view drawn is a little steeper or flatter than the pose's (measured with the curve: 0.098 degrees of view per count
// near level, falling to 0 at the top limit, up to 0.112 below level, while yaw is 0.0957 everywhere).
// While the mouse is the sensor the pitch moves instead by the yaw's own degrees per degree of controller turn, in
// degrees of the view drawn. Each frame the change of the curve's input since the previous frame (the mouse's tilt
// and the stick) is turned into degrees at the yaw's gain and added to the view pitch aimed at. The pose's view pitch
// to reach is the current one plus the distance from the view drawn last frame to that aim, divided by how much the
// view drawn moved per degree of the pose's in the previous frame, and the pitch whose pose has it replaces the
// curve's result. What the later placement adds is so taken back the next frame, and the view drawn ends where the
// counts put it, also while the later placement goes on settling after the mouse stopped (measured below 40 degrees
// down: up to 0.03 degrees a frame, for more than 20 frames). The aim stays within AIM_SPAN of the view
// drawn, is dropped when the view did not follow the pose, becomes the view drawn while the pitch rests against a
// limit, and once the frames since the curve's input last changed have moved the pose by IDLE_SPAN in all, so a move
// of the game's own camera is not fought for more than that and the pitch cannot wander while the mouse rests. Both
// maps are the game's functions, called for the camera's current parameters;
// nothing of them is copied here. The pitch stops at the pose's own ends (the curve's -1 and 1) and motion past them
// is dropped, so a turn back moves the view at once; the base is left to the game. When the game moves the pitch
// itself (the Y recentre or a reset moving the base, a write to the pitch, a jump of the curve's input), the frame
// takes the curve's result and its excess as the game computed them and the next one aims from the view drawn.
// Without a measured yaw gain yet, or with the setting off, the curve's result is handed out as it is.
#define HOST_MOUSE_NATIVE_CURVE (RT_PATCH_PAGE + 0xf40)
#define HOST_MOUSE_NATIVE_GAIN (RT_PATCH_PAGE + 0xf30)
#define CAMERA_PITCH 0x16c       // the camera's pitch, -1 to 1
#define CAMERA_TURN_SCALE 0x64   // the camera update's stack slot with the scale of this frame's yaw turn

#define CAMERA_VIEW 0x54         // the camera's final view direction, from its target toward its position
#define AIM_SPAN 1.0             // degrees the view pitch aimed at may be away from the view drawn
#define IDLE_SPAN 2.0            // degrees of the pose's view pitch the aim may move it by while the mouse rests
static volatile float yaw_gain;   // degrees of camera yaw per degree of controller yaw this frame; 0: not seen yet
// The previous frame's pitch: camera, pitch handed out, curve input and bases; the view pitch aimed at (when aimed),
// the view pitch drawn for the frame before and the pose's view pitch of that frame's pitch; how far the aim moved the
// pose since the curve's input last changed
static struct {
    uint64_t cam;
    float out, input, base1, base2;
    double target, seen, pose, idle;
    int valid, aimed;
} pz;

// The view pitch in degrees (up positive) the camera drew in its last frame, from its final view direction.
static float final_view_pitch(uint64_t cam) {
    float v[3];
    memcpy(v, (const void *)(uintptr_t)(cam + CAMERA_VIEW), sizeof v);
    double n = sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);
    return n > 0 ? (float)(-asin(fmax(-1.0, fmin(1.0, v[1] / n))) * (180.0 / M_PI)) : 0;
}
static MouseNativePitch dbg;

// In place of the camera's `fmul s12, s8, s9` (main+0x28f66f8): s8 the yaw gain of the motion sensitivity, s9 the
// frame's yaw rate, s12 the turn. The gyro processor scales the angular velocity by pi/54, the camera turns s8 times
// the stack slot's scale radians per unit of that, and the virtual controller reports a frame's turn as an angular
// velocity over 1/60 s: a degree of controller yaw turns the camera (pi/54) (60/360) (180/pi) s8 scale = 5/9 s8 scale
// degrees (measured: 2.393 at the bench's sensitivity, the gain the bench's yaw circles fit).
static void yaw_gain_seen(Cpu *c) {
    float s8 = sreg(c, 8), scale;
    memcpy(&scale, (const void *)(uintptr_t)(c->sp + CAMERA_TURN_SCALE), sizeof scale);
    yaw_gain = s8 * scale * (5.0f / 9.0f);
    set_sreg(c, 12, s8 * sreg(c, 9));
}

// The view pitch in degrees (up positive) of the camera's pose for pitch n: the game's pose function, called with the
// camera, two vectors for its position and target, and n.
static double view_pitch(Cpu *c, uint64_t cam, float n) {
    uint64_t pose = ldr_module(0)->base + splat3_addr()->camera_pose;
    set_sreg(c, 0, n);
    rt_call(c, pose, 4, cam, (uint64_t)(uintptr_t)pose_out, (uint64_t)(uintptr_t)(pose_out + 4), (uint64_t)0);
    double dx = pose_out[4] - pose_out[0], dy = pose_out[5] - pose_out[1], dz = pose_out[6] - pose_out[2];
    return atan2(dy, sqrt(dx * dx + dz * dz)) * (180.0 / M_PI);
}

// The pitch in [-1, 1] whose view pitch is want: false position with the Illinois step, from the current pitch n0
// (view pitch p0) and the ends; the view pitch rises with the pitch.
static float pitch_for(Cpu *c, uint64_t cam, double want, float n0, double p0) {
    double plo = view_pitch(c, cam, -1.0f), phi = view_pitch(c, cam, 1.0f);
    if (want <= plo) return -1.0f;
    if (want >= phi) return 1.0f;
    double a = -1, b = 1, pa = plo, pb = phi;
    if (n0 > -1 && n0 < 1) {
        if (p0 <= want) a = n0, pa = p0;
        else b = n0, pb = p0;
    }
    int kept = 0;
    for (int i = 0; i < 30; i++) {
        double m = a + (want - pa) * (b - a) / (pb - pa);
        if (!(m > a && m < b)) m = 0.5 * (a + b);
        double pm = view_pitch(c, cam, (float)m);
        if (fabs(pm - want) < 1e-4 || b - a < 1e-6) return (float)m;
        if (pm < want) {
            a = m, pa = pm;
            if (kept == -1) pb = want + (pb - want) * 0.5;
            kept = -1;
        } else {
            b = m, pb = pm;
            if (kept == 1) pa = want + (pa - want) * 0.5;
            kept = 1;
        }
    }
    return (float)(0.5 * (a + b));
}

// In place of the camera's `bl` to its pitch curve (main+0x28f7560 and +0x28f7660): s0 the motion sensitivity, s1 the
// curve's input (tilt plus stick offset, degrees), s2 and s3 the bases, w0 and w1 flags, x2 where the excess past a
// limit goes; the result in s0. The curve is always called as the game would; x19 is the camera.
static void pitch_curve(Cpu *c) {
    Cpu saved;
    memcpy(&saved, c, sizeof saved);
    const Splat3Addr *s3 = splat3_addr();
    float input = sreg(c, 1), base1 = sreg(c, 2), base2 = sreg(c, 3);
    uint64_t cam = c->x[19], excess_at = c->x[2];
    rt_call(c, ldr_module(0)->base + s3->pitch_curve, 3, c->x[0], c->x[1], c->x[2]);
    float game = sreg(c, 0), out = game;
    int on = mouse_native_active() && yaw_gain > 0 && pose_out;
    dbg.game = game, dbg.resync = 1, dbg.gain = yaw_gain;
    if (on) {
        float now;
        memcpy(&now, (const void *)(uintptr_t)(cam + CAMERA_PITCH), sizeof now);
        int resync = !pz.valid || pz.cam != cam || fabsf(now - pz.out) > 1e-5f || base1 != pz.base1 ||
                     base2 != pz.base2 || fabsf(input - pz.input) > 30;
        if (resync) {
            pz.aimed = 0;
        } else {
            // The view drawn last frame for the current pitch, the pose's view pitch for it, and how much the offset
            // between the two moved per degree of the pose's in the previous frame, when that frame moved the pose
            // enough to tell (0 otherwise: the offset is taken as it is).
            double seen = final_view_pitch(cam), p0 = view_pitch(c, cam, now), slope = 0, step = input - pz.input;
            if (pz.aimed && fabs(p0 - pz.pose) > 0.5) {
                double s = ((seen - p0) - (pz.seen - pz.pose)) / (p0 - pz.pose);
                if (fabs(s) > 0.5) pz.aimed = 0;   // the view did not follow the pose: the game moved it
                else slope = fmax(-0.15, fmin(0.15, s));
            }
            int pinned = (now >= 1.0f && step >= 0) || (now <= -1.0f && step <= 0);   // resting against a limit
            if (step != 0) pz.idle = 0;
            if (!pz.aimed || pz.idle >= IDLE_SPAN || pinned) pz.target = seen, pz.aimed = 1;
            pz.target = fmax(seen - AIM_SPAN, fmin(seen + AIM_SPAN, pz.target)) + (double)yaw_gain * step;
            double want = p0 + (pz.target - seen) / (1 + slope);
            if (step == 0) pz.idle += fabs(want - p0);
            out = now;
            if (fabs(want - p0) > 1e-4) {
                out = pitch_for(c, cam, want, now, p0);
                if (out <= -1.0f || out >= 1.0f)   // at a limit: the motion past it is dropped
                    pz.target = seen + (view_pitch(c, cam, out) - p0) * (1 + slope);
            }
            pz.seen = seen, pz.pose = p0;
            float zero = 0;
            memcpy((void *)(uintptr_t)excess_at, &zero, sizeof zero);   // nothing past a limit: the base stays
            dbg.view = (float)p0, dbg.want = (float)want, dbg.target = (float)pz.target, dbg.slope = (float)slope;
            dbg.resync = 0;
        }
        pz.cam = cam, pz.out = out, pz.input = input, pz.base1 = base1, pz.base2 = base2, pz.valid = 1;
    } else {
        pz.valid = 0;
    }
    dbg.out = out;
    memcpy(c, &saved, sizeof *c);
    set_sreg(c, 0, out);
}

MouseNativePitch mouse_native_pitch_debug(void) { return dbg; }

__attribute__((constructor)) static void mouse_native_register(void) {
    rt_register_host(HOST_MOUSE_NATIVE_YAW, yaw_turn);
    rt_register_host(HOST_MOUSE_NATIVE_PITCH, pitch_share);
    rt_register_host(HOST_MOUSE_NATIVE_CURVE, pitch_curve);
    rt_register_host(HOST_MOUSE_NATIVE_GAIN, yaw_gain_seen);
}
#else
MouseNativePitch mouse_native_pitch_debug(void) { return (MouseNativePitch){0}; }
#endif
