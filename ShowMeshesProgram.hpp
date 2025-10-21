#pragma once

#include "GL.hpp"
#include "Load.hpp"

#include "Scene.hpp"

//Shader program that provides various modes for visualizing positions,
// colors, normals, and texture coordinates; mostly useful for debugging.
struct ShowMeshesProgram {
	ShowMeshesProgram();
	~ShowMeshesProgram();

	GLuint program = 0;

	//Attribute (per-vertex variable) locations:
	GLuint Position_vec4 = -1U;
	GLuint Normal_vec3 = -1U;
	GLuint Color_vec4 = -1U;
	GLuint TexCoord_vec2 = -1U;

	//Uniform (per-invocation variable) locations:
	GLuint CLIP_FROM_OBJECT_mat4 = -1U;
	GLuint LIGHT_FROM_OBJECT_mat4x3 = -1U;
	GLuint LIGHT_FROM_NORMAL_mat3 = -1U;

	GLuint INSPECT_MODE_int = -1U; //0: basic lighting; 1: position only; 2: normal only; 3: color only; 4: texcoord only

	//Textures:
	//no textures used
};

extern Load< ShowMeshesProgram > show_meshes_program;
extern Scene::Drawable::Pipeline show_meshes_program_pipeline; //Drawable::Pipeline already initialized with proper uniform locations for this program.
