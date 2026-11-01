// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.
//
// The vendored decoder's implementation, alone in its own target so our warning
// flags do not apply to it (ADR-0019). JPEG for album art, PNG for sprites, from
// memory only.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
