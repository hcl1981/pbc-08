#pragma once

// PBC+ port: inline, so the synth (running from RAM) does not call into flash

//std::clamp is in c++17, but not c++11
inline int clamp (int val, int lo, int hi) {
	val = (val > hi) ? hi : val;
    return (val < lo) ? lo : val;
}

inline float clamp (float val, float lo, float hi) {
	val = (val > hi) ? hi : val;
    return (val < lo) ? lo : val;
}

inline float lerp (float a, float b, float t) {
	return (b - a) * t + a;
}
