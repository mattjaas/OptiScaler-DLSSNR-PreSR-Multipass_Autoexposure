#pragma once
#include <cstdint>

namespace DlssNr::PrivateModel
{
constexpr unsigned NoDlss = 0;
inline bool Enabled(unsigned preference) { return preference != NoDlss; }

enum Family : uint32_t
{
    Cnn = 0,
    Transformer = 1,
    Transformer2 = 2
};
inline Family FromPreference(unsigned p) { return p == 12 || p == 13 ? Transformer2 : p == 11 ? Transformer : Cnn; }
inline Family FromPreset(unsigned p)
{
    return p == 12 || p == 13 ? Transformer2 : p == 10 || p == 11 ? Transformer : Cnn;
}
inline unsigned Preference(unsigned p)
{
    const auto f = FromPreference(p);
    return f == Transformer2 ? 13u : f == Transformer ? 11u : 1u;
}
} // namespace DlssNr::PrivateModel
