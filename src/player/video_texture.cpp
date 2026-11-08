//
// Created by cpasjuste on 09/12/18.
//

#include "main.h"
#include "video_texture.h"
#include "pplay_config.h"
#include <string>
#include <algorithm>
#include <cctype>

using namespace c2d;

VideoTexture::VideoTexture(Main *m, const c2d::Vector2f &size)
    : GLTextureBuffer({(int) size.x, (int) size.y}, Format::RGBA8) {
    main = m;
    // created undefined (some drivers: garbage until the first frame): black
    GLfloat previous[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previous);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(previous[0], previous[1], previous[2], previous[3]);
}

void VideoTexture::clearFrame() {
    // the previous frame stays in the buffer: drawn black until a new one
    if (!blank) {
        blank = true;
        setFillColor(Color::Black);
    }
}

void VideoTexture::drawFrame() {
    forceDraw = true;
}

void VideoTexture::setWaiting(bool wait) {
    if (waiting && !wait) {
        forceDraw = true;
    }
    waiting = wait;
}

void VideoTexture::onDraw(c2d::Transform &transform, bool draw) {
    if (draw) {
        mpv_render_context *ctx = main->getPlayer()->getMpv()->getContext();
        if (ctx != nullptr) {
            uint64_t flags = mpv_render_context_update(ctx);
            if (flags > 0 || forceDraw) { // flags & MPV_RENDER_UPDATE_FRAME
                int zero{0};
                mpv_opengl_fbo mpv_fbo;
                std::memset(&mpv_fbo, 0, sizeof(mpv_fbo));
                mpv_fbo.fbo = (int) fbo;
                mpv_fbo.w = (int) getSize().x;
                mpv_fbo.h = (int) getSize().y;
                mpv_fbo.internal_format = GL_RGBA8;

                mpv_render_param r_params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
                                               {MPV_RENDER_PARAM_FLIP_Y, &zero},
                                               {MPV_RENDER_PARAM_INVALID, nullptr}};
                mpv_render_context_render(ctx, r_params);
                if (waiting) {
                    clearFrame();
                } else if (blank) {
                    blank = false;
                    setFillColor(Color::White);
                }
                forceDraw = false;
            }
        }
    }

    GLTextureBuffer::onDraw(transform, draw);
}