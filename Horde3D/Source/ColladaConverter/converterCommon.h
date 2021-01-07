#pragma once

#include <string>
#include <vector>
#include "utMath.h"

namespace Horde3D {
namespace AssetConverter {

struct Joint;

// Overloaded for each converter
struct VertexParameters;
struct SceneNodeParameters;
struct MeshParameters;
struct JointParameters;

struct Vertex
{
	Vec3f  storedPos, pos;
	Vec3f  storedNormal, normal, tangent, bitangent;
	Vec3f  texCoords[ 4 ];
	Joint  *joints[ 4 ];
	float  weights[ 4 ];

// 	void   *converterVertexParams;
 	int    daePosIndex;


	Vertex()
	{
		joints[ 0 ] = 0x0; joints[ 1 ] = 0x0; joints[ 2 ] = 0x0; joints[ 3 ] = 0x0;
		weights[ 0 ] = 1; weights[ 1 ] = 0; weights[ 2 ] = 0; weights[ 3 ] = 0;

// 		converterVertexParams = nullptr;
	}

// 	~Vertex()
// 	{
// 		if ( converterVertexParams )
// 		{
// 			delete converterVertexParams; converterVertexParams = nullptr;
// 		}
// 	}
};


struct TriGroup
{
	unsigned int  first, count;
	unsigned int  vertRStart, vertREnd;
	std::string   matName;

	unsigned int                 numPosIndices;
	std::vector< unsigned int >  *posIndexToVertices;

	TriGroup() : posIndexToVertices( 0x0 )
	{
	}

	~TriGroup() { delete[] posIndexToVertices; }
};


struct MorphDiff
{
	unsigned int  vertIndex;
	Vec3f         posDiff;
	Vec3f         normDiff, tanDiff, bitanDiff;
};


struct MorphTarget
{
	char                      name[ 256 ];
	std::vector< MorphDiff >  diffs;

	MorphTarget()
	{
		memset( name, 0, sizeof( name ) );
	}
};


struct SceneNode
{
	char                        name[ 256 ];
	Matrix4f                    matRel, matAbs;
	SceneNode                   *parent;
	std::vector< SceneNode * >  children;

	// Animation
	std::vector< Matrix4f >     frames;  // Relative transformation for every frame

	void						*converterNodeParams;

	bool                        typeJoint;

	SceneNode()
	{
		memset( name, 0, sizeof( name ) );
		parent = 0x0;
		converterNodeParams = nullptr;
	}

	virtual ~SceneNode()
	{
		for ( unsigned int i = 0; i < children.size(); ++i ) delete children[ i ];

		if ( converterNodeParams ) delete converterNodeParams;
	}
};


struct Mesh : public SceneNode
{
	std::vector< TriGroup* > triGroups;
	unsigned int             lodLevel;

	void					*converterMeshParams;

	Mesh(): SceneNode()
	{
		typeJoint = false;
		parent = 0x0;
		lodLevel = 0;
		converterMeshParams = nullptr;
	}

	~Mesh() 
	{
		for ( size_t i = triGroups.size(); i > 0; ) delete triGroups[ --i ]; 
		
		if ( converterMeshParams ) delete converterMeshParams;
	}
};


struct Joint : public SceneNode
{
	unsigned int		index;
	Matrix4f			invBindMat;
	bool				used;

	void				*converterJointParams;

	Joint()
	{
		typeJoint = true;
		used = false;
		converterJointParams = nullptr;
	}

	~Joint()
	{
		if ( converterJointParams ) delete converterJointParams;
	} 
};

enum class AvailableSceneNodeTypes
{
	Mesh,
	Joint
};

class IConverter
{
public:

	IConverter() {}
	virtual ~IConverter() {}

	virtual bool convertModel( bool optimize ) = 0;

	virtual bool writeModel( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const = 0;
	virtual bool writeMaterials( const std::string &assetPath, const std::string &modelName, bool replace ) const = 0;
	virtual bool hasAnimation() const = 0;
	virtual bool writeAnimation( const std::string &assetPath, const std::string &assetName ) const = 0;

	virtual SceneNode *createSceneNode( AvailableSceneNodeTypes type ) = 0;
protected:

	std::vector< Vertex >				_vertices;
	std::vector< unsigned int >			_indices;

	std::string							_outPath;
	float								_lodDist1, _lodDist2, _lodDist3, _lodDist4;
	unsigned int						_frameCount;
	unsigned int						_maxLodLevel;
	bool								_animNotSampled;

};

} // namespace AssetConverter
} // namespace Horde3D