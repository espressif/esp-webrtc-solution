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
 * @file sigv4.c
 * @brief Implements the user-facing functions in sigv4.h
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sigv4.h"
#include "sigv4_internal.h"
#include "sigv4_quicksort.h"

/* ESP-IDF heap allocation for CanonicalContext_t */
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif


/*-----------------------------------------------------------*/

#if ( SIGV4_USE_CANONICAL_SUPPORT == 1 )

/**
 * @brief Canonicalize the full URI path. The input URI starts after the
 * HTTP host and ends at the question mark character ("?") that begins the
 * query string parameters (if any). Example: folder/subfolder/item.txt"
 *
 * @param[in] pUri HTTP request URI, also known that the request absolute
 * path.
 * @param[in] uriLen Length of pUri.
 * @param[in] encodeTwice Service-dependent option to indicate whether
 * encoding should be done twice. For example, S3 requires that the
 * URI is encoded only once, while other services encode twice.
 * @param[in, out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 */
    static SigV4Status_t generateCanonicalURI( const char * pUri,
                                               size_t uriLen,
                                               bool encodeTwice,
                                               CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Canonicalize the query string HTTP URL, beginning (but not
 * including) at the "?" character. Does not include "/".
 *
 * @param[in] pQuery HTTP request query.
 * @param[in] queryLen Length of pQuery.
 * @param[in] doubleEncodeEqualsInParmsValues whether to double-encode any equals ( = ) characters in parameter values.
 * @param[in, out] pCanonicalContext Struct to maintain intermediary buffer
 * and state of canonicalization.
 */
    static SigV4Status_t generateCanonicalQuery( const char * pQuery,
                                                 size_t queryLen,
                                                 const bool doubleEncodeEqualsInParmsValues,
                                                 CanonicalContext_t * pCanonicalContext );

/**
 * @brief Determine if a character can be written without needing URI encoding when generating Canonical Request.
 *
 * @param[in] c The character to evaluate.
 * @param[in] encodeSlash Whether slashes may be encoded.
 *
 * @return `true` if the character does not need encoding, `false` if it does.
 */
    static bool isAllowedChar( char c,
                               bool encodeSlash );

/**
 * @brief Compare two SigV4 data structures lexicographically, without case-sensitivity.
 *
 * @param[in] pFirstVal SigV4 key value data structure to sort.
 * @param[in] pSecondVal SigV4 key value data structure to sort.
 *
 * @return Returns a value less than 0 if @pFirstVal < @pSecondVal, or
 * a value greater than 0 if @pSecondVal < @pFirstVal. 0 is never returned in
 * order to provide stability to quickSort() calls.
 */
    static int32_t cmpHeaderField( const void * pFirstVal,
                                   const void * pSecondVal );

#endif /* #if (SIGV4_USE_CANONICAL_SUPPORT == 1) */

/**
 * @brief Converts an integer value to its ASCII representation, and stores the
 * result in the provided buffer.
 *
 * @param[in] value The value to convert to ASCII.
 * @param[in, out] pBuffer The starting location of the buffer on input, and the
 * ending location on output.
 * @param[in] bufferLen Width of value to write (padded with leading 0s if
 * necessary).
 */
static void intToAscii( int32_t value,
                        char ** pBuffer,
                        size_t bufferLen );

/**
 * @brief Extract all header key-value pairs from the passed headers data and add them
 * to the canonical request.
 *
 * @param[in] pHeaders HTTP headers to canonicalize.
 * @param[in] headersLen Length of HTTP headers to canonicalize.
 * @param[in] flags Flag to indicate if headers are already
 * in the canonical form.
 * @param[out] canonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 * @param[out] pSignedHeaders The starting location of the signed headers.
 * @param[out] pSignedHeadersLen The length of the signed headers.
 *
 * @return Following statuses will be returned by the function:
 * #SigV4Success if headers are successfully added to the canonical request.
 * #SigV4InsufficientMemory if canonical request buffer cannot accommodate the header.
 * #SigV4InvalidParameter if HTTP headers are invalid.
 * #SigV4MaxHeaderPairCountExceeded if number of headers that needs to be canonicalized
 * exceed the SIGV4_MAX_HTTP_HEADER_COUNT macro defined in the config file.
 */
static SigV4Status_t generateCanonicalAndSignedHeaders( const char * pHeaders,
                                                        size_t headersLen,
                                                        uint32_t flags,
                                                        CanonicalContext_t * canonicalRequest,
                                                        char ** pSignedHeaders,
                                                        size_t * pSignedHeadersLen );

/**
 * @brief Append Signed Headers to the Canonical Request buffer.
 *
 * @param[in] headerCount Number of headers which needs to be appended.
 * @param[in] flags Flag to indicate if headers are already
 * in the canonical form.
 * @param[in,out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 * @param[out] pSignedHeaders The starting location of the signed headers.
 * @param[out] pSignedHeadersLen The length of the signed headers.
 */
static SigV4Status_t appendSignedHeaders( size_t headerCount,
                                          uint32_t flags,
                                          CanonicalContext_t * pCanonicalRequest,
                                          char ** pSignedHeaders,
                                          size_t * pSignedHeadersLen );

/**
 * @brief Canonicalize headers and append it to the Canonical Request buffer.
 *
 * @param[in] headerCount Number of headers which needs to be appended.
 * @param[in] flags Flag to indicate if headers are already
 * in the canonical form.
 * @param[in,out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 *
 * @return Following statuses will be returned by the function:
 * #SigV4Success if headers are successfully added to the canonical request.
 * #SigV4InsufficientMemory if canonical request buffer cannot accommodate the header.
 */
static SigV4Status_t appendCanonicalizedHeaders( size_t headerCount,
                                                 uint32_t flags,
                                                 CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Store the location of HTTP request hashed payload in the HTTP request.
 *
 * @param[in] headerIndex Index of request Header in the list of parsed headers.
 * @param[in] pAmzSHA256Header Literal for x-amz-content-sha256 header in HTTP request.
 * @param[in] amzSHA256HeaderLen Length of @p pAmzSHA256Header.
 * @param[in,out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 */
static void storeHashedPayloadLocation( size_t headerIndex,
                                        const char * pAmzSHA256Header,
                                        size_t amzSHA256HeaderLen,
                                        CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Parse each header key and value pair from HTTP headers.
 *
 * @param[in] pHeaders HTTP headers to parse.
 * @param[in] headersDataLen Length of HTTP headers to parse.
 * @param[in] flags Flag to indicate if headers are already
 * in the canonical form.
 * @param[out] headerCount Count of key-value pairs parsed from pData.
 * @param[out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 *
 * @return Following statuses will be returned by the function:
 * #SigV4Success if header key or value is successfully added to the canonical request.
 * #SigV4InsufficientMemory if canonical request buffer cannot accommodate the header.
 * #SigV4MaxHeaderPairCountExceeded if number of key-value entries in the headers data
 * exceeds the SIGV4_MAX_HTTP_HEADER_COUNT macro defined in the config file.
 */
static SigV4Status_t parseHeaderKeyValueEntries( const char * pHeaders,
                                                 size_t headersDataLen,
                                                 uint32_t flags,
                                                 size_t * headerCount,
                                                 CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Copy header key or header value to the Canonical Request buffer.
 *
 * @param[in] pData Header Key or value to be copied to the canonical request.
 * @param[in] dataLen Length of Header Key or value.
 * @param[in] flags Flag to indicate if headers are already
 * in the canonical form.
 * @param[in] separator Character separating the multiple key-value pairs or key and values.
 * @param[in,out] pCanonicalRequest Struct to maintain intermediary buffer
 * and state of canonicalization.
 *
 * @return Following statuses will be returned by the function:
 * #SigV4Success if the headers are successfully added to the canonical request.
 * #SigV4InsufficientMemory if canonical request buffer cannot accommodate the header.
 */
static SigV4Status_t copyHeaderStringToCanonicalBuffer( const char * pData,
                                                        size_t dataLen,
                                                        uint32_t flags,
                                                        char separator,
                                                        CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Helper function to determine whether a header string character represents a space
 * that can be trimmed when creating "Canonical Headers".
 * All leading and trailing spaces in the header strings need to be trimmed. Also, sequential spaces
 * in the header value need to be trimmed to a single space.
 *
 * Example of modifying header field for Canonical Headers:
 * Actual header pair:                 |      Modifier header pair
 * My-Header2:    "a   b   c"  \n      |      my-header2:"a b c"\n
 *
 * @param[in] value Header value or key string to be trimmed.
 * @param[in] index Index of current character.
 * @param[in] valLen Length of the string.
 * @param[in] trimmedLength Current length of trimmed string.
 *
 * @return `true` if the character needs to be trimmed, else `false`.
 */
static bool isTrimmableSpace( const char * value,
                              size_t index,
                              size_t valLen,
                              size_t trimmedLength );

/**
 * @brief Generate the canonical request but excluding the canonical headers
 * and anything that goes after it. Write it onto @p pSignedHeaders and update
 * it to point to the next location to write the rest of the canonical request.
 *
 * @param[in] pParams The application-defined parameters used to
 * generate the canonical request.
 * @param[in] pCanonicalContext The context of the canonical request.
 * @param[in,out] pSignedHeaders The location to start writing the canonical request and
 * becomes the location to write the rest of it when this function returns.
 * @param[in,out] pSignedHeadersLen The amount of buffer available and becomes the number
 * of bytes actually written when this function returns.
 * @return SigV4InsufficientMemory if the length of the canonical request output
 * buffer cannot fit the actual request before the headers, #SigV4Success otherwise.
 */
static SigV4Status_t generateCanonicalRequestUntilHeaders( const SigV4Parameters_t * pParams,
                                                           CanonicalContext_t * pCanonicalContext,
                                                           char ** pSignedHeaders,
                                                           size_t * pSignedHeadersLen );

/**
 * @brief Generates the prefix of the Authorization header of the format:
 * "<algorithm> Credential=<access key ID>/<credential scope>, SignedHeaders=<SignedHeaders>, Signature="
 *
 * @param[in] pParams The application-defined parameters used to
 * generate the canonical request.
 * @param[in] pAlgorithm The signing algorithm used for SigV4 authentication.
 * @param[in] algorithmLen The length of @p pAlgorithm.
 * @param[in] pSignedHeaders The signed headers of the SigV4 request.
 * @param[in] signedHeadersLen The length of @p pSignedHeaders.
 * @param[in,out] pAuthBuf The authorization buffer where to write the prefix.
 * Pointer is updated with the next location to write the value of the signature.
 * @param[in, out] pAuthPrefixLen On input, it should contain the total length of @p pAuthBuf.
 * On output, this will be filled with the length of the Authorization header, if
 * operation is successful.
 *
 * @return #SigV4InsufficientMemory if the length of the authorization buffer, @p pAuthBuf
 * is insufficient to store the entire authorization header value (i.e. Prefix + HexEncoded Signature);
 * otherwise #SigV4Success.
 */
static SigV4Status_t generateAuthorizationValuePrefix( const SigV4Parameters_t * pParams,
                                                       const char * pAlgorithm,
                                                       size_t algorithmLen,
                                                       const char * pSignedHeaders,
                                                       size_t signedHeadersLen,
                                                       char * pAuthBuf,
                                                       size_t * pAuthPrefixLen );

/**
 * @brief Write a line in the canonical request.
 * @note Used whenever there are components of the request that
 * are already canonicalized.
 *
 * @param[in] pLine The line to write to the canonical request.
 * @param[in] lineLen The length of @p pLine
 * @param[in,out] pCanonicalContext The canonical context where
 * the line should be written.
 * @return SigV4InsufficientMemory if the length of the canonical request
 * buffer cannot write the desired line, #SigV4Success otherwise.
 */
static SigV4Status_t writeLineToCanonicalRequest( const char * pLine,
                                                  size_t lineLen,
                                                  CanonicalContext_t * pCanonicalContext );

/**
 * @brief Set a query parameter key in the canonical request.
 *
 * @param[in] currentParameter The index of the query key to set
 * @param[in] pKey The pointer to the query key
 * @param[in] keyLen The length of @p pKey
 * @param[in,out] pCanonicalRequest The canonical request containing the
 * query parameter array of keys and values
 */
static void setQueryParameterKey( size_t currentParameter,
                                  const char * pKey,
                                  size_t keyLen,
                                  CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Set a query parameter value in the canonical request.
 *
 * @param[in] currentParameter The index of the query value to set
 * @param[in] pValue The pointer to the query value
 * @param[in] valueLen The length of @p pValue
 * @param[in,out] pCanonicalRequest The canonical request containing the
 * query parameter array of keys and values
 */
static void setQueryParameterValue( size_t currentParameter,
                                    const char * pValue,
                                    size_t valueLen,
                                    CanonicalContext_t * pCanonicalRequest );

/**
 * @brief Convert the character to lowercase.
 *
 * @param[in] inputChar character to be lowercased.
 *
 * @return Input character converted to lowercase.
 */
static char lowercaseCharacter( char inputChar );

/**
 * @brief Write the HTTP request payload hash to the canonical request.
 *
 * @param[in] pParams The application-defined parameters used for writing
 * the payload hash.
 * @param[out] pCanonicalContext The canonical context where the payload
 * hash should be written.
 *
 * @return SigV4InsufficientMemory if the length of the canonical request
 * buffer cannot write the desired line, #SigV4Success otherwise.
 */
static SigV4Status_t writePayloadHashToCanonicalRequest( const SigV4Parameters_t * pParams,
                                                         CanonicalContext_t * pCanonicalContext );

/**
 * @brief Generates the key for the HMAC operation.
 *
 * @note This function can be called multiple times before calling
 * #hmacIntermediate. Appending multiple substrings, then calling #hmacAddKey
 * on the appended string is also equivalent to calling #hmacAddKey on
 * each individual substring.
 * @note This function accepts a const char * so that string literals
 * can be passed in.
 *
 * @param[in] pHmacContext The context used for HMAC calculation.
 * @param[in] pKey The key used as input for HMAC calculation.
 * @param[in] keyLen The length of @p pKey.
 * @param[in] isKeyPrefix Flag to indicate whether the passed key is
 * prefix of a complete key for an HMAC operation. If this is a prefix,
 * then it will be stored in cache for use with remaining part of the
 * key that will be provided in a subsequent call to @ref hmacAddKey.
 *
 * @return Zero on success, all other return values are failures.
 */
static int32_t hmacAddKey( HmacContext_t * pHmacContext,
                           const char * pKey,
                           size_t keyLen,
                           bool isKeyPrefix );

/**
 * @brief Generates the intermediate hash output in the HMAC signing process.
 * This represents the H( K' ^ i_pad || message ) part of the HMAC algorithm.
 *
 * @note This MUST be ONLY called after #hmacAddKey; otherwise results in
 * undefined behavior. Likewise, one SHOULD NOT call #hmacAddKey after
 * calling #hmacIntermediate. One must call #hmacFinal first before calling
 * #hmacAddKey again.
 *
 * @param[in] pHmacContext The context used for HMAC calculation.
 * @param[in] pData The data used as input for HMAC calculation.
 * @param[in] dataLen The length of @p pData.
 *
 * @return Zero on success, all other return values are failures.
 */
static int32_t hmacIntermediate( HmacContext_t * pHmacContext,
                                 const char * pData,
                                 size_t dataLen );

/**
 * @brief Generates the end output of the HMAC algorithm.
 *
 * This represents the second hash operation in the HMAC algorithm:
 *   H( K' ^ o_pad || Intermediate Hash Output )
 * where the Intermediate Hash Output is generated from the call
 * to @ref hmacIntermediate.
 *
 * @param[in] pHmacContext The context used for HMAC calculation.
 * @param[out] pMac The buffer onto which to write the HMAC digest.
 * @param[in] macLen The length of @p pMac.
 *
 * @return Zero on success, all other return values are failures.
 */
static int32_t hmacFinal( HmacContext_t * pHmacContext,
                          char * pMac,
                          size_t macLen );

/**
 * @brief Generates the complete HMAC digest given a key and value, then write
 * the digest in the provided output buffer.
 *
 * @param[in] pHmacContext The context used for the current HMAC calculation.
 * @param[in] pKey The key passed as input to the HMAC function.
 * @param[in] keyLen The length of @p pKey.
 * @param[in] pData The data passed as input to the HMAC function.
 * @param[in] dataLen The length of @p pData.
 * @param[out] pOutput The buffer onto which to write the HMAC digest.
 * @param[out] outputLen The length of @p pOutput and must be greater
 * than pCryptoInterface->hashDigestLen for this function to succeed.
 * @return Zero on success, all other return values are failures.
 */
static int32_t completeHmac( HmacContext_t * pHmacContext,
                             const char * pKey,
                             size_t keyLen,
                             const char * pData,
                             size_t dataLen,
                             char * pOutput,
                             size_t outputLen );

/**
 * @brief Generates the complete hash of an input string, then write
 * the digest in the provided output buffer.
 * @note Unlike #completeHashAndHexEncode, this function will not
 * encode the hash and will simply output the bytes written by the
 * hash function.
 *
 * @param[in] pInput The data passed as input to the hash function.
 * @param[in] inputLen The length of @p pInput.
 * @param[out] pOutput The buffer onto which to write the hash.
 * @param[out] outputLen The length of @p pOutput and must be greater
 * than pCryptoInterface->hashDigestLen for this function to succeed.
 * @param[in] pCryptoInterface The interface used to call hash functions.
 * @return Zero on success, all other return values are failures.
 */
static int32_t completeHash( const uint8_t * pInput,
                             size_t inputLen,
                             uint8_t * pOutput,
                             size_t outputLen,
                             const SigV4CryptoInterface_t * pCryptoInterface );

/**
 * @brief Generate the complete hash of an input string, then write
 * the digest in an intermediary buffer before hex encoding and
 * writing it onto @p pOutput.
 *
 * @param[in] pInput The data passed as input to the hash function.
 * @param[in] inputLen The length of @p pInput.
 * @param[out] pOutput The buffer onto which to write the hex-encoded hash.
 * @param[out] pOutputLen The length of @p pOutput and must be greater
 * than pCryptoInterface->hashDigestLen * 2 for this function to succeed.
 * @param[in] pCryptoInterface The interface used to call hash functions.
 * @return Zero on success, all other return values are failures.
 */
static SigV4Status_t completeHashAndHexEncode( const char * pInput,
                                               size_t inputLen,
                                               char * pOutput,
                                               size_t * pOutputLen,
                                               const SigV4CryptoInterface_t * pCryptoInterface );

/**
 * @brief Generate the prefix of the string to sign containing the
 * algorithm and date then write it onto @p pBufStart.
 * @note This function assumes that enough bytes remain in @p pBufStart in
 * order to write the algorithm and date.
 *
 * @param[in] pBufStart The starting location of the buffer to write the string
 * to sign.
 * @param[in] pAlgorithm The algorithm used for generating the SigV4 signature.
 * @param[in] algorithmLen The length of @p pAlgorithm.
 * @param[in] pDateIso8601 The date used as part of the string to sign.
 * @return The number of bytes written to @p pBufStart.
 */
static size_t writeStringToSignPrefix( char * pBufStart,
                                       const char * pAlgorithm,
                                       size_t algorithmLen,
                                       const char * pDateIso8601 );

/**
 * @brief Generate the string to sign and write it onto a #SigV4String_t.
 *
 * @param[in] pParams The application-defined parameters used to
 * generate the string to sign.
 * @param[in] pAlgorithm The algorithm used for generating the SigV4 signature.
 * @param[in] algorithmLen The length of @p pAlgorithm.
 * @param[in,out] pCanonicalContext The context of the canonical request.
 * @return SigV4InsufficientMemory if the length of the canonical request output
 * buffer cannot fit the string to sign, #SigV4Success otherwise.
 */
static SigV4Status_t writeStringToSign( const SigV4Parameters_t * pParams,
                                        const char * pAlgorithm,
                                        size_t algorithmLen,
                                        CanonicalContext_t * pCanonicalContext );

/**
 * @brief Generate the signing key and write it onto a #SigV4String_t.
 *
 * @param[in] pSigV4Params The application-defined parameters used to
 * generate the signing key.
 * @param[in] pHmacContext The context used for the current HMAC calculation.
 * @param[out] pSigningKey The #SigV4String_t onto which the signing key will be written.
 * @param[in,out] pBytesRemaining The number of bytes remaining in the canonical buffer.
 * @return SigV4InsufficientMemory if the length of @p pSigningKey was insufficient to
 * fit the actual signing key, #SigV4Success otherwise.
 */
static SigV4Status_t generateSigningKey( const SigV4Parameters_t * pSigV4Params,
                                         HmacContext_t * pHmacContext,
                                         SigV4String_t * pSigningKey,
                                         size_t * pBytesRemaining );

/**
 * @brief Format the credential scope for the authorization header.
 * Credential scope includes the access key ID, date, region, and service parameters, and
 * ends with "aws4_request" terminator.
 *
 * @param[in] pSigV4Params The application parameters defining the credential's scope.
 * @param[in, out] pCredScope The credential scope in the SigV4 format.
 */
static void generateCredentialScope( const SigV4Parameters_t * pSigV4Params,
                                     SigV4String_t * pCredScope );

/**
 * @brief Check if the date represents a valid leap year day.
 *
 * @param[in] pDateElements The date representation to be verified.
 *
 * @return #SigV4Success if the date corresponds to a valid leap year,
 * #SigV4ISOFormattingError otherwise.
 */
static SigV4Status_t checkLeap( const SigV4DateTime_t * pDateElements );

/**
 * @brief Verify the date stored in a SigV4DateTime_t date representation.
 *
 * @param[in] pDateElements The date representation to be verified.
 *
 * @return #SigV4Success if the date is valid, and #SigV4ISOFormattingError if
 * any member of SigV4DateTime_t is invalid or represents an out-of-range date.
 */
static SigV4Status_t validateDateTime( const SigV4DateTime_t * pDateElements );

/**
 * @brief Append the value of a date element to the internal date representation
 * structure.
 *
 * @param[in] formatChar The specifier identifying the struct member to fill.
 * @param[in] result The value to assign to the specified struct member.
 * @param[out] pDateElements The date representation structure to modify.
 */
static void addToDate( const char formatChar,
                       int32_t result,
                       SigV4DateTime_t * pDateElements );

/**
 * @brief Interpret the value of the specified characters in date, based on the
 * format specifier, and append to the internal date representation.
 *
 * @param[in] pDate The date to be parsed.
 * @param[in] formatChar The format specifier used to interpret characters.
 * @param[in] readLoc The index of pDate to read from.
 * @param[in] lenToRead The number of characters to read.
 * @param[out] pDateElements The date representation to modify.
 *
 * @return #SigV4Success if parsing succeeded, #SigV4ISOFormattingError if the
 * characters read did not match the format specifier.
 */
static SigV4Status_t scanValue( const char * pDate,
                                const char formatChar,
                                size_t readLoc,
                                size_t lenToRead,
                                SigV4DateTime_t * pDateElements );

/**
 * @brief Parses date according to format string parameter, and populates date
 * representation struct SigV4DateTime_t with its elements.
 *
 * @param[in] pDate The date to be parsed.
 * @param[in] dateLen Length of pDate, the date to be formatted.
 * @param[in] pFormat The format string used to extract date pDateElements from
 * pDate. This string, among other characters, may contain specifiers of the
 * form "%LV", where L is the number of characters to be read, and V is one of
 * {Y, M, D, h, m, s, *}, representing a year, month, day, hour, minute, second,
 * or skipped (un-parsed) value, respectively.
 * @param[in] formatLen Length of the format string pFormat.
 * @param[out] pDateElements The deconstructed date representation of pDate.
 *
 * @return #SigV4Success if all format specifiers were matched successfully,
 * #SigV4ISOFormattingError otherwise.
 */
static SigV4Status_t parseDate( const char * pDate,
                                size_t dateLen,
                                const char * pFormat,
                                size_t formatLen,
                                SigV4DateTime_t * pDateElements );

/**
 * @brief Verify input parameters to the SigV4_GenerateHTTPAuthorization API.
 *
 * @param[in] pParams Complete SigV4 configurations passed by application.
 * @param[in] pAuthBuf The user-supplied buffer for filling Authorization Header.
 * @param[in] authBufLen The user-supplied size value of @p pAuthBuf buffer.
 * @param[in] pSignature The user-supplied pointer memory to store starting location of
 * Signature in Authorization Buffer.
 * @param[in] signatureLen The user-supplied pointer to store length of Signature.
 *
 * @return #SigV4Success if successful, #SigV4InvalidParameter otherwise.
 */
static SigV4Status_t verifyParamsToGenerateAuthHeaderApi( const SigV4Parameters_t * pParams,
                                                          const char * pAuthBuf,
                                                          const size_t * authBufLen,
                                                          char * const * pSignature,
                                                          const size_t * signatureLen );

/**
 * @brief Assign default arguments based on parameters set in @p pParams.
 *
 * @param[in] pParams Complete SigV4 configurations passed by application.
 * @param[out] pAlgorithm The algorithm used for SigV4 authentication.
 * @param[out] algorithmLen The length of @p pAlgorithm.
 */
static void assignDefaultArguments( const SigV4Parameters_t * pParams,
                                    const char ** pAlgorithm,
                                    size_t * algorithmLen );

/**
 * @brief Hex digest of provided string parameter.
 *
 * @param[in] pInputStr String to encode.
 * @param[out] pHexOutput Hex representation of @p pInputStr.
 *
 * @return #SigV4Success if successful, #SigV4InsufficientMemory otherwise.
 */
static SigV4Status_t lowercaseHexEncode( const SigV4String_t * pInputStr,
                                         SigV4String_t * pHexOutput );

/**
 * @brief Calculate number of bytes needed for the credential scope.
 * @note This does not include the linefeed character.
 *
 * @param[in] pSigV4Params SigV4 configurations passed by application.
 *
 * @return Number of bytes needed for credential scope.
 */
static size_t sizeNeededForCredentialScope( const SigV4Parameters_t * pSigV4Params );

/**
 * @brief Copy a string into a char * buffer.
 * @note This function can be used to copy a string literal without
 * MISRA warnings.
 *
 * @note This function assumes the destination buffer is large enough to hold
 * the string to copy, so will always write @p length bytes.
 *
 * @param[in] destination The buffer to write.
 * @param[in] source String to copy.
 * @param[in] length Number of characters to copy.
 *
 * @return @p length The number of characters written from @p source into
 * @p destination.
 */
static size_t copyString( char * destination,
                          const char * source,
                          size_t length );

/*-----------------------------------------------------------*/

static void intToAscii( int32_t value,
                        char ** pBuffer,
                        size_t bufferLen )
{
    int32_t currentVal = value;
    size_t lenRemaining = bufferLen;

    assert( pBuffer != NULL );
    assert( bufferLen > 0U );

    /* Write base-10 remainder in its ASCII representation, and fill any
     * remaining width with '0' characters. */
    while( lenRemaining > 0U )
    {
        lenRemaining--;
        ( *pBuffer )[ lenRemaining ] = ( char ) ( ( currentVal % 10 ) + '0' );
        currentVal /= 10;
    }

    /* Move pointer to follow last written character. */
    *pBuffer = &( ( *pBuffer )[ bufferLen ] );
}

/*-----------------------------------------------------------*/

static SigV4Status_t checkLeap( const SigV4DateTime_t * pDateElements )
{
    SigV4Status_t returnStatus = SigV4ISOFormattingError;

    assert( pDateElements != NULL );

    /* If the date represents a leap day, verify that the leap year is valid. */
    if( ( pDateElements->mon == 2 ) && ( pDateElements->mday == 29 ) )
    {
        if( ( ( pDateElements->year % 400 ) != 0 ) &&
            ( ( ( pDateElements->year % 4 ) != 0 ) ||
              ( ( pDateElements->year % 100 ) == 0 ) ) )
        {
            LogError( ( "%ld is not a valid leap year.",
                        ( long int ) pDateElements->year ) );
        }
        else
        {
            returnStatus = SigV4Success;
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t validateDateTime( const SigV4DateTime_t * pDateElements )
{
    SigV4Status_t returnStatus = SigV4Success;
    const int32_t daysPerMonth[] = MONTH_DAYS;

    assert( pDateElements != NULL );

    if( pDateElements->year < YEAR_MIN )
    {
        LogError( ( "Invalid 'year' value parsed from date string. "
                    "Expected an integer %ld or greater, received: %ld",
                    ( long int ) YEAR_MIN,
                    ( long int ) pDateElements->year ) );
        returnStatus = SigV4ISOFormattingError;
    }

    if( ( pDateElements->mon < 1 ) || ( pDateElements->mon > 12 ) )
    {
        LogError( ( "Invalid 'month' value parsed from date string. "
                    "Expected an integer between 1 and 12, received: %ld",
                    ( long int ) pDateElements->mon ) );
        returnStatus = SigV4ISOFormattingError;
    }

    /* Ensure that the day of the month is valid for the relevant month. */
    if( ( returnStatus != SigV4ISOFormattingError ) &&
        ( ( pDateElements->mday < 1 ) ||
          ( pDateElements->mday > daysPerMonth[ pDateElements->mon - 1 ] ) ) )
    {
        /* Check if the date is a valid leap year day. */
        returnStatus = checkLeap( pDateElements );

        if( returnStatus == SigV4ISOFormattingError )
        {
            LogError( ( "Invalid 'day' value parsed from date string. "
                        "Expected an integer between 1 and %ld, received: %ld",
                        ( long int ) daysPerMonth[ pDateElements->mon - 1 ],
                        ( long int ) pDateElements->mday ) );
        }
    }

    /* SigV4DateTime_t values are asserted to be non-negative before they are
     * assigned in function addToDate(). Therefore, we only verify logical upper
     * bounds for the following values. */
    if( pDateElements->hour > 23 )
    {
        LogError( ( "Invalid 'hour' value parsed from date string. "
                    "Expected an integer between 0 and 23, received: %ld",
                    ( long int ) pDateElements->hour ) );
        returnStatus = SigV4ISOFormattingError;
    }

    if( pDateElements->min > 59 )
    {
        LogError( ( "Invalid 'minute' value parsed from date string. "
                    "Expected an integer between 0 and 59, received: %ld",
                    ( long int ) pDateElements->min ) );
        returnStatus = SigV4ISOFormattingError;
    }

    /* An upper limit of 60 accounts for the occasional leap second UTC
     * adjustment. */
    if( pDateElements->sec > 60 )
    {
        LogError( ( "Invalid 'second' value parsed from date string. "
                    "Expected an integer between 0 and 60, received: %ld",
                    ( long int ) pDateElements->sec ) );
        returnStatus = SigV4ISOFormattingError;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static void addToDate( const char formatChar,
                       int32_t result,
                       SigV4DateTime_t * pDateElements )
{
    assert( pDateElements != NULL );
    assert( result >= 0 );

    switch( formatChar )
    {
        case 'Y':
            pDateElements->year = result;
            break;

        case 'M':
            pDateElements->mon = result;
            break;

        case 'D':
            pDateElements->mday = result;
            break;

        case 'h':
            pDateElements->hour = result;
            break;

        case 'm':
            pDateElements->min = result;
            break;

        case 's':
            pDateElements->sec = result;
            break;

        default:

            /* Do not assign values for skipped characters ('*'), or
             * unrecognized format specifiers. */
            break;
    }
}

/*-----------------------------------------------------------*/

static SigV4Status_t scanValue( const char * pDate,
                                const char formatChar,
                                size_t readLoc,
                                size_t lenToRead,
                                SigV4DateTime_t * pDateElements )
{
    SigV4Status_t returnStatus = SigV4InvalidParameter;
    const char * const pMonthNames[] = MONTH_NAMES;
    const char * pLoc = &( pDate[ readLoc ] );
    size_t remainingLenToRead = lenToRead;
    int32_t result = 0;

    assert( pDate != NULL );
    assert( pDateElements != NULL );

    if( formatChar == '*' )
    {
        remainingLenToRead = 0U;
    }

    /* Determine if month value is non-numeric. */
    if( ( formatChar == 'M' ) && ( remainingLenToRead == MONTH_ASCII_LEN ) )
    {
        while( result++ < 12 )
        {
            /* Search month array for parsed string. */
            if( strncmp( pMonthNames[ result - 1 ], pLoc, MONTH_ASCII_LEN ) == 0 )
            {
                returnStatus = SigV4Success;
                break;
            }
        }

        if( returnStatus != SigV4Success )
        {
            LogError( ( "Unable to match string '%.3s' to a month value.",
                        pLoc ) );
            returnStatus = SigV4ISOFormattingError;
        }

        remainingLenToRead = 0U;
    }

    /* Interpret integer value of numeric representation. */
    while( ( remainingLenToRead > 0U ) && ( *pLoc >= '0' ) && ( *pLoc <= '9' ) )
    {
        result = ( result * 10 ) + ( int32_t ) ( *pLoc - '0' );
        remainingLenToRead--;
        pLoc = &( pLoc[ 1 ] );
    }

    if( remainingLenToRead != 0U )
    {
        LogError( ( "Parsing Error: Expected numerical string of type '%%%d%c', "
                    "but received '%.*s'.",
                    ( int ) lenToRead,
                    formatChar,
                    ( int ) lenToRead,
                    pLoc ) );
        returnStatus = SigV4ISOFormattingError;
    }

    if( returnStatus != SigV4ISOFormattingError )
    {
        addToDate( formatChar,
                   result,
                   pDateElements );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t parseDate( const char * pDate,
                                size_t dateLen,
                                const char * pFormat,
                                size_t formatLen,
                                SigV4DateTime_t * pDateElements )
{
    SigV4Status_t returnStatus = SigV4InvalidParameter;
    size_t readLoc = 0U, formatIndex = 0U;
    uint8_t lenToRead = 0U;

    assert( pDate != NULL );
    assert( pFormat != NULL );
    assert( pDateElements != NULL );
    ( void ) dateLen;

    /* Loop through the format string. */
    while( ( formatIndex < formatLen ) && ( returnStatus != SigV4ISOFormattingError ) )
    {
        if( pFormat[ formatIndex ] == '%' )
        {
            /* '%' must be followed by a length and type specification. */
            assert( formatIndex < formatLen - 2 );
            formatIndex++;

            /* Numerical value of length specifier character. */
            lenToRead = ( ( uint8_t ) pFormat[ formatIndex ] - ( uint8_t ) '0' );
            formatIndex++;

            /* Ensure read is within buffer bounds. */
            assert( readLoc + lenToRead - 1 < dateLen );
            returnStatus = scanValue( pDate,
                                      pFormat[ formatIndex ],
                                      readLoc,
                                      lenToRead,
                                      pDateElements );

            readLoc += lenToRead;
        }
        else if( pDate[ readLoc ] != pFormat[ formatIndex ] )
        {
            LogError( ( "Parsing error: Expected character '%c', "
                        "but received '%c'.",
                        pFormat[ formatIndex ], pDate[ readLoc ] ) );
            returnStatus = SigV4ISOFormattingError;
        }
        else
        {
            readLoc++;
            LogDebug( ( "Successfully matched character '%c' found in format string.",
                        pDate[ readLoc - 1 ] ) );
        }

        formatIndex++;
    }

    if( ( returnStatus != SigV4ISOFormattingError ) )
    {
        returnStatus = SigV4Success;
    }
    else
    {
        LogError( ( "Parsing Error: Date did not match expected string format." ) );
        returnStatus = SigV4ISOFormattingError;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t lowercaseHexEncode( const SigV4String_t * pInputStr,
                                         SigV4String_t * pHexOutput )
{
    SigV4Status_t returnStatus = SigV4Success;
    static const char digitArr[] = "0123456789abcdef";
    char * hex = NULL;
    size_t i = 0U;
    const uint8_t * bytes;

    assert( pInputStr != NULL );
    assert( pHexOutput != NULL );
    assert( pInputStr->pData != NULL );
    assert( pHexOutput->pData != NULL );

    hex = pHexOutput->pData;
    bytes = ( const uint8_t * ) pInputStr->pData;

    /* Hex string notification of binary data takes twice the size. */
    if( pHexOutput->dataLen < ( pInputStr->dataLen * 2U ) )
    {
        returnStatus = SigV4InsufficientMemory;
        LOG_INSUFFICIENT_MEMORY_ERROR( "hex encode",
                                       ( pInputStr->dataLen * 2U ) - pHexOutput->dataLen );
    }
    else
    {
        for( i = 0; i < pInputStr->dataLen; i++ )
        {
            *hex = digitArr[ ( bytes[ i ] & 0xF0U ) >> 4 ];
            hex++;
            *hex = digitArr[ ( bytes[ i ] & 0x0FU ) ];
            hex++;
        }

        pHexOutput->dataLen = pInputStr->dataLen * 2U;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static size_t sizeNeededForCredentialScope( const SigV4Parameters_t * pSigV4Params )
{
    assert( pSigV4Params != NULL );
    return ISO_DATE_SCOPE_LEN +                                        \
           CREDENTIAL_SCOPE_SEPARATOR_LEN + pSigV4Params->regionLen +  \
           CREDENTIAL_SCOPE_SEPARATOR_LEN + pSigV4Params->serviceLen + \
           CREDENTIAL_SCOPE_SEPARATOR_LEN + CREDENTIAL_SCOPE_TERMINATOR_LEN;
}

/*-----------------------------------------------------------*/

static size_t copyString( char * destination,
                          const char * source,
                          size_t length )
{
    ( void ) memcpy( destination, source, length );
    return length;
}

/*-----------------------------------------------------------*/

static void generateCredentialScope( const SigV4Parameters_t * pSigV4Params,
                                     SigV4String_t * pCredScope )
{
    char * pBufWrite = NULL;
    size_t credScopeLen = sizeNeededForCredentialScope( pSigV4Params );
    size_t copyStringResult;

    assert( pSigV4Params != NULL );
    assert( pSigV4Params->pCredentials != NULL );
    assert( pSigV4Params->pRegion != NULL );
    assert( pSigV4Params->pService != NULL );
    assert( pCredScope != NULL );
    assert( pCredScope->pData != NULL );
    assert( pCredScope->dataLen >= credScopeLen );

    pBufWrite = pCredScope->pData;

    /* Each concatenated component is separated by a '/' character. */
    /* Concatenate first 8 characters from the provided ISO 8601 string (YYYYMMDD). */
    ( void ) memcpy( pBufWrite, pSigV4Params->pDateIso8601, ISO_DATE_SCOPE_LEN );
    pBufWrite = &( pBufWrite[ ISO_DATE_SCOPE_LEN ] );

    *pBufWrite = CREDENTIAL_SCOPE_SEPARATOR;
    pBufWrite = &( pBufWrite[ CREDENTIAL_SCOPE_SEPARATOR_LEN ] );

    /* Concatenate AWS region. */
    ( void ) memcpy( pBufWrite, pSigV4Params->pRegion, pSigV4Params->regionLen );
    pBufWrite = &( pBufWrite[ pSigV4Params->regionLen ] );

    *pBufWrite = CREDENTIAL_SCOPE_SEPARATOR;
    pBufWrite = &( pBufWrite[ CREDENTIAL_SCOPE_SEPARATOR_LEN ] );

    /* Concatenate AWS service. */
    ( void ) memcpy( pBufWrite, pSigV4Params->pService, pSigV4Params->serviceLen );
    pBufWrite = &( pBufWrite[ pSigV4Params->serviceLen ] );

    *pBufWrite = CREDENTIAL_SCOPE_SEPARATOR;
    pBufWrite = &( pBufWrite[ CREDENTIAL_SCOPE_SEPARATOR_LEN ] );

    /* Concatenate terminator. */
    copyStringResult = copyString( pBufWrite, CREDENTIAL_SCOPE_TERMINATOR, CREDENTIAL_SCOPE_TERMINATOR_LEN );
    pBufWrite = &( pBufWrite[ copyStringResult ] );

    /* Verify that the number of bytes written match the sizeNeededForCredentialScope()
     * utility function for calculating size of credential scope. */
    assert( ( size_t ) ( pBufWrite - pCredScope->pData ) == credScopeLen );

    pCredScope->dataLen = credScopeLen;
}

/*-----------------------------------------------------------*/

#if ( SIGV4_USE_CANONICAL_SUPPORT == 1 )

    static int32_t cmpHeaderField( const void * pFirstVal,
                                   const void * pSecondVal )
    {
        const SigV4KeyValuePair_t * pFirst, * pSecond = NULL;
        size_t lenSmall = 0U;

        assert( pFirstVal != NULL );
        assert( pSecondVal != NULL );

        pFirst = ( const SigV4KeyValuePair_t * ) pFirstVal;
        pSecond = ( const SigV4KeyValuePair_t * ) pSecondVal;

        assert( ( pFirst->key.pData != NULL ) && ( pFirst->key.dataLen != 0U ) );
        assert( ( pSecond->key.pData != NULL ) && ( pSecond->key.dataLen != 0U ) );

        if( pFirst->key.dataLen <= pSecond->key.dataLen )
        {
            lenSmall = pFirst->key.dataLen;
        }
        else
        {
            lenSmall = pSecond->key.dataLen;
        }

        return strncmp( pFirst->key.pData,
                        pSecond->key.pData,
                        lenSmall );
    }

/*-----------------------------------------------------------*/

    static int32_t cmpQueryFieldValue( const void * pFirstVal,
                                       const void * pSecondVal )
    {
        const SigV4KeyValuePair_t * pFirst, * pSecond = NULL;
        size_t lenSmall = 0U;
        int32_t compResult = -1;

        assert( pFirstVal != NULL );
        assert( pSecondVal != NULL );

        pFirst = ( const SigV4KeyValuePair_t * ) pFirstVal;
        pSecond = ( const SigV4KeyValuePair_t * ) pSecondVal;

        assert( ( pFirst->key.pData != NULL ) && ( pFirst->key.dataLen != 0U ) );
        assert( ( pSecond->key.pData != NULL ) && ( pSecond->key.dataLen != 0U ) );

        lenSmall = ( pFirst->key.dataLen < pSecond->key.dataLen ) ? pFirst->key.dataLen : pSecond->key.dataLen;
        compResult = ( int32_t ) strncmp( pFirst->key.pData,
                                          pSecond->key.pData,
                                          lenSmall );

        if( compResult == 0 )
        {
            if( pFirst->key.dataLen == pSecond->key.dataLen )
            {
                /* The fields are equal, so sorting must be done by value. */
                lenSmall = ( pFirst->value.dataLen < pSecond->value.dataLen ) ? pFirst->value.dataLen : pSecond->value.dataLen;
                compResult = ( int32_t ) strncmp( pFirst->value.pData,
                                                  pSecond->value.pData,
                                                  lenSmall );
            }
            else
            {
                /* Fields share a common prefix, so the shorter one should come first. */
                compResult = ( pFirst->key.dataLen < pSecond->key.dataLen ) ? -1 : 1;
            }
        }

        if( ( compResult == 0 ) && ( pFirst->value.dataLen != pSecond->value.dataLen ) )
        {
            /* Values share a common prefix, so the shorter one should come first. */
            compResult = ( pFirst->value.dataLen < pSecond->value.dataLen ) ? -1 : 1;
        }

        return compResult;
    }

/*-----------------------------------------------------------*/

    static char toUpperHexChar( uint8_t value )
    {
        static const char upperHexArr[] = "0123456789ABCDEF";

        assert( value < 16U );

        return upperHexArr[ value ];
    }

/*-----------------------------------------------------------*/

    static size_t writeHexCodeOfChar( char * pBuffer,
                                      size_t bufferLen,
                                      char code )
    {
        assert( pBuffer != NULL );
        assert( bufferLen >= URI_ENCODED_SPECIAL_CHAR_SIZE );

        /* Suppress unused warning in when asserts are disabled. */
        ( void ) bufferLen;

        pBuffer[ 0 ] = '%';
        pBuffer[ 1 ] = toUpperHexChar( ( ( uint8_t ) code ) >> 4U );
        pBuffer[ 2 ] = toUpperHexChar( ( ( uint8_t ) code ) & 0x0FU );

        return URI_ENCODED_SPECIAL_CHAR_SIZE;
    }

/*-----------------------------------------------------------*/

    static size_t writeDoubleEncodedEquals( char * pBuffer,
                                            size_t bufferLen )
    {
        assert( pBuffer != NULL );
        assert( bufferLen > URI_DOUBLE_ENCODED_EQUALS_CHAR_SIZE );

        /* Suppress unused warning in when asserts are disabled. */
        ( void ) bufferLen;

        pBuffer[ 0 ] = '%';
        pBuffer[ 1 ] = '2';
        pBuffer[ 2 ] = '5';
        pBuffer[ 3 ] = '3';
        pBuffer[ 4 ] = 'D';

        return URI_DOUBLE_ENCODED_EQUALS_CHAR_SIZE;
    }

/*-----------------------------------------------------------*/

    static bool isAllowedChar( char c,
                               bool encodeSlash )
    {
        bool ret = false;

        /* Lowercase. */
        if( ( c >= 'a' ) && ( c <= 'z' ) )
        {
            ret = true;
        }
        /* Uppercase. */
        else if( ( c >= 'A' ) && ( c <= 'Z' ) )
        {
            ret = true;
        }
        /* Numeric. */
        else if( ( c >= '0' ) && ( c <= '9' ) )
        {
            ret = true;
        }
        /* Other characters. */
        else if( ( c == '-' ) || ( c == '_' ) || ( c == '.' ) || ( c == '~' ) )
        {
            ret = true;
        }
        else if( c == '/' )
        {
            ret = !encodeSlash;
        }
        else
        {
            ret = false;
        }

        return ret;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t generateCanonicalURI( const char * pUri,
                                               size_t uriLen,
                                               bool encodeTwice,
                                               CanonicalContext_t * pCanonicalRequest )
    {
        SigV4Status_t returnStatus = SigV4Success;
        size_t uxBufIndex;
        size_t encodedLen = 0U;

        assert( pUri != NULL );
        assert( pCanonicalRequest != NULL );

        uxBufIndex = pCanonicalRequest->uxCursorIndex;
        encodedLen = pCanonicalRequest->bufRemaining;

        /* If the canonical URI needs to be encoded twice, then we encode once here,
         * and again at the end of the buffer. Afterwards, the second encode is copied
         * to overwrite the first one. */
        returnStatus = SigV4_EncodeURI( pUri, uriLen, ( char * ) &( pCanonicalRequest->pBufProcessing[ uxBufIndex ] ), &encodedLen, false, false );

        if( returnStatus == SigV4Success )
        {
            if( encodeTwice )
            {
                /* Calculate remaining buffer space AFTER first encoding */
                size_t remainingAfterFirstEncode = pCanonicalRequest->bufRemaining - encodedLen;

                /* Ensure we have enough space for double encoding */
                if( remainingAfterFirstEncode < encodedLen )
                {
                    /* Not enough space - double encoding could be up to 3x the original */
                    returnStatus = SigV4InsufficientMemory;
                }
                else
                {
                    size_t doubleEncodedLen = remainingAfterFirstEncode;

                    /* Note that the result of encoding the URI a second time must be
                     * written to a different position in the buffer. It should not be done
                     * at an overlapping position of the single-encoded URI. Once written,
                     * the double-encoded URI is moved to the starting location of the single-encoded URI. */
                    returnStatus = SigV4_EncodeURI( ( char * ) &( pCanonicalRequest->pBufProcessing[ uxBufIndex ] ),
                                                    encodedLen,
                                                    ( char * ) &( pCanonicalRequest->pBufProcessing[ uxBufIndex + encodedLen ] ),
                                                    &doubleEncodedLen,
                                                    false,
                                                    false );

                    if( returnStatus == SigV4Success )
                    {
                        /* Validate doubleEncodedLen doesn't exceed remaining space */
                        if( doubleEncodedLen > remainingAfterFirstEncode )
                        {
                            returnStatus = SigV4InsufficientMemory;
                        }
                        else
                        {
                            ( void ) memmove( &( pCanonicalRequest->pBufProcessing[ uxBufIndex ] ), &( pCanonicalRequest->pBufProcessing[ uxBufIndex + encodedLen ] ), doubleEncodedLen );
                            uxBufIndex = uxBufIndex + doubleEncodedLen;
                            pCanonicalRequest->bufRemaining -= ( encodedLen + doubleEncodedLen );
                        }
                    }
                }
            }
            else
            {
                uxBufIndex = uxBufIndex + encodedLen;
                pCanonicalRequest->bufRemaining -= encodedLen;
            }
        }

        if( returnStatus == SigV4Success )
        {
            if( pCanonicalRequest->bufRemaining < 1U )
            {
                returnStatus = SigV4InsufficientMemory;
                LOG_INSUFFICIENT_MEMORY_ERROR( "write newline character after canonical URI", 1U );
            }
            else
            {
                ( ( char * ) ( pCanonicalRequest->pBufProcessing ) )[ uxBufIndex ] = LINEFEED_CHAR;
                pCanonicalRequest->uxCursorIndex = uxBufIndex + 1U;
                pCanonicalRequest->bufRemaining -= 1U;
            }
        }

        return returnStatus;
    }

/*-----------------------------------------------------------*/

    static bool isTrimmableSpace( const char * value,
                                  size_t index,
                                  size_t valLen,
                                  size_t trimmedLength )
    {
        bool ret = false;

        assert( ( value != NULL ) && ( index < valLen ) );

        /* Only trim spaces. */
        if( isWhitespace( value[ index ] ) )
        {
            /* The last character is a trailing space. */
            if( ( index + 1U ) == valLen )
            {
                ret = true;
            }
            /* Trim if the next character is also a space. */
            else if( isWhitespace( value[ index + 1U ] ) )
            {
                ret = true;
            }
            /* It is a leading space if no characters have been written yet. */
            else if( trimmedLength == 0U )
            {
                ret = true;
            }
            else
            {
                /* Empty else. */
            }
        }

        return ret;
    }

/*-----------------------------------------------------------*/
    static char lowercaseCharacter( char inputChar )
    {
        char outputChar;

        /* Get the offset from a capital to lowercase character */
        int8_t offset = 'a' - 'A';

        if( ( inputChar >= 'A' ) && ( inputChar <= 'Z' ) )
        {
            outputChar = inputChar + offset;
        }
        else
        {
            outputChar = inputChar;
        }

        return outputChar;
    }

    static SigV4Status_t copyHeaderStringToCanonicalBuffer( const char * pData,
                                                            size_t dataLen,
                                                            uint32_t flags,
                                                            char separator,
                                                            CanonicalContext_t * pCanonicalRequest )
    {
        SigV4Status_t status = SigV4Success;
        size_t index = 0;
        size_t numOfBytesCopied = 0;
        size_t buffRemaining;
        size_t uxCurrBufIndex;

        assert( ( pData != NULL ) && ( dataLen > 0 ) );
        assert( pCanonicalRequest != NULL );

        buffRemaining = pCanonicalRequest->bufRemaining;
        uxCurrBufIndex = pCanonicalRequest->uxCursorIndex;

        for( index = 0; index < dataLen; index++ )
        {
            /* If the header field is not in canonical form already, we need to check
             * whether this character represents a trimmable space. */
            if( !FLAG_IS_SET( flags, SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG ) &&
                isTrimmableSpace( pData, index, dataLen, numOfBytesCopied ) )
            {
                /* Cannot copy trimmable space into canonical request buffer. */
            }
            /* Remaining buffer space should at least accommodate the character to copy and the trailing separator character. */
            else if( buffRemaining <= 1U )
            {
                status = SigV4InsufficientMemory;
                break;
            }
            else
            {
                /* Lowercase header key only. '\n' character marks the end of the value and header value
                 * does not need to be lowercased. */
                if( separator == '\n' )
                {
                    ( ( char * ) pCanonicalRequest->pBufProcessing )[ uxCurrBufIndex ] = pData[ index ];
                }
                else
                {
                    ( ( char * ) pCanonicalRequest->pBufProcessing )[ uxCurrBufIndex ] = lowercaseCharacter( pData[ index ] );
                }

                uxCurrBufIndex++;
                numOfBytesCopied++;
                buffRemaining--;
            }
        }

        /* Check that data to be copied does not contain all spaces only. */
        if( ( status == SigV4Success ) && ( numOfBytesCopied == 0U ) )
        {
            status = SigV4InvalidParameter;
        }

        /* Add the ending separating character passed to the function.
         * Note: Space for the separator character is accounted for while copying
         * header field data to canonical request buffer. */
        if( status == SigV4Success )
        {
            assert( buffRemaining >= 1 );
            ( ( char * ) ( pCanonicalRequest->pBufProcessing ) )[ uxCurrBufIndex ] = separator;
            uxCurrBufIndex++;
            pCanonicalRequest->uxCursorIndex = uxCurrBufIndex;
            pCanonicalRequest->bufRemaining = ( buffRemaining - 1U );
        }

        return status;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t appendSignedHeaders( size_t headerCount,
                                              uint32_t flags,
                                              CanonicalContext_t * pCanonicalRequest,
                                              char ** pSignedHeaders,
                                              size_t * pSignedHeadersLen )
    {
        size_t headerIndex = 0, keyLen = 0;
        SigV4Status_t sigV4Status = SigV4Success;
        const char * headerKey;
        size_t uxSignedHeaderIndex;

        assert( pCanonicalRequest != NULL );
        assert( headerCount > 0 );

        /* Store the starting location of the Signed Headers in the Canonical Request buffer. */
        *pSignedHeaders = ( char * ) &( pCanonicalRequest->pBufProcessing[ pCanonicalRequest->uxCursorIndex ] );
        uxSignedHeaderIndex = pCanonicalRequest->uxCursorIndex;

        for( headerIndex = 0; headerIndex < headerCount; headerIndex++ )
        {
            assert( ( pCanonicalRequest->pHeadersLoc[ headerIndex ].key.pData ) != NULL );
            keyLen = pCanonicalRequest->pHeadersLoc[ headerIndex ].key.dataLen;

            headerKey = pCanonicalRequest->pHeadersLoc[ headerIndex ].key.pData;

            /* ';' is used to separate signed multiple headers in the canonical request. */
            sigV4Status = copyHeaderStringToCanonicalBuffer( headerKey, keyLen, flags, ';', pCanonicalRequest );

            if( sigV4Status != SigV4Success )
            {
                LogError( ( "Unable to write Signed Headers for Canonical Request: Insufficient memory configured in \"SIGV4_PROCESSING_BUFFER_LENGTH\"" ) );
                break;
            }
        }

        /* Store the length of the "Signed Headers" data appended to the Canonical Request. */
        *pSignedHeadersLen = pCanonicalRequest->uxCursorIndex - uxSignedHeaderIndex - 1U;

        if( sigV4Status == SigV4Success )
        {
            /* Replacing the last ';' with '\n' as last header should not have ';'. */
            ( ( char * ) ( pCanonicalRequest->pBufProcessing ) )[ pCanonicalRequest->uxCursorIndex - 1U ] = '\n';
        }

        return sigV4Status;
    }

/*-----------------------------------------------------------*/
    static void storeHashedPayloadLocation( size_t headerIndex,
                                            const char * pAmzSHA256Header,
                                            size_t amzSHA256HeaderLen,
                                            CanonicalContext_t * pCanonicalRequest )
    {
        assert( pCanonicalRequest != NULL );

        const uint8_t * pHeaderData = ( const uint8_t * ) pCanonicalRequest->pHeadersLoc[ headerIndex ].key.pData;
        size_t headerLen = pCanonicalRequest->pHeadersLoc[ headerIndex ].key.dataLen;
        const uint8_t * pHeaderLiteral = ( const uint8_t * ) pAmzSHA256Header;

        if( ( headerLen == amzSHA256HeaderLen ) && ( memcmp( pHeaderData, pHeaderLiteral, headerLen ) == 0 ) )
        {
            pCanonicalRequest->pHashPayloadLoc = pCanonicalRequest->pHeadersLoc[ headerIndex ].value.pData;
            pCanonicalRequest->hashPayloadLen = pCanonicalRequest->pHeadersLoc[ headerIndex ].value.dataLen;
        }
    }

    static SigV4Status_t appendCanonicalizedHeaders( size_t headerCount,
                                                     uint32_t flags,
                                                     CanonicalContext_t * pCanonicalRequest )
    {
        size_t headerIndex = 0, keyLen = 0, valLen = 0;
        const char * value;
        const char * headerKey;
        SigV4Status_t sigV4Status = SigV4Success;

        assert( pCanonicalRequest != NULL );
        assert( headerCount > 0 );

        for( headerIndex = 0; headerIndex < headerCount; headerIndex++ )
        {
            assert( pCanonicalRequest->pHeadersLoc[ headerIndex ].key.pData != NULL );
            keyLen = pCanonicalRequest->pHeadersLoc[ headerIndex ].key.dataLen;
            valLen = pCanonicalRequest->pHeadersLoc[ headerIndex ].value.dataLen;
            headerKey = pCanonicalRequest->pHeadersLoc[ headerIndex ].key.pData;
            /* ':' is used to separate header key and header value in the canonical request. */
            sigV4Status = copyHeaderStringToCanonicalBuffer( headerKey, keyLen, flags, ':', pCanonicalRequest );

            if( sigV4Status == SigV4Success )
            {
                value = pCanonicalRequest->pHeadersLoc[ headerIndex ].value.pData;
                /* '\n' is used to separate each key-value pair in the canonical request. */
                sigV4Status = copyHeaderStringToCanonicalBuffer( value, valLen, flags, '\n', pCanonicalRequest );
            }

            if( sigV4Status != SigV4Success )
            {
                break;
            }
        }

        return sigV4Status;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t parseHeaderKeyValueEntries( const char * pHeaders,
                                                     size_t headersDataLen,
                                                     uint32_t flags,
                                                     size_t * headerCount,
                                                     CanonicalContext_t * pCanonicalRequest )
    {
        size_t index = 0, noOfHeaders;
        const char * pKeyOrValStartLoc;
        const char * pCurrLoc;
        bool keyFlag = true;
        SigV4Status_t sigV4Status = SigV4Success;
        ptrdiff_t dataLen = 0;

        assert( pHeaders != NULL );
        assert( headersDataLen > 0 );
        assert( pCanonicalRequest != NULL );
        assert( headerCount != NULL );

        noOfHeaders = *headerCount;
        pKeyOrValStartLoc = pHeaders;
        pCurrLoc = pHeaders;

        for( index = 0; index < headersDataLen; index++ )
        {
            if( noOfHeaders == SIGV4_MAX_HTTP_HEADER_COUNT )
            {
                sigV4Status = SigV4MaxHeaderPairCountExceeded;
                break;
            }
            /* Look for key part of an header field entry. */
            else if( ( keyFlag ) && ( pHeaders[ index ] == ':' ) )
            {
                dataLen = pCurrLoc - pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].key.pData = pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].key.dataLen = ( size_t ) dataLen;
                pKeyOrValStartLoc = &( pCurrLoc[ 1 ] );
                keyFlag = false;
            }
            /* Look for header value part of a header field entry for both canonicalized and non-canonicalized forms. */
            /* Non-canonicalized headers will have header values ending with "\r\n". */
            else if( ( !keyFlag ) && !FLAG_IS_SET( flags, SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG ) &&
                     ( ( index + HTTP_REQUEST_LINE_ENDING_LEN ) < headersDataLen ) &&
                     ( 0 == strncmp( pCurrLoc, HTTP_REQUEST_LINE_ENDING, HTTP_REQUEST_LINE_ENDING_LEN ) ) )
            {
                dataLen = pCurrLoc - pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].value.pData = pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].value.dataLen = ( size_t ) dataLen;

                /* Storing location of hashed request payload */
                storeHashedPayloadLocation( noOfHeaders, SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER, SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER_LENGTH, pCanonicalRequest );

                /* Set starting location of the next header key string after the "\r\n". */
                /* Validate we don't go past buffer end */
                if( ( index + HTTP_REQUEST_LINE_ENDING_LEN ) < headersDataLen )
                {
                    pKeyOrValStartLoc = &( pCurrLoc[ HTTP_REQUEST_LINE_ENDING_LEN ] );
                    keyFlag = true;
                    noOfHeaders++;
                }
                else
                {
                    sigV4Status = SigV4InvalidHttpHeaders;
                    break;
                }
            }
            /* Canonicalized headers will have header values ending just with "\n". */
            else if( ( !keyFlag ) && FLAG_IS_SET( flags, SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG ) && ( pHeaders[ index ] == '\n' ) )
            {
                dataLen = pCurrLoc - pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].value.pData = pKeyOrValStartLoc;
                pCanonicalRequest->pHeadersLoc[ noOfHeaders ].value.dataLen = ( size_t ) dataLen;

                /* Storing location of hashed request payload */
                storeHashedPayloadLocation( noOfHeaders, SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER, SIGV4_HTTP_X_AMZ_CONTENT_SHA256_HEADER_LENGTH, pCanonicalRequest );

                /* Set starting location of the next header key string after the "\n". */
                pKeyOrValStartLoc = &( pCurrLoc[ 1 ] );
                keyFlag = true;
                noOfHeaders++;
            }
            else
            {
                /* Empty else. */
            }

            pCurrLoc++;
        }

        /* Ensure each key has its corresponding value. */
        assert( keyFlag == true );

        /* If no header was found OR header value was not found for a header key,
         *  that represents incorrect HTTP headers data passed by the application. */
        if( ( noOfHeaders == 0U ) || ( keyFlag == false ) )
        {
            sigV4Status = SigV4InvalidHttpHeaders;
        }
        else
        {
            *headerCount = noOfHeaders;
        }

        return sigV4Status;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t generateCanonicalAndSignedHeaders( const char * pHeaders,
                                                            size_t headersLen,
                                                            uint32_t flags,
                                                            CanonicalContext_t * canonicalRequest,
                                                            char ** pSignedHeaders,
                                                            size_t * pSignedHeadersLen )
    {
        size_t noOfHeaders = 0;
        SigV4Status_t sigV4Status = SigV4Success;

        assert( pHeaders != NULL );
        assert( canonicalRequest != NULL );
        assert( pSignedHeaders != NULL );
        assert( pSignedHeadersLen != NULL );

        /* Parsing header string to extract key and value. */
        sigV4Status = parseHeaderKeyValueEntries( pHeaders,
                                                  headersLen,
                                                  flags,
                                                  &noOfHeaders,
                                                  canonicalRequest );

        if( sigV4Status == SigV4Success )
        {
            if( FLAG_IS_SET( flags, SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG ) )
            {
                /* Headers are already canonicalized, so just write it to the buffer as is. */
                sigV4Status = writeLineToCanonicalRequest( pHeaders,
                                                           headersLen,
                                                           canonicalRequest );
            }
            else
            {
                /* Sorting headers based on keys. */
                quickSort( canonicalRequest->pHeadersLoc, noOfHeaders, sizeof( SigV4KeyValuePair_t ), cmpHeaderField );

                /* If the headers are canonicalized, we will copy them directly into the buffer as they do not
                 * need processing, else we need to call the following function. */
                sigV4Status = appendCanonicalizedHeaders( noOfHeaders, flags, canonicalRequest );
            }
        }

        /* The \n character must be written if provided headers are not already canonicalized. */
        if( ( sigV4Status == SigV4Success ) && !FLAG_IS_SET( flags, SIGV4_HTTP_HEADERS_ARE_CANONICAL_FLAG ) )
        {
            if( canonicalRequest->bufRemaining < 1U )
            {
                sigV4Status = SigV4InsufficientMemory;
                LOG_INSUFFICIENT_MEMORY_ERROR( "write the newline character after canonical headers", 1U );
            }
            else
            {
                ( ( char * ) ( canonicalRequest->pBufProcessing ) )[ canonicalRequest->uxCursorIndex ] = LINEFEED_CHAR;
                canonicalRequest->uxCursorIndex++;
                canonicalRequest->bufRemaining--;
            }
        }

        if( sigV4Status == SigV4Success )
        {
            sigV4Status = appendSignedHeaders( noOfHeaders,
                                               flags,
                                               canonicalRequest,
                                               pSignedHeaders,
                                               pSignedHeadersLen );
        }

        return sigV4Status;
    }

/*-----------------------------------------------------------*/

    static void setQueryParameterKey( size_t currentParameter,
                                      const char * pKey,
                                      size_t keyLen,
                                      CanonicalContext_t * pCanonicalRequest )
    {
        assert( pKey != NULL );
        assert( keyLen > 0U );
        assert( ( pCanonicalRequest != NULL ) && ( pCanonicalRequest->pQueryLoc != NULL ) );

        pCanonicalRequest->pQueryLoc[ currentParameter ].key.pData = pKey;
        pCanonicalRequest->pQueryLoc[ currentParameter ].key.dataLen = keyLen;
    }

/*-----------------------------------------------------------*/

    static void setQueryParameterValue( size_t currentParameter,
                                        const char * pValue,
                                        size_t valueLen,
                                        CanonicalContext_t * pCanonicalRequest )
    {
        assert( ( pCanonicalRequest != NULL ) && ( pCanonicalRequest->pQueryLoc != NULL ) );

        pCanonicalRequest->pQueryLoc[ currentParameter ].value.pData = pValue;
        pCanonicalRequest->pQueryLoc[ currentParameter ].value.dataLen = valueLen;
    }

/*-----------------------------------------------------------*/

    static void processAmpersandInQueryString( bool fieldHasValue,
                                               size_t currQueryIndex,
                                               size_t * pStartOfFieldOrValue,
                                               size_t * pCurrParamCount,
                                               const char * pQuery,
                                               CanonicalContext_t * pCanonicalRequest )
    {
        bool previousParamIsValid = true;

        assert( pCurrParamCount != NULL );
        assert( pStartOfFieldOrValue != NULL );
        assert( pQuery != NULL );
        assert( pCanonicalRequest != NULL );

        /* Process scenarios when a value for the previous parameter entry has been detected. */
        if( fieldHasValue )
        {
            /* Case when parameter has empty value even though the query parameter entry has the form
             * "<param>=". */
            if( ( currQueryIndex - *pStartOfFieldOrValue ) == 0U )
            {
                /* Store the previous parameter's empty value information. Use NULL to represent empty value. */
                setQueryParameterValue( *pCurrParamCount, NULL, 0U, pCanonicalRequest );
            }

            /* This represents the case when the previous parameter has a value associated with it. */
            else
            {
                /* End of value reached, so store a pointer to the previously set value. */
                setQueryParameterValue( *pCurrParamCount, &( pQuery[ *pStartOfFieldOrValue ] ), currQueryIndex - *pStartOfFieldOrValue, pCanonicalRequest );
            }
        }
        /* A parameter value has not been found for the previous parameter. */
        else
        {
            /* Ignore unnecessary '&' that represent empty parameter names.
             * Trimmable '&'s can be leading, trailing or repeated as separators between parameter
             * pairs.
             * For example, this function will prune "&p1=v1&&p2=v2&" to "p1=v1&p2=v2". */
            if( ( currQueryIndex - *pStartOfFieldOrValue ) == 0U )
            {
                /* Do nothing as previous parameter name is empty. */
                previousParamIsValid = false;
            }

            /* Case when a new query parameter pair has begun without the previous parameter having an associated value,
             * i.e. of the format "<param1>&<param2>=<value>" where "<param1>".
             * In thus case, as the previous parameter does not have a value, we will store an empty value for it. */
            else
            {
                /* Store information about previous query parameter name. The query parameter has no associated value. */
                setQueryParameterKey( *pCurrParamCount, &( pQuery[ *pStartOfFieldOrValue ] ), currQueryIndex - *pStartOfFieldOrValue, pCanonicalRequest );

                /* Store the previous parameter's empty value information. Use NULL to represent empty value. */
                setQueryParameterValue( *pCurrParamCount, NULL, 0U, pCanonicalRequest );
            }
        }

        /* Increment the parameter count if the previous parameter is not empty
         * An empty previous parameter is caused in query strings where the '&' are unnecessary. */
        if( previousParamIsValid == true )
        {
            /* As the previous parameter is valid, increment the total parameters
             * parsed so far from the query string. */
            ( *pCurrParamCount )++;
        }

        /* As '&' is always treated as a separator, update the starting index to represent the next
         * query parameter. */
        *pStartOfFieldOrValue = currQueryIndex + 1U;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t parseQueryString( const char * pQuery,
                                           size_t queryLen,
                                           size_t * pNumberOfParameters,
                                           CanonicalContext_t * pCanonicalRequest )
    {
        size_t currentParameter = 0U, i = 0U, startOfFieldOrValue = 0U;
        bool fieldHasValue = false;
        SigV4Status_t returnStatus = SigV4Success;

        assert( pNumberOfParameters != NULL );
        assert( pCanonicalRequest != NULL );
        assert( pCanonicalRequest->pQueryLoc != NULL );

        /* Note: Constness of the query string is casted out here, taking care not to modify
         * its contents in any way. */

        /* Set cursors to each field and value in the query string.
         * Note: We iterate the index to queryLen (instead of queryLen - 1) for the
         * special case of handling the last index as a terminating value of a query
         * parameter. */
        for( i = 0U; i <= queryLen; i++ )
        {
            /* This test is at the beginning of the loop to ensure that
             * `pCanonicalRequest->pQueryLoc`is only accessed with a valid index.
             * The final iteration may result in `currentParameter` holding
             * SIGV4_MAX_QUERY_PAIR_COUNT, in order to set the number of parameters. */
            if( currentParameter >= SIGV4_MAX_QUERY_PAIR_COUNT )
            {
                returnStatus = SigV4MaxQueryPairCountExceeded;
                LogError( ( "Failed to parse query string: Number of query parameters exceeds max threshold defined in config. "
                            "SIGV4_MAX_QUERY_PAIR_COUNT=%lu", ( unsigned long ) SIGV4_MAX_QUERY_PAIR_COUNT ) );
                break;
            }

            /* Encountering an '&' indicates the start of a new key, and the end of the
             * old value (or key if the field has no value). The last value will not be
             * followed by anything, so we iterate to one beyond the end of the string.
             * Short circuit evaluation ensures the string is not dereferenced for that case. */
            if( ( i == queryLen ) || ( pQuery[ i ] == '&' ) )
            {
                processAmpersandInQueryString( fieldHasValue, i, &startOfFieldOrValue,
                                               &currentParameter, pQuery, pCanonicalRequest );

                /* As '&' represents a new parameter, reset the flag about parameter value state.*/
                fieldHasValue = false;
            }
            else if( ( pQuery[ i ] == '=' ) && !fieldHasValue )
            {
                /* Store information about Query Parameter Key in the canonical context. This query parameter has an associated value. */
                setQueryParameterKey( currentParameter, &( pQuery[ startOfFieldOrValue ] ), i - startOfFieldOrValue, pCanonicalRequest );

                /* Set the starting index for the query parameter's value. */
                startOfFieldOrValue = i + 1U;

                /* Set the flag to indicate that the current parameter's value has been found. */
                fieldHasValue = true;
            }
            else
            {
                /* Empty else. */
            }
        }

        *pNumberOfParameters = currentParameter;

        return returnStatus;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t writeValueInCanonicalizedQueryString( char * pBufCur,
                                                               size_t bufferLen,
                                                               const char * pValue,
                                                               size_t valueLen,
                                                               size_t * pEncodedLen,
                                                               const bool doubleEncodeEqualsInParmsValues )
    {
        SigV4Status_t returnStatus = SigV4Success;
        size_t valueBytesWritten = 0U;

        assert( pBufCur != NULL );
        assert( pEncodedLen != NULL );
        assert( ( pValue == NULL ) || ( valueLen != 0U ) );

        /* Check that there is space at least for the equals to character. */
        if( bufferLen < 1U )
        {
            LOG_INSUFFICIENT_MEMORY_ERROR( "write '=' query parameter separator", 1U );
            returnStatus = SigV4InsufficientMemory;
        }
        else
        {
            *pBufCur = '=';
            *pEncodedLen = 1U;
            valueBytesWritten = bufferLen - 1U;

            /* Encode parameter value if non-empty. Query parameters can have empty values. */
            if( valueLen > 0U )
            {
                returnStatus = SigV4_EncodeURI( pValue,
                                                valueLen,
                                                &( pBufCur[ 1 ] ),
                                                &valueBytesWritten,
                                                true /* Encode slash (/) */,
                                                doubleEncodeEqualsInParmsValues );

                if( returnStatus == SigV4Success )
                {
                    *pEncodedLen += valueBytesWritten;
                }
            }
        }

        return returnStatus;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t writeCanonicalQueryParameters( CanonicalContext_t * pCanonicalRequest,
                                                        size_t numberOfParameters,
                                                        const bool doubleEncodeEqualsInParmsValues )
    {
        SigV4Status_t returnStatus = SigV4Success;
        size_t uxBufIndex;
        size_t encodedLen = 0U, remainingLen = 0U, paramsIndex = 0U;

        assert( pCanonicalRequest != NULL );
        assert( pCanonicalRequest->pQueryLoc != NULL );

        uxBufIndex = pCanonicalRequest->uxCursorIndex;
        remainingLen = pCanonicalRequest->bufRemaining;

        for( paramsIndex = 0U; paramsIndex < numberOfParameters; paramsIndex++ )
        {
            assert( pCanonicalRequest->pQueryLoc[ paramsIndex ].key.pData != NULL );
            assert( pCanonicalRequest->pQueryLoc[ paramsIndex ].key.dataLen > 0U );

            encodedLen = remainingLen;
            returnStatus = SigV4_EncodeURI( pCanonicalRequest->pQueryLoc[ paramsIndex ].key.pData,
                                            pCanonicalRequest->pQueryLoc[ paramsIndex ].key.dataLen,
                                            ( char * ) &( pCanonicalRequest->pBufProcessing[ uxBufIndex ] ),
                                            &encodedLen,
                                            true /* Encode slash (/) */,
                                            false /* Do not double encode '='. */ );

            if( returnStatus == SigV4Success )
            {
                uxBufIndex = uxBufIndex + encodedLen;
                remainingLen -= encodedLen;

                assert( pCanonicalRequest->pQueryLoc[ paramsIndex ].value.pData != NULL );
                returnStatus = writeValueInCanonicalizedQueryString( ( char * ) &( pCanonicalRequest->pBufProcessing[ uxBufIndex ] ),
                                                                     remainingLen,
                                                                     pCanonicalRequest->pQueryLoc[ paramsIndex ].value.pData,
                                                                     pCanonicalRequest->pQueryLoc[ paramsIndex ].value.dataLen,
                                                                     &encodedLen,
                                                                     doubleEncodeEqualsInParmsValues );
                uxBufIndex = uxBufIndex + encodedLen;
                remainingLen -= encodedLen;
            }

            /* If there is a succeeding query parameter, add the '&' separator in the canonical query. */
            if( ( returnStatus == SigV4Success ) && ( ( paramsIndex + 1U ) != numberOfParameters ) )
            {
                /* Before adding the '&' for the next query parameter, check that there is
                 * space in the buffer. */
                if( remainingLen > 0U )
                {
                    ( ( char * ) ( pCanonicalRequest->pBufProcessing ) )[ uxBufIndex ] = '&';
                    uxBufIndex++;
                    remainingLen--;
                }

                /* There is at least another query parameter entry to encode
                 * but no space in the buffer. */
                else
                {
                    returnStatus = SigV4InsufficientMemory;
                    LOG_INSUFFICIENT_MEMORY_ERROR( "write query parameter separator, '&'", 1U );
                    break;
                }
            }
        }

        /* Update the context state if canonical query generation was successful. */
        if( returnStatus == SigV4Success )
        {
            pCanonicalRequest->uxCursorIndex = uxBufIndex;
            pCanonicalRequest->bufRemaining = remainingLen;
        }

        return returnStatus;
    }

/*-----------------------------------------------------------*/

    static SigV4Status_t generateCanonicalQuery( const char * pQuery,
                                                 size_t queryLen,
                                                 const bool doubleEncodeEqualsInParmsValues,
                                                 CanonicalContext_t * pCanonicalContext )
    {
        SigV4Status_t returnStatus = SigV4Success;
        size_t numberOfParameters = 0U;

        assert( pCanonicalContext != NULL );

        if( pQuery != NULL )
        {
            returnStatus = parseQueryString( pQuery, queryLen, &numberOfParameters, pCanonicalContext );
        }

        if( ( returnStatus == SigV4Success ) && ( numberOfParameters > 0U ) )
        {
            /* Sort the parameter names by character code point in ascending order.
             * Parameters with duplicate names should be sorted by value. */
            quickSort( pCanonicalContext->pQueryLoc, numberOfParameters, sizeof( SigV4KeyValuePair_t ), cmpQueryFieldValue );

            /* URI-encode each parameter name and value according to the following rules specified for SigV4:
             *  - Do not URI-encode any of the unreserved characters that RFC 3986 defines:
             *      A-Z, a-z, 0-9, hyphen ( - ), underscore ( _ ), period ( . ), and tilde ( ~ ).
             *  - Percent-encode all other characters with %XY, where X and Y are hexadecimal characters (0-9 and uppercase A-F).
             */
            returnStatus = writeCanonicalQueryParameters( pCanonicalContext, numberOfParameters, doubleEncodeEqualsInParmsValues );
        }

        if( returnStatus == SigV4Success )
        {
            if( pCanonicalContext->bufRemaining > 0U )
            {
                /* Append a linefeed at the end. */
                ( ( char * ) ( pCanonicalContext->pBufProcessing ) )[ pCanonicalContext->uxCursorIndex ] = LINEFEED_CHAR;
                pCanonicalContext->uxCursorIndex++;
                pCanonicalContext->bufRemaining -= 1U;
            }
            else
            {
                returnStatus = SigV4InsufficientMemory;
                LOG_INSUFFICIENT_MEMORY_ERROR( "write newline character after canonical query", 1U );
            }
        }

        return returnStatus;
    }

#endif /* #if ( SIGV4_USE_CANONICAL_SUPPORT == 1 ) */

/*-----------------------------------------------------------*/

static SigV4Status_t verifyParamsToGenerateAuthHeaderApi( const SigV4Parameters_t * pParams,
                                                          const char * pAuthBuf,
                                                          const size_t * authBufLen,
                                                          char * const * pSignature,
                                                          const size_t * signatureLen )
{
    SigV4Status_t returnStatus = SigV4Success;

    /* Check for NULL members of struct pParams */
    if( ( pParams == NULL ) || ( pAuthBuf == NULL ) || ( authBufLen == NULL ) ||
        ( pSignature == NULL ) || ( signatureLen == NULL ) )
    {
        LogError( ( "Parameter check failed: At least one of the input parameters is NULL. "
                    "Input parameters cannot be NULL" ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pCredentials == NULL )
    {
        LogError( ( "Parameter check failed: pParams->pCredentials is NULL." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pCredentials->pAccessKeyId == NULL ) || ( pParams->pCredentials->accessKeyIdLen == 0U ) )
    {
        LogError( ( "Parameter check failed: Access Key ID data is empty." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pCredentials->pSecretAccessKey == NULL ) || ( pParams->pCredentials->secretAccessKeyLen == 0U ) )
    {
        LogError( ( "Parameter check failed: Secret Access Key data is empty." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pDateIso8601 == NULL )
    {
        LogError( ( "Parameter check failed: pParams->DateIso8601 data is NULL." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pRegion == NULL ) || ( pParams->regionLen == 0U ) )
    {
        LogError( ( "Parameter check failed: Region data is empty." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pService == NULL ) || ( pParams->serviceLen == 0U ) )
    {
        LogError( ( "Parameter check failed: Service data is empty." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pAlgorithm != NULL ) && ( pParams->algorithmLen == 0U ) )
    {
        LogError( ( "Parameter check failed: Algorithm is specified but length (pParams->algorithmLen) passed is 0U." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pCryptoInterface == NULL )
    {
        LogError( ( "Parameter check failed: pParams->pCryptoInterface is NULL." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pCryptoInterface->hashInit == NULL ) || ( pParams->pCryptoInterface->hashUpdate == NULL ) ||
             ( pParams->pCryptoInterface->hashFinal == NULL ) )
    {
        LogError( ( "Parameter check failed: At least one of hashInit, hashUpdate, hashFinal function pointer members is NULL." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pCryptoInterface->hashBlockLen > SIGV4_HASH_MAX_BLOCK_LENGTH )
    {
        LogError( ( "Parameter check failed: pParams->pCryptoInterface->hashBlockLen is greater than `SIGV4_HASH_MAX_BLOCK_LENGTH`, "
                    "which can be configured in sigv4_config.h." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pCryptoInterface->hashDigestLen > SIGV4_HASH_MAX_DIGEST_LENGTH )
    {
        LogError( ( "Parameter check failed: pParams->pCryptoInterface->hashDigestLen is greater than `SIGV4_HASH_MAX_DIGEST_LENGTH`, "
                    "which can be configured in sigv4_config.h." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( pParams->pHttpParameters == NULL )
    {
        LogError( ( "Parameter check failed: pParams->pHttpParameters is NULL." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pHttpParameters->pHttpMethod == NULL ) || ( pParams->pHttpParameters->httpMethodLen == 0U ) )
    {
        LogError( ( "Parameter check failed: HTTP Method data is either NULL or zero bytes in length." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else if( ( pParams->pHttpParameters->pHeaders == NULL ) || ( pParams->pHttpParameters->headersLen == 0U ) )
    {
        LogError( ( "Parameter check failed: HTTP URI path information is either NULL or zero bytes in length." ) );
        returnStatus = SigV4InvalidParameter;
    }
    else
    {
        /* Empty else block for MISRA C:2012 compliance. */
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static void assignDefaultArguments( const SigV4Parameters_t * pParams,
                                    const char ** pAlgorithm,
                                    size_t * algorithmLen )
{
    if( pParams->pAlgorithm == NULL )
    {
        /* The default algorithm is AWS4-HMAC-SHA256. */
        *pAlgorithm = SIGV4_AWS4_HMAC_SHA256;
        *algorithmLen = SIGV4_AWS4_HMAC_SHA256_LENGTH;
    }
    else
    {
        *pAlgorithm = pParams->pAlgorithm;
        *algorithmLen = pParams->algorithmLen;
    }
}

/*-----------------------------------------------------------*/

static int32_t completeHash( const uint8_t * pInput,
                             size_t inputLen,
                             uint8_t * pOutput,
                             size_t outputLen,
                             const SigV4CryptoInterface_t * pCryptoInterface )
{
    int32_t hashStatus = -1;

    assert( pOutput != NULL );
    assert( outputLen > 0 );
    assert( pCryptoInterface != NULL );
    assert( pCryptoInterface->hashInit != NULL );
    assert( pCryptoInterface->hashUpdate != NULL );
    assert( pCryptoInterface->hashFinal != NULL );

    hashStatus = pCryptoInterface->hashInit( pCryptoInterface->pHashContext );

    if( hashStatus == 0 )
    {
        hashStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                   pInput, inputLen );
    }

    if( hashStatus == 0 )
    {
        hashStatus = pCryptoInterface->hashFinal( pCryptoInterface->pHashContext,
                                                  pOutput, outputLen );
    }

    return hashStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t completeHashAndHexEncode( const char * pInput,
                                               size_t inputLen,
                                               char * pOutput,
                                               size_t * pOutputLen,
                                               const SigV4CryptoInterface_t * pCryptoInterface )
{
    SigV4Status_t returnStatus = SigV4Success;
    /* Used to store the hash of the request payload. */
    uint8_t hashBuffer[ SIGV4_HASH_MAX_DIGEST_LENGTH ];
    SigV4String_t originalHash;
    SigV4String_t hexEncodedHash;

    assert( pOutput != NULL );
    assert( pOutputLen != NULL );
    assert( pCryptoInterface != NULL );
    assert( pCryptoInterface->hashInit != NULL );
    assert( pCryptoInterface->hashUpdate != NULL );
    assert( pCryptoInterface->hashFinal != NULL );

    originalHash.pData = ( char * ) hashBuffer;
    originalHash.dataLen = pCryptoInterface->hashDigestLen;
    hexEncodedHash.pData = pOutput;
    hexEncodedHash.dataLen = *pOutputLen;

    if( completeHash( ( const uint8_t * ) pInput,
                      inputLen,
                      hashBuffer,
                      pCryptoInterface->hashDigestLen,
                      pCryptoInterface ) != 0 )
    {
        returnStatus = SigV4HashError;
    }

    if( returnStatus == SigV4Success )
    {
        /* Hex-encode the request payload. */
        returnStatus = lowercaseHexEncode( &originalHash,
                                           &hexEncodedHash );
    }

    if( returnStatus == SigV4Success )
    {
        assert( hexEncodedHash.dataLen == pCryptoInterface->hashDigestLen * 2U );
        *pOutputLen = hexEncodedHash.dataLen;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t hmacAddKey( HmacContext_t * pHmacContext,
                           const char * pKey,
                           size_t keyLen,
                           bool isKeyPrefix )
{
    int32_t returnStatus = 0;
    const SigV4CryptoInterface_t * pCryptoInterface = NULL;
    const uint8_t * pUnsignedKey = ( const uint8_t * ) pKey;

    assert( pHmacContext != NULL );
    assert( pHmacContext->key != NULL );
    assert( pHmacContext->pCryptoInterface != NULL );
    assert( pHmacContext->pCryptoInterface->hashInit != NULL );
    assert( pHmacContext->pCryptoInterface->hashUpdate != NULL );
    assert( pHmacContext->pCryptoInterface->hashFinal != NULL );

    pCryptoInterface = pHmacContext->pCryptoInterface;

    /* At the first time this function is called, it is important that pHmacContext->keyLen
     * is set to 0U so that the key can be copied to the start of the buffer. */
    if( ( pHmacContext->keyLen + keyLen ) <= pCryptoInterface->hashBlockLen )
    {
        /* The key fits into the block so just append it. */
        ( void ) memcpy( &pHmacContext->key[ pHmacContext->keyLen ], pUnsignedKey, keyLen );
        pHmacContext->keyLen += keyLen;
    }
    else
    {
        returnStatus = pCryptoInterface->hashInit( pCryptoInterface->pHashContext );

        /* Hash part of the key that is cached in the HMAC context. */
        if( returnStatus == 0 )
        {
            returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                         pHmacContext->key,
                                                         pHmacContext->keyLen );
        }

        /* Hash down the remaining part of the key in order to create a block-sized derived key. */
        if( returnStatus == 0 )
        {
            returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                         pUnsignedKey,
                                                         keyLen );
        }

        if( returnStatus == 0 )
        {
            /* Store the final block-sized derived key. */
            returnStatus = pCryptoInterface->hashFinal( pCryptoInterface->pHashContext,
                                                        pHmacContext->key,
                                                        pCryptoInterface->hashBlockLen );
            pHmacContext->keyLen = pCryptoInterface->hashDigestLen;
        }
    }

    /* If the complete key has been obtained and it is less than the hash block size, then append
     * padding with zero valued bytes to make it block-sized. */
    if( !isKeyPrefix && ( returnStatus == 0 ) && ( pHmacContext->keyLen < pCryptoInterface->hashBlockLen ) )
    {
        /* Zero pad to the right so that the key has the same size as the block size. */
        ( void ) memset( ( void * ) ( &pHmacContext->key[ pHmacContext->keyLen ] ),
                         0,
                         pCryptoInterface->hashBlockLen - pHmacContext->keyLen );

        pHmacContext->keyLen = pCryptoInterface->hashBlockLen;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t hmacIntermediate( HmacContext_t * pHmacContext,
                                 const char * pData,
                                 size_t dataLen )
{
    int32_t returnStatus = 0;
    size_t i = 0U;
    const SigV4CryptoInterface_t * pCryptoInterface = pHmacContext->pCryptoInterface;

    assert( pHmacContext != NULL );
    assert( dataLen > 0U );
    assert( pHmacContext->key != NULL );
    assert( pHmacContext->pCryptoInterface != NULL );
    assert( pHmacContext->pCryptoInterface->hashInit != NULL );
    assert( pHmacContext->pCryptoInterface->hashUpdate != NULL );
    assert( pHmacContext->pCryptoInterface->hashFinal != NULL );
    assert( pHmacContext->keyLen == pCryptoInterface->hashBlockLen );

    /* Derive the inner HMAC key by XORing the key with inner pad byte. */
    for( i = 0U; i < pCryptoInterface->hashBlockLen; i++ )
    {
        /* XOR the key with the ipad. */
        pHmacContext->key[ i ] ^= HMAC_INNER_PAD_BYTE;
    }

    /* Initialize the Hash Crypto interface for performing new hash operation
     * of H(Inner Key || Data) in the HMAC algorithm. */
    returnStatus = pCryptoInterface->hashInit( pCryptoInterface->pHashContext );

    if( returnStatus == 0 )
    {
        /* Hash the inner-padded block-sized key. */
        returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                     pHmacContext->key,
                                                     pCryptoInterface->hashBlockLen );
    }

    if( returnStatus == 0 )
    {
        /* Hash the data. */
        returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                     ( const uint8_t * ) pData,
                                                     dataLen );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t hmacFinal( HmacContext_t * pHmacContext,
                          char * pMac,
                          size_t macLen )
{
    int32_t returnStatus = -1;
    uint8_t innerHashDigest[ SIGV4_HASH_MAX_DIGEST_LENGTH ];
    size_t i = 0U;
    const SigV4CryptoInterface_t * pCryptoInterface = NULL;

    assert( pHmacContext != NULL );
    assert( pHmacContext->key != NULL );
    assert( pHmacContext->pCryptoInterface != NULL );
    /* Note that we must have a block-sized derived key before calling this function. */
    assert( pHmacContext->pCryptoInterface->hashInit != NULL );
    assert( pHmacContext->pCryptoInterface->hashUpdate != NULL );
    assert( pHmacContext->pCryptoInterface->hashFinal != NULL );

    pCryptoInterface = pHmacContext->pCryptoInterface;

    /* Write the inner hash. */
    returnStatus = pCryptoInterface->hashFinal( pCryptoInterface->pHashContext,
                                                innerHashDigest,
                                                pCryptoInterface->hashDigestLen );

    if( returnStatus == 0 )
    {
        /* Create the outer-padded key by retrieving the original key from
         * the inner-padded key (by XORing with ipad byte) and then XOR with opad
         * to generate the outer-padded key. As XOR is associative, one way to do
         * this is by performing XOR on each byte of the inner-padded key (ipad ^ opad).  */
        for( i = 0U; i < pCryptoInterface->hashBlockLen; i++ )
        {
            pHmacContext->key[ i ] ^= HMAX_IPAD_XOR_OPAD_BYTE;
        }

        returnStatus = pCryptoInterface->hashInit( pCryptoInterface->pHashContext );
    }

    if( returnStatus == 0 )
    {
        /* Update hash using the outer-padded key. */
        returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                     pHmacContext->key,
                                                     pCryptoInterface->hashBlockLen );
    }

    if( returnStatus == 0 )
    {
        /* Update hash using the inner digest. */
        returnStatus = pCryptoInterface->hashUpdate( pCryptoInterface->pHashContext,
                                                     innerHashDigest,
                                                     pCryptoInterface->hashDigestLen );
    }

    if( returnStatus == 0 )
    {
        /* Write the final HMAC value. */
        returnStatus = pCryptoInterface->hashFinal( pCryptoInterface->pHashContext,
                                                    ( uint8_t * ) pMac,
                                                    macLen );
    }

    /* Reset the HMAC context. */
    pHmacContext->keyLen = 0U;

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t writeLineToCanonicalRequest( const char * pLine,
                                                  size_t lineLen,
                                                  CanonicalContext_t * pCanonicalContext )
{
    SigV4Status_t returnStatus = SigV4Success;

    assert( pLine != NULL );
    assert( pCanonicalContext != NULL );

    /* Make sure that there is space for the Method and the newline character.*/
    if( pCanonicalContext->bufRemaining < ( lineLen + 1U ) )
    {
        returnStatus = SigV4InsufficientMemory;
    }
    else
    {
        ( void ) memcpy( ( char * ) &( pCanonicalContext->pBufProcessing[ pCanonicalContext->uxCursorIndex ] ),
                         pLine,
                         lineLen );
        pCanonicalContext->uxCursorIndex += lineLen;

        ( ( char * ) ( pCanonicalContext->pBufProcessing ) )[ pCanonicalContext->uxCursorIndex ] = LINEFEED_CHAR;
        pCanonicalContext->uxCursorIndex++;

        pCanonicalContext->bufRemaining -= ( lineLen + 1U );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int32_t completeHmac( HmacContext_t * pHmacContext,
                             const char * pKey,
                             size_t keyLen,
                             const char * pData,
                             size_t dataLen,
                             char * pOutput,
                             size_t outputLen )
{
    int32_t returnStatus = 0;

    assert( pHmacContext != NULL );
    assert( pKey != NULL );
    assert( keyLen > 0U );
    assert( pData != NULL );
    assert( dataLen > 0U );
    assert( pOutput != NULL );
    assert( outputLen >= pHmacContext->pCryptoInterface->hashDigestLen );

    returnStatus = hmacAddKey( pHmacContext,
                               pKey,
                               keyLen,
                               false /* Not a key prefix. */ );

    if( returnStatus == 0 )
    {
        returnStatus = hmacIntermediate( pHmacContext, pData, dataLen );
    }

    if( returnStatus == 0 )
    {
        returnStatus = hmacFinal( pHmacContext, pOutput, outputLen );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static size_t writeStringToSignPrefix( char * pBufStart,
                                       const char * pAlgorithm,
                                       size_t algorithmLen,
                                       const char * pDateIso8601 )
{
    char * pBuffer = pBufStart;

    assert( pBufStart != NULL );
    assert( pAlgorithm != NULL );
    assert( pDateIso8601 != NULL );

    /* Need to write all substrings that come before the hash in the string to sign. */

    /* Write HMAC and hashing algorithm used for SigV4 authentication. */
    ( void ) memcpy( pBuffer, pAlgorithm, algorithmLen );
    pBuffer = &( pBuffer[ algorithmLen ] );

    *pBuffer = LINEFEED_CHAR;
    pBuffer = &( pBuffer[ 1 ] );

    /* Concatenate entire ISO 8601 date string. */
    ( void ) memcpy( pBuffer, pDateIso8601, SIGV4_ISO_STRING_LEN );
    pBuffer = &( pBuffer[ SIGV4_ISO_STRING_LEN ] );

    *pBuffer = LINEFEED_CHAR;

    return algorithmLen + 1U + SIGV4_ISO_STRING_LEN + 1U;
}

/*-----------------------------------------------------------*/

static SigV4Status_t writeStringToSign( const SigV4Parameters_t * pParams,
                                        const char * pAlgorithm,
                                        size_t algorithmLen,
                                        CanonicalContext_t * pCanonicalContext )
{
    SigV4Status_t returnStatus = SigV4Success;
    char * pBufStart = ( char * ) pCanonicalContext->pBufProcessing;
    size_t uxBufferLen = pCanonicalContext->uxCursorIndex;
    /* An overestimate but sufficient memory is checked before proceeding. */
    size_t encodedLen = SIGV4_PROCESSING_BUFFER_LENGTH;

    /* The string to sign is composed of (+ means string concatenation):
     * Algorithm + \n +
     * RequestDateTime + \n +
     * CredentialScope + \n +
     * HashedCanonicalRequest
     *
     * The processing buffer is verified beforehand that it has enough
     * space to hold this string. */
    size_t sizeNeededBeforeHash = algorithmLen + 1U + \
                                  SIGV4_ISO_STRING_LEN + 1U;

    assert( pParams != NULL );
    assert( ( pAlgorithm != NULL ) && ( algorithmLen > 0 ) );
    assert( pCanonicalContext != NULL );

    sizeNeededBeforeHash += sizeNeededForCredentialScope( pParams ) + 1U;

    /* Check if there is enough space for the string to sign. */
    if( ( sizeNeededBeforeHash + ( pParams->pCryptoInterface->hashDigestLen * 2U ) ) >
        SIGV4_PROCESSING_BUFFER_LENGTH )
    {
        returnStatus = SigV4InsufficientMemory;
        LOG_INSUFFICIENT_MEMORY_ERROR( "for string to sign",
                                       sizeNeededBeforeHash + ( pParams->pCryptoInterface->hashDigestLen * 2U ) - SIGV4_PROCESSING_BUFFER_LENGTH );
    }
    else
    {
        /* Hash the canonical request to its precalculated location in the string to sign. */
        returnStatus = completeHashAndHexEncode( pBufStart,
                                                 uxBufferLen,
                                                 &( pBufStart[ sizeNeededBeforeHash ] ),
                                                 &encodedLen,
                                                 pParams->pCryptoInterface );
    }

    if( returnStatus == SigV4Success )
    {
        size_t bytesWritten = 0U;
        SigV4String_t credentialScope;

        pCanonicalContext->uxCursorIndex = sizeNeededBeforeHash + encodedLen;
        pCanonicalContext->bufRemaining = SIGV4_PROCESSING_BUFFER_LENGTH - encodedLen - sizeNeededBeforeHash;

        bytesWritten = writeStringToSignPrefix( pBufStart,
                                                pAlgorithm,
                                                algorithmLen,
                                                pParams->pDateIso8601 );
        pBufStart = &( pBufStart[ bytesWritten ] );
        credentialScope.pData = pBufStart;
        credentialScope.dataLen = sizeNeededForCredentialScope( pParams );
        /* Concatenate credential scope. */
        ( void ) generateCredentialScope( pParams, &credentialScope );
        pBufStart = &( pBufStart[ credentialScope.dataLen ] );
        /* Concatenate linefeed character. */
        *pBufStart = LINEFEED_CHAR;
    }

    if( returnStatus == SigV4Success )
    {
        LogDebug( ( "Generated String To Sign Key: %.*s",
                    ( unsigned int ) ( pCanonicalContext->uxCursorIndex ),
                    pBufStart ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t generateCanonicalRequestUntilHeaders( const SigV4Parameters_t * pParams,
                                                           CanonicalContext_t * pCanonicalContext,
                                                           char ** pSignedHeaders,
                                                           size_t * pSignedHeadersLen )
{
    SigV4Status_t returnStatus = SigV4Success;
    const char * pPath = NULL;
    size_t pathLen = 0U;
    bool doubleEncodeEqualsInParmsValues = true;

    /* In presigned URL we do not want to double-encode any equals ( = ) characters in parameter values */
    if( FLAG_IS_SET( pParams->pHttpParameters->flags, SIGV4_HTTP_IS_PRESIGNED_URL ) )
    {
        doubleEncodeEqualsInParmsValues = false;
    }

    /* Set defaults for path and algorithm. */
    if( ( pParams->pHttpParameters->pPath == NULL ) ||
        ( pParams->pHttpParameters->pathLen == 0U ) )
    {
        /* If the absolute path is empty, use a forward slash (/). */
        pPath = HTTP_EMPTY_PATH;
        pathLen = HTTP_EMPTY_PATH_LEN;
    }
    else
    {
        pPath = pParams->pHttpParameters->pPath;
        pathLen = pParams->pHttpParameters->pathLen;
    }

    pCanonicalContext->uxCursorIndex = 0;
    pCanonicalContext->bufRemaining = SIGV4_PROCESSING_BUFFER_LENGTH;

    /* Write the HTTP Request Method to the canonical request. */
    returnStatus = writeLineToCanonicalRequest( pParams->pHttpParameters->pHttpMethod,
                                                pParams->pHttpParameters->httpMethodLen,
                                                pCanonicalContext );

    if( returnStatus == SigV4Success )
    {
        /* Write the URI to the canonical request. */
        if( FLAG_IS_SET( pParams->pHttpParameters->flags, SIGV4_HTTP_PATH_IS_CANONICAL_FLAG ) )
        {
            /* URI is already canonicalized, so just write it to the buffer as is. */
            returnStatus = writeLineToCanonicalRequest( pPath,
                                                        pathLen,
                                                        pCanonicalContext );
        }
        else if( ( pParams->serviceLen == S3_SERVICE_NAME_LEN ) &&
                 ( strncmp( pParams->pService, S3_SERVICE_NAME, S3_SERVICE_NAME_LEN ) == 0 ) )
        {
            /* S3 is the only service in which the URI must only be encoded once. */
            returnStatus = generateCanonicalURI( pPath, pathLen,
                                                 false /* Do not encode twice. */,
                                                 pCanonicalContext );
        }
        else
        {
            returnStatus = generateCanonicalURI( pPath, pathLen,
                                                 true /* Encode twice */,
                                                 pCanonicalContext );
        }
    }

    if( returnStatus == SigV4Success )
    {
        /* Write the query to the canonical request. */
        if( FLAG_IS_SET( pParams->pHttpParameters->flags, SIGV4_HTTP_QUERY_IS_CANONICAL_FLAG ) &&
            ( pParams->pHttpParameters->pQuery != NULL ) )
        {
            /* HTTP query is already canonicalized, so just write it to the buffer as is. */
            returnStatus = writeLineToCanonicalRequest( pParams->pHttpParameters->pQuery,
                                                        pParams->pHttpParameters->queryLen,
                                                        pCanonicalContext );
        }
        else
        {
            returnStatus = generateCanonicalQuery( pParams->pHttpParameters->pQuery,
                                                   pParams->pHttpParameters->queryLen,
                                                   doubleEncodeEqualsInParmsValues,
                                                   pCanonicalContext );
        }
    }

    if( returnStatus == SigV4Success )
    {
#ifdef ESP_PLATFORM
#endif
        /* Canonicalize original HTTP headers before writing to buffer. */
        returnStatus = generateCanonicalAndSignedHeaders( pParams->pHttpParameters->pHeaders,
                                                          pParams->pHttpParameters->headersLen,
                                                          pParams->pHttpParameters->flags,
                                                          pCanonicalContext,
                                                          pSignedHeaders,
                                                          pSignedHeadersLen );
#ifdef ESP_PLATFORM
#endif
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t generateAuthorizationValuePrefix( const SigV4Parameters_t * pParams,
                                                       const char * pAlgorithm,
                                                       size_t algorithmLen,
                                                       const char * pSignedHeaders,
                                                       size_t signedHeadersLen,
                                                       char * pAuthBuf,
                                                       size_t * pAuthPrefixLen )
{
    SigV4Status_t returnStatus = SigV4Success;
    SigV4String_t credentialScope;
    size_t authPrefixLen = 0U;
    size_t numOfBytesWritten = 0U;

    assert( pParams != NULL );
    assert( pAlgorithm != NULL );
    assert( algorithmLen > 0 );
    assert( pSignedHeaders != NULL );
    assert( signedHeadersLen > 0 );
    assert( pAuthBuf != NULL );
    assert( pAuthPrefixLen != NULL );

    /* Since the signed headers are required to be a part of final Authorization header value,
     * we copy the signed headers onto the auth buffer before continuing to generate the signature
     * in order to prevent an additional copy and/or usage of extra space. */
    size_t encodedSignatureLen = ( pParams->pCryptoInterface->hashDigestLen * 2U );

    /* Check if the authorization buffer has enough space to hold the final SigV4 Authorization header value. */
    authPrefixLen = algorithmLen + SPACE_CHAR_LEN +                                            \
                    AUTH_CREDENTIAL_PREFIX_LEN + pParams->pCredentials->accessKeyIdLen +       \
                    CREDENTIAL_SCOPE_SEPARATOR_LEN + sizeNeededForCredentialScope( pParams ) + \
                    AUTH_SEPARATOR_LEN + AUTH_SIGNED_HEADERS_PREFIX_LEN + signedHeadersLen +   \
                    AUTH_SEPARATOR_LEN + AUTH_SIGNATURE_PREFIX_LEN;

    if( *pAuthPrefixLen < ( authPrefixLen + encodedSignatureLen ) )
    {
        LogError( ( "Insufficient memory provided to write the Authorization header value, bytesExceeded=%lu",
                    ( unsigned long ) ( authPrefixLen + encodedSignatureLen - *pAuthPrefixLen ) ) );
        returnStatus = SigV4InsufficientMemory;
    }
    else
    {
        /* START:  Writing of authorization value prefix. */
        /******************* Write <algorithm> *******************************************/
        ( void ) memcpy( pAuthBuf, pAlgorithm, algorithmLen );
        numOfBytesWritten += algorithmLen;

        /* Add space separator. */
        pAuthBuf[ numOfBytesWritten ] = SPACE_CHAR;
        numOfBytesWritten += SPACE_CHAR_LEN;

        /**************** Write "Credential=<access key ID>/<credential scope>, " ****************/
        numOfBytesWritten += copyString( &( pAuthBuf[ numOfBytesWritten ] ), AUTH_CREDENTIAL_PREFIX, AUTH_CREDENTIAL_PREFIX_LEN );
        ( void ) memcpy( &( pAuthBuf[ numOfBytesWritten ] ),
                         pParams->pCredentials->pAccessKeyId,
                         pParams->pCredentials->accessKeyIdLen );
        numOfBytesWritten += pParams->pCredentials->accessKeyIdLen;

        pAuthBuf[ numOfBytesWritten ] = CREDENTIAL_SCOPE_SEPARATOR;
        numOfBytesWritten += CREDENTIAL_SCOPE_SEPARATOR_LEN;
        credentialScope.pData = &( pAuthBuf[ numOfBytesWritten ] );
        /* #authBufLen is an overestimate but the validation was already done earlier. */
        credentialScope.dataLen = *pAuthPrefixLen;
        ( void ) generateCredentialScope( pParams, &credentialScope );
        numOfBytesWritten += credentialScope.dataLen;

        /* Add separator before the Signed Headers information. */
        numOfBytesWritten += copyString( &( pAuthBuf[ numOfBytesWritten ] ), AUTH_SEPARATOR, AUTH_SEPARATOR_LEN );


        /************************ Write "SignedHeaders=<signedHeaders>, " *******************************/
        numOfBytesWritten += copyString( &( pAuthBuf[ numOfBytesWritten ] ), AUTH_SIGNED_HEADERS_PREFIX, AUTH_SIGNED_HEADERS_PREFIX_LEN );
        ( void ) memcpy( &( pAuthBuf[ numOfBytesWritten ] ), pSignedHeaders, signedHeadersLen );
        numOfBytesWritten += signedHeadersLen;

        /* Add separator before the Signature field name. */
        numOfBytesWritten += copyString( &( pAuthBuf[ numOfBytesWritten ] ), AUTH_SEPARATOR, AUTH_SEPARATOR_LEN );

        /****************************** Write "Signature=<signature>" *******************************/
        numOfBytesWritten += copyString( &( pAuthBuf[ numOfBytesWritten ] ), AUTH_SIGNATURE_PREFIX, AUTH_SIGNATURE_PREFIX_LEN );

        /* END: Writing of authorization value prefix. */

        /* Avoid warnings from last write if asserts are disabled. */
        ( void ) numOfBytesWritten;
        assert( authPrefixLen == numOfBytesWritten );
        *pAuthPrefixLen = authPrefixLen;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t generateSigningKey( const SigV4Parameters_t * pSigV4Params,
                                         HmacContext_t * pHmacContext,
                                         SigV4String_t * pSigningKey,
                                         size_t * pBytesRemaining )
{
    SigV4Status_t returnStatus = SigV4Success;
    int32_t hmacStatus = 0;
    char * pSigningKeyStart = NULL;

    assert( pSigV4Params != NULL );
    assert( pHmacContext != NULL );
    assert( pSigningKey != NULL );
    assert( pBytesRemaining != NULL );

    /* To calculate the final signing key, this function needs at least enough
     * buffer to hold the length of two digests since one digest is used to
     * calculate the other. */
    if( *pBytesRemaining < ( pSigV4Params->pCryptoInterface->hashDigestLen * 2U ) )
    {
        returnStatus = SigV4InsufficientMemory;
        LOG_INSUFFICIENT_MEMORY_ERROR( "generate signing key",
                                       ( pSigV4Params->pCryptoInterface->hashDigestLen * 2U ) - *pBytesRemaining );
    }

    if( returnStatus != SigV4InsufficientMemory )
    {
        /* Fill the key prefix, "AWS4" in the HMAC context cache.
         * The prefix is part of the key for the first round of HMAC operation:
         * HMAC("AWS4" + SecretKey, Date) */
        hmacStatus = hmacAddKey( pHmacContext,
                                 SIGV4_HMAC_SIGNING_KEY_PREFIX,
                                 SIGV4_HMAC_SIGNING_KEY_PREFIX_LEN,
                                 true /* Is key prefix. */ );

        /* The above call should always succeed as it only populates the HMAC key cache. */
        assert( hmacStatus == 0 );
    }

    if( ( returnStatus != SigV4InsufficientMemory ) && ( hmacStatus == 0 ) )
    {
        hmacStatus = completeHmac( pHmacContext,
                                   pSigV4Params->pCredentials->pSecretAccessKey,
                                   pSigV4Params->pCredentials->secretAccessKeyLen,
                                   pSigV4Params->pDateIso8601,
                                   ISO_DATE_SCOPE_LEN,
                                   pSigningKey->pData,
                                   pSigningKey->dataLen );
        *pBytesRemaining -= pSigV4Params->pCryptoInterface->hashDigestLen;
    }

    if( ( returnStatus != SigV4InsufficientMemory ) && ( hmacStatus == 0 ) )
    {
        pSigningKeyStart = &pSigningKey->pData[ pSigV4Params->pCryptoInterface->hashDigestLen + 1U ];
        hmacStatus = completeHmac( pHmacContext,
                                   pSigningKey->pData,
                                   pSigV4Params->pCryptoInterface->hashDigestLen,
                                   pSigV4Params->pRegion,
                                   pSigV4Params->regionLen,
                                   pSigningKeyStart,
                                   *pBytesRemaining );
        *pBytesRemaining -= pSigV4Params->pCryptoInterface->hashDigestLen;
    }

    if( ( returnStatus != SigV4InsufficientMemory ) && ( hmacStatus == 0 ) )
    {
        hmacStatus = completeHmac( pHmacContext,
                                   pSigningKeyStart,
                                   pSigV4Params->pCryptoInterface->hashDigestLen,
                                   pSigV4Params->pService,
                                   pSigV4Params->serviceLen,
                                   pSigningKey->pData,
                                   pSigV4Params->pCryptoInterface->hashDigestLen );
    }

    if( ( returnStatus != SigV4InsufficientMemory ) && ( hmacStatus == 0 ) )
    {
        hmacStatus = completeHmac( pHmacContext,
                                   pSigningKey->pData,
                                   pSigV4Params->pCryptoInterface->hashDigestLen,
                                   CREDENTIAL_SCOPE_TERMINATOR,
                                   CREDENTIAL_SCOPE_TERMINATOR_LEN,
                                   pSigningKeyStart,
                                   pSigV4Params->pCryptoInterface->hashDigestLen );
    }

    if( ( returnStatus != SigV4InsufficientMemory ) && ( hmacStatus == 0 ) )
    {
        pSigningKey->pData = pSigningKeyStart;
        pSigningKey->dataLen = pSigV4Params->pCryptoInterface->hashDigestLen;
    }

    /* If there was a hashing error in HMAC operations, set the appropriate error code. */
    if( hmacStatus != 0 )
    {
        returnStatus = SigV4HashError;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static SigV4Status_t writePayloadHashToCanonicalRequest( const SigV4Parameters_t * pParams,
                                                         CanonicalContext_t * pCanonicalContext )
{
    size_t encodedLen = 0U;
    SigV4Status_t returnStatus = SigV4Success;

    assert( pParams != NULL );
    assert( pCanonicalContext != NULL );

    if( FLAG_IS_SET( pParams->pHttpParameters->flags, SIGV4_HTTP_PAYLOAD_IS_HASH ) )
    {
        /* Copy the hashed payload data supplied by the user in the headers data list. */
        returnStatus = copyHeaderStringToCanonicalBuffer( pCanonicalContext->pHashPayloadLoc, pCanonicalContext->hashPayloadLen, pParams->pHttpParameters->flags, '\n', pCanonicalContext );
        /* Remove new line at the end of the payload. */
        pCanonicalContext->uxCursorIndex--;
    }
    else if( FLAG_IS_SET( pParams->pHttpParameters->flags, SIGV4_HTTP_IS_PRESIGNED_URL ) )
    {
        /* Copy the UNSIGNED-PAYLOAD data in the headers data list. */
        returnStatus = copyHeaderStringToCanonicalBuffer( "UNSIGNED-PAYLOAD", strlen( "UNSIGNED-PAYLOAD" ), pParams->pHttpParameters->flags, '\n', pCanonicalContext );
        /* Remove new line at the end of the payload. */
        pCanonicalContext->uxCursorIndex--;
    }
    else
    {
        encodedLen = pCanonicalContext->bufRemaining;
        /* Calculate hash of the request payload. */
        returnStatus = completeHashAndHexEncode( pParams->pHttpParameters->pPayload,
                                                 pParams->pHttpParameters->payloadLen,
                                                 ( char * ) &( pCanonicalContext->pBufProcessing[ pCanonicalContext->uxCursorIndex ] ),
                                                 &encodedLen,
                                                 pParams->pCryptoInterface );
        pCanonicalContext->uxCursorIndex += encodedLen;
        pCanonicalContext->bufRemaining -= encodedLen;
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

SigV4Status_t SigV4_AwsIotDateToIso8601( const char * pDate,
                                         size_t dateLen,
                                         char * pDateISO8601,
                                         size_t dateISO8601Len )
{
    SigV4Status_t returnStatus = SigV4InvalidParameter;
    SigV4DateTime_t date = { 0 };
    char * pWriteLoc = pDateISO8601;
    const char * pFormatStr = NULL;
    size_t formatLen = 0U;

    /* Check for NULL parameters. */
    if( pDate == NULL )
    {
        LogError( ( "Parameter check failed: pDate is NULL." ) );
    }
    else if( pDateISO8601 == NULL )
    {
        LogError( ( "Parameter check failed: pDateISO8601 is NULL." ) );
    }
    /* Check that the date provided is of the expected length. */
    else if( ( dateLen != SIGV4_EXPECTED_LEN_RFC_3339 ) &&
             ( dateLen != SIGV4_EXPECTED_LEN_RFC_5322 ) )
    {
        LogError( ( "Parameter check failed: dateLen must be either %u or %u, "
                    "for RFC 3339 and RFC 5322 formats, respectively.",
                    SIGV4_EXPECTED_LEN_RFC_3339,
                    SIGV4_EXPECTED_LEN_RFC_5322 ) );
    }

    /* Check that the output buffer provided is large enough for the formatted
     * string. */
    else if( dateISO8601Len < SIGV4_ISO_STRING_LEN )
    {
        LogError( ( "Parameter check failed: dateISO8601Len must be at least %u.",
                    SIGV4_ISO_STRING_LEN ) );
    }
    else
    {
        /* Assign format string according to input type received. */
        pFormatStr = ( dateLen == SIGV4_EXPECTED_LEN_RFC_3339 ) ?
                     ( FORMAT_RFC_3339 ) : ( FORMAT_RFC_5322 );

        formatLen = ( dateLen == SIGV4_EXPECTED_LEN_RFC_3339 ) ?
                    ( FORMAT_RFC_3339_LEN ) : ( FORMAT_RFC_5322_LEN );

        returnStatus = parseDate( pDate, dateLen, pFormatStr, formatLen, &date );
    }

    if( returnStatus == SigV4Success )
    {
        returnStatus = validateDateTime( &date );
    }

    if( returnStatus == SigV4Success )
    {
        /* Combine date elements into complete ASCII representation, and fill
         * buffer with result. */
        intToAscii( date.year, &pWriteLoc, ISO_YEAR_LEN );
        intToAscii( date.mon, &pWriteLoc, ISO_NON_YEAR_LEN );
        intToAscii( date.mday, &pWriteLoc, ISO_NON_YEAR_LEN );
        *pWriteLoc = 'T';
        pWriteLoc++;
        intToAscii( date.hour, &pWriteLoc, ISO_NON_YEAR_LEN );
        intToAscii( date.min, &pWriteLoc, ISO_NON_YEAR_LEN );
        intToAscii( date.sec, &pWriteLoc, ISO_NON_YEAR_LEN );
        *pWriteLoc = 'Z';

        LogDebug( ( "Successfully formatted ISO 8601 date: \"%.*s\"",
                    ( int ) dateISO8601Len,
                    pDateISO8601 ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

SigV4Status_t SigV4_GenerateHTTPAuthorization( const SigV4Parameters_t * pParams,
                                               char * pAuthBuf,
                                               size_t * authBufLen,
                                               char ** pSignature,
                                               size_t * signatureLen )
{
    SigV4Status_t returnStatus = SigV4Success;
    CanonicalContext_t *pCanonicalContext = NULL;
    const char * pAlgorithm = NULL;
    char * pSignedHeaders = NULL;
    size_t algorithmLen = 0U, signedHeadersLen = 0U, authPrefixLen = 0U;
    HmacContext_t hmacContext = { 0 };

    SigV4String_t signingKey;
    size_t uxBufferLen;

#ifdef ESP_PLATFORM
    /* Allocate CanonicalContext_t on heap to avoid stack overflow */
    pCanonicalContext = (CanonicalContext_t *)heap_caps_calloc(1, sizeof(CanonicalContext_t), MALLOC_CAP_SPIRAM);
    if (!pCanonicalContext) {
        ESP_LOGE("SIGV4", "Failed to allocate CanonicalContext_t on heap");
        return SigV4InsufficientMemory;
    }
#else
    /* For non-ESP platforms, use stack allocation */
    static CanonicalContext_t canonicalContext_static;
    pCanonicalContext = &canonicalContext_static;
#endif

    returnStatus = verifyParamsToGenerateAuthHeaderApi( pParams,
                                                        pAuthBuf, authBufLen,
                                                        pSignature, signatureLen );

    if( returnStatus == SigV4Success )
    {
        assignDefaultArguments( pParams, &pAlgorithm, &algorithmLen );
    }

    if( returnStatus == SigV4Success )
    {
        returnStatus = generateCanonicalRequestUntilHeaders( pParams, pCanonicalContext,
                                                             &pSignedHeaders,
                                                             &signedHeadersLen );
    }

    /* Hash and hex-encode the canonical request to the buffer. */
    if( returnStatus == SigV4Success )
    {
        /* Write HTTP request payload hash to the canonical request. */
        returnStatus = writePayloadHashToCanonicalRequest( pParams, pCanonicalContext );
    }

    /* Write the prefix of the Authorization header value. */
    if( returnStatus == SigV4Success )
    {
        LogDebug( ( "Generated Canonical Request: %.*s",
                    ( unsigned int ) ( pCanonicalContext->uxCursorIndex ),
                    pCanonicalContext->pBufProcessing ) );

        authPrefixLen = *authBufLen;
        returnStatus = generateAuthorizationValuePrefix( pParams,
                                                         pAlgorithm, algorithmLen,
                                                         pSignedHeaders, signedHeadersLen,
                                                         pAuthBuf, &authPrefixLen );
    }

    /* Write string to sign. */
    if( returnStatus == SigV4Success )
    {
        returnStatus = writeStringToSign( pParams, pAlgorithm, algorithmLen, pCanonicalContext );
    }

    /* Write the signing key. The is done by computing the following function
     * where the + operator means concatenation:
     * HMAC(HMAC(HMAC(HMAC("AWS4" + kSecret,pDate),pRegion),pService),"aws4_request") */
    if( returnStatus == SigV4Success )
    {
        hmacContext.pCryptoInterface = pParams->pCryptoInterface;
        signingKey.pData = ( char * ) &( pCanonicalContext->pBufProcessing[ pCanonicalContext->uxCursorIndex ] );
        signingKey.dataLen = pCanonicalContext->bufRemaining;
        returnStatus = generateSigningKey( pParams,
                                           &hmacContext,
                                           &signingKey,
                                           &pCanonicalContext->bufRemaining );
    }

    /* Use the SigningKey and StringToSign to produce the final signature.
     * Note that the StringToSign starts from the beginning of the processing buffer. */
    if( returnStatus == SigV4Success )
    {
        uxBufferLen = pCanonicalContext->uxCursorIndex;
        returnStatus = ( completeHmac( &hmacContext,
                                       signingKey.pData,
                                       signingKey.dataLen,
                                       ( char * ) pCanonicalContext->pBufProcessing,
                                       uxBufferLen,
                                       ( char * ) &( pCanonicalContext->pBufProcessing[ pCanonicalContext->uxCursorIndex ] ),
                                       pParams->pCryptoInterface->hashDigestLen ) != 0 )
                       ? SigV4HashError : SigV4Success;
    }

    /* Hex-encode the final signature beforehand to its precalculated
     * location in the buffer provided for the Authorization header value. */
    if( returnStatus == SigV4Success )
    {
        SigV4String_t originalHmac;
        SigV4String_t hexEncodedHmac;
        originalHmac.pData = ( char * ) &( pCanonicalContext->pBufProcessing[ pCanonicalContext->uxCursorIndex ] );
        originalHmac.dataLen = pParams->pCryptoInterface->hashDigestLen;
        hexEncodedHmac.pData = &( pAuthBuf[ authPrefixLen ] );
        /* #authBufLen is an overestimate but the validation was already done earlier. */
        hexEncodedHmac.dataLen = *authBufLen;
        returnStatus = lowercaseHexEncode( &originalHmac,
                                           &hexEncodedHmac );
        *pSignature = hexEncodedHmac.pData;
        *signatureLen = hexEncodedHmac.dataLen;
        *authBufLen = authPrefixLen + ( pParams->pCryptoInterface->hashDigestLen << 1U );
    }

#ifdef ESP_PLATFORM
    if (pCanonicalContext) {
        heap_caps_free(pCanonicalContext);
    }
#endif
    return returnStatus;
}

/*-----------------------------------------------------------*/

#if ( SIGV4_USE_CANONICAL_SUPPORT == 1 )

    SigV4Status_t SigV4_EncodeURI( const char * pUri,
                                   size_t uriLen,
                                   char * pCanonicalURI,
                                   size_t * canonicalURILen,
                                   bool encodeSlash,
                                   bool doubleEncodeEquals )
    {
        size_t uriIndex = 0U, bytesConsumed = 0U;
        size_t bufferLen = 0U;
        SigV4Status_t returnStatus = SigV4Success;

        assert( pUri != NULL );
        assert( pCanonicalURI != NULL );
        assert( canonicalURILen != NULL );

        bufferLen = *canonicalURILen;

        while( ( uriIndex < uriLen ) && ( returnStatus == SigV4Success ) )
        {
            if( doubleEncodeEquals && ( pUri[ uriIndex ] == '=' ) )
            {
                if( ( bufferLen - bytesConsumed ) < URI_DOUBLE_ENCODED_EQUALS_CHAR_SIZE )
                {
                    returnStatus = SigV4InsufficientMemory;
                    LOG_INSUFFICIENT_MEMORY_ERROR( "double encode '=' character in canonical query",
                                                   ( bytesConsumed + URI_DOUBLE_ENCODED_EQUALS_CHAR_SIZE - bufferLen ) );
                }
                else
                {
                    bytesConsumed += writeDoubleEncodedEquals( &( pCanonicalURI[ bytesConsumed ] ), bufferLen - bytesConsumed );
                }
            }
            else if( isAllowedChar( pUri[ uriIndex ], encodeSlash ) )
            {
                /* If the output buffer has space, add the character as-is in URI encoding as it
                 * is neither a special character nor an '=' character requiring double encoding. */
                if( bytesConsumed < bufferLen )
                {
                    pCanonicalURI[ bytesConsumed ] = pUri[ uriIndex ];
                    ++bytesConsumed;
                }
                else
                {
                    returnStatus = SigV4InsufficientMemory;
                    LogError( ( "Failed to encode URI in buffer due to insufficient memory" ) );
                }
            }
            else if( pUri[ uriIndex ] == '\0' )
            {
                /* The URI path beyond the NULL terminator is not encoded. */
                uriIndex = uriLen;
            }
            else
            {
                if( ( bufferLen - bytesConsumed ) < URI_ENCODED_SPECIAL_CHAR_SIZE )
                {
                    returnStatus = SigV4InsufficientMemory;
                    LOG_INSUFFICIENT_MEMORY_ERROR( "encode special character in canonical URI",
                                                   ( bytesConsumed + URI_ENCODED_SPECIAL_CHAR_SIZE - bufferLen ) );
                }
                else
                {
                    bytesConsumed += writeHexCodeOfChar( &( pCanonicalURI[ bytesConsumed ] ), bufferLen - bytesConsumed, pUri[ uriIndex ] );
                }
            }

            uriIndex++;
        }

        if( returnStatus == SigV4Success )
        {
            /* Set the output parameter of the number of URI encoded bytes written
             * to the buffer. */
            *canonicalURILen = bytesConsumed;
        }

        return returnStatus;
    }

#endif /* #if (SIGV4_USE_CANONICAL_SUPPORT == 1) */

/*-----------------------------------------------------------*/
