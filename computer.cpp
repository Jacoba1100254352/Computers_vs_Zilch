#include "computer.h"
#include "InputClosed.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace zilch {

namespace {

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

Policy defaultPolicy()
{
    return {};
}

void clampPolicy(Policy& policy)
{
    for (std::size_t index = 1; index < policy.bankThresholdByDice.size(); ++index) {
        policy.bankThresholdByDice[index] = std::clamp(policy.bankThresholdByDice[index], 200, 3000);
        if (index > 1) {
            policy.bankThresholdByDice[index] =
                std::max(policy.bankThresholdByDice[index], policy.bankThresholdByDice[index - 1]);
        }
    }

    policy.scoreWeight = std::clamp(policy.scoreWeight, 0.25, 3.0);
    policy.remainingDiceWeight = std::clamp(policy.remainingDiceWeight, 0.0, 200.0);
    policy.hotDiceWeight = std::clamp(policy.hotDiceWeight, -50.0, 500.0);
    policy.multipleWeight = std::clamp(policy.multipleWeight, -50.0, 250.0);
    policy.leadFactor = std::clamp(policy.leadFactor, 0.0, 0.5);
    policy.trailFactor = std::clamp(policy.trailFactor, 0.0, 0.5);
    policy.closingFactor = std::clamp(policy.closingFactor, 0.0, 1.0);
    policy.rollBias = std::clamp(policy.rollBias, -200.0, 200.0);
}

std::string readLineOrThrow(std::istream& input)
{
    std::string line;
    if (!std::getline(input, line))
        throw InputClosed();
    return trim(line);
}

std::string lowercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parseStrictInt(const std::string& value, int& output)
{
    std::size_t parsed = 0;
    output = std::stoi(value, &parsed);
    return parsed == value.size();
}

bool parseStrictDouble(const std::string& value, double& output)
{
    std::size_t parsed = 0;
    output = std::stod(value, &parsed);
    return parsed == value.size() && std::isfinite(output);
}

std::size_t highestScoringOptionIndex(const std::vector<ScoringOption>& options)
{
    const auto best = std::max_element(
        options.begin(),
        options.end(),
        [](const ScoringOption& lhs, const ScoringOption& rhs) {
            if (lhs.scoreGain != rhs.scoreGain)
                return lhs.scoreGain < rhs.scoreGain;
            if (lhs.diceUsed != rhs.diceUsed)
                return lhs.diceUsed < rhs.diceUsed;
            return !lhs.resetsToFullSet && rhs.resetsToFullSet;
        });
    return static_cast<std::size_t>(std::distance(options.begin(), best));
}

void printOptions(std::ostream& output, const std::vector<ScoringOption>& options)
{
    output << "Options:\n";
    for (std::size_t index = 0; index < options.size(); ++index)
        output << "  " << (index + 1) << ". " << options[index].label << '\n';
}

Policy policyFromPath(const std::optional<std::string>& path)
{
    Policy policy = defaultPolicy();
    if (path && !loadPolicy(*path, policy))
        throw std::runtime_error("Failed to load policy from " + *path);
    return policy;
}

void validateMatchConfiguration(const std::uint32_t scoreLimit, const RuleConfig& ruleConfig)
{
    if (scoreLimit < 1000)
        throw std::invalid_argument("Score limit must be at least 1000.");
    if (ruleConfig.openingScoreLimit() > scoreLimit)
        throw std::invalid_argument("Opening score must not exceed the score limit.");
    if (!ruleConfig.hasScoringRuleEnabled())
        throw std::invalid_argument("At least one scoring rule must be enabled.");
}

void recordMatch(
    Trainer::Stats& firstStats,
    Trainer::Stats& secondStats,
    const MatchResult& result,
    const std::size_t firstSeatOwner)
{
    const std::size_t secondSeatOwner = firstSeatOwner == 0 ? 1 : 0;
    const auto firstScore = result.finalScores[firstSeatOwner];
    const auto secondScore = result.finalScores[secondSeatOwner];

    ++firstStats.games;
    ++secondStats.games;
    firstStats.pointsFor += firstScore;
    firstStats.pointsAgainst += secondScore;
    secondStats.pointsFor += secondScore;
    secondStats.pointsAgainst += firstScore;

    if (!result.winnerIndex) {
        firstStats.matchPoints += 0.5;
        secondStats.matchPoints += 0.5;
        ++firstStats.ties;
        ++secondStats.ties;
        return;
    }

    if (*result.winnerIndex == firstSeatOwner) {
        firstStats.matchPoints += 1.0;
        ++firstStats.wins;
    } else {
        secondStats.matchPoints += 1.0;
        ++secondStats.wins;
    }
}

void playTurn(GameManager& game, Controller& controller, std::mt19937& rng, std::ostream* output)
{
    game.startTurn(game.currentIndex());
    auto& player = game.currentPlayer();

    if (output) {
        *output << "\n== " << player.name() << "'s turn ==\n";
        *output << "Scoreboard: " << formatScoreboard(game) << "\n";
    }

    if (game.hasStealOfferForCurrentPlayer()) {
        const auto carriedScore = game.stealOfferScore();
        const auto carriedDice = game.stealOfferDiceCount();

        if (!game.canCurrentPlayerSteal()) {
            if (output) {
                *output << "Stealing is unavailable until " << player.name() << " has banked "
                        << game.ruleConfig().openingScoreLimit() << " points. Starting fresh.\n";
            }
            game.declineStealOffer();
        } else if (controller.decideTurnStart(game) == TurnStartDecision::AcceptSteal &&
                   game.acceptStealOffer()) {
            if (output) {
                *output << player.name() << " accepts the continuation: " << carriedScore
                        << " round points at risk with " << carriedDice << " dice.\n";
            }
        } else {
            game.declineStealOffer();
            if (output)
                *output << player.name() << " declines the continuation and starts with six dice.\n";
        }
    }

    while (game.turnActive()) {
        player.dice().rollDice(rng);
        game.registerRoll();

        Checker checker(game);

        if (output) {
            *output << "Roll: " << formatRoll(player.dice()) << '\n';
            *output << "Round score: " << player.score().roundScore() << '\n';
        }

        if (!checker.hasAvailableOption()) {
            const auto previousRoundScore = player.score().roundScore();
            checker.handleBust();

            if (output) {
                if (game.turnActive() && player.score().roundScore() > previousRoundScore) {
                    *output << "First-roll bust bonus applied. Round score is now "
                            << player.score().roundScore() << ". Rolling again.\n";
                } else {
                    *output << "Bust. The turn ends with 0 round points.\n";
                }
            }
            continue;
        }

        while (game.turnActive()) {
            const auto options = checker.availableOptions();
            if (options.empty())
                break;

            if (output)
                printOptions(*output, options);

            const auto choiceIndex = std::min(controller.chooseOption(game, options), options.size() - 1);
            const auto choice = options[choiceIndex];
            checker.applyOption(choice);

            if (output) {
                *output << "Selected: " << choice.label << '\n';
                *output << "Round score: " << player.score().roundScore() << '\n';
                *output << "Next roll will use " << player.dice().numDiceInPlay() << " dice";
                if (choice.resetsToFullSet)
                    *output << " (hot dice)";
                *output << ".\n";
            }

            const auto remainingOptions = checker.availableOptions();
            const auto decision = controller.decideAfterSelection(game, remainingOptions);

            if (decision == PostSelectionDecision::Bank && game.canBankCurrentScore()) {
                const auto bankedScore = player.score().roundScore();
                game.bankCurrentScore();
                if (output) {
                    *output << player.name() << " banks " << bankedScore << " points.\n";
                    *output << "Scoreboard: " << formatScoreboard(game) << "\n";
                }
                break;
            }

            if (decision == PostSelectionDecision::SelectAgain && !remainingOptions.empty())
                continue;

            break;
        }
    }
}

MatchResult playMatchInternal(
    const std::vector<std::string>& playerNames,
    std::vector<Controller*>& controllers,
    const std::uint32_t scoreLimit,
    const RuleConfig& ruleConfig,
    const std::uint64_t seed,
    std::ostream* output)
{
    GameManager game;
    game.ruleConfig() = ruleConfig;
    game.setPlayers(playerNames);
    game.setScoreLimit(scoreLimit);

    std::mt19937 rng;
    if (seed == 0) {
        std::random_device randomDevice;
        rng.seed(randomDevice());
    } else {
        rng.seed(static_cast<std::mt19937::result_type>(seed));
    }

    while (true) {
        playTurn(game, *controllers[game.currentIndex()], rng, output);

        if (!game.finalRoundActive() && game.currentPlayer().score().permanentScore() >= game.scoreLimit()) {
            if (game.ruleConfig().finalChaseEnabled() && game.playerCount() > 1) {
                game.beginFinalRound();
                if (output) {
                    *output << '\n'
                            << game.currentPlayer().name() << " reached " << game.scoreLimit()
                            << ". Everyone else gets one final turn.\n";
                }
            } else {
                break;
            }
        }

        if (game.finalRoundActive())
            game.updateFinalRoundLeaderForCurrentPlayer();

        if (game.finalRoundActive() && game.wouldEndAfterCurrentTurn())
            break;

        game.switchToNextPlayer();
    }

    MatchResult result;
    result.finalScores.reserve(game.playerCount());
    for (const auto& player : game.players())
        result.finalScores.push_back(player.score().permanentScore());

    result.winningScore = *std::max_element(result.finalScores.begin(), result.finalScores.end());
    const auto winnerCount =
        std::count(result.finalScores.begin(), result.finalScores.end(), result.winningScore);
    if (winnerCount == 1) {
        const auto winner =
            std::distance(result.finalScores.begin(),
                          std::find(result.finalScores.begin(), result.finalScores.end(), result.winningScore));
        result.winnerIndex = static_cast<std::size_t>(winner);
    } else if (!game.ruleConfig().tiesAllowed()) {
        if (game.finalRoundActive()) {
            result.winnerIndex = game.finalRoundLeaderIndex();
        } else {
            const auto winner =
                std::distance(result.finalScores.begin(),
                              std::find(result.finalScores.begin(), result.finalScores.end(), result.winningScore));
            result.winnerIndex = static_cast<std::size_t>(winner);
        }
    }

    if (output) {
        *output << "\nFinal scores:\n";
        for (std::size_t index = 0; index < game.players().size(); ++index)
            *output << "  " << game.players()[index].name() << ": " << result.finalScores[index] << '\n';
        if (result.winnerIndex) {
            *output << game.players()[*result.winnerIndex].name() << " wins.\n";
        } else {
            *output << "The game ends in a tie.\n";
        }
    }

    return result;
}

} // namespace

