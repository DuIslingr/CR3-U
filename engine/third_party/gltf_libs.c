// The model bridge's libraries, built once: cgltf 1.13 (MIT, Johannes Kuhlmann) reads glTF 2.0
// (.glb / .gltf) replacements; stb_image_write 1.16 (public domain / MIT, Sean Barrett) encodes the
// exports' PNGs (stb_image itself is built by stb/stb_image.c)
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"
