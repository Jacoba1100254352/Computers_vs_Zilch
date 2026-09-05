#include "computer.h"
#include "selection_checkpoint.h"

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
    zilch::ResearchFeatures featuresA;
    zilch::ResearchFeatures featuresB;
    std::uint32_t atRisk{2800};
    std::uint32_t bankedA{0};
    std::uint32_t bankedB{0};
    std::uint16_t dice{6};
    std::vector<std::uint16_t> roll;
    std::vector<std::uint16_t> selectLeft;
    std::vector<std::uint16_t> selectRight;
    zilch::MatchEntry actionLeft{zilch::MatchEntry::RollCurrentTurn};
    zilch::MatchEntry actionRight{zilch::MatchEntry::RollCurrentTurn};
    std::array<std::uint32_t, 6> savedMultiples{};
    bool activeFinalChase{};
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

std::vector<std::uint32_t> numberList(const std::string& text)
{
    std::vector<std::uint32_t> result;
    std::size_t start{};
    for (;;) {
        const auto end = text.find(',', start);
        result.push_back(score(text.substr(start, end == std::string::npos ? end : end - start)));
        if (end == std::string::npos)
            return result;
        start = end + 1;
    }
}

std::vector<std::uint16_t> diceList(const std::string& text)
{
    const auto numbers = numberList(text);
    if (numbers.empty() || numbers.size() > 6 ||
        std::any_of(numbers.begin(), numbers.end(), [](const auto value) { return value < 1 || value > 6; }))
        throw std::invalid_argument("Dice lists require one through six comma-separated faces, each one through six.");
    return {numbers.begin(), numbers.end()};
}

zilch::MatchEntry branchAction(const std::string& text)
{
    if (text == "roll")
        return zilch::MatchEntry::RollCurrentTurn;
    if (text == "bank")
        return zilch::MatchEntry::BankCurrentTurn;
    throw std::invalid_argument("Branch actions must be roll or bank.");
}