std::string describePolicy(const Policy& policy)
{
    std::ostringstream output;
    output << policy.name << " | thresholds="
           << policy.bankThresholdByDice[1] << ','
           << policy.bankThresholdByDice[2] << ','
           << policy.bankThresholdByDice[3] << ','
           << policy.bankThresholdByDice[4] << ','
           << policy.bankThresholdByDice[5] << ','
           << policy.bankThresholdByDice[6]
           << " | scoreWeight=" << std::fixed << std::setprecision(2) << policy.scoreWeight
           << " | diceWeight=" << policy.remainingDiceWeight
           << " | hotDice=" << policy.hotDiceWeight
           << " | multiple=" << policy.multipleWeight
           << " | lead=" << policy.leadFactor
           << " | trail=" << policy.trailFactor
           << " | closing=" << policy.closingFactor
           << " | rollBias=" << policy.rollBias;
    return output.str();
}

bool loadPolicy(const std::string& path, Policy& policy)
{
    std::ifstream input(path);
    if (!input)
        return false;

    Policy loaded = defaultPolicy();
    bool parsedName = false;
    bool parsedThresholds = false;
    bool parsedScoreWeight = false;
    bool parsedRemainingDiceWeight = false;
    bool parsedHotDiceWeight = false;
    bool parsedMultipleWeight = false;
    bool parsedLeadFactor = false;
    bool parsedTrailFactor = false;
    bool parsedClosingFactor = false;
    bool parsedRollBias = false;
    std::set<std::string> seenKeys;

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.starts_with('#'))
            continue;

        const auto separator = line.find('=');
        if (separator == std::string::npos)
            return false;

        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key.empty() || !seenKeys.insert(key).second)
            return false;

        try {
            if (key == "name") {
                if (value.empty())
                    return false;
                loaded.name = value;
                parsedName = true;
            } else if (key == "bank_thresholds") {
                std::stringstream parser(value);
                std::string entry;
                std::size_t index = 1;
                while (std::getline(parser, entry, ',')) {
                    if (index >= loaded.bankThresholdByDice.size())
                        return false;
                    int threshold = 0;
                    if (!parseStrictInt(trim(entry), threshold))
                        return false;
                    loaded.bankThresholdByDice[index++] = threshold;
                }
                if (index != loaded.bankThresholdByDice.size())
                    return false;
                parsedThresholds = true;
            } else if (key == "score_weight") {
                parsedScoreWeight = parseStrictDouble(value, loaded.scoreWeight);
            } else if (key == "remaining_dice_weight") {
                parsedRemainingDiceWeight = parseStrictDouble(value, loaded.remainingDiceWeight);
            } else if (key == "hot_dice_weight") {
                parsedHotDiceWeight = parseStrictDouble(value, loaded.hotDiceWeight);
            } else if (key == "multiple_weight") {
                parsedMultipleWeight = parseStrictDouble(value, loaded.multipleWeight);
            } else if (key == "lead_factor") {
                parsedLeadFactor = parseStrictDouble(value, loaded.leadFactor);
            } else if (key == "trail_factor") {
                parsedTrailFactor = parseStrictDouble(value, loaded.trailFactor);
            } else if (key == "closing_factor") {
                parsedClosingFactor = parseStrictDouble(value, loaded.closingFactor);
            } else if (key == "roll_bias") {
                parsedRollBias = parseStrictDouble(value, loaded.rollBias);
            } else {
                return false;
            }
        } catch (const std::exception&) {
            return false;
        }

        if (!parsedScoreWeight && key == "score_weight")
            return false;
        if (!parsedRemainingDiceWeight && key == "remaining_dice_weight")
            return false;
        if (!parsedHotDiceWeight && key == "hot_dice_weight")
            return false;
        if (!parsedMultipleWeight && key == "multiple_weight")
            return false;
        if (!parsedLeadFactor && key == "lead_factor")
            return false;
        if (!parsedTrailFactor && key == "trail_factor")
            return false;
        if (!parsedClosingFactor && key == "closing_factor")
            return false;
        if (!parsedRollBias && key == "roll_bias")
            return false;
    }

    if (!parsedName || !parsedThresholds || !parsedScoreWeight || !parsedRemainingDiceWeight || !parsedHotDiceWeight ||
        !parsedMultipleWeight || !parsedLeadFactor || !parsedTrailFactor || !parsedClosingFactor || !parsedRollBias) {
        return false;
    }

    clampPolicy(loaded);
    policy = loaded;
    return true;
}

