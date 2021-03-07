include_directories(../Shared)

# Do not build asset converter for ios or android
if( (NOT ${CMAKE_SYSTEM_NAME} MATCHES "iOS") AND (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Android") )
add_executable(AssetConv 
	converterCommon.h
	gltfConverter.h
	colladaConverter.h
	daeCommon.h
	daeLibAnimations.h
	daeLibControllers.h
	daeLibEffects.h
	daeLibGeometries.h
	daeLibImages.h
	daeLibMaterials.h
	daeLibVisualScenes.h
	gltf/tiny_gltf.h
	daeMain.h
	optimizer.h
	utils.h
	converterCommon.cpp
	colladaConverter.cpp
	gltfConverter.cpp
	daeMain.cpp
	main.cpp
	optimizer.cpp
	utils.cpp
	)
endif()
