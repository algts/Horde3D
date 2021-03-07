#include "gltfConverter.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "gltf/tiny_gltf.h"

#include "utils.h"
#include "optimizer.h"
#include <iosfwd>
#include <vector>
#include <map>
#include <string>

namespace Horde3D {
namespace AssetConverter {

bool readGLTFModel( tinygltf::Model &mdl, bool binary, const std::string &pathToFile, std::string &errors, std::string &warnings )
{
	tinygltf::TinyGLTF loader;

	if ( binary )
	{
		return loader.LoadBinaryFromFile( &mdl, &errors, &warnings, pathToFile );
	}
	else
	{
		return loader.LoadASCIIFromFile( &mdl, &errors, &warnings, pathToFile );
	}

	// never reaches
	return false;
}

static Matrix4f assembleMatrix( const tinygltf::Node &node ) 
{
	Matrix4f mat;

	// Check if node contains matrix property
	if ( !node.matrix.empty() )
	{
		for ( size_t i = 0; i < 16; ++i )
		{
			mat.x[ i ] = (float) node.matrix[ i ];
		}

		return mat.transposed();
	}

	// Node does not contain matrix, it should contain TRS properties (transform, rotate, scale)
	if ( !node.translation.empty() )
	{
		mat = mat * Matrix4f::TransMat( (float) node.translation[ 0 ], (float) node.translation[ 1 ],
										(float) node.translation[ 2 ] );
	}
	if ( !node.rotation.empty() )
	{
		mat = mat * Matrix4f::RotMat( Vec3f( (float) node.rotation[ 0 ], ( float ) node.rotation[ 1 ],
									  ( float ) node.rotation[ 2 ] ), degToRad( ( float ) node.rotation[ 3 ] ) );
	}
	if ( !node.scale.empty() )
	{
		mat = mat * Matrix4f::ScaleMat( (float) node.scale[ 0 ], ( float ) node.scale[ 1 ], ( float ) node.scale[ 2 ] );
	}

	return mat;
}

Matrix4f assembleAnimMatrix( const Vec3f &trans, const Quaternion &quat, const Vec3f &scale ) 
{
	Matrix4f mat;

	// translation
	mat = mat * Matrix4f::TransMat( trans.x, trans.y, trans.z );
	
	// rotation
	mat = mat * Matrix4f::RotMat( Vec3f( quat.x, quat.y, quat.z ), quat.w ); // angle always in radians

	// scale
	mat = mat * Matrix4f::ScaleMat( scale.x, scale.y, scale.z );

	return std::move( mat );
}

GLTFConverter::GLTFConverter( const tinygltf::Model &model, const std::string &outPath, const float *lodDists ) : IConverter(), _model( model )
{
	_outPath = outPath;

	_lodDist1 = lodDists[ 0 ];
	_lodDist2 = lodDists[ 1 ];
	_lodDist3 = lodDists[ 2 ];
	_lodDist4 = lodDists[ 3 ];

	_splitAnimations = true;
}

GLTFConverter::~GLTFConverter()
{

}

bool GLTFConverter::convertModel( bool optimize )
{
	if ( _model.scenes.empty() ) return true; // Nothing to convert

	// Get max animation frames from all animations
	_frameCount = (unsigned int) getAnimationTotalFrameCount();

	// Output default pose if no animation is available
	if ( _frameCount == 0 ) _frameCount = 1;

	std::vector< Matrix4f > animTransAccum;
	animTransAccum.resize( _frameCount );

	// Process all nodes
	if ( _model.scenes.size() > 1 )
		log( "GLTF file contains more than one scene. Only the first one will be processed." );

	auto loopSize = _model.scenes[ 0 ].nodes.size();
	for ( unsigned int i = 0; i < loopSize; ++i )
	{
		auto nodeIdx = _model.scenes[ 0 ].nodes[ i ];
		_nodes.push_back( processNode( _model.nodes[ nodeIdx ], nodeIdx, 0x0, Matrix4f(), animTransAccum ) );
	}

	if ( _animNotSampled )
		log( "Warning: Animation is not sampled and will probably be wrong" );

	// Process joints and meshes
	processJoints();
	processMeshes( optimize );

	return true;
}

SceneNode * GLTFConverter::createSceneNode( AvailableSceneNodeTypes type )
{
	switch ( type )
	{
		case AvailableSceneNodeTypes::Mesh:
		{
			auto mesh = new Mesh();
			_meshes.push_back( mesh );

			return mesh;
		}
		case AvailableSceneNodeTypes::Joint:
		{
			auto joint = new Joint();
			_joints.push_back( joint );

			return joint;
		}
		default:
			break;
	}

	return nullptr;
}

static inline int getByteOffset( int dataType, int componentType, int count )
{
	int byteOffset = 0;
	int compBytes = 0;
	int components = 0;

	switch ( componentType )
	{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			compBytes = 1;
			break;
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			compBytes = 2;
			break;
		case TINYGLTF_COMPONENT_TYPE_INT:
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
			compBytes = 4;
			break;
		case TINYGLTF_COMPONENT_TYPE_DOUBLE:
			compBytes = 8;
			break;
		default:
			log( "Unknown component type! Converted model will likely be incorrect." );
			compBytes = 4;
			break;
	}

	switch ( dataType )
	{
		case TINYGLTF_TYPE_SCALAR:
			components = 1;
			break;
		case TINYGLTF_TYPE_VEC2:
			components = 2;
			break;
		case TINYGLTF_TYPE_VEC3:
			components = 3;
			break;
		case TINYGLTF_TYPE_VEC4:
		case TINYGLTF_TYPE_MAT2:
			components = 4;
			break;
		case TINYGLTF_TYPE_MAT3:
			components = 9;
			break;
		case TINYGLTF_TYPE_MAT4:
			components = 16;
			break;
		default:
			log( "Unknown parameter type! Converted model will likely be incorrect." );
			components = 3;
			break;
	}

	return count * ( compBytes * components );
}

static_any< 32 > GLTFConverter::getNodeData( GLTFDataType type, int accessorId, int index )
{
	// get accessor and buffer view
	auto &accessor = _model.accessors[ accessorId ];
	auto &bufView = _model.bufferViews[ accessor.bufferView ];
	auto &buf = _model.buffers[ bufView.buffer ];
	auto byteOffset = getByteOffset( accessor.type, accessor.componentType, index );
	const float *posF = reinterpret_cast< const float * >( &buf.data[ bufView.byteOffset + accessor.byteOffset + byteOffset ] );
	const short *posS = reinterpret_cast< const short * >( &buf.data[ bufView.byteOffset + accessor.byteOffset + byteOffset ] );

	switch ( type )
	{
		case GLTFDataType::VertexID:
			if ( accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT )
				return ( int ) posS[ 0 ];
			else 
				return ( int ) posF[ 0 ];
		case GLTFDataType::VertexPosition:
		case GLTFDataType::VertexScale:
		case GLTFDataType::AnimationPosition:
		case GLTFDataType::AnimationScale:
		case GLTFDataType::Normal:
		{
			Vec3f v( posF[ 0 ], posF[ 1 ], posF[ 2 ] );
			return std::move( v );
		}
		case GLTFDataType::VertexRotation:
		case GLTFDataType::AnimationRotation:
		{
			Quaternion q( posF[ 0 ], posF[ 1 ], posF[ 2 ], posF[ 3 ] );
			return std::move( q );
		}
		case GLTFDataType::TextureCoordinates:
		{
			Vec3f t( posF[ 0 ], posF[ 1 ], 0.0f );
			return std::move( t );
		}
		default:
			break;
	}

	return 0;
}

int GLTFConverter::findAnimationIndex( tinygltf::Model &model, int nodeId )
{
	// Find animation that works with current node
	for ( size_t i = 0; i < model.animations.size(); ++i )
	{
		auto &a = model.animations[ i ];
		for ( size_t j = 0; j < a.channels.size(); ++j )
		{
			auto &chan = a.channels[ j ];
			if ( chan.target_node == nodeId ) return ( int ) i;
		}
	}

	return -1;
}

Matrix4f GLTFConverter::getNodeTransform( tinygltf::Node &node, int nodeId, unsigned int frame )
{
	// Note: Function assumes sampled animation data
	// In terms of GLTF: in animation sampler input refers to accessor where keyframe time is specified,
	// while output refers to accessor that holds the data for translation/rotation/scale/weights

	int animIndex = findAnimationIndex( _model, nodeId );
	if ( animIndex != -1 )
	{
		// get translation/rotation/scale channels and samplers
		int chan_t, chan_r, chan_s;
		chan_t = chan_r = chan_s = -1;
		
		int sampler_t, sampler_r, sampler_s;
		sampler_t = sampler_r = sampler_s = -1;
		int input, output;
		
		static_any< 32 > data_t, data_r, data_s;

		auto &anim = _model.animations[ animIndex ];
		for ( size_t i = 0; i < anim.channels.size(); ++i )
		{
			if ( anim.channels[ i ].target_node != nodeId ) continue;

			if ( anim.channels[ i ].target_path == "translation" )
			{
				chan_t = i;
				sampler_t = anim.channels[ i ].sampler;

				// get input and output from sampler
				input = anim.samplers[ sampler_t ].input;
				output = anim.samplers[ sampler_t ].output;

				// get animation translation
				data_t = getNodeData( GLTFDataType::AnimationPosition, output, frame );

				continue;
			}
			if ( anim.channels[ i ].target_path == "rotation" )
			{
				chan_r = i;
				sampler_r = anim.channels[ i ].sampler;

				// get input and output from sampler
				input = anim.samplers[ sampler_r ].input;
				output = anim.samplers[ sampler_r ].output;

				// get animation rotation
				data_r = getNodeData( GLTFDataType::AnimationRotation, output, frame );

				continue;
			}
			if ( anim.channels[ i ].target_path == "scale" )
			{
				chan_s = i;
				sampler_s = anim.channels[ i ].sampler;

				// get input and output from sampler
				input = anim.samplers[ sampler_s ].input;
				output = anim.samplers[ sampler_s ].output;

				// get animation scale
				data_s = getNodeData( GLTFDataType::AnimationScale, output, frame );
			}
		}

		if ( chan_t == -1 || sampler_t == -1 ||
		     chan_r == -1 || sampler_r == -1 ||
		 	 chan_s == -1 || sampler_s == -1 )
		{
			log( "Corrupted animation. Skipping." );
			return makeMatrix4f( assembleMatrix( node ).transposed().x, true ); // GLTF always uses Y-up
		}

		// make final matrix
		Vec3f t = any_cast< Vec3f >( data_t );
		Quaternion r = any_cast< Quaternion >( data_r );
		Vec3f s = any_cast< Vec3f >( data_s );

		return makeMatrix4f( assembleAnimMatrix( t, r, s ).transposed().x, true ); // GLTF always uses Y-up
	}
	else
	{
		// Animation is not found, copy current matrix/trs as animation first frame
		return makeMatrix4f( assembleMatrix( node ).transposed().x, true ); // GLTF always uses Y-up
	}

// 	for ( unsigned int i = 0; i < node.transStack.size(); ++i )
// 	{
// 		int compIndex;
// 		DaeSampler *sampler = _daeDoc.libAnimations.findAnimForTarget( node.id, node.transStack[ i ].sid, &compIndex );
// 
// 		if ( sampler != 0x0 )
// 		{
// 			if ( compIndex >= 0 )	// Handle animation of single components like X or ANGLE
// 			{
// 				if ( sampler->output->floatArray.size() != _frameCount )
// 					_animNotSampled = true;
// 				else
// 					node.transStack[ i ].animValues[ compIndex ] = sampler->output->floatArray[ frame ];
// 			}
// 			else
// 			{
// 				unsigned int size = 0;
// 				switch ( node.transStack[ i ].type )
// 				{
// 					case DaeTransformation::MATRIX:
// 						size = 16;
// 						break;
// 						// Note: Routine assumes that order is X, Y, Z, ANGLE
// 					case DaeTransformation::SCALE:
// 					case DaeTransformation::TRANSLATION:
// 						size = 3;
// 						break;
// 					case DaeTransformation::ROTATION:
// 						size = 4;
// 						break;
// 				}
// 				if ( sampler->output->floatArray.size() != _frameCount * size )
// 					_animNotSampled = true;
// 				else
// 					memcpy( node.transStack[ i ].animValues, &sampler->output->floatArray[ frame * size ], size * sizeof( float ) );
// 			}
// 		}
// 		else
// 		{
// 			// If no animation data is found, use standard transformation
// 			memcpy( node.transStack[ i ].animValues, node.transStack[ i ].values, 16 * sizeof( float ) );
// 		}
// 	}

}

size_t GLTFConverter::getAnimationTotalFrameCount()
{
	size_t animFrames = 0;
	for ( auto &a : _model.animations )
	{
		auto &accessor = _model.accessors[ a.samplers[ 0 ].input ];
		animFrames = std::max( accessor.count, (size_t)animFrames );
	}

	return animFrames;
}

size_t GLTFConverter::getAnimationFrameCount( int animIndex, int nodeID )
{
	auto &anim = _model.animations[ animIndex ];

	for ( size_t i = 0; i < anim.channels.size(); ++i )
	{
		if ( anim.channels[ i ].target_node == nodeID )
		{
			auto accessorId = anim.samplers[ anim.channels[ i ].sampler ].input;
			return _model.accessors[ accessorId ].count;
		}
	}

	return 0;
}

GLTFNodeType GLTFConverter::validateInstance( const tinygltf::Node &node, int nodeId )
{
	// check whether node is joint 
	for ( auto &skin : _model.skins )
	{
		for ( size_t i = 0; i < skin.joints.size(); ++i )
		{
			if ( skin.joints[ i ] == nodeId ) return GLTFNodeType::Joint;
		}
	}

	// check that mesh is available
	if ( node.mesh != -1 ) return GLTFNodeType::Mesh;

	return GLTFNodeType::Transformation;
}

SceneNode *GLTFConverter::processNode( tinygltf::Node &node, int nodeId, SceneNode *parentNode, Matrix4f transAccum, 
									   std::vector< Matrix4f > animTransAccum )
{
	// Note: animTransAccum is used for pure transformation nodes of Collada that are no joints or meshes

	// Process instances and create nodes using the following rules:
	//		* DaeJoint and no Instance -> create joint
	//		* DaeJoint and one Instance -> create joint and mesh
	//		* DaeJoint and several Instances -> create joint and mesh with submeshes
	//		* DaeNode and no Instance -> forward transformation to children (pure transformation node)
	//		* DaeNode and one Instance -> create mesh
	//		* DaeNode and several Instances -> create mesh with submeshes

	// Assemble matrix
 	Matrix4f relMat = transAccum * makeMatrix4f( assembleMatrix( node ).transposed().x, true ); // GLTF always uses Y-up.

	SceneNode *oNode = 0x0;

 	// Find valid instances
	auto instanceType = validateInstance( node, nodeId );

	SceneNodeParameters p;

	// Create node
	if ( instanceType == GLTFNodeType::Joint )
	{
		oNode = createSceneNode( AvailableSceneNodeTypes::Joint );
	}
	else
	{
		if ( instanceType == GLTFNodeType::Mesh )
		{
			oNode = createSceneNode( AvailableSceneNodeTypes::Mesh );
		}
	}

	// Set node params
	if ( oNode != 0x0 )
	{
		processSceneNode( oNode, parentNode, node.name, relMat );

		// Save gltf node id
		p.nodeID = nodeId;
		if ( _model.nodes[ nodeId ].skin != -1 ) p.skinID = _model.nodes[ nodeId ].skin;
		if ( _model.nodes[ nodeId ].mesh != -1 ) p.meshID = _model.nodes[ nodeId ].mesh;

		oNode->scncp = p;
	}

	// Create sub-nodes if necessary
// 	if ( oNode != 0x0 )
// 	{
// 		if ( node.joint && !validInsts.empty() )
// 		{
// 			// 			SceneNode *oNode2 = new Mesh();
// 			// 			_meshes.push_back( (Mesh *)oNode2 );
// 			SceneNode *oNode2 = createSceneNode( AvailableSceneNodeTypes::Mesh );
// 			oNode->children.push_back( _meshes.back() );
// 
// 			*oNode2 = *oNode;
// 			oNode2->typeJoint = false;
// 			oNode2->matRel = Matrix4f();
// 			SceneNodeParameters *prm = &any_cast< SceneNodeParameters >( oNode2->scncp );
// 			prm->daeInstance = &node.instances[ validInsts[ 0 ] ];
// 			// 			oNode2->daeInstance = &node.instances[validInsts[0]];
// 			oNode2->parent = oNode;
// 			oNode2->children.clear();
// 			for ( unsigned int i = 0; i < oNode2->frames.size(); ++i )
// 				oNode2->frames[ i ] = Matrix4f();
// 
// 			// Create submeshes if there are several instances
// 			for ( unsigned int i = 1; i < validInsts.size(); ++i )
// 			{
// 				// 				SceneNode *oNode3 = new Mesh();
// 				// 				_meshes.push_back( (Mesh *)oNode3 );
// 				SceneNode *oNode3 = createSceneNode( AvailableSceneNodeTypes::Mesh );
// 				oNode2->children.push_back( _meshes.back() );
// 
// 				*oNode3 = *oNode2;
// 				SceneNodeParameters *p = &any_cast< SceneNodeParameters >( oNode3->scncp );
// 				p->daeInstance = &node.instances[ validInsts[ i ] ];
// 				// 				oNode3->daeInstance = &node.instances[validInsts[i]];
// 				oNode3->parent = oNode2;
// 			}
// 		}
// 		else if ( !node.joint && validInsts.size() > 1 )
// 		{
// 			// Create submeshes
// 			for ( unsigned int i = 1; i < validInsts.size(); ++i )
// 			{
// 				// 				SceneNode *oNode2 = new Mesh();
// 				// 				_meshes.push_back( (Mesh *)oNode2 );
// 				SceneNode *oNode2 = createSceneNode( AvailableSceneNodeTypes::Mesh );
// 				oNode->children.push_back( _meshes.back() );
// 
// 				*oNode2 = *oNode;
// 				oNode2->matRel = Matrix4f();
// 				// 				oNode2->daeInstance = &node.instances[validInsts[i]];
// 				SceneNodeParameters *p = &any_cast< SceneNodeParameters >( oNode2->scncp );
// 				p->daeInstance = &node.instances[ validInsts[ i ] ];
// 				oNode2->parent = oNode;
// 				oNode2->children.clear();
// 				for ( unsigned int j = 0; j < oNode2->frames.size(); ++j )
// 					oNode2->frames[ j ] = Matrix4f();
// 			}
// 		}
//  	}

	if ( oNode != 0x0 ) transAccum = Matrix4f();
	else transAccum = relMat;

	// Animation
	if ( _splitAnimations )
	{
		for( size_t i = 0; i < _model.animations.size(); ++i )
		{
			Animation a;
			auto &gltfAnimation = _model.animations[ i ];
			strncpy( a.name, gltfAnimation.name.c_str(), gltfAnimation.name.size() );

			size_t animFrames = getAnimationFrameCount( i, nodeId );
			if ( oNode ) a.frames.reserve( animFrames );

			for ( size_t j = 0; j < animFrames; ++j )
			{
				// TODO: correct animTransAccum
				Matrix4f mat = /*animTransAccum[ i ] * */getNodeTransform( node, nodeId, j );
				if ( oNode != 0x0 )
				{
//					oNode->frames.push_back( mat );
					a.frames.emplace_back( mat );
					animTransAccum[ i ] = Matrix4f();
				}
				else animTransAccum[ i ] = mat;	// Pure transformation node
			}

			if ( oNode ) oNode->animations.emplace_back( a );
		}
	}
	else
	{
		for ( unsigned int i = 0; i < _frameCount; ++i )
		{
			Matrix4f mat = animTransAccum[ i ] * getNodeTransform( node, nodeId, i );
			if ( oNode != 0x0 )
			{
				oNode->frames.push_back( mat );
				animTransAccum[ i ] = Matrix4f();
			}
			else animTransAccum[ i ] = mat;	// Pure transformation node
		}
	}

	// Process children
	for ( unsigned int i = 0; i < node.children.size(); ++i )
	{
		SceneNode *parNode = oNode != 0x0 ? oNode : parentNode;
		int childNodeId = node.children[ i ];

		SceneNode *childNode = processNode( _model.nodes[ node.children[ i ] ], childNodeId, parNode,
											transAccum, animTransAccum );
		if ( childNode != 0x0 && parNode != 0x0 ) parNode->children.push_back( childNode );
	}

	return oNode;
}


void GLTFConverter::processTriGroup( tinygltf::Mesh *geo, unsigned int geoTriGroupIndex, SceneNodeParameters *meshParams, int skinID, std::vector<Joint *> &jointLookup, unsigned int i )
{
	tinygltf::Primitive &prim = geo->primitives[ geoTriGroupIndex ];
	TriGroup* oTriGroup = new TriGroup();

	tinygltf::Material *mat = &_model.materials[ prim.material ];
	if ( mat != 0x0 )
	{
		oTriGroup->matName = mat->name;
	}
	else
		log( "Warning: Material '" + oTriGroup->matName + "' not found" );

	oTriGroup->first = ( unsigned int ) _indices.size();
	oTriGroup->count = ( unsigned int ) _model.accessors[ prim.indices ].count;
	oTriGroup->vertRStart = ( unsigned int ) _vertices.size();

	if ( oTriGroup->count == -1 )
	{
		log( "Indices are not provided for the mesh. Currently this is not supported. Skipping mesh " + geo->name );
		return;
	}

	auto findAccessor = []( const tinygltf::Primitive &prim, const char *prop )
	{
		auto it = prim.attributes.find( prop );
		if ( it != prim.attributes.end() )
			return it->second;

		return -1;
	};

	// Accessors
	auto posAccessorID = findAccessor( prim, "POSITION" ); 
	auto normAccessorID = findAccessor( prim, "NORMAL" );
	auto tex0AccessorID = findAccessor( prim, "TEXCOORD_0" );
	auto tex1AccessorID = findAccessor( prim, "TEXCOORD_1" );
	auto joint0AccessorID = findAccessor( prim, "JOINTS_0" );
	auto weight0AccessorID = findAccessor( prim, "WEIGHTS_0" );

	if ( posAccessorID == -1 )
	{
		log( "Incorrect mesh - POSITION attribute not provided. Skipping mesh." );
		return;
	}

	// Add indices and vertices
	oTriGroup->numPosIndices = _model.accessors[ posAccessorID ].count; // / 3; // gltf always uses vec3 for positions
// 	oTriGroup->numPosIndices = ( unsigned int ) iTriGroup.vSource->posSource->floatArray.size() /
// 		iTriGroup.vSource->posSource->paramsPerItem;
	oTriGroup->posIndexToVertices = new std::vector< unsigned int >[ oTriGroup->numPosIndices ];

	for ( unsigned int k = 0; k < oTriGroup->count; ++k )
	{
		// Try to find vertex
		int vertIdx = any_cast< int >( getNodeData( GLTFDataType::VertexID, prim.indices, k ) );
		std::vector< unsigned int > &vertList = oTriGroup->posIndexToVertices[ vertIdx ]; /*iTriGroup.indices[ k ].posIndex*/ 
		bool found = false;
		unsigned int index = ( unsigned int ) _vertices.size();

		// Only check vertices that have the same GLTF position index
		for ( unsigned int l = 0; l < vertList.size(); ++l )
		{
			Vertex &v = _vertices[ vertList[ l ] ];

			auto gltfPos = any_cast< Vec3f >( getNodeData( GLTFDataType::VertexPosition, posAccessorID, vertIdx ) );
			auto gltfNormal = normAccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::Normal, normAccessorID, vertIdx ) ) : Vec3f();
			auto gltfTex0 = tex0AccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::TextureCoordinates, tex0AccessorID, vertIdx ) ) : Vec3f();
			auto gltfTex1 = tex1AccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::TextureCoordinates, tex1AccessorID, vertIdx ) ) : Vec3f();

			if ( v.storedPos == gltfPos			&&
				 v.storedNormal == gltfNormal	&&
				 v.texCoords[ 0 ] == gltfTex0	&&
 				 v.texCoords[ 1 ] == gltfTex1	) // &&
