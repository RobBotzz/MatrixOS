// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/portal.h"

#include "net/web_assets.h"
#include "os/log.h"
#include "os/provisioning.h"

#include <utility>

namespace matrixos
{
namespace
{

/// Everything an operating system fetches to decide whether a network has
/// internet access. Answering these with a redirect is what makes the phone open
/// the setup page by itself instead of showing "no internet" and giving up.
constexpr const char *kCaptiveProbes[] = {
    "/generate_204",        // Android
    "/gen_204",             // Android, older
    "/hotspot-detect.html", // iOS, macOS
    "/library/test/success.html",
    "/connecttest.txt", // Windows
    "/ncsi.txt",        // Windows
    "/redirect",        // Windows
    "/success.txt",     // Firefox
    "/canonical.html",  // Ubuntu
};

/// Deliberately plain: no JavaScript, no web fonts, no external anything. This
/// page is rendered by the captive-portal WebView, which is not a browser, and a
/// page that fails there is a device nobody can put into service (ADR-0014).
constexpr const char *kSetupStyle = R"CSS(
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;padding:28px 20px 40px;background:#0b0d12;color:#e8ecf4;
 font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;line-height:1.5}
.wrap{max-width:420px;margin:0 auto}
h1{font-size:24px;margin:0 0 2px;letter-spacing:-.02em}
.sub{color:#97a1b5;font-size:13px;margin:0 0 22px}
.card{background:#161a24;border:1px solid #242a38;border-radius:14px;padding:18px;margin-bottom:14px}
label{display:block;font-size:13px;color:#97a1b5;margin:0 0 6px}
select,input{width:100%;padding:12px;border-radius:10px;border:1px solid #242a38;
 background:#12151d;color:#e8ecf4;font:inherit;margin-bottom:14px}
button{width:100%;padding:13px;border:0;border-radius:10px;background:#ff4326;color:#fff;
 font:inherit;font-weight:600;cursor:pointer}
button.ghost{background:transparent;color:#97a1b5;border:1px solid #242a38;font-weight:500;margin-top:10px}
.hint{color:#97a1b5;font-size:12px;margin:-8px 0 14px}
.note{color:#97a1b5;font-size:13px;line-height:1.6;margin:12px 0 0}
.note+.note{margin-top:9px}
.note b{color:#e8ecf4;font-weight:600}
.bad{color:#ff5468;border-color:#5a2630;background:#1d1218}
.ok{color:#2ae070}
.state{font-size:16px;font-weight:600;margin:0;overflow-wrap:anywhere}
.dots::after{content:"";animation:dots 1.4s steps(4,end) infinite}
@keyframes dots{0%{content:""}25%{content:"."}50%{content:".."}75%{content:"..."}}
)CSS";

std::string page(std::string_view title, std::string_view body, std::string_view head = {})
{
    std::string html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    html += head;
    html += "<title>";
    html += title;
    html += "</title><style>";
    html += kSetupStyle;
    html += "</style></head><body><div class=\"wrap\">";
    html += body;
    html += "</div></body></html>";
    return html;
}

} // namespace

Portal::Portal(Provisioning &provisioning, Identity identity, std::string version,
               std::string commit)
    : provisioning_(provisioning), identity_(std::move(identity)), version_(std::move(version)),
      commit_(std::move(commit))
{
}

void Portal::install(HttpServer &server)
{
    server.route("GET", "/", [this](const HttpRequest &request) { return index(request); });

    server.route("GET", "/api/status",
                 [this](const HttpRequest &) { return HttpResponse::json(statusJson()); });

    server.route("POST", "/connect",
                 [this](const HttpRequest &request) { return connect(request); });

    server.route("POST", "/api/scan",
                 [this](const HttpRequest &)
                 {
                     provisioning_.requestScan();
                     return HttpResponse::noContent();
                 });

    server.route("POST", "/rescan",
                 [this](const HttpRequest &)
                 {
                     provisioning_.requestScan();
                     return HttpResponse::redirect("/");
                 });

    // FR-42: the whole point is that this needs no terminal. It is a POST, so no
    // link and no prefetch can trigger it.
    server.route("POST", "/api/reset",
                 [this](const HttpRequest &)
                 {
                     logWarn("portal: factory reset requested");
                     provisioning_.requestReset();
                     return HttpResponse::noContent();
                 });

    for (const char *probe : kCaptiveProbes)
    {
        server.route("GET", probe, [this](const HttpRequest &request) { return captive(request); });
    }

    server.fallback([this](const HttpRequest &request) { return captive(request); });
}

HttpResponse Portal::index(const HttpRequest &) const
{
    if (provisioning_.needsSetup())
    {
        return HttpResponse::html(setupPage());
    }

    return HttpResponse::html(std::string(assets::kConfigPage));
}

HttpResponse Portal::captive(const HttpRequest &request) const
{
    if (!provisioning_.needsSetup())
    {
        return HttpResponse::text(404, "not found");
    }

    // Absolute, and by address: the phone asked a name that is not ours, and a
    // relative redirect would keep it on that name.
    if (request.path != "/")
    {
        return HttpResponse::redirect(std::string("http://") + kAccessPointAddress + "/");
    }

    return HttpResponse::html(setupPage());
}

HttpResponse Portal::connect(const HttpRequest &request)
{
    const std::string ssid(request.param("ssid"));
    const std::string psk(request.param("psk"));
    const bool from_config_page = request.param("source") == "config";

    if (!provisioning_.requestConnect(ssid, psk))
    {
        if (from_config_page)
        {
            return HttpResponse::text(503, "busy");
        }
        return HttpResponse::html(
            page("MatrixOS setup", "<h1>MatrixOS</h1><p class=\"sub\">" +
                                       htmlEscape(identity_.hostname) +
                                       "</p><div class=\"card bad\"><p class=\"state\">"
                                       "Please pick a network first.</p></div>"
                                       "<form method=\"get\" action=\"/\">"
                                       "<button class=\"ghost\">Back</button></form>"));
    }

    if (from_config_page)
    {
        return HttpResponse::noContent();
    }

    // POST then redirect, so a reload does not resubmit the password.
    return HttpResponse::redirect("/");
}

std::string Portal::setupPage() const
{
    const SetupStatus status = provisioning_.status();

    std::string body =
        "<h1>MatrixOS</h1><p class=\"sub\">" + htmlEscape(identity_.hostname) + "</p>";
    std::string head;

    switch (status.state)
    {
    case SetupState::Connecting:
        // No JavaScript, so the page reloads itself. Three seconds is short
        // enough to feel live and long enough not to hammer the device.
        head = "<meta http-equiv=\"refresh\" content=\"3\">";
        body += "<div class=\"card\"><p class=\"state dots\">Connecting to " +
                htmlEscape(status.ssid) +
                "</p><p class=\"note\">The device has one radio, so this network closes "
                "while it joins yours. This page will stop loading — that is what success "
                "looks like from here.</p>"
                "<p class=\"note\"><b>Watch the panel:</b> it shows CONNECTED when it "
                "worked, or TRY AGAIN if the password was wrong.</p></div>";
        break;

    case SetupState::Connected:
        body += "<div class=\"card\"><p class=\"state ok\">Connected to " +
                htmlEscape(status.ssid) +
                "</p><p class=\"note\">Setup is finished — you can close this page. Back "
                "on your own network, the device is reachable at <b>" +
                htmlEscape(identity_.hostname) + ".local</b></p></div>";
        break;

    default:
    {
        if (status.state == SetupState::Failed && !status.message.empty())
        {
            body += "<div class=\"card bad\"><p class=\"state\">Could not connect: " +
                    htmlEscape(status.message) + "</p></div>";
        }

        body += "<form class=\"card\" method=\"post\" action=\"/connect\">"
                "<label for=\"ssid\">WiFi network</label>"
                "<select id=\"ssid\" name=\"ssid\">";

        const std::vector<WifiNetwork> networks = provisioning_.networks();
        if (networks.empty())
        {
            body += "<option value=\"\">No networks found — try Rescan</option>";
        }
        for (const WifiNetwork &network : networks)
        {
            const std::string name = htmlEscape(network.ssid);
            body += "<option value=\"" + name + "\">" + name + (network.secured ? "" : " (open)") +
                    "</option>";
        }

        body += "</select>"
                "<label for=\"psk\">Password</label>"
                "<input id=\"psk\" name=\"psk\" type=\"password\" autocomplete=\"off\">"
                "<p class=\"hint\">Leave empty for an open network.</p>"
                "<button type=\"submit\">Connect</button></form>"
                "<form method=\"post\" action=\"/rescan\">"
                "<button class=\"ghost\" type=\"submit\">Rescan</button></form>";
        break;
    }
    }

    return page("MatrixOS setup", body, head);
}

std::string Portal::statusJson() const
{
    const SetupStatus status = provisioning_.status();

    std::string json = "{";
    json += "\"state\":\"" + std::string(toString(status.state)) + "\",";
    json += "\"ssid\":\"" + jsonEscape(status.ssid) + "\",";
    json += "\"accessPoint\":\"" + jsonEscape(identity_.access_point) + "\",";
    json += "\"message\":\"" + jsonEscape(status.message) + "\",";
    json += "\"version\":\"" + jsonEscape(version_) + "\",";
    json += "\"commit\":\"" + jsonEscape(commit_) + "\",";
    json += "\"hostname\":\"" + jsonEscape(identity_.hostname) + "\",";
    json += "\"networks\":[";

    bool first = true;
    for (const WifiNetwork &network : provisioning_.networks())
    {
        if (!first)
        {
            json += ',';
        }
        first = false;
        json += "{\"ssid\":\"" + jsonEscape(network.ssid) +
                "\",\"signal\":" + std::to_string(network.signal) +
                ",\"secured\":" + (network.secured ? "true" : "false") + "}";
    }

    json += "]}";
    return json;
}

} // namespace matrixos
