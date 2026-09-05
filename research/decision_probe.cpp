#include "computer.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Input {
    std::uint32_t turnScore{};
    std::uint32_t ownScore{};
    std::uint32_t opponentScore{};
    std::uint32_t target{};
    std::uint32_t opening{};
    bool stealing{};
    bool finalChase{};
    bool finalChaseActive{};
    bool ties{};
    bool sets{};
    std::vector<std::uint32_t> dice;
    std::vector<std::uint32_t> chainScores;
};

std::uint32_t number(const std::string& token)
{
    std::uint32_t value{};
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || value > 1'000'000)
        throw std::invalid_argument("Expected an integer from 0 through 1000000.");
    return value;
}

bool boolean(const std::string& token)
{
    if (token == "1" || token == "true")
        return true;
    if (token == "0" || token == "false")
        return false;
    throw std::invalid_argument("Expected 0/1 or false/true.");
}

std::vector<std::uint32_t> numbers(const std::string& token)
{
    std::vector<std::uint32_t> values;
    std::size_t offset = 0;
    for (;;) {
        const auto comma = token.find(',', offset);
        values.push_back(number(token.substr(offset, comma == std::string::npos ? comma : comma - offset)));
        if (comma == std::string::npos)
            break;
        offset = comma + 1;
    }
    return values;
}

Input parseInput(const std::string& line)
{
    std::istringstream stream(line);
    std::array<std::string, 12> fields;
    for (auto& field : fields) {
        if (!(stream >> field))
            throw std::invalid_argument("Expected exactly 12 whitespace-separated fields.");
    }
    std::string extra;
    if (stream >> extra)
        throw std::invalid_argument("Expected exactly 12 whitespace-separated fields.");
    Input input{
        number(fields[0]), number(fields[1]), number(fields[2]), number(fields[3]), number(fields[4]),
        boolean(fields[5]), boolean(fields[6]), boolean(fields[7]), boolean(fields[8]), boolean(fields[9]),
        numbers(fields[10]), numbers(fields[11]),
    };
    if (input.target < 1000 || input.opening > input.target)
        throw std::invalid_argument("Target must be at least 1000 and opening must not exceed target.");
    if (input.finalChaseActive && !input.finalChase)
        throw std::invalid_argument("Active Final Chase requires the rule to be enabled.");
    if (input.dice.empty() || input.dice.size() > 6)
        throw std::invalid_argument("Provide between one and six dice.");
    for (const auto die : input.dice) {
        if (die < 1 || die > 6)
            throw std::invalid_argument("Each die value must be from 1 through 6.");
    }
    if (input.chainScores.size() != 6)
        throw std::invalid_argument("Provide six saved multiple scores, ordered by face.");
    return input;
}

struct Decision {
    std::array<std::uint32_t, 6> selected{};
    std::string action;
    std::uint32_t scoreGain{};
    std::uint32_t projectedTurnScore{};
    std::uint16_t nextDice{};
    bool canBank{};
};

Decision decide(const Input& input, const zilch::Policy& policy, const bool collect)
{
    zilch::GameManager game;
    game.setPlayers({"Computer", "Opponent"});
    game.setScoreLimit(input.target);
    game.ruleConfig().setOpeningScoreLimit(input.opening);
    game.ruleConfig().setStealingEnabled(input.stealing);
    game.ruleConfig().setFinalChaseEnabled(input.finalChase);
    game.ruleConfig().setAllowTies(input.ties);
    game.ruleConfig().setThreePairsEnabled(input.sets);
    game.players()[0].score().addPermanentScore(input.ownScore);
    game.players()[1].score().addPermanentScore(input.opponentScore);
    if (input.finalChaseActive) {
        game.startTurn(1);
        game.beginFinalRound();
    }
    game.startTurn(0);
    game.currentPlayer().score().setRoundScore(input.turnScore);
    game.manageDiceCount(static_cast<std::uint16_t>(input.dice.size()));
    auto& liveDice = game.currentPlayer().dice().diceSetMap();
    for (const auto die : input.dice)
        ++liveDice[static_cast<std::uint16_t>(die)];
    for (std::uint16_t face = 1; face <= 6; ++face) {
        if (input.chainScores[face - 1] != 0)
            game.setSavedMultipleScore(face, input.chainScores[face - 1]);
    }
    // This probe starts after a roll and before any scoring selection. It does
    // not simulate bust handling or consume random numbers.
    game.registerRoll();
    const auto originalDice = liveDice;
    zilch::Checker checker(game);
    zilch::ComputerController controller(policy, zilch::ComputerDifficulty::Hard, collect);
    Decision result;
    auto options = checker.availableOptions();
    if (options.empty()) {
        result.action = "Bust";
    } else {
        for (;;) {
            const auto choice = controller.chooseOption(game, options);
            if (choice >= options.size())
                throw std::logic_error("Controller returned an invalid option index.");
            checker.applyOption(options[choice]);
            options = checker.availableOptions();
            const auto action = controller.decideAfterSelection(game, options);
            if (action == zilch::PostSelectionDecision::SelectAgain && !options.empty())
                continue;
            result.action = action == zilch::PostSelectionDecision::Bank && game.canBankCurrentScore()
                                ? "Bank" : "Roll";
            break;
        }
    }
    for (const auto& [face, count] : originalDice) {
        const auto remaining = liveDice.find(face);
        result.selected[face - 1] = count - (remaining == liveDice.end() ? 0 : remaining->second);
    }
    result.projectedTurnScore = game.currentPlayer().score().roundScore();
    result.scoreGain = result.projectedTurnScore - input.turnScore;
    result.nextDice = game.currentPlayer().dice().numDiceInPlay();
    result.canBank = game.canBankCurrentScore();
    return result;
}

