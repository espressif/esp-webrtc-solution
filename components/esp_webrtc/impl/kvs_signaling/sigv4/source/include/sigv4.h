/*
 * SigV4 Library v1.3.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * @file sigv4.h
 * @brief Interface for the SigV4 Library.
 */

#ifndef SIGV4_H_
#define SIGV4_H_

/* Standard includes. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

/* Convenience macros for library optimization */

/** @addtogroup sigv4_constants
 *  @{
 */
#define SIGV4_AWS4_HMAC_SHA256                      "AWS4-HMAC-SHA256"                                              /**< AWS identifier for SHA256 signing algorithm. */
#define SIGV4_AWS4_HMAC_SHA256_LENGTH               ( sizeof( SIGV4_AWS4_HMAC_SHA256 ) - 1U )                       /**< Length of AWS identifier for SHA256 signing algorithm. */
#define SIGV4_HTTP_X_AMZ_DATE_HEADER                "x-amz-date"                                                    /**< AWS identifier for HTTP date header. */
#define SIGV4_HTTP_X_AMZ_SECURITY_TOKEN_HEADER      "x-amz-security-token"                                          /**< AWS identifier for security token. */

#define SIGV4_STREAMING_AWS4_HMAC_SHA256_PAYLOAD    "STREAMING-AWS4-HMAC-SHA256-PAYLOAD"                            /**< S3 identifier for chunked payloads. */
/* MISRA Ref 5.4.1 [Macro identifiers] */
/* More details at: https://github.com/aws/SigV4-for-AWS-IoT-embedded-sdk/blob/main/MISRA.md#rule-54 */
/* coverity[other_declaration] */
#define SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER           "x-amz-content-sha256"                                     /**< S3 identifier for streaming requests. */
#define SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER_LENGTH    ( sizeof( SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER ) - 1U )  /**< Length of S3 identifier for streaming requests. */
#define SIGV4_HTTP_X_AMZ_STORAGE_CLASS_HEADER            "x-amz-storage-class"                                      /**< S3 identifier for reduced streaming redundancy. */

#define SIGV4_ACCESS_KEY_ID_LENGTH                       20U                                                        /**< Length of access key ID. */
#define SIGV4_SECRET_ACCESS_KEY_LENGTH                   40U                                                        /**< Length of secret access key. */

#define SIGV4_ISO_STRING_LEN                             16U                                                        /**< Length of ISO 8601 date string. */
#define SIGV4_EXPECTED_LEN_RFC_3339                      20U                                                        /**< Length of RFC 3339 date input. */
#define SIGV4_EXPECTED_LEN_RFC_5322                      29U
/**< Length of RFC 5322 date input. */

/** @}*/

/**
 * @defgroup sigv4_canonical_flags SigV4HttpParameters_t Flags
 * @brief Flags for SigV4HttpParameters_t.flags. These flags inform the library
 * of parameters already in canonical form.
 *
 * Flags should be bitwise-ORed with each other to change the behavior of
 * #SigV4_GenerateHTTPAuthorization.
 */

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request path input is already
 * canonicalized.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_PATH_IS_CANONICAL_FLAG        0x1U

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request query input is already
 * canonicalized.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG       0x2U

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request headers input is
 * already canonicalized.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG    0x4U

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request payload is
 * already hashed.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_PAYLOAD_IS_HASH               0x8U

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request is
 * a presigned URL.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_IS_PRESIGNED_URL              0x10U

/**
 * @ingroup sigv4_canonical_flags
 * @brief Set this flag to indicate that the HTTP request path, query, and
 * headers are all already canonicalized.
 *
 * This flag is valid only for #SigV4HttpParameters_t.flags.
 */
#define SIGV4_HTTP_ALL_ARE_CANONICAL_FLAG        0x7U

/**
 * @ingroup sigv4_enum_types
 * @brief Return status of the SigV4 Library.
 */
