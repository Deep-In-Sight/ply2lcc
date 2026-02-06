#ifndef PLY2LCC_LCC_WRITER_HPP
#define PLY2LCC_LCC_WRITER_HPP

#include "lcc_types.hpp"
#include <string>

namespace ply2lcc {

class LccWriter {
public:
    explicit LccWriter(const std::string& output_dir);

    // Write complete LCC output (data.bin, shcoef.bin, index.bin, meta.lcc, attrs.lcp)
    void write(const LccData& data);

    // Write environment.bin separately
    void write_environment(const EncodedEnvironment& env, bool has_sh);

private:
    void write_data_bin(const LccData& data);
    void write_index_bin(const LccData& data);
    void write_meta_lcc(const LccData& data);
    void write_attrs_lcp();

    static std::string generate_guid();

    std::string output_dir_;
};

} // namespace ply2lcc

#endif // PLY2LCC_LCC_WRITER_HPP
