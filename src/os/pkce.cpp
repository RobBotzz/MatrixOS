// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/pkce.h"

#include <random>
#include <vector>

namespace matrixos
{
namespace
{

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data)
{
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::vector<std::uint8_t> message(data.begin(), data.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    message.push_back(0x80);
    while (message.size() % 64 != 56)
    {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bits >> shift));
    }

    for (std::size_t block = 0; block < message.size(); block += 64)
    {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
        {
            const std::uint8_t *p = &message[block + static_cast<std::size_t>(i) * 4];
            w[i] = static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
                   static_cast<std::uint32_t>(p[2]) << 8 | p[3];
        }
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::array<std::uint32_t, 8> v = h;
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + choice + kRound[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + majority;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; ++i)
        {
            h[i] += v[i];
        }
    }

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            digest[static_cast<std::size_t>(i * 4 + j)] =
                static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
        }
    }
    return digest;
}

std::string base64Url(std::string_view data)
{
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const unsigned char c : data)
    {
        buffer = buffer << 8 | c;
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            out += kAlphabet[(buffer >> bits) & 0x3f];
        }
    }
    if (bits > 0)
    {
        out += kAlphabet[(buffer << (6 - bits)) & 0x3f];
    }
    return out;
}

std::optional<std::string> base64UrlDecode(std::string_view text)
{
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text)
    {
        int value = -1;
        if (c >= 'A' && c <= 'Z')
        {
            value = c - 'A';
        }
        else if (c >= 'a' && c <= 'z')
        {
            value = c - 'a' + 26;
        }
        else if (c >= '0' && c <= '9')
        {
            value = c - '0' + 52;
        }
        else if (c == '-')
        {
            value = 62;
        }
        else if (c == '_')
        {
            value = 63;
        }
        if (value < 0)
        {
            return std::nullopt;
        }
        buffer = buffer << 6 | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    return out;
}

std::string randomToken()
{
    static constexpr std::string_view kUnreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::random_device random;
    std::uniform_int_distribution<std::size_t> pick(0, kUnreserved.size() - 1);
    std::string token;
    for (int i = 0; i < 64; ++i)
    {
        token += kUnreserved[pick(random)];
    }
    return token;
}

std::string codeChallenge(std::string_view verifier)
{
    const std::array<std::uint8_t, 32> digest = sha256(verifier);
    return base64Url(
        std::string_view(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

} // namespace matrixos
