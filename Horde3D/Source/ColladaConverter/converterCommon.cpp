#include "converterCommon.h"
#include "utils.h"

#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>

namespace Horde3D {
namespace AssetConverter {

using namespace std;

void IConverter::processSceneNode( SceneNode *node, SceneNode *parentNode, std::string &name, Matrix4f &m )
{
	node->parent = parentNode;
	node->matRel = m;

	// Name
	if ( name.length() > 255 )
	{
		log( "Warning: node name is too long" );
		name.erase( 255, name.length() - 255 );
	}
	strcpy( node->name, name.c_str() );

	// Check for duplicate node name
	checkNodeName( node );

	// Calculate absolute transformation
	if ( parentNode != 0x0 ) node->matAbs = parentNode->matAbs * node->matRel;
	else node->matAbs = node->matRel;
}

// void IConverter::calcTangentSpaceBasis( vector<Vertex> &verts ) const
// {
// 	for ( unsigned int i = 0; i < verts.size(); ++i )
// 	{
// 		verts[ i ].normal = Vec3f( 0, 0, 0 );
// 		verts[ i ].tangent = Vec3f( 0, 0, 0 );
// 		verts[ i ].bitangent = Vec3f( 0, 0, 0 );
// 	}
// 
// 	// Basic algorithm: Eric Lengyel, Mathematics for 3D Game Programming & Computer Graphics
// 	for ( unsigned int i = 0; i < _meshes.size(); ++i )
// 	{
// 		for ( unsigned int j = 0; j < _meshes[ i ]->triGroups.size(); ++j )
// 		{
// 			TriGroup *triGroup = _meshes[ i ]->triGroups[ j ];
// 
// 			for ( unsigned int k = triGroup->first; k < triGroup->first + triGroup->count; k += 3 )
// 			{
// 				// Compute basis vectors for triangle
// 				Vec3f edge1uv = verts[ _indices[ k + 1 ] ].texCoords[ 0 ] - verts[ _indices[ k ] ].texCoords[ 0 ];
// 				Vec3f edge2uv = verts[ _indices[ k + 2 ] ].texCoords[ 0 ] - verts[ _indices[ k ] ].texCoords[ 0 ];
// 				Vec3f edge1 = verts[ _indices[ k + 1 ] ].pos - verts[ _indices[ k ] ].pos;
// 				Vec3f edge2 = verts[ _indices[ k + 2 ] ].pos - verts[ _indices[ k ] ].pos;
// 				Vec3f normal = edge1.cross( edge2 );  // Normal weighted by triangle size (hence unnormalized)
// 
// 				float r = 1.0f / ( edge1uv.x * edge2uv.y - edge2uv.x * edge1uv.y ); // UV area normalization
// 				Vec3f uDir = ( edge1 * edge2uv.y - edge2 * edge1uv.y ) * r;
// 				Vec3f vDir = ( edge2 * edge1uv.x - edge1 * edge2uv.x ) * r;
// 
// 				// Accumulate basis for vertices
// 				for ( unsigned int l = 0; l < 3; ++l )
// 				{
// 					verts[ _indices[ k + l ] ].normal += normal;
// 					verts[ _indices[ k + l ] ].tangent += uDir;
// 					verts[ _indices[ k + l ] ].bitangent += vDir;
// 
// 					// Handle texture seams where vertices were split
// 					VertexParameters *vParams = &any_cast< VertexParameters >( verts[ _indices[ k + l ] ].vp );
// 					vector< unsigned int > &vertList =
// 												triGroup->posIndexToVertices[ vParams->daePosIndex ];
// //						triGroup->posIndexToVertices[ verts[ _indices[ k + l ] ].daePosIndex ];
// 					for ( unsigned int m = 0; m < vertList.size(); ++m )
// 					{
// 						if ( vertList[ m ] != _indices[ k + l ] &&
// 							verts[ vertList[ m ] ].storedNormal == verts[ _indices[ k + l ] ].storedNormal )
// 						{
// 							verts[ vertList[ m ] ].normal += normal;
// 							verts[ vertList[ m ] ].tangent += uDir;
// 							verts[ vertList[ m ] ].bitangent += vDir;
// 						}
// 					}
// 				}
// 			}
// 		}
// 	}
// 
// 	// Normalize tangent space basis
// 	unsigned int numInvalidBasis = 0;
// 	for ( unsigned int i = 0; i < verts.size(); ++i )
// 	{
// 		// Check if tangent space basis is invalid
// 		if ( verts[ i ].normal.length() == 0 || verts[ i ].tangent.length() == 0 || verts[ i ].bitangent.length() == 0 )
// 			++numInvalidBasis;
// 
// 		// Gram-Schmidt orthogonalization
// 		verts[ i ].normal.normalize();
// 		Vec3f &n = verts[ i ].normal;
// 		Vec3f &t = verts[ i ].tangent;
// 		verts[ i ].tangent = ( t - n * n.dot( t ) ).normalized();
// 
// 		// Calculate handedness (required to support mirroring) and final bitangent
// 		float handedness = n.cross( t ).dot( verts[ i ].bitangent ) < 0 ? -1.0f : 1.0f;
// 		verts[ i ].bitangent = n.cross( t ) * handedness;
// 	}
// 
// 	if ( numInvalidBasis > 0 )
// 	{
// 		log( "Warning: Geometry has zero-length basis vectors" );
// 		log( "   Maybe two faces point in opposite directions and share same vertices" );
// 	}
// }


SceneNode *IConverter::findNode( const char *name, SceneNode *ignoredNode )
{
	for ( size_t i = 0, s = _joints.size(); i < s; ++i )
	{
		if ( _joints[ i ] != ignoredNode && strcmp( _joints[ i ]->name, name ) == 0 )
			return _joints[ i ];
	}

	for ( size_t i = 0, s = _meshes.size(); i < s; ++i )
	{
		if ( _meshes[ i ] != ignoredNode && strcmp( _meshes[ i ]->name, name ) == 0 )
			return _meshes[ i ];
	}

	return 0x0;
}


void IConverter::checkNodeName( SceneNode *node )
{
	// Check if a different node with the same name exists
	if ( findNode( node->name, node ) != 0x0 )
	{
		// If necessary, cut name to make room for the postfix
		if ( strlen( node->name ) > 240 ) node->name[ 240 ] = '\0';

		char newName[ 512 ];
		unsigned int index = 2;

		// Find a free name
		while ( true )
		{
			snprintf( newName, sizeof( newName ), "%s_%i", node->name, index++ );

			if ( !findNode( newName, node ) )
			{
				char msg[ 1024 ];
				sprintf( msg, "Warning: Node with name '%s' already exists. "
					"Node was renamed to '%s'.", node->name, newName );
				log( msg );

				strcpy( node->name, newName );
				break;
			}
		}
	}
}

void IConverter::processJoints()
{
	for ( unsigned int i = 0; i < _joints.size(); ++i )
	{
		_joints[ i ]->index = i + 1;	// First index is identity matrix
		_joints[ i ]->invBindMat = _joints[ i ]->matAbs.inverted();
	}

	if ( _joints.size() + 1 > 75 ) log( "Warning: Model has more than 75 joints. It may render incorrectly if used with OpenGL 2 render backend." );
	if ( _joints.size() + 1 > 330 ) log( "Warning: Model has more than 330 joints. Currently it is not supported." );
}

bool IConverter::writeGeometry( const string &assetPath, const string &assetName ) const
{
	string fileName = _outPath + assetPath + assetName + ".geo";
	FILE *f = fopen( fileName.c_str(), "wb" );
	if ( f == 0x0 )
	{
		log( "Failed to write " + fileName + " file" );
		return false;
	}

	// Write header
	unsigned int version = 5;
	fwrite_le( "H3DG", 4, f );
	fwrite_le( &version, 1, f );

	// Write joints
	unsigned int count = ( unsigned int ) _joints.size() + 1;
	fwrite_le( &count, 1, f );

	// Write default identity matrix
	Matrix4f identity;
	for ( unsigned int j = 0; j < 16; ++j )
		fwrite_le<float>( &identity.x[ j ], 1, f );

	for ( unsigned int i = 0; i < _joints.size(); ++i )
	{
		// Inverse bind matrix
		for ( unsigned int j = 0; j < 16; ++j )
		{
			fwrite_le<float>( &_joints[ i ]->invBindMat.x[ j ], 1, f );
		}
	}

	// Write vertex stream data
	if ( _joints.empty() ) count = 6; else count = 8;	// Number of streams
	fwrite_le( &count, 1, f );
	count = ( unsigned int ) _vertices.size();
	fwrite_le( &count, 1, f );

	for ( unsigned int i = 0; i < 8; ++i )
	{
		if ( _joints.empty() && ( i == 4 || i == 5 ) ) continue;

		unsigned char uc;
		short sh;
		unsigned int streamElemSize;

		switch ( i )
		{
			case 0:		// Position
				fwrite_le( &i, 1, f );
				streamElemSize = 3 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					fwrite_le<float>( &_vertices[ j ].pos.x, 1, f );
					fwrite_le<float>( &_vertices[ j ].pos.y, 1, f );
					fwrite_le<float>( &_vertices[ j ].pos.z, 1, f );
				}
				break;
			case 1:		// Normal
				fwrite_le( &i, 1, f );
				streamElemSize = 3 * sizeof( short ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					sh = ( short ) ( _vertices[ j ].normal.x * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].normal.y * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].normal.z * 32767 ); fwrite_le<short>( &sh, 1, f );
				}
				break;
			case 2:		// Tangent
				fwrite_le( &i, 1, f );
				streamElemSize = 3 * sizeof( short ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					sh = ( short ) ( _vertices[ j ].tangent.x * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].tangent.y * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].tangent.z * 32767 ); fwrite_le<short>( &sh, 1, f );
				}
				break;
			case 3:		// Bitangent
				fwrite_le( &i, 1, f );
				streamElemSize = 3 * sizeof( short ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					sh = ( short ) ( _vertices[ j ].bitangent.x * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].bitangent.y * 32767 ); fwrite_le<short>( &sh, 1, f );
					sh = ( short ) ( _vertices[ j ].bitangent.z * 32767 ); fwrite_le<short>( &sh, 1, f );
				}
				break;
			case 4:		// Joint indices
				fwrite_le( &i, 1, f );
				streamElemSize = 4 * sizeof( char ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					unsigned char jointIndices[ 4 ] = { 0, 0, 0, 0 };
					if ( _vertices[ j ].joints[ 0 ] != 0x0 )
						jointIndices[ 0 ] = ( unsigned char ) _vertices[ j ].joints[ 0 ]->index;
					if ( _vertices[ j ].joints[ 1 ] != 0x0 )
						jointIndices[ 1 ] = ( unsigned char ) _vertices[ j ].joints[ 1 ]->index;
					if ( _vertices[ j ].joints[ 2 ] != 0x0 )
						jointIndices[ 2 ] = ( unsigned char ) _vertices[ j ].joints[ 2 ]->index;
					if ( _vertices[ j ].joints[ 3 ] != 0x0 )
						jointIndices[ 3 ] = ( unsigned char ) _vertices[ j ].joints[ 3 ]->index;
					fwrite_le( &jointIndices[ 0 ], 1, f );
					fwrite_le( &jointIndices[ 1 ], 1, f );
					fwrite_le( &jointIndices[ 2 ], 1, f );
					fwrite_le( &jointIndices[ 3 ], 1, f );
				}
				break;
			case 5:		// Weights
				fwrite_le( &i, 1, f );
				streamElemSize = 4 * sizeof( char ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					uc = ( unsigned char ) ( _vertices[ j ].weights[ 0 ] * 255 ); fwrite_le( &uc, 1, f );
					uc = ( unsigned char ) ( _vertices[ j ].weights[ 1 ] * 255 ); fwrite_le( &uc, 1, f );
					uc = ( unsigned char ) ( _vertices[ j ].weights[ 2 ] * 255 ); fwrite_le( &uc, 1, f );
					uc = ( unsigned char ) ( _vertices[ j ].weights[ 3 ] * 255 ); fwrite_le( &uc, 1, f );
				}
				break;
			case 6:		// Texture Coord Set 1
				fwrite_le( &i, 1, f );
				streamElemSize = 2 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					fwrite_le<float>( &_vertices[ j ].texCoords[ 0 ].x, 1, f );
					fwrite_le<float>( &_vertices[ j ].texCoords[ 0 ].y, 1, f );
				}
				break;
			case 7:		// Texture Coord Set 2
				fwrite_le( &i, 1, f );
				streamElemSize = 2 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
				for ( unsigned int j = 0; j < count; ++j )
				{
					fwrite_le<float>( &_vertices[ j ].texCoords[ 1 ].x, 1, f );
					fwrite_le<float>( &_vertices[ j ].texCoords[ 1 ].y, 1, f );
				}
				break;
		}
	}

	// Write triangle indices
	count = ( unsigned int ) _indices.size();
	fwrite_le( &count, 1, f );

	for ( unsigned int i = 0; i < _indices.size(); ++i )
	{
		fwrite_le( &_indices[ i ], 1, f );
	}

	// Write morph targets
	count = ( unsigned int ) _morphTargets.size();
	fwrite_le( &count, 1, f );

	for ( unsigned int i = 0; i < _morphTargets.size(); ++i )
	{
		fwrite_le( _morphTargets[ i ].name, 256, f );

		// Write vertex indices
		count = ( unsigned int ) _morphTargets[ i ].diffs.size();
		fwrite_le( &count, 1, f );

		for ( unsigned int j = 0; j < count; ++j )
		{
			fwrite_le( &_morphTargets[ i ].diffs[ j ].vertIndex, 1, f );
		}

		// Write stream data
		unsigned int numStreams = 4, streamElemSize;
		fwrite_le( &numStreams, 1, f );

		for ( unsigned int j = 0; j < 4; ++j )
		{
			switch ( j )
			{
				case 0:		// Position
					fwrite_le( &j, 1, f );
					streamElemSize = 3 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
					for ( unsigned int k = 0; k < count; ++k )
					{
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].posDiff.x, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].posDiff.y, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].posDiff.z, 1, f );
					}
					break;
				case 1:		// Normal
					fwrite_le( &j, 1, f );
					streamElemSize = 3 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
					for ( unsigned int k = 0; k < count; ++k )
					{
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].normDiff.x, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].normDiff.y, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].normDiff.z, 1, f );
					}
					break;
				case 2:		// Tangent
					fwrite_le( &j, 1, f );
					streamElemSize = 3 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
					for ( unsigned int k = 0; k < count; ++k )
					{
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].tanDiff.x, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].tanDiff.y, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].tanDiff.z, 1, f );
					}
					break;
				case 3:		// Bitangent
					fwrite_le( &j, 1, f );
					streamElemSize = 3 * sizeof( float ); fwrite_le( &streamElemSize, 1, f );
					for ( unsigned int k = 0; k < count; ++k )
					{
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].bitanDiff.x, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].bitanDiff.y, 1, f );
						fwrite_le<float>( &_morphTargets[ i ].diffs[ k ].bitanDiff.z, 1, f );
					}
					break;
			}
		}
	}

	fclose( f );

	return true;
}


