#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

// const glm::vec3 minBoundary(-20.0f, -30.0f, -15.0f);
// const glm::vec3 maxBoundary( 20.0f,  30.0f,  15.0f);

// for prototype just temporarily put it here
// will remove in future refactor -- Yifan
std::vector<glm::vec3> uni_spawnPositions;

static void request_quit() {
    SDL_Event quit;
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

GLuint hexapod_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > hexapod_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("hexapod.pnct"));
	hexapod_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > hexapod_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("hexapod.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = hexapod_meshes->lookup(mesh_name);

		if (mesh_name.rfind("SpawnPos", 0) == 0)
		{
			uni_spawnPositions.push_back(transform->position);
			return;
		}

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = hexapod_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

// - Player --------------------------------------------------------------------

Player::Player(Scene::Transform *transform_)
	: transform(transform_), start_position(transform_->position)
{}

float Player::dash_speed_curve_(float t) const
{ // an ease in-out cubic curve
	t = glm::clamp(t, 0.0f, 1.0f);
	return t < 0.5 ? 4 * t * t * t : 1 - glm::pow(-2 * t + 2, 3) / 2;
}

void Player::dash(glm::vec2 const &input)
{
	dash_direction_ = input;
	if (input != glm::vec2(0.0f) && dash_cooldown_timer_ <= 0.0f) {
		is_dashing_ = true;
		conserve_momentum_ = true;
	}
}

void Player::update_position(float elapsed, glm::vec2 const &input)
{
	if (dash_cooldown_timer_ > 0.0f) { dash_cooldown_timer_ -= elapsed; }
	if (is_dashing_) {
		dash_progress_ += elapsed;
		if (dash_progress_ < dash_duration_) {
			float t = dash_progress_ / dash_duration_;
			float speed = dash_speed_ * dash_speed_curve_(t);
			velocity_ = speed * dash_direction_;
			transform->position += glm::vec3(velocity_.x, 0.0f, velocity_.y) * elapsed;
			return;
		}
		is_dashing_ = false;
		dash_progress_ = 0.0f;
		dash_cooldown_timer_ = dash_cooldown_;
	}

	target_velocity_ = input * swim_speed_;
	if (glm::dot(velocity_, velocity_) <= swim_speed_ * swim_speed_) {
		conserve_momentum_ = false;
	}

	glm::vec2 delta_v = target_velocity_ - velocity_;
	if (glm::dot(delta_v, delta_v) < 0.001f) {
		velocity_ = target_velocity_;
	} else {
		float accel = conserve_momentum_ ? drag_ * glm::dot(velocity_, velocity_) / (swim_speed_*swim_speed_) : acceleration_;
		velocity_ += accel * glm::normalize(delta_v) * elapsed;
	}

	transform->position += glm::vec3(velocity_.x, 0.0f, velocity_.y) * elapsed;
}


void Player::resolve_collisions(std::vector<BoxCollider> const &boxes)
{
	for (auto const &box : boxes)
	{
		glm::vec3 local_pos = glm::transpose(box.rotation) * (transform->position - box.center);

		glm::vec3 closest = glm::clamp(local_pos, -box.halfSize, box.halfSize);

		glm::vec3 local_delta = local_pos - closest;
		float dist2 = glm::dot(local_delta, local_delta);
		if (dist2 > 0.0f)
		{
			float dist = std::sqrt(dist2);
			if (dist < radius)
			{
				glm::vec3 push_local = (local_delta / dist) * (radius - dist);
				glm::vec3 push = box.rotation * push_local;
				transform->position += push;
				// reflect velocity with some energy loss
				glm::vec2 n = glm::normalize(glm::vec2(push.x, push.z));
				velocity_ = 0.5f * (velocity_ - 2.0f * glm::dot(velocity_, n) * n);
			}
			continue;
		}
	
		glm::vec3 penetration;
		float min_overlap = std::numeric_limits<float>::max();
		int axis = -1;
	
		for (int i = 0; i < 3; ++i)
		{
			float dist_to_pos = box.halfSize[i] - local_pos[i];
			float dist_to_neg = box.halfSize[i] + local_pos[i];
			float overlap = std::min(dist_to_pos, dist_to_neg);
			if (overlap < min_overlap)
			{
				min_overlap = overlap;
				axis = i;
				penetration[i] = (dist_to_pos < dist_to_neg) ? overlap : -overlap;
			}
		}
	
		glm::vec3 push_local(0.0f);
		if (axis >= 0)
		{
			push_local[axis] = (penetration[axis] > 0 ? radius : -radius);
		}

		glm::vec3 push = box.rotation * push_local;
		transform->position += push;
		// reflect velocity with some energy loss
		glm::vec2 n = glm::normalize(glm::vec2(push.x, push.z));
		velocity_ = 0.5f * (velocity_ - 2.0f * glm::dot(velocity_, n) * n);
	}
}

// -----------------------------------------------------------------------------

// - FollowCamera --------------------------------------------------------------

FollowCamera::FollowCamera(Scene::Transform *transform_, Scene::Transform *target)
	: transform(transform_), target_(target)
{}

void FollowCamera::update_position(float elapsed)
{
	transform->position = glm::vec3(target_->position.x, transform->position.y, target_->position.z);
}

// -----------------------------------------------------------------------------

void PlayMode::generate_uni(glm::vec3 position)
{
	Uni uni;

    Scene::Transform *t = &scene.transforms.emplace_back();
    t->name = "Uni_Instance";
    t->position = position;
	uni.transform = t;

    Scene::Drawable *d = &scene.drawables.emplace_back(t);
    d->pipeline = drawable_uni->pipeline;
	uni.drawable = d;

	unis.push_back(uni);
}

void PlayMode::collect_uni()
{
	for (auto it = unis.begin(); it != unis.end();)
	{
		float dist = glm::length(player.transform->position - it->transform->position);
		if (dist < player.radius + it->radius)
		{
			auto d_it = std::find_if(
				scene.drawables.begin(), scene.drawables.end(),
				[&](Scene::Drawable &d){ return &d == it->drawable; }
			);
			
			scene.drawables.erase(d_it);

			it = unis.erase(it);
			uniCount++;

			std::cout << "Total Unis collected: " << uniCount << "\n";
		}
		else
		{
			++it;
		}
	}
}




void PlayMode::reset_game() { //place holder. Can also dump this and just recreate a new gamemode
    score = 0;
    player.transform->position = player.start_position;
    // TODO: reset objects, timer.
    game_state = GameState::Playing;
}

void PlayMode::end_game() {
    game_state = GameState::GameOver;
}

glm::vec2 PlayMode::get_move_input() const {
	glm::vec2 move = glm::vec2(0.0f);
	if (left.pressed && !right.pressed) move.x =-1.0f;
	if (!left.pressed && right.pressed) move.x = 1.0f;
	if (down.pressed && !up.pressed) move.y =-1.0f;
	if (!down.pressed && up.pressed) move.y = 1.0f;
	if (move != glm::vec2(0.0f)) move = glm::normalize(move);
	return move;
}


PlayMode::PlayMode() : scene(*hexapod_scene) {
	for (auto &transform : scene.transforms)
	{
		if (transform.name == "Sphere") player = Player(&transform);

		if (transform.name.rfind("Collider", 0) == 0)
		{
			BoxCollider box;
			box.center = transform.position;
			box.halfSize = transform.scale;
			box.rotation = glm::mat3_cast(transform.rotation);
			colliders.push_back(box);
		}
	}

	for (auto &drawable : scene.drawables)
	{
		if (drawable.transform->name == "Uni")
		{
			drawable_uni = &drawable;
		}
	}

	// spawn unis
	for (auto pos : uni_spawnPositions)
	{
		generate_uni(pos);
	}

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
	virtual_camera = FollowCamera(camera->transform, player.transform);

    game_state = GameState::Title; 
	ui = std::make_unique<UiOverlay>();
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		switch(game_state) {
			case GameState::Title:
				if (evt.key.key == SDLK_RETURN) {
					reset_game();
					return true;
				}
				if (evt.key.key == SDLK_ESCAPE) {
					request_quit();
					return true;
				}
				break;
			
			case GameState::Playing:
				if (evt.key.key == SDLK_ESCAPE) {
					//SDL_SetWindowRelativeMouseMode(Mode::window, false);
					game_state = GameState::Paused;
					left.pressed = false;
					right.pressed = false;
					up.pressed = false;
					down.pressed = false;
					return true;
				} else if (evt.key.key == SDLK_A) {
					left.downs += 1;
					left.pressed = true;
					return true;
				} else if (evt.key.key == SDLK_D) {
					right.downs += 1;
					right.pressed = true;
					return true;
				} else if (evt.key.key == SDLK_W) {
					up.downs += 1;
					up.pressed = true;
					return true;
				} else if (evt.key.key == SDLK_S) {
					down.downs += 1;
					down.pressed = true;
					return true;
				} else if (evt.key.key == SDLK_SPACE) {
					player.dash(get_move_input());
					return true;
				} else if (evt.key.key == SDLK_P) {
					game_state = GameState::GameOver;
					return true;
				} else if (evt.key.key == SDLK_U) {
					generate_uni(player.transform->position + glm::vec3(0.0f, 0.0f, -5.0f));
					return true;
				}

				break;

			case GameState::Paused:
				if (evt.key.key == SDLK_ESCAPE) {
					game_state = GameState::Playing;
					return true;
				}
				break;
			
			case GameState::GameOver:
				if (evt.key.key == SDLK_RETURN) {
					reset_game();
					return true;
				}
				if (evt.key.key == SDLK_ESCAPE) {
					request_quit();
					return true;
				}
				break;
		}
	}

	else if (evt.type == SDL_EVENT_KEY_UP) {
		if (game_state == GameState::Playing) {
			if (evt.key.key == SDLK_A) {
				left.pressed = false;
				return true;
			} else if (evt.key.key == SDLK_D) {
				right.pressed = false;
				return true;
			} else if (evt.key.key == SDLK_W) {
				up.pressed = false;
				return true;
			} else if (evt.key.key == SDLK_S) {
				down.pressed = false;
				return true;
			}
		}
		
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window) == false) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		// if (SDL_GetWindowRelativeMouseMode(Mode::window) == true) {
		// 	glm::vec2 motion = glm::vec2(
		// 		evt.motion.xrel / float(window_size.y),
		// 		evt.motion.yrel / float(window_size.y)
		// 	);
		// 	camera->transform->rotation = glm::normalize(
		// 		glm::angleAxis(-motion.x * camera->fovy, glm::vec3(0.0f, 0.0f, 1.0f))
		// 		* camera->transform->rotation
		// 		* glm::angleAxis(-motion.y * camera->fovy, glm::vec3(1.0f, 0.0f, 0.0f))
		// 	);
		// 	return true;
		// }
	}
		
	return false;
}

