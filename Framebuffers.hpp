// adapted from https://github.com/15-466/15-466-framebuffer/blob/master/Framebuffers.cpp

#pragma once

#include "GL.hpp"
#include <glm/glm.hpp>

//A global set of framebuffers for use in various offscreen rendering effects:

struct Framebuffers {
	void realloc(glm::uvec2 const &drawable_size); //called to trigger (re-)allocation on window size change
	//current size of framebuffer attachments:
	glm::uvec2 size = glm::uvec2(0,0);

	GLuint depth_tex = 0;
	GLuint color_tex = 0;
	GLuint dof_blur_x_tex = 0;

	GLuint main_fb = 0;
	GLuint dof_blur_x_fb = 0;

	void dof_blur();
};

//the actual storage:
extern Framebuffers framebuffers;
//NOTE: I could have used a namespace and declared every element extern but that seemed more cumbersome to write
