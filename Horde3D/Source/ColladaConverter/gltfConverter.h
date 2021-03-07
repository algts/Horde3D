#pragma once

#include "converterCommon.h"
#include "gltf/tiny_gltf.h"

namespace Horde3D {
namespace AssetConverter {

// Type of processed node
enum class GLTFNodeType
{
	Transformation,
	Mesh,
	Joint
};

enum class GLTFDataType
{
	VertexID,
	VertexPosition,
	VertexRotation,
	VertexScale,
	TextureCoordinates,
	Normal,
	AnimationPosition,
	AnimationRotation,
	AnimationScale
};

struct VertexParameters
{
	int gltfPosIndex = 0;
};

struct SceneNodeParameters
{
	int nodeID = -1;
	int meshID = -1;
	int skinID = -1;
	
	bool morphTargets = false;
};

struct MeshParameters
{
	
};
// struct JointParameters
// {
// 	// Temporary
// 	Matrix4f      daeInvBindMat;
// };

// Reader function	
bool readGLTFModel( tinygltf::Model &mdl, bool binary, const std::string &pathToFile, std::string &errors, std::string &warnings );


class GLTFConverter : public IConverter
{
public:
	GLTFConverter( const tinygltf::Model &model, const std::string &outPath, const float *lodDists );
	~GLTFConverter();

	bool convertModel( bool optimize ) override;


	bool writeModel( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const override;


	bool writeMaterials( const std::string &assetPath, const std::string &modelName, bool replace ) const override;


	bool hasAnimation() const override;


	bool writeAnimation( const std::string &assetPath, const std::string &assetName ) const override;


	SceneNode * createSceneNode( AvailableSceneNodeTypes type ) override;

protected:
	size_t getAnimationTotalFrameCount();
	size_t getAnimationFrameCount( int animIndex, int nodeID );
	Matrix4f getNodeTransform( tinygltf::Node &node, int nodeId, unsigned int frame );

	int findAnimationIndex( tinygltf::Model &model, int nodeId );

	static_any< 32 > getNodeData( GLTFDataType type, int nodeId, int index );

	GLTFNodeType validateInstance( const tinygltf::Node &node, int nodeId );

	SceneNode *processNode( tinygltf::Node &node, int nodeId, SceneNode *parentNode,
							Matrix4f transAccum, std::vector< Matrix4f > animTransAccum );

	void processTriGroup( tinygltf::Mesh *geo, unsigned int geoTriGroupIndex, SceneNodeParameters *meshParams, int skinID, std::vector<Joint *> &jointLookup, unsigned int i );
	void processMeshes( bool optimize );

	void processMaterials();

private:

	tinygltf::Model _model;

	bool			_splitAnimations;
};

} // namespace AssetConverter
} // namespace Horde3D