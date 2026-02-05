#ifndef PLY2LCC_SPLAT_BUFFER_HPP
#define PLY2LCC_SPLAT_BUFFER_HPP

#include "types.hpp"
#include "ply_reader_mmap.hpp"
#include <cstdint>
#include <string>
#include <memory>

namespace ply2lcc {

/// Property offset table for Gaussian splatting data
struct PropTable {
    uint32_t pos;
    uint32_t normal;
    uint32_t f_dc;
    uint32_t opacity;
    uint32_t scale;
    uint32_t rot;
    uint32_t f_rest;
    uint32_t row_stride;
    uint32_t num_rows;
    int num_f_rest;
    int sh_degree;
    bool has_normal;
};

/// Zero-copy view into a single splat
class SplatView {
public:
    SplatView(const uint8_t* row, const PropTable& table)
        : m_row(row), m_table(table) {}

    const Vec3f& pos() const {
        return Vec3f::from_ptr(reinterpret_cast<const float*>(m_row + m_table.pos));
    }

    const Vec3f& normal() const {
        return Vec3f::from_ptr(reinterpret_cast<const float*>(m_row + m_table.normal));
    }

    const Vec3f& f_dc() const {
        return Vec3f::from_ptr(reinterpret_cast<const float*>(m_row + m_table.f_dc));
    }

    const float& opacity() const {
        return *reinterpret_cast<const float*>(m_row + m_table.opacity);
    }

    const Vec3f& scale() const {
        return Vec3f::from_ptr(reinterpret_cast<const float*>(m_row + m_table.scale));
    }

    const Quat& rot() const {
        return Quat::from_ptr(reinterpret_cast<const float*>(m_row + m_table.rot));
    }

    const float& f_rest(int i) const {
        return *reinterpret_cast<const float*>(m_row + m_table.f_rest + i * sizeof(float));
    }

    int num_f_rest() const { return m_table.num_f_rest; }
    bool has_normal() const { return m_table.has_normal; }

private:
    const uint8_t* m_row;
    const PropTable& m_table;
};

/// Iterator over splats
class SplatIterator {
public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = SplatView;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = SplatView;

    SplatIterator(const uint8_t* row, const PropTable* table)
        : m_row(row), m_table(table) {}

    SplatView operator*() const { return SplatView(m_row, *m_table); }

    SplatIterator& operator++() {
        m_row += m_table->row_stride;
        return *this;
    }

    SplatIterator operator++(int) {
        SplatIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    SplatIterator& operator--() {
        m_row -= m_table->row_stride;
        return *this;
    }

    SplatIterator& operator+=(difference_type n) {
        m_row += n * m_table->row_stride;
        return *this;
    }

    SplatIterator operator+(difference_type n) const {
        return SplatIterator(m_row + n * m_table->row_stride, m_table);
    }

    difference_type operator-(const SplatIterator& other) const {
        return (m_row - other.m_row) / static_cast<difference_type>(m_table->row_stride);
    }

    bool operator==(const SplatIterator& other) const { return m_row == other.m_row; }
    bool operator!=(const SplatIterator& other) const { return m_row != other.m_row; }
    bool operator<(const SplatIterator& other) const { return m_row < other.m_row; }

private:
    const uint8_t* m_row;
    const PropTable* m_table;
};

/// Memory-mapped buffer over Gaussian splatting PLY data (zero-copy)
/// Owns a PLYReaderMmap and validates the data is valid splat format
class SplatBuffer {
public:
    SplatBuffer() = default;
    ~SplatBuffer() = default;

    // Non-copyable, movable
    SplatBuffer(const SplatBuffer&) = delete;
    SplatBuffer& operator=(const SplatBuffer&) = delete;
    SplatBuffer(SplatBuffer&&) = default;
    SplatBuffer& operator=(SplatBuffer&&) = default;

    /// Initialize from a PLY file. Returns false on error (check error()).
    bool initialize(const std::string& path);

    bool valid() const { return m_data != nullptr; }
    size_t size() const { return m_table.num_rows; }

    SplatView operator[](size_t i) const {
        return SplatView(m_data + i * m_table.row_stride, m_table);
    }

    SplatIterator begin() const { return SplatIterator(m_data, &m_table); }
    SplatIterator end() const {
        return SplatIterator(m_data + m_table.num_rows * m_table.row_stride, &m_table);
    }

    // Convenience accessor
    const Vec3f& pos(size_t i) const {
        return Vec3f::from_ptr(reinterpret_cast<const float*>(
            m_data + i * m_table.row_stride + m_table.pos));
    }

    // Metadata
    int sh_degree() const { return m_table.sh_degree; }
    int num_f_rest() const { return m_table.num_f_rest; }
    bool has_normal() const { return m_table.has_normal; }
    const PropTable& table() const { return m_table; }

    // Error message if construction failed
    const std::string& error() const { return m_error; }

    // Materialize to vector<Splat> for compatibility with legacy code
    std::vector<Splat> to_vector() const;

    // Compute bounding box
    BBox compute_bbox() const;

private:
    std::unique_ptr<PLYReaderMmap> m_reader;
    const uint8_t* m_data = nullptr;
    PropTable m_table{};
    std::string m_error;
};

} // namespace ply2lcc

#endif // PLY2LCC_SPLAT_BUFFER_HPP
