#include "ShowSceneProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Scene::Drawable::Pipeline show_scene_program_pipeline;

Load< ShowSceneProgram > show_scene_program(LoadTagEarly, []() -> ShowSceneProgram * {
	auto *ret = new ShowSceneProgram();

	show_scene_program_pipeline.program = ret->program;

	show_scene_program_pipeline.CLIP_FROM_OBJECT_mat4 = ret->CLIP_FROM_OBJECT_mat4;
	show_scene_program_pipeline.LIGHT_FROM_OBJECT_mat4x3 = ret->LIGHT_FROM_OBJECT_mat4x3;
	show_scene_program_pipeline.LIGHT_FROM_NORMAL_mat3 = ret->LIGHT_FROM_NORMAL_mat3;

	return ret;
});

ShowSceneProgram::ShowSceneProgram() {
	//Compile vertex and fragment shaders using the convenient 'gl_compile_program' helper function:
	program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"uniform mat4 CLIP_FROM_OBJECT;\n"
		"uniform mat4x3 LIGHT_FROM_OBJECT;\n"
		"uniform mat3 LIGHT_FROM_NORMAL;\n"
		"in vec4 Position;\n"
		"in vec3 Normal;\n"
		"in vec4 Color;\n"
		"in vec2 TexCoord;\n"
		"out vec3 position;\n"
		"out vec3 normal;\n"
		"out vec4 color;\n"
		"out vec2 texCoord;\n"
		"void main() {\n"
		"	gl_Position = CLIP_FROM_OBJECT * Position;\n"
		"	position = LIGHT_FROM_OBJECT * Position;\n"
		"	normal = LIGHT_FROM_NORMAL * Normal;\n"
		"	color = Color;\n"
		"	texCoord = TexCoord;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"uniform int INSPECT_MODE;\n"
		"in vec3 position;\n"
		"in vec3 normal;\n"
		"in vec4 color;\n"
		"in vec2 texCoord;\n"
		"out vec4 fragColor;\n"
		"vec3 grid(vec3 p) {\n"
		"	vec3 ret;\n"
		"	ret.x = fract(p.x);\n"
		"	ret.y = fract(p.y);\n"
		"	ret.z = fract(p.z);\n"
		"	return ret;\n"
		"}\n"
		"void main() {\n"
		"	vec3 n = normalize(normal);\n"
		"	if (INSPECT_MODE == 1) {\n"
		"		fragColor = vec4(grid(position), 1.0);\n"
		"	} else if (INSPECT_MODE == 2) {\n"
		"		fragColor = vec4((0.5 * n) + 0.5, 1.0);\n"
		"	} else if (INSPECT_MODE == 3) {\n"
		"		fragColor = color;\n"
		"	} else if (INSPECT_MODE == 4) {\n"
		"		fragColor = vec4(grid(vec3(texCoord,0.0)), 1.0);\n"
		"	} else {\n"
		"		vec3 l = vec3(0.0,0.0,1.0);\n"
		"		fragColor = vec4(mix(vec3(0.5), vec3(1.0), 0.5 * dot(n,l) + 0.5) * color.rgb, color.a);\n"
		"	}\n"
		"}\n"
	);

	//look up the locations of vertex attributes:
	Position_vec4 = glGetAttribLocation(program, "Position");
	Normal_vec3 = glGetAttribLocation(program, "Normal");
	Color_vec4 = glGetAttribLocation(program, "Color");
	TexCoord_vec2 = glGetAttribLocation(program, "TexCoord");

	//look up the locations of uniforms:
	CLIP_FROM_OBJECT_mat4 = glGetUniformLocation(program, "CLIP_FROM_OBJECT");
	LIGHT_FROM_OBJECT_mat4x3 = glGetUniformLocation(program, "LIGHT_FROM_OBJECT");
	LIGHT_FROM_NORMAL_mat3 = glGetUniformLocation(program, "LIGHT_FROM_NORMAL");

	INSPECT_MODE_int = glGetUniformLocation(program, "INSPECT_MODE");
}

ShowSceneProgram::~ShowSceneProgram() {
	glDeleteProgram(program);
	program = 0;
}

