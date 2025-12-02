#pragma once

#include "Mode.hpp"
#include "Sound.hpp"
#include "GL.hpp"

#include <functional>

/*
 *
 * Intro for 15-466-f25, including the class logo and a little sound drop.
 *
 * Usage: construct with a function that sets your main mode as current as a parameter.
 *
 */

struct GP25IntroMode : Mode {
	GP25IntroMode(std::function< void() > const &on_finished);
	virtual ~GP25IntroMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//run through the animation and print out frame timings:
	void benchmark();

	struct {
		GLuint program = -1U;
		GLuint ORIGIN_FROM_CLIP_mat3 = -1U;
		GLuint DIRECTION_FROM_CLIP_mat3 = -1U;
		GLuint LIGHTING_float = -1U;
		GLuint LIGHT_vec3 = -1U;
	} trace;

	GLuint empty_vao = -1U;

	float time = 0.0f;

	//cubic bezier spline in (time, val); time must be monotonic.
	struct Spline : public std::vector< std::pair< float, glm::vec3 > > {
		glm::vec3 interp(float t) const;
		void set_smooth_tangents(); //for lazy tangent setting
	};

	struct {
		Spline camera_at;
		Spline camera_up;
		Spline camera_target;
	} anim;

	std::shared_ptr< Sound::PlayingSample > music;

	void finish(); //sets current mode to null, then calls [a local copy of] on_finished
	std::function< void() > on_finished;

	bool loop_forever = false; //Loop forever. For debugging/testing.
};