typedef enum SigV4Status
{
    /**
     * @brief The SigV4 library function completed successfully.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     * - #SigV4_AwsIotDateToIso8601
     * - #SigV4_EncodeURI
     */
    SigV4Success,

    /**
     * @brief The SigV4 library function received an invalid input
     * parameter.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     * - #SigV4_AwsIotDateToIso8601
     */
    SigV4InvalidParameter,

    /**
     * @brief The application buffer was not large enough for the specified hash
     * function.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     * - #SigV4_EncodeURI
     */
    SigV4InsufficientMemory,

    /**
     * @brief An error occurred while formatting the provided date header.
     *
     * Functions that may return this value:
     * - #SigV4_AwsIotDateToIso8601
     */
    SigV4ISOFormattingError,

    /**
     * @brief The maximum number of header parameters was exceeded while parsing
     * the http header string passed to the library.
     * The maximum number of supported HTTP headers can be configured
     * with the SIGV4_MAX_HTTP_HEADER_COUNT macro in the library config file
     * passed by the application.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     */
    SigV4MaxHeaderPairCountExceeded,

    /**
     * @brief The maximum number of query parameters was exceeded while parsing
     * the query string passed to the library.
     * The maximum number of supported query parameters can be configured
     * with the SIGV4_MAX_QUERY_PAIR_COUNT macro in the library config file
     * passed by the application.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     */
    SigV4MaxQueryPairCountExceeded,

    /**
     * @brief An error occurred while performing a hash operation.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     */
    SigV4HashError,

    /**
     * @brief HTTP headers parsed to the library are invalid.
     *
     * Functions that may return this value:
     * - #SigV4_GenerateHTTPAuthorization
     */
    SigV4InvalidHttpHeaders
} SigV4Status_t;

/**
 * @ingroup sigv4_struct_types
 * @brief The cryptography interface used to supply the user-defined hash
 * implementation.
 */
typedef struct SigV4CryptoInterface
{
    /**
     * @brief Initializes the @p pHashContext.
     *
     * @param[in] pHashContext Context used to maintain the hash's current state
     * during incremental updates.
     *
     * @return Zero on success, all other return values are failures.
     */
    int32_t ( * hashInit )( void * pHashContext );

    /**
     * @brief Calculates an ongoing hash update (SHA-256, for example).
     *
     * @param[in] pHashContext Context used to maintain the hash's current state
     * during incremental updates.
     * @param[in] pInput Buffer holding the data to hash.
     * @param[in] inputLen length of the input buffer data.
     *
     * @return Zero on success, all other return values are failures.
     */
    int32_t ( * hashUpdate )( void * pHashContext,
                              const uint8_t * pInput,
                              size_t inputLen );

    /**
     * @brief Calculates the final binary digest of the hash from the context.
     *
     * @param[in] pHashContext Context used to maintain the hash's current state
     * during incremental updates.
     * @param[out] pOutput The buffer used to place final hash binary digest
     * output.
     * @param[in] outputLen The length of the pOutput buffer, which must be
     * larger than the hash digest length specified in
     * #SIGV4_HASH_MAX_DIGEST_LENGTH.
     *
     * @return Zero on success, all other return values are failures.
     */
    int32_t ( * hashFinal )( void * pHashContext,
                             uint8_t * pOutput,
                             size_t outputLen );

    /**
     * @brief Context for the hashInit, hashUpdate, and hashFinal interfaces.
     */
    void * pHashContext;

    /**
     * @brief The block length of the hash function.
     */
    size_t hashBlockLen;

    /**
     * @brief The digest length of the hash function.
     */
    size_t hashDigestLen;
} SigV4CryptoInterface_t;

/**
 * @ingroup sigv4_struct_types
 * @brief Configurations of the HTTP request used to create the Canonical
 * Request.
 */
