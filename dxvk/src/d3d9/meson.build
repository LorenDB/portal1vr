fs = import('fs')
portal_root = fs.parent(meson.project_source_root())
portal1vr_cpp_args = [
  '-I' + join_paths(portal_root, 'mingw-include'),
  '-I' + portal_root,
  '-I' + join_paths(portal_root, 'L4D2VR'),
  '-I' + join_paths(portal_root, 'L4D2VR/sdk'),
  '-I' + join_paths(portal_root, 'thirdparty/minhook/include'),
  '-I' + join_paths(portal_root, 'thirdparty/openvr/include'),
  '-DPORTAL_1',
  '-D_USRDLL',
  '-D_CRT_SECURE_NO_WARNINGS',
  '-fms-extensions',
]
openvr_lib = cpp.find_library('openvr_api', dirs : join_paths(portal_root, 'thirdparty/openvr/lib/win32'))
lib_psapi = cpp.find_library('psapi')
lib_user32 = cpp.find_library('user32')

d3d9_res = wrc_generator.process('version.rc')

# OpenVR matrix methods return HmdMatrix34_t by value using MSVC thiscall.
# The MinGW Clang ABI puts that return slot in ECX, which crashes the first
# tracking update. Compile just this file for the MSVC x86 C++ ABI.
msvc_cc = find_program(cpp.cmd_array()[0])
msvc_openvr_abi = custom_target(
  'msvc_openvr_abi',
  input : files(join_paths(portal_root, 'L4D2VR/msvc_openvr_abi.cpp')),
  output : 'msvc_openvr_abi.o',
  command : [
    msvc_cc, '--target=i686-pc-windows-msvc', '-c', '-O2',
    '-fno-exceptions', '-fno-rtti', '-o', '@OUTPUT@', '@INPUT@',
  ],
)

d3d9_shaders = files([
  'shaders/d3d9_convert_yuy2_uyvy.comp',
  'shaders/d3d9_convert_l6v5u5.comp',
  'shaders/d3d9_convert_x8l8v8u8.comp',
  'shaders/d3d9_convert_a2w10v10u10.comp',
  'shaders/d3d9_convert_nv12.comp',
  'shaders/d3d9_convert_yv12.comp'
])

d3d9_src = [
  'd3d9_main.cpp',
  'd3d9_interface.cpp',
  'd3d9_adapter.cpp',
  'd3d9_monitor.cpp',
  'd3d9_device.cpp',
  'd3d9_state.cpp',
  'd3d9_cursor.cpp',
  'd3d9_swapchain.cpp',
  'd3d9_format.cpp',
  'd3d9_common_texture.cpp',
  'd3d9_texture.cpp',
  'd3d9_surface.cpp',
  'd3d9_volume.cpp',
  'd3d9_common_buffer.cpp',
  'd3d9_buffer.cpp',
  'd3d9_shader.cpp',
  'd3d9_vertex_declaration.cpp',
  'd3d9_query.cpp',
  'd3d9_multithread.cpp',
  'd3d9_options.cpp',
  'd3d9_stateblock.cpp',
  'd3d9_sampler.cpp',
  'd3d9_util.cpp',
  'd3d9_initializer.cpp',
  'd3d9_fixed_function.cpp',
  'd3d9_names.cpp',
  'd3d9_swvp_emu.cpp',
  'd3d9_format_helpers.cpp',
  'd3d9_hud.cpp',
  'd3d9_annotation.cpp',
  'd3d9_vr.cpp',
  '../../../L4D2VR/dllmain.cpp',
  '../../../L4D2VR/game.cpp',
  '../../../L4D2VR/hooks.cpp',
  '../../../L4D2VR/sdk/bitbuf.cpp',
  '../../../L4D2VR/sdk/checksum_crc.cpp',
  '../../../L4D2VR/sdk/newbitbuf.cpp',
  '../../../L4D2VR/vr.cpp',
  '../../../thirdparty/minhook/src/buffer.c',
  '../../../thirdparty/minhook/src/hook.c',
  '../../../thirdparty/minhook/src/trampoline.c',
  '../../../thirdparty/minhook/src/hde/hde32.c',
  '../../../thirdparty/minhook/src/hde/hde64.c',
]

d3d9_dll = shared_library('d3d9'+dll_ext, d3d9_src, glsl_generator.process(d3d9_shaders), d3d9_res, msvc_openvr_abi,
  name_prefix         : '',
  dependencies        : [ dxso_dep, dxvk_dep, openvr_lib, lib_psapi, lib_user32 ],
  include_directories : dxvk_include_path,
  cpp_args            : portal1vr_cpp_args,
  install             : true,
  vs_module_defs      : 'd3d9'+def_spec_ext,
)

d3d9_dep = declare_dependency(
  link_with           : [ d3d9_dll ],
  include_directories : [ dxvk_include_path ],
)
