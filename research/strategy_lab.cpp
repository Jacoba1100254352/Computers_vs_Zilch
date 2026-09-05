#include "computer.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Config {
    std::string mode{"duel"};
    std::size_t pairs{1000};
    std::size_t threads{1};
    std::uint64_t seed{2026090501};
    std::uint32_t target{5000};
    zilch::RuleConfig rules;
    std::optional<std::string> policyA;
    std::optional<std::string> policyB;
    std::optional<zilch::ComputerDifficulty> difficultyA{zilch::ComputerDifficulty::Hard};
    std::optional<zilch::ComputerDifficulty> difficultyB{zilch::ComputerDifficulty::Hard};
    bool collectA{false};
    bool collectB{false};
    std::uint32_t atRisk{2800};
    std::uint32_t bankedA{0};
    std::uint32_t bankedB{0};
    std::uint16_t dice{6};
    std::optional<std::string> output;
};

std::uint64_t number(const std::string& text)
{
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Expected a nonnegative integer, got: " + text);
    return result;
}

std::uint32_t score(const std::string& text)
{
    const auto result = number(text);
    if (result > 1'000'000)
        throw std::invalid_argument("Research score values must not exceed 1000000.");
    return static_cast<std::uint32_t>(result);
}

bool boolean(const std::string& text)
{
    if (text == "true" || text == "on")
        return true;
    if (text == "false" || text == "off")
        return false;
    throw std::invalid_argument("Expected true/false or on/off, got: " + text);
}

std::optional<zilch::ComputerDifficulty> difficulty(const std::string& text)
{
    if (text == "raw")
        return std::nullopt;
    const auto result = zilch::parseComputerDifficulty(text);
    if (!result)
        throw std::invalid_argument("Difficulty must be easy, medium, hard, or raw.");
    return result;
}

Config parse(const int argc, const char* const* argv)
{
    Config config;
    bool countSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--help") {
            std::cout
                << "zilch_research --mode duel|state [options]\n"
                << "  --pairs N             Independent mirrored pairs or state treatment pairs\n"
                << "  --games N             Alias for an even total of N games (N/2 pairs)\n"
                << "  --seed N --threads N  Reproducible seed and worker count (0 = hardware)\n"
                << "  --policy-a FILE --policy-b FILE\n"
                << "  --difficulty-a hard|medium|easy|raw --difficulty-b hard|medium|easy|raw\n"
                << "  --collect-a true|false --collect-b true|false\n"
                << "  --target N --opening-score N --sets on|off --stealing on|off\n"
                << "  --final-chase on|off --first-roll-mercy on|off --ties on|off\n"
                << "  --at-risk N --banked-a N --banked-b N --dice N  State mode only\n"
                << "  --output FILE         Save the same JSON also printed to stdout\n"
                << "State mode compares bank now against roll once, then resume A versus B.\n"
                << "A is seat 0 with an already-scored turn, no saved multiples, no Final Chase.\n"
                << "Custom policies retain the selected difficulty's endgame layer; raw disables it.\n";
            std::exit(0);
        }
        if (index + 1 >= argc)
            throw std::invalid_argument("Missing value for " + flag);
        const std::string value = argv[++index];
        if (flag == "--mode")
            config.mode = value;
        else if (flag == "--pairs" || flag == "--games") {
            if (countSpecified)
                throw std::invalid_argument("Specify only one --pairs or --games count.");
            const auto count = number(value);
            if (count == 0 || count > 100'000'000 || (flag == "--games" && count % 2 != 0))
                throw std::invalid_argument("Count must be positive, at most 100000000, and games must be even.");
            config.pairs = static_cast<std::size_t>(flag == "--games" ? count / 2 : count);
            countSpecified = true;
        } else if (flag == "--threads") {
            const auto count = number(value);
            if (count > 256)
                throw std::invalid_argument("Thread count must be at most 256.");
            config.threads = static_cast<std::size_t>(count);
        } else if (flag == "--seed")
            config.seed = number(value);
        else if (flag == "--policy-a")
            config.policyA = value;
        else if (flag == "--policy-b")
            config.policyB = value;
        else if (flag == "--difficulty-a")
            config.difficultyA = difficulty(value);
        else if (flag == "--difficulty-b")
            config.difficultyB = difficulty(value);
        else if (flag == "--collect-a")
            config.collectA = boolean(value);
        else if (flag == "--collect-b")
            config.collectB = boolean(value);
        else if (flag == "--target")
            config.target = score(value);
        else if (flag == "--opening-score")
            config.rules.setOpeningScoreLimit(score(value));
        else if (flag == "--sets")
            config.rules.setThreePairsEnabled(boolean(value));
        else if (flag == "--stealing")
            config.rules.setStealingEnabled(boolean(value));
        else if (flag == "--final-chase")
            config.rules.setFinalChaseEnabled(boolean(value));
        else if (flag == "--first-roll-mercy")
            config.rules.setFirstRollBustBonusEnabled(boolean(value));
        else if (flag == "--ties")
            config.rules.setAllowTies(boolean(value));
        else if (flag == "--at-risk")
            config.atRisk = score(value);
        else if (flag == "--banked-a")
            config.bankedA = score(value);
        else if (flag == "--banked-b")
            config.bankedB = score(value);
        else if (flag == "--dice") {
            const auto count = number(value);
            if (count < 1 || count > 6)
                throw std::invalid_argument("Dice count must be 1 through 6.");
            config.dice = static_cast<std::uint16_t>(count);
        } else if (flag == "--output")
            config.output = value;
        else
            throw std::invalid_argument("Unknown option: " + flag);
    }
    if (config.mode != "duel" && config.mode != "state")
        throw std::invalid_argument("Mode must be duel or state.");
    if (config.target < 1000 || config.rules.openingScoreLimit() > config.target)
        throw std::invalid_argument("Target must be at least 1000 and not below the opening score.");
    if (config.mode == "state" && (config.bankedA >= config.target || config.bankedB >= config.target))
        throw std::invalid_argument("State mode currently requires pre-Final-Chase banked scores below the target.");
    config.threads = config.threads == 0 ? std::max(1U, std::thread::hardware_concurrency()) : config.threads;
    config.threads = std::min(config.threads, config.pairs);
    return config;
}