typedef struct SigV4HttpParameters
{
    const char * pHttpMethod; /**< @brief The HTTP method: GET, POST, PUT, etc. */
    size_t httpMethodLen;     /**< @brief Length of pHttpMethod. */

    /**
     * @brief These flags are used to indicate if the path, query, or headers are already
     * in the canonical form. This is to bypass the internal sorting, white space
     * trimming, and encoding done by the library. This is a performance optimization
     * option. Please see https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
     * for information on generating a canonical path, query, and headers string.
     * - #SIGV4_HTTP_PATH_IS_CANONICAL_FLAG     0x1
     * - #SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG    0x2
     * - #SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG 0x4
     * - #SIGV4_HTTP_ALL_ARE_CANONICAL_FLAG     0x7
     */
    uint32_t flags;

    /**
     * @brief The path in the HTTP request. This is the absolute request URI,
     * which contains everything in the URI following the HTTP host until the
     * question mark character ("?") that begins any query string parameters
     * (e.g. "/path/to/item.txt"). If SIGV4_HTTP_PATH_IS_CANONICAL_FLAG is set,
     * then this input must already be in canonical form.
     *
     * @note If there exists no path for the HTTP request, then this can be
     * NULL.
     */
    const char * pPath;
    size_t pathLen; /**< @brief Length of pPath. */

    /**
     * @brief The HTTP request query from the URL, if it exists. This contains all
     * characters following the question mark character ("?") that denotes the start
     * of the query. If SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG is set, then this input
     * must already be in canonical form.
     *
     * @note If the HTTP request does not contain query string, this can
     * be NULL.
     */
    const char * pQuery;
    size_t queryLen; /**< @brief Length of pQuery. */

    /**
     * @brief The headers from the HTTP request that we want to sign. This
     * should be the raw headers in HTTP request format. If
     * SIGV4_HTTP_HEADERS_IS_CANONICAL_FLAG is set, then this input must
     * already be in canonical form.
     *
     * @note The headers data MUST NOT be empty. For HTTP/1.1 requests, it is
     * required that the "host" header MUST be part of the SigV4 signature.
     */
    const char * pHeaders;
    size_t headersLen; /**< @brief Length of pHeaders. */

    /**
     * @brief The HTTP response body, if one exists (ex. PUT request). If this
     * body is chunked, then this field should be set with
     * STREAMING-AWS4-HMAC-SHA256-PAYLOAD.
     */
    const char * pPayload;
    size_t payloadLen; /**< @brief Length of pPayload. */
} SigV4HttpParameters_t;

/**
 * @ingroup sigv4_struct_types
 * @brief Configurations for the AWS credentials used to generate the Signing
 * Key.
 */
typedef struct SigV4Credentials
{
    /**
     * @brief The pAccessKeyId MUST be at least 16 characters long
     * but not more than 128 characters long.
     */
    const char * pAccessKeyId;
    size_t accessKeyIdLen; /**< @brief Length of pAccessKeyId. */

    /**
     * @brief The pSecretAccessKey MUST be at least 40 characters long.
     */
    const char * pSecretAccessKey;
    size_t secretAccessKeyLen; /**< @brief Length of pSecretAccessKey. */
} SigV4Credentials_t;

/**
 * @ingroup sigv4_struct_types
 * @brief Complete configurations required for generating "String to Sign" and
 * "Signing Key" values.
 *
 * Consists of parameter structures #SigV4Credentials_t,
 * #SigV4CryptoInterface_t, and #SigV4HttpParameters_t, along with date, region,
 * and service specifications.
 */
typedef struct SigV4Parameters
{
    /**
     * @brief The AccessKeyId, SecretAccessKey, and SecurityToken used to
     * generate the Authorization header.
     */
    SigV4Credentials_t * pCredentials;

    /**
     * @brief The date in ISO 8601 format, e.g. "20150830T123600Z". This is
     * always 16 characters long.
     */
    const char * pDateIso8601;

    /**
     * @brief The algorithm used for SigV4 authentication. If set to NULL,
     * this will automatically be set to "AWS4-HMAC-SHA256" by default.
     */
    const char * pAlgorithm;

    size_t algorithmLen; /**< @brief Length of pAlgorithm. */

    /**
     * @brief The target AWS region for the request. Please see
     * https://docs.aws.amazon.com/general/latest/gr/rande.html for a list of
     * region names and codes.
     */
    const char * pRegion;
    size_t regionLen; /**< @brief Length of pRegion. */

    /**
     * @brief The target AWS service for the request. The service name can be
     * found as the first segment of the service endpoint. Please see
     * https://docs.aws.amazon.com/general/latest/gr/aws-service-information.html
     * (https://docs.aws.amazon.com/general/latest/gr/aws-service-information.html)
     * for your service of interest.
     */
    const char * pService;
    size_t serviceLen; /**< @brief Length of pService. */

    /**
     * @brief The cryptography interface.
     */
    SigV4CryptoInterface_t * pCryptoInterface;

    /**
     * @brief HTTP specific SigV4 parameters for canonical request calculation.
     */
    SigV4HttpParameters_t * pHttpParameters;
} SigV4Parameters_t;

