/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */


#include "LuaMaterial.h"

#include "LuaHashString.h"
#include "LuaHandle.h"
#include "LuaOpenGL.h"

#include "Game/Camera.h"
#include "Rendering/ShadowHandler.h"
#include "Rendering/Env/ISky.h"
#include "Sim/Objects/SolidObject.h"
#include "System/Log/ILog.h"
#include "System/Util.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>


LuaMatHandler LuaMatHandler::handler;
LuaMatHandler& luaMatHandler = LuaMatHandler::handler;


#define STRING_CASE(ptr, x) case x: ptr = #x; break;


/******************************************************************************/
/******************************************************************************/
//
//  LuaObjectUniforms
//

void LuaObjectUniforms::SetLocs(const LuaMatShader* s)
{
	// FIXME: <s> can be forward or deferred, do not know which here
	if (false && setUniforms)
		return;

	// speedUniform.loc = glGetUniformLocation(s->openglID, "speed");
	// healthUniform.loc = glGetUniformLocation(s->openglID, "health");
	tcolorUniform.loc = glGetUniformLocation(s->openglID, "teamColor");

	setUniforms = true;
	haveUniforms = false;
	// haveUniforms |= (speedUniform.loc != -1);
	// haveUniforms |= (healthUniform.loc != -1);
	haveUniforms |= (tcolorUniform.loc != -1);
}

void LuaObjectUniforms::SetData(unsigned int type, const void* data)
{
	switch (type) {
		// case UNIFORM_SPEED: {} break;
		// case UNIFORM_HEALTH: {} break;

		case UNIFORM_TCOLOR: {
			const float* vals = reinterpret_cast<const float*>(data);

			tcolorUniform.val[0] = vals[0];
			tcolorUniform.val[1] = vals[1];
			tcolorUniform.val[2] = vals[2];
			tcolorUniform.val[3] = vals[3];
		} break;
	}
}

