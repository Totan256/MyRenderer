#include "core/Application.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/Device.hpp"
#include "rhi/ModelBuilder.hpp"
#include "utils/bvh/BVHManager.hpp"
#include "utils/bvh/CpuBVHBuilder.hpp"
#include "utils/ModelImporter.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
// Paths are relative to the renderer repository root (the process working directory).
constexpr auto kMesh = "assets/teapot.obj";
constexpr auto kExport = "assets/renderer_export";
constexpr auto kBvhShader = "apps/lsnif_compare/bvh.comp";
constexpr auto kLsnifShader = "apps/lsnif_compare/lsnif.comp";

// These are deliberately app-local controls; both methods use the same object transform.
constexpr glm::vec3 kObjectPosition(0.0f);
constexpr float kObjectScale = 1.0f;
constexpr float kObjectYawDegrees = 0.0f;
constexpr glm::vec3 kCamera(0.0f, 4.0f, 8.0f);
constexpr glm::vec3 kTarget(0.0f, 0.5f, 0.0f);
constexpr float kFovDegrees = 45.0f;

std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open " + path.string());
    auto end = in.tellg();
    if (end < 0) throw std::runtime_error("Cannot size " + path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read " + path.string());
    return bytes;
}

// Minimal SHA-256: the export contains a hash for every file, including reference files.
std::string sha256(const std::vector<uint8_t>& input) {
    constexpr uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> b = input;
    uint64_t bitCount = uint64_t(input.size()) * 8;
    b.push_back(0x80);
    while (b.size() % 64 != 56) b.push_back(0);
    for (int i = 7; i >= 0; --i) b.push_back(uint8_t(bitCount >> (i * 8)));
    for (size_t block = 0; block < b.size(); block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            size_t p = block + i * 4;
            w[i] = uint32_t(b[p]) << 24 | uint32_t(b[p+1]) << 16 | uint32_t(b[p+2]) << 8 | b[p+3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = std::rotr(w[i-15],7) ^ std::rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = std::rotr(w[i-2],17) ^ std::rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], q=h[7], z=h[1];
        for (int i=0; i<64; ++i) {
            uint32_t s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=q+s1+ch+K[i]+w[i];
            uint32_t s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            uint32_t maj=(a&z)^(a&c)^(z&c);
            uint32_t t2=s0+maj;
            q=g;g=f;f=e;e=d+t1;d=c;c=z;z=a;a=t1+t2;
        }
        const uint32_t v[8]={a,z,c,d,e,f,g,q};
        for (int i=0; i<8; ++i) h[i]+=v[i];
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto v:h) out << std::setw(8) << v;
    return out.str();
}