bool savePolicy(const std::string& path, const Policy& policy)
{
    const std::filesystem::path filePath(path);
    try {
        if (filePath.has_parent_path())
            std::filesystem::create_directories(filePath.parent_path());
    } catch (const std::exception&) {
        return false;
    }

    std::ofstream output(path);
    if (!output)
        return false;

    output << "name=" << policy.name << '\n';
    output << "bank_thresholds="
           << policy.bankThresholdByDice[1] << ','
           << policy.bankThresholdByDice[2] << ','
           << policy.bankThresholdByDice[3] << ','
           << policy.bankThresholdByDice[4] << ','
           << policy.bankThresholdByDice[5] << ','
           << policy.bankThresholdByDice[6] << '\n';
    output << "score_weight=" << policy.scoreWeight << '\n';
    output << "remaining_dice_weight=" << policy.remainingDiceWeight << '\n';
    output << "hot_dice_weight=" << policy.hotDiceWeight << '\n';
    output << "multiple_weight=" << policy.multipleWeight << '\n';
    output << "lead_factor=" << policy.leadFactor << '\n';
    output << "trail_factor=" << policy.trailFactor << '\n';
    output << "closing_factor=" << policy.closingFactor << '\n';
    output << "roll_bias=" << policy.rollBias << '\n';
    output.flush();
    return static_cast<bool>(output);
}