void IConverter::writeSGNode( const string &assetPath, const string &modelName, SceneNode *node, unsigned int depth, ofstream &outf ) const
{
	Vec3f trans, rot, scale;
	node->matRel.decompose( trans, rot, scale );
	rot.x = radToDeg( rot.x );
	rot.y = radToDeg( rot.y );
	rot.z = radToDeg( rot.z );

	// Write mesh
	if ( !node->typeJoint )
	{
		Mesh *mesh = ( Mesh * ) node;

		// Write triangle groups as submeshes of first triangle group
		for ( unsigned int i = 0; i < mesh->triGroups.size(); ++i )
		{
			for ( unsigned int j = 0; j < depth + 1; ++j ) outf << "\t";
			if ( i > 0 ) outf << "\t";
			outf << "<Mesh ";
			outf << "name=\"" << ( i > 0 ? "#" : "" ) << mesh->name << "\" ";
			if ( mesh->lodLevel > 0 ) outf << "lodLevel=\"" << mesh->lodLevel << "\" ";
			outf << "material=\"";
			outf << assetPath + modelName + mesh->triGroups[ i ]->matName + ".material.xml\" ";

			if ( i == 0 )
			{
				if ( trans != Vec3f( 0, 0, 0 ) )
					outf << "tx=\"" << trans.x << "\" ty=\"" << trans.y << "\" tz=\"" << trans.z << "\" ";
				if ( rot != Vec3f( 0, 0, 0 ) )
					outf << "rx=\"" << rot.x << "\" ry=\"" << rot.y << "\" rz=\"" << rot.z << "\" ";
				if ( scale != Vec3f( 1, 1, 1 ) )
					outf << "sx=\"" << scale.x << "\" sy=\"" << scale.y << "\" sz=\"" << scale.z << "\" ";
			}

			outf << "batchStart=\"";
			outf << mesh->triGroups[ i ]->first;
			outf << "\" batchCount=\"";
			outf << mesh->triGroups[ i ]->count;
			outf << "\" vertRStart=\"";
			outf << mesh->triGroups[ i ]->vertRStart;
			outf << "\" vertREnd=\"";
			outf << mesh->triGroups[ i ]->vertREnd;
			outf << "\"";

			if ( i == 0 && mesh->triGroups.size() > 1 ) outf << ">\n";
			if ( i > 0 ) outf << " />\n";
		}
	}
	else
	{
		Joint *joint = ( Joint * ) node;

		for ( unsigned int i = 0; i < depth + 1; ++i ) outf << "\t";
		outf << "<Joint ";
		outf << "name=\"" << joint->name << "\" ";
		if ( trans != Vec3f( 0, 0, 0 ) )
			outf << "tx=\"" << trans.x << "\" ty=\"" << trans.y << "\" tz=\"" << trans.z << "\" ";
		if ( rot != Vec3f( 0, 0, 0 ) )
			outf << "rx=\"" << rot.x << "\" ry=\"" << rot.y << "\" rz=\"" << rot.z << "\" ";
		if ( scale != Vec3f( 1, 1, 1 ) )
			outf << "sx=\"" << scale.x << "\" sy=\"" << scale.y << "\" sz=\"" << scale.z << "\" ";
		outf << "jointIndex=\"" << joint->index << "\"";
	}

	if ( node->children.size() == 0 )
	{
		if ( !node->typeJoint && ( ( Mesh * ) node )->triGroups.size() > 1 )
		{
			for ( unsigned int j = 0; j < depth + 1; ++j ) outf << "\t";
			outf << "</Mesh>\n";
		}
		else
		{
			outf << " />\n";
		}
	}
	else
	{
		outf << ">\n";
		for ( unsigned int j = 0; j < node->children.size(); ++j )
			writeSGNode( assetPath, modelName, node->children[ j ], depth + 1, outf );

		// Closing tag
		for ( unsigned int j = 0; j < depth + 1; ++j ) outf << "\t";
		if ( !node->typeJoint ) outf << "</Mesh>\n";
		else outf << "</Joint>\n";
	}
}


