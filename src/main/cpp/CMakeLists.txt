cmake_minimum_required(VERSION 3.22.1)
project(kesho_cad CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OCCT_DIR "" CACHE PATH "OCCT install dir for this ABI")

add_library(kesho_cad SHARED cad_jni.cpp)

if(OCCT_DIR)
  target_include_directories(kesho_cad PRIVATE ${OCCT_DIR}/include/opencascade)
  target_link_directories(kesho_cad PRIVATE ${OCCT_DIR}/lib)
  target_link_libraries(kesho_cad PRIVATE
    TKernel TKDE TKCDF TKLCAF TKMath TKG2d TKG3d TKGeomBase
    TKBRep TKGeomAlgo TKTopAlgo TKPrim TKShHealing TKBO TKCAF
    TKService TKMesh TKHLR TKV3d TKVCAF TKXCAF TKXSBase
    TKDESTEP TKDEIGES TKXDESTEP TKXDEIGES TKBool)
endif()

find_library(log-lib log)
target_link_libraries(kesho_cad PRIVATE ${log-lib})
