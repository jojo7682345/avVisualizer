#include "logging.h"
#include <AvUtils/avString.h>
/**
 * @brief Expects expected to be equal to actual.
 */
#define expect_should_be(expected, actual)                                                              \
	if (actual != expected) {                                                                           \
		avError("--> Expected %lld, but got: %lld. File: %s:%u.", expected, actual, __FILE__, __LINE__); \
		return false;                                                                                   \
	}

/**
 * @brief Expects expected to NOT be equal to actual.
 */
#define expect_should_not_be(expected, actual)                                                                   \
	if (actual == expected) {                                                                                    \
		avError("--> Expected %u != %u, but they are equal. File: %s:%u.", expected, actual, __FILE__, __LINE__); \
		return false;                                                                                            \
	}

/**
 * @brief Expects expected to be actual given a tolerance of K_FLOAT_EPSILON.
 */
#define expect_float_to_be(expected, actual)                                                        \
	if (kabs(expected - actual) > 0.001f) {                                                         \
		avError("--> Expected %f, but got: %f. File: %s:%u.", expected, actual, __FILE__, __LINE__); \
		return false;                                                                               \
	}

/**
 * @brief Expects actual to be true.
 */
#define expect_to_be_true(actual)                                                      \
	if (actual != true) {                                                              \
		avError("--> Expected true, but got: false. File: %s:%u.", __FILE__, __LINE__); \
		return false;                                                                  \
	}

/**
 * @brief Expects actual to be false.
 */
#define expect_to_be_false(actual)                                                     \
	if (actual != false) {                                                             \
		avError("--> Expected false, but got: true. File: %s:%u.", __FILE__, __LINE__); \
		return false;                                                                  \
	}

#define expect_string_to_be(expected, actual)                                                           \
	if (!avStringEquals(AV_CSTR(expected), AV_CSTR(actual))) {                                                             \
		avError("--> Expected '%s', but got: '%s'. File: %s:%u.", expected, actual, __FILE__, __LINE__); \
		return false;                                                                                   \
	}