bool IConverter::writeSceneGraph( const string &assetPath, const string &assetName, const string &modelName ) const
{
	ofstream outf;
	outf.open( ( _outPath + assetPath + assetName + ".scene.xml" ).c_str(), ios::out );
	if ( !outf.good() )
	{
		log( "Failed to write " + _outPath + assetPath + assetName + ".scene file" );
		return false;
	}

	outf << "<Model name=\"" << assetName << "\" geometry=\"" << assetPath << assetName << ".geo\"";
	if ( _maxLodLevel >= 1 ) outf << " lodDist1=\"" << _lodDist1 << "\"";
	if ( _maxLodLevel >= 2 ) outf << " lodDist2=\"" << _lodDist2 << "\"";
	if ( _maxLodLevel >= 3 ) outf << " lodDist3=\"" << _lodDist3 << "\"";
	if ( _maxLodLevel >= 4 ) outf << " lodDist4=\"" << _lodDist4 << "\"";
	outf << ">\n";

	// Output morph target names as comment
	if ( !_morphTargets.empty() )
	{
		outf << "\t<!-- Morph targets: ";
		for ( unsigned int i = 0; i < _morphTargets.size(); ++i )
		{
			outf << "\"" << _morphTargets[ i ].name << "\" ";
		}
		outf << "-->\n\n";
	}

	// Joints
	for ( unsigned int i = 0; i < _joints.size(); ++i )
	{
		if ( _joints[ i ]->parent == 0x0 ) writeSGNode( assetPath, modelName, _joints[ i ], 0, outf );
	}

	outf << "\n";

	// Meshes
	for ( unsigned int i = 0; i < _meshes.size(); ++i )
	{
		if ( _meshes[ i ]->parent == 0x0 ) writeSGNode( assetPath, modelName, _meshes[ i ], 0, outf );
	}

	outf << "</Model>\n";

	outf.close();

	return true;
}


