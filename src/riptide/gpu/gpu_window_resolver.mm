#include <riptide/gpu/gpu_window_resolver.hpp>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

#include <riptide/common/trace.hpp>

namespace riptide {
namespace {

// The kernels are compiled from source when the resolver is created, so the build
// needs no offline Metal compiler.
//
// Buffers hold one window (or map) per slot of kWindowSize entries. Block b covers
// maps [begins[b], begins[b + 1]) with begins[b] = n * b / B, as on the CPU; the
// host computes the boundaries, since integer division is slow on the GPU. Each
// step is a 2-D dispatch: x is the position in the window, y the block.
constexpr const char* kKernelSource = R"(
#include <metal_stdlib>
using namespace metal;

constant ushort kMarkerBase = 0x8000;
constant ulong kWindowSize = 32768;

struct Step {
    uint step;  // index of the map within each block
};

// Phase 1, one step: acc_out[b] = map[begin(b) + step] o acc_in[b]. On step 0 the
// accumulated map is the identity, so the map is taken as it is. Blocks that have
// no map at this step carry their result over to the other buffer.
kernel void compose_step(device const ushort* maps [[buffer(0)]],
                         device const ushort* acc_in [[buffer(1)]],
                         device ushort* acc_out [[buffer(2)]],
                         constant Step& s [[buffer(3)]],
                         constant uint* begins [[buffer(4)]],
                         uint2 gid [[thread_position_in_grid]]) {
    const ulong slot = ulong(gid.y) * kWindowSize;
    const uint k = begins[gid.y] + s.step;
    if (k >= begins[gid.y + 1]) {
        acc_out[slot + gid.x] = acc_in[slot + gid.x];
        return;
    }
    ushort symbol = maps[ulong(k) * kWindowSize + gid.x];
    if (symbol >= kMarkerBase && s.step > 0) {
        symbol = acc_in[slot + (symbol - kMarkerBase)];
    }
    acc_out[slot + gid.x] = symbol;
}

// Phase 2, one step: the window entering the next block, out = map(previous).
kernel void apply_map(device const ushort* map [[buffer(0)]],
                      device const uchar* previous [[buffer(1)]],
                      device uchar* out [[buffer(2)]],
                      uint i [[thread_position_in_grid]]) {
    const ushort symbol = map[i];
    out[i] = symbol >= kMarkerBase ? previous[symbol - kMarkerBase] : uchar(symbol);
}

// Phase 3, one step: window k + 1 = map[k](window k) for k = begin(b) + step. The
// window before a block's first map is the block's entering window.
kernel void resolve_step(device const ushort* maps [[buffer(0)]],
                         device const uchar* entering [[buffer(1)]],
                         device uchar* windows [[buffer(2)]],
                         constant Step& s [[buffer(3)]],
                         constant uint* begins [[buffer(4)]],
                         uint2 gid [[thread_position_in_grid]]) {
    const uint k = begins[gid.y] + s.step;
    if (k >= begins[gid.y + 1]) {
        return;
    }
    const ushort symbol = maps[ulong(k) * kWindowSize + gid.x];
    uchar value = uchar(symbol);
    if (symbol >= kMarkerBase) {
        device const uchar* previous = s.step == 0 ? entering + ulong(gid.y) * kWindowSize
                                                   : windows + ulong(k) * kWindowSize;
        value = previous[symbol - kMarkerBase];
    }
    windows[ulong(k + 1) * kWindowSize + gid.x] = value;
}
)";

struct Step {
    std::uint32_t step;
};

/// Keeps a Metal buffer alive for as long as a shared_ptr refers to it.
struct BufferHolder {
    id<MTLBuffer> buffer;
};

/// Host time in seconds, the clock of Metal's command-buffer timestamps.
double host_seconds() {
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info;
        mach_timebase_info(&info);
        return info;
    }();
    return static_cast<double>(mach_absolute_time()) * timebase.numer / timebase.denom * 1e-9;
}

void check(id<MTLCommandBuffer> buffer) {
    if (buffer.status == MTLCommandBufferStatusError) {
        throw std::runtime_error(std::string("GPU window resolution failed: ") +
                                 buffer.error.localizedDescription.UTF8String);
    }
}

}  // namespace

