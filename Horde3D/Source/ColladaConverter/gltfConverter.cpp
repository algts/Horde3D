#include "gltfConverter.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "gltf/tiny_gltf.h"

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

GLTFConverter::GLTFConverter( const tinygltf::Model &model, const std::string &outPath, const float *lodDists ): IConverter() 
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

	return true;
}

bool GLTFConverter::writeModel( const std::string &assetPath, const std::string &assetName, const std::string &modelName ) const
{
	throw std::logic_error( "The method or operation is not implemented." );
}

bool GLTFConverter::writeMaterials( const std::string &assetPath, const std::string &modelName, bool replace ) const
{
	throw std::logic_error( "The method or operation is not implemented." );
}

bool GLTFConverter::hasAnimation() const
{
	throw std::logic_error( "The method or operation is not implemented." );
}

bool GLTFConverter::writeAnimation( const std::string &assetPath, const std::string &assetName ) const
{
	throw std::logic_error( "The method or operation is not implemented." );
}

} // namespace AssetConverter
} // namespace Horde3D
