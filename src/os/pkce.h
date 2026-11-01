// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace matrixos
{

/// FIPS 180-4. Only for the PKCE challenge (RFC 7636), which is why there is no
/// streaming interface.
std::array<std::uint8_t, 32> sha256(std::string_view data);

/// RFC 4648 §5, without padding.
std::string base64Url(std::string_view data);

/// The inverse; anything outside the alphabet is `nullopt`.
std::optional<std::string> base64UrlDecode(std::string_view text);

/// 64 characters from the unreserved set, from `std::random_device`.
std::string randomToken();

/// `S256`: base64url(sha256(verifier)).
std::string codeChallenge(std::string_view verifier);

} // namespace matrixos