std::string quote(const std::string& text)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\')
            output << '\\' << character;
        else if (character < 0x20)
            output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character)
                   << std::dec;
        else
            output << character;
    }
    output << '"';
    return output.str();
}

zilch::Policy load(const std::optional<std::string>& path,
                   const std::optional<zilch::ComputerDifficulty> level, const bool stealing)
{
    auto policy = level ? zilch::policyForDifficulty(*level, stealing) : zilch::Policy{};
    if (path && !zilch::loadPolicy(*path, policy))
        throw std::invalid_argument("Unable to load policy: " + *path);
    return policy;
}

void printPolicy(std::ostream& output, const zilch::Policy& policy,
                 const std::optional<std::string>& path,
                 const std::optional<zilch::ComputerDifficulty> level, const bool collect)
{
    output << "{\"name\":" << quote(policy.name)
           << ",\"source\":" << quote(path.value_or("builtin"))
           << ",\"difficulty\":" << quote(level ? std::string(zilch::computerDifficultyName(*level)) : "raw")
           << ",\"collect_before_bank\":" << (collect ? "true" : "false")
           << ",\"bank_thresholds\":[";
    for (std::size_t index = 1; index <= 6; ++index)
        output << (index == 1 ? "" : ",") << policy.bankThresholdByDice[index];
    output << "],\"score_weight\":" << policy.scoreWeight
           << ",\"remaining_dice_weight\":" << policy.remainingDiceWeight
           << ",\"hot_dice_weight\":" << policy.hotDiceWeight
           << ",\"multiple_weight\":" << policy.multipleWeight
           << ",\"lead_factor\":" << policy.leadFactor
           << ",\"trail_factor\":" << policy.trailFactor
           << ",\"closing_factor\":" << policy.closingFactor
           << ",\"roll_bias\":" << policy.rollBias << '}';
}

struct Outcomes {
    std::uint64_t games{};
    std::uint64_t wins{};
    std::uint64_t ties{};
    std::uint64_t pointsFor{};
    std::uint64_t pointsAgainst{};

    double add(const zilch::MatchResult& result, const std::size_t seat)
    {
        ++games;
        pointsFor += result.finalScores[seat];
        pointsAgainst += result.finalScores[1 - seat];
        if (!result.winnerIndex) {
            ++ties;
            return 0.5;
        }
        if (*result.winnerIndex == seat) {
            ++wins;
            return 1.0;
        }
        return 0.0;
    }

    void merge(const Outcomes& other)
    {
        games += other.games;
        wins += other.wins;
        ties += other.ties;
        pointsFor += other.pointsFor;
        pointsAgainst += other.pointsAgainst;
    }

