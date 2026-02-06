#include "collision_encoder.hpp"
#include "miniply/miniply.h"
#include <algorithm>
#include <map>
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cctype>

namespace ply2lcc {

void CollisionEncoder::log(const std::string& msg) {
    if (log_cb_) {
        log_cb_(msg);
    } else {
        std::cout << msg;
    }
}

bool CollisionEncoder::read_mesh(const std::string& path,
                                  std::vector<Vec3f>& vertices,
                                  std::vector<Triangle>& faces) {
    // Detect format by extension
    std::string ext;
    size_t dot_pos = path.rfind('.');
    if (dot_pos != std::string::npos) {
        ext = path.substr(dot_pos);
        // Convert to lowercase
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    if (ext == ".obj") {
        return read_obj(path, vertices, faces);
    } else if (ext == ".ply") {
        return read_ply(path, vertices, faces);
    } else {
        log("Unknown mesh format: " + ext + " (supported: .ply, .obj)\n");
        return false;
    }
}

bool CollisionEncoder::read_obj(const std::string& path,
                                 std::vector<Vec3f>& vertices,
                                 std::vector<Triangle>& faces) {
    std::ifstream file(path);
    if (!file) {
        log("Failed to open OBJ file: " + path + "\n");
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            // Vertex: v x y z
            Vec3f v;
            if (iss >> v.x >> v.y >> v.z) {
                vertices.push_back(v);
            }
        } else if (prefix == "f") {
            // Face: f v1 v2 v3 [v4] or f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            std::vector<uint32_t> indices;
            std::string token;
            while (iss >> token) {
                // Parse vertex index (handle v, v/vt, v/vt/vn, v//vn formats)
                size_t slash_pos = token.find('/');
                std::string idx_str = (slash_pos == std::string::npos)
                    ? token : token.substr(0, slash_pos);
                int idx = std::stoi(idx_str);
                // OBJ uses 1-based indices, convert to 0-based
                // Negative indices are relative to current vertex count
                if (idx < 0) {
                    idx = static_cast<int>(vertices.size()) + idx;
                } else {
                    idx -= 1;
                }
                indices.push_back(static_cast<uint32_t>(idx));
            }

            // Triangulate if needed (fan triangulation for convex polygons)
            for (size_t i = 2; i < indices.size(); ++i) {
                Triangle tri;
                tri.v0 = indices[0];
                tri.v1 = indices[i - 1];
                tri.v2 = indices[i];
                faces.push_back(tri);
            }
        }
        // Ignore other lines (vt, vn, mtllib, usemtl, etc.)
    }

    log("  Read " + std::to_string(vertices.size()) + " vertices, " +
        std::to_string(faces.size()) + " triangles\n");

    return !vertices.empty() && !faces.empty();
}

bool CollisionEncoder::read_ply(const std::string& path,
                                 std::vector<Vec3f>& vertices,
                                 std::vector<Triangle>& faces) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
        log("Failed to open PLY file: " + path + "\n");
        return false;
    }

    // Find and load vertex element
    uint32_t vertex_idx = reader.find_element(miniply::kPLYVertexElement);
    if (vertex_idx == miniply::kInvalidIndex) {
        log("No vertex element in PLY file\n");
        return false;
    }

    // Find and load face element
    uint32_t face_idx = reader.find_element(miniply::kPLYFaceElement);
    if (face_idx == miniply::kInvalidIndex) {
        log("No face element in PLY file\n");
        return false;
    }

    // Read vertices
    while (reader.has_element() && !reader.element_is(miniply::kPLYVertexElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        log("Could not find vertex element\n");
        return false;
    }

    uint32_t pos_idx[3];
    if (!reader.find_pos(pos_idx)) {
        log("Vertices missing position properties\n");
        return false;
    }

    uint32_t num_vertices = reader.num_rows();
    vertices.resize(num_vertices);

    if (!reader.load_element()) {
        log("Failed to load vertex element\n");
        return false;
    }

    reader.extract_properties(pos_idx, 3, miniply::PLYPropertyType::Float,
                              vertices.data());
    reader.next_element();

    // Read faces
    while (reader.has_element() && !reader.element_is(miniply::kPLYFaceElement)) {
        reader.next_element();
    }

    if (!reader.has_element()) {
        log("Could not find face element\n");
        return false;
    }

    uint32_t indices_idx[1];
    if (!reader.find_indices(indices_idx)) {
        log("Faces missing vertex_indices property\n");
        return false;
    }

    if (!reader.load_element()) {
        log("Failed to load face element\n");
        return false;
    }

    // Check if triangulation is needed
    uint32_t num_triangles = reader.num_triangles(indices_idx[0]);
    faces.resize(num_triangles);

    bool needs_triangulation = reader.requires_triangulation(indices_idx[0]);
    if (needs_triangulation) {
        // Triangulate polygons
        reader.extract_triangles(indices_idx[0],
                                  reinterpret_cast<const float*>(vertices.data()),
                                  num_vertices,
                                  miniply::PLYPropertyType::UInt,
                                  faces.data());
    } else {
        // Directly extract triangles
        reader.extract_list_property(indices_idx[0],
                                      miniply::PLYPropertyType::UInt,
                                      faces.data());
    }

    log("  Read " + std::to_string(vertices.size()) + " vertices, " +
        std::to_string(faces.size()) + " triangles\n");

    return true;
}

