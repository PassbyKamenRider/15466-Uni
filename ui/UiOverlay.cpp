#include "UiOverlay.hpp"
#include "../DrawLines.hpp"
#include "../gl_errors.hpp"
#include <glm/glm.hpp>
#include <cstdio>
#include <cstring>

static void make_ortho(float aspect, DrawLines &out) {
    out = DrawLines(glm::mat4(
        1.0f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    ));
}

void UiOverlay::update(float elapsed) {
    if (elapsed > 0.0f) {
        float inst = 1.0f / elapsed;
        fps_smooth_ = (fps_smooth_ <= 0.0f) ? inst : (0.9f * fps_smooth_ + 0.1f * inst);
    }
}

void UiOverlay::draw(glm::uvec2 drawable_size, UiModel const& m) {
    if (!visible_) return;
    glDisable(GL_DEPTH_TEST);

    float aspect = float(drawable_size.x) / float(drawable_size.y);
    DrawLines lines(glm::mat4(1.0f));
    make_ortho(aspect, lines);

    constexpr float H = 0.09f;
    float ofs = 2.0f / drawable_size.y;

    // 左上：操作提示
    {
        const char* tip = "WASD move / ESC pause";
        lines.draw_text(tip,
            glm::vec3(-aspect + 0.1f*H, 1.0f - 1.5f*H, 0.0f),
            glm::vec3(H,0,0), glm::vec3(0,H,0),
            glm::u8vec4(0,0,0,0));
        lines.draw_text(tip,
            glm::vec3(-aspect + 0.1f*H + ofs, 1.0f - 1.5f*H + ofs, 0.0f),
            glm::vec3(H,0,0), glm::vec3(0,H,0),
            glm::u8vec4(0xff,0xff,0xff,0x00));
    }

    // 右上：FPS + Pos
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FPS: %.1f   Pos:(%.1f, %.1f, %.1f)",
            fps_smooth_, m.player_pos.x, m.player_pos.y, m.player_pos.z);
        lines.draw_text(buf,
            glm::vec3(+aspect - 12.0f*H, 1.0f - 1.5f*H, 0.0f),
            glm::vec3(H,0,0), glm::vec3(0,H,0),
            glm::u8vec4(0,0,0,0));
        lines.draw_text(buf,
            glm::vec3(+aspect - 12.0f*H + ofs, 1.0f - 1.5f*H + ofs, 0.0f),
            glm::vec3(H,0,0), glm::vec3(0,H,0),
            glm::u8vec4(0xff,0xff,0xff,0x00));
    }

    glEnable(GL_DEPTH_TEST);
}

static void draw_centered_text(DrawLines &lines, float aspect, glm::uvec2 drawable_size,
                               const char* s, float H, float y_ndc) {
    float ofs = 2.0f / drawable_size.y;
    float w = 0.6f * H * float(std::strlen(s)); // 近似宽度
    glm::vec3 pos(-w / aspect * 0.5f, y_ndc, 0.0f);
    lines.draw_text(s, pos, glm::vec3(H,0,0), glm::vec3(0,H,0), glm::u8vec4(0,0,0,0));
    lines.draw_text(s, pos + glm::vec3(ofs, ofs, 0.0f),
        glm::vec3(H,0,0), glm::vec3(0,H,0), glm::u8vec4(0xff,0xff,0xff,0x00));
}

void UiOverlay::draw_title(glm::uvec2 drawable_size, const char* title) {
    glDisable(GL_DEPTH_TEST);
    float aspect = float(drawable_size.x) / float(drawable_size.y);
    DrawLines lines(glm::mat4(1.0f));
    make_ortho(aspect, lines);

    draw_centered_text(lines, aspect, drawable_size, title, 0.18f, 0.25f);
    draw_centered_text(lines, aspect, drawable_size, "Press Enter to Start", 0.09f, -0.05f);
    draw_centered_text(lines, aspect, drawable_size, "Press Esc to Quit", 0.09f, -0.18f);

    glEnable(GL_DEPTH_TEST);
}

void UiOverlay::draw_pause(glm::uvec2 drawable_size) {
    glDisable(GL_DEPTH_TEST);
    float aspect = float(drawable_size.x) / float(drawable_size.y);
    DrawLines lines(glm::mat4(1.0f));
    make_ortho(aspect, lines);

    // rect frame
    glm::vec3 tl(-0.6f,  0.35f, 0.0f);
    glm::vec3 tr(+0.6f,  0.35f, 0.0f);
    glm::vec3 br(+0.6f, -0.35f, 0.0f);
    glm::vec3 bl(-0.6f, -0.35f, 0.0f);
    glm::u8vec4 col(0xff,0xff,0xff,0x00);
    lines.draw(tl, tr, col); lines.draw(tr, br, col);
    lines.draw(br, bl, col); lines.draw(bl, tl, col);

    draw_centered_text(lines, aspect, drawable_size, "PAUSED", 0.12f, 0.02f);
    draw_centered_text(lines, aspect, drawable_size, "Press ESC to Resume", 0.08f, -0.20f);

    glEnable(GL_DEPTH_TEST);
}

void UiOverlay::draw_gameover(glm::uvec2 drawable_size, int score) {
    glDisable(GL_DEPTH_TEST);
    float aspect = float(drawable_size.x) / float(drawable_size.y);
    DrawLines lines(glm::mat4(1.0f));
    make_ortho(aspect, lines);

    char score_line[64];
    std::snprintf(score_line, sizeof(score_line), "Score: %d", score);

    draw_centered_text(lines, aspect, drawable_size, "GAME OVER", 0.18f, 0.25f);
    draw_centered_text(lines, aspect, drawable_size, score_line, 0.09f, 0.03f);
    draw_centered_text(lines, aspect, drawable_size, "Press Enter to Restart", 0.09f, -0.16f);
    draw_centered_text(lines, aspect, drawable_size, "Press Esc to Quit", 0.09f, -0.29f);

    glEnable(GL_DEPTH_TEST);
}
