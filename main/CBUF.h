/****************************************************************************
*
*   Since this code originated from code which is public domain, I
*   hereby declare this code to be public domain as well.
*
*   Dave Hylands - dhylands@gmail.com
*
****************************************************************************/
/**
*
*   @file   CBUF.h
*
*   @defgroup   CBUF Circular Buffer
*   @{
*
*   @brief  A simple and efficient set of circular buffer manipulations.
*
*   These macros implement a circular buffer which employs get and put
*   pointers, in such a way that mutual exclusion is not required
*   (assumes one reader & one writer).
*
*   It requires that the circular buffer size be a power of two, and the
*   size of the buffer needs to smaller than the index. So an 8 bit index
*   supports a circular buffer upto (1 << 7) = 128 entries, and a 16 bit index
*   supports a circular buffer upto (1 << 15) = 32768 entries.
*
*   The basis for these routines came from an article in Jack Ganssle's
*   Embedded Muse: http://www.ganssle.com/tem/tem110.pdf
*
*   The structure which defines the circular buffer needs to have 3 members
*   <tt>m_get_idx, m_put_idx,</tt> and @c m_entry, and m_entry needs to be
*   an array of elements rather than a pointer to an array of elements.
*
*   @c m_get_idx and @c m_put_idx need to be unsigned integers of the same size.
*
*   @c m_entry needs to be an array of entries. The type of each entry is
*   entirely up to the caller.
*
*   @code
*   struct
*   {
*       volatile uint8_t     m_get_idx;
*       volatile uint8_t     m_put_idx;
*                uint8_t     m_entry[ 64 ];
*
*   } myQ;
*   @endcode
*
*   You could then use CBUF_Push to add a character to the circular buffer:
*
*   @code
*   CBUF_Push(myQ, 'x');
*   @endcode
*
*   And CBUF_Pop to retrieve an element from the buffer:
*
*   @code
*   ch = CBUF_Pop(myQ);
*   @endcode
*
****************************************************************************/

#if !defined(CBUF_H)
#define CBUF_H

/* ---- Include Files ---------------------------------------------------- */

/* ---- Constants and Types ---------------------------------------------- */

/**
*   Initializes the circular buffer for use.
*/

#define CBUF_Init(cbuf)             cbuf.m_get_idx = cbuf.m_put_idx = 0

/**
*   Returns the number of elements which are currently
*   contained in the circular buffer.
*/

#define CBUF_Len(cbuf)              ((typeof(cbuf.m_put_idx))((cbuf.m_put_idx) - (cbuf.m_get_idx)))

/**
*   Returns the size of the buffer (in entries)
*/
#define CBUF_Size(cbuf)             (sizeof(cbuf.m_entry) / sizeof(cbuf.m_entry[0]))

/**
*   Returns the number of unused entries which are currently in
*   the buffer.
*/
#define CBUF_Space(cbuf)            (CBUF_Size(cbuf) - CBUF_Len(cbuf))

/**
*   Determines the mask used to extract the real get & put
*   pointers.
*/
#define CBUF_Mask(cbuf)             (CBUF_Size(cbuf) - 1)

/**
*   Determines if the get and put pointers are "wrapped".
*/
#define CBUF_Wrapped(cbuf)          (((cbuf.m_put_idx ^ cbuf.m_get_idx) & ~CBUF_Mask(cbuf)) != 0)

/**
*   Returns the number of contiguous entries which can be
*   retrieved from the buffer.
*/
#define CBUF_ContigLen(cbuf)        (CBUF_Wrapped(cbuf) ? (CBUF_Size(cbuf) - (cbuf.m_get_idx & CBUF_Mask(cbuf))) : CBUF_Len(cbuf))

/**
*   Returns the number of contiguous entries which can be placed
*   into the buffer.
*/
#define CBUF_ContigSpace(cbuf)      (CBUF_Wrapped(cbuf) ? CBUF_Space(cbuf) : (CBUF_Size(cbuf) - (cbuf.m_put_idx & CBUF_Mask(cbuf))))

/**
*   Appends an element to the end of the circular buffer. The
*   element is expected to be of the same type as the @c m_entry
*   member.
*/

#define CBUF_Push(cbuf, elem) do { \
    (cbuf.m_entry)[cbuf.m_put_idx & CBUF_Mask(cbuf)] = (elem); \
    cbuf.m_put_idx++; \
} while (0)

/**
*   Retrieves an element from the beginning of the circular buffer
*/

#define CBUF_Pop(cbuf) ({ \
    __typeof__(cbuf.m_entry[0]) _elem = (cbuf.m_entry)[cbuf.m_get_idx & CBUF_Mask(cbuf)]; \
    cbuf.m_get_idx++; \
    _elem; })

/**
*   Returns a pointer to the last spot that was pushed.
*/

#define CBUF_GetLastEntryPtr(cbuf)  &(cbuf.m_entry)[ (cbuf.m_put_idx - 1) & CBUF_Mask(cbuf)]

/**
*   Returns a pointer to the next spot to push. This can be used
*   in conjunction with CBUF_AdvancePushIdx to fill out an entry
*   before indicating that it's available. It is the caller's
*   responsibility to enure that space is available, and that no
*   other items are pushed to overwrite the entry returned.
*/

#define CBUF_GetPushEntryPtr(cbuf)  &(cbuf.m_entry)[ cbuf.m_put_idx & CBUF_Mask(cbuf)]

/**
*   Advances the put index. This is useful if you need to
*   reserve space for an item but can't fill in the contents
*   yet. CBUG_GetLastEntryPtr can be used to get a pointer to
*   the item. It is the caller's responsibility to ensure that
*   the item isn't popped before the contents are filled in.
*/

#define CBUF_AdvancePushIdx(cbuf)           cbuf.m_put_idx++
#define CBUF_AdvancePushIdxBy(cbuf, len)    cbuf.m_put_idx += (len)

/**
*   Advances the get index. This is slightly more efficient than
*   popping and tossing the result.
*/

#define CBUF_AdvancePopIdx(cbuf)            cbuf.m_get_idx++
#define CBUF_AdvancePopIdxBy(cbuf, len)     cbuf.m_get_idx += (len)

/**
*   Retrieves the <tt>idx</tt>'th element from the beginning of
*   the circular buffer
*/

#define CBUF_Get(cbuf, idx)         (cbuf.m_entry)[(cbuf.m_get_idx + idx) & CBUF_Mask(cbuf)]

/**
*   Retrieves the <tt>idx</tt>'th element from the end of the
*   circular buffer.
*/

#define CBUF_GetEnd(cbuf, idx)      (cbuf.m_entry)[(cbuf.m_put_idx - idx - 1) & CBUF_Mask(cbuf)]

/**
*   Returns a pointer to the next spot to push.
*/

#define CBUF_GetPopEntryPtr(cbuf)   &(cbuf.m_entry)[cbuf.m_get_idx & CBUF_Mask(cbuf)]

/**
*   Determines if the circular buffer is empty.
*/

#define CBUF_IsEmpty(cbuf)          (CBUF_Len(cbuf) == 0)

/**
*   Determines if the circular buffer is full.
*/

#define CBUF_IsFull(cbuf)           (CBUF_Space(cbuf) == 0)

/**
*   Determines if the circular buffer is currenly overflowed or underflowed.
*/

#define CBUF_Error(cbuf)            (CBUF_Len(cbuf) > CBUF_Size(cbuf))

/* ---- Variable Externs ------------------------------------------------- */
/* ---- Function Prototypes ---------------------------------------------- */

/** @} */

#endif // CBUF_H
