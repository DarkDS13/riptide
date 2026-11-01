// GPU window resolution for builds without Metal: no GPU is ever available, and the
// GPU strategies fail with a clear message instead of a link error.

#include <riptide/gpu/gpu_window_resolver.hpp>

#include <stdexcept>

namespace riptide {

struct GpuWindowResolver::Impl {};

GpuWindowResolver::GpuWindowResolver() {
    throw std::runtime_error("this build has no GPU support (Metal is required)");
}

GpuWindowResolver::~GpuWindowResolver() = default;

bool GpuWindowResolver::available() noexcept { return false; }

std::string GpuWindowResolver::device_name() const { return {}; }

GpuWindowResolver::Maps GpuWindowResolver::allocate_maps(std::size_t) const { return {}; }

WindowResolution GpuWindowResolver::resolve(const ByteWindow&, const Maps&, WindowStrategy) const {
    throw std::runtime_error("this build has no GPU support (Metal is required)");
}

}  // namespace riptide
