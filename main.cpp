#include "computer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void printUsage()
{
    std::cout
        << "Usage:\n"
        << "  ./zilch play [--name NAME] [--score-limit N] [--difficulty LEVEL | --policy PATH] [--seed N]\n"
        << "  ./zilch arena [--bot-a LEVEL | --policy-a PATH] [--bot-b LEVEL | --policy-b PATH]\n"
        << "                 [--games N] [--threads N]\n"
        << "                 [--score-limit N] [--seed N]\n"
        << "  ./zilch train [--generations N] [--population N] [--matches N] [--threads N]\n"
        << "                [--score-limit N] [--output PATH] [--resume PATH] [--seed N]\n"
        << "\n"
        << "Common rule options for play, arena, and train:\n"
        << "  --score-limit N --opening-score N\n"
        << "  --straight BOOL --three-pairs BOOL --multiples BOOL --singles BOOL\n"
        << "  --first-roll-bust BOOL --final-chase BOOL --allow-ties BOOL --stealing BOOL\n"
        << "  BOOL accepts on/off, true/false, yes/no, enabled/disabled, or 1/0.\n"
        << "  LEVEL accepts easy, medium, or hard. Hard automatically uses the Stealing-trained\n"
        << "  policy when --stealing is enabled. Policy paths remain available for research runs.\n"
        << "Notes: score limits must be at least 1000; opening score cannot exceed the score limit;\n"
        << "       arena games and training matches must be even.\n";
}

inline constexpr std::array<std::string_view, 10> COMMON_OPTIONS{
    "score-limit",
    "opening-score",
    "straight",
    "three-pairs",
    "multiples",
    "singles",
    "first-roll-bust",
    "final-chase",
    "allow-ties",
    "stealing",
};

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
        if (std::ranges::find(COMMON_OPTIONS, key) == COMMON_OPTIONS.end() &&
            std::ranges::find(allowed, key) == allowed.end()) {
            throw std::invalid_argument("Unknown option: --" + key);
        }
    }
}

bool parseBoolean(const std::map<std::string, std::string>& options, const std::string& key, const bool fallback)
{
    const auto it = options.find(key);
    if (it == options.end())
        return fallback;

    std::string normalized = it->second;
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (normalized == "on" || normalized == "true" || normalized == "yes" || normalized == "enabled" ||
        normalized == "1") {
        return true;
    }
    if (normalized == "off" || normalized == "false" || normalized == "no" || normalized == "disabled" ||
        normalized == "0") {
        return false;
    }

    throw std::invalid_argument("Invalid boolean value for --" + key + ": " + it->second);
}

std::optional<zilch::ComputerDifficulty> parseDifficultyOption(
    const std::map<std::string, std::string>& options,
    const std::string& key)
{
    const auto it = options.find(key);
    if (it == options.end())
        return std::nullopt;

    const auto difficulty = zilch::parseComputerDifficulty(it->second);
    if (!difficulty)
        throw std::invalid_argument("Invalid computer difficulty for --" + key + ": " + it->second);
    return difficulty;
}

void applyCommonRuleOptions(
    const std::map<std::string, std::string>& options,
    const std::uint32_t scoreLimit,
    zilch::RuleConfig& ruleConfig)
{
    ruleConfig.setOpeningScoreLimit(parseUnsigned<std::uint32_t>(
        options, "opening-score", ruleConfig.openingScoreLimit(), 0, scoreLimit));
    ruleConfig.setStraightEnabled(parseBoolean(options, "straight", ruleConfig.straightEnabled()));
    ruleConfig.setThreePairsEnabled(parseBoolean(options, "three-pairs", ruleConfig.threePairsEnabled()));
    ruleConfig.setMultiplesEnabled(parseBoolean(options, "multiples", ruleConfig.multiplesEnabled()));
    ruleConfig.setSinglesEnabled(parseBoolean(options, "singles", ruleConfig.singlesEnabled()));
    ruleConfig.setFirstRollBustBonusEnabled(
        parseBoolean(options, "first-roll-bust", ruleConfig.firstRollBustBonusEnabled()));
    ruleConfig.setFinalChaseEnabled(parseBoolean(options, "final-chase", ruleConfig.finalChaseEnabled()));
    ruleConfig.setAllowTies(parseBoolean(options, "allow-ties", ruleConfig.tiesAllowed()));
    ruleConfig.setStealingEnabled(parseBoolean(options, "stealing", ruleConfig.stealingEnabled()));

    if (!ruleConfig.hasScoringRuleEnabled())
        throw std::invalid_argument("At least one scoring rule must be enabled.");
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
            rejectUnknownOptions(options, {"name", "policy", "difficulty", "seed"});
            zilch::PlayConfig config;
            if (const auto it = options.find("name"); it != options.end())
                config.humanName = it->second;
            if (const auto it = options.find("policy"); it != options.end())
                config.policyPath = it->second;
            config.difficulty = parseDifficultyOption(options, "difficulty");
            if (config.policyPath && config.difficulty)
                throw std::invalid_argument("--policy cannot be combined with --difficulty.");

            config.scoreLimit = parseUnsigned<std::uint32_t>(
                options, "score-limit", config.scoreLimit, 1000);
            config.seed = parseUnsigned<std::uint64_t>(options, "seed", config.seed, 0);
            applyCommonRuleOptions(options, config.scoreLimit, config.ruleConfig);

            return zilch::runHumanVsComputer(config) ? 0 : 1;
        }

        if (command == "train") {
            rejectUnknownOptions(
                options,
                {"generations", "population", "matches", "threads", "output", "resume", "seed"});
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
            applyCommonRuleOptions(options, config.scoreLimit, config.ruleConfig);

            zilch::Trainer trainer(config);
            const auto bestPolicy = trainer.train(std::cout);
            std::cout << "Best policy:\n" << zilch::describePolicy(bestPolicy) << '\n';
            return 0;
        }

        if (command == "arena") {
            rejectUnknownOptions(options, {"policy-a", "policy-b", "bot-a", "bot-b", "games", "threads", "seed"});
            zilch::ArenaConfig config;
            if (const auto it = options.find("policy-a"); it != options.end())
                config.policyAPath = it->second;
            if (const auto it = options.find("policy-b"); it != options.end())
                config.policyBPath = it->second;
            config.difficultyA = parseDifficultyOption(options, "bot-a");
            config.difficultyB = parseDifficultyOption(options, "bot-b");
            if (config.policyAPath && config.difficultyA)
                throw std::invalid_argument("--policy-a cannot be combined with --bot-a.");
            if (config.policyBPath && config.difficultyB)
                throw std::invalid_argument("--policy-b cannot be combined with --bot-b.");

            config.games = parseUnsigned<std::size_t>(options, "games", config.games, 2);
            config.threads = parseUnsigned<std::size_t>(options, "threads", config.threads, 0, 1024);
            config.scoreLimit = parseUnsigned<std::uint32_t>(
                options, "score-limit", config.scoreLimit, 1000);
            config.seed = parseUnsigned<std::uint64_t>(options, "seed", config.seed, 0);
            applyCommonRuleOptions(options, config.scoreLimit, config.ruleConfig);

            return zilch::runArena(config) ? 0 : 1;
        }

        printUsage();
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