double chainWeight(const std::string& text)
{
    std::size_t consumed{};
    const auto value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value) || value < 0 || value > 8)
        throw std::invalid_argument("Chain-risk weight must be a finite number between 0 and 8.");
    return value;
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
    bool selectionSpecified = false;
    bool diceSpecified = false;
    bool atRiskSpecified = false;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--help") {
            std::cout
                << "zilch_research --mode duel|state|selection [options]\n"
                << "  --pairs N             Independent mirrored pairs or state treatment pairs\n"
                << "  --games N             Alias for an even total of N games (N/2 pairs)\n"
                << "  --seed N --threads N  Reproducible seed and worker count (0 = hardware)\n"
                << "  --policy-a FILE --policy-b FILE\n"
                << "  --difficulty-a hard|medium|easy|raw --difficulty-b hard|medium|easy|raw\n"
                << "  --collect-a true|false --collect-b true|false\n"
                << "  --chain-risk-a N --chain-risk-b N  Research chain-risk weights (default 0)\n"
                << "  --chain-mode-a raise|blend --chain-mode-b raise|blend (default raise)\n"
                << "  --safe-finish-a true|false --safe-finish-b true|false (default false)\n"
                << "  --target N --opening-score N --sets on|off --stealing on|off\n"
                << "  --final-chase on|off --first-roll-mercy on|off --ties on|off\n"
                << "  --at-risk N --banked-a N --banked-b N  State/selection modes\n"
                << "  --dice N              State mode only\n"
                << "  --roll 6,6,6,5,2,3   Selection mode: fixed roll, before selecting dice\n"
                << "  --select-left 6,6,6 --select-right 6,6,6,5\n"
                << "  --action-left roll|bank --action-right roll|bank\n"
                << "  --saved-multiples 0,0,0,0,0,0 --active-final-chase true|false\n"
                << "  --output FILE         Save the same JSON also printed to stdout\n"
                << "State mode compares bank now against roll once, then resume A versus B.\n"
                << "A is seat 0 with an already-scored turn, no saved multiples, no Final Chase.\n"
                << "Selection mode pairs two legal selections/actions from the same fixed roll.\n"
                << "Its at-risk value is BEFORE either selection; A acts and B is its opponent.\n"
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
        else if (flag == "--chain-risk-a")
            config.featuresA.chainRiskWeight = chainWeight(value);
        else if (flag == "--chain-risk-b")
            config.featuresB.chainRiskWeight = chainWeight(value);
        else if (flag == "--safe-finish-a")
            config.featuresA.safeFinishCollection = boolean(value);
        else if (flag == "--safe-finish-b")
            config.featuresB.safeFinishCollection = boolean(value);
        else if (flag == "--chain-mode-a" || flag == "--chain-mode-b") {
            if (value != "raise" && value != "blend")
                throw std::invalid_argument("Chain mode must be raise or blend.");
            auto& features = flag == "--chain-mode-a" ? config.featuresA : config.featuresB;
            features.lowerChainThresholds = value == "blend";
        } else if (flag == "--target")
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
        else if (flag == "--at-risk") {
            atRiskSpecified = true;
            config.atRisk = score(value);
        } else if (flag == "--banked-a")
            config.bankedA = score(value);
        else if (flag == "--banked-b")
            config.bankedB = score(value);
        else if (flag == "--dice") {
            diceSpecified = true;
            const auto count = number(value);
            if (count < 1 || count > 6)
                throw std::invalid_argument("Dice count must be 1 through 6.");
            config.dice = static_cast<std::uint16_t>(count);
        } else if (flag == "--output")
            config.output = value;
        else if (flag == "--roll" || flag == "--select-left" || flag == "--select-right") {
            selectionSpecified = true;
            if (flag == "--roll")
                config.roll = diceList(value);
            else if (flag == "--select-left")
                config.selectLeft = diceList(value);
            else
                config.selectRight = diceList(value);
        } else if (flag == "--action-left" || flag == "--action-right") {
            selectionSpecified = true;
            if (flag == "--action-left")
                config.actionLeft = branchAction(value);
            else
                config.actionRight = branchAction(value);
        } else if (flag == "--saved-multiples") {
            selectionSpecified = true;
            const auto scores = numberList(value);
            if (scores.size() != 6)
                throw std::invalid_argument("Saved multiples require exactly six scores, ordered by die face.");
            std::copy(scores.begin(), scores.end(), config.savedMultiples.begin());
        } else if (flag == "--active-final-chase") {
            selectionSpecified = true;
            config.activeFinalChase = boolean(value);
        } else
            throw std::invalid_argument("Unknown option: " + flag);
    }
    if (config.mode != "duel" && config.mode != "state" && config.mode != "selection")
        throw std::invalid_argument("Mode must be duel, state, or selection.");
    if (selectionSpecified && config.mode != "selection")
        throw std::invalid_argument("Fixed roll/selection options require selection mode.");
    if (diceSpecified && config.mode == "selection")
        throw std::invalid_argument("Selection mode derives its dice count from --roll, not --dice.");
    if (config.mode == "selection" && !atRiskSpecified)
        config.atRisk = 0;
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
                 const std::optional<zilch::ComputerDifficulty> level, const bool collect,
                 const zilch::ResearchFeatures& features)
{
    output << "{\"name\":" << quote(policy.name)
           << ",\"source\":" << quote(path.value_or("builtin"))
           << ",\"difficulty\":" << quote(level ? std::string(zilch::computerDifficultyName(*level)) : "raw")
           << ",\"collect_before_bank\":" << (collect ? "true" : "false")
           << ",\"chain_risk_weight\":" << features.chainRiskWeight
           << ",\"chain_mode\":" << quote(features.lowerChainThresholds ? "blend" : "raise")
           << ",\"lower_chain_thresholds\":" << (features.lowerChainThresholds ? "true" : "false")
           << ",\"safe_finish_collection\":" << (features.safeFinishCollection ? "true" : "false")
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
    if (config.mode == "selection") {
        return zilch::research::makeSelectionCheckpoint({config.rules, config.target, config.atRisk,
            config.bankedA, config.bankedB, config.roll, config.savedMultiples, config.activeFinalChase});
    }
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

template <typename Values>
void printValues(std::ostream& output, const Values& values)
{
    output << '[';
    bool first = true;
    for (const auto value : values) {
        output << (first ? "" : ",") << value;
        first = false;
    }
    output << ']';
}

void printCheckpointState(std::ostream& output, const zilch::GameManager& game)
{
    output << "{\"banked_a\":" << game.players()[0].score().permanentScore()
           << ",\"banked_b\":" << game.players()[1].score().permanentScore()
           << ",\"at_risk\":" << game.currentPlayer().score().roundScore()
           << ",\"next_dice\":" << game.currentPlayer().dice().numDiceInPlay()
           << ",\"seat\":" << game.currentIndex()
           << ",\"selected_option\":" << (game.selectedOption() ? "true" : "false")
           << ",\"turn_active\":" << (game.turnActive() ? "true" : "false")
           << ",\"can_bank\":" << (game.canBankCurrentScore() ? "true" : "false")
           << ",\"final_chase_active\":" << (game.finalRoundActive() ? "true" : "false")
           << ",\"last_turn_of_match\":" << (game.wouldEndAfterCurrentTurn() ? "true" : "false")
           << ",\"final_chase_leader_seat\":";
    if (game.finalRoundActive())
        output << game.finalRoundLeaderIndex();
    else
        output << "null";
    output << ",\"remaining_rolled_counts\":[";
    const auto& dice = game.currentPlayer().dice().diceSetMap();
    for (std::uint16_t face = 1; face <= 6; ++face) {
        const auto found = dice.find(face);
        output << (face == 1 ? "" : ",") << (found == dice.end() ? 0 : found->second);
    }
    output << "],\"saved_multiple_scores\":[";
    for (std::uint16_t face = 1; face <= 6; ++face)
        output << (face == 1 ? "" : ",") << game.savedMultipleScore(face);
    output << "],\"roll_count_this_turn\":" << game.rollCountThisTurn()
           << ",\"mercy_available_next_roll\":false}";
}

void printSelectionBranch(std::ostream& output, const zilch::research::SelectionBranch& branch,
                          const std::vector<std::uint16_t>& selected, const std::uint32_t beforeScore)
{
    output << "{\"selected_dice\":";
    printValues(output, selected);
    output << ",\"action\":" << quote(branch.action == zilch::MatchEntry::BankCurrentTurn ? "bank" : "roll")
           << ",\"score_gain\":" << branch.game.currentPlayer().score().roundScore() - beforeScore
           << ",\"applied_options\":[";
    for (std::size_t index = 0; index < branch.options.size(); ++index) {
        const auto& option = branch.options[index];
        const auto type = option.type == zilch::OptionType::Multiple ? "multiple" :
            option.type == zilch::OptionType::Single ? "single" :
            option.type == zilch::OptionType::Straight ? "straight" : "three_pairs";
        output << (index == 0 ? "" : ",") << "{\"type\":" << quote(type)
               << ",\"face\":" << option.dieValue << ",\"dice_used\":" << option.diceUsed
               << ",\"score_gain\":" << option.scoreGain << ",\"next_dice\":" << option.nextDiceCount
               << ",\"extends_multiple\":" << (option.extendsMultiple ? "true" : "false")
               << ",\"hot_dice\":" << (option.resetsToFullSet ? "true" : "false")
               << ",\"label\":" << quote(option.label) << '}';
    }
    output << "],\"state\":";
    printCheckpointState(output, branch.game);
    output << '}';
}

void printIncumbentSelection(std::ostream& output, const zilch::GameManager& initial,
                             const zilch::Policy& policy, const Config& config)
{
    auto game = initial;
    zilch::ComputerController controller(policy, config.difficultyA, config.collectA, config.featuresA);
    zilch::Checker checker(game);
    std::vector<zilch::ScoringOption> choices;
    auto action = zilch::MatchEntry::RollCurrentTurn;
    for (auto options = checker.availableOptions(); !options.empty(); options = checker.availableOptions()) {
        const auto index = controller.chooseOption(game, options);
        if (index >= options.size())
            throw std::logic_error("Incumbent controller chose an invalid option.");
        choices.push_back(options[index]);
        checker.applyOption(options[index]);
        const auto remaining = checker.availableOptions();
        const auto decision = controller.decideAfterSelection(game, remaining);
        if (decision == zilch::PostSelectionDecision::SelectAgain && !remaining.empty())
            continue;
        action = decision == zilch::PostSelectionDecision::Bank && game.canBankCurrentScore()
            ? zilch::MatchEntry::BankCurrentTurn : zilch::MatchEntry::RollCurrentTurn;
        break;
    }
    std::vector<std::uint16_t> selected;
    for (const auto& [face, originalCount] : initial.currentPlayer().dice().diceSetMap()) {
        const auto& remaining = game.currentPlayer().dice().diceSetMap();
        const auto found = remaining.find(face);
        const auto count = originalCount - (found == remaining.end() ? 0 : found->second);
        selected.insert(selected.end(), static_cast<std::size_t>(count), face);
    }
    printSelectionBranch(output, {game, choices, action}, selected, config.atRisk);
}

std::string run(const Config& config)
{
    const auto policyA = load(config.policyA, config.difficultyA, config.rules.stealingEnabled());
    const auto policyB = load(config.policyB, config.difficultyB, config.rules.stealingEnabled());
    const auto initial = makeState(config);
    const auto left = config.mode == "selection"
        ? std::make_optional(zilch::research::makeSelectionBranch(initial, config.selectLeft, config.actionLeft))
        : std::nullopt;
    const auto right = config.mode == "selection"
        ? std::make_optional(zilch::research::makeSelectionBranch(initial, config.selectRight, config.actionRight))
        : std::nullopt;
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
                zilch::ComputerController firstA(policyA, config.difficultyA, config.collectA, config.featuresA);
                zilch::ComputerController firstB(policyB, config.difficultyB, config.collectB, config.featuresB);
                zilch::ComputerController secondA(policyA, config.difficultyA, config.collectA, config.featuresA);
                zilch::ComputerController secondB(policyB, config.difficultyB, config.collectB, config.featuresB);
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
                } else if (config.mode == "state") {
                    const auto bank = zilch::playMatchFromState(initial, {&firstA, &firstB}, firstRng,
                                                              zilch::MatchEntry::BankCurrentTurn);
                    const auto roll = zilch::playMatchFromState(initial, {&secondA, &secondB}, secondRng,
                                                              zilch::MatchEntry::RollCurrentTurn);
                    const auto bankPoints = aggregate.a.add(bank, 0);
                    const auto rollPoints = aggregate.b.add(roll, 0);
                    aggregate.primary.add(rollPoints - bankPoints);
                    aggregate.margin.add(static_cast<long double>(roll.finalScores[0]) - roll.finalScores[1]
                                         - bank.finalScores[0] + bank.finalScores[1]);
                } else {
                    const auto first = zilch::playMatchFromState(left->game, {&firstA, &firstB}, firstRng, left->action);
                    const auto second = zilch::playMatchFromState(right->game, {&secondA, &secondB}, secondRng, right->action);
                    const auto leftPoints = aggregate.a.add(first, 0);
                    const auto rightPoints = aggregate.b.add(second, 0);
                    aggregate.primary.add(rightPoints - leftPoints);
                    aggregate.margin.add(static_cast<long double>(second.finalScores[0]) - second.finalScores[1]
                                         - first.finalScores[0] + first.finalScores[1]);
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
           << "{\"schema_version\":" << (config.mode == "selection" ? 2 : 1) << ",\"mode\":" << quote(config.mode)
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
    printPolicy(output, policyA, config.policyA, config.difficultyA, config.collectA, config.featuresA);
    output << ",\"policy_b\":";
    printPolicy(output, policyB, config.policyB, config.difficultyB, config.collectB, config.featuresB);
    if (config.mode == "duel") {
        output << ",\"a\":";
        total.a.print(output);
        output << ",\"b\":";
        total.b.print(output);
        output << ",\"a_match_point_rate_paired\":";
        total.primary.print(output);
        output << ",\"a_score_margin_paired\":";
        total.margin.print(output);
    } else if (config.mode == "state") {
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
    } else {
        output << ",\"selection_state\":{\"roll\":";
        printValues(output, config.roll);
        output << ",\"at_risk_before_selection\":" << config.atRisk
               << ",\"state\":";
        printCheckpointState(output, initial);
        output << "},\"branches\":{\"left\":";
        printSelectionBranch(output, *left, config.selectLeft, config.atRisk);
        output << ",\"right\":";
        printSelectionBranch(output, *right, config.selectRight, config.atRisk);
        output << "},\"incumbent_recommendation\":";
        printIncumbentSelection(output, initial, policyA, config);
        output << ",\"left\":";
        total.a.print(output);
        output << ",\"right\":";
        total.b.print(output);
        output << ",\"right_minus_left_match_points_paired\":";
        total.primary.print(output);
        output << ",\"right_minus_left_score_margin_paired\":";
        total.margin.print(output);
    }
    output << "}\n";
    return output.str();
}

void saveResult(const std::filesystem::path& path, const std::string& result)
{
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::out | std::ios::noreplace);
    output << result;
    if (!output)
        throw std::runtime_error("Unable to save research output without overwriting: " + path.string());
}

} // namespace

#ifndef ZILCH_RESEARCH_TESTING
int main(const int argc, const char* const* argv)
{
    try {
        const auto config = parse(argc, argv);
        if (config.output && std::filesystem::exists(*config.output))
            throw std::invalid_argument("Refusing to overwrite existing research evidence: " + *config.output);
        const auto result = run(config);
        if (config.output)
            saveResult(*config.output, result);
        std::cout << result;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Research error: " << error.what() << '\n';
        return 1;
    }
}
#endif
