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

#include "daeMain.h"
#include "converter.h"
#include "utPlatform.h"
#include <algorithm>
#include <memory>

#ifdef PLATFORM_WIN
#   define WIN32_LEAN_AND_MEAN 1
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#	include <direct.h>
#else
#   include <unistd.h>
#   define _chdir chdir
#	include <sys/stat.h>
#	include <dirent.h>
#endif

using namespace std;
using namespace Horde3D;
using namespace AssetConverter;


struct AssetTypes
{
	enum List
	{
		Unknown,
		Model,
		Animation
	};
};

struct SupportedFormats
{
	enum List
	{
		Unsupported = 0,
		Collada,
		GLTF,
		FBX
	};
};

struct Asset
{
	SupportedFormats::List	format;
	std::string				path;

	Asset() {}

	Asset( int f, const std::string &p ): format( ( SupportedFormats::List ) f ), path( p )
	{

	}
};

struct ConverterParameters
{
	std::string assetPath;
	std::string assetName;
	std::string modelName;
	std::string sourcePath;
	std::string outPath;

	float lodDists[ 4 ];

	int assetType;
	bool overwriteMats;
	bool optimizeGeometry;
};

int checkFileSupported( const std::string &input )
{
	size_t len = input.length();
	const char *file = input.c_str();

	if ( len > 4 && _stricmp( file + ( len - 4 ), ".dae" ) == 0 ) return SupportedFormats::Collada;
	if ( len > 4 && _stricmp( file + ( len - 4 ), ".fbx" ) == 0 ) return SupportedFormats::FBX;
	if ( len > 4 && _stricmp( file + ( len - 4 ), ".bin" ) == 0 ) return SupportedFormats::GLTF;
	if ( len > 5 && _stricmp( file + ( len - 5 ), ".gltf" ) == 0 ) return SupportedFormats::GLTF;

	return SupportedFormats::Unsupported;
}

