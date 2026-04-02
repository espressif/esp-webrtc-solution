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
 * @file sigv4_quicksort.c
 * @brief Implements an Iterative Quicksort Algorithm for the SigV4 Library.
 */

#include "sigv4_quicksort.h"

#include <string.h>
#include <assert.h>

/**
 * @brief Push a value to the stack.
 */
#define PUSH_STACK( valueToPush, stack, index )   \
    {                                             \
        ( stack )[ ( index ) ] = ( valueToPush ); \
        ++( index );                              \
    }

/**
 * @brief Pop a value from the stack.
 */
#define POP_STACK( valueToPop, stack, index )    \
    {                                            \
        --( index );                             \
        ( valueToPop ) = ( stack )[ ( index ) ]; \
    }

/*-----------------------------------------------------------*/

/**
 * @brief A helper function to swap the value of two pointers
 * given their sizes.
 *
 * @param[in] pFirstItem The item to swap with @p pSecondItem.
 * @param[in] pSecondItem The item to swap with @p pFirstItem.
 * @param[in] itemSize The amount of memory per entry in the array.
 */
static void swap( void * pFirstItem,
                  void * pSecondItem,
                  size_t itemSize );

/**
 * @brief A helper function to perform quicksort on a subarray.
 *
 * @param[in] pArray The array to be sorted.
 * @param[in] low The low index of the array.
 * @param[in] high The high index of the array.
 * @param[in] itemSize The amount of memory per entry in the array.
 * @param[out] comparator The comparison function to determine if one item is less than another.
 */
static void quickSortHelper( void * pArray,
                             size_t low,
                             size_t high,
                             size_t itemSize,
                             ComparisonFunc_t comparator );

/**
 * @brief A helper function to partition a subarray using the last element
 * of the array as the pivot. All items smaller than the pivot end up
 * at its left while all items greater than end up at its right.
 *
 * @param[in] pArray The array to be sorted.
 * @param[in] low The low index of the array.
 * @param[in] high The high index of the array.
 * @param[in] itemSize The amount of memory per entry in the array.
 * @param[out] comparator The comparison function to determine if one item is less than another.
 *
 * @return The index of the pivot
 */
static size_t partition( void * pArray,
                         size_t low,
                         size_t high,
                         size_t itemSize,
                         ComparisonFunc_t comparator );

/*-----------------------------------------------------------*/

static void swap( void * pFirstItem,
                  void * pSecondItem,
                  size_t itemSize )
{
    uint8_t * pFirstByte = pFirstItem;
    uint8_t * pSecondByte = pSecondItem;
    size_t dataSize = itemSize;

    assert( pFirstItem != NULL );
    assert( pSecondItem != NULL );

    /* Swap one byte at a time. */
    while( dataSize-- > 0U )
    {
        uint8_t tmp = *pFirstByte;
        *pFirstByte = *pSecondByte;
        ++pFirstByte;
        *pSecondByte = tmp;
        ++pSecondByte;
    }
}

static void quickSortHelper( void * pArray,
                             size_t low,
                             size_t high,
                             size_t itemSize,
                             ComparisonFunc_t comparator )
{
    size_t stack[ SIGV4_WORST_CASE_SORT_STACK_SIZE ];

    /* Low and high are first two items on the stack. Note
     * that we use an intermediary variable for MISRA compliance. */
    size_t top = 0U, lo = low, hi = high;

    PUSH_STACK( lo, stack, top );
    PUSH_STACK( hi, stack, top );

    while( top > 0U )
    {
        size_t partitionIndex;
        size_t len1, len2;
        POP_STACK( hi, stack, top );
        POP_STACK( lo, stack, top );

        partitionIndex = partition( pArray, lo, hi, itemSize, comparator );

        /* Calculate length of the left partition containing items smaller
         * than the pivot element.
         * The length is zero if either:
         * 1. The pivoted item is the smallest in the the array before partitioning.
         *              OR
         * 2. The left partition is only of single length which can be treated as
         * sorted, and thus, of zero length for avoided adding to the stack. */
        len1 = ( ( partitionIndex != 0U ) && ( ( partitionIndex - 1U ) > lo ) ) ? ( partitionIndex - lo ) : 0U;

        /* Calculate length of the right partition containing items greater than
         * or equal to the pivot item.
         * The calculated length is zero if either:
         * 1. The pivoted item is the greatest in the the array before partitioning.
         *              OR
         * 2. The right partition contains only a single length which can be treated as
         * sorted, and thereby, of zero length to avoid adding to the stack. */
        len2 = ( ( partitionIndex + 1U ) < hi ) ? ( hi - partitionIndex ) : 0U;

        /* Push the information of the left and right partitions to the stack.
         * Note: For stack space optimization, the larger of the partitions is pushed
         * first and the smaller is pushed later so that the smaller part of the tree
         * is completed first without increasing stack space usage before coming back
         * to the larger partition. */
        if( len1 > len2 )
        {
            PUSH_STACK( lo, stack, top );
            PUSH_STACK( partitionIndex - 1U, stack, top );

            if( len2 > 0U )
            {
                PUSH_STACK( partitionIndex + 1U, stack, top );
                PUSH_STACK( hi, stack, top );
            }
        }
        else
        {
            if( len2 > 0U )
            {
                PUSH_STACK( partitionIndex + 1U, stack, top );
                PUSH_STACK( hi, stack, top );
            }

            if( len1 > 0U )
            {
                PUSH_STACK( lo, stack, top );
                PUSH_STACK( partitionIndex - 1U, stack, top );
            }
        }
    }
}

static size_t partition( void * pArray,
                         size_t low,
                         size_t high,
                         size_t itemSize,
                         ComparisonFunc_t comparator )
{
    uint8_t * pivot;
    uint8_t * pArrayLocal = ( uint8_t * ) pArray;
    size_t i = low - 1U, j = low;

    assert( pArray != NULL );
    assert( comparator != NULL );

    /* Choose pivot as the highest indexed item in the current partition. */
    pivot = &( pArrayLocal[ high * itemSize ] );

    /* Iterate over all elements of the current array to partition it
     * in comparison to the chosen pivot with smaller items on the left
     * and larger or equal to items on the right. */
    for( ; j < high; j++ )
    {
        /* Use comparator function to check current element is smaller than the pivot */
        if( comparator( &( pArrayLocal[ j * itemSize ] ), pivot ) < 0 )
        {
            ++i;
            swap( &( pArrayLocal[ i * itemSize ] ), &( pArrayLocal[ j * itemSize ] ), itemSize );
        }
    }

    /* Place the pivot between the smaller and larger item chunks of
     * the array. This represents the 2 partitions of the array. */
    swap( &( pArrayLocal[ ( i + 1U ) * itemSize ] ), pivot, itemSize );

    /* Return the pivot item's index. */
    return i + 1U;
}

void quickSort( void * pArray,
                size_t numItems,
                size_t itemSize,
                ComparisonFunc_t comparator )
{
    assert( pArray != NULL );
    assert( numItems > 0U );
    assert( itemSize > 0U );
    assert( comparator != NULL );

    quickSortHelper( pArray, 0U, numItems - 1U, itemSize, comparator );
}
