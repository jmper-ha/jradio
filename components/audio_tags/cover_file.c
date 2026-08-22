#include "cover_file.h"

#include <string.h>

/* Its own rather than tolower(): that one answers to the C locale, and a name
 * off a FAT drive is bytes, not text in whatever locale the build happened to
 * pick up. Only ASCII is folded, which is all "cover" needs. */
static char lowered(char character)
{
    return (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') : character;
}

static bool equals_ignoring_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (lowered(*left) != lowered(*right)) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

bool cover_file_name_matches(const char *name)
{
    if (name == NULL) return false;
    static const char stem[] = "cover";
    const size_t stem_length = sizeof(stem) - 1U;
    for (size_t index = 0U; index < stem_length; ++index) {
        if (lowered(name[index]) != stem[index]) return false;
    }
    if (name[stem_length] != '.') return false;

    // The extension is what is left, and it has to be the whole of it:
    // "cover.png.bak" is not a picture.
    const char *extension = name + stem_length + 1U;
    return equals_ignoring_case(extension, "jpg") || equals_ignoring_case(extension, "jpeg") ||
           equals_ignoring_case(extension, "png");
}