bool IConverter::writeMaterial( const Material &mat, bool replace ) const
{
	if ( !replace )
	{
		// Skip writing material file if it already exists
		ifstream inf( mat.fileName.c_str() );
		if ( inf.good() )
		{
			log( "Skipping material '" + mat.fileName + ".material.xml'" );
			return true;
		}
	}

	ofstream outf;
	outf.open( mat.fileName.c_str(), ios::out );
	if ( !outf.good() )
	{
		log( "Failed writing .material.xml file" );
		return false;
	}

	outf << "<Material>\n";
	outf << "\t<Shader source=\"shaders/model.shader\" />\n";

	if ( !_joints.empty() )
		outf << "\t<ShaderFlag name=\"_F01_Skinning\" />\n";
	outf << "\n";

	if ( !mat.diffuseMapFileName.empty() )
	{
		outf << "\t<Sampler name=\"albedoMap\" map=\"";
		outf << mat.diffuseMapFileName;
// 		outf << assetPath << material.effect->diffuseMap->fileName;
		outf << "\" />\n";
	}
	else if ( !mat.diffuseColor.empty() )
	{
		outf << "\t<Uniform name=\"matDiffuseCol\" ";
		char value = 'a';
		std::istringstream iss( mat.diffuseColor );
		std::string token;
		while ( std::getline( iss, token, ' ' ) )
		{
			outf << value++ << "=\"" << token << "\" ";
		}
		outf << "/>\n";
	}

	if ( !mat.specularColor.empty() )
	{
		outf << "\t<Uniform name=\"matSpecParams\" ";
		char value = 'a';
		std::istringstream iss( mat.specularColor );
		std::string token;
		while ( std::getline( iss, token, ' ' ) && value < 'd' )
		{
			outf << value++ << "=\"" << token << "\" ";
		}
		outf << "d=\"" << mat.shininess << "\" ";
		outf << "/>\n";
	}

	outf << "</Material>\n";
	outf.close();

	return true;
}


