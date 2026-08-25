#include <vector>
#include <chrono>
#include <iostream>

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
}

int main()  {
    std::cout << MeasureL1CacheMiss();
}