void CollisionEncoder::partition_by_cell(const std::vector<Vec3f>& vertices,
                                          const std::vector<Triangle>& faces,
                                          float cell_size_x, float cell_size_y,
                                          std::vector<CollisionCell>& cells,
                                          BBox& bbox) {
    // Compute bounding box
    bbox = BBox();
    for (const auto& v : vertices) {
        bbox.expand(v);
    }

    // Map from cell index to cell data
    std::map<uint32_t, CollisionCell> cell_map;

    // Assign triangles to cells based on centroid
    for (const auto& tri : faces) {
        const Vec3f& v0 = vertices[tri.v0];
        const Vec3f& v1 = vertices[tri.v1];
        const Vec3f& v2 = vertices[tri.v2];

        // Compute centroid
        float cx = (v0.x + v1.x + v2.x) / 3.0f;
        float cy = (v0.y + v1.y + v2.y) / 3.0f;

        // Compute cell coordinates
        int cell_x = static_cast<int>((cx - bbox.min.x) / cell_size_x);
        int cell_y = static_cast<int>((cy - bbox.min.y) / cell_size_y);
        if (cell_x < 0) cell_x = 0;
        if (cell_y < 0) cell_y = 0;

        uint32_t cell_idx = (static_cast<uint32_t>(cell_y) << 16) | static_cast<uint32_t>(cell_x);

        // Get or create cell
        auto& cell = cell_map[cell_idx];
        if (cell.vertices.empty()) {
            cell.index = cell_idx;
        }

        // Map global vertex indices to local indices
        auto get_local_idx = [&cell, &vertices](uint32_t global_idx) -> uint32_t {
            const Vec3f& v = vertices[global_idx];
            // Linear search for existing vertex (could use map for large meshes)
            for (size_t i = 0; i < cell.vertices.size(); ++i) {
                const Vec3f& cv = cell.vertices[i];
                if (cv.x == v.x && cv.y == v.y && cv.z == v.z) {
                    return static_cast<uint32_t>(i);
                }
            }
            // Add new vertex
            uint32_t idx = static_cast<uint32_t>(cell.vertices.size());
            cell.vertices.push_back(v);
            return idx;
        };

        Triangle local_tri;
        local_tri.v0 = get_local_idx(tri.v0);
        local_tri.v1 = get_local_idx(tri.v1);
        local_tri.v2 = get_local_idx(tri.v2);
        cell.faces.push_back(local_tri);
    }

    // Convert map to vector and sort by index
    cells.clear();
    cells.reserve(cell_map.size());
    for (auto& [idx, cell] : cell_map) {
        cells.push_back(std::move(cell));
    }
    std::sort(cells.begin(), cells.end(),
              [](const CollisionCell& a, const CollisionCell& b) {
                  return a.index < b.index;
              });

    log("  Partitioned into " + std::to_string(cells.size()) + " cells\n");
}

// Helper for BVH construction
namespace {

struct BVHBuildEntry {
    uint32_t start;
    uint32_t count;
    uint32_t parent_idx;
    bool is_right_child;
};

void compute_triangle_bbox(const std::vector<Vec3f>& verts,
                            const Triangle& tri,
                            float* bmin, float* bmax) {
    const Vec3f& v0 = verts[tri.v0];
    const Vec3f& v1 = verts[tri.v1];
    const Vec3f& v2 = verts[tri.v2];

    bmin[0] = std::min({v0.x, v1.x, v2.x});
    bmin[1] = std::min({v0.y, v1.y, v2.y});
    bmin[2] = std::min({v0.z, v1.z, v2.z});
    bmax[0] = std::max({v0.x, v1.x, v2.x});
    bmax[1] = std::max({v0.y, v1.y, v2.y});
    bmax[2] = std::max({v0.z, v1.z, v2.z});
}

float compute_centroid(const std::vector<Vec3f>& verts, const Triangle& tri, int axis) {
    const Vec3f& v0 = verts[tri.v0];
    const Vec3f& v1 = verts[tri.v1];
    const Vec3f& v2 = verts[tri.v2];
    return (v0[axis] + v1[axis] + v2[axis]) / 3.0f;
}

} // anonymous namespace

