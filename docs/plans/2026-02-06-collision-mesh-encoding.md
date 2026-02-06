# Collision Mesh Encoding Design

## Overview

Implement collision mesh encoding based on the LCC Whitepaper specification. The encoder reads a PLY mesh file, partitions triangles by spatial grid cells, builds BVH acceleration structures, and outputs a `Collision.lci` binary file.

## Input/Output

**Input**: `collision.ply` or `collision.obj` - Mesh file with:

- PLY: `vertex` element (x, y, z float32), `face` element (vertex_indices)
- OBJ: `v x y z` vertices, `f v1 v2 v3` faces (supports v/vt/vn format)

**Output**: `Collision.lci` per whitepaper spec (version 2)

## Data Structures

```cpp
// Triangle face
struct Triangle {
    uint32_t v0, v1, v2;
};

// Per-cell collision mesh
struct CollisionCell {
    uint32_t index;       // (cell_y << 16) | cell_x
    std::vector<Vec3f> vertices;
    std::vector<Triangle> faces;
    std::vector<uint8_t> bvh_data;
};

// BVH node (32 bytes)
struct BVHNode {
    float bbox_min[3];
    float bbox_max[3];
    union {
        struct { uint32_t right; uint16_t split_axis; uint16_t flags; };      // Internal
        struct { uint32_t face_offset; uint16_t face_count; uint16_t leaf; }; // Leaf (leaf=0xFFFF)
    };
};

// Complete collision data
struct CollisionData {
    BBox bbox;
    float cell_size_x, cell_size_y;
    std::vector<CollisionCell> cells;
};
```

## File Format (Collision.lci)

### Header
| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic (0x6c6c6f63 = "coll") |
| 4 | 4 | version (2) |
| 8 | 4 | headerLen |
| 12 | 12 | min (x,y,z) |
| 24 | 12 | max (x,y,z) |
| 36 | 4 | cellLengthX |
| 40 | 4 | cellLengthY |
| 44 | 4 | meshNum |
| 48+ | 40×N | meshHeaders[] |

### meshHeader (40 bytes)
| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | indexX |
| 4 | 4 | indexY |
| 8 | 8 | offset |
| 16 | 8 | bytesSize |
| 24 | 4 | vertexNum |
| 28 | 4 | faceNum |
| 32 | 4 | bvhSize |
| 36 | 4 | reserved |

### Data (per mesh)
- vertices: float32[vertexNum × 3]
- faces: uint32[faceNum × 3]
- bvh: uint32[4] reserved + BVH nodes

## Components

### CollisionEncoder
- `read_collision_ply()`: Read vertices and faces from PLY
- `partition_by_cell()`: Assign triangles to cells by centroid, remap vertices
- `build_bvh()`: SAH-based BVH per cell
- `encode()`: Returns CollisionData

### LccWriter (extended)
- `write_collision()`: Output Collision.lci binary file (called from `write()`)

## New/Modified Files

- `src/collision_encoder.hpp` - New
- `src/collision_encoder.cpp` - New
- `src/lcc_types.hpp` - Added Triangle, BVHNode, CollisionCell, CollisionData
- `src/lcc_writer.cpp` - Added write_collision()

## Integration
Add to `ConvertApp::run()` after splat encoding, before completion.
