#pragma once
//#include "core/platform.h"

#define loadPlugin(name, handle) {\
	extern const unsigned char _binary_plugins_##name##_bin_lib##name##_so_start[];\
	extern const unsigned char _binary_plugins_##name##_bin_lib##name##_so_end[];\
	handle = platformLoadPlugin(#name, _binary_plugins_##name##_bin_lib##name##_so_start, _binary_plugins_##name##_bin_lib##name##_so_end - _binary_plugins_##name##_bin_lib##name##_so_start);\
}
