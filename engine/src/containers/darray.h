/**
 * @file darray.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This files contains an implementation of a dynamic array.
 *
 * @details
 * Memory layout:
 * - uint64 capacity = number elements that can be held.
 * - uint64 length = number of elements currently contained
 * - uint64 stride = size of each element in bytes
 * - void* elements
 * @version 2.0
 * @date 2023-08-30
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include "defines.h"
#include <AvUtils/memory/avAllocator.h>

struct frame_allocator_int;

/**
 * @brief Creates a new darray of the given length and stride.
 * Note that this performs a dynamic memory allocation.
 * @note Avoid using this directly; use the darray_create macro instead.
 * @param length The default number of elements in the array.
 * @param stride The size of each array element.
 * @returns A pointer representing the block of memory containing the array.
 */
AV_API void* _darrayCreate(uint64 length, uint64 stride, uint64 initLength, AvAllocator* frameAllocator);

/**
 * @brief Resizes the given array using internal resizing amounts.
 * Causes a new allocation.
 * @note This is an internal implementation detail and should not be called directly.
 * @param array The array to be resized.
 * @returns A pointer to the resized array block.
 */
AV_API void* _darrayResize(void* array);

/**
 * @brief Pushes a new entry to the given array. Resizes if necessary.
 * @note Avoid using this directly; call the darrayPush macro instead.
 * @param array The array to be pushed to.
 * @param value_ptr A pointer to the value to be pushed. A copy of this value is taken.
 * @returns A pointer to the array block.
 */
AV_API void* _darrayPush(void* array, const void* value_ptr);

/**
 * @brief Inserts a copy of the given value into the supplied array at the given index.
 * Triggers an array resize if required.
 * @note Avoid using this directly; call the darrayInsertAt macro instead.
 * @param array The array to insert into.
 * @param index The index to insert at.
 * @param value_ptr A pointer holding the value to be inserted.
 * @returns The array block.
 */
AV_API void* _darrayInsertAt(void* array, uint64 index, void* value_ptr);

/** @brief The default darray capacity. */
#define DARRAY_DEFAULT_CAPACITY 1

/** @brief The default resize factor (doubles on resize) */
#define DARRAY_RESIZE_FACTOR 2

/**
 * @brief Creates a new darray of the given type with the default capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @returns A pointer to the array's memory block.
 */
#define darrayCreate(type) \
    _darrayCreate(DARRAY_DEFAULT_CAPACITY, sizeof(type), 0, 0)

/**
 * @brief Creates a new darray of the given type with the default capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param allocator A pointer to a frame allocator.
 * @returns A pointer to the array's memory block.
 */
#define darrayCreateWithAllocator(type, allocator) \
    _darrayCreate(DARRAY_DEFAULT_CAPACITY, sizeof(type), 0, allocator)

#define darrayCreateSized(type, size) \
    _darrayCreate((size), sizeof(type), (size), 0)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @returns A pointer to the array's memory block.
 */
#define darrayReserve(type, capacity) \
    _darrayCreate(capacity, sizeof(type), 0, 0)

/**
 * @brief Creates a new darray of the given type with the provided capacity.
 * Performs a dynamic memory allocation.
 * @param type The type to be used to create the darray.
 * @param capacity The number of elements the darray can initially hold (can be resized).
 * @param allocator A pointer to a frame allocator.
 * @returns A pointer to the array's memory block.
 */
#define darrayReserveWithAllocator(type, capacity, allocator) \
    _darrayCreate(capacity, sizeof(type), 0, allocator)

/**
 * @brief Destroys the provided array, freeing any memory allocated by it.
 * @param array The array to be destroyed.
 */
AV_API void darrayDestroy(void* array);

/**
 * @brief Pushes a new entry to the given array. Resizes if necessary.
 * @param array The array to be pushed to.
 * @param value The value to be pushed. A copy of this value is taken.
 * @returns A pointer to the array block.
 */
#define darrayPush(array, value)           \
    {                                       \
        typeof(value) temp = value;         \
        array = _darrayPush(array, &temp); \
    }
// NOTE: could use __auto_type for temp above, but intellisense
// for VSCode flags it as an unknown type. typeof() seems to
// work just fine, though. Both are GNU extensions.

/**
 * @brief Pops an entry out of the array and places it into dest.
 * @param array The array to pop from.
 * @param dest A pointer to hold the popped value.
 */
AV_API void darrayPop(void* array, void* value_ptr);

/**
 * @brief Inserts a copy of the given value into the supplied array at the given index.
 * Triggers an array resize if required.
 * @param array The array to insert into.
 * @param index The index to insert at.
 * @param value_ptr A pointer holding the value to be inserted.
 * @returns The array block.
 */
#define darrayInsertAt(array, index, value)           \
    {                                                   \
        typeof(value) temp = value;                     \
        array = _darrayInsertAt(array, index, &temp); \
    }

/**
 * @brief Pops an entry out of the array at the given index and places it into dest.
 * Brings in all entries after the popped index in by one.
 * @param array The array to pop from.
 * @param index The index to pop from.
 * @param dest A pointer to hold the popped value.
 * @returns The array block.
 */
AV_API void* darrayPopAt(void* array, uint64 index, void* value_ptr);

/**
 * @brief Clears all entries from the array. Does not release any internally-allocated memory.
 * @param array The array to be cleared.
 */
AV_API void darrayClear(void* array);

/**
 * @brief Gets the given array's capacity.
 * @param array The array whose capacity to retrieve.
 * @returns The capacity of the given array.
 */
AV_API uint64 darrayCapacity(void* array);

/**
 * @brief Gets the length (number of elements) in the given array.
 * @param array The array to obtain the length of.
 * @returns The length of the given array.
 */
AV_API uint64 darrayLength(void* array);

/**
 * @brief Gets the stride (element size) of the given array.
 * @param array The array to obtain the stride of.
 * @returns The stride of the given array.
 */
AV_API uint64 darrayStride(void* array);

/**
 * @brief Sets the length of the given array. This ensures the array has the
 * required capacity to be able to set entries directly, for instance. Can trigger
 * an internal reallocation.
 * @param array The array to set the length of.
 * @param value The length to set the array to.
 */
AV_API void darrayLengthSet(void* array, uint64 value);