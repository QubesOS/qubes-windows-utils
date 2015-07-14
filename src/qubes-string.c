#include <Windows.h>
#include <strsafe.h>

#include "qubes-string.h"

// NOTE: REG_MULTI_SZ is defined as:
// A sequence of null-terminated strings, terminated by an empty string (\0).
// This means that a single NULL character is a valid multi-string.

/**
 * @brief Get size, in bytes, of a multi-string (including the final null terminator).
 * @param MultiStr The input multi-string.
 * @param Count Number of non-empty strings contained in the @a MultiStr.
 * @return Size, in bytes, of @a MultiStr.
 */
ULONG
MultiStrSize(
    IN  PCHAR MultiStr,
    OUT PULONG Count OPTIONAL
    )
{
    ULONG length = 0;

    if (Count) *Count = 0;

    while (*MultiStr)
    {
        for (; *MultiStr; ++MultiStr, ++length);
        
        ++MultiStr;
        ++length;
        
        if (Count) ++(*Count);
    }
    ++length; // final NULL

    return length;
}

/**
* @brief Get size, in bytes, of a multi-wstring (including the final null terminator).
* @param MultiStr The input multi-wstring.
* @param Count Number of non-empty wstrings contained in the @a MultiStr.
* @return Size, in bytes, of @a MultiStr.
*/
ULONG
MultiWStrSize(
    IN  PWCHAR MultiStr,
    OUT PULONG Count OPTIONAL
    )
{
    ULONG length = 0;

    if (Count) *Count = 0;

    while (*MultiStr)
    {
        for (; *MultiStr; ++MultiStr, ++length);
        
        ++MultiStr;
        ++length;
        
        if (Count) ++(*Count);
    }
    ++length; // final NULL

    return length * sizeof(WCHAR);
}

/**
* @brief Add string to a multi-string.
* @param MultiStr The input multi-string, modified in-place.
* @param BufferSize Size, in bytes, of @a MultiStr buffer.
* @param Str String to add.
* @return TRUE if successful, FALSE otherwise.
*/
BOOL
MultiStrAdd(
    IN OUT PCHAR MultiStr,
    IN     ULONG BufferSize,
    IN     PCHAR Str
    )
{
    size_t strSize;
    ULONG multiStrSize;

    if (S_OK != StringCbLengthA(Str, BufferSize, &strSize))
        return FALSE;

    strSize++; // include null terminator

    // get size actually occupied by MultiStr
    multiStrSize = MultiStrSize(MultiStr, NULL);

    if (BufferSize < multiStrSize + strSize)
        return FALSE;

    memcpy(MultiStr + multiStrSize - 1, Str, strSize);
    MultiStr[multiStrSize + strSize - 1] = '\0';
    
    return TRUE;
}

/**
* @brief Add wstring to a multi-wstring.
* @param MultiStr The input multi-wstring, modified in-place.
* @param BufferSize Size, in bytes, of @a MultiStr buffer.
* @param Str Wstring to add.
* @return TRUE if successful, FALSE otherwise.
*/
BOOL
MultiWStrAdd(
    IN OUT PWCHAR MultiStr,
    IN     ULONG  BufferSize,
    IN     PWCHAR Str
    )
{
    size_t strSize;
    ULONG multiStrSize;

    if (S_OK != StringCbLengthW(Str, BufferSize, &strSize))
        return FALSE;

    strSize += sizeof(WCHAR); // include null terminator

    // get size actually occupied by MultiStr
    multiStrSize = MultiWStrSize(MultiStr, NULL);

    if (BufferSize < multiStrSize + strSize)
        return FALSE;

    memcpy(MultiStr + multiStrSize / sizeof(WCHAR) - 1, Str, strSize);
    MultiStr[(multiStrSize + strSize) / sizeof(WCHAR) - 1] = L'\0';
    
    return TRUE;
}
