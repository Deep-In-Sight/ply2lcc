#include "ply_reader.hpp"
#include "miniply/miniply.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace ply2lcc {

// Compute sh_degree from number of f_rest properties
// num_f_rest = ((sh_degree+1)^2 - 1) * 3
static int compute_sh_degree(int num_f_rest) {
    if (num_f_rest == 0) return 0;
    if (num_f_rest == 9) return 1;   // (4-1)*3
    if (num_f_rest == 24) return 2;  // (9-1)*3
    if (num_f_rest == 45) return 3;  // (16-1)*3
    if (num_f_rest == 72) return 4;  // (25-1)*3
    return 3;  // default to degree 3 for unknown
}

bool PLYReader::read_header(const std::string& path, PLYHeader& header) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
        std::cerr << "Failed to open PLY file: " << path << "\n";
        return false;
    }

    // Find vertex element
    while (reader.has_element() && !reader.element_is(miniply::kPLYVertexElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        std::cerr << "No vertex element found in: " << path << "\n";
        return false;
    }

    header.vertex_count = reader.num_rows();

    // Count f_rest properties to determine sh_degree
    header.num_f_rest = 0;
    for (int i = 0; i < 128; ++i) {  // max reasonable count
        char name[16];
        snprintf(name, sizeof(name), "f_rest_%d", i);
        if (reader.find_property(name) == miniply::kInvalidIndex) {
            break;
        }
        header.num_f_rest++;
    }

    header.sh_degree = compute_sh_degree(header.num_f_rest);
    header.has_sh = (header.num_f_rest > 0);

    return true;
}

bool PLYReader::read_splats(const std::string& path,
                           std::vector<Splat>& splats,
                           PLYHeader& header) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
        std::cerr << "Failed to open PLY file: " << path << "\n";
        return false;
    }

    // Find vertex element
    while (reader.has_element() && !reader.element_is(miniply::kPLYVertexElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        std::cerr << "No vertex element found in: " << path << "\n";
        return false;
    }

    const uint32_t num_verts = reader.num_rows();
    header.vertex_count = num_verts;

    // Find property indices
    uint32_t pos_idx[3], normal_idx[3], f_dc_idx[3], opacity_idx, scale_idx[3], rot_idx[4];

    if (!reader.find_properties(pos_idx, 3, "x", "y", "z")) {
        std::cerr << "Missing position properties\n";
        return false;
    }

    bool has_normals = reader.find_properties(normal_idx, 3, "nx", "ny", "nz");

    if (!reader.find_properties(f_dc_idx, 3, "f_dc_0", "f_dc_1", "f_dc_2")) {
        std::cerr << "Missing f_dc properties\n";
        return false;
    }

    opacity_idx = reader.find_property("opacity");
    if (opacity_idx == miniply::kInvalidIndex) {
        std::cerr << "Missing opacity property\n";
        return false;
    }

    if (!reader.find_properties(scale_idx, 3, "scale_0", "scale_1", "scale_2")) {
        std::cerr << "Missing scale properties\n";
        return false;
    }

    if (!reader.find_properties(rot_idx, 4, "rot_0", "rot_1", "rot_2", "rot_3")) {
        std::cerr << "Missing rotation properties\n";
        return false;
    }

    // Count and find f_rest properties
    header.num_f_rest = 0;
    for (int i = 0; i < 128; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "f_rest_%d", i);
        if (reader.find_property(name) == miniply::kInvalidIndex) {
            break;
        }
        header.num_f_rest++;
    }
    header.sh_degree = compute_sh_degree(header.num_f_rest);
    header.has_sh = (header.num_f_rest > 0);

    // Get indices for f_rest properties that exist
    std::vector<uint32_t> f_rest_idx(header.num_f_rest);
    for (int i = 0; i < header.num_f_rest; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "f_rest_%d", i);
        f_rest_idx[i] = reader.find_property(name);
    }

    // Load element data
    if (!reader.load_element()) {
        std::cerr << "Failed to load vertex data\n";
        return false;
    }

    // Allocate temporary buffers
    std::vector<float> positions(num_verts * 3);
    std::vector<float> normals(num_verts * 3);
    std::vector<float> f_dc(num_verts * 3);
    std::vector<float> f_rest(header.has_sh ? num_verts * header.num_f_rest : 0);
    std::vector<float> opacities(num_verts);
    std::vector<float> scales(num_verts * 3);
    std::vector<float> rotations(num_verts * 4);

    // Extract properties
    reader.extract_properties(pos_idx, 3, miniply::PLYPropertyType::Float, positions.data());

    if (has_normals) {
        reader.extract_properties(normal_idx, 3, miniply::PLYPropertyType::Float, normals.data());
    }

    reader.extract_properties(f_dc_idx, 3, miniply::PLYPropertyType::Float, f_dc.data());
    reader.extract_properties(&opacity_idx, 1, miniply::PLYPropertyType::Float, opacities.data());
    reader.extract_properties(scale_idx, 3, miniply::PLYPropertyType::Float, scales.data());
    reader.extract_properties(rot_idx, 4, miniply::PLYPropertyType::Float, rotations.data());

    if (header.has_sh) {
        for (int i = 0; i < header.num_f_rest; ++i) {
            std::vector<float> tmp(num_verts);
            reader.extract_properties(&f_rest_idx[i], 1, miniply::PLYPropertyType::Float, tmp.data());
            for (uint32_t v = 0; v < num_verts; ++v) {
                f_rest[v * header.num_f_rest + i] = tmp[v];
            }
        }
    }

    // Convert to Splat structs
    splats.resize(num_verts);
    for (uint32_t i = 0; i < num_verts; ++i) {
        Splat& s = splats[i];
        s.pos = Vec3f(positions[i*3], positions[i*3+1], positions[i*3+2]);

        if (has_normals) {
            s.normal = Vec3f(normals[i*3], normals[i*3+1], normals[i*3+2]);
        } else {
            s.normal = Vec3f(0, 0, 0);
        }

        s.f_dc[0] = f_dc[i*3];
        s.f_dc[1] = f_dc[i*3+1];
        s.f_dc[2] = f_dc[i*3+2];

        // Copy f_rest, zero-padding if fewer than 45 coefficients
        memset(s.f_rest, 0, sizeof(s.f_rest));
        if (header.has_sh) {
            int copy_count = std::min(header.num_f_rest, 45);
            for (int j = 0; j < copy_count; ++j) {
                s.f_rest[j] = f_rest[i * header.num_f_rest + j];
            }
        }

        s.opacity = opacities[i];
        s.scale = Vec3f(scales[i*3], scales[i*3+1], scales[i*3+2]);
        s.rot[0] = rotations[i*4];     // w
        s.rot[1] = rotations[i*4+1];   // x
        s.rot[2] = rotations[i*4+2];   // y
        s.rot[3] = rotations[i*4+3];   // z

        // Update bbox
        header.bbox.expand(s.pos);
    }

    return true;
}

bool PLYReader::stream_splats(const std::string& path,
                             const SplatCallback& callback,
                             PLYHeader& header) {
    std::vector<Splat> splats;
    if (!read_splats(path, splats, header)) {
        return false;
    }

    for (size_t i = 0; i < splats.size(); ++i) {
        callback(splats[i], i);
    }

    return true;
}

} // namespace ply2lcc
