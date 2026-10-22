// stb_image 2.30 (public domain / MIT, by Sean Barrett): the JPEG and PNG pictures a gene card's
// page shows (DBSystem::ShowCard's index.html - its NNN.jpg, r_NN.png and e_NN.png)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
