#include "splat_buffer.hpp"
#include <cstring>
#include <iostream>

namespace ply2lcc {

// Compute sh_degree from number of f_rest properties
static int compute_sh_degree(int num_f_rest) {
    if (num_f_rest == 0) return 0;
    if (num_f_rest == 9) return 1;
    if (num_f_rest == 24) return 2;
    if (num_f_rest == 45) return 3;
    if (num_f_rest == 72) return 4;
    return 3;
}

bool SplatBuffer::initialize(const std::string& path) {
    // Use PLYReaderMmap for PLY parsing and memory mapping
    m_reader = std::make_unique<PLYReaderMmap>(path);

    if (!m_reader->valid()) {
        m_error = "Failed to open PLY file: " + path;
        return false;
    }

    // Find vertex element
    while (m_reader->has_element() && !m_reader->element_is(miniply::kPLYVertexElement)) {
        m_reader->next_element();
    }

    if (!m_reader->has_element()) {
        m_error = "No vertex element found";
        return false;
    }

    // Validate required Gaussian splatting properties
    uint32_t pos_idx[3], normal_idx[3], f_dc_idx[3], opacity_idx, scale_idx[3], rot_idx[4];

    if (!m_reader->find_properties(pos_idx, 3, "x", "y", "z")) {
        m_error = "Missing position properties (x, y, z)";
        return false;
    }

    bool has_normals = m_reader->find_properties(normal_idx, 3, "nx", "ny", "nz");

    if (!m_reader->find_properties(f_dc_idx, 3, "f_dc_0", "f_dc_1", "f_dc_2")) {
        m_error = "Missing f_dc properties - not a Gaussian splatting file";
        return false;
    }

    opacity_idx = m_reader->find_property("opacity");
    if (opacity_idx == miniply::kInvalidIndex) {
        m_error = "Missing opacity property";
        return false;
    }

    if (!m_reader->find_properties(scale_idx, 3, "scale_0", "scale_1", "scale_2")) {
        m_error = "Missing scale properties";
        return false;
    }

    if (!m_reader->find_properties(rot_idx, 4, "rot_0", "rot_1", "rot_2", "rot_3")) {
        m_error = "Missing rotation properties";
        return false;
    }

    // Count f_rest properties
    uint32_t f_rest_first_idx = miniply::kInvalidIndex;
    int num_f_rest = 0;
    for (int i = 0; i < 128; ++i) {
        char name[20];
        snprintf(name, sizeof(name), "f_rest_%d", i);
        uint32_t idx = m_reader->find_property(name);
        if (idx == miniply::kInvalidIndex) break;
        if (i == 0) f_rest_first_idx = idx;
        num_f_rest++;
    }

    // Memory map the element data
    uint32_t rowStride, numRows;
    const uint8_t* data = m_reader->map_element(&rowStride, &numRows);
    if (!data) {
        m_error = "Failed to map element: " + std::string(
            m_reader->file_type() != miniply::PLYFileType::Binary
                ? "only binary little-endian format supported"
                : "mapping failed");
        return false;
    }

    // Build property table from miniply's element metadata
    const miniply::PLYElement* elem = m_reader->element();

    m_table.pos = elem->properties[pos_idx[0]].offset;
    m_table.normal = has_normals ? elem->properties[normal_idx[0]].offset : 0;
    m_table.f_dc = elem->properties[f_dc_idx[0]].offset;
    m_table.opacity = elem->properties[opacity_idx].offset;
    m_table.scale = elem->properties[scale_idx[0]].offset;
    m_table.rot = elem->properties[rot_idx[0]].offset;
    m_table.f_rest = (num_f_rest > 0) ? elem->properties[f_rest_first_idx].offset : 0;
    m_table.row_stride = rowStride;
    m_table.num_rows = numRows;
    m_table.num_f_rest = num_f_rest;
    m_table.sh_degree = compute_sh_degree(num_f_rest);
    m_table.has_normal = has_normals;

    m_data = data;
    return true;
}

std::vector<Splat> SplatBuffer::to_vector() const {
    if (!valid()) return {};

    std::vector<Splat> result(m_table.num_rows);
    const int copy_count = std::min(m_table.num_f_rest, 45);

    for (uint32_t i = 0; i < m_table.num_rows; ++i) {
        SplatView v = (*this)[i];
        Splat& s = result[i];

        s.pos = v.pos();
        s.normal = m_table.has_normal ? v.normal() : Vec3f(0, 0, 0);

        const Vec3f& dc = v.f_dc();
        s.f_dc[0] = dc.x;
        s.f_dc[1] = dc.y;
        s.f_dc[2] = dc.z;

        std::memset(s.f_rest, 0, sizeof(s.f_rest));
        for (int j = 0; j < copy_count; ++j) {
            s.f_rest[j] = v.f_rest(j);
        }

        s.opacity = v.opacity();
        s.scale = v.scale();

        const Quat& q = v.rot();
        s.rot[0] = q.w;
        s.rot[1] = q.x;
        s.rot[2] = q.y;
        s.rot[3] = q.z;
    }

    return result;
}

BBox SplatBuffer::compute_bbox() const {
    BBox bbox;
    for (uint32_t i = 0; i < m_table.num_rows; ++i) {
        bbox.expand(pos(i));
    }
    return bbox;
}

} // namespace ply2lcc
