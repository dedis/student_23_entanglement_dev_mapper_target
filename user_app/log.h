/*
 *  Copyright The Shufflecake Project Authors (2022)
 *  Copyright The Shufflecake Project Contributors (2022)
 *  Copyright Contributors to the The Shufflecake Project.
 *
 *  See the AUTHORS file at the top-level directory of this distribution and at
 *  <https://www.shufflecake.net/permalinks/shufflecake-userland/AUTHORS>
 *
 *  This file is part of the program shufflecake-c, which is part of the
 *  Shufflecake Project. Shufflecake is a plausible deniability (hidden storage)
 *  layer for Linux. See <https://www.shufflecake.net>.
 *
 *  This program is free software: you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 2 of the License, or (at your option)
 *  any later version. This program is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 *  Public License for more details. You should have received a copy of the
 *  GNU General Public License along with this program.
 *  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _UTILS_LOG_H_
#define _UTILS_LOG_H_


/*****************************************************
 *                  INCLUDE SECTION                  *
 *****************************************************/

#include <stdio.h>
#include <string.h>

/*****************************************************
 *                    CONSTANTS                      *
 *****************************************************/

// Printf colours (regular text)
#define ENT_LOG_BLK "\033[0;30m"
#define ENT_LOG_RED "\033[0;31m"
#define ENT_LOG_GRN "\033[0;32m"
#define ENT_LOG_YEL "\033[0;33m"
#define ENT_LOG_BLU "\033[0;34m"
#define ENT_LOG_MAG "\033[0;35m"
#define ENT_LOG_CYN "\033[0;36m"
#define ENT_LOG_WHT "\033[0;37m"
// Printf colours (bold text)
#define ENT_LOG_BBLK "\033[1;30m"
#define ENT_LOG_BRED "\033[1;31m"
#define ENT_LOG_BGRN "\033[1;32m"
#define ENT_LOG_BYEL "\033[1;33m"
#define ENT_LOG_BBLU "\033[1;34m"
#define ENT_LOG_BMAG "\033[1;35m"
#define ENT_LOG_BCYN "\033[1;36m"
#define ENT_LOG_BWHT "\033[1;37m"
// Reset colour
#define ENT_LOG_RESET "\033[0m"

// Log level: debug implies detailed logs
#ifdef CONFIG_ENT_LOG_DEBUG
#define CONFIG_ENT_LOG_DETAILED
#endif


/*****************************************************
 *                       MACROS                      *
 *****************************************************/

// Gives the point in the code where it was called
#define ent_log_detailed(col, ...)	do{					\
	printf(ENT_LOG_GRN "FUNC " ENT_LOG_RESET "%s() "	\
           ENT_LOG_GRN "FILE " ENT_LOG_RESET "%s "    \
           ENT_LOG_GRN "LINE " ENT_LOG_RESET "%d | ", \
           __func__, __FILE__, __LINE__);				\
    ent_log_concise(col, __VA_ARGS__);					\
}while(0)

// Only writes using the given colour
#define ent_log_concise(col, ...)	do{	\
	printf(col);						\
	printf(__VA_ARGS__);				\
	printf(ENT_LOG_RESET "\n");		\
}while(0)

// Maps to one or the other, based on a Makefile switch
#ifdef CONFIG_ENT_LOG_DETAILED
	#define ent_log_colour(...) ent_log_detailed(__VA_ARGS__)
#else
	#define ent_log_colour(...) ent_log_concise(__VA_ARGS__)
#endif

// Using specific colours
#define ent_log_green(...) ent_log_colour(ENT_LOG_GRN, __VA_ARGS__)
#define ent_log_red(...) ent_log_colour(ENT_LOG_RED, __VA_ARGS__)
#define ent_log_yellow(...) ent_log_colour(ENT_LOG_YEL, __VA_ARGS__)
#define ent_log_blue(...) ent_log_colour(ENT_LOG_BLU, __VA_ARGS__)
#define ent_log_normal(...) ent_log_colour(ENT_LOG_RESET, __VA_ARGS__)

// With log levels
#define ent_log_error(...)	ent_log_colour(ENT_LOG_RED, "[ERROR] " __VA_ARGS__)
#define ent_log_warn(...)	ent_log_colour(ENT_LOG_MAG, "[WARN] " __VA_ARGS__)
#ifdef CONFIG_ENT_LOG_DEBUG
	#define ent_log_debug(...) ent_log_colour(ENT_LOG_CYN, "[DEBUG] " __VA_ARGS__)
#else
	#define ent_log_debug(...)
#endif


/*****************************************************
 *                  INLINE FUNCTIONS                 *
 *****************************************************/

// Log a hex string
static inline void ent_log_hex(char *str, size_t len)
{
	int i;
	unsigned char *s = (unsigned char *) str;

	for (i = 0; i < len; i++) {
		printf("%02x ", s[i]);
		// Nice aligned wrapping
		if (i % 16 == 15) {
			printf("\n");
		}
	}

	// Always end with a newline
	if (i % 16 != 0) {
		printf("\n");
	}

	return;
}

#endif /* _UTILS_LOG_H_ */
