// developed with help from perplexity pro and from referencing https://github.com/15-466/15-466-framebuffer/blob/master/Framebuffers.cpp

#include "Framebuffers.hpp"
#include "GL.hpp"
#include "Load.hpp"
#include "gl_compile_program.hpp"
#include "gl_check_fb.hpp"
#include "gl_errors.hpp"

#include <array>

Framebuffers framebuffers;

float NEAR_CLIP = 0.01f;
float FAR_CLIP = 55.0f;
float FOCAL_DISTANCE = 30.0f;
float BLUR_SCALE = 150.0f;
float BLUR_RADIUS_MAX = 30.0f;

void Framebuffers::realloc(glm::uvec2 const &drawable_size) {
	if (drawable_size == size) return;
	size = drawable_size;

	if (depth_tex == 0) glGenTextures(1, &depth_tex);
	glBindTexture(GL_TEXTURE_2D, depth_tex);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_DEPTH_COMPONENT24, // Or GL_DEPTH_COMPONENT16/GL_DEPTH_COMPONENT32F
		drawable_size.x,
		drawable_size.y,
		0,
		GL_DEPTH_COMPONENT,
		GL_FLOAT, // Or GL_UNSIGNED_BYTE for lower precision
		NULL
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (color_tex == 0) glGenTextures(1, &color_tex);
	glBindTexture(GL_TEXTURE_2D, color_tex);
	glTexImage2D(GL_TEXTURE_2D, 0,
		GL_RGB16F,
		drawable_size.x,
		drawable_size.y,
		0,
		GL_RGB,
		GL_FLOAT,
		nullptr
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (dof_blur_x_tex == 0) glGenTextures(1, &dof_blur_x_tex);
	glBindTexture(GL_TEXTURE_2D, dof_blur_x_tex);
	glTexImage2D(GL_TEXTURE_2D, 0,
		GL_RGB16F,
		drawable_size.x,
		drawable_size.y,
		0,
		GL_RGB,
		GL_FLOAT,
		nullptr
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);


	if (main_fb == 0) {
		glGenFramebuffers(1, &main_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, main_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, main_fb);
	gl_check_fb(); //<-- helper function to check framebuffer completeness
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (dof_blur_x_fb == 0) {
		glGenFramebuffers(1, &dof_blur_x_fb);
		glBindFramebuffer(GL_FRAMEBUFFER, dof_blur_x_fb);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dof_blur_x_tex, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	GL_ERRORS();
}

struct DoFBlurXProgram {
	DoFBlurXProgram() {
		program = gl_compile_program(
			//vertex shader -- draws a fullscreen triangle using no attribute streams
			"#version 330\n"
			"out vec2 texCoord;\n"
			"void main() {\n"
			"	gl_Position = vec4(4 * (gl_VertexID & 1) - 1,  2 * (gl_VertexID & 2) - 1, 0.0, 1.0);\n"
			"	texCoord = gl_Position.xy * 0.5 + 0.5;\n"
			"}\n"
		,
			//fragment shader -- 
			"#version 330\n"
			"in vec2 texCoord;\n"

			"uniform sampler2D depthTex;\n"
			"uniform sampler2D colorTex;\n"
			"uniform float nearClip;\n"
			"uniform float farClip;\n"
			"uniform float focalDistance;\n"
			"uniform int blurScale;\n"
			"uniform float blurMax;\n"

			"out vec4 fragColor;\n"

			"float LinearizeDepth(float depth)\n" // return world space distance from camera
			"{\n"
			"   float z = depth * 2.0 - 1.0;\n"
			"   return (2.0 * nearClip * farClip) / (farClip + nearClip - z * (farClip - nearClip));\n"
			"}\n"

			"float computeBlurRadius(float depth)\n"
			"{\n"
			"   float x = (depth - focalDistance) / farClip;\n"
			"   float coc = clamp(4 * x * x, 0.0, 1.0);\n" // if focalDistance was 0.5, this easing function outputs 1 when normalized depth is 0 and 1
			"   return clamp(blurScale * coc, 0.0, blurMax);\n"
			"}\n"

			"void main() {\n"
			"   float depth = LinearizeDepth(texture(depthTex, texCoord).r);\n"
			"   float f_radius = computeBlurRadius(depth);\n"


			// If nearly in focus, just show original color:
			"if (f_radius < 1.5) {\n"
			"	fragColor = texture(colorTex, texCoord);\n"
			"	return;\n"
			"}\n"

			"vec2 texelSize = 1.0 / textureSize(colorTex, 0);\n"
			"int radius = int(f_radius);\n"

			"vec3 acc  = vec3(0.0);\n"
			"float wSum = 0.0;\n"

			"int dx = max(1, radius / 30);" // take at most 2*30 samples for performance

			"for (int x = -radius; x <= radius; x += dx)\n"
			"{\n"
			"	float dist = float(x);\n"
			"	vec2 uv = texCoord + vec2(dist, 0.0) * texelSize;\n"

			"   float w = exp(-0.5 * (dist * dist) / (radius * radius));\n"
			"   float sampleDepth = LinearizeDepth(texture(depthTex, uv).r);\n"
			"   if (focalDistance - 10 < sampleDepth && sampleDepth < focalDistance + 10 && int(sampleDepth) < depth)\n"
			"   {\n"
			"      float sampleRadius = computeBlurRadius(sampleDepth);\n"
			"      if (int(sampleRadius) < radius)\n"
			"      {\n"
			"         w *= 0.1;\n"
			"      }\n"
			"   }\n"
			
			"	acc  += texture(colorTex, uv).rgb * w;\n"
			"	wSum += w;\n"
			"}\n"

			"fragColor = vec4(acc / wSum, 1.0);\n"
			"}\n"
		);
		
		GLuint depthTex_sampler2D = glGetUniformLocation(program, "depthTex");
		GLuint colorTex_sampler2D = glGetUniformLocation(program, "colorTex");
		glUseProgram(program);
		glUniform1i(depthTex_sampler2D, 0);
		glUniform1i(colorTex_sampler2D, 1);

		glUniform1f(glGetUniformLocation(program, "nearClip"), NEAR_CLIP);
		glUniform1f(glGetUniformLocation(program, "farClip"), FAR_CLIP);
		glUniform1f(glGetUniformLocation(program, "focalDistance"), FOCAL_DISTANCE);
		glUniform1f(glGetUniformLocation(program, "blurScale"), BLUR_SCALE);
		glUniform1f(glGetUniformLocation(program, "blurMax"), BLUR_RADIUS_MAX);

		glUseProgram(0);

		GL_ERRORS();
	}

	GLuint program = 0;
};

GLuint empty_vao = 0;
Load< DoFBlurXProgram > dof_blur_x_program(LoadTagEarly, []() -> DoFBlurXProgram const * {
	glGenVertexArrays(1, &empty_vao);
	return new DoFBlurXProgram();
});

struct DoFBlurYProgram {
	DoFBlurYProgram() {
		program = gl_compile_program(
			//vertex shader -- draws a fullscreen triangle using no attribute streams
			"#version 330\n"
			"out vec2 texCoord;\n"
			"void main() {\n"
			"	gl_Position = vec4(4 * (gl_VertexID & 1) - 1,  2 * (gl_VertexID & 2) - 1, 0.0, 1.0);\n"
			"	texCoord = gl_Position.xy * 0.5 + 0.5;\n"
			"}\n"
		,
			//fragment shader -- 
			"#version 330\n"
			"in vec2 texCoord;\n"

			"uniform sampler2D depthTex;\n"
			"uniform sampler2D blurXTex;\n"
			"uniform float nearClip;\n"
			"uniform float farClip;\n"
			"uniform float focalDistance;\n"
			"uniform int blurScale;\n"
			"uniform float blurMax;\n"

			"out vec4 fragColor;\n"

			"float LinearizeDepth(float depth)\n" // return world space distance from camera
			"{\n"
			"   float z = depth * 2.0 - 1.0;\n"
			"   return (2.0 * nearClip * farClip) / (farClip + nearClip - z * (farClip - nearClip));\n"
			"}\n"

			"float computeBlurRadius(float depth)\n"
			"{\n"
			"   float x = (depth - focalDistance) / farClip;\n"
			"   float coc = clamp(4 * x * x, 0.0, 1.0);\n" // if focalDistance was 0.5, this easing function outputs 1 when normalized depth is 0 and 1
			"   return clamp(blurScale * coc, 0.0, blurMax);\n"
			"}\n"

			"void main() {\n"
			"   float depth = LinearizeDepth(texture(depthTex, texCoord).r);\n"
			"   float f_radius = computeBlurRadius(depth);\n"


			// If nearly in focus, just show original color:
			"if (f_radius < 1.5) {\n"
			"	fragColor = texture(blurXTex, texCoord);\n"
			"	return;\n"
			"}\n"

			"vec2 texelSize = 1.0 / textureSize(blurXTex, 0);\n"
			"int radius = int(f_radius);\n"

			"vec3 acc  = vec3(0.0);\n"
			"float wSum = 0.0;\n"

			"int dy = max(1, radius / 30);" // take at most 2*30 samples for performance

			"for (int y = -radius; y <= radius; y += dy)\n"
			"{\n"
			"	float dist = float(y);\n"
			"	vec2 uv = texCoord + vec2(0.0, dist) * texelSize;\n"

			"   float w = exp(-0.5 * (dist * dist) / (radius * radius));\n"
			"   float sampleDepth = LinearizeDepth(texture(depthTex, uv).r);\n"
			"   if (focalDistance - 10 < sampleDepth && sampleDepth < focalDistance + 10 && int(sampleDepth) < depth)\n"
			"   {\n"
			"      float sampleRadius = computeBlurRadius(sampleDepth);\n"
			"      if (int(sampleRadius) < radius)\n"
			"      {\n"
			"         w *= 0.1;\n"
			"      }\n"
			"   }\n"
			
			"	acc  += texture(blurXTex, uv).rgb * w;\n"
			"	wSum += w;\n"
			"}\n"

			"fragColor = vec4(acc / wSum, 1.0);\n"
			"}\n"
		);

		GLuint depthTex_sampler2D = glGetUniformLocation(program, "depthTex");
		GLuint colorTex_sampler2D = glGetUniformLocation(program, "blurXTex");
		glUseProgram(program);
		glUniform1i(depthTex_sampler2D, 0);
		glUniform1i(colorTex_sampler2D, 1);

		glUniform1f(glGetUniformLocation(program, "nearClip"), NEAR_CLIP);
		glUniform1f(glGetUniformLocation(program, "farClip"), FAR_CLIP);
		glUniform1f(glGetUniformLocation(program, "focalDistance"), FOCAL_DISTANCE);
		glUniform1f(glGetUniformLocation(program, "blurScale"), BLUR_SCALE);
		glUniform1f(glGetUniformLocation(program, "blurMax"), BLUR_RADIUS_MAX);

		glUseProgram(0);

		GL_ERRORS();
	}

	GLuint program = 0;
};

Load< DoFBlurYProgram > dof_blur_y_program(LoadTagEarly, []() -> DoFBlurYProgram const * {
	glGenVertexArrays(1, &empty_vao);
	return new DoFBlurYProgram();
});

void Framebuffers::dof_blur() {
	glDisable(GL_DEPTH_TEST);

	glBindFramebuffer(GL_FRAMEBUFFER, dof_blur_x_fb);
	glUseProgram(dof_blur_x_program->program);
	glBindVertexArray(empty_vao);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, depth_tex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, color_tex);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(dof_blur_y_program->program);
	glBindVertexArray(empty_vao);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, depth_tex);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, dof_blur_x_tex);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	GL_ERRORS();
}