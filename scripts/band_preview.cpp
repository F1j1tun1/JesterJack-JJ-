// Developer tool: renders the band with a tiny software rasteriser (no GPU, no window) so poses and
// animation can be inspected as PPM images. Not part of the game.
//
//   g++ -std=c++17 -O2 -Isrc -isystem third_party/glm scripts/band_preview.cpp src/graphics/BandModel.cpp -o band_preview
//   ./band_preview close 12.3 out.ppm     # band close-up at beat 12.3
//   ./band_preview game  12.3 out.ppm     # as seen from the default table camera (1050 x 642)
//   ./band_preview idle  0    out.ppm     # between numbers
#include "graphics/BandModel.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using glm::mat3;
using glm::mat4;
using glm::vec3;
using glm::vec4;

struct Tri { vec3 p[3], n[3]; };
using Mesh = std::vector<Tri>;
constexpr float pi = 3.14159265f;

Mesh cubeMesh() {
    Mesh m;
    const vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const vec3& normal : n) {
        const vec3 u = std::abs(normal.y) > .5f ? vec3(1, 0, 0) : vec3(0, 1, 0);
        const vec3 a = glm::normalize(glm::cross(normal, u)), b = glm::cross(normal, a);
        const vec3 c = normal * .5f, q[4] = {c - a * .5f - b * .5f, c + a * .5f - b * .5f, c + a * .5f + b * .5f, c - a * .5f + b * .5f};
        m.push_back({{q[0], q[1], q[2]}, {normal, normal, normal}});
        m.push_back({{q[0], q[2], q[3]}, {normal, normal, normal}});
    }
    return m;
}
Mesh sphereMesh() {
    Mesh m;
    const int slices = 20, rings = 14;
    auto P = [&](int r, int s) {
        const float lat = pi * r / rings, lon = 2 * pi * s / slices;
        return vec3(std::sin(lat) * std::cos(lon), std::cos(lat), std::sin(lat) * std::sin(lon));
    };
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            const vec3 a = P(r, s), b = P(r + 1, s), c = P(r + 1, s + 1), d = P(r, s + 1);
            m.push_back({{a, b, c}, {a, b, c}});
            m.push_back({{a, c, d}, {a, c, d}});
        }
    return m;
}
Mesh cylinderMesh() {
    Mesh m;
    const int seg = 28;
    for (int i = 0; i < seg; ++i) {
        const float a0 = 2 * pi * i / seg, a1 = 2 * pi * (i + 1) / seg;
        const vec3 c0(std::cos(a0), 0, std::sin(a0)), c1(std::cos(a1), 0, std::sin(a1));
        const vec3 b0 = c0 + vec3(0, -.5f, 0), b1 = c1 + vec3(0, -.5f, 0), t0 = c0 + vec3(0, .5f, 0), t1 = c1 + vec3(0, .5f, 0);
        m.push_back({{b0, b1, t1}, {c0, c1, c1}});
        m.push_back({{b0, t1, t0}, {c0, c1, c0}});
        const vec3 up(0, 1, 0), dn(0, -1, 0);
        m.push_back({{vec3(0, .5f, 0), t1, t0}, {up, up, up}});
        m.push_back({{vec3(0, -.5f, 0), b0, b1}, {dn, dn, dn}});
    }
    return m;
}

struct Image {
    int w, h;
    std::vector<float> depth;
    std::vector<vec3> color;
    Image(int w_, int h_, vec3 bg) : w(w_), h(h_), depth(static_cast<std::size_t>(w_ * h_), 1e9f), color(static_cast<std::size_t>(w_ * h_), bg) {}
};