struct GpuWindowResolver::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> compose;
    id<MTLComputePipelineState> carry;
    id<MTLComputePipelineState> resolve;

    // Buffers are kept between calls and reused when large enough, as malloc
    // reuses freed memory for the CPU strategies. A fresh buffer costs a page fault
    // per page on first use and has to be mapped for the GPU; for the 64 KiB-chunk
    // runs that is a third of a gigabyte of maps on every call. A cached buffer that
    // a caller still holds (a previous result's windows) is not reused.
    std::shared_ptr<BufferHolder> maps_cache;
    std::shared_ptr<BufferHolder> windows_cache;
    std::shared_ptr<BufferHolder> scratch_cache[4];  // two block-map buffers, entering windows, block starts

    std::shared_ptr<BufferHolder> reuse(std::shared_ptr<BufferHolder>& cache, std::size_t bytes) const {
        if (!cache || cache.use_count() > 1 || cache->buffer.length < bytes) {
            cache.reset();  // release the old buffer before allocating its replacement
            auto created = std::make_shared<BufferHolder>();
            created->buffer = buffer(bytes);
            cache = std::move(created);
        }
        return cache;
    }

    id<MTLBuffer> buffer(std::size_t bytes) const {
        id<MTLBuffer> created = [device newBufferWithLength:std::max<std::size_t>(bytes, 1)
                                                    options:MTLResourceStorageModeShared];
        if (created == nil) {
            throw std::bad_alloc();
        }
        return created;
    }

    id<MTLComputePipelineState> pipeline(id<MTLLibrary> library, NSString* name) const {
        NSError* error = nil;
        id<MTLFunction> function = [library newFunctionWithName:name];
        id<MTLComputePipelineState> state =
            function == nil ? nil : [device newComputePipelineStateWithFunction:function error:&error];
        if (state == nil) {
            throw std::runtime_error("cannot create GPU kernel " + std::string(name.UTF8String));
        }
        return state;
    }
};

GpuWindowResolver::GpuWindowResolver() : impl_(std::make_unique<Impl>()) {
    @autoreleasepool {
        impl_->device = MTLCreateSystemDefaultDevice();
        if (impl_->device == nil) {
            throw std::runtime_error("no Metal GPU available");
        }
        impl_->queue = [impl_->device newCommandQueue];
        NSError* error = nil;
        id<MTLLibrary> library =
            [impl_->device newLibraryWithSource:[NSString stringWithUTF8String:kKernelSource]
                                        options:[[MTLCompileOptions alloc] init]
                                          error:&error];
        if (library == nil) {
            throw std::runtime_error(std::string("cannot compile GPU kernels: ") +
                                     error.localizedDescription.UTF8String);
        }
        impl_->compose = impl_->pipeline(library, @"compose_step");
        impl_->carry = impl_->pipeline(library, @"apply_map");
        impl_->resolve = impl_->pipeline(library, @"resolve_step");
    }
}

GpuWindowResolver::~GpuWindowResolver() = default;

bool GpuWindowResolver::available() noexcept {
    @autoreleasepool {
        return MTLCreateSystemDefaultDevice() != nil;
    }
}

GpuWindowResolver::Maps GpuWindowResolver::allocate_maps(std::size_t count) const {
    auto holder = impl_->reuse(impl_->maps_cache, count * kWindowSize * sizeof(std::uint16_t));
    Maps maps;
    maps.symbols_ = static_cast<std::uint16_t*>(holder->buffer.contents);
    maps.count_ = count;
    maps.buffer_ = std::move(holder);
    return maps;
}