void LuaObjectUniforms::Execute() const
{
	if (!haveUniforms)
		return;

	// if (speedUniform.loc >= 0) { glUniform3fv(speedUniform.loc, 1, &speedUniform.val[0]); }
	// if (healthUniform.loc >= 0) { glUniform1f(healthUniform.loc, healthUniform.val[0]); }
	if (tcolorUniform.loc >= 0) { glUniform4fv(tcolorUniform.loc, 1, &tcolorUniform.val[0]); }
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaObjectMaterial
//

bool LuaObjectMaterial::SetLODCount(unsigned int count)
{
	lodCount = count;
	lastLOD = lodCount - 1;
	lodMats.resize(count);
	return true;
}


bool LuaObjectMaterial::SetLastLOD(unsigned int lod)
{
	lastLOD = std::min(lod, lodCount - 1);
	return true;
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaMatShader
//

void LuaMatShader::Finalize()
{
	if (type != LUASHADER_GL) {
		openglID = 0;
	}
}


int LuaMatShader::Compare(const LuaMatShader& a, const LuaMatShader& b)
{
	if (a.type != b.type) {
		return (a.type < b.type) ? -1 : +1;
	}
	if (a.type == LUASHADER_GL) {
		if (a.openglID != b.openglID) {
			return (a.openglID < b.openglID) ? -1 : +1;
		}
	}
	return 0; // LUASHADER_NONE and LUASHADER_SHADOWMAP ignore openglID
}


void LuaMatShader::Execute(const LuaMatShader& prev, bool deferredPass) const
{
	if (type != prev.type) {
		switch (prev.type) {
			case LUASHADER_GL: {
				glUseProgram(0);
			} break;
			case LUASHADER_DEF_3DO:
			case LUASHADER_DEF_S3O:
			case LUASHADER_DEF_OBJ:
			case LUASHADER_DEF_ASS: {
				if (luaMatHandler.resetDrawStateFuncs[prev.type]) {
					luaMatHandler.resetDrawStateFuncs[prev.type](prev.type, deferredPass);
				}
			} break;
			case LUASHADER_NONE: {} break;
			default: { assert(false); } break;
		}

		switch (type) {
			case LUASHADER_GL: {
				// custom shader
				glUseProgram(openglID);
			} break;
			case LUASHADER_DEF_3DO:
			case LUASHADER_DEF_S3O:
			case LUASHADER_DEF_OBJ:
			case LUASHADER_DEF_ASS: {
				if (luaMatHandler.setupDrawStateFuncs[type]) {
					luaMatHandler.setupDrawStateFuncs[type](type, deferredPass);
				}
			} break;
			case LUASHADER_NONE: {} break;
			default: { assert(false); } break;
		}
	}
	else if (type == LUASHADER_GL) {
		if (openglID != prev.openglID) {
			glUseProgram(openglID);
		}
	}
}


void LuaMatShader::Print(const string& indent) const
{
	const char* typeName = "Unknown";
	switch (type) {
		STRING_CASE(typeName, LUASHADER_NONE);
		STRING_CASE(typeName, LUASHADER_GL);
		STRING_CASE(typeName, LUASHADER_DEF_3DO);
		STRING_CASE(typeName, LUASHADER_DEF_S3O);
		STRING_CASE(typeName, LUASHADER_DEF_OBJ);
		STRING_CASE(typeName, LUASHADER_DEF_ASS);
		default: { assert(false); } break;
	}
	LOG("%s%s %i", indent.c_str(), typeName, openglID);
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaMaterial
//

const LuaMaterial LuaMaterial::defMat;

void LuaMaterial::Parse(
	lua_State* L,
	const int tableIdx,
	std::function<void(lua_State*, int, LuaMatShader&)> ParseShader,
	std::function<void(lua_State*, int, LuaMatTexture&)> ParseTexture,
	std::function<GLuint(lua_State*, int)> ParseDisplayList
) {
	const std::unordered_map<std::string, GLint*> uniformLocs = {
		{"cameraloc",        &viewMatrixLoc},
		{"viewmatrixloc",    &viewMatrixLoc},
		{"projmatrixloc",    &projMatrixLoc},
		{"viprmatrixloc",    &viprMatrixLoc},
		{"camerainvloc",     &viewMatrixInvLoc},
		{"viewmatrixinvloc", &viewMatrixInvLoc},
		{"projmatrixinvloc", &projMatrixInvLoc},
		{"viprmatrixinvloc", &viprMatrixInvLoc},

		{"cameraposloc",     &cameraPosLoc},
		{"cameradirloc",     &cameraDirLoc},

		{"sunposloc",        &sunDirLoc},
		{"sundirloc",        &sunDirLoc},

		{"shadowloc",        &shadowMatrixLoc},
		{"shadowmatrixloc",  &shadowMatrixLoc},
		{"shadowparamsloc",  &shadowParamsLoc},
	};

	for (lua_pushnil(L); lua_next(L, tableIdx) != 0; lua_pop(L, 1)) {
		if (!lua_israwstring(L, -2))
			continue;

		const string key = StringToLower(lua_tostring(L, -2));
		const auto loc = uniformLocs.find(key);

		// uniforms
		if (loc != uniformLocs.end()) {
			if (!lua_isnumber(L, -1))
				continue;

			*(loc->second) = static_cast<GLint>(lua_tonumber(L, -1));
		}

		// shaders
		if (key == "shader" || key == "standard_shader") {
			ParseShader(L, -1, shaders[LuaMatShader::LUASHADER_PASS_FWD]);
			continue;
		}
		if (key == "deferred" || key == "deferred_shader") {
			ParseShader(L, -1, shaders[LuaMatShader::LUASHADER_PASS_DFR]);
			continue;
		}

		// textures
		if (key.substr(0, 7) == "texunit") {
			if (key.size() < 8)
				continue;

			if (key[7] == 's') {
				// "texunits" = {[0] = string|table, ...}
				if (!lua_istable(L, -1))
					continue;

				const int texTable = (int)lua_gettop(L);

				for (lua_pushnil(L); lua_next(L, texTable) != 0; lua_pop(L, 1)) {
					if (!lua_israwnumber(L, -2))
						continue;

					const unsigned int texUnit = lua_toint(L, -2);

					if (texUnit >= LuaMatTexture::maxTexUnits)
						continue;

					ParseTexture(L, -1, textures[texUnit]);
				}
			} else {
				// "texunitX" = string|table
				const unsigned int texUnit = atoi(key.c_str() + 7);

				if (texUnit >= LuaMatTexture::maxTexUnits)
					continue;

				ParseTexture(L, -1, textures[texUnit]);
			}

			continue;
		}

		// dlists
		if (key == "prelist") {
			preList = ParseDisplayList(L, -1);
			continue;
		}
		if (key == "postlist") {
			postList = ParseDisplayList(L, -1);
			continue;
		}

		// misc
		if (key == "order") {
			order = luaL_checkint(L, -1);
			continue;
		}
		if (key == "culling") {
			if (lua_isnumber(L, -1))
				cullingMode = (GLenum)lua_tonumber(L, -1);

			continue;
		}
		if (key == "usecamera") {
			useCamera = lua_isboolean(L, -1) && lua_toboolean(L, -1);
			continue;
		}
	}
}

void LuaMaterial::Finalize()
{
	shaders[LuaMatShader::LUASHADER_PASS_FWD].Finalize();
	shaders[LuaMatShader::LUASHADER_PASS_DFR].Finalize();

	for (int t = 0; t < LuaMatTexture::maxTexUnits; t++) {
		textures[t].Finalize();

		if (textures[t].type != LuaMatTexture::LUATEX_NONE) {
			texCount = (t + 1);
		}
	}
}


void LuaMaterial::Execute(const LuaMaterial& prev, bool deferredPass) const
{
	if (prev.postList != 0)
		glCallList(prev.postList);
	if (preList != 0)
		glCallList(preList);

	shaders[deferredPass].Execute(prev.shaders[deferredPass], deferredPass);

	if (viewMatrixLoc >= 0)
		glUniformMatrix4fv(viewMatrixLoc, 1, GL_FALSE, camera->GetViewMatrix());
	if (projMatrixLoc >= 0)
		glUniformMatrix4fv(projMatrixLoc, 1, GL_FALSE, camera->GetProjectionMatrix());
	if (viprMatrixLoc >= 0)
		glUniformMatrix4fv(viprMatrixLoc, 1, GL_FALSE, camera->GetViewProjectionMatrix());

	if (viewMatrixInvLoc >= 0)
		glUniformMatrix4fv(viewMatrixInvLoc, 1, GL_FALSE, camera->GetViewMatrixInverse());
	if (projMatrixInvLoc >= 0)
		glUniformMatrix4fv(projMatrixInvLoc, 1, GL_FALSE, camera->GetProjectionMatrixInverse());
	if (viprMatrixInvLoc >= 0)
		glUniformMatrix4fv(viprMatrixInvLoc, 1, GL_FALSE, camera->GetViewProjectionMatrixInverse());

	if (cameraPosLoc >= 0)
		glUniformf3(cameraPosLoc, camera->GetPos());
	if (cameraDirLoc >= 0)
		glUniformf3(cameraDirLoc, camera->GetDir());

	if (sunDirLoc >= 0)
		glUniformf3(sunDirLoc, sky->GetLight()->GetLightDir());

	if (shadowMatrixLoc >= 0)
		glUniformMatrix4fv(shadowMatrixLoc, 1, GL_FALSE, shadowHandler->GetShadowMatrixRaw());
	if (shadowParamsLoc >= 0)
		glUniform4fv(shadowParamsLoc, 1, &(shadowHandler->GetShadowParams().x));


	const int maxTex = std::max(texCount, prev.texCount);

	for (int t = maxTex - 1; t >= 0; t--) {
		glActiveTexture(GL_TEXTURE0 + t);
		prev.textures[t].Unbind();
		textures[t].Bind();
	}

	if (useCamera != prev.useCamera) {
		if (useCamera) {
			glPopMatrix();
		} else {
			glPushMatrix();
			glLoadIdentity();
		}
	}

	if (cullingMode != prev.cullingMode) {
		if (cullingMode != 0) {
			glEnable(GL_CULL_FACE);
			glCullFace(cullingMode);
		} else {
			glDisable(GL_CULL_FACE);
		}
	}
}


int LuaMaterial::Compare(const LuaMaterial& a, const LuaMaterial& b)
{
	// NOTE: the order of the comparisons is important
	int cmp = 0;

	if (a.type != b.type)
		return (a.type < b.type) ? -1 : +1;  // should not happen
	if (a.order != b.order)
		return (a.order < b.order) ? -1 : +1;


	if ((cmp = LuaMatShader::Compare(a.shaders[LuaMatShader::LUASHADER_PASS_FWD], b.shaders[LuaMatShader::LUASHADER_PASS_FWD])) != 0)
		return cmp;

	if ((cmp = LuaMatShader::Compare(a.shaders[LuaMatShader::LUASHADER_PASS_DFR], b.shaders[LuaMatShader::LUASHADER_PASS_DFR])) != 0)
		return cmp;


	const int maxTex = std::min(a.texCount, b.texCount);

	for (int t = 0; t < maxTex; t++) {
		if ((cmp = LuaMatTexture::Compare(a.textures[t], b.textures[t])) != 0) {
			return cmp;
		}
	}


	if (a.texCount != b.texCount)
		return (a.texCount < b.texCount) ? -1 : +1;

	if (a.preList != b.preList)
		return (a.preList < b.preList) ? -1 : +1;
	if (a.postList != b.postList)
		return (a.postList < b.postList) ? -1 : +1;

	if (a.useCamera != b.useCamera)
		return a.useCamera ? -1 : +1;

	if (a.cullingMode != b.cullingMode)
		return (a.cullingMode < b.cullingMode) ? -1 : +1;

	if (a.viewMatrixLoc != b.viewMatrixLoc)
		return (a.viewMatrixLoc < b.viewMatrixLoc) ? -1 : +1;
	if (a.projMatrixLoc != b.projMatrixLoc)
		return (a.projMatrixLoc < b.projMatrixLoc) ? -1 : +1;
	if (a.viprMatrixLoc != b.viprMatrixLoc)
		return (a.viprMatrixLoc < b.viprMatrixLoc) ? -1 : +1;

	if (a.viewMatrixInvLoc != b.viewMatrixInvLoc)
		return (a.viewMatrixInvLoc < b.viewMatrixInvLoc) ? -1 : +1;
	if (a.projMatrixInvLoc != b.projMatrixInvLoc)
		return (a.projMatrixInvLoc < b.projMatrixInvLoc) ? -1 : +1;
	if (a.viprMatrixInvLoc != b.viprMatrixInvLoc)
		return (a.viprMatrixInvLoc < b.viprMatrixInvLoc) ? -1 : +1;

	if (a.cameraPosLoc != b.cameraPosLoc)
		return (a.cameraPosLoc < b.cameraPosLoc) ? -1 : +1;
	if (a.cameraDirLoc != b.cameraDirLoc)
		return (a.cameraDirLoc < b.cameraDirLoc) ? -1 : +1;

	if (a.sunDirLoc != b.sunDirLoc)
		return (a.sunDirLoc < b.sunDirLoc) ? -1 : +1;

	if (a.shadowMatrixLoc != b.shadowMatrixLoc)
		return (a.shadowMatrixLoc < b.shadowMatrixLoc) ? -1 : +1;
	if (a.shadowParamsLoc != b.shadowParamsLoc)
		return (a.shadowParamsLoc < b.shadowParamsLoc) ? -1 : +1;

	return 0;
}


static const char* GetMatTypeName(LuaMatType type)
{
	const char* typeName = "Unknown";
	switch (type) {
		STRING_CASE(typeName, LUAMAT_ALPHA);
		STRING_CASE(typeName, LUAMAT_OPAQUE);
		STRING_CASE(typeName, LUAMAT_ALPHA_REFLECT);
		STRING_CASE(typeName, LUAMAT_OPAQUE_REFLECT);
		STRING_CASE(typeName, LUAMAT_SHADOW);
		case LUAMAT_TYPE_COUNT: break;
	}
	return typeName;
}


void LuaMaterial::Print(const string& indent) const
{
#define CULL_TO_STR(x) \
	(x==GL_FRONT) ? "front" : (x==GL_BACK) ? "back" : (x!=0) ? "false" : "unknown"

	LOG("%s%s", indent.c_str(), GetMatTypeName(type));
	LOG("%sorder = %i", indent.c_str(), order);

	shaders[LuaMatShader::LUASHADER_PASS_FWD].Print(indent);
	shaders[LuaMatShader::LUASHADER_PASS_DFR].Print(indent);

	LOG("%stexCount = %i", indent.c_str(), texCount);

	for (int t = 0; t < texCount; t++) {
		char buf[32];
		SNPRINTF(buf, sizeof(buf), "%s  tex[%i] ", indent.c_str(), t);
		textures[t].Print(buf);
	}

	LOG("%spreList  = %i",  indent.c_str(), preList);
	LOG("%spostList = %i",  indent.c_str(), postList);

	LOG("%suseCamera   = %s", indent.c_str(), (useCamera ? "true" : "false"));
	LOG("%scullingMode = %s", indent.c_str(), CULL_TO_STR(cullingMode));

	LOG("%sviewMatrixLoc    = %i", indent.c_str(), viewMatrixLoc);
	LOG("%sprojMatrixLoc    = %i", indent.c_str(), projMatrixLoc);
	LOG("%sviewMatrixInvLoc = %i", indent.c_str(), viewMatrixInvLoc);
	LOG("%sprojMatrixInvLoc = %i", indent.c_str(), projMatrixInvLoc);

	LOG("%scameraPosLoc     = %i", indent.c_str(), cameraPosLoc);
	LOG("%scameraDirLoc     = %i", indent.c_str(), cameraDirLoc);

	LOG("%ssunDirLoc        = %i", indent.c_str(), sunDirLoc);

	LOG("%sshadowMatrixLoc  = %i", indent.c_str(), shadowMatrixLoc);
	LOG("%sshadowParamsLoc  = %i", indent.c_str(), shadowParamsLoc);
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaMatRef
//

LuaMatRef::LuaMatRef(LuaMatBin* _bin)
{
	bin = _bin;
	if (bin != NULL) {
		bin->Ref();
	}
}


LuaMatRef::~LuaMatRef()
{
	if (bin != NULL) {
		bin->UnRef();
	}
}


void LuaMatRef::Reset()
{
	if (bin != NULL) {
		bin->UnRef();
	}
	bin = NULL;
}


LuaMatRef& LuaMatRef::operator=(const LuaMatRef& mr)
{
	if (bin != mr.bin) {
		if (bin != NULL) { bin->UnRef(); }
		bin = mr.bin;
		if (bin != NULL) { bin->Ref(); }
	}
	return *this;
}


LuaMatRef::LuaMatRef(const LuaMatRef& mr)
{
	bin = mr.bin;
	if (bin != NULL) {
		bin->Ref();
	}
}


void LuaMatRef::AddUnit(CSolidObject* o)
{
	if (bin != NULL) {
		bin->AddUnit(o);
	}
}

void LuaMatRef::AddFeature(CSolidObject* o)
{
	if (bin != NULL) {
		bin->AddFeature(o);
	}
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaMatBin
//

void LuaMatBin::Ref()
{
	refCount++;
}


void LuaMatBin::UnRef()
{
	refCount--;
	if (refCount <= 0) {
		luaMatHandler.FreeBin(this);
	}
}


void LuaMatBin::Print(const string& indent) const
{
	LOG("%s|units| = " _STPF_, indent.c_str(), units.size());
	LOG("%s|features| = " _STPF_, indent.c_str(), features.size());
	LOG("%spointer = %p", indent.c_str(), this);
	LuaMaterial::Print(indent + "  ");
}


/******************************************************************************/
/******************************************************************************/
//
//  LuaMatHandler
//

LuaMatHandler::LuaMatHandler()
{
	for (unsigned int i = LuaMatShader::LUASHADER_NONE; i < LuaMatShader::LUASHADER_LAST; i++) {
		setupDrawStateFuncs[i] = nullptr;
		resetDrawStateFuncs[i] = nullptr;
	}

	prevMat = NULL;
}


LuaMatHandler::~LuaMatHandler()
{
	for (int m = 0; m < LUAMAT_TYPE_COUNT; m++) {
		LuaMatBinSet& binSet = binTypes[LuaMatType(m)];
		LuaMatBinSet::iterator it;
		for (it = binSet.begin(); it != binSet.end(); ++it) {
			delete *it;
		}
	}
}


LuaMatRef LuaMatHandler::GetRef(const LuaMaterial& mat)
{
	if ((mat.type < 0) || (mat.type >= LUAMAT_TYPE_COUNT)) {
		LOG_L(L_WARNING, "LuaMatHandler::GetRef() untyped material");
		return LuaMatRef();
	}
	LuaMatBinSet& binSet = binTypes[mat.type];

	LuaMatBin* fakeBin = (LuaMatBin*) &mat;
	LuaMatBinSet::iterator it = binSet.find(fakeBin);
	if (it != binSet.end()) {
		return LuaMatRef(*it);
	}

	LuaMatBin* bin = new LuaMatBin(mat);
	binSet.insert(bin);
	return LuaMatRef(bin);
}


void LuaMatHandler::ClearBins(LuaObjType objType, LuaMatType matType)
{
	if ((matType < 0) || (matType >= LUAMAT_TYPE_COUNT))
		return;

	LuaMatBinSet& binSet = binTypes[matType];
	LuaMatBinSet::iterator it;

	for (it = binSet.begin(); it != binSet.end(); ++it) {
		LuaMatBin* bin = *it;

		switch (objType) {
			case LUAOBJ_UNIT   : { bin->ClearUnits   (); } break;
			case LUAOBJ_FEATURE: { bin->ClearFeatures(); } break;
			default            : {        assert(false); } break;
		}
	}
}


void LuaMatHandler::ClearBins(LuaObjType objType)
{
	for (int m = 0; m < LUAMAT_TYPE_COUNT; m++) {
		ClearBins(objType, LuaMatType(m));
	}
}


void LuaMatHandler::FreeBin(LuaMatBin* bin)
{
	LuaMatBinSet& binSet = binTypes[bin->type];
	LuaMatBinSet::iterator it = binSet.find(bin);
	if (it != binSet.end()) {
		if (*it != bin) {
			LOG_L(L_WARNING, "LuaMatHandler::FreeBin pointer mismatch");
		}
		delete bin;
		binSet.erase(it);
	}
}


void LuaMatHandler::PrintBins(const string& indent, LuaMatType type) const
{
	if ((type < 0) || (type >= LUAMAT_TYPE_COUNT)) {
		return;
	}
	const LuaMatBinSet& binSet = binTypes[type];
	LuaMatBinSet::const_iterator it;
	int num = 0;
	LOG("%sBINCOUNT = " _STPF_, indent.c_str(), binSet.size());
	for (it = binSet.begin(); it != binSet.end(); ++it) {
		LuaMatBin* bin = *it;
		LOG("%sBIN %i:", indent.c_str(), num);
		bin->Print(indent + "    ");
		num++;
	}
}


void LuaMatHandler::PrintAllBins(const string& indent) const
{
	for (int m = 0; m < LUAMAT_TYPE_COUNT; m++) {
		string newIndent = indent + GetMatTypeName(LuaMatType(m));
		newIndent += "  ";
		PrintBins(newIndent, LuaMatType(m));
	}
}


/******************************************************************************/
/******************************************************************************/

float LuaObjectMaterialData::GLOBAL_LOD_FACTORS[LUAOBJ_LAST] = {1.0f, 1.0f};

