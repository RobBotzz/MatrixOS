// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/identity.h"

#include <unistd.h>

#include <cctype>
#include <fstream>
#include <sstream>

namespace matrixos
{
namespace
{

constexpr std::string_view kFallbackSuffix = "0000";

} // namespace

std::string serialSuffix(std::string_view cpuinfo)
{
    while (!cpuinfo.empty())
    {
        const auto end = cpuinfo.find('\n');
        const std::string_view line = cpuinfo.substr(0, end);

        if (line.starts_with("Serial"))
        {
            const auto colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view value = line.substr(colon + 1);
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                {
                    value.remove_prefix(1);
                }
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                {
                    value.remove_suffix(1);
                }

                if (value.size() >= 4)
                {
                    std::string suffix(value.substr(value.size() - 4));
                    for (char &c : suffix)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    return suffix;
                }
            }
        }

        if (end == std::string_view::npos)
        {
            break;
        }
        cpuinfo.remove_prefix(end + 1);
    }

    return {};
}

Identity deviceIdentity()
{
    std::string suffix;

    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo)
    {
        std::ostringstream contents;
        contents << cpuinfo.rdbuf();
        suffix = serialSuffix(contents.str());
    }

    if (suffix.empty())
    {
        suffix = kFallbackSuffix;
    }

    Identity identity;
    identity.suffix = suffix;
    identity.access_point = "MatrixOS-" + suffix;

    // The system hostname wins when it is set, because that is what mDNS
    // actually answers to (FR-36) — the derived name is what provision.sh put
    // there in the first place.
    char name[256] = {};
    if (::gethostname(name, sizeof(name) - 1) == 0 && name[0] != '\0')
    {
        identity.hostname = name;
    }
    else
    {
        identity.hostname = "matrixos-" + suffix;
    }

    return identity;
}

} // namespace matrixos