    void print(std::ostream& output) const
    {
        output << "{\"games\":" << games << ",\"wins\":" << wins << ",\"ties\":" << ties
               << ",\"losses\":" << games - wins - ties
               << ",\"match_point_rate\":" << (static_cast<double>(wins) + 0.5 * static_cast<double>(ties)) / static_cast<double>(games)
               << ",\"points_for\":" << pointsFor << ",\"points_against\":" << pointsAgainst
               << ",\"average_score\":" << static_cast<double>(pointsFor) / static_cast<double>(games)
               << ",\"average_opponent_score\":" << static_cast<double>(pointsAgainst) / static_cast<double>(games)
               << ",\"average_score_margin\":"
               << (static_cast<double>(pointsFor) - static_cast<double>(pointsAgainst)) / static_cast<double>(games)
               << '}';
    }
};

struct Moments {
    std::uint64_t count{};
    long double sum{};
    long double squares{};

    void add(const long double value)
    {
        ++count;
        sum += value;
        squares += value * value;
    }

    void merge(const Moments& other)
    {
        count += other.count;
        sum += other.sum;
        squares += other.squares;
    }

    void print(std::ostream& output) const
    {
        const auto mean = sum / static_cast<long double>(count);
        output << "{\"independent_pairs\":" << count << ",\"sum\":" << sum
               << ",\"sum_squares\":" << squares << ",\"mean\":" << mean;
        if (count < 2) {
            output << ",\"standard_error\":null,\"ci95\":null}";
            return;
        }
        const auto variance = std::max(0.0L, (squares - sum * mean) / static_cast<long double>(count - 1));
        const auto error = std::sqrt(variance / static_cast<long double>(count));
        output << ",\"standard_error\":" << error << ",\"ci95\":["
               << mean - 1.959963984540054L * error << ',' << mean + 1.959963984540054L * error << "]}";
    }
};

struct Aggregate {
    Outcomes a;
    Outcomes b;
    Moments primary;
    Moments margin;
};

std::mt19937 engine(const std::uint64_t seed)
{
    std::seed_seq sequence{static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)};
    return std::mt19937(sequence);
}

zilch::GameManager makeState(const Config& config)
{
    zilch::GameManager game;
    game.setPlayers({"A", "B"});
    game.setScoreLimit(config.target);
    game.ruleConfig() = config.rules;
    if (config.mode == "state") {
        game.players()[0].score().addPermanentScore(config.bankedA);
        game.players()[1].score().addPermanentScore(config.bankedB);
        game.startTurn(0);
        game.currentPlayer().score().setRoundScore(config.atRisk);
        game.manageDiceCount(config.dice);
        game.setSelectedOption(true);
        game.registerRoll();
        // The investigated roll follows earlier scoring. A bust on the next
        // roll cannot be rescued by the first-roll-only mercy rule.
        game.setBustBonusUsedThisTurn(true);
        if (!game.canBankCurrentScore())
            throw std::invalid_argument("State mode must start from a bankable, already-scored turn.");
    }
    return game;
}

