/* ASCII conversion hooks required by FatFs when long-file-name support is
 * enabled. All firmware-generated SD filenames are intentionally ASCII. */
#include "../ff.h"

#if _USE_LFN

WCHAR ff_convert(WCHAR chr, UINT dir)
{
    (void)dir;
    return (chr < 0x80U) ? chr : 0U;
}

WCHAR ff_wtoupper(WCHAR chr)
{
    if (chr >= (WCHAR)'a' && chr <= (WCHAR)'z')
        chr -= (WCHAR)('a' - 'A');
    return chr;
}

#endif