// Small JSON reader for the export manifest; it intentionally has no external dependency.
struct Json {
    std::string value;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json& at(const std::string& key) const { return object.at(key); }
    const Json& at(size_t i) const { return array.at(i); }
    std::string str() const { return value; }
    double number() const { return std::stod(value); }
    uint32_t u32() const {
        double n = number();
        if (!std::isfinite(n) || n < 0 || n > UINT32_MAX || std::floor(n) != n)
            throw std::runtime_error("Invalid uint32 in manifest: " + value);
        return static_cast<uint32_t>(n);
    }
    size_t size() const { return array.size(); }
};
class JsonParser {
    const std::string source;
    size_t pos = 0;
    void ws() { while (pos < source.size() && (source[pos]==' ' || source[pos]=='\n' || source[pos]=='\r' || source[pos]=='\t')) ++pos; }
    char peek() { ws(); if (pos == source.size()) throw std::runtime_error("Incomplete JSON"); return source[pos]; }
    void eat(char c) { if (peek() != c) throw std::runtime_error("Invalid JSON delimiter"); ++pos; }
    std::string quoted() {
        eat('"'); std::string s;
        while (pos < source.size()) {
            char c = source[pos++];
            if (c == '"') return s;
            if (c == '\\') {
                if (pos == source.size()) throw std::runtime_error("Invalid JSON escape");
                c = source[pos++];
                if (c == 'u') { // Key names used by this manifest are ASCII; preserve escaped Unicode as placeholder.
                    if (pos + 4 > source.size()) throw std::runtime_error("Invalid Unicode escape");
                    pos += 4; s += '?'; continue;
                }
                if (c == 'n') c='\n'; else if (c == 'r') c='\r'; else if (c == 't') c='\t';
            }
            s += c;
        }
        throw std::runtime_error("Unterminated JSON string");
    }
    Json parse() {
        Json j;
        char c=peek();
        if (c=='"') { j.value=quoted(); return j; }
        if (c=='[') {
            eat('['); if (peek()==']') { ++pos; return j; }
            do { j.array.push_back(parse()); c=peek(); if (c==']') { ++pos; return j; } eat(','); } while (true);
        }
        if (c=='{') {
            eat('{'); if (peek()=='}') { ++pos; return j; }
            do { std::string key=quoted(); eat(':'); j.object.emplace(std::move(key),parse()); c=peek(); if (c=='}') { ++pos; return j; } eat(','); } while (true);
        }
        size_t start=pos;
        while (pos < source.size() && source[pos]!=',' && source[pos]!=']' && source[pos]!='}' && source[pos]!=' ' && source[pos]!='\n' && source[pos]!='\r') ++pos;
        if (start==pos) throw std::runtime_error("Invalid JSON value");
        j.value=source.substr(start,pos-start);
        return j;
    }
public:
    explicit JsonParser(std::vector<uint8_t> bytes):source(bytes.begin(),bytes.end()){}
    Json root() { Json j=parse(); ws(); if (pos!=source.size()) throw std::runtime_error("Trailing JSON"); return j; }
};

struct alignas(16) Scene {
    glm::vec4 camPos, camTarget, resolutionFov, boxMin, boxMax, objectTransform;
    glm::vec4 eps, infer; // parallel/tie/merge/boundary, slope/threshold/tMin/tMax
    glm::uvec4 dims, levels, widths, offsets0, offsets1;
};
static_assert(sizeof(Scene) % 16 == 0);