void CollisionEncoder::build_bvh(CollisionCell& cell) {
    if (cell.faces.empty()) {
        // Empty cell - minimal BVH data (just the 16-byte reserved header)
        cell.bvh_data.resize(16, 0);
        return;
    }

    std::vector<BVHNode> nodes;
    std::vector<uint32_t> face_order;  // Reordered face indices

    // Working copy of face indices for partitioning
    std::vector<uint32_t> indices(cell.faces.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<uint32_t>(i);
    }

    constexpr uint32_t MAX_LEAF_SIZE = 4;

    // Build stack
    std::vector<BVHBuildEntry> stack;
    stack.push_back({0, static_cast<uint32_t>(indices.size()), UINT32_MAX, false});

    while (!stack.empty()) {
        BVHBuildEntry entry = stack.back();
        stack.pop_back();

        // Compute bounds for this node
        float bmin[3] = {1e30f, 1e30f, 1e30f};
        float bmax[3] = {-1e30f, -1e30f, -1e30f};

        for (uint32_t i = entry.start; i < entry.start + entry.count; ++i) {
            float tmin[3], tmax[3];
            compute_triangle_bbox(cell.vertices, cell.faces[indices[i]], tmin, tmax);
            for (int a = 0; a < 3; ++a) {
                bmin[a] = std::min(bmin[a], tmin[a]);
                bmax[a] = std::max(bmax[a], tmax[a]);
            }
        }

        uint32_t node_idx = static_cast<uint32_t>(nodes.size());

        // Update parent's right child pointer if needed
        if (entry.parent_idx != UINT32_MAX && entry.is_right_child) {
            nodes[entry.parent_idx].data0 = node_idx;
        }

        if (entry.count <= MAX_LEAF_SIZE) {
            // Create leaf node
            uint32_t face_offset = static_cast<uint32_t>(face_order.size());
            for (uint32_t i = entry.start; i < entry.start + entry.count; ++i) {
                face_order.push_back(indices[i]);
            }
            nodes.push_back(BVHNode::make_leaf(bmin, bmax, face_offset,
                                                static_cast<uint16_t>(entry.count)));
        } else {
            // Find best split axis (longest extent)
            int axis = 0;
            float max_extent = bmax[0] - bmin[0];
            for (int a = 1; a < 3; ++a) {
                float extent = bmax[a] - bmin[a];
                if (extent > max_extent) {
                    max_extent = extent;
                    axis = a;
                }
            }

            // Sort by centroid along split axis
            std::sort(indices.begin() + entry.start,
                      indices.begin() + entry.start + entry.count,
                      [&](uint32_t a, uint32_t b) {
                          return compute_centroid(cell.vertices, cell.faces[a], axis) <
                                 compute_centroid(cell.vertices, cell.faces[b], axis);
                      });

            // Split at median
            uint32_t mid = entry.count / 2;

            // Create internal node (right child will be filled later)
            nodes.push_back(BVHNode::make_internal(bmin, bmax, 0, static_cast<uint16_t>(axis)));

            // Push right child first (processed later), then left
            stack.push_back({entry.start + mid, entry.count - mid, node_idx, true});
            stack.push_back({entry.start, mid, node_idx, false});
        }
    }

    // Reorder faces according to BVH leaf order
    std::vector<Triangle> reordered_faces(cell.faces.size());
    for (size_t i = 0; i < face_order.size(); ++i) {
        reordered_faces[i] = cell.faces[face_order[i]];
    }
    cell.faces = std::move(reordered_faces);

    // Serialize BVH data: 16-byte reserved header + nodes
    size_t bvh_size = 16 + nodes.size() * sizeof(BVHNode);
    cell.bvh_data.resize(bvh_size);

    // Clear reserved header
    std::memset(cell.bvh_data.data(), 0, 16);

    // Copy nodes
    std::memcpy(cell.bvh_data.data() + 16, nodes.data(), nodes.size() * sizeof(BVHNode));
}

CollisionData CollisionEncoder::encode(const std::string& mesh_path,
                                        float cell_size_x, float cell_size_y) {
    CollisionData data;
    data.cell_size_x = cell_size_x;
    data.cell_size_y = cell_size_y;

    log("Reading collision mesh: " + mesh_path + "\n");

    std::vector<Vec3f> vertices;
    std::vector<Triangle> faces;

    if (!read_mesh(mesh_path, vertices, faces)) {
        return data;
    }

    log("Partitioning mesh...\n");
    partition_by_cell(vertices, faces, cell_size_x, cell_size_y, data.cells, data.bbox);

    log("Building BVH for each cell...\n");
    for (auto& cell : data.cells) {
        build_bvh(cell);
    }

    log("Collision encoding complete: " + std::to_string(data.total_triangles()) +
        " triangles in " + std::to_string(data.cells.size()) + " cells\n");

    return data;
}

} // namespace ply2lcc
