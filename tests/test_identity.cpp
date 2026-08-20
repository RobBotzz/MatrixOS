// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "os/identity.h"

#include <catch2/catch_test_macros.hpp>

using namespace matrixos;

namespace
{

/// Trimmed from a real Pi Zero 2 W. The serial is the last line, which is where
/// the Raspberry Pi firmware puts it.
constexpr const char *kPiCpuinfo = R"(processor	: 0
BogoMIPS	: 108.00
Features	: fp asimd evtstrm crc32 cpuid
CPU implementer	: 0x41
CPU architecture: 8
CPU variant	: 0x0
CPU part	: 0xd03
CPU revision	: 4

Hardware	: BCM2835
Revision	: 902120
Serial		: 00000000a3f1b2c4
Model		: Raspberry Pi Zero 2 W Rev 1.0
)";

} // namespace

TEST_CASE("the identity suffix is the last four characters of the CPU serial")
{
    CHECK(serialSuffix(kPiCpuinfo) == "b2c4");
}

TEST_CASE("the suffix is lowercased, so the two units never differ by case alone")
{
    CHECK(serialSuffix("Serial\t\t: 00000000DEADBEEF\n") == "beef");
}

TEST_CASE("a machine without a serial gets no suffix rather than a wrong one")
{
    CHECK(serialSuffix("processor\t: 0\nmodel name\t: AMD Ryzen\n").empty());
    CHECK(serialSuffix("").empty());
}

TEST_CASE("a truncated or empty serial line is not mistaken for an identity")
{
    CHECK(serialSuffix("Serial\t\t: abc\n").empty());
    CHECK(serialSuffix("Serial\t\t: \n").empty());
    CHECK(serialSuffix("Serial\n").empty());
}

TEST_CASE("a serial on the last line, without a trailing newline, still counts")
{
    CHECK(serialSuffix("Hardware\t: BCM2835\nSerial\t\t: 00000000a3f1b2c4") == "b2c4");
}

TEST_CASE("the device names itself from the suffix")
{
    // deviceIdentity() reads the real /proc/cpuinfo, so only the shape can be
    // asserted here — the derivation itself is what the cases above cover.
    const Identity identity = deviceIdentity();

    CHECK(identity.suffix.size() == 4);
    CHECK(identity.access_point == "MatrixOS-" + identity.suffix);
    CHECK_FALSE(identity.hostname.empty());
}