struct Export {
    Scene scene{};
    std::vector<uint8_t> weights, occupancy;
};
Export loadExport(const fs::path& directory) {
    static_assert(std::endian::native == std::endian::little, "Raw renderer export is little-endian");
    Json manifest = JsonParser(readBytes(directory / "manifest.json")).root();
    if (manifest.at("format").str() != "lsnif.renderer.phase06.v1" ||
        manifest.at("coordinate_space").str() != "object_local")
        throw std::runtime_error("Unsupported LSNIF renderer export");
    for (const auto& [name, record] : manifest.at("files").object) {
        fs::path relative(name);
        if (relative.is_absolute() || name.find("..") != std::string::npos)
            throw std::runtime_error("Invalid export file path: " + name);
        auto data=readBytes(directory / relative);
        if (data.size() != static_cast<size_t>(record.at("size_bytes").number()) ||
            sha256(data) != record.at("sha256").str())
            throw std::runtime_error("Size/SHA-256 mismatch: " + name);
    }
    Export out;
    out.weights=readBytes(directory / "weights.bin");
    out.occupancy=readBytes(directory / "occupancy.bin");
    const auto& arch=manifest.at("architecture");
    const auto& pg=manifest.at("point_generation");
    const auto& occ=manifest.at("occupancy");
    uint32_t V=occ.at("resolution").u32();
    uint32_t points=pg.at("max_points").u32();
    uint32_t hashSize=arch.at("hash_map_size").u32();
    uint32_t feature=arch.at("feature_dimension").u32();
    if (V==0 || V>256 || points!=18 || feature!=3 || arch.at("level_resolutions").size()!=2 ||
        hashSize==0 || (hashSize & (hashSize-1)) || arch.at("hash_primes").size()!=3 ||
        kObjectScale <= 0.0f || out.occupancy.size()!=size_t(occ.at("word_count").u32())*4 ||
        size_t(occ.at("word_count").u32())!=(size_t(V)*V*V+31)/32)
        throw std::runtime_error("Unexpected occupancy/grid architecture");
    out.scene.dims={V,points,hashSize,feature};
    out.scene.levels={arch.at("level_resolutions").at(0).u32(),arch.at("level_resolutions").at(1).u32(),
                      arch.at("hash_primes").at(0).u32(),arch.at("hash_primes").at(1).u32()};
    out.scene.widths.x=arch.at("hash_primes").at(2).u32();
    out.scene.widths.w=8+arch.at("material_count").u32();
    out.scene.eps={float(pg.at("parallel_epsilon").number()),float(pg.at("tie_epsilon").number()),
                   float(pg.at("merge_epsilon").number()),float(arch.at("boundary_epsilon").number())};
    out.scene.infer={float(arch.at("negative_slope").number()),
                     float(manifest.at("postprocess").at("occlusion_threshold").number()),0.0f,10000.0f};
    out.scene.boxMin={float(occ.at("aabb_min").at(0).number()),float(occ.at("aabb_min").at(1).number()),float(occ.at("aabb_min").at(2).number()),0};
    out.scene.boxMax={float(occ.at("aabb_max").at(0).number()),float(occ.at("aabb_max").at(1).number()),float(occ.at("aabb_max").at(2).number()),0};
    for (int axis=0;axis<3;++axis)
        if (!(out.scene.boxMin[axis]<out.scene.boxMax[axis])) throw std::runtime_error("Invalid AABB");
    const std::array<std::string,8> names={"encoder.tables.0","encoder.tables.1","network.0.weight","network.0.bias",
        "network.2.weight","network.2.bias","network.4.weight","network.4.bias"};
    const auto& arrays=manifest.at("arrays").array;
    if (arrays.size()!=names.size()) throw std::runtime_error("Unexpected weight array count");
    std::array<uint32_t,8> offsets{};
    auto checkShape=[&](const Json& a, std::initializer_list<uint32_t> expected) {
        if (a.at("shape").size()!=expected.size()) throw std::runtime_error("Unexpected array rank");
        size_t i=0;
        for (uint32_t dim:expected) if (a.at("shape").at(i++).u32()!=dim) throw std::runtime_error("Unexpected weight shape: "+a.at("name").str());
    };
    for (size_t i=0;i<arrays.size();++i) {
        const Json& a=arrays[i];
        uint32_t offset=a.at("offset_bytes").u32(), size=a.at("size_bytes").u32();
        if (a.at("name").str()!=names[i] || a.at("dtype").str()!="float32_le" ||
            offset%4 || size%4 || size_t(offset)+size>out.weights.size())
            throw std::runtime_error("Invalid weight array: "+names[i]);
        size_t product=1;
        for (const auto& d:a.at("shape").array) product*=d.u32();
        if (product*4!=size) throw std::runtime_error("Invalid weight shape/size");
        offsets[i]=offset/4;
    }
    uint32_t hidden0=arrays[3].at("shape").at(0).u32();
    uint32_t hidden1=arrays[5].at("shape").at(0).u32();
    out.scene.widths.y=hidden0; out.scene.widths.z=hidden1;
    if (hidden0==0 || hidden0>128 || hidden1==0 || hidden1>128 || out.scene.widths.w<8 || out.scene.widths.w>64)
        throw std::runtime_error("Unsupported MLP widths");
    checkShape(arrays[0],{hashSize,3}); checkShape(arrays[1],{hashSize,3});
    checkShape(arrays[2],{hidden0,points*2*feature}); checkShape(arrays[3],{hidden0});
    checkShape(arrays[4],{hidden1,hidden0}); checkShape(arrays[5],{hidden1});
    checkShape(arrays[6],{out.scene.widths.w,hidden1}); checkShape(arrays[7],{out.scene.widths.w});
    out.scene.offsets0={offsets[0],offsets[1],offsets[2],offsets[3]};
    out.scene.offsets1={offsets[4],offsets[5],offsets[6],offsets[7]};
    return out;
}

