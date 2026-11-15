// Native mouse aiming for Splatoon 3 (TOTK_MOUSE_NATIVE, mouse_native.c, docs/controls.md "Mouse as the gyro"): while
// the mouse's virtual six-axis sensor is the one the game reads, the game's own gyro smoothing is switched off so each
// frame's mouse counts reach the aim whole in the frame that reads them, and the pitch turns by the yaw's degrees per
// count over its whole range. A real gamepad's gyro keeps the game's smoothing and pitch curve.
#pragma once

// ov_input.c six_fill, on every read of the connected controller's sensor: mouse is 1 when the states handed out are
// the mouse's. Switches the game's smoothing off or back as the source changes or the setting does.
void mouse_native_read(int mouse);
// 1 while the smoothing is off for the mouse (the last read handed out the mouse's states with the setting on).
int mouse_native_active(void);
// The last frame of the linear pitch, for the bench (mgyro_bench.c): the curve's own result, the pitch handed out,
// the pose's view pitch for the camera's pitch before the frame and the one wanted, the final view pitch aimed at
// (degrees, up positive), the yaw gain used (camera degrees per controller degree), the slope of the final view's
// offset from the pose's that the frame used, and 1 when the frame took the curve's result as it is.
typedef struct { float game, out, view, want, target, gain, slope; int resync; } MouseNativePitch;
MouseNativePitch mouse_native_pitch_debug(void);
