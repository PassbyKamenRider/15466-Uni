#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

#include <memory>
#include "ui/UiOverlay.hpp"
#include "Mesh.hpp"

struct BoxCollider
{
	glm::vec3 center;
    glm::vec3 halfSize;
    glm::mat3 rotation;
};

struct TriggerCollider
{
    glm::vec3 center;
    glm::vec3 halfSize;
    glm::mat3 rotation;
    std::list<Scene::Drawable>::iterator drawable_idx;
	Scene::Drawable::Pipeline backup_pipeline;
    bool is_triggered = false;
};

struct PlayerPartInfo
{
	Scene::Transform* transform;
    glm::vec3 local_offset;
    glm::quat local_rotation;
};

class Player
{
  public:
  	Player() = default;
	Player(Scene::Transform *transform_);
	Scene::Transform *transform;
	glm::vec3 start_position; // const

	std::vector<PlayerPartInfo> parts;
	PlayerPartInfo rakePart{};
	glm::vec3 rakeStartPosition{};
	bool isAttacking = false;
	float attack_timer = 0.0f;
	float attack_duration = 0.5f;
	BoxCollider attack_range_;
	glm::vec3 attack_base_offset;

	void update_position(float elapsed, glm::vec2 const &input);
	void rotate_player(float angle);
	void resolve_collisions(std::vector<BoxCollider> const &boxes, std::vector<TriggerCollider> &triggers);
	void dash(glm::vec2 const &input);
	void reset_player_speed();
	void start_attack();
	void update_attack(float elapsed);
	
	private:
	float radius_ = 1.0f; // const
	float swim_speed_ = 7.0f; // const
	float acceleration_ = 10.0f; // const
	
	float dash_speed_ = 20.0f; // const
	float drag_ = 7.0f; // const
	float dash_cooldown_ = 1.0f; // const
	float dash_duration_ = 0.2f; // const
	bool is_dashing_ = false;
	float dash_speed_curve_(float t) const;
	float dash_progress_ = 0.0f;
	float dash_cooldown_timer_ = 0.0f;
	glm::vec2 dash_direction_ = glm::vec2(0.0f);
	bool conserve_momentum_ = false;

	glm::vec2 velocity_ = glm::vec2(0.0f);
	glm::vec2 target_velocity_ = glm::vec2(0.0f);
};

class FollowCamera
{
  public:
	FollowCamera() = default;
	FollowCamera(Scene::Transform *transform_, Scene::Transform *target);
	Scene::Transform *transform;
	void update_position(float elapsed);
  private:
	Scene::Transform *target_;
};

struct Uni
{
	Scene::Transform *transform = nullptr;
	std::list< Scene::Drawable >::iterator drawable_idx;
	float radius = 2.0f;
};

class UniManager
{
  public:
  UniManager() = default;
  void spawn_unis(Scene &scene);
  void clear_unis(Scene &scene);
  void collect_uni(Scene &scene, glm::vec3 player_position, BoxCollider const &attackRange);
  size_t num_collected = 0;

  Scene::Drawable *uni_prefab = nullptr;
  std::vector<Scene::Transform *> spawn_transforms;
  
  private:
  std::vector<Uni> unis_;
  
  void spawn_uni(Scene &scene, Scene::Transform *transform);
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----
	enum class GameState { Title, Intro, Playing, Paused, GameOver };
	void end_game();
    void reset_game();

	float level_time_limit = 60.0f;
	float time_remaining = 0.0f;

	std::unique_ptr<UiOverlay> ui;
    UiModel ui_model;

    GameState game_state = GameState::Title;

	GLuint title_tex = 0;

	GLuint ending_success_tex = 0;
	GLuint ending_fail_tex = 0;

	size_t win_threshold = 20;
	bool did_win = false;

	// ----- intro screens -----
	std::array<GLuint, 4> intro_tex = {0,0,0,0}; // p1~p4
	int intro_idx = 0;

	GLuint intro_vao = 0;
	GLuint intro_vbo = 0;
	GLuint intro_ebo = 0;
	
	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	glm::vec2 get_move_input() const;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	Player player;
	FollowCamera virtual_camera;
	Scene::Camera *camera;
	UniManager uni_manager;

	// ----- collisions -----
	std::vector<BoxCollider> colliders;
	std::vector<TriggerCollider> triggers;
};