// 				v.texCoords[ 2 ] == iTriGroup.getTexCoords( iTriGroup.indices[ k ].texIndex[ 2 ], 2 ) &&
// 				v.texCoords[ 3 ] == iTriGroup.getTexCoords( iTriGroup.indices[ k ].texIndex[ 3 ], 3 ) )
			{
				found = true;
				index = vertList[ l ];
				break;
			}
		}

		if ( found )
		{
			_indices.push_back( index );
		}
		else
		{
			Vertex v;
			v.vp = VertexParameters();
			auto vp = &any_cast< VertexParameters >( v.vp );

			vp->gltfPosIndex = vertIdx;

			auto gltfPos = any_cast< Vec3f >( getNodeData( GLTFDataType::VertexPosition, posAccessorID, vertIdx ) );
			auto gltfNormal = normAccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::Normal, normAccessorID, vertIdx ) ) : Vec3f();
			auto gltfTex0 = tex0AccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::TextureCoordinates, tex0AccessorID, vertIdx ) ) : Vec3f();
			auto gltfTex1 = tex1AccessorID != -1 ? any_cast< Vec3f >( getNodeData( GLTFDataType::TextureCoordinates, tex1AccessorID, vertIdx ) ) : Vec3f();

			// Position
			v.storedPos = gltfPos;
			v.pos = v.storedPos;

			// Texture coordinates
			v.texCoords[ 0 ] = gltfTex0;
			v.texCoords[ 1 ] = gltfTex1;