TurnStartDecision HumanController::decideTurnStart(GameManager& game)
{
    while (true) {
        output_ << "Steal " << game.stealOfferScore() << " round points with "
                << game.stealOfferDiceCount() << " dice [s=steal, f=fresh]: " << std::flush;
        const auto normalized = lowercase(readLineOrThrow(input_));

        if (normalized == "s" || normalized == "steal" || normalized == "y" || normalized == "yes")
            return TurnStartDecision::AcceptSteal;
        if (normalized == "f" || normalized == "fresh" || normalized == "n" || normalized == "no")
            return TurnStartDecision::FreshRoll;

        output_ << "Invalid turn-start action.\n";
    }
}

std::size_t HumanController::chooseOption(GameManager&, const std::vector<ScoringOption>& options)
{
    if (autoScoreRemaining_)
        return highestScoringOptionIndex(options);

    while (true) {
        output_ << "Choose an option [1-" << options.size() << "]: " << std::flush;
        const auto line = readLineOrThrow(input_);
        const auto normalized = lowercase(line);

        if (normalized == "all" || normalized == "a") {
            autoScoreRemaining_ = true;
            return highestScoringOptionIndex(options);
        }

        if (normalized == "?") {
            printOptions(output_, options);
            continue;
        }

        try {
            const auto choice = static_cast<std::size_t>(std::stoul(line));
            if (choice >= 1 && choice <= options.size())
                return choice - 1;
        } catch (const std::exception&) {
        }
        output_ << "Invalid option.\n";
    }
}

PostSelectionDecision HumanController::decideAfterSelection(
    GameManager& game,
    const std::vector<ScoringOption>& remainingOptions)
{
    const bool canBank = game.canBankCurrentScore();

    if (autoScoreRemaining_) {
        if (!remainingOptions.empty())
            return PostSelectionDecision::SelectAgain;
        autoScoreRemaining_ = false;
    }

    if (!canBank && remainingOptions.empty())
        return PostSelectionDecision::Roll;

    while (true) {
        output_ << "Next action [";
        if (!remainingOptions.empty())
            output_ << "s=score more, ";
        output_ << "r=roll";
        if (canBank)
            output_ << ", b=bank";
        output_ << "]: " << std::flush;

        const auto line = readLineOrThrow(input_);
        if (line.empty())
            continue;

        switch (static_cast<char>(std::tolower(line.front()))) {
        case 's':
            if (!remainingOptions.empty())
                return PostSelectionDecision::SelectAgain;
            break;
        case 'r':
            return PostSelectionDecision::Roll;
        case 'b':
            if (canBank)
                return PostSelectionDecision::Bank;
            break;
        default:
            break;
        }

        output_ << "Invalid action.\n";
    }
}

TurnStartDecision ComputerController::decideTurnStart(GameManager& game)
{
    const auto continuationUtility =
        policy_.scoreWeight * static_cast<double>(game.stealOfferScore()) +
        policy_.remainingDiceWeight * static_cast<double>(game.stealOfferDiceCount()) +
        policy_.rollBias;
    const auto freshRollUtility =
        policy_.remainingDiceWeight * static_cast<double>(FULL_SET_OF_DICE);

    return continuationUtility >= freshRollUtility ? TurnStartDecision::AcceptSteal :
                                                     TurnStartDecision::FreshRoll;
}

