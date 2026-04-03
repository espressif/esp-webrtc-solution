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
 * @file sigv4_quicksort.h
 * @brief Declaration of Quicksort function for the SigV4 Library.
 */

#ifndef SIGV4_QUICKSORT_H_
#define SIGV4_QUICKSORT_H_

/* Standard includes. */
#include <stdint.h>
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

/**
 * @brief The comparison function used for sorting.
 * @param[in] pFirstVal The first value to compare
 * @param[in] pSecondVal The second value to compare
 *
 * @return A value less than 0 if @p pFirstVal is less than
 * @p pSecondVal. Otherwise, greater than 0.
 */
typedef int32_t ( * ComparisonFunc_t )( const void * pFirstVal,
                                        const void * pSecondVal );

/**
 * @brief Perform quicksort on an array.
 *
 * @param[in] pArray The array to be sorted.
 * @param[in] numItems The number of items in an array.
 * @param[in] itemSize The amount of memory per entry in the array.
 * @param[out] comparator The comparison function to determine if one item is less than another.
 */
void quickSort( void * pArray,
                size_t numItems,
                size_t itemSize,
                ComparisonFunc_t comparator );

/* *INDENT-OFF* */
#ifdef __cplusplus
    }
#endif
/* *INDENT-ON* */

#endif /* ifndef SIGV4_QUICKSORT_H_ */
