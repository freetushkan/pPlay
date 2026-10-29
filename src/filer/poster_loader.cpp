//
// Details panel posters.
//

#include <cstdio>

#include "main.h"
#include "image.h"
#include "net_http.h"
#include "poster_loader.h"

#define SETTLE_MS 250

namespace {
    const char *imageFormat(const std::string &data) {
        auto u = [&data](size_t i) { return (unsigned char) data[i]; };
        if (data.size() < 12) {
            return nullptr;
        }
        if (u(0) == 0xFF && u(1) == 0xD8) {
            return "jpeg";
        }
        if (u(0) == 0x89 && data.compare(1, 3, "PNG") == 0) {
            return "png";
        }
        if (data.compare(0, 3, "GIF") == 0) {
            return "gif";
        }
        if (data.compare(0, 2, "BM") == 0) {
            return "bmp";
        }
        if (data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WEBP") == 0) {
            return "webp";
        }
        if (data.compare(4, 4, "ftyp") == 0) {
            return "avif"; // avif / heif
        }
        return nullptr;
    }
}

PosterLoader::PosterLoader(Main *m) : main(m) {}

PosterLoader::~PosterLoader() {
    if (thread != nullptr) {
        thread->join();
        delete (thread);
    }
}

std::string PosterLoader::cachePath(const std::string &url) const {
    return main->getIo()->getDataPath() + "cache/posters/" + pplay::Utility::md5hash(url);
}

void PosterLoader::request(const std::string &url) {
    if (url != wanted) {
        wanted = url;
        done.clear();
        requestClock.restart();
    }
}

bool PosterLoader::poll(const c2d::Vector2i &maxSize, c2d::Texture **texture, std::string *error) {
    *texture = nullptr;
    error->clear();

    if (thread != nullptr && downloadDone) {
        thread->join();
        delete (thread);
        thread = nullptr;
        if (!downloadOk) {
            failed = downloadUrl;
            if (downloadUrl == wanted) {
                done = wanted;
                *error = downloadError;
                return true;
            }
        }
    }

    if (wanted.empty() || wanted == done || wanted == failed || thread != nullptr
        || requestClock.getElapsedTime().asMilliseconds() < SETTLE_MS) {
        return false;
    }

    std::string path = cachePath(wanted);
    if (main->getIo()->exist(path)) {
        std::string decodeError;
        done = wanted;
        *texture = pplay::Image::load(path, maxSize, &decodeError);
        if (*texture == nullptr) {
            pplay::Utility::deleteFile(path);
            failed = wanted;
            *error = pplay::tr("unsupported image") + " (" + decodeError + ")";
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Details: poster decode failed: " + wanted + ": " + decodeError);
            return true;
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "Details: poster " + wanted + " " + std::to_string((*texture)->getTextureSize().x) + "x"
                                    + std::to_string((*texture)->getTextureSize().y));
        return true;
    }

    std::string dir = main->getIo()->getDataPath() + "cache/posters";
    if (!main->getIo()->exist(dir)) {
        main->getIo()->create(dir);
    }
    downloadUrl = wanted;
    downloadPath = path;
    downloadDone = false;
    downloadOk = false;
    thread = new c2d::C2DThread(downloadThread, this);
    return false;
}

int PosterLoader::downloadThread(void *data) {
    auto *self = (PosterLoader *) data;
    pplay::Http::Request request;
    request.url = self->downloadUrl;
    request.timeout = 15;
    request.maxSize = 8 * 1024 * 1024;
    request.headers = {"Accept: image/jpeg,image/png;q=0.9,image/*;q=0.5,*/*;q=0.1",
                       "User-Agent: Mozilla/5.0 (PlayStation; pPlay) AppleWebKit/605.1.15 (KHTML, like Gecko)"};
    pplay::Http::Response response = pplay::Http::perform(request);

    std::string error;
    bool ok = false;
    if (!response.ok()) {
        error = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
    } else if (imageFormat(response.body) == nullptr) {
        error = "not an image";
    } else {
        FILE *f = fopen(self->downloadPath.c_str(), "wb");
        ok = f != nullptr && fwrite(response.body.data(), 1, response.body.size(), f) == response.body.size();
        if (f) {
            fclose(f);
        }
        if (!ok) {
            pplay::Utility::deleteFile(self->downloadPath);
            error = "can't write cache file";
        }
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Details: poster download " + self->downloadUrl + " status=" + std::to_string(response.status)
                                + " size=" + std::to_string(response.body.size()) + (ok ? " ok" : " failed: " + error));
    self->downloadError = error;
    self->downloadOk = ok;
    self->downloadDone = true;
    return 0;
}
