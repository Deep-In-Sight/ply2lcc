#ifndef PATH_RESOLUTION_HPP
#define PATH_RESOLUTION_HPP

#include <string>
#include <optional>
#include <variant>

namespace ply2lcc {

struct ResolvedPath {
    std::string path;           // Full path to iteration directory
    int iteration_number;       // The iteration number (e.g., 30000)
};

struct PathError {
    std::string message;
};

class PathResult {
public:
    PathResult(ResolvedPath path) : result_(std::move(path)) {}
    PathResult(PathError error) : result_(std::move(error)) {}

    bool has_value() const { return std::holds_alternative<ResolvedPath>(result_); }
    const ResolvedPath& operator*() const { return std::get<ResolvedPath>(result_); }
    const ResolvedPath* operator->() const { return &std::get<ResolvedPath>(result_); }
    std::string error() const { return std::get<PathError>(result_).message; }

private:
    std::variant<ResolvedPath, PathError> result_;
};

// Resolves input_dir to the actual iteration directory
// Returns the path to highest iteration_<N> directory containing PLY files
// Returns error if structure is invalid
PathResult resolve_input_path(const std::string& input_dir);

// Creates output directory structure: output_dir/LCC_Results/
// Returns the full path to LCC_Results directory
std::string resolve_output_path(const std::string& output_dir);

}  // namespace ply2lcc

#endif  // PATH_RESOLUTION_HPP
