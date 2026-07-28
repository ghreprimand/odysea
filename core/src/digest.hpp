// OdySea core internals: message digest used for cache file naming.
//
// Not part of the installed interface. This header lives beside the core
// sources so tests can reach it directly and so the public headers stay free of
// implementation detail.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace odysea::core::detail {

/// RFC 1321 MD5 digest of `data`.
///
/// This exists for one reason: the shared thumbnail cache names its files after
/// the MD5 of the source URI, so interoperating with other desktops requires
/// exactly this function. It is a naming scheme, never a security primitive.
/// MD5 is not collision resistant, so anything looked up by a name derived here
/// must also verify the source it claims to describe rather than trusting the
/// name alone.
[[nodiscard]] std::array<std::uint8_t, 16> md5(std::string_view data);

/// The digest of `data` as 32 lowercase hexadecimal characters.
[[nodiscard]] std::string md5_hex(std::string_view data);

} // namespace odysea::core::detail
