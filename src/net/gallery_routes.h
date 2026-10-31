// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.

#pragma once

#include "net/http_server.h"
#include "os/gallery.h"

#include <string>

namespace matrixos
{

/// `GET /api/gallery` (send link, picture count) and `POST /api/gallery/address`
/// (draw a new address) for the configuration page.
void installGalleryRoutes(HttpServer &server, GalleryService &gallery);

std::string galleryJson(const GalleryService &gallery);

} // namespace matrixos
