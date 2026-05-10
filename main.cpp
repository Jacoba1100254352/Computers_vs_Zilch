#include "computer.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void printUsage()
{
    std::cout
        << "Usage:\n"
        << "  ./zilch play [--name NAME] [--score-limit N] [--policy PATH] [--seed N]\n"
        << "  ./zilch arena [--policy-a PATH] [--policy-b PATH] [--games N] [--threads N]\n"
        << "                 [--score-limit N] [--seed N]\n"
        << "  ./zilch train [--generations N] [--population N] [--matches N] [--threads N]\n"
        << "                [--score-limit N] [--output PATH] [--resume PATH] [--seed N]\n"
        << "\n"
        << "Notes: score limits must be at least 1000; arena games and training matches must be even.\n";
}

std::map<std::string, std::string> parseOptions(const int argc, char* argv[], const int startIndex)
{
    std::map<std::string, std::string> options;

    for (int index = startIndex; index < argc; ++index) {
        std::string key = argv[index];
        if (!key.starts_with("--"))
            throw std::invalid_argument("Unexpected argument: " + key);

        key.erase(0, 2);
        if (options.contains(key))
            throw std::invalid_argument("Duplicate option: --" + key);

        if (index + 1 < argc && !std::string(argv[index + 1]).starts_with("--")) {
            options[key] = argv[++index];
        } else {
            options[key] = "true";
        }
    }

    return options;
}

template <typename T>
T parseUnsigned(
    const std::map<std::string, std::string>& options,
    const std::string& key,
    const T fallback,
    const T minimum,
    const T maximum = std::numeric_limits<T>::max())
{
    if (const auto it = options.find(key); it != options.end()) {
        if (it->second.empty() || it->second.front() == '-' || it->second.front() == '+')
            throw std::invalid_argument("Invalid numeric value for --" + key + ": " + it->second);
        std::size_t parsed = 0;
        const auto value = std::stoull(it->second, &parsed);
        if (parsed != it->second.size())
            throw std::invalid_argument("Invalid numeric value for --" + key + ": " + it->second);
        if (value < minimum || value > maximum)
            throw std::invalid_argument("Value out of range for --" + key + ": " + it->second);
        return static_cast<T>(value);
    }
    return fallback;
}

void rejectUnknownOptions(
    const std::map<std::string, std::string>& options,
    const std::initializer_list<std::string_view> allowed)
{
    for (const auto& [key, value] : options) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw std::invalid_argument("Unknown option: --" + key);
    }
}

} // namespace

int main(const int argc, char* argv[])
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    if (command == "--help" || command == "help") {
        printUsage();
        return 0;
    }

    try {
        const auto options = parseOptions(argc, argv, 2);

        if (command == "play") {
            rejectUnknownOptions(options, {"name", "score-limit", "policy", "seed"});
            zilch::PlayConfig config;
            if (const auto it = options.find("name"); it != options.end())
                config.humanName = it->second;
            if (const auto it = options.find("policy"); it != options.end())
                config.policyPath = it->second;

            config.scoreLimit = parseUnsigned<std::uint32_t>(
                options, "score-limit", config.scoreLimit, 1000);
            config.seed = parseUnsigned<std::uint64_t>(options, "seed", config.seed, 0);

            return zilch::runHumanVsComputer(config) ? 0 : 1;
        }

        if (command == "train") {
            rejectUnknownOptions(
                options,
                {"generations", "population", "matches", "threads", "score-limit", "output", "resume", "seed"});
            zilch::TrainingConfig config;
            if (const auto it = options.find("output"); it != options.end())
                config.outputPath = it->second;
            if (const auto it = options.find("resume"); it != options.end())
                config.resumePolicyPath = it->second;

            config.generations = parseUnsigned<std::size_t>(options, "generations", config.generations, 1);
            config.population = parseUnsigned<std::size_t>(options, "population", config.population, 4);
            config.matchesPerGeneration =
                parseUnsigned<std::size_t>(options, "matches", config.matchesPerGeneration, 2);
            config.threads = parseUnsigned<std::size_t>(options, "threads", config.threads, 0, 1024);
            config.scoreLimit = parseUnsigned<std::uint32_t>(
                options, "score-limit", config.scoreLimit, 1000);
            config.seed = parseUnsigned<std::uint64_t>(options, "seed", config.seed, 0);

            zilch::Trainer trainer(config);
            const auto bestPolicy = trainer.train(std::cout);
            std::cout << "Best policy:\n" << zilch::describePolicy(bestPolicy) << '\n';
            return 0;
        }

        if (command == "arena") {
            rejectUnknownOptions(options, {"policy-a", "policy-b", "games", "threads", "score-limit", "seed"});
            zilch::ArenaConfig config;
            if (const auto it = options.find("policy-a"); it != options.end())
                config.policyAPath = it->second;
            if (const auto it = options.find("policy-b"); it != options.end())
                config.policyBPath = it->second;

            config.games = parseUnsigned<std::size_t>(options, "games", config.games, 2);
            config.threads = parseUnsigned<std::size_t>(options, "threads", config.threads, 0, 1024);
            config.scoreLimit = parseUnsigned<std::uint32_t>(
                options, "score-limit", config.scoreLimit, 1000);
            config.seed = parseUnsigned<std::uint64_t>(options, "seed", config.seed, 0);

            return zilch::runArena(config) ? 0 : 1;
        }

        printUsage();
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