void createAssetList( const string &basePath, const string &assetPath, vector< Asset > &assetList )
{
	vector< string >  directories;
	vector< string >  files;
	
// Find all files and subdirectories in current search path
#ifdef PLATFORM_WIN
	string searchString( basePath + assetPath + "*" );
	
	WIN32_FIND_DATA fdat;
	HANDLE h = FindFirstFile( searchString.c_str(), &fdat );
	if( h == INVALID_HANDLE_VALUE ) return;
	do
	{
		// Ignore hidden files
		if( strcmp( fdat.cFileName, "." ) == 0 || strcmp( fdat.cFileName, ".." ) == 0 ||
		    fdat.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN )
		{	
			continue;
		}
		
		if( fdat.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			directories.push_back( fdat.cFileName );
		else
			files.push_back( fdat.cFileName );
	} while( FindNextFile( h, &fdat ) );
#else
	dirent *dirEnt;
	struct stat fileStat;
	string finalPath = basePath + assetPath;
	DIR *dir = opendir( finalPath.c_str() );
	if( dir == 0x0 ) return;

	while( (dirEnt = readdir( dir )) != 0x0 )
	{
		if( dirEnt->d_name[0] == '.' ) continue;  // Ignore hidden files

		lstat( (finalPath + dirEnt->d_name).c_str(), &fileStat );
		
		if( S_ISDIR( fileStat.st_mode ) )
			directories.push_back( dirEnt->d_name );
		else if( S_ISREG( fileStat.st_mode ) )
			files.push_back( dirEnt->d_name );
	}

	closedir( dir );

	sort( directories.begin(), directories.end() );
	sort( files.begin(), files.end() );
#endif

	// Check file extensions
	for( unsigned int i = 0; i < files.size(); ++i )
	{
		int fileSupported = checkFileSupported( files[ i ] );
		if ( fileSupported != SupportedFormats::Unsupported )
		{
			Asset asset( fileSupported, assetPath + files[ i ] );
			assetList.emplace_back( asset );
		}
	}
	
	// Search in subdirectories
	for( unsigned int i = 0; i < directories.size(); ++i )
	{
		createAssetList( basePath, assetPath + directories[i] + "/", assetList );
	}
}


void printHelp()
{
	log( "Usage:" );
	log( "AssetConv input [optional arguments]" );
	log( "Supported formats: collada (dae), gltf (gltf, bin), fbx (fbx)" );
	log( "" );
	log( "input             asset file or directory to be processed" );
	log( "-type model|anim  asset type to be processed (default: model)" );
	log( "-base path        base path where the repository root is located" );
	log( "-dest path        existing destination path where output is written" );
	log( "-noGeoOpt         disable geometry optimization" );
	log( "-overwriteMats    force update of existing materials" );
	log( "-addModelName     adds model name before material name" );
	log( "-lodDist1 dist    distance for LOD1" );
	log( "-lodDist2 dist    distance for LOD2" );
	log( "-lodDist3 dist    distance for LOD3" );
	log( "-lodDist4 dist    distance for LOD4" );
}

bool parseColladaFile( const Asset &asset, const ConverterParameters &params )
{
	std::unique_ptr< ColladaDocument > daeDoc = std::unique_ptr< ColladaDocument >( new ColladaDocument() ); // make_unique not available in c++11

	log( "Parsing dae asset '" + asset.path + "'..." );
	if ( !daeDoc->parseFile( params.sourcePath ) )
		return false;

	if ( params.assetType == AssetTypes::Model )
	{
		log( "Compiling model data..." );
		Converter *converter = new Converter( *daeDoc, params.outPath, params.lodDists );
		converter->convertModel( params.optimizeGeometry );

		createDirectories( params.outPath, params.assetPath );
		converter->writeModel( params.assetPath, params.assetName, params.modelName );
		converter->writeMaterials( params.assetPath, params.modelName, params.overwriteMats );

		delete converter; converter = 0x0;
	}
	else if ( params.assetType == AssetTypes::Animation )
	{
		log( "Compiling animation data..." );
		Converter *converter = new Converter( *daeDoc, params.outPath, params.lodDists );
		converter->convertModel( false );

		if ( converter->hasAnimation() )
		{
			createDirectories( params.outPath, params.assetPath );
			converter->writeAnimation( params.assetPath, params.assetName );
		}
		else
		{
			log( "Skipping file (does not contain animation data)" );
		}

		delete converter; converter = 0x0;
	}

// 	delete daeDoc; daeDoc = 0x0;
	return true;
}

bool parseFBXFile( const Asset &asset )
{
	return true;
}

bool parseGLTFFile( const Asset &asset )
{
	return true;
}

int main( int argc, char **argv )
{
	log( "Horde3D Asset Converter - 2.1.0" );
	log( "" );
	
	if( argc < 2 )
	{
		printHelp();
		return 1;
	}
	
	// =============================================================================================
	// Parse arguments
	// =============================================================================================

	vector< Asset > assetList;
	string input = argv[1], basePath = "./", outPath = "./";
	AssetTypes::List assetType = AssetTypes::Model;
	bool geoOpt = true, overwriteMats = false, addModelName = false;
	float lodDists[4] = { 10, 20, 40, 80 };
	string modelName = "";	

	// Make sure that first argument is not an option
	if( argv[1][0] == '-' )
	{
		log( "Missing input file or dir; use . for repository root" );
		return 1;
	}
	
	// Check optional arguments
	for( int i = 2; i < argc; ++i )
	{
		std::string arg = argv[i];
		arg.erase(remove_if(arg.begin(), arg.end(), ::isspace), arg.end());


		if( _stricmp( arg.c_str(), "-type" ) == 0 && argc > i + 1 )
		{
			if( _stricmp( argv[++i], "model" ) == 0 ) assetType = AssetTypes::Model;
			else if( _stricmp( argv[i], "anim" ) == 0 ) assetType = AssetTypes::Animation;
			else assetType = AssetTypes::Unknown;
		}
		else if( _stricmp( arg.c_str(), "-base" ) == 0 && argc > i + 1 )
		{
			basePath = cleanPath( argv[++i] ) + "/";
		}
		else if( _stricmp( arg.c_str(), "-dest" ) == 0 && argc > i + 1 )
		{
			outPath = cleanPath( argv[++i] ) + "/";
		}
		else if( _stricmp( arg.c_str(), "-noGeoOpt" ) == 0 )
		{
			geoOpt = false;
		}
		else if( _stricmp( arg.c_str(), "-overwriteMats" ) == 0 )
		{
			overwriteMats = true;
		}
		else if( (_stricmp( arg.c_str(), "-lodDist1" ) == 0 || _stricmp( arg.c_str(), "-lodDist2" ) == 0 ||
		          _stricmp( arg.c_str(), "-lodDist3" ) == 0 || _stricmp( arg.c_str(), "-lodDist4" ) == 0) && argc > i + 1 )
		{
			int index = 0;
			if( _stricmp( arg.c_str(), "-lodDist2" ) == 0 ) index = 1;
			else if( _stricmp( arg.c_str(), "-lodDist3" ) == 0 ) index = 2;
			else if( _stricmp( arg.c_str(), "-lodDist4" ) == 0 ) index = 3;
			
			lodDists[index] = toFloat( argv[++i] );
		}
		else if( _stricmp( arg.c_str(), "-addModelName" ) == 0 )
		{
			addModelName = true;
		}
		else
		{
			log( std::string( "Invalid arguments: '" ) + arg.c_str() + std::string( "'" ) );
			printHelp();
			return 1;
		}
	}

	// Check whether input is single file or directory and create asset input list
	int fileSupported = checkFileSupported( input );
	if( fileSupported != SupportedFormats::Unsupported )
	{
		// Check if it's an absolute path
		if( input[0] == '/' || input[1] == ':' || input[0] == '\\' )
		{
			size_t index = input.find_last_of( "\\/" );
			_chdir( input.substr( 0, index ).c_str() );
			input = input.substr( index + 1, input.length() - index );
		}

		Asset asset( fileSupported, input );
		assetList.emplace_back( asset );
	}
	else
	{
		if( input == "." ) input = "";
		else input = cleanPath( input ) + "/";
		createAssetList( basePath, input, assetList );
	}

	// =============================================================================================
	// Batch conversion
	// =============================================================================================

	if( assetType == AssetTypes::Unknown )
	{
		log( "Error: Asset type not supported by ColladaConv" );
		return 1;
	}
	else
	{
		if( assetType == AssetTypes::Model )
			log( "Processing MODELS - Path: " + input );
		else if( assetType == AssetTypes::Animation )
			log( "Processing ANIMATIONS - Path: " + input );
		log( "" );
	}
	
	string tmpStr;
	tmpStr.reserve( 256 );
	
	// Prepare converter parameters and start parsing/converting
	ConverterParameters cvParams;
	cvParams.optimizeGeometry = geoOpt;
	cvParams.overwriteMats = overwriteMats;
	memcpy( &cvParams.lodDists, &lodDists, sizeof( float ) * 4 );
	cvParams.outPath = outPath;

	for( unsigned int i = 0; i < assetList.size(); ++i )
	{
		if( assetType == AssetTypes::Model || assetType == AssetTypes::Animation )
		{
			cvParams.sourcePath = basePath + assetList[i].path;
			cvParams.assetName = extractFileName( assetList[i].path, false );

			if( addModelName )	
				cvParams.modelName = cvParams.assetName + "_";

			cvParams.assetPath = cleanPath( extractFilePath( assetList[i].path ) );
			if( !cvParams.assetPath.empty() ) cvParams.assetPath += "/";
			
			bool result = true;
			switch ( assetList[ i ].format )
			{
				case SupportedFormats::Collada:
					result = parseColladaFile( assetList[ i ], cvParams );
					break;
				case SupportedFormats::FBX:
					result = parseFBXFile( assetList[ i ] );
					break;
				case SupportedFormats::GLTF:
					result = parseGLTFFile( assetList[ i ] );
					break;
				default:
					break;
			}

			if ( !result )
			{
				log( "Failed to parse and convert file '" + assetList[ i ].path + "'. Skipping." );
				continue;
			}
		}

		log( "" );
	}
	
	return 0;
}
