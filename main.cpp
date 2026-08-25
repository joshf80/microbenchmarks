#include <vector>
#include <chrono>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <cassert>

auto MeasureL1CacheMiss(){
    constexpr auto CacheLineSize = std::hardware_destructive_interference_size;
    constexpr auto EvictCacheLineSize = CacheLineSize * (2 << 12);
    constexpr auto Trials = 20000;
    std::vector<uint64_t> CacheLineData(CacheLineSize), CacheLineEvictData(EvictCacheLineSize);
    volatile uint64_t sink = 0;

    using namespace std::chrono;
    auto floodTime = 0.0;

    const auto startTrialing = steady_clock::now();

    for (auto trials{0uz}; trials < Trials; ++trials)  {
        auto floodStart = steady_clock::now();
        for (auto i{0uz}; i < CacheLineEvictData.size(); i+=CacheLineSize){
            sink += CacheLineEvictData[i];
        }
        auto floodEnd = steady_clock::now();
        const auto iterationFloodTime = duration_cast<nanoseconds>(floodEnd - floodStart).count();
        floodTime += static_cast<double>(iterationFloodTime);

        sink += CacheLineEvictData[0];
    }
    const auto endTrialing = steady_clock::now();
    const auto totalTrialTime = static_cast<double>(
        duration_cast<nanoseconds>(endTrialing - startTrialing).count());
    const auto totalReadLatency = totalTrialTime - floodTime;
    return totalReadLatency / Trials;
}


auto MeasureL1CacheSize() {
    constexpr auto CacheLine = std::hardware_destructive_interference_size;
    constexpr uint64_t MaxBytes = 8 * 1024 * 1024;
    constexpr auto Trials = CacheLine * (2 << 6);
    std::vector<uint64_t> buffer(MaxBytes / sizeof(uint64_t));
    volatile uint64_t sink = 0;
    auto data = buffer.data();
    std::vector<std::pair<int, double>> track;

    for (size_t Bytes = 1024; Bytes <= MaxBytes; Bytes <<= 1) {
        auto elements = Bytes / sizeof(uint64_t);
        auto stride = CacheLine / sizeof(uint64_t);


        using namespace std::chrono;

        for (size_t j = 0; j < elements; j += stride) {
            sink += data[j];
        }


        const auto start = steady_clock::now();

        for (int i = 0; i < Trials; ++i) {
            for (auto j{0uz}; j < elements; j += stride) {
                sink += data[j];
            }
        }

        auto end = steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        auto cacheLinesPerTrial = elements / stride;
        auto totalCacheLines = cacheLinesPerTrial * Trials;
        auto nsPerCacheLine = static_cast<double>(duration) / totalCacheLines;

        track.emplace_back(static_cast<int>(Bytes / 1024), nsPerCacheLine);
    }
    std::string result = "";
    for (auto &it : track) {
        result += std::to_string(it.first) + "kb : " + std::to_string(it.second) + "ns\n";
    }
    return result;
}

int main()  {
    std::cout << MeasureL1CacheSize() << std::endl;
    std::cout << MeasureL1CacheMiss();
}
