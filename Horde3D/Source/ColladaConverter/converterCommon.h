#pragma once

#include <string>
#include <vector>

#include "any.hpp"
#include "utMath.h"
#include "utPlatform.h"
#include "utEndian.h"

namespace Horde3D {
namespace AssetConverter {

// constants
#define VERTEX_PARAMETERS_SIZE		16 // in bytes
#define SCENENODE_PARAMETERS_SIZE	32 // in bytes
#define MESH_PARAMETERS_SIZE		16 // in bytes
#define JOINT_PARAMETERS_SIZE		64 // in bytes

// forward declarations
struct Joint;

// Overloaded for each converter
struct VertexParameters;
struct SceneNodeParameters;
struct MeshParameters;
struct JointParameters;

// little endian element writer
template<class T>
static inline void fwrite_le( const T* data, size_t count, FILE* f )
{
	char buffer[ 256 ];
	ASSERT( sizeof( T ) < sizeof( buffer ) );
	const size_t capacity = sizeof( buffer ) / sizeof( T );

	size_t i = 0;
	while ( i < count )
	{
		size_t nelems = std::min( capacity, ( count - i ) );
		data = ( const T* ) elemcpy_le( ( T* ) ( buffer ), data, nelems );
		fwrite( buffer, sizeof( T ), nelems, f );
		i += nelems;
	}
}

struct Vertex
{
	Vec3f  storedPos, pos;
	Vec3f  storedNormal, normal, tangent, bitangent;
	Vec3f  texCoords[ 4 ];
	Joint  *joints[ 4 ];
	float  weights[ 4 ];

	// 	Converter parameters for vertex
	static_any< VERTEX_PARAMETERS_SIZE > vp;
// 	int    daePosIndex;

	Vertex()
	{
		joints[ 0 ] = 0x0; joints[ 1 ] = 0x0; joints[ 2 ] = 0x0; joints[ 3 ] = 0x0;
		weights[ 0 ] = 1; weights[ 1 ] = 0; weights[ 2 ] = 0; weights[ 3 ] = 0;
	}
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


struct Material 
{
	std::string fileName;
	std::string diffuseMapFileName;
	std::string diffuseColor;
	std::string specularColor;
	std::string shininess;
};


struct SceneNode
{
	char                        name[ 256 ];
	Matrix4f                    matRel, matAbs;
	SceneNode                   *parent;
	std::vector< SceneNode * >  children;

	// Animation
	std::vector< Matrix4f >     frames;  // Relative transformation for every frame

	// Scene node parameters, used by specific converter
	static_any< SCENENODE_PARAMETERS_SIZE > scncp;

	bool                        typeJoint;

	SceneNode()
	{
		memset( name, 0, sizeof( name ) );
		parent = 0x0;
		typeJoint = false;
	}

	virtual ~SceneNode()
	{
		for ( unsigned int i = 0; i < children.size(); ++i ) delete children[ i ];
	}
};


struct Mesh : public SceneNode
{
	std::vector< TriGroup* > triGroups;
	unsigned int             lodLevel;

	// Mesh node parameters, used by specific converter
	static_any< MESH_PARAMETERS_SIZE > mshp;

	Mesh(): SceneNode()
	{
		typeJoint = false;
		parent = 0x0;
		lodLevel = 0;
	}

	~Mesh() 
	{
		for ( size_t i = triGroups.size(); i > 0; ) delete triGroups[ --i ]; 	
	}
};


struct Joint : public SceneNode
{
	unsigned int		index;
	Matrix4f			invBindMat;
	bool				used;

	// Joint node parameters, used by specific converter
	static_any< JOINT_PARAMETERS_SIZE > jp;

	Joint()
	{
		typeJoint = true;
		used = false;
		index = 0;
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

protected:
	virtual SceneNode *createSceneNode( AvailableSceneNodeTypes type ) = 0;

	void calcTangentSpaceBasis( std::vector<Vertex> &verts ) const;

	bool writeGeometry( const std::string &assetPath, const std::string &assetName ) const;

	void writeSGNode( const std::string &assetPath, const std::string &modelName, SceneNode *node, unsigned int depth, std::ofstream &outf ) const;
	bool writeSceneGraph( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const;
	bool writeMaterial( const Material &mat, bool replace ) const;
	void writeAnimFrames( SceneNode &node, FILE *f ) const;

	bool writeAnimationCommon( const std::string &assetPath, const std::string &assetName ) const;
protected:

	std::vector< Vertex >				_vertices;
	std::vector< unsigned int >			_indices;
	std::vector< Mesh * >				_meshes;
	std::vector< Joint * >				_joints;
	std::vector< MorphTarget >			_morphTargets;
	std::vector< SceneNode* >			_nodes;

	std::string							_outPath;
	float								_lodDist1, _lodDist2, _lodDist3, _lodDist4;
	unsigned int						_frameCount;
	unsigned int						_maxLodLevel;
	bool								_animNotSampled;

};

} // namespace AssetConverter
} // namespace Horde3D