class CompareApp : public core::Application {
public:
    CompareApp():Application({"LSNIF (left) / BVH (right)",1280,720}){}
    void run() {
        auto exported=loadExport(kExport);
        CpuModelData mesh=ModelImporter::loadFromFile(kMesh);
        auto model=ModelBuilder::buildAndEnqueue(getDevice(),getDevice().getUploadManager(),mesh);
        BVHConfig cfg; cfg.maxDepth=24; cfg.minPrimitivesPerLeaf=4;
        auto bvh=BVHManager::buildAndEnqueue(getDevice(),getDevice().getUploadManager(),mesh,
            std::make_unique<CpuBVHBuilder>(BVHSplitMethod::SpatialMedian,cfg));
        auto weights=getDevice().createBuffer(rhi::BufferDesc::Absolute(exported.weights.size(),
            rhi::BufferUsageFlags::StorageBuffer|rhi::BufferUsageFlags::TransferDst));
        auto occupancy=getDevice().createBuffer(rhi::BufferDesc::Absolute(exported.occupancy.size(),
            rhi::BufferUsageFlags::StorageBuffer|rhi::BufferUsageFlags::TransferDst));
        auto* uploader=getDevice().getUploadManager();
        uploader->enqueueBufferUpload(weights.get(),exported.weights.data(),exported.weights.size());
        uploader->enqueueBufferUpload(occupancy.get(),exported.occupancy.data(),exported.occupancy.size());
        uploader->submitUploadsAsync(); uploader->waitUploads();

        auto graph=getDevice().createRenderGraph(); registerRenderGraph(graph.get());
        auto swap=graph->importSwapchain(getSwapchain(),"swapchainImage"_hash);
        model->importToGraph(*graph); bvh->importToGraph(*graph);
        auto hWeights=graph->importResource(weights.get(),"Weights"_hash);
        auto hOccupancy=graph->importResource(occupancy.get(),"Occupancy"_hash);
        auto& inference=graph->addComputePass("LSNIF primary rays",kLsnifShader);
        auto& neural=inference.dispatchThreads([this](uint32_t& x,uint32_t& y,uint32_t& z){
            x=(getWidth()+1)/2; y=getHeight(); z=1;
        });
        neural.write(swap).read(hWeights).read(hOccupancy);
        auto& exact=graph->addComputePass("BVH primary rays",kBvhShader);
        // Separate batches place the shared-image write barrier before the BVH dispatch.
        exact.forceBatchBreak();
        auto& triangles=exact.dispatchThreads([this](uint32_t& x,uint32_t& y,uint32_t& z){
            x=getWidth()>1 ? getWidth()/2 : 1; y=getHeight(); z=1;
        });
        triangles.write(swap).read(model->hPosition).read(bvh->hReorderedIndices).read(bvh->hNodes);
        auto profiler=getDevice().createGPUProfiler(); graph->setProfiler(profiler.get());
        graph->compile();
        std::cout << "LSNIF left | BVH right; GPU pass times reported every 120 frames.\n";
        while (isRunning()) {
            if (!beginFrame()) continue;
            processEvents();
            Scene frame=exported.scene;
            frame.camPos=glm::vec4(kCamera,0);
            frame.camTarget=glm::vec4(kTarget,glm::radians(kObjectYawDegrees));
            frame.resolutionFov={float(getWidth()),float(getHeight()),glm::radians(kFovDegrees),0};
            frame.objectTransform=glm::vec4(kObjectPosition,kObjectScale);
            neural.setUniform("sceneGlobalsPtr"_hash,frame);
            triangles.setUniform("sceneGlobalsPtr"_hash,frame);
            profiler->resolveResults(getDevice().getCurrentFrame());
            if (profiler->hasNewResults() && getDevice().getCurrentFrame()%120==0) profiler->dumpToConsole();
            graph->execute(); endFrame();
        }
        getDevice().waitForIdle();
        unregisterRenderGraph(graph.get());
    }
};
} // namespace

int main() {
    try { CompareApp app; app.run(); }
    catch (const std::exception& e) { std::cerr << "LSNIFCompare: " << e.what() << '\n'; return 1; }
    return 0;
}