WindowResolution GpuWindowResolver::resolve(const ByteWindow& first, const Maps& maps,
                                            WindowStrategy strategy) const {
    if (!is_gpu(strategy)) {
        throw std::invalid_argument("not a GPU window strategy");
    }
    const std::size_t n = maps.size();
    const std::size_t blocks = gpu_block_count(strategy, n);
    auto begin = [&](std::size_t b) { return n * b / blocks; };

    WindowResolution result;
    std::optional<trace::Scope> setup(std::in_place, "GPU setup", "window");
    const auto windows = impl_->reuse(impl_->windows_cache, (n + 1) * kWindowSize);
    auto* window_data = static_cast<ByteWindow*>(windows->buffer.contents);
    window_data[0] = first;
    result.windows = std::span<const ByteWindow>(window_data, n + 1);
    result.storage = windows;
    if (n == 0) {
        return result;
    }

    // Steps of each phase: the longest block decides. The last block's own map is
    // never needed, so phase 1 composes blocks 0 .. B-2 only.
    std::size_t compose_steps = 0;
    std::size_t composed = 0;
    for (std::size_t b = 0; b + 1 < blocks; ++b) {
        compose_steps = std::max(compose_steps, begin(b + 1) - begin(b));
        composed += begin(b + 1) - begin(b);
    }
    std::size_t resolve_steps = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
        resolve_steps = std::max(resolve_steps, begin(b + 1) - begin(b));
    }

    @autoreleasepool {
        id<MTLBuffer> map_buffer = static_cast<BufferHolder*>(maps.buffer_.get())->buffer;
        auto& scratch = impl_->scratch_cache;
        const std::size_t block_map_bytes = (blocks - 1) * kWindowSize * sizeof(std::uint16_t);
        id<MTLBuffer> accumulated[2] = {impl_->reuse(scratch[0], block_map_bytes)->buffer,
                                        impl_->reuse(scratch[1], block_map_bytes)->buffer};
        id<MTLBuffer> entering = impl_->reuse(scratch[2], blocks * kWindowSize)->buffer;
        std::memcpy(entering.contents, first.data(), kWindowSize);

        std::vector<std::uint32_t> begins(blocks + 1);
        for (std::size_t b = 0; b <= blocks; ++b) {
            begins[b] = static_cast<std::uint32_t>(begin(b));
        }
        id<MTLBuffer> begin_buffer = impl_->reuse(scratch[3], begins.size() * sizeof(std::uint32_t))->buffer;
        std::memcpy(begin_buffer.contents, begins.data(), begins.size() * sizeof(std::uint32_t));

        const MTLSize group = MTLSizeMake(256, 1, 1);
        id<MTLCommandBuffer> phases[3] = {nil, nil, nil};

        // Phase 1: every block but the last composes its maps into one block map.
        if (blocks > 1) {
            phases[0] = [impl_->queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [phases[0] computeCommandEncoder];
            [encoder setComputePipelineState:impl_->compose];
            [encoder setBuffer:map_buffer offset:0 atIndex:0];
            [encoder setBuffer:begin_buffer offset:0 atIndex:4];
            for (std::size_t t = 0; t < compose_steps; ++t) {
                const Step step{static_cast<std::uint32_t>(t)};
                [encoder setBuffer:accumulated[(t + 1) % 2] offset:0 atIndex:1];
                [encoder setBuffer:accumulated[t % 2] offset:0 atIndex:2];
                [encoder setBytes:&step length:sizeof(step) atIndex:3];
                [encoder dispatchThreads:MTLSizeMake(kWindowSize, blocks - 1, 1) threadsPerThreadgroup:group];
            }
            [encoder endEncoding];

            // Phase 2: the window entering block b is block b-1's map applied to the
            // window entering block b-1, one block after another.
            phases[1] = [impl_->queue commandBuffer];
            encoder = [phases[1] computeCommandEncoder];
            [encoder setComputePipelineState:impl_->carry];
            id<MTLBuffer> block_maps = accumulated[(compose_steps - 1) % 2];
            for (std::size_t b = 1; b < blocks; ++b) {
                [encoder setBuffer:block_maps offset:(b - 1) * kWindowSize * sizeof(std::uint16_t) atIndex:0];
                [encoder setBuffer:entering offset:(b - 1) * kWindowSize atIndex:1];
                [encoder setBuffer:entering offset:b * kWindowSize atIndex:2];
                [encoder dispatchThreads:MTLSizeMake(kWindowSize, 1, 1) threadsPerThreadgroup:group];
            }
            [encoder endEncoding];
        }

        // Phase 3: every block resolves its own windows, all blocks at once.
        phases[2] = [impl_->queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [phases[2] computeCommandEncoder];
        [encoder setComputePipelineState:impl_->resolve];
        [encoder setBuffer:map_buffer offset:0 atIndex:0];
        [encoder setBuffer:entering offset:0 atIndex:1];
        [encoder setBuffer:windows->buffer offset:0 atIndex:2];
        [encoder setBuffer:begin_buffer offset:0 atIndex:4];
        for (std::size_t t = 0; t < resolve_steps; ++t) {
            const Step step{static_cast<std::uint32_t>(t)};
            [encoder setBytes:&step length:sizeof(step) atIndex:3];
            [encoder dispatchThreads:MTLSizeMake(kWindowSize, blocks, 1) threadsPerThreadgroup:group];
        }
        [encoder endEncoding];

        // Command buffers on one queue execute in order.
        setup.reset();
        const trace::Scope wait("wait for GPU", "wait");
        for (id<MTLCommandBuffer> phase : phases) {
            [phase commit];
        }
        [phases[2] waitUntilCompleted];
        for (id<MTLCommandBuffer> phase : phases) {
            if (phase != nil) {
                check(phase);
            }
        }

        const double gpu_begin = (phases[0] != nil ? phases[0] : phases[2]).GPUStartTime;
        result.device_seconds = phases[2].GPUEndTime - gpu_begin;

        if (trace::active()) {
            // Metal timestamps are host seconds; shift them onto the trace clock.
            const double offset = static_cast<double>(trace::now_ns()) - host_seconds() * 1e9;
            auto record = [&](id<MTLCommandBuffer> phase, const char* name, std::size_t steps) {
                if (phase != nil) {
                    trace::interval("GPU", name, "gpu",
                                    static_cast<std::uint64_t>(phase.GPUStartTime * 1e9 + offset),
                                    static_cast<std::uint64_t>(phase.GPUEndTime * 1e9 + offset),
                                    {"dispatches", static_cast<std::int64_t>(steps)},
                                    {"blocks", static_cast<std::int64_t>(blocks)});
                }
            };
            record(phases[0], "compose blocks", compose_steps);
            record(phases[1], "carry between blocks", blocks - 1);
            record(phases[2], blocks > 1 ? "resolve blocks" : "serial chain", resolve_steps);
        }
    }

    result.cost.rounds = compose_steps + (blocks - 1) + resolve_steps;
    result.cost.combinations = composed + (blocks - 1) + n;
    return result;
}

}  // namespace riptide