/**
 * @brief Generates the HTTP Authorization header value.
 * @note The API does not support HTTP headers containing empty HTTP header keys or values.
 *
 * @param[in] pParams Parameters for generating the SigV4 signature.
 * @param[out] pAuthBuf Buffer to hold the generated Authorization header value.
 * @param[in, out] authBufLen Input: the length of @p pAuthBuf, output: the length
 * of the authorization value written to the buffer.
 * @param[out] pSignature Location of the signature in the authorization string.
 * @param[out] signatureLen The length of @p pSignature.
 *
 * @return #SigV4Success if successful, error code otherwise.
 *
 * <b>Example</b>
 * @code{c}
 * // The following example shows how to use the SigV4_GenerateHTTPAuthorization
 * // function to generate the HTTP Authorization header value for HTTP requests
 * // to AWS services requiring SigV4 authentication.
 *
 * SigV4Status_t status = SigV4Success;
 *
 * // Buffer to hold the authorization header.
 * char pSigv4Auth[ 2048U ];
 * size_t sigv4AuthLen = sizeof( pSigv4Auth );
 *
 * // Pointer to signature in the Authorization header that will be populated in
 * // pSigv4Auth by the SigV4_GenerateHTTPAuthorization API function.
 * char * signature = NULL;
 * size_t signatureLen = 0;
 *
 * SigV4Parameters_t sigv4Params =
 * {
 *     // Parsed temporary credentials obtained from AWS IoT Credential Provider.
 *     .pCredentials     = &sigv4Creds,
 *     // Date in ISO8601 format.
 *     .pDateIso8601     = pDateISO8601,
 *     // The AWS region for the request.
 *     .pRegion          = AWS_REGION,
 *     .regionLen        = strlen( AWS_REGION ),
 *     // The AWS service for the request.
 *     .pService         = AWS_SERVICE_NAME,
 *     .serviceLen       = strlen( AWS_SERVICE_NAME ),
 *     // SigV4 crypto interface. See SigV4CryptoInterface_t interface documentation.
 *     .pCryptoInterface = &cryptoInterface,
 *     // HTTP parameters for the HTTP request to generate a SigV4 authorization header for.
 *     .pHttpParameters  = &sigv4HttpParams
 * };
 *
 * status = SigV4_GenerateHTTPAuthorization( &sigv4Params, pSigv4Auth, &sigv4AuthLen, &signature, &signatureLen );
 *
 * if( status != SigV4Success )
 * {
 *    // Failed to generate authorization header.
 * }
 * @endcode
 */
/* @[declare_sigV4_generateHTTPAuthorization_function] */
SigV4Status_t SigV4_GenerateHTTPAuthorization( const SigV4Parameters_t * pParams,
                                               char * pAuthBuf,
                                               size_t * authBufLen,
                                               char ** pSignature,
                                               size_t * signatureLen );
/* @[declare_sigV4_generateHTTPAuthorization_function] */

