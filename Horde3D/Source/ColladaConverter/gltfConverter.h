#pragma once

#include "converterCommon.h"
#include "gltf/tiny_gltf.h"

namespace Horde3D {
namespace AssetConverter {

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


	SceneNode * createSceneNode( AvailableSceneNodeTypes type ) override
	{
		throw std::logic_error( "The method or operation is not implemented." );
	}

private:

	tinygltf::Model _model;
};

} // namespace AssetConverter
} // namespace Horde3D