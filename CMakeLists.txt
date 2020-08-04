cmake_minimum_required(VERSION 2.8.3)
project(coarsetofine)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS "-std=c++11")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")


find_package(PCL REQUIRED)
find_package(Ceres REQUIRED)
FIND_PACKAGE(yaml-cpp REQUIRED)


include_directories(
  ${PCL_INCLUDE_DIRS}
  ${CERES_INCLUDE_DIRS}
)


ADD_LIBRARY(coarsetofineICP SHARED scene_alignment.cpp)
target_link_libraries(coarsetofineICP ${PCL_LIBRARIES} ${CERES_LIBRARIES})





# add_executable(ascanRegistration src/scanRegistration.cpp
# 		                         src/RangenetAPI.cpp)
# target_link_libraries(ascanRegistration ${catkin_LIBRARIES} ${PCL_LIBRARIES}  ${OpenCV_LIBS})

# add_executable(alaserOdometry src/laserOdometry.cpp)
# target_link_libraries(alaserOdometry ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${CERES_LIBRARIES})

# add_executable(alaserMapping src/laserMapping.cpp
# 		                     src/RangenetAPI.cpp)
# target_link_libraries(alaserMapping ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${CERES_LIBRARIES}
# 		${OpenCV_LIBS}
# 		LiPMatch
# 		)

# add_executable(loopclosureFast src/loopclosureFast.cpp)
# target_link_libraries(loopclosureFast ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${CERES_LIBRARIES} ${OpenCV_LIBS} LiPMatch)


# add_executable(makeTargetMap src/makeTargetMap.cpp)
# target_link_libraries(makeTargetMap ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${CERES_LIBRARIES} ${OpenCV_LIBS} LiPMatch)


# add_executable(localizationFast src/localizationFast.cpp)
# target_link_libraries(localizationFast ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${CERES_LIBRARIES} ${OpenCV_LIBS} LiPMatch)



# add_executable(kittiHelper src/kittiHelper.cpp
# 		                   src/RangenetAPI.cpp)
# target_link_libraries(kittiHelper ${catkin_LIBRARIES} ${PCL_LIBRARIES} ${OpenCV_LIBS})