// 			v.texCoords[ 2 ] = iTriGroup.getTexCoords( iTriGroup.indices[ k ].texIndex[ 2 ], 2 );
// 			v.texCoords[ 3 ] = iTriGroup.getTexCoords( iTriGroup.indices[ k ].texIndex[ 3 ], 3 );

			// Normal
			v.storedNormal = gltfNormal;

			// Skinning
// 			if ( skin != 0x0 && /*v.daePosIndex*/ vp->daePosIndex < ( int ) skin->vertWeights.size() )
// 			{
// 				DaeVertWeights vertWeights = skin->vertWeights[/*v.daePosIndex*/ vp->daePosIndex ];
// 
// 				// Sort weights
// 				for ( unsigned int xx = 0; xx < vertWeights.size(); ++xx )
// 				{
// 					for ( unsigned int yy = 0; yy < xx; ++yy )
// 					{
// 						if ( skin->weightArray->floatArray[ ( int ) vertWeights[ xx ].weight ] >
// 							skin->weightArray->floatArray[ ( int ) vertWeights[ yy ].weight ] )
// 						{
// 							swap( vertWeights[ xx ], vertWeights[ yy ] );
// 						}
// 					}
// 				}
// 
// 				// Take the four most significant weights
// 				for ( unsigned int l = 0; l < vertWeights.size(); ++l )
// 				{
// 					if ( l == 4 ) break;
// 					v.weights[ l ] = skin->weightArray->floatArray[ vertWeights[ l ].weight ];
// 					v.joints[ l ] = jointLookup[ vertWeights[ l ].joint ];
// 				}
// 
// 				// Normalize weights
// 				float weightSum = v.weights[ 0 ] + v.weights[ 1 ] + v.weights[ 2 ] + v.weights[ 3 ];
// 				if ( weightSum > Math::Epsilon )
// 				{
// 					v.weights[ 0 ] /= weightSum;
// 					v.weights[ 1 ] /= weightSum;
// 					v.weights[ 2 ] /= weightSum;
// 					v.weights[ 3 ] /= weightSum;
// 				}
// 				else
// 				{
// 					v.weights[ 0 ] = 1.0f;
// 					v.weights[ 1 ] = v.weights[ 2 ] = v.weights[ 3 ] = 0.0f;
// 				}
// 
// 				// Apply skinning to vertex
// 				if ( v.joints[ 0 ] != 0x0 || v.joints[ 1 ] != 0x0 || v.joints[ 2 ] != 0x0 || v.joints[ 3 ] != 0x0 )
// 				{
// 					Vec3f newPos( 0, 0, 0 );
// 					if ( v.joints[ 0 ] != 0x0 )
// 					{
// 						JointParameters *vJointParams = &any_cast< JointParameters >( v.joints[ 0 ]->jp );
// 						//								newPos += v.joints[ 0 ]->matAbs * v.joints[ 0 ]->daeInvBindMat * v.pos * v.weights[ 0 ];
// 						newPos += v.joints[ 0 ]->matAbs * vJointParams->daeInvBindMat * v.pos * v.weights[ 0 ];
// 
// 					}
// 					if ( v.joints[ 1 ] != 0x0 )
// 					{
// 						JointParameters *vJointParams = &any_cast< JointParameters >( v.joints[ 1 ]->jp );
// 						// 								newPos += v.joints[ 1 ]->matAbs * v.joints[ 1 ]->daeInvBindMat * v.pos * v.weights[ 1 ];
// 						newPos += v.joints[ 1 ]->matAbs * vJointParams->daeInvBindMat * v.pos * v.weights[ 1 ];
// 					}
// 					if ( v.joints[ 2 ] != 0x0 )
// 					{
// 						JointParameters *vJointParams = &any_cast< JointParameters >( v.joints[ 2 ]->jp );
// 						//								newPos += v.joints[ 2 ]->matAbs * v.joints[ 2 ]->daeInvBindMat * v.pos * v.weights[ 2 ];
// 						newPos += v.joints[ 2 ]->matAbs * vJointParams->daeInvBindMat * v.pos * v.weights[ 2 ];
// 					}
// 					if ( v.joints[ 3 ] != 0x0 )
// 					{
// 						JointParameters *vJointParams = &any_cast< JointParameters >( v.joints[ 3 ]->jp );
// 						//								newPos += v.joints[ 3 ]->matAbs * v.joints[ 3 ]->daeInvBindMat * v.pos * v.weights[ 3 ];
// 						newPos += v.joints[ 3 ]->matAbs * vJointParams->daeInvBindMat * v.pos * v.weights[ 3 ];
// 					}
// 					v.pos = newPos;
// 				}
// 			}

			_vertices.emplace_back( v );
			_indices.push_back( index );

			vertList.push_back( ( unsigned int ) _vertices.size() - 1 );
		}
	}

	oTriGroup->vertREnd = ( unsigned int ) _vertices.size() - 1;

	// Remove degenerated triangles
	unsigned int numDegTris = MeshOptimizer::removeDegeneratedTriangles( oTriGroup, _vertices, _indices );
	if ( numDegTris > 0 )
	{
		std::stringstream ss;
		ss << numDegTris;
		log( "Removed " + ss.str() + " degenerated triangles from mesh " + std::to_string( meshParams->nodeID )/*_meshes[i]->daeNode->id*/ );
	}

	_meshes[ i ]->triGroups.push_back( oTriGroup );
}


