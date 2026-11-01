// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/http_types.h"

#include <cctype>
#include <cstdio>

namespace matrixos
{

std::string percentEncode(std::string_view text)
{
    std::string out;
    for (const unsigned char c : text)
    {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += static_cast<char>(c);
        }
        else
        {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            out += escaped;
        }
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields)
{
    std::string out;
    for (const auto &[name, value] : fields)
    {
        if (!out.empty())
        {
            out += '&';
        }
        out += percentEncode(name) + "=" + percentEncode(value);
    }
    return out;
}

} // namespace matrixos
