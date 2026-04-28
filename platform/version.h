/*
 * version.h — Picocalc_NESco system-wide version information
 *
 * System version is managed intentionally by the project.
 * The assistant bumps PICOCALC_NESCO_VERSION before each reported build.
 * Build id changes on every compile so hardware logs can distinguish binaries.
 */

#pragma once

#define PICOCALC_NESCO_VERSION "1.1.0"
#define PICOCALC_NESCO_BUILD_ID __DATE__ " " __TIME__
#define PICOCALC_NESCO_BANNER "PicoCalc NESco Ver. " PICOCALC_NESCO_VERSION
#define PICOCALC_NESCO_BANNER_FULL PICOCALC_NESCO_BANNER " Build " PICOCALC_NESCO_BUILD_ID
