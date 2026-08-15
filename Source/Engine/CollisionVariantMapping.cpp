#include "Engine/CollisionVariantMapping.h"

#include <algorithm>
#include <tuple>

namespace drift::engine
{
namespace
{
constexpr std::array<CollisionVariantRule, 6> rules {{
    { "bass", "chords", "chords" },
    { "bass", "drums", "bass" },
    { "bass", "melody", "bass" },
    { "chords", "drums", "drums" },
    { "chords", "melody", "chords" },
    { "drums", "melody", "melody" },
}};

std::pair<std::string, std::string> orderedPair (const std::string& firstPhraseId,
                                                  const std::string& secondPhraseId)
{
    if (firstPhraseId <= secondPhraseId)
        return { firstPhraseId, secondPhraseId };
    return { secondPhraseId, firstPhraseId };
}
} // namespace

const std::array<CollisionVariantRule, 6>& collisionVariantRules() noexcept
{
    return rules;
}

const CollisionVariantRule* findCollisionVariantRule (
    const std::string& firstPhraseId,
    const std::string& secondPhraseId) noexcept
{
    const auto [orderedFirst, orderedSecond] = orderedPair (firstPhraseId, secondPhraseId);
    const auto rule = std::find_if (rules.begin(), rules.end(), [&] (const auto& candidate) {
        return orderedFirst == candidate.firstPhraseId
               && orderedSecond == candidate.secondPhraseId;
    });
    return rule == rules.end() ? nullptr : &*rule;
}

std::optional<std::string> nextVariantId (const music::Phrase& phrase)
{
    if (phrase.variants.empty())
        return std::nullopt;

    const auto current = std::find_if (
        phrase.variants.begin(), phrase.variants.end(), [&] (const auto& variant) {
            return variant.id == phrase.currentVariantId;
        });
    if (current == phrase.variants.end())
        return std::nullopt;

    const auto next = std::next (current) == phrase.variants.end()
                          ? phrase.variants.begin()
                          : std::next (current);
    return next->id;
}

bool collisionContactLess (const CollisionContact& first,
                           const CollisionContact& second) noexcept
{
    return std::tie (first.firstPhraseId, first.secondPhraseId)
           < std::tie (second.firstPhraseId, second.secondPhraseId);
}
} // namespace drift::engine
