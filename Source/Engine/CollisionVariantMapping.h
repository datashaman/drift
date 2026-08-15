#pragma once

#include "Engine/SpatialWorld.h"
#include "Music/Phrase.h"

#include <array>
#include <optional>
#include <string>

namespace drift::engine
{
struct CollisionVariantRule
{
    const char* firstPhraseId;
    const char* secondPhraseId;
    const char* targetPhraseId;
};

const std::array<CollisionVariantRule, 6>& collisionVariantRules() noexcept;
const CollisionVariantRule* findCollisionVariantRule (
    const std::string& firstPhraseId,
    const std::string& secondPhraseId) noexcept;
std::optional<std::string> nextVariantId (const music::Phrase& phrase);
bool collisionContactLess (const CollisionContact& first,
                           const CollisionContact& second) noexcept;
} // namespace drift::engine