double ComputerController::optionUtility(const GameManager&, const ScoringOption& option) const
{
    double utility = policy_.scoreWeight * static_cast<double>(option.scoreGain);
    utility += policy_.remainingDiceWeight * static_cast<double>(option.nextDiceCount);
    if (option.resetsToFullSet)
        utility += policy_.hotDiceWeight;
    if (option.type == OptionType::Multiple)
        utility += policy_.multipleWeight;
    if (option.extendsMultiple)
        utility += policy_.multipleWeight * 0.5;
    return utility;
}

double ComputerController::rollUtility(const GameManager& game) const
{
    return policy_.rollBias +
           policy_.remainingDiceWeight * static_cast<double>(game.currentPlayer().dice().numDiceInPlay());
}

int ComputerController::bankThreshold(const GameManager& game) const
{
    const auto nextDiceCount = game.currentPlayer().dice().numDiceInPlay();
    auto threshold = policy_.bankThresholdByDice[nextDiceCount];

    const auto playerScore = game.currentPlayer().score().permanentScore();
    const auto lead = static_cast<int>(playerScore) -
                      static_cast<int>(maxOpponentScore(game, game.currentIndex()));

    if (lead > 0)
        threshold -= static_cast<int>(static_cast<double>(lead) * policy_.leadFactor);
    else
        threshold += static_cast<int>(static_cast<double>(-lead) * policy_.trailFactor);

    const auto distanceToWin = static_cast<int>(game.scoreLimit()) -
                               static_cast<int>(playerScore + game.currentPlayer().score().roundScore());
    const auto closingWindow = std::max(0, 1500 - std::max(distanceToWin, 0));
    threshold -= static_cast<int>(static_cast<double>(closingWindow) * policy_.closingFactor);

    return std::clamp(threshold, 200, static_cast<int>(game.scoreLimit()));
}

std::size_t ComputerController::chooseOption(GameManager& game, const std::vector<ScoringOption>& options)
{
    std::size_t bestIndex = 0;
    auto bestUtility = optionUtility(game, options.front());

    for (std::size_t index = 1; index < options.size(); ++index) {
        const auto utility = optionUtility(game, options[index]);
        if (utility > bestUtility) {
            bestUtility = utility;
            bestIndex = index;
        }
    }

    return bestIndex;
}

PostSelectionDecision ComputerController::decideAfterSelection(
    GameManager& game,
    const std::vector<ScoringOption>& remainingOptions)
{
    if (!remainingOptions.empty()) {
        auto bestContinuationUtility = optionUtility(game, remainingOptions.front());
        for (std::size_t index = 1; index < remainingOptions.size(); ++index)
            bestContinuationUtility = std::max(bestContinuationUtility, optionUtility(game, remainingOptions[index]));

        if (bestContinuationUtility >= rollUtility(game))
            return PostSelectionDecision::SelectAgain;
    }

    if (game.canBankCurrentScore() &&
        static_cast<int>(game.currentPlayer().score().roundScore()) >= bankThreshold(game)) {
        return PostSelectionDecision::Bank;
    }

    return PostSelectionDecision::Roll;
}

bool runHumanVsComputer(const PlayConfig& config)
{
    try {
        validateMatchConfiguration(config.scoreLimit, config.ruleConfig);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return false;
    }

    Policy policy = defaultPolicy();
    std::optional<std::string> loadedPath = config.policyPath;

    if (!loadedPath && std::filesystem::exists("trained_policy.cfg"))
        loadedPath = "trained_policy.cfg";

    if (loadedPath && !loadPolicy(*loadedPath, policy)) {
        std::cerr << "Failed to load policy from " << *loadedPath << '\n';
        return false;
    }

    if (loadedPath)
        std::cout << "Using computer policy from " << *loadedPath << '\n';
    else
        std::cout << "Using built-in computer policy\n";

    std::cout << describePolicy(policy) << "\n";

    HumanController human(std::cin, std::cout);
    ComputerController computer(policy);

    std::vector<Controller*> controllers{&human, &computer};
    const std::vector<std::string> names{config.humanName, "Computer"};

    try {
        playMatchInternal(names, controllers, config.scoreLimit, config.ruleConfig, config.seed, &std::cout);
    } catch (const InputClosed& exception) {
        std::cout << '\n' << exception.what() << ". Exiting Zilch.\n";
        return true;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return false;
    }

    return true;
}

