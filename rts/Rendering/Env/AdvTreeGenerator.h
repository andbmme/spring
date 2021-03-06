/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _ADV_TREE_GENERATOR_H
#define _ADV_TREE_GENERATOR_H

#include "Rendering/GL/myGL.h"
#include "Rendering/GL/VertexArrayTypes.h"

#define MAX_TREE_HEIGHT 60

class CBitmap;

namespace Shader {
	struct IProgramObject;
}

class CAdvTreeGenerator
{
public:
	~CAdvTreeGenerator();

	void Init();

	void BindPineTreeBuffer(unsigned int i) const;
	void BindBushTreeBuffer(unsigned int i) const;
	void DrawPineTreeBuffer(unsigned int i) const;
	void DrawBushTreeBuffer(unsigned int i) const;

	unsigned int GetPineBuffer() const { return pineBuffer; }
	unsigned int GetBushBuffer() const { return bushBuffer; }
	unsigned int GetBarkTex() const { return barkTex; }

private:
	bool CopyBarkTexMem(const std::string& barkTexFile);
	bool GenBarkTextures(const std::string& leafTexFile);
	void GenVertexBuffers();

	void CreateLeaves(const float3& start, const float3& dir, float length, float3& orto1, float3& orto2);
	void TrunkIterator(const float3& start, const float3& dir, float length, float size, int depth);
	void DrawTrunk(const float3& start, const float3& end, const float3& orto1, const float3& orto2, float size);
	void DrawPineTrunk(const float3& start, const float3& end, float size);
	void DrawPineBranch(const float3& start, const float3& dir, float size);
	void CreateGranTexBranch(const float3& start, const float3& end);

	void GenPineTree(int numBranch, float height);
	void GenBushTree(int numBranch, float height, float width);

	void CreateGranTex(uint8_t* data, int xpos, int ypos, int xsize);
	void CreateLeafTex(uint8_t* data, int xpos, int ypos, int xsize);

private:
	VA_TYPE_TN* prvVertPtr = nullptr;
	VA_TYPE_TN* curVertPtr = nullptr;

	size_t numBushVerts[8] = {0};
	size_t numPineVerts[8] = {0};

	GLuint bushBuffer = 0;
	GLuint pineBuffer = 0;

	GLuint barkTex = 0;
};

#endif // _ADV_TREE_GENERATOR_H