bool IConverter::writeModelCommon( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const
{
	bool result = true;

	if ( !writeGeometry( assetPath, assetName ) ) result = false;
	if ( !writeSceneGraph( assetPath, assetName, modelName ) ) result = false;

	return result;
}


void IConverter::writeAnimFrames( SceneNode &node, FILE *f ) const
{
	fwrite_le( node.name, 256, f );

	// Animation compression: just store a single frame if all frames are equal
	char canCompress = 0;
	if ( node.frames.size() > 1 )
	{
		canCompress = 1;

		// Check if all frames are equal
		for ( unsigned int i = 1; i < node.frames.size(); ++i )
		{
			if ( memcmp( node.frames[ 0 ].x, node.frames[ i ].x, 16 * sizeof( float ) ) != 0 )
			{
				canCompress = 0;
				break;
			}
		}
	}
	fwrite_le( &canCompress, 1, f );

	for ( size_t i = 0; i < ( canCompress ? 1 : node.frames.size() ); ++i )
	{
		Vec3f transVec, rotVec, scaleVec;
		node.frames[ i ].decompose( transVec, rotVec, scaleVec );
		Quaternion rotQuat( rotVec.x, rotVec.y, rotVec.z );

		fwrite_le<float>( &rotQuat.x, 1, f );
		fwrite_le<float>( &rotQuat.y, 1, f );
		fwrite_le<float>( &rotQuat.z, 1, f );
		fwrite_le<float>( &rotQuat.w, 1, f );
		fwrite_le<float>( &transVec.x, 1, f );
		fwrite_le<float>( &transVec.y, 1, f );
		fwrite_le<float>( &transVec.z, 1, f );
		fwrite_le<float>( &scaleVec.x, 1, f );
		fwrite_le<float>( &scaleVec.y, 1, f );
		fwrite_le<float>( &scaleVec.z, 1, f );
	}
}


bool IConverter::writeAnimationCommon( const string &assetPath, const string &assetName ) const
{
	FILE *f = fopen( ( _outPath + assetPath + assetName + ".anim" ).c_str(), "wb" );
	if ( f == 0x0 )
	{
		log( "Failed writing " + _outPath + assetPath + assetName + ".anim file" );
		return false;
	}

	// Write header
	unsigned int version = 3;
	fwrite_le( "H3DA", 4, f );
	fwrite_le( &version, 1, f );

	// Write number of nodes
	unsigned int count = 0;
	for ( unsigned int i = 0; i < _joints.size(); ++i )
		if ( _joints[ i ]->frames.size() > 0 ) ++count;
	for ( unsigned int i = 0; i < _meshes.size(); ++i )
		if ( _meshes[ i ]->frames.size() > 0 ) ++count;
	fwrite_le( &count, 1, f );
	fwrite_le( &_frameCount, 1, f );

	for ( unsigned int i = 0; i < _joints.size(); ++i )
	{
		if ( _joints[ i ]->frames.size() == 0 ) continue;

		writeAnimFrames( *_joints[ i ], f );
	}

	for ( unsigned int i = 0; i < _meshes.size(); ++i )
	{
		if ( _meshes[ i ]->frames.size() == 0 ) continue;

		writeAnimFrames( *_meshes[ i ], f );
	}

	fclose( f );

	return true;
}

} // namespace AssetConverter
} // namespace Horde3D