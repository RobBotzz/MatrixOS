// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#include "net/gallery_routes.h"

namespace matrixos
{

std::string galleryJson(const GalleryService &gallery)
{
    std::string json = "{";
    json += "\"sendLink\":\"" + jsonEscape(gallery.sendLink()) + "\",";
    json += "\"pictures\":" + std::to_string(gallery.snapshot().pictures.size()) + ",";
    json += "\"keep\":" + std::to_string(GalleryService::kKeep);
    json += "}";
    return json;
}

void installGalleryRoutes(HttpServer &server, GalleryService &gallery)
{
    server.route("GET", "/api/gallery", [&gallery](const HttpRequest &)
                 { return HttpResponse::json(galleryJson(gallery)); });

    // A POST, so no link and no prefetch can invalidate the address.
    server.route("POST", "/api/gallery/address",
                 [&gallery](const HttpRequest &)
                 {
                     gallery.newAddress();
                     return HttpResponse::json(galleryJson(gallery));
                 });
}

} // namespace matrixos
