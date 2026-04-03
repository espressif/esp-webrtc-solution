/*
 * SigV4 Library v1.3.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file sigv4_internal.h
 * @brief Internal definitions for the SigV4 Library.
 */

#ifndef SIGV4_INTERNAL_H_
#define SIGV4_INTERNAL_H_

/* *INDENT-OFF* */
#ifdef __cplusplus
    extern "C" {
#endif
/* *INDENT-ON* */

/* SIGV4_DO_NOT_USE_CUSTOM_CONFIG allows building of the SigV4 library without a
 * config file. If a config file is provided, the SIGV4_DO_NOT_USE_CUSTOM_CONFIG
 * macro must not be defined.
 */
#ifndef SIGV4_DO_NOT_USE_CUSTOM_CONFIG
    #include "sigv4_config.h"
#endif

/* Include config defaults header to get default values of configurations not
 * defined in sigv4_config.h file. */
#include "sigv4_config_defaults.h"

/* Constants for date verification. */
#define YEAR_MIN               1900L               /**< Earliest year accepted. */
#define MONTH_ASCII_LEN        3U                  /**< Length of month abbreviations. */

/**
 * @brief Month name abbreviations for RFC 5322 date parsing.
 */
#define MONTH_NAMES            { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" }

/**
 * @brief Number of days in each respective month.
 */
#define MONTH_DAYS             { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }

#define FORMAT_RFC_3339        "%4Y-%2M-%2DT%2h:%2m:%2sZ"                       /**< Format string to parse RFC 3339 date. */
#define FORMAT_RFC_3339_LEN    sizeof( FORMAT_RFC_3339 ) - 1U                   /**< Length of the RFC 3339 format string. */

#define FORMAT_RFC_5322        "%3*, %2D %3M %4Y %2h:%2m:%2s GMT"               /**< Format string to parse RFC 5322 date. */
#define FORMAT_RFC_5322_LEN    sizeof( FORMAT_RFC_5322 ) - 1U                   /**< Length of the RFC 3339 format string. */

#define ISO_YEAR_LEN           4U                                               /**< Length of year value in ISO 8601 date. */
#define ISO_NON_YEAR_LEN       2U                                               /**< Length of non-year values in ISO 8601 date. */

#define ISO_DATE_SCOPE_LEN     8U                                               /**< Length of date substring used in credential scope. */

/* SigV4 related string literals and lengths. */

/**
 * @brief The separator between each component of the credential scope.
 */
#define CREDENTIAL_SCOPE_SEPARATOR             '/'
#define CREDENTIAL_SCOPE_SEPARATOR_LEN         1U /**< The length of #CREDENTIAL_SCOPE_SEPARATOR. */

/**
 * @brief The last component that terminates the credential scope.
 */
#define CREDENTIAL_SCOPE_TERMINATOR            "aws4_request"
#define CREDENTIAL_SCOPE_TERMINATOR_LEN        ( sizeof( CREDENTIAL_SCOPE_TERMINATOR ) - 1U ) /**< The length of #CREDENTIAL_SCOPE_TERMINATOR. */

/**
 * @brief Default value when HttpParameters_t.pPath == NULL.
 */
#define HTTP_EMPTY_PATH                        "/"
#define HTTP_EMPTY_PATH_LEN                    ( sizeof( HTTP_EMPTY_PATH ) - 1U )               /**< The length of #HTTP_EMPTY_PATH. */

#define URI_ENCODED_SPECIAL_CHAR_SIZE          3U                                               /**< The size of an encoded URI special character. */
#define URI_DOUBLE_ENCODED_EQUALS_CHAR_SIZE    5U                                               /**< The size of the double-encoded "=" character. */

#define LINEFEED_CHAR                          '\n'                                             /**< A linefeed character used to build the canonical request. */
#define LINEFEED_CHAR_LEN                      1U                                               /**< The length of #LINEFEED_CHAR. */

#define HTTP_REQUEST_LINE_ENDING               "\r\n"                                           /**< The string used in non-canonicalized HTTP headers to separate header entries in HTTP request. */
#define HTTP_REQUEST_LINE_ENDING_LEN           ( sizeof( HTTP_REQUEST_LINE_ENDING ) - 1U )      /**< The length of #HTTP_REQUEST_LINE_ENDING. */

#define SPACE_CHAR                             ' '                                              /**< A linefeed character used to build the Authorization header value. */
#define SPACE_CHAR_LEN                         1U                                               /**< The length of #SPACE_CHAR. */

#define S3_SERVICE_NAME                        "s3"                                             /**< S3 is the only service where the URI must only be encoded once. */
#define S3_SERVICE_NAME_LEN                    ( sizeof( S3_SERVICE_NAME ) - 1U )               /**< The length of #S3_SERVICE_NAME. */

#define SIGV4_HMAC_SIGNING_KEY_PREFIX          "AWS4"                                           /**< HMAC signing key prefix. */
#define SIGV4_HMAC_SIGNING_KEY_PREFIX_LEN      ( sizeof( SIGV4_HMAC_SIGNING_KEY_PREFIX ) - 1U ) /**< The length of #SIGV4_HMAC_SIGNING_KEY_PREFIX. */

#define AUTH_CREDENTIAL_PREFIX                 "Credential="                                    /**< The prefix that goes before the credential value in the Authorization header value. */
#define AUTH_CREDENTIAL_PREFIX_LEN             ( sizeof( AUTH_CREDENTIAL_PREFIX ) - 1U )        /**< The length of #AUTH_CREDENTIAL_PREFIX. */
#define AUTH_SEPARATOR                         ", "                                             /**< The separator between each component in the Authorization header value. */
#define AUTH_SEPARATOR_LEN                     ( sizeof( AUTH_SEPARATOR ) - 1U )                /**< The length of #AUTH_SEPARATOR. */
#define AUTH_SIGNED_HEADERS_PREFIX             "SignedHeaders="                                 /**< The prefix that goes before the signed headers in the Authorization header value. */
#define AUTH_SIGNED_HEADERS_PREFIX_LEN         ( sizeof( AUTH_SIGNED_HEADERS_PREFIX ) - 1U )    /**< The length of #AUTH_SIGNED_HEADERS_PREFIX. */
#define AUTH_SIGNATURE_PREFIX                  "Signature="                                     /**< The prefix that goes before the signature in the Authorization header value. */
#define AUTH_SIGNATURE_PREFIX_LEN              ( sizeof( AUTH_SIGNATURE_PREFIX ) - 1U )         /**< The length of #AUTH_SIGNATURE_PREFIX. */

#define HMAC_INNER_PAD_BYTE                    ( 0x36U )                                        /**< The "ipad" byte used for generating the inner key in the HMAC calculation process. */
#define HMAC_OUTER_PAD_BYTE                    ( 0x5CU )                                        /**< The "opad" byte used for generating the outer key in the HMAC calculation process. */
#define HMAX_IPAD_XOR_OPAD_BYTE                ( 0x6AU )                                        /**< The XOR of the "ipad" and "opad" bytes to extract outer key from inner key. */

/**
 * @brief A helper macro to print insufficient memory errors.
 */
#define LOG_INSUFFICIENT_MEMORY_ERROR( purposeOfWrite, bytesExceeded )                                                                         \
    {                                                                                                                                          \
        LogError( ( "Unable to " purposeOfWrite ": Insufficient memory configured in SIGV4_PROCESSING_BUFFER_LENGTH macro. BytesExceeded=%lu", \
                    ( unsigned long ) ( bytesExceeded ) ) );                                                                                   \
    }

/**
 * @brief A helper macro to test if a flag is set.
 */
#define FLAG_IS_SET( bits, flag )    ( ( ( bits ) & ( flag ) ) == ( flag ) )

/**
 * @brief A helper macro to determine if a character is whitespace.
 * @note The ctype function isspace() returns true for the following characters:
 * ` `, `\t`, `\n`, `\v`, `\f`, `\r`. However, according to RFC5234:
 * https://datatracker.ietf.org/doc/html/rfc5234#appendix-B.1
 * the only whitespace characters in an HTTP header are spaces and
 * horizontal tabs.
 */
#define isWhitespace( c )            ( ( ( c ) == ' ' ) || ( ( c ) == '\t' ) )

/**
 * @brief An aggregator representing the individually parsed elements of the
 * user-provided date parameter. This is used to verify the complete date
 * representation, and construct the final ISO 8601 string.
 */
typedef struct SigV4DateTime
{
    int32_t year; /**< Year (1900 or later) */
    int32_t mon;  /**< Month (1 to 12) */
    int32_t mday; /**< Day of Month (1 to 28/29/30/31) */
    int32_t hour; /**< Hour (0 to 23) */
    int32_t min;  /**< Minutes (0 to 59) */
    int32_t sec;  /**< Seconds (0 to 60) */
} SigV4DateTime_t;

/**
 * @brief A library structure holding the string and length values of parameters to
 * be sorted and standardized. This allows for a layer of abstraction during the
 * canonicalization step of the V4 signing process.
 */
typedef struct SigV4String
{
    char * pData;   /**< SigV4 string data */
    size_t dataLen; /**< Length of pData */
} SigV4String_t;

/**
 * @brief A library structure holding the string and length values of parameters to
 * be sorted and standardized. This allows for a layer of abstraction during the
 * canonicalization step of the V4 signing process.
 */
typedef struct SigV4ConstString
{
    const char * pData; /**< SigV4 string data */
    size_t dataLen;     /**< Length of pData */
} SigV4ConstString_t;

/**
 * @brief A key-value pair data structure that allows for sorting of SigV4
 * string values using internal comparison functions, and provides additional
 * stability to quickSort(), to comply with Misra rule 21.9.
 */
typedef struct SigV4KeyValuePair
{
    SigV4ConstString_t key;   /**< SigV4 string identifier */
    SigV4ConstString_t value; /**< SigV4 data */
} SigV4KeyValuePair_t;

/**
 * @brief An aggregator to maintain the internal state of canonicalization
 * during intermediate calculations.
 */
typedef struct CanonicalContext
{
    SigV4KeyValuePair_t pQueryLoc[ SIGV4_MAX_QUERY_PAIR_COUNT ];    /**< Query pointers used during sorting. */
    SigV4KeyValuePair_t pHeadersLoc[ SIGV4_MAX_HTTP_HEADER_COUNT ]; /**< Header pointers used during sorting. */

    uint8_t pBufProcessing[ SIGV4_PROCESSING_BUFFER_LENGTH ];       /**< Internal calculation buffer used during canonicalization. */
    size_t uxCursorIndex;                                           /**< pBufProcessing cursor. */
    size_t bufRemaining;                                            /**< pBufProcessing value used during internal calculation. */
    const char * pHashPayloadLoc;                                   /**< Pointer used to store the location of hashed HTTP request payload. */
    size_t hashPayloadLen;                                          /**< Length of hashed HTTP request payload. */
} CanonicalContext_t;

/**
 * @brief An aggregator to maintain the internal state of HMAC
 * calculations.
 */
typedef struct HmacContext
{
    /**
     * @brief The cryptography interface.
     */
    const SigV4CryptoInterface_t * pCryptoInterface;

    /**
     * @brief All accumulated key data.
     */
    uint8_t key[ SIGV4_HASH_MAX_BLOCK_LENGTH ];

    /**
     * @brief The length of the accumulated key data.
     */
    size_t keyLen;
} HmacContext_t;

/* *INDENT-OFF* */
#ifdef __cplusplus
    }
#endif
/* *INDENT-ON* */

#endif /* ifndef SIGV4_INTERNAL_H_ */