bool runArena(const ArenaConfig& config)
{
    if (config.games < 2 || config.games % 2 != 0) {
        std::cerr << "Arena games must be an even number of at least 2 because evaluation uses mirrored pairs.\n";
        return false;
    }
    try {
        validateMatchConfiguration(config.scoreLimit, config.ruleConfig);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return false;
    }

    Policy policyA;
    Policy policyB;

    try {
        policyA = policyFromPath(config.policyAPath);
        policyB = policyFromPath(config.policyBPath);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return false;
    }

    const auto threadCount = config.threads == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency()) :
                                                   config.threads;
    const auto seriesCount = config.games / 2;
    std::mt19937_64 seedRng(config.seed == 0 ? std::random_device{}() : config.seed);
    std::vector<std::uint64_t> seriesSeeds;
    seriesSeeds.reserve(seriesCount);
    for (std::size_t index = 0; index < seriesCount; ++index)
        seriesSeeds.push_back(seedRng());

    struct Aggregate {
        Trainer::Stats first;
        Trainer::Stats second;
    };

    std::atomic<std::size_t> nextSeries{0};
    std::vector<Aggregate> workerStats(threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (std::size_t workerIndex = 0; workerIndex < threadCount; ++workerIndex) {
        workers.emplace_back([&, workerIndex]() {
            while (true) {
                const auto seriesIndex = nextSeries.fetch_add(1);
                if (seriesIndex >= seriesSeeds.size())
                    break;

                ComputerController firstController(policyA);
                ComputerController secondController(policyB);
                std::vector<Controller*> controllers{&firstController, &secondController};

                const auto firstSeat = playMatchInternal(
                    {"Policy A", "Policy B"},
                    controllers,
                    config.scoreLimit,
                    config.ruleConfig,
                    seriesSeeds[seriesIndex],
                    nullptr);

                ComputerController swappedFirst(policyB);
                ComputerController swappedSecond(policyA);
                std::vector<Controller*> swappedControllers{&swappedFirst, &swappedSecond};
                const auto secondSeat = playMatchInternal(
                    {"Policy B", "Policy A"},
                    swappedControllers,
                    config.scoreLimit,
                    config.ruleConfig,
                    seriesSeeds[seriesIndex],
                    nullptr);

                recordMatch(workerStats[workerIndex].first, workerStats[workerIndex].second, firstSeat, 0);
                recordMatch(workerStats[workerIndex].first, workerStats[workerIndex].second, secondSeat, 1);
            }
        });
    }

    for (auto& worker : workers)
        worker.join();

    Aggregate totals;
    for (const auto& worker : workerStats) {
        totals.first.matchPoints += worker.first.matchPoints;
        totals.first.wins += worker.first.wins;
        totals.first.ties += worker.first.ties;
        totals.first.games += worker.first.games;
        totals.first.pointsFor += worker.first.pointsFor;
        totals.first.pointsAgainst += worker.first.pointsAgainst;

        totals.second.matchPoints += worker.second.matchPoints;
        totals.second.wins += worker.second.wins;
        totals.second.ties += worker.second.ties;
        totals.second.games += worker.second.games;
        totals.second.pointsFor += worker.second.pointsFor;
        totals.second.pointsAgainst += worker.second.pointsAgainst;
    }

    const auto winRateA =
        totals.first.games == 0 ? 0.0 : totals.first.matchPoints / static_cast<double>(totals.first.games);
    const auto averageMarginA =
        totals.first.games == 0 ? 0.0 :
                                  (static_cast<double>(totals.first.pointsFor) -
                                   static_cast<double>(totals.first.pointsAgainst)) /
                                      static_cast<double>(totals.first.games);
    const auto averageScoreA =
        totals.first.games == 0 ? 0.0 : static_cast<double>(totals.first.pointsFor) /
                                      static_cast<double>(totals.first.games);
    const auto averageScoreB =
        totals.second.games == 0 ? 0.0 : static_cast<double>(totals.second.pointsFor) /
                                       static_cast<double>(totals.second.games);

    std::cout << "Policy A: " << describePolicy(policyA) << "\n";
    std::cout << "Policy B: " << describePolicy(policyB) << "\n";
    std::cout << "Games: " << totals.first.games << " (" << seriesCount << " mirrored series)\n";
    std::cout << "Policy A win rate: " << std::fixed << std::setprecision(3) << winRateA
              << " | wins=" << totals.first.wins
              << " | ties=" << totals.first.ties
              << " | avg score=" << std::setprecision(1) << averageScoreA
              << " | avg margin=" << averageMarginA << "\n";
    std::cout << "Policy B win rate: " << std::fixed << std::setprecision(3)
              << (totals.second.games == 0 ? 0.0 :
                  totals.second.matchPoints / static_cast<double>(totals.second.games))
              << " | wins=" << totals.second.wins
              << " | ties=" << totals.second.ties
              << " | avg score=" << std::setprecision(1) << averageScoreB
              << " | avg margin=" << -averageMarginA << "\n";

    return true;
}

