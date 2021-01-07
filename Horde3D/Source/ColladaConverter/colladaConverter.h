// *************************************************************************************************
//
// Horde3D
//   Next-Generation Graphics Engine
// --------------------------------------
// Copyright (C) 2006-2020 Nicolas Schulz and Horde3D team
//
// This software is distributed under the terms of the Eclipse Public License v1.0.
// A copy of the license may be obtained at: http://www.eclipse.org/legal/epl-v10.html
//
// *************************************************************************************************

#ifndef _ColladaConverter_H_
#define _ColladaConverter_H_

#include "daeMain.h"

#include "converterCommon.h"
#include <string.h> // memset

namespace Horde3D {
namespace AssetConverter {

namespace ColladaConverterNS {

struct VertexParameters
{
	int    daePosIndex = 0;
};

struct SceneNodeParameters
{
	DaeNode                     *daeNode = nullptr;
	DaeInstance                 *daeInstance = nullptr;
};

struct JointParameters
{
	// Temporary
	Matrix4f      daeInvBindMat;
};

class ColladaConverter : public IConverter
{
public:
	ColladaConverter( ColladaDocument &doc, const std::string &outPath, const float *lodDists );
	~ColladaConverter();
	
	bool convertModel( bool optimize ) override;
	
	bool writeModel( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const override;
	bool writeMaterials( const std::string &assetPath, const std::string &modelName, bool replace ) const override;
	bool hasAnimation() const override;
	bool writeAnimation( const std::string &assetPath, const std::string &assetName ) const override;

	SceneNode *createSceneNode( AvailableSceneNodeTypes type ) override;

private:
	Matrix4f getNodeTransform( DaeNode &node, unsigned int frame );
	SceneNode *findNode( const char *name, SceneNode *ignoredNode );
	void checkNodeName( SceneNode *node );
	bool validateInstance( const std::string &instanceId ) const;
	SceneNode *processNode( DaeNode &node, SceneNode *parentNode,
	                        Matrix4f transAccum, std::vector< Matrix4f > animTransAccum );
	void calcTangentSpaceBasis( std::vector< Vertex > &vertices ) const;
	void processJoints();
	void processMeshes( bool optimize );
	bool writeGeometry( const std::string &assetPath, const std::string &assetName ) const;
	void writeSGNode( const std::string &assetPath, const std::string &modelName, SceneNode *node, unsigned int depth, std::ofstream &outf ) const;
	bool writeSceneGraph( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const;
	void writeAnimFrames( SceneNode &node, FILE *f ) const;

private:
	ColladaDocument						&_daeDoc;
	
	std::vector< Mesh * >				_meshes;
	std::vector< Joint * >				_joints;
	std::vector< MorphTarget >			_morphTargets;
	std::vector< SceneNode* >			_nodes;
};


} // namespace ColladaConverterNS
} // namespace AssetConverter
} // namespace Horde3D

#endif // _ColladaConverter_H_
