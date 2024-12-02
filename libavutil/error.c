/*
 * FFmpeg Error Handling
 * Enhanced for Windows 11 compatibility and thread safety
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#undef _GNU_SOURCE
#define _XOPEN_SOURCE 600 /* XSI-compliant version of strerror_r */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "avstring.h"
#include "error.h"
#include "macros.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Define thread-local storage specifier based on the compiler
#if defined(_WIN32)
    #define THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
    #define THREAD_LOCAL __thread
#else
    #define THREAD_LOCAL
    #warning "Thread-local storage not supported on this platform. av_err2str may not be thread-safe."
#endif

// Define the maximum string size for error messages
#define AV_ERROR_MAX_STRING_SIZE 64

struct error_entry {
    int num;
    const char *tag;
    const char *str;
};

#define ERROR_TAG(tag) AVERROR_##tag, #tag
#define EERROR_TAG(tag) AVERROR(tag), #tag

static const struct error_entry error_entries[] = {
    { ERROR_TAG(BSF_NOT_FOUND),      "Bitstream filter not found"                     },
    { ERROR_TAG(BUG),                "Internal bug, should not have happened"         },
    { ERROR_TAG(BUG2),               "Internal bug, should not have happened"         },
    { ERROR_TAG(BUFFER_TOO_SMALL),   "Buffer too small"                               },
    { ERROR_TAG(DECODER_NOT_FOUND),  "Decoder not found"                              },
    { ERROR_TAG(DEMUXER_NOT_FOUND),  "Demuxer not found"                              },
    { ERROR_TAG(ENCODER_NOT_FOUND),  "Encoder not found"                              },
    { ERROR_TAG(EOF),                "End of file"                                    },
    { ERROR_TAG(EXIT),               "Immediate exit requested"                       },
    { ERROR_TAG(EXTERNAL),           "Generic error in an external library"           },
    { ERROR_TAG(FILTER_NOT_FOUND),   "Filter not found"                               },
    { ERROR_TAG(INPUT_CHANGED),      "Input changed"                                  },
    { ERROR_TAG(INVALIDDATA),        "Invalid data found when processing input"       },
    { ERROR_TAG(MUXER_NOT_FOUND),    "Muxer not found"                                },
    { ERROR_TAG(OPTION_NOT_FOUND),   "Option not found"                               },
    { ERROR_TAG(OUTPUT_CHANGED),     "Output changed"                                 },
    { ERROR_TAG(PATCHWELCOME),       "Not yet implemented in FFmpeg, patches welcome" },
    { ERROR_TAG(PROTOCOL_NOT_FOUND), "Protocol not found"                             },
    { ERROR_TAG(STREAM_NOT_FOUND),   "Stream not found"                               },
    { ERROR_TAG(UNKNOWN),            "Unknown error occurred"                         },
    { ERROR_TAG(EXPERIMENTAL),       "Experimental feature"                           },
    { ERROR_TAG(INPUT_AND_OUTPUT_CHANGED), "Input and output changed"                 },
    { ERROR_TAG(HTTP_BAD_REQUEST),   "Server returned 400 Bad Request"                },
    { ERROR_TAG(HTTP_UNAUTHORIZED),  "Server returned 401 Unauthorized (authorization failed)" },
    { ERROR_TAG(HTTP_FORBIDDEN),     "Server returned 403 Forbidden (access denied)"  },
    { ERROR_TAG(HTTP_NOT_FOUND),     "Server returned 404 Not Found"                  },
    { ERROR_TAG(HTTP_TOO_MANY_REQUESTS), "Server returned 429 Too Many Requests"      },
    { ERROR_TAG(HTTP_OTHER_4XX),     "Server returned 4XX Client Error, but not one of 40{0,1,3,4}" },
    { ERROR_TAG(HTTP_SERVER_ERROR),  "Server returned 5XX Server Error reply"         },
    // Additional standard error codes
    { AVERROR(EINVAL),               "Invalid argument"                               },
    { AVERROR(ENOMEM),               "Cannot allocate memory"                         },
    { AVERROR(EIO),                  "I/O error"                                      },
    { AVERROR(ENOENT),               "No such file or directory"                      },
    { AVERROR(ESPIPE),               "Illegal seek"                                   },
    // Add more as needed
};

// Thread-local buffer to store error messages
static THREAD_LOCAL char av_error_buf[AV_ERROR_MAX_STRING_SIZE];

/**
 * @brief Convert a negative error code into a human-readable string.
 *        This function is thread-safe.
 *
 * @param errnum Error code to be converted.
 * @return const char* Pointer to a thread-local string containing the error message.
 */
const char *av_err2str(int errnum)
{
    av_strerror(errnum, av_error_buf, sizeof(av_error_buf));
    return av_error_buf;
}

/**
 * @brief Convert a negative error code into a human-readable string.
 *        This function is thread-safe.
 *
 * @param errnum Error code to be converted.
 * @param errbuf Buffer to store the error string.
 * @param errbuf_size Size of the buffer.
 * @return int 0 on success, negative value on failure.
 */
int av_strerror(int errnum, char *errbuf, size_t errbuf_size)
{
    int ret = 0;
    const char *errstr = NULL;

    if (errbuf_size > 0) {
        errbuf[0] = '\0'; // Ensure the buffer is null-terminated
    }

    // Check if the error code matches one of the custom FFmpeg errors
    for (size_t i = 0; i < FF_ARRAY_ELEMS(error_entries); i++) {
        if (errnum == error_entries[i].num) {
            errstr = error_entries[i].str;
            break;
        }
    }

    if (errstr) {
        av_strlcpy(errbuf, errstr, errbuf_size);
    } else {
        // Handle standard system errors
#ifdef _WIN32
        // Use FormatMessage on Windows
        DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD len = FormatMessageA(flags, NULL, -errnum, 0, errbuf, (DWORD)errbuf_size, NULL);
        if (len == 0) {
            // FormatMessage failed
            snprintf(errbuf, errbuf_size, "Unknown error code: %d", errnum);
            ret = AVERROR_UNKNOWN;
        }
#else
        // Use strerror_r on POSIX systems
        ret = strerror_r(-errnum, errbuf, errbuf_size);
        if (ret != 0) {
            snprintf(errbuf, errbuf_size, "Unknown error code: %d", errnum);
            ret = AVERROR_UNKNOWN;
        }
#endif
    }

    return ret;
}

#undef THREAD_LOCAL
