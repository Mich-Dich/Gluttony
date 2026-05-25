

sudo chown -R mich:mich ~/workspace/Gluttony









time ( cmake --preset debug; cmake --build --preset debug --parallel )
real	0m27,951s
user	3m55,128s
sys	    0m29,524s









-- Repository already present in /home/mich/workspace/Gluttony/vendor/glm
-- Repository already present in /home/mich/workspace/Gluttony/vendor/imgui
-- Already on correct branch 'docking'
-- 
-- -------------------------------------------------------------------------------------------------
--  [Gluttony] source file details: 22 source files, total size 136.52 KiB
--   121 lines    4.60 KiB   src/application.cpp
--   104 lines    4.33 KiB   src/config/config.cpp
--   508 lines   21.74 KiB   src/config/imgui_config.cpp
--    67 lines    2.69 KiB   src/entry_point.cpp
--    40 lines    1.42 KiB   src/layer/layer.cpp
--    81 lines    2.69 KiB   src/layer/layer_stack.cpp
--   502 lines   18.99 KiB   src/plugin_system/plugin_manager.cpp
--   231 lines    7.81 KiB   src/util/crash_handler.cpp
--    70 lines    2.23 KiB   src/util/data_structures/UUID.cpp
--   107 lines    3.33 KiB   src/util/data_structures/string_manipulation.cpp
--   107 lines    3.14 KiB   src/util/data_structures/thread_pool.cpp
--    74 lines    2.69 KiB   src/util/data_structures/type_deletion_queue.cpp
--   216 lines    6.85 KiB   src/util/io/logger.cpp
--    56 lines    2.06 KiB   src/util/io/serializer_binary.cpp
--   350 lines   11.39 KiB   src/util/io/serializer_yaml.cpp
--   428 lines   13.75 KiB   src/util/io/vfs.cpp
--   207 lines    6.28 KiB   src/util/math/math.cpp
--    46 lines    1.86 KiB   src/util/math/random.cpp
--   256 lines    9.75 KiB   src/util/system.cpp
--    58 lines    2.40 KiB   src/util/timing/interval_controller.cpp
--    72 lines    2.60 KiB   src/util/timing/stopwatch.cpp
--    97 lines    3.79 KiB   src/world/object/camera.cpp
-- -------------------------------------------------------------------------------------------------
-- Repository already present in /home/mich/workspace/Gluttony/plugin/glfw_window/vendor/glfw
-- Including X11 support
-- 
-- -------------------------------------------------------------------------------------------------
--  [glfw_window] source file details: 1 source files, total size 8.15 KiB
--   200 lines   8.15 KiB   src/entry_point.cpp
-- -------------------------------------------------------------------------------------------------
-- 
-- -------------------------------------------------------------------------------------------------
--  [input_action_mapper] source file details: 1 source files, total size 3.32 KiB
--   95 lines   3.32 KiB   src/entry_point.cpp
-- -------------------------------------------------------------------------------------------------
-- 
-- -------------------------------------------------------------------------------------------------
--  [logger] source file details: 2 source files, total size 29.63 KiB
--    88 lines    4.09 KiB   src/entry_point.cpp
--   537 lines   25.53 KiB   src/logger.cpp
-- -------------------------------------------------------------------------------------------------
-- Repository already present in /home/mich/workspace/Gluttony/plugin/renderer_vk_ray/vendor/vk-bootstrap
-- Repository already present in /home/mich/workspace/Gluttony/plugin/renderer_vk_ray/vendor/vk_ray
-- Repository already present in /home/mich/workspace/Gluttony/plugin/renderer_vk_ray/vendor/VulkanMemoryAllocator
-- Could NOT find Vulkan (missing: dxc) (found suitable version "1.4.341", minimum required is "1.3")
-- vk_ray: Using external dependencies from vendor/
-- vk_ray Configuration:
--   - VK_RAY_BUILD_DENOISERS: OFF
--   - VK_RAY_BUILD_VULKAN_BUILDER: ON
--   - VK_RAY_USE_EXTERNAL_DEPS: ON
--     Using external vk-bootstrap and VMA from vendor/
-- 
-- -------------------------------------------------------------------------------------------------
--  [renderer_vk_ray] source file details: 4 source files, total size 47.26 KiB
--   223 lines    9.82 KiB   src/entry_point.cpp
--    92 lines    3.43 KiB   src/util/data_structures.cpp
--   265 lines   11.88 KiB   src/util/shader_compiler.cpp
--   421 lines   22.11 KiB   src/util/utils.cpp
-- -------------------------------------------------------------------------------------------------
-- Repository already present in /home/mich/workspace/Gluttony/plugin/virtual_file_system_vfspp/vendor/vfspp
-- Patched miniz-cpp for LP64 compatibility.
-- 
-- -------------------------------------------------------------------------------------------------
--  [virtual_file_system_vfspp] source file details: 2 source files, total size 17.54 KiB
--    96 lines    4.39 KiB   src/entry_point.cpp
--   378 lines   13.15 KiB   src/native.cpp
-- -------------------------------------------------------------------------------------------------
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /home/mich/workspace/Gluttony/build
[  2%] Built target vk-bootstrap
[  4%] Built target logger
[  7%] Building CXX object plugin/virtual_file_system_vfspp/CMakeFiles/virtual_file_system_vfspp_static.dir/src/native.cpp.o
[  8%] Building CXX object plugin/virtual_file_system_vfspp/CMakeFiles/virtual_file_system_vfspp_static.dir/src/entry_point.cpp.o
[  8%] Built target logger_static
[  9%] Building CXX object plugin/virtual_file_system_vfspp/CMakeFiles/virtual_file_system_vfspp.dir/src/entry_point.cpp.o
[ 10%] Building CXX object plugin/virtual_file_system_vfspp/CMakeFiles/virtual_file_system_vfspp.dir/src/native.cpp.o
[ 12%] Built target input_action_mapper
[ 13%] Built target input_action_mapper_static
[ 31%] Built target glfw
[ 40%] Built target vk_ray
[ 45%] Built target imgui
[ 58%] Built target glfw_window
[ 59%] Built target glfw_window_static
[ 68%] Built target renderer_vk_ray
[ 77%] Built target renderer_vk_ray_static
[100%] Built target Gluttony
[100%] Linking CXX shared library ../../bin/debug/plugin/virtual_file_system_vfspp/libvirtual_file_system_vfspp.so
[100%] Linking CXX static library ../../bin/debug/plugin/virtual_file_system_vfspp/libvirtual_file_system_vfspp_static.a
-- ====== virtual_file_system_vfspp built successfully =============================================
[100%] Built target virtual_file_system_vfspp
[100%] Built target virtual_file_system_vfspp_static
root@kubuntutower:/home/mich/workspace/Gluttony# 