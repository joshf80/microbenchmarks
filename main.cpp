#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t CacheLineBytes = std::hardware_destructive_interference_size;
constexpr std::size_t MaxWorkingSetBytes = 8 * 1024 * 1024;
constexpr std::size_t MeasurementSamples = 5;
constexpr std::size_t WordsPerCacheLine = CacheLineBytes / sizeof(std::size_t);

static_assert(CacheLineBytes % sizeof(std::size_t) == 0);

volatile std::size_t benchmarkSink = 0;

struct PointerCycle {
    std::vector<std::size_t> links;
    std::size_t lineCount;
    std::size_t startIndex;
};

struct LatencyResult {
    std::size_t workingSetBytes;
    double nanosecondsPerLoad;
};

PointerCycle MakePointerCycle(std::size_t workingSetBytes, std::mt19937_64& generator) {
    const auto lineCount = workingSetBytes / CacheLineBytes;
    std::vector<std::size_t> links(lineCount * WordsPerCacheLine);
    std::vector<std::size_t> order(lineCount);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), generator);

    for (std::size_t i = 0; i < lineCount; ++i) {
        links[order[i] * WordsPerCacheLine] = order[(i + 1) % lineCount];
    }

    return {std::move(links), lineCount, order.front()};
}

std::size_t Traverse(const PointerCycle& cycle, std::size_t index, std::size_t loads) {
    for (std::size_t i = 0; i < loads; ++i) {
        index = cycle.links[index * WordsPerCacheLine];
    }
    return index;
}

double MeasureLatency(const PointerCycle& cycle, std::size_t batchSize) {
    const auto totalLoads = cycle.lineCount * batchSize + 1;
    std::vector<double> samples;
    samples.reserve(MeasurementSamples);

    for (std::size_t sample = 0; sample < MeasurementSamples; ++sample) {
        auto index = Traverse(cycle, cycle.startIndex, cycle.lineCount);
        const auto start = std::chrono::steady_clock::now();
        index = Traverse(cycle, index, totalLoads);
        const auto end = std::chrono::steady_clock::now();
        benchmarkSink = index;

        const auto elapsed = std::chrono::duration<double, std::nano>(end - start).count();
        samples.push_back(elapsed / static_cast<double>(totalLoads));
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::vector<LatencyResult> MeasureL1Capacity(std::size_t batchSize) {
    std::mt19937_64 generator(0xCACE23);
    std::vector<LatencyResult> results;

    for (std::size_t workingSetBytes = 1024; workingSetBytes <= MaxWorkingSetBytes; workingSetBytes *= 2) {
        auto cycle = MakePointerCycle(workingSetBytes, generator);
        results.push_back({workingSetBytes, MeasureLatency(cycle, batchSize)});
    }

    return results;
}

std::size_t FindL1Transition(const std::vector<LatencyResult>& results) {
    const auto baseline = std::min({
        results[0].nanosecondsPerLoad,
        results[1].nanosecondsPerLoad,
        results[2].nanosecondsPerLoad
    });
    const auto threshold = baseline * 1.5;

    for (std::size_t i = 1; i + 1 < results.size(); ++i) {
        if (results[i].nanosecondsPerLoad >= threshold && results[i + 1].nanosecondsPerLoad >= threshold) {
            return results[i].workingSetBytes;
        }
    }

    return 0;
}

} // namespace

int main() {
    std::cout << "Test batch size: ";
    std::string input;
    if (!(std::cin >> input)) {
        std::cerr << "invalid input";
        return 1;
    }

    std::size_t batchSize = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), batchSize);
    if (error != std::errc{} || end != input.data() + input.size() || batchSize == 0) {
        std::cerr << "invalid batch size";
        return 1;
    }

    const auto maxLines = MaxWorkingSetBytes / CacheLineBytes;
    if (batchSize > (std::numeric_limits<std::size_t>::max() - 1) / maxLines) {
        std::cerr << "invalid batch size";
        return 1;
    }

    const auto capacityResults = MeasureL1Capacity(batchSize);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\load latency by set size (median of " << MeasurementSamples << " samples):\n";
    for (const auto& result : capacityResults) {
        std::cout << result.workingSetBytes / 1024 << " KiB: " << result.nanosecondsPerLoad << " ns/load\n";
    }

    const auto transitionBytes = FindL1Transition(capacityResults);
    if (transitionBytes == 0 || transitionBytes > MaxWorkingSetBytes / 2) {
        std::cerr << "bad data";
        return 0;
    }

    const auto missWorkingSetBytes = transitionBytes * 2;
    std::mt19937_64 generator(0xCACE23);
    auto missCycle = MakePointerCycle(missWorkingSetBytes, generator);
    const auto missLatency = MeasureLatency(missCycle, batchSize);

    std::cout << "\nL1 latency transition: between " << transitionBytes / 2 / 1024 << " and " << transitionBytes / 1024 << " KiB (coarse estimate)\n";
    std::cout << "load latency at " << missWorkingSetBytes / 1024 << " KiB "<< "(our expected L2 hits): " << missLatency << " ns/load\n";
}