void print(const Decision& result, const std::size_t lineNumber)
{
    std::cout << "{\"line\":" << lineNumber << ",\"selected_counts\":[";
    for (std::size_t index = 0; index < result.selected.size(); ++index)
        std::cout << (index == 0 ? "" : ",") << result.selected[index];
    std::cout << "],\"action\":\"" << result.action << "\",\"score_gain\":" << result.scoreGain
              << ",\"projected_turn_score\":" << result.projectedTurnScore
              << ",\"next_dice\":" << result.nextDice
              << ",\"can_bank\":" << (result.canBank ? "true" : "false") << "}\n";
}

void selfTest()
{
    auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    policy.bankThresholdByDice[6] = 5000;
    const auto check = [](const bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    const auto collection = parseInput("2700 0 0 5000 1000 0 1 0 1 1 1,1,5,2,3,4 0,0,0,0,0,0");
    const auto collected = decide(collection, policy, true);
    check(collected.action == "Bank" && collected.projectedTurnScore == 2950 &&
              collected.selected == std::array<std::uint32_t, 6>{2, 0, 0, 0, 1, 0},
          "Collector probe must include every safe scoring die.");
    const auto uncollected = decide(collection, policy, false);
    check(uncollected.action == "Bank" && uncollected.projectedTurnScore == 2800,
          "Explicit collector-off flag must preserve the incumbent selection.");
    const auto hotDice = decide(parseInput("1800 0 0 5000 1000 0 1 0 1 1 2,2,3,3,4,4 0,0,0,0,0,0"), policy, true);
    check(hotDice.action == "Roll" && hotDice.projectedTurnScore == 2800 && hotDice.nextDice == 6,
          "Reported 2800 hot-dice position must roll.");
    const auto opening = decide(parseInput("0 0 0 5000 1000 0 1 0 1 1 1,2,3 0,0,0,0,0,0"), policy, true);
    check(opening.action == "Roll" && !opening.canBank, "Opening minimum must prevent banking.");
    const auto chain = decide(parseInput("200 0 0 5000 0 0 1 0 1 1 2,4,6 0,200,0,0,0,0"), policy, true);
    check(chain.scoreGain == 200 && chain.selected[1] == 1 && chain.nextDice == 2,
          "Saved chain scores must be applied by the actual checker.");
    const auto chase = decide(parseInput("100 5000 5100 5000 1000 0 1 1 1 1 5,2 0,0,0,0,0,0"), policy, true);
    check(chase.action == "Bank", "An active Final Chase must bank an overtaking score.");
    const auto noSets = decide(parseInput("1800 0 0 5000 1000 0 1 0 1 0 2,2,3,3,4,4 0,0,0,0,0,0"), policy, true);
    check(noSets.action == "Bust", "Disabling Sets must remove the three-pairs scoring option.");
    const auto bust = decide(parseInput("250 0 0 5000 1000 0 1 0 1 1 2,3,4 0,0,0,0,0,0"), policy, true);
    check(bust.action == "Bust" && bust.scoreGain == 0, "No scoring options must return Bust.");
    auto commitmentPolicy = policy;
    commitmentPolicy.bankThresholdByDice = {0, 600, 600, 600, 600, 600, 4000};
    commitmentPolicy.scoreWeight = 1;
    commitmentPolicy.remainingDiceWeight = 200;
    commitmentPolicy.hotDiceWeight = -50;
    commitmentPolicy.multipleWeight = -50;
    commitmentPolicy.rollBias = 200;
    commitmentPolicy.leadFactor = 0;
    commitmentPolicy.trailFactor = 0;
    commitmentPolicy.closingFactor = 0;
    const auto commitment = decide(parseInput("600 0 0 5000 0 0 1 0 1 1 1,5,2,2,2 0,0,0,0,0,0"), commitmentPolicy, true);
    check(commitment.action == "Bank" && commitment.projectedTurnScore == 950 && commitment.nextDice == 6,
          "The probe must preserve a bank commitment when final collection produces hot dice.");
    std::cout << "Decision-probe self-test passed.\n";
}

} // namespace

int main(const int argc, const char* const* argv)
{
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            selfTest();
            return 0;
        }
        std::optional<std::string> policyPath;
        std::optional<bool> collect;
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (index + 1 >= argc)
                throw std::invalid_argument("Each option requires a value.");
            const std::string value = argv[++index];
            if (option == "--policy" && !policyPath)
                policyPath = value;
            else if (option == "--collect" && !collect.has_value())
                collect = boolean(value);
            else
                throw std::invalid_argument("Supported options are --policy FILE and --collect true|false, each once.");
        }
        if (!policyPath || !collect.has_value())
            throw std::invalid_argument("Specify --policy FILE and --collect true|false explicitly.");
        zilch::Policy policy;
        if (!zilch::loadPolicy(*policyPath, policy))
            throw std::invalid_argument("Unable to load the requested policy file.");
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(std::cin, line)) {
            ++lineNumber;
            try {
                print(decide(parseInput(line), policy, *collect), lineNumber);
            } catch (const std::exception& error) {
                std::cerr << "Input line " << lineNumber << ": " << error.what() << '\n';
                return 1;
            }
        }
        if (!std::cin.eof())
            throw std::runtime_error("Unable to read the complete input stream.");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Decision probe: " << error.what() << '\n';
        return 1;
    }
}
