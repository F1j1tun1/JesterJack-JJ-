#pragma once

#include <glad/gl.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gfx {

// Uncompressed true-colour TGA. A small, portable reader keeps the art assets
// independent of platform image libraries. UV (0, 0) is the image's top left.
class ArtTexture {
public:
    ArtTexture() = default;
    explicit ArtTexture(const std::filesystem::path& path) { load(path); }
    ~ArtTexture() { release(); }
    ArtTexture(const ArtTexture&) = delete;
    ArtTexture& operator=(const ArtTexture&) = delete;
    ArtTexture(ArtTexture&& other) noexcept { *this = std::move(other); }
    ArtTexture& operator=(ArtTexture&& other) noexcept {
        if (this != &other) {
            release();
            id_ = std::exchange(other.id_, 0);
            width_ = std::exchange(other.width_, 0);
            height_ = std::exchange(other.height_, 0);
        }
        return *this;
    }
    GLuint id() const noexcept { return id_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    explicit operator bool() const noexcept { return id_ != 0; }

    void load(const std::filesystem::path& path, bool flipVertical = false) {
        std::ifstream file(path, std::ios::binary);
        const auto invalid = [&]() {
            return std::runtime_error("Cannot read artwork: " + path.string());
        };
        std::array<unsigned char, 18> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) throw invalid();
        const int w = header[12] | (header[13] << 8);
        const int h = header[14] | (header[15] << 8);
        const int channels = header[16] / 8;
        if (header[1] != 0 || header[2] != 2 || (header[16] != 24 && header[16] != 32) ||
            w < 1 || h < 1 || w > 8192 || h > 8192 || (header[17] & 0xc0) != 0)
            throw invalid();
        file.seekg(header[0], std::ios::cur);
        const auto count = static_cast<std::size_t>(w) * h;
        std::vector<unsigned char> raw(count * channels);
        if (!file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size())))
            throw invalid();
        std::vector<unsigned char> rgba(count * 4);
        for (int y = 0; y < h; ++y) {
            const int imageY = flipVertical ? h - 1 - y : y;
            const int sourceY = (header[17] & 0x20) ? imageY : h - 1 - imageY;
            for (int x = 0; x < w; ++x) {
                const int sourceX = (header[17] & 0x10) ? w - 1 - x : x;
                const auto from = (static_cast<std::size_t>(sourceY) * w + sourceX) * channels;
                const auto to = (static_cast<std::size_t>(y) * w + x) * 4;
                rgba[to] = raw[from + 2];
                rgba[to + 1] = raw[from + 1];
                rgba[to + 2] = raw[from];
                rgba[to + 3] = channels == 4 ? raw[from + 3] : 255;
            }
        }
        GLint previousTexture = 0, previousAlignment = 4;
        GLint previousRowLength = 0, previousSkipRows = 0, previousSkipPixels = 0, previousBuffer = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previousRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &previousSkipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &previousSkipPixels);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &previousBuffer);
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, previousTexture == static_cast<GLint>(id_) ? 0 : previousTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, previousRowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, previousSkipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, previousSkipPixels);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, previousBuffer);
        release();
        id_ = texture;
        width_ = w;
        height_ = h;
    }

private:
    void release() noexcept {
        if (id_) glDeleteTextures(1, &id_);
        id_ = 0;
        width_ = height_ = 0;
    }
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace gfx