/**
 * @brief Parse the date header value from the AWS IoT response, and generate
 * the formatted ISO 8601 date required for authentication.
 *
 * This is an optional utility function available to the application, to assist
 * with formatting of the date header obtained from AWS IoT (when requesting a
 * temporary token or sending a POST request).
 *
 * AWS SigV4 authentication requires an ISO 8601 date to be present in the
 * "x-amz-date" request header, as well as in the credential scope (must be
 * identical). For additional information on date handling, please see
 * https://docs.aws.amazon.com/general/latest/gr/sigv4-date-handling.html.
 *
 * Acceptable Input Formats:
 * - RFC 5322 (ex. "Thu, 18 Jan 2018 09:18:06 GMT"), the preferred format in
 *   HTTP 'Date' response headers. If using this format, the date parameter
 *   should match "***, DD 'MMM' YYYY hh:mm:ss GMT" exactly.
 * - RFC 3339 (ex. "2018-01-18T09:18:06Z"), found occasionally in 'Date' and
 *   expiration headers. If using this format, the date parameter should match
 *   "YYYY-MM-DD'T'hh:mm:ss'Z'" exactly.
 *
 * Formatted Output:
 * - The ISO8601-formatted date will be returned in the form
 *   "YYYYMMDD'T'HHMMSS'Z'" (ex. "20180118T091806Z").
 *
 * @param[in] pDate The date header (in
 * [RFC 3339](https://tools.ietf.org/html/rfc3339) or
 * [RFC 5322](https://tools.ietf.org/html/rfc5322) formats). An acceptable date
 * header can be found in the HTTP response returned by AWS IoT. This value
 * should use UTC (with no time-zone offset), and be exactly 20 or 29 characters
 * in length (excluding the null character), to comply with RFC 3339 and RFC
 * 5322 formats, respectively.
 * @param[in] dateLen The length of the pDate header value. Must be either
 * SIGV4_EXPECTED_LEN_RFC_3339 or SIGV4_EXPECTED_LEN_RFC_5322, for valid input
 * parameters.
 * @param[out] pDateISO8601 The formatted ISO8601-compliant date. The date value
 * written to this buffer will be exactly 16 characters in length, to comply
 * with the ISO8601 standard required for SigV4 authentication.
 * @param[in] dateISO8601Len The length of buffer pDateISO8601. Must be at least
 * SIGV4_ISO_STRING_LEN bytes, for valid input parameters.
 *
 * @return #SigV4Success code if successful, error code otherwise.
 *
 *
 * <b>Example</b>
 * @code{c}
 * // The following example shows how to use the SigV4_AwsIotDateToIso8601
 * // function to convert an AWS IoT date header value to a ISO 8601 date.
 *
 * SigV4Status_t status = SigV4Success;
 * char pDateISO8601[SIGV4_ISO_STRING_LEN] = {0};
 * size_t pDateISO8601Len = SIGV4_ISO_STRING_LEN;
 *
 * // pDate and dateLen are the date header and length which are parsed from
 * // an AWS IoT Credential Provider HTTP response, using an HTTP library.
 * status = SigV4_AwsIotDateToIso8601( pDate, dateLen, pDateISO8601, pDateISO8601Len );
 *
 * if( status != SigV4Success )
 * {
 *    // Failed to parse date
 * }
 * @endcode
 */
/* @[declare_sigV4_awsIotDateToIso8601_function] */
SigV4Status_t SigV4_AwsIotDateToIso8601( const char * pDate,
                                         size_t dateLen,
                                         char * pDateISO8601,
                                         size_t dateISO8601Len );
/* @[declare_sigV4_awsIotDateToIso8601_function] */

#if ( SIGV4_USE_CANONICAL_SUPPORT == 1 )

/**
 * @brief Normalize a URI string according to RFC 3986 and fill destination
 * buffer with the formatted string.
 *
 * @param[in] pUri The URI string to encode.
 * @param[in] uriLen Length of pUri.
 * @param[out] pCanonicalURI The resulting canonicalized URI.
 * @param[in, out] canonicalURILen input: the length of pCanonicalURI,
 * output: the length of the generated canonical URI.
 * @param[in] encodeSlash Option to indicate if slashes should be encoded.
 * @param[in] doubleEncodeEquals Option to indicate if equals should be double-encoded.
 *
 * @return #SigV4Success code if successful, error code otherwise.
 */
/* @[declare_sigV4_encodeURI_function] */
    SigV4Status_t SigV4_EncodeURI( const char * pUri,
                                   size_t uriLen,
                                   char * pCanonicalURI,
                                   size_t * canonicalURILen,
                                   bool encodeSlash,
                                   bool doubleEncodeEquals );
/* @[declare_sigV4_encodeURI_function] */

#endif /* #if (SIGV4_USE_CANONICAL_SUPPORT == 1) */

/* *INDENT-OFF* */
#ifdef __cplusplus
    }
#endif
/* *INDENT-ON* */

#endif /* SIGV4_H_ */