void draw(Image& img, const Mesh& mesh, const mat4& model, const mat4& viewProj, const vec3& eye,
          vec3 base, float specular, float shininess) {
    const mat3 normalMatrix = glm::transpose(glm::inverse(mat3(model)));
    const vec3 light = glm::normalize(vec3(-.35f, .8f, .55f));
    for (const Tri& t : mesh) {
        vec4 clip[3];
        vec3 world[3], normal[3];
        bool behind = false;
        for (int i = 0; i < 3; ++i) {
            world[i] = vec3(model * vec4(t.p[i], 1));
            normal[i] = glm::normalize(normalMatrix * t.n[i]);
            clip[i] = viewProj * vec4(world[i], 1);
            if (clip[i].w < .05f) behind = true;
        }
        if (behind) continue;
        vec3 screen[3];
        for (int i = 0; i < 3; ++i)
            screen[i] = vec3((clip[i].x / clip[i].w * .5f + .5f) * img.w, (1 - (clip[i].y / clip[i].w * .5f + .5f)) * img.h, clip[i].w);
        const float area = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) - (screen[2].x - screen[0].x) * (screen[1].y - screen[0].y);
        if (std::abs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({screen[0].x, screen[1].x, screen[2].x}))));
        const int x1 = std::min(img.w - 1, static_cast<int>(std::ceil(std::max({screen[0].x, screen[1].x, screen[2].x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({screen[0].y, screen[1].y, screen[2].y}))));
        const int y1 = std::min(img.h - 1, static_cast<int>(std::ceil(std::max({screen[0].y, screen[1].y, screen[2].y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = x + .5f, py = y + .5f;
                const float w0 = ((screen[1].x - px) * (screen[2].y - py) - (screen[2].x - px) * (screen[1].y - py)) / area;
                const float w1 = ((screen[2].x - px) * (screen[0].y - py) - (screen[0].x - px) * (screen[2].y - py)) / area;
                const float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = w0 * screen[0].z + w1 * screen[1].z + w2 * screen[2].z;
                float& zb = img.depth[static_cast<std::size_t>(y * img.w + x)];
                if (z >= zb) continue;
                zb = z;
                const vec3 n = glm::normalize(w0 * normal[0] + w1 * normal[1] + w2 * normal[2]);
                const vec3 p = w0 * world[0] + w1 * world[1] + w2 * world[2];
                const vec3 view = glm::normalize(eye - p);
                const float diffuse = std::max(0.0f, glm::dot(n, light));
                const float spec = std::pow(std::max(0.0f, glm::dot(n, glm::normalize(light + view))), shininess) * specular;
                // Exposure matches how dark the game's palette is next to its warm stage lights.
                vec3 c = base * (.55f + 3.2f * diffuse) + vec3(spec * .8f);
                img.color[static_cast<std::size_t>(y * img.w + x)] = c;
            }
    }
}

void save(const Image& img, const char* path) {
    FILE* f = std::fopen(path, "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    for (const vec3& c : img.color)
        for (int i = 0; i < 3; ++i) {
            const float v = std::pow(std::clamp(c[i], 0.0f, 1.0f), 1.0f / 2.2f);
            std::fputc(static_cast<int>(v * 255 + .5f), f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "close";
    const double beats = argc > 2 ? std::atof(argv[2]) : 0.0;
    const char* out = argc > 3 ? argv[3] : "band.ppm";
    const float energy = argc > 4 ? static_cast<float>(std::atof(argv[4])) : .8f;
    gfx::band::Playback pb;
    pb.playing = mode != "idle";
    pb.beats = beats;
    pb.bpm = 110;
    pb.low = pb.mid = pb.high = pb.hit = energy;
    pb.time = static_cast<float>(beats * 60.0 / 110.0);
    std::vector<gfx::band::Part> parts;
    gfx::band::build(pb, parts);

    const Mesh meshes[3] = {cubeMesh(), sphereMesh(), cylinderMesh()};
    mat4 world(1), view, proj;
    vec3 eye;
    int w = 1000, h = 560;
    if (mode == "game") {
        // The default table camera and the stage root the renderer uses (see Renderer.cpp).
        world = glm::translate(mat4(1), vec3(0, .975f, -39.6f)) * glm::scale(mat4(1), vec3(2.0f));
        eye = vec3(0, 9.1f * std::sin(glm::radians(32.0f)), 9.1f * std::cos(glm::radians(32.0f)));
        w = 1050; h = 642;
        const float fov = 2 * std::atan(std::tan(glm::radians(21.5f)) * (642.0f / 900.0f) / .53f);
        view = glm::lookAt(eye, vec3(0, .6f, -.5f), vec3(0, 1, 0));
        proj = glm::perspective(fov, 1050.0f / 642.0f, .1f, 100.0f);
    } else {
        eye = vec3(.4f, 2.6f, 8.3f);
        vec3 target(.0f, .85f, -1.0f);
        float fov = 46.0f;
        if (argc > 10) { // Optional framing: eyeX eyeY eyeZ targetX targetY targetZ fov
            eye = vec3(std::atof(argv[5]), std::atof(argv[6]), std::atof(argv[7]));
            target = vec3(std::atof(argv[8]), std::atof(argv[9]), std::atof(argv[10]));
            if (argc > 11) fov = static_cast<float>(std::atof(argv[11]));
            w = 640; h = 640;
        }
        view = glm::lookAt(eye, target, vec3(0, 1, 0));
        proj = glm::perspective(glm::radians(fov), static_cast<float>(w) / h, .1f, 100.0f);
    }
    Image img(w, h, vec3(.004f, .005f, .008f));
    const mat4 vp = proj * view;
    // Stage floor and the dark back curtain, for context.
    draw(img, meshes[0], world * glm::translate(mat4(1), vec3(0, -.05f, -2.0f)) * glm::scale(mat4(1), vec3(14, .1f, 11)), vp, eye, vec3(.16f, .09f, .05f), .1f, 20);
    draw(img, meshes[0], world * glm::translate(mat4(1), vec3(0, 2.5f, -5.6f)) * glm::scale(mat4(1), vec3(14, 5, .1f)), vp, eye, vec3(.10f, .035f, .03f), 0, 8);
    for (const auto& part : parts)
        draw(img, meshes[static_cast<int>(part.shape)], world * part.model, vp, eye, part.color, part.specular, part.shininess);
    save(img, out);
    std::printf("%zu parts -> %s\n", parts.size(), out);
}
