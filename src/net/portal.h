// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/http_server.h"
#include "os/identity.h"

#include <string>

namespace matrixos
{

class Provisioning;

/// The two web pages of the device, and the API under them (ADR-0014).
///
/// `GET /` answers differently depending on where the request can possibly have
/// come from: while the device is its own access point, the visitor is inside a
/// captive-portal WebView and gets the plain server-rendered setup page; once it
/// is on a network, the visitor typed `matrixos-xxxx.local` into a real browser
/// and gets the React configuration page.
class Portal
{
public:
    /// Where NetworkManager's shared mode puts us. Captive-portal probes are
    /// redirected here by address, because the name they asked for is not ours.
    static constexpr const char *kAccessPointAddress = "10.42.0.1";

    Portal(Provisioning &provisioning, Identity identity, std::string version, std::string commit);

    void install(HttpServer &server);

    /// Exposed for the tests: the page a given state produces, without a socket.
    std::string setupPage() const;
    std::string statusJson() const;

private:
    HttpResponse index(const HttpRequest &request) const;
    HttpResponse connect(const HttpRequest &request);
    HttpResponse captive(const HttpRequest &request) const;

    Provisioning &provisioning_;
    Identity identity_;
    std::string version_;
    std::string commit_;
};

} // namespace matrixos
