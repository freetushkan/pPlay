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

VideoTexture::VideoTexture(Main *m, const c2d::Vector2f &size) : GLTextureBuffer({(int) size.x, (int) size.y}, Format::RGBA8) {
    main = m;

    // fade
    fade = new C2DTexture(main->getIo()->getDataPath() + "skin/fade.png");
    fade->setScale(size.x / fade->getSize().x, size.y / fade->getSize().y);
    fade->setFillColor(Color::Black);
    fade->setAlpha(0);
    fadeTween = new TweenAlpha(0, 255, 0.5f);
    fade->add(fadeTween);
    GLTextureBuffer::add(fade);
}

int VideoTexture::resize(const Vector2i &size, bool keepPixels) {
    fade->setScale((float) size.x / fade->getSize().x, (float) size.y / fade->getSize().y);
    return GLTextureBuffer::resize(size, keepPixels);
}

void VideoTexture::hideFade() {
    fadeTween->play(TweenDirection::Backward);
}

void VideoTexture::showFade() {
    fadeTween->play(TweenDirection::Forward);
}

void VideoTexture::clearFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) fbo);
    glViewport(0, 0, (GLsizei) getSize().x, (GLsizei) getSize().y);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void VideoTexture::drawFrame() {
    forceDraw = true;
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

                mpv_render_param r_params[] = {
                        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
                        {MPV_RENDER_PARAM_FLIP_Y,     &zero},
                        {MPV_RENDER_PARAM_INVALID,    nullptr}
                };
                mpv_render_context_render(ctx, r_params);
#ifdef FULL_TEXTURE_TEST
                // mpv change viewport, restore
                glViewport(0, 0, (GLsizei) main->getSize().x, (GLsizei) main->getSize().y);
#endif
                forceDraw = false;
            }
        }
    }

    GLTextureBuffer::onDraw(transform, draw);
}