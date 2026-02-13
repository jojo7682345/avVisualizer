#ifndef __DEFINES__
#define __DEFINES__

#include <AvUtils/avTypes.h>

#ifdef AV_EXPORT
// Exports
#ifdef _MSC_VER
#define AV_API __declspec(dllexport)
#else
#define AV_API __attribute__((visibility("default")))
#endif
#else
// Imports
#ifdef _MSC_VER
/** @brief Import/export qualifier */
#define AV_API __declspec(dllimport)
#else
/** @brief Import/export qualifier */
#define AV_API
#endif
#endif


#endif