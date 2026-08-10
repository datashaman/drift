#include "Music/Phrase.h"

namespace drift::music
{
Phrase makeBassPhrase()
{
    return {
        "bass",
        4.0,
        1,
        {
            { 0.0, 36, 100, 0.75 },
            { 1.5, 36, 88, 0.25 },
            { 2.0, 31, 96, 0.75 },
            { 3.0, 34, 92, 0.5 },
        },
    };
}
} // namespace drift::music