void GLTFConverter::processMeshes( bool optimize )
{
	// Note: At the moment the geometry for all nodes is copied and not referenced
	for ( unsigned int i = 0; i < _meshes.size(); ++i )
	{
		// Interpret mesh LOD level
		if ( strstr( _meshes[ i ]->name, "_lod1" ) == _meshes[ i ]->name + strlen( _meshes[ i ]->name ) - 5 )
			_meshes[ i ]->lodLevel = 1;
		else if ( strstr( _meshes[ i ]->name, "_lod2" ) == _meshes[ i ]->name + strlen( _meshes[ i ]->name ) - 5 )
			_meshes[ i ]->lodLevel = 2;
		else if ( strstr( _meshes[ i ]->name, "_lod3" ) == _meshes[ i ]->name + strlen( _meshes[ i ]->name ) - 5 )
			_meshes[ i ]->lodLevel = 3;
		else if ( strstr( _meshes[ i ]->name, "_lod4" ) == _meshes[ i ]->name + strlen( _meshes[ i ]->name ) - 5 )
			_meshes[ i ]->lodLevel = 4;

		if ( _meshes[ i ]->lodLevel > 0 )
		{
			if ( _meshes[ i ]->lodLevel > _maxLodLevel ) _maxLodLevel = _meshes[ i ]->lodLevel;
			_meshes[ i ]->name[ strlen( _meshes[ i ]->name ) - 5 ] = '\0';  // Cut off lod postfix from name
		}

		// Find geometry/controller for node
		SceneNodeParameters *meshParams = &any_cast< SceneNodeParameters >( _meshes[ i ]->scncp );
		int id = meshParams->nodeID; 
		int skin = meshParams->skinID;
		int gltfMesh = meshParams->meshID;

		// Check that morph targets are present

// 		DaeSkin *skin = _daeDoc.libControllers.findSkin( id );
// 		DaeMorph *morpher = _daeDoc.libControllers.findMorph( id );

		// Check that skin has all required arrays
// 		if ( skin != 0x0 )
// 		{
// 			if ( skin->jointArray == 0x0 || skin->bindMatArray == 0x0 || skin->weightArray == 0x0 )
// 			{
// 				log( "Skin controller '" + skin->id + "' is missing information and is ignored" );
// 				skin = 0x0;
// 			}
// 		}

		auto *geo = &_model.meshes[ gltfMesh ];
		ASSERT( geo != 0x0 );

 		std::vector< Joint * > jointLookup;
// 		if ( skin != 0x0 )
// 		{
// 			buildSkinLookupTable( skin, jointLookup );
// 		}

		unsigned int firstGeoVert = ( unsigned int ) _vertices.size();

		for ( unsigned int j = 0; j < geo->primitives.size(); ++j )
		{
			processTriGroup( geo, j, meshParams, skin, jointLookup, i );
		}

		unsigned int numGeoVerts = ( unsigned int ) _vertices.size() - firstGeoVert;

		// Morph targets
// 		if ( morpher != 0x0 && morpher->targetArray != 0x0 )
// 		{
// 			processMorphTargets( morpher, geo, numGeoVerts, firstGeoVert );
// 		}
	}

	// Calculate tangent space basis for base mesh
// 	calcTangentSpaceBasis( _vertices );
// 
// 	// Calculate tangent space basis for morph targets
// 	vector< Vertex > verts( _vertices );
// 	for ( unsigned int i = 0; i < _morphTargets.size(); ++i )
// 	{
// 		// Morph
// 		for ( unsigned int j = 0; j < _morphTargets[ i ].diffs.size(); ++j )
// 		{
// 			verts[ _morphTargets[ i ].diffs[ j ].vertIndex ].pos += _morphTargets[ i ].diffs[ j ].posDiff;
// 		}
// 
// 		calcTangentSpaceBasis( verts );
// 
// 		// Find basis differences and undo morphing
// 		for ( unsigned int j = 0; j < _morphTargets[ i ].diffs.size(); ++j )
// 		{
// 			MorphDiff &md = _morphTargets[ i ].diffs[ j ];
// 
// 			verts[ md.vertIndex ].pos -= md.posDiff;
// 			md.normDiff = verts[ md.vertIndex ].normal - _vertices[ md.vertIndex ].normal;
// 			md.tanDiff = verts[ md.vertIndex ].tangent - _vertices[ md.vertIndex ].tangent;
// 			md.bitanDiff = verts[ md.vertIndex ].bitangent - _vertices[ md.vertIndex ].bitangent;
// 		}
// 	}

	// Optimization and clean up
	float optEffBefore = 0, optEffAfter = 0;
	unsigned int optNumCalls = 0;
	for ( unsigned int i = 0; i < _meshes.size(); ++i )
	{
		for ( unsigned int j = 0; j < _meshes[ i ]->triGroups.size(); ++j )
		{
			// Optimize order of indices for best vertex cache usage and remap vertices
			if ( optimize )
			{
				std::map< unsigned int, unsigned int > vertMap;

				++optNumCalls;
				optEffBefore += MeshOptimizer::calcCacheEfficiency( _meshes[ i ]->triGroups[ j ], _indices );
				MeshOptimizer::optimizeIndexOrder( _meshes[ i ]->triGroups[ j ], _vertices, _indices, vertMap );
				optEffAfter += MeshOptimizer::calcCacheEfficiency( _meshes[ i ]->triGroups[ j ], _indices );

				// Update morph target vertex indices according to vertex remapping
				for ( unsigned int k = 0; k < _morphTargets.size(); ++k )
				{
					for ( unsigned int l = 0; l < _morphTargets[ k ].diffs.size(); ++l )
					{
						std::map< unsigned int, unsigned int >::iterator itr1 =
							vertMap.find( _morphTargets[ k ].diffs[ l ].vertIndex );

						if ( itr1 != vertMap.end() )
						{
							_morphTargets[ k ].diffs[ l ].vertIndex = itr1->second;
						}
					}
				}
			}

			// Clean up
			delete[] _meshes[ i ]->triGroups[ j ]->posIndexToVertices;
			_meshes[ i ]->triGroups[ j ]->posIndexToVertices = 0x0;
		}
	}

}
void GLTFConverter::processMaterials()
{

}

bool GLTFConverter::writeModel( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const
{
	return writeModelCommon( assetPath, assetName, modelName );
}

bool GLTFConverter::writeMaterials( const std::string &assetPath, const std::string &modelName, bool replace ) const
{
	for ( const tinygltf::Material &m : _model.materials )
	{
		
	}
}

bool GLTFConverter::hasAnimation() const
{
	return _frameCount > 0;
}

bool GLTFConverter::writeAnimation( const std::string &assetPath, const std::string &assetName ) const
{
	return writeAnimationCommon( assetPath, assetName );
}

} // namespace AssetConverter
} // namespace Horde3D