Trainer::Trainer(TrainingConfig config)
    : config_(std::move(config)),
      seedRng_(config_.seed == 0 ? std::random_device{}() : config_.seed)
{
    if (config_.generations == 0)
        throw std::invalid_argument("Training generations must be at least 1.");
    if (config_.population < 4)
        throw std::invalid_argument("Training population must be at least 4.");
    if (config_.matchesPerGeneration < config_.population * 2 || config_.matchesPerGeneration % 2 != 0) {
        throw std::invalid_argument(
            "Training matches must be even and at least twice as large as the population.");
    }
    validateMatchConfiguration(config_.scoreLimit, config_.ruleConfig);

    if (config_.threads == 0) {
        config_.threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
}

std::vector<Trainer::Candidate> Trainer::buildInitialPopulation()
{
    Policy seedPolicy = defaultPolicy();
    if (config_.resumePolicyPath) {
        Policy loaded;
        if (!loadPolicy(*config_.resumePolicyPath, loaded))
            throw std::runtime_error("Failed to load resume policy from " + *config_.resumePolicyPath);
        seedPolicy = loaded;
    }

    std::vector<Candidate> population;
    population.reserve(config_.population);

    seedPolicy.name = "seed";
    population.push_back({seedPolicy, {}, 0.0});
    for (std::size_t index = 1; index < config_.population; ++index)
        population.push_back({mutate(seedPolicy, 0, index), {}, 0.0});

    return population;
}

MatchResult Trainer::playComputerMatch(const Policy& first, const Policy& second, const std::uint64_t seed) const
{
    ComputerController firstController(first);
    ComputerController secondController(second);
    std::vector<Controller*> controllers{&firstController, &secondController};
    return playMatchInternal(
        {"Policy A", "Policy B"}, controllers, config_.scoreLimit, config_.ruleConfig, seed, nullptr);
}

Trainer::SeriesResult Trainer::playMirroredSeries(
    const Policy& first,
    const Policy& second,
    const std::uint64_t seed) const
{
    return {
        playComputerMatch(first, second, seed),
        playComputerMatch(second, first, seed),
    };
}

void Trainer::evaluatePopulation(std::vector<Candidate>& population)
{
    for (auto& candidate : population) {
        candidate.stats = {};
        candidate.fitness = 0.0;
    }

    struct Job {
        std::size_t first;
        std::size_t second;
        std::uint64_t seed;
    };

    std::vector<Job> jobs;
    jobs.reserve(std::max<std::size_t>(population.size(), (config_.matchesPerGeneration + 1) / 2));

    std::vector<std::pair<std::size_t, std::size_t>> pairings;
    pairings.reserve((population.size() * (population.size() - 1)) / 2);
    for (std::size_t first = 0; first < population.size(); ++first)
        for (std::size_t second = first + 1; second < population.size(); ++second)
            pairings.emplace_back(first, second);

    std::shuffle(pairings.begin(), pairings.end(), seedRng_);

    const auto targetSeries = std::max<std::size_t>(population.size(), (config_.matchesPerGeneration + 1) / 2);
    while (jobs.size() < targetSeries) {
        for (const auto& [first, second] : pairings) {
            jobs.push_back({first, second, seedRng_()});
            if (jobs.size() >= targetSeries)
                break;
        }
        std::shuffle(pairings.begin(), pairings.end(), seedRng_);
    }

    std::atomic<std::size_t> nextJob{0};
    std::vector<std::vector<Stats>> localStats(
        config_.threads,
        std::vector<Stats>(population.size()));
    std::vector<std::thread> workers;
    workers.reserve(config_.threads);

    for (std::size_t workerIndex = 0; workerIndex < config_.threads; ++workerIndex) {
        workers.emplace_back([&, workerIndex]() {
            while (true) {
                const auto jobIndex = nextJob.fetch_add(1);
                if (jobIndex >= jobs.size())
                    break;

                const auto& job = jobs[jobIndex];
                const auto results = playMirroredSeries(
                    population[job.first].policy,
                    population[job.second].policy,
                    job.seed);

                auto& firstStats = localStats[workerIndex][job.first];
                auto& secondStats = localStats[workerIndex][job.second];
                recordMatch(firstStats, secondStats, results.firstSeat, 0);
                recordMatch(firstStats, secondStats, results.secondSeat, 1);
            }
        });
    }

    for (auto& worker : workers)
        worker.join();

    for (const auto& workerStats : localStats) {
        for (std::size_t index = 0; index < population.size(); ++index) {
            population[index].stats.matchPoints += workerStats[index].matchPoints;
            population[index].stats.wins += workerStats[index].wins;
            population[index].stats.ties += workerStats[index].ties;
            population[index].stats.games += workerStats[index].games;
            population[index].stats.pointsFor += workerStats[index].pointsFor;
            population[index].stats.pointsAgainst += workerStats[index].pointsAgainst;
        }
    }

    for (auto& candidate : population)
        candidate.fitness = computeFitness(candidate.stats);
}

Policy Trainer::mutate(const Policy& parent, const std::size_t generation, const std::size_t index)
{
    Policy child = parent;

    std::normal_distribution<double> thresholdNoise(0.0, 120.0);
    std::normal_distribution<double> weightNoise(0.0, 15.0);
    std::normal_distribution<double> factorNoise(0.0, 0.03);

    for (std::size_t thresholdIndex = 1; thresholdIndex < child.bankThresholdByDice.size(); ++thresholdIndex)
        child.bankThresholdByDice[thresholdIndex] += static_cast<int>(std::lround(thresholdNoise(seedRng_)));

    child.scoreWeight += factorNoise(seedRng_);
    child.remainingDiceWeight += weightNoise(seedRng_);
    child.hotDiceWeight += weightNoise(seedRng_) * 2.0;
    child.multipleWeight += weightNoise(seedRng_);
    child.leadFactor += factorNoise(seedRng_);
    child.trailFactor += factorNoise(seedRng_);
    child.closingFactor += factorNoise(seedRng_);
    child.rollBias += weightNoise(seedRng_);

    child.name = "g" + std::to_string(generation) + "_m" + std::to_string(index);
    clampPolicy(child);
    return child;
}

Policy Trainer::crossover(const Policy& lhs, const Policy& rhs, const std::size_t generation, const std::size_t index)
{
    Policy child = lhs;
    std::bernoulli_distribution pickSide(0.5);

    for (std::size_t thresholdIndex = 1; thresholdIndex < child.bankThresholdByDice.size(); ++thresholdIndex)
        child.bankThresholdByDice[thresholdIndex] =
            pickSide(seedRng_) ? lhs.bankThresholdByDice[thresholdIndex] : rhs.bankThresholdByDice[thresholdIndex];

    child.scoreWeight = pickSide(seedRng_) ? lhs.scoreWeight : rhs.scoreWeight;
    child.remainingDiceWeight = pickSide(seedRng_) ? lhs.remainingDiceWeight : rhs.remainingDiceWeight;
    child.hotDiceWeight = pickSide(seedRng_) ? lhs.hotDiceWeight : rhs.hotDiceWeight;
    child.multipleWeight = pickSide(seedRng_) ? lhs.multipleWeight : rhs.multipleWeight;
    child.leadFactor = pickSide(seedRng_) ? lhs.leadFactor : rhs.leadFactor;
    child.trailFactor = pickSide(seedRng_) ? lhs.trailFactor : rhs.trailFactor;
    child.closingFactor = pickSide(seedRng_) ? lhs.closingFactor : rhs.closingFactor;
    child.rollBias = pickSide(seedRng_) ? lhs.rollBias : rhs.rollBias;

    child.name = "g" + std::to_string(generation) + "_c" + std::to_string(index);
    clampPolicy(child);
    return child;
}

double Trainer::computeFitness(const Stats& stats)
{
    if (stats.games == 0)
        return 0.0;

    const auto winRate = stats.matchPoints / static_cast<double>(stats.games);
    const auto averageMargin =
        (static_cast<double>(stats.pointsFor) - static_cast<double>(stats.pointsAgainst)) /
        static_cast<double>(stats.games);
    return winRate * 10000.0 + averageMargin;
}

Policy Trainer::train(std::ostream& output)
{
    auto population = buildInitialPopulation();
    Policy bestOverall = population.front().policy;
    double bestFitness = std::numeric_limits<double>::lowest();

    for (std::size_t generation = 0; generation < config_.generations; ++generation) {
        const auto startedAt = std::chrono::steady_clock::now();
        evaluatePopulation(population);

        std::sort(
            population.begin(),
            population.end(),
            [](const Candidate& lhs, const Candidate& rhs) { return lhs.fitness > rhs.fitness; });

        if (population.front().fitness > bestFitness) {
            bestFitness = population.front().fitness;
            bestOverall = population.front().policy;
            bestOverall.name = "best";
            if (!savePolicy(config_.outputPath, bestOverall))
                throw std::runtime_error("Failed to save policy to " + config_.outputPath);
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
        const auto& best = population.front();
        const auto bestWinRate = best.stats.games == 0 ? 0.0 :
                                                       best.stats.matchPoints / static_cast<double>(best.stats.games);

        output << "Generation " << (generation + 1) << '/' << config_.generations
               << " | best fitness=" << std::fixed << std::setprecision(2) << best.fitness
               << " | win rate=" << std::setprecision(3) << bestWinRate
               << " | elapsed=" << elapsed.count() << "ms\n";
        output << "  " << describePolicy(best.policy) << "\n";

        if (generation + 1 == config_.generations)
            break;

        const auto survivorCount = std::max<std::size_t>(2, population.size() / 4);
        std::vector<Candidate> nextGeneration;
        nextGeneration.reserve(population.size());

        nextGeneration.push_back(population[0]);
        nextGeneration.push_back(population[1]);

        std::uniform_int_distribution<std::size_t> survivorDist(0, survivorCount - 1);
        while (nextGeneration.size() < population.size()) {
            if (nextGeneration.size() % 2 == 0) {
                const auto& parent = population[survivorDist(seedRng_)].policy;
                nextGeneration.push_back({mutate(parent, generation + 1, nextGeneration.size()), {}, 0.0});
            } else {
                const auto& first = population[survivorDist(seedRng_)].policy;
                const auto& second = population[survivorDist(seedRng_)].policy;
                nextGeneration.push_back(
                    {crossover(first, second, generation + 1, nextGeneration.size()), {}, 0.0});
            }
        }

        population = std::move(nextGeneration);
    }

    if (!savePolicy(config_.outputPath, bestOverall))
        throw std::runtime_error("Failed to save policy to " + config_.outputPath);
    output << "Saved best policy to " << config_.outputPath << "\n";
    return bestOverall;
}

} // namespace zilch
