#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

#include <memory>
#include "ui/UiOverlay.hpp"

struct BoxCollider
{
	glm::vec3 center;
    glm::vec3 halfSize;
    glm::mat3 rotation;
};

struct Uni
{
	Scene::Transform* transform = nullptr;
    Scene::Drawable* drawable = nullptr;
	float radius = 1.0f;
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----
	enum class GameState { Title, Playing, Paused, GameOver };
	void end_game();   // 进入 GameOver
    void reset_game(); // 重新开始
    void add_score(int delta) { score += delta; } // 可选：加分接口

	std::unique_ptr<UiOverlay> ui;
    UiModel ui_model;

    GameState game_state = GameState::Title;
    int score = 0;

    glm::vec3 player_start_pos = glm::vec3(0.0f);

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	float player_radius = 1.0f;
	Scene::Transform *player = nullptr; // add pointer to player object
	
	//camera:
	Scene::Camera *camera = nullptr;

	// ----- collisions -----
	std::vector<BoxCollider> colliders;

	glm::vec3 resolve_collision(BoxCollider const &box);

	// ----- Uni -----
	Scene::Drawable *drawable_uni = nullptr;
	std::vector<Uni> unis;
	int uniCount = 0;

	void generate_uni(glm::vec3 position);
	void collect_uni();
};