std::string run(const Config& config)
{
    const auto policyA = load(config.policyA, config.difficultyA, config.rules.stealingEnabled());
    const auto policyB = load(config.policyB, config.difficultyB, config.rules.stealingEnabled());
    const auto initial = makeState(config);
    std::mt19937_64 seedGenerator(config.seed);
    std::vector<std::uint64_t> seeds(config.pairs);
    for (auto& seed : seeds)
        seed = seedGenerator();
    std::atomic<std::size_t> nextPair{};
    std::vector<Aggregate> workers(config.threads);
    std::vector<std::thread> threads;
    for (std::size_t worker = 0; worker < config.threads; ++worker) {
        threads.emplace_back([&, worker]() {
            auto& aggregate = workers[worker];
            for (;;) {
                const auto pair = nextPair.fetch_add(1);
                if (pair >= config.pairs)
                    break;
                zilch::ComputerController firstA(policyA, config.difficultyA, config.collectA);
                zilch::ComputerController firstB(policyB, config.difficultyB, config.collectB);
                zilch::ComputerController secondA(policyA, config.difficultyA, config.collectA);
                zilch::ComputerController secondB(policyB, config.difficultyB, config.collectB);
                auto firstRng = engine(seeds[pair]);
                auto secondRng = engine(seeds[pair]);
                if (config.mode == "duel") {
                    const auto first = zilch::playMatchFromState(initial, {&firstA, &firstB}, firstRng);
                    const auto second = zilch::playMatchFromState(initial, {&secondB, &secondA}, secondRng);
                    const auto firstPoints = aggregate.a.add(first, 0);
                    const auto secondPoints = aggregate.a.add(second, 1);
                    aggregate.b.add(first, 1);
                    aggregate.b.add(second, 0);
                    aggregate.primary.add((firstPoints + secondPoints) / 2.0);
                    aggregate.margin.add((static_cast<long double>(first.finalScores[0]) - first.finalScores[1]
                                          + second.finalScores[1] - second.finalScores[0]) / 2.0L);
                } else {
                    const auto bank = zilch::playMatchFromState(initial, {&firstA, &firstB}, firstRng,
                                                              zilch::MatchEntry::BankCurrentTurn);
                    const auto roll = zilch::playMatchFromState(initial, {&secondA, &secondB}, secondRng,
                                                              zilch::MatchEntry::RollCurrentTurn);
                    const auto bankPoints = aggregate.a.add(bank, 0);
                    const auto rollPoints = aggregate.b.add(roll, 0);
                    aggregate.primary.add(rollPoints - bankPoints);
                    aggregate.margin.add(static_cast<long double>(roll.finalScores[0]) - roll.finalScores[1]
                                         - bank.finalScores[0] + bank.finalScores[1]);
                }
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    Aggregate total;
    for (const auto& worker : workers) {
        total.a.merge(worker.a);
        total.b.merge(worker.b);
        total.primary.merge(worker.primary);
        total.margin.merge(worker.margin);
    }

    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema_version\":1,\"mode\":" << quote(config.mode)
           << ",\"seed\":" << config.seed << ",\"pairs\":" << config.pairs
           << ",\"total_games\":" << config.pairs * 2 << ",\"threads\":" << config.threads
           << ",\"rng\":\"mt19937_64 master; each draw seeds mt19937 via seed_seq(low32,high32); same stream within each pair\""
           << ",\"interval_method\":\"sample variance of independent pair observations; normal 95% interval; no multiple-comparison adjustment\""
           << ",\"rules\":{\"target\":" << config.target
           << ",\"opening_score\":" << config.rules.openingScoreLimit()
           << ",\"straight\":true,\"multiples\":true,\"singles\":true"
           << ",\"three_pairs\":" << (config.rules.threePairsEnabled() ? "true" : "false")
           << ",\"stealing\":" << (config.rules.stealingEnabled() ? "true" : "false")
           << ",\"first_roll_mercy\":" << (config.rules.firstRollBustBonusEnabled() ? "true" : "false")
           << ",\"final_chase\":" << (config.rules.finalChaseEnabled() ? "true" : "false")
           << ",\"ties\":" << (config.rules.tiesAllowed() ? "true" : "false") << '}'
           << ",\"policy_a\":";
    printPolicy(output, policyA, config.policyA, config.difficultyA, config.collectA);
    output << ",\"policy_b\":";
    printPolicy(output, policyB, config.policyB, config.difficultyB, config.collectB);
    if (config.mode == "duel") {
        output << ",\"a\":";
        total.a.print(output);
        output << ",\"b\":";
        total.b.print(output);
        output << ",\"a_match_point_rate_paired\":";
        total.primary.print(output);
        output << ",\"a_score_margin_paired\":";
        total.margin.print(output);
    } else {
        output << ",\"state\":{\"banked_a\":" << config.bankedA << ",\"banked_b\":" << config.bankedB
               << ",\"at_risk\":" << config.atRisk << ",\"next_dice\":" << config.dice
               << ",\"seat\":0,\"final_chase_active\":false,\"prior_scoring_roll\":true,\"saved_multiples\":{},\"mercy_available_next_roll\":false}"
               << ",\"bank\":";
        total.a.print(output);
        output << ",\"roll\":";
        total.b.print(output);
        output << ",\"roll_minus_bank_match_points_paired\":";
        total.primary.print(output);
        output << ",\"roll_minus_bank_score_margin_paired\":";
        total.margin.print(output);
    }
    output << "}\n";
    return output.str();
}

} // namespace

int main(const int argc, const char* const* argv)
{
    try {
        const auto config = parse(argc, argv);
        if (config.output && std::filesystem::exists(*config.output))
            throw std::invalid_argument("Refusing to overwrite existing research evidence: " + *config.output);
        const auto result = run(config);
        if (config.output) {
            const auto path = std::filesystem::path(*config.output);
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::out | std::ios::noreplace);
            output << result;
            if (!output)
                throw std::runtime_error("Unable to save research output: " + *config.output);
        }
        std::cout << result;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Research error: " << error.what() << '\n';
        return 1;
    }
}
