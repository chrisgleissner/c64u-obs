#ifndef C64U_VERSION_H
#define C64U_VERSION_H

/**
 * @file c64u-version.h
 * @brief Version and build information for C64U OBS Plugin
 */

// Version information (will be populated by build system)
#ifndef C64U_VERSION_TAG
#define C64U_VERSION_TAG "dev"
#endif

#ifndef C64U_GIT_HASH
#define C64U_GIT_HASH "unknown"
#endif

#ifndef C64U_BUILD_TIME
#define C64U_BUILD_TIME "unknown"
#endif

// Function to get formatted version string
const char *c64u_get_version_string(void);

// Function to get build information
const char *c64u_get_build_info(void);

#endif /* C64U_VERSION_H */