void PlayMode::update(float elapsed) {
	
	if (ui) ui->update(elapsed);

    if (game_state != GameState::Playing) {
        left.downs = right.downs = up.downs = down.downs = 0;
        return;
    }

	player.update_position(elapsed, get_move_input());
	player.resolve_collisions(colliders);
	virtual_camera.update_position(elapsed);

	collect_uni();

	{ //update listener to camera position:
		glm::mat4x3 frame = virtual_camera.transform->make_parent_from_local();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	if (game_state == GameState::Title || game_state == GameState::GameOver) {
        glClearColor(0.0f,0.0f,0.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        if (game_state == GameState::Title) ui->draw_title(drawable_size, "Uni");
        else ui->draw_gameover(drawable_size, score);
        glEnable(GL_DEPTH_TEST);
        GL_ERRORS();
        return;
    }

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	// { //use DrawLines to overlay some text:
	// 	glDisable(GL_DEPTH_TEST);
	// 	float aspect = float(drawable_size.x) / float(drawable_size.y);
	// 	DrawLines lines(glm::mat4(
	// 		1.0f / aspect, 0.0f, 0.0f, 0.0f,
	// 		0.0f, 1.0f, 0.0f, 0.0f,
	// 		0.0f, 0.0f, 1.0f, 0.0f,
	// 		0.0f, 0.0f, 0.0f, 1.0f
	// 	));

	// 	constexpr float H = 0.09f;
	// 	lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
	// 		glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
	// 		glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 		glm::u8vec4(0x00, 0x00, 0x00, 0x00));
	// 	float ofs = 2.0f / drawable_size.y;
	// 	lines.draw_text("Mouse motion rotates camera; WASD moves; escape ungrabs mouse",
	// 		glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
	// 		glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
	// 		glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	// }

	if (game_state == GameState::Playing) {
        ui_model.player_pos = player.transform->position;
        ui_model.show_crosshair = true;
        ui->draw(drawable_size, ui_model);
    } else { // Paused
        ui->draw_pause(drawable_size);
    }

	GL_ERRORS();
}
