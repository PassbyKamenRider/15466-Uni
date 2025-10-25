#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp> // glm::uvec2

// UI 输入模型：仅需基础信息（无 HP）
struct UiModel {
    glm::vec3 player_pos = glm::vec3(0.0f);
    bool show_crosshair = true;
};

class UiOverlay {
public:
    UiOverlay() = default;

    void update(float elapsed);                          // FPS 平滑
    void draw(glm::uvec2 drawable_size, UiModel const&); // 游戏内 HUD（FPS/坐标/准星）
    void draw_title(glm::uvec2 drawable_size, const char* title);
    void draw_pause(glm::uvec2 drawable_size);
    void draw_gameover(glm::uvec2 drawable_size, int score);

    void set_visible(bool v) { visible_ = v; }
    void toggle_visible() { visible_ = !visible_; }
    bool visible() const { return visible_; }
    float fps() const { return fps_smooth_; }

private:
    float fps_smooth_ = 0.0f;
    bool visible_ = true;
};
