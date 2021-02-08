#include "gltfConverter.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "gltf/tiny_gltf.h"

#include "utils.h"

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

Matrix4f assembleAnimMatrix() 
{
	Matrix4f mat;

// 	for ( unsigned int i = 0; i < transStack.size(); ++i )
// 	{
// 		switch ( transStack[ i ].type )
// 		{
// 			case DaeTransformation::MATRIX:
// 				mat = mat * Matrix4f( transStack[ i ].animValues ).transposed();
// 				break;
// 			case DaeTransformation::TRANSLATION:
// 				mat = mat * Matrix4f::TransMat( transStack[ i ].animValues[ 0 ], transStack[ i ].animValues[ 1 ],
// 					transStack[ i ].animValues[ 2 ] );
// 				break;
// 			case DaeTransformation::ROTATION:
// 				mat = mat * Matrix4f::RotMat( Vec3f( transStack[ i ].animValues[ 0 ], transStack[ i ].animValues[ 1 ],
// 					transStack[ i ].animValues[ 2 ] ), degToRad( transStack[ i ].animValues[ 3 ] ) );
// 				break;
// 			case DaeTransformation::SCALE:
// 				mat = mat * Matrix4f::ScaleMat( transStack[ i ].animValues[ 0 ], transStack[ i ].animValues[ 1 ],
// 					transStack[ i ].animValues[ 2 ] );
// 				break;
// 		}
// 	}

	return mat;
}

GLTFConverter::GLTFConverter( const tinygltf::Model &model, const std::string &outPath, const float *lodDists ) : IConverter(), _model( model )
{
	_outPath = outPath;

	_lodDist1 = lodDists[ 0 ];
	_lodDist2 = lodDists[ 1 ];
	_lodDist3 = lodDists[ 2 ];
	_lodDist4 = lodDists[ 3 ];
}

GLTFConverter::~GLTFConverter()
{

}

bool GLTFConverter::convertModel( bool optimize )
{
	if ( _model.scenes.empty() ) return true; // Nothing to convert

	// Get max animation frames from all animations
	_frameCount = getAnimationFrameCount();

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

static_any< 32 > GLTFConverter::getNodeData( GLTFDataType type, int accessorId )
{
	switch ( type )
	{
		case GLTFDataType::VertexPosition:
		{
			break;
		}
		case GLTFDataType::VertexRotation:
			break;
		case GLTFDataType::VertexScale:
			break;
		case GLTFDataType::AnimationPosition:
		{
			// get accessor and buffer view
			auto &accessor = _model.accessors[ accessorId ];
			auto &bufView = _model.bufferViews[ accessor.bufferView ];
			auto &buf = _model.buffers[ bufView.buffer ];
			const float *pos = reinterpret_cast< const float * >( &buf.data[ bufView.byteOffset + accessor.byteOffset ] );

			Vec3f v ( pos[ 0 ], pos[ 1 ], pos[ 2 ] );
			return std::move( v );
			break;
		}
		case GLTFDataType::AnimationRotation:
			break;
		case GLTFDataType::AnimationScale:
			break;
		default:
			break;
	}
}

Matrix4f GLTFConverter::getNodeTransform( tinygltf::Node &node, int nodeId, unsigned int frame )
{
	// Note: Function assumes sampled animation data

	// Find animation that works with current node
	auto findAnimIndex = []( tinygltf::Model &model, int nodeId )
	{
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
	};

	int animIndex = findAnimIndex( _model, nodeId );
	if ( animIndex != -1 )
	{
		// get translation/rotation/scale channels and samplers
		int chan_t, chan_r, chan_s;
		chan_t = chan_r = chan_s = -1;
		
		int sampler_t, sampler_r, sampler_s;
		sampler_t = sampler_r = sampler_s = -1;
		
		auto &anim = _model.animations[ animIndex ];
		for ( size_t i = 0; i < anim.channels.size(); ++i )
		{
			if ( anim.channels[ i ].target_path == "translation" )
			{
				chan_t = i;
				sampler_t = anim.channels[ i ].sampler;
				continue;
			}
			if ( anim.channels[ i ].target_path == "rotation" )
			{
				chan_r = i;
				sampler_r = anim.channels[ i ].sampler;
				continue;
			}
			if ( anim.channels[ i ].target_path == "scale" )
			{
				chan_s = i;
				sampler_s = anim.channels[ i ].sampler;
			}
		}

		if ( chan_t == -1 || sampler_t == -1 ||
		     chan_r == -1 || sampler_r == -1 ||
		 	 chan_s == -1 || sampler_s == -1 )
		{
			log( "Corrupted animation. Skipping." );
			return makeMatrix4f( assembleMatrix( node ).transposed().x, true ); // GLTF always uses Y-up
		}

		// get input and output from sampler
// 		int input, output;
// 		input = anim.samplers[ sampler ].input;
// 		output = anim.samplers[ sampler ].output;
// 		// Animation is found, get animation matrix for current frame
// 		auto data_t = getNodeData( GLTFDataType::AnimationPosition, animIndex );
// 		auto data_r = getNodeData( GLTFDataType::AnimationRotation, animIndex );
// 		auto data_s = getNodeData( GLTFDataType::AnimationScale, animIndex );
// 
// 		Vec3f t = any_cast< Vec3f >( data_t );
// 		Quaternion r = any_cast< Quaternion >( data_r );
// 		Vec3f s = any_cast< Vec3f >( data_s );

		return makeMatrix4f( assembleAnimMatrix().transposed().x, true ); // GLTF always uses Y-up
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

size_t GLTFConverter::getAnimationFrameCount()
{
	size_t animFrames = 0;
	for ( auto &a : _model.animations )
	{
		// we can judge the total number of animation frames by the animation samplers
		animFrames = std::max( a.samplers.size(), (size_t)animFrames );
	}

	return animFrames;
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

void GLTFConverter::processMeshes( bool optimize )
{
	throw std::logic_error( "The method or operation is not implemented." );
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
	throw std::logic_error( "The method or operation is not implemented." );
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
