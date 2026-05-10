#include "computer.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(const bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Callable>
void expectThrows(Callable&& callable, const std::string& message)
{
    try {
        callable();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected exception: " + message);
}

zilch::GameManager makeGame()
{
    zilch::GameManager game;
    game.setPlayers({"A", "B"});
    game.startTurn(0);
    return game;
}

void setDice(zilch::GameManager& game, const std::initializer_list<std::uint16_t> dice)
{
    auto& playerDice = game.currentPlayer().dice();
    playerDice.diceSetMap().clear();
    playerDice.setNumDiceInPlay(static_cast<std::uint16_t>(dice.size()));
    for (const auto die : dice)
        ++playerDice.diceSetMap()[die];
}

const zilch::ScoringOption* findOption(
    const std::vector<zilch::ScoringOption>& options,
    const zilch::OptionType type,
    const std::uint16_t dieValue = 0)
{
    for (const auto& option : options) {
        if (option.type == type && (dieValue == 0 || option.dieValue == dieValue))
            return &option;
    }
    return nullptr;
}

void writePolicyFile(const std::filesystem::path& path, const std::string& body)
{
    std::ofstream output(path);
    output << body;
}

void testScoringOptionsAndRuleToggles()
{
    auto game = makeGame();
    zilch::Checker checker(game);

    setDice(game, {1, 2, 3, 4, 5, 6});
    auto options = checker.availableOptions();
    const auto* straight = findOption(options, zilch::OptionType::Straight);
    expect(straight && straight->scoreGain == 1000 && straight->resetsToFullSet, "straight should score 1000");

    game.ruleConfig().setStraitsEnabled(false);
    options = checker.availableOptions();
    expect(findOption(options, zilch::OptionType::Straight) == nullptr, "disabled straights should be unavailable");
    game.ruleConfig().setStraitsEnabled(true);

    setDice(game, {1, 1, 2, 2, 3, 3});
    options = checker.availableOptions();
    const auto* threePairs = findOption(options, zilch::OptionType::ThreePairs);
    expect(threePairs && threePairs->scoreGain == 1000, "three pairs should score 1000");

    game.ruleConfig().setThreePairsEnabled(false);
    options = checker.availableOptions();
    expect(findOption(options, zilch::OptionType::ThreePairs) == nullptr, "disabled three pairs should be unavailable");
    game.ruleConfig().setThreePairsEnabled(true);

    setDice(game, {2, 2, 2, 2, 5, 6});
    options = checker.availableOptions();
    const auto* fourTwos = findOption(options, zilch::OptionType::Multiple, 2);
    expect(fourTwos && fourTwos->diceUsed == 4 && fourTwos->scoreGain == 400, "four twos should score 400");
    expect(findOption(options, zilch::OptionType::Single, 5) != nullptr, "single five should be available");

    game.ruleConfig().setMultiplesEnabled(false);
    options = checker.availableOptions();
    expect(findOption(options, zilch::OptionType::Multiple, 2) == nullptr, "disabled multiples should be unavailable");

    game.ruleConfig().setSinglesEnabled(false);
    options = checker.availableOptions();
    expect(options.empty(), "disabled multiples and singles should leave no options for this hand");
}

void testMultipleExtensionAndHotDiceReset()
{
    auto game = makeGame();
    zilch::Checker checker(game);

    setDice(game, {1, 1, 1, 2, 3, 4});
    auto options = checker.availableOptions();
    const auto* tripleOnes = findOption(options, zilch::OptionType::Multiple, 1);
    expect(tripleOnes && tripleOnes->scoreGain == 1000, "triple ones should score 1000");
    checker.applyOption(*tripleOnes);
    expect(game.hasSavedMultiple(1), "scored multiple should be saved for extension");

    setDice(game, {1, 5, 2});
    options = checker.availableOptions();
    const auto* extension = findOption(options, zilch::OptionType::Multiple, 1);
    expect(extension && extension->extendsMultiple && extension->scoreGain == 1000, "single one should extend saved ones");
    checker.applyOption(*extension);
    expect(game.savedMultipleScore(1) == 2000, "saved multiple score should double after one extension");

    setDice(game, {1, 1});
    options = checker.availableOptions();
    const auto* hotDiceOnes = findOption(options, zilch::OptionType::Multiple, 1);
    expect(hotDiceOnes && hotDiceOnes->extendsMultiple && hotDiceOnes->resetsToFullSet, "remaining ones should reset to hot dice");
    checker.applyOption(*hotDiceOnes);
    expect(game.currentPlayer().dice().numDiceInPlay() == zilch::FULL_SET_OF_DICE, "hot dice should restore six dice");
    expect(!game.hasSavedMultiple(1), "hot dice should clear saved multiple chains");

    setDice(game, {1, 2, 2, 3, 3, 4});
    options = checker.availableOptions();
    expect(options.size() == 1, "post-hot-dice single one hand should have one option");
    expect(options[0].type == zilch::OptionType::Single && options[0].scoreGain == 100, "single one should not extend old chain");
}

void testSinglesMultiplesAndHotDiceEdges()
{
    {
        auto game = makeGame();
        zilch::Checker checker(game);

        setDice(game, {1, 1, 1, 5});
        const auto options = checker.availableOptions();
        expect(findOption(options, zilch::OptionType::Multiple, 1) != nullptr, "triple ones should expose a multiple option");
        expect(findOption(options, zilch::OptionType::Single, 1) == nullptr, "triple ones should not masquerade as a single option");

        const auto* singleFive = findOption(options, zilch::OptionType::Single, 5);
        expect(singleFive != nullptr, "standalone five should remain scoreable beside a triple");
        checker.applyOption(*singleFive);
        expect(game.currentPlayer().score().roundScore() == 50, "single five should score exactly 50");
        expect(game.currentPlayer().dice().numDiceInPlay() == 3, "single five should remove one die");
        expect(game.currentPlayer().dice().diceSetMap().count(5) == 0, "single five should remove only the five");
        expect(game.currentPlayer().dice().diceSetMap().at(1) == 3, "single five should leave the triple ones intact");
    }

    {
        auto game = makeGame();
        zilch::Checker checker(game);

        setDice(game, {1, 1});
        const auto options = checker.availableOptions();
        const auto* singleOne = findOption(options, zilch::OptionType::Single, 1);
        expect(singleOne != nullptr, "paired ones should expose a single-one option");
        checker.applyOption(*singleOne);
        expect(game.currentPlayer().score().roundScore() == 100, "single one should score exactly 100");
        expect(game.currentPlayer().dice().numDiceInPlay() == 1, "single one should remove exactly one die");
        expect(game.currentPlayer().dice().diceSetMap().at(1) == 1, "one die of value one should remain");
    }

    {
        auto game = makeGame();
        zilch::Checker checker(game);

        setDice(game, {1, 1, 1, 1});
        const auto options = checker.availableOptions();
        const auto* fourOnes = findOption(options, zilch::OptionType::Multiple, 1);
        expect(fourOnes != nullptr, "four ones should expose a multiple option");
        checker.applyOption(*fourOnes);
        expect(game.currentPlayer().score().roundScore() == 2000, "four ones should double the base one multiple");
        expect(game.currentPlayer().dice().numDiceInPlay() == zilch::FULL_SET_OF_DICE, "scoring every die should reset to hot dice");
        expect(game.currentPlayer().dice().diceSetMap().empty(), "all ones should be consumed");
        expect(!game.hasSavedMultiple(1), "hot dice should clear the completed multiple chain");
    }
}

void testBustAndBankingRules()
{
    auto game = makeGame();
    zilch::Checker checker(game);

    game.registerRoll();
    setDice(game, {2, 2, 3, 3, 4, 6});
    expect(!checker.hasAvailableOption(), "non-scoring hand should have no options");
    checker.handleBust();
    expect(game.turnActive(), "first-roll bust bonus should keep turn active");
    expect(game.currentPlayer().score().roundScore() == 50, "first-roll bust should add 50");
    expect(game.bustBonusUsedThisTurn(), "first-roll bust bonus should be marked used");
    expect(!game.hasRolledThisTurn(), "first-roll bust should reset rolled status");

    game.registerRoll();
    setDice(game, {2, 2, 3, 3, 4, 6});
    checker.handleBust();
    expect(!game.turnActive(), "second bust should end turn");
    expect(game.bustPending(), "bust should be pending after turn-ending bust");
    expect(game.currentPlayer().score().roundScore() == 0, "turn-ending bust should clear round score");

    game.startTurn(0);
    game.ruleConfig().setFirstRollBustBonusEnabled(false);
    game.registerRoll();
    setDice(game, {2, 2, 3, 3, 4, 6});
    checker.handleBust();
    expect(!game.turnActive(), "disabled first-roll bust bonus should end turn");

    game.startTurn(0);
    game.registerRoll();
    setDice(game, {1, 1, 1, 2, 3, 4});
    const auto options = checker.availableOptions();
    const auto* tripleOnes = findOption(options, zilch::OptionType::Multiple, 1);
    expect(tripleOnes != nullptr, "triple ones should be bankable after scoring");
    checker.applyOption(*tripleOnes);
    expect(game.canBankCurrentScore(), "1000 round points should meet default bank threshold");
    game.bankCurrentScore();
    expect(!game.turnActive(), "banking should end turn");
    expect(game.currentPlayer().score().permanentScore() == 1000, "banking should add permanent score");
    expect(game.currentPlayer().score().roundScore() == 0, "banking should clear round score");
}

void testGameStateResetAndWinnerLookup()
{
    zilch::GameManager emptyGame;
    expect(emptyGame.highestScoringPlayer() == nullptr, "empty game should have no highest-scoring player");
    emptyGame.manageDiceCount(3);
    expect(!emptyGame.canBankCurrentScore(), "empty game should not be bankable after dice management");

    zilch::GameManager game;
    game.setPlayers({"A", "B", "C"});
    game.players()[0].score().addPermanentScore(700);
    game.players()[1].score().addPermanentScore(1200);
    game.players()[2].score().addPermanentScore(900);
    const auto* highest = game.highestScoringPlayer();
    expect(highest != nullptr && highest->name() == "B", "highestScoringPlayer should return the leader");

    game.startTurn(5);
    expect(game.currentIndex() == 2, "startTurn should wrap out-of-range indexes");
    game.switchToNextPlayer();
    expect(game.currentIndex() == 0, "switchToNextPlayer should wrap to the first player");

    game.startTurn(1);
    game.beginFinalRound();
    game.startTurn(2);
    expect(!game.wouldEndAfterCurrentTurn(), "middle final-round player should not end the round");
    game.startTurn(0);
    expect(game.wouldEndAfterCurrentTurn(), "last player before the starter should end the final round");

    game.setSavedMultipleScore(5, 400);
    game.setPlayers({"Only"});
    expect(game.playerCount() == 1, "resetting players should install the new roster");
    expect(game.currentIndex() == 0, "resetting players should reset the current index");
    expect(!game.finalRoundActive(), "resetting players should clear final-round state");
    expect(!game.turnActive(), "resetting players should clear turn state");
    expect(!game.hasSavedMultiple(5), "resetting players should clear saved multiple chains");

    game.setPlayers({});
    expect(game.playerCount() == 0, "empty player reset should clear players");
    expect(game.highestScoringPlayer() == nullptr, "empty player reset should leave no leader");
    game.manageDiceCount(4);
    expect(!game.canBankCurrentScore(), "managing dice without players should remain safe");
}

void testRuleConfigAndDiceBasics()
{
    zilch::RuleConfig config;
    expect(config.bankThreshold() == 1000, "default bank threshold should be 1000");
    config.adjustBankThreshold(-2000);
    expect(config.bankThreshold() == 0, "bank threshold should not underflow below zero");
    config.adjustBankThreshold(250);
    expect(config.getBankThreshold() == 250, "bank threshold aliases should agree");
    config.toggleAllowTies();
    expect(!config.tiesAllowed(), "tie toggle should flip tiesAllowed");

    zilch::Dice dice;
    dice.setNumDiceInPlay(9);
    expect(dice.numDiceInPlay() == zilch::FULL_SET_OF_DICE, "dice count should clamp to full set");

    std::mt19937 rng(7);
    dice.rollDice(rng);
    expect(dice.lastRoll().size() == zilch::FULL_SET_OF_DICE, "roll should produce six dice");
    std::uint16_t counted = 0;
    for (const auto& [die, count] : dice.diceSetMap()) {
        expect(1 <= die && die <= zilch::FULL_SET_OF_DICE, "die value should be in range");
        counted += count;
    }
    expect(counted == zilch::FULL_SET_OF_DICE, "dice map counts should match dice in play");
    expect(!zilch::formatRoll(dice).empty(), "formatted roll should not be empty after roll");
}

void testPolicyPersistenceValidation()
{
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto validPath = tempDir / "zilch_valid_policy.cfg";
    const auto blankPath = tempDir / "zilch_blank_policy.cfg";
    const auto shortThresholdPath = tempDir / "zilch_short_threshold_policy.cfg";
    const auto extraThresholdPath = tempDir / "zilch_extra_threshold_policy.cfg";
    const auto unknownKeyPath = tempDir / "zilch_unknown_key_policy.cfg";
    const auto malformedPath = tempDir / "zilch_malformed_policy.cfg";
    const auto duplicateKeyPath = tempDir / "zilch_duplicate_key_policy.cfg";
    const auto missingNamePath = tempDir / "zilch_missing_name_policy.cfg";
    const auto nonFinitePath = tempDir / "zilch_nonfinite_policy.cfg";
    const auto clampedPath = tempDir / "zilch_clamped_policy.cfg";

    zilch::Policy policy;
    policy.name = "test";
    expect(zilch::savePolicy(validPath.string(), policy), "valid policy should save");

    zilch::Policy loaded;
    expect(zilch::loadPolicy(validPath.string(), loaded), "valid saved policy should load");
    expect(loaded.name == "test", "policy name should round-trip");

    writePolicyFile(blankPath, "");
    expect(!zilch::loadPolicy(blankPath.string(), loaded), "blank policy should fail");

    const std::string validTail =
        "score_weight=1\n"
        "remaining_dice_weight=55\n"
        "hot_dice_weight=240\n"
        "multiple_weight=95\n"
        "lead_factor=0.08\n"
        "trail_factor=0.10\n"
        "closing_factor=0.25\n"
        "roll_bias=15\n";

    writePolicyFile(shortThresholdPath, "name=bad\nbank_thresholds=350,500\n" + validTail);
    expect(!zilch::loadPolicy(shortThresholdPath.string(), loaded), "too few thresholds should fail");

    writePolicyFile(extraThresholdPath, "name=bad\nbank_thresholds=1,2,3,4,5,6,7\n" + validTail);
    expect(!zilch::loadPolicy(extraThresholdPath.string(), loaded), "too many thresholds should fail");

    writePolicyFile(unknownKeyPath, "name=bad\nbank_thresholds=350,500,700,850,1000,1150\nunknown=1\n" + validTail);
    expect(!zilch::loadPolicy(unknownKeyPath.string(), loaded), "unknown policy keys should fail");

    writePolicyFile(
        malformedPath,
        "name=bad\nbank_thresholds=350,500,700,850,1000,1150\nnot-a-policy-line\n" + validTail);
    expect(!zilch::loadPolicy(malformedPath.string(), loaded), "malformed policy lines should fail");

    writePolicyFile(
        duplicateKeyPath,
        "name=bad\nname=still-bad\nbank_thresholds=350,500,700,850,1000,1150\n" + validTail);
    expect(!zilch::loadPolicy(duplicateKeyPath.string(), loaded), "duplicate policy keys should fail");

    writePolicyFile(missingNamePath, "bank_thresholds=350,500,700,850,1000,1150\n" + validTail);
    expect(!zilch::loadPolicy(missingNamePath.string(), loaded), "missing policy name should fail");

    writePolicyFile(
        nonFinitePath,
        "name=bad\nbank_thresholds=350,500,700,850,1000,1150\nscore_weight=nan\n"
        "remaining_dice_weight=55\nhot_dice_weight=240\nmultiple_weight=95\nlead_factor=0.08\n"
        "trail_factor=0.10\nclosing_factor=0.25\nroll_bias=15\n");
    expect(!zilch::loadPolicy(nonFinitePath.string(), loaded), "non-finite weights should fail");

    writePolicyFile(
        clampedPath,
        "name=clamped\nbank_thresholds=1,2,3,4,5,6\nscore_weight=99\nremaining_dice_weight=-1\n"
        "hot_dice_weight=999\nmultiple_weight=-999\nlead_factor=5\ntrail_factor=-5\n"
        "closing_factor=5\nroll_bias=999\n");
    expect(zilch::loadPolicy(clampedPath.string(), loaded), "out-of-range but parseable policy should load and clamp");
    expect(loaded.bankThresholdByDice[1] == 200, "thresholds should clamp low values to 200");
    expect(loaded.scoreWeight == 3.0, "score weight should clamp high values");
    expect(loaded.remainingDiceWeight == 0.0, "remaining dice weight should clamp low values");
    expect(loaded.rollBias == 200.0, "roll bias should clamp high values");

    expect(!zilch::savePolicy(tempDir.string(), policy), "saving to a directory should fail");

    std::filesystem::remove(validPath);
    std::filesystem::remove(blankPath);
    std::filesystem::remove(shortThresholdPath);
    std::filesystem::remove(extraThresholdPath);
    std::filesystem::remove(unknownKeyPath);
    std::filesystem::remove(malformedPath);
    std::filesystem::remove(duplicateKeyPath);
    std::filesystem::remove(missingNamePath);
    std::filesystem::remove(nonFinitePath);
    std::filesystem::remove(clampedPath);
}

void testHumanControllerShortcuts()
{
    auto game = makeGame();
    std::stringstream input("?\nall\n");
    std::ostringstream output;
    zilch::HumanController human(input, output);
    const std::vector<zilch::ScoringOption> options{
        {zilch::OptionType::Single, 5, 1, 5, 50, false, false, "single five"},
        {zilch::OptionType::Multiple, 1, 3, 3, 1000, false, false, "triple ones"},
    };

    expect(human.chooseOption(game, options) == 1, "all shortcut should choose highest-scoring option");
    expect(output.str().find("Options:") != std::string::npos, "? should reprint options");
    expect(
        human.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
        "all shortcut should keep selecting while options remain");
    expect(
        human.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Roll,
        "all shortcut should stop and roll when no options remain and banking is unavailable");

    std::stringstream retryInput("bad\n1\n");
    std::ostringstream retryOutput;
    zilch::HumanController retryHuman(retryInput, retryOutput);
    expect(retryHuman.chooseOption(game, options) == 0, "human input should retry invalid options");
}

void testTrainerAndArenaValidation()
{
    zilch::TrainingConfig config;
    config.generations = 0;
    expectThrows([&]() { zilch::Trainer trainer(config); }, "zero generations");

    config = {};
    config.population = 3;
    expectThrows([&]() { zilch::Trainer trainer(config); }, "too-small population");

    config = {};
    config.population = 4;
    config.matchesPerGeneration = 6;
    expectThrows([&]() { zilch::Trainer trainer(config); }, "too few mirrored training matches");

    config = {};
    config.scoreLimit = 999;
    expectThrows([&]() { zilch::Trainer trainer(config); }, "low training score limit");

    config = {};
    config.generations = 1;
    config.population = 4;
    config.matchesPerGeneration = 8;
    config.scoreLimit = 1000;
    config.outputPath = (std::filesystem::temp_directory_path() / "zilch_missing_resume_policy.cfg").string();
    config.resumePolicyPath = "/tmp/zilch_missing_resume_does_not_exist.cfg";
    expectThrows(
        [&]() {
            zilch::Trainer trainer(config);
            std::ostringstream output;
            trainer.train(output);
        },
        "missing resume policy");

    zilch::ArenaConfig arena;
    arena.games = 1;
    arena.scoreLimit = 1000;
    expect(!zilch::runArena(arena), "odd arena games should fail");
    arena.games = 2;
    arena.scoreLimit = 999;
    expect(!zilch::runArena(arena), "low arena score limit should fail");
}

} // namespace

int main()
{
    try {
        testScoringOptionsAndRuleToggles();
        testMultipleExtensionAndHotDiceReset();
        testSinglesMultiplesAndHotDiceEdges();
        testBustAndBankingRules();
        testGameStateResetAndWinnerLookup();
        testRuleConfigAndDiceBasics();
        testPolicyPersistenceValidation();
        testHumanControllerShortcuts();
        testTrainerAndArenaValidation();
    } catch (const std::exception& exception) {
        std::cerr << "Test failed: " << exception.what() << '\n';
        return 1;
    }

    std::cout << "All Zilch regression tests passed.\n";
    return 0;
}
