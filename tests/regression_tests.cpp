#include "computer.h"

#include <cmath>
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

void prepareStealOffer(
    zilch::GameManager& game,
    const std::uint32_t roundScore,
    const std::uint16_t diceCount,
    const bool recipientIsOn)
{
    game.ruleConfig().setStealingEnabled(true);
    game.players()[0].score().addPermanentScore(1000);
    if (recipientIsOn)
        game.players()[1].score().addPermanentScore(1000);

    game.currentPlayer().score().addRoundScore(roundScore);
    game.currentPlayer().dice().setNumDiceInPlay(diceCount);
    game.setSavedMultipleScore(1, 2000);
    game.setSavedMultipleScore(5, 1000);
    game.registerRoll();
    game.setSelectedOption(true);
    game.bankCurrentScore();
    game.switchToNextPlayer();
    game.startTurn(game.currentIndex());
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

void expectPoliciesEqual(const zilch::Policy& actual, const zilch::Policy& expected, const std::string& context)
{
    expect(actual.name == expected.name, context + ": policy names should match");
    expect(actual.bankThresholdByDice == expected.bankThresholdByDice, context + ": bank thresholds should match");
    expect(std::abs(actual.scoreWeight - expected.scoreWeight) < 1e-9, context + ": score weights should match");
    expect(std::abs(actual.remainingDiceWeight - expected.remainingDiceWeight) < 1e-9,
           context + ": remaining-dice weights should match");
    expect(std::abs(actual.hotDiceWeight - expected.hotDiceWeight) < 1e-9,
           context + ": hot-dice weights should match");
    expect(std::abs(actual.multipleWeight - expected.multipleWeight) < 1e-9,
           context + ": multiple weights should match");
    expect(std::abs(actual.leadFactor - expected.leadFactor) < 1e-9, context + ": lead factors should match");
    expect(std::abs(actual.trailFactor - expected.trailFactor) < 1e-9, context + ": trail factors should match");
    expect(std::abs(actual.closingFactor - expected.closingFactor) < 1e-9,
           context + ": closing factors should match");
    expect(std::abs(actual.rollBias - expected.rollBias) < 1e-9, context + ": roll biases should match");
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

void testStealingCarriesStateAndChains()
{
    auto game = makeGame();
    prepareStealOffer(game, 300, 3, true);

    expect(game.hasStealOfferForCurrentPlayer(), "the immediately following player should receive the offer");
    expect(game.canCurrentPlayerSteal(), "a player already on the board should be eligible to steal");
    expect(game.stealOfferScore() == 300, "the offer should carry the banked round score");
    expect(game.stealOfferDiceCount() == 3, "the offer should carry the unrolled dice count");
    expect(game.acceptStealOffer(), "an eligible player should be able to accept the offer");
    expect(game.stealContinuationActive(), "accepting should activate the continuation");
    expect(game.currentPlayer().score().roundScore() == 300, "accepting should copy the carried round score");
    expect(game.currentPlayer().dice().numDiceInPlay() == 3, "accepting should restore the carried dice count");
    expect(game.savedMultipleScore(1) == 2000, "accepting should restore the saved ones chain");
    expect(game.savedMultipleScore(5) == 1000, "accepting should restore the full saved-multiple map");

    game.currentPlayer().score().addRoundScore(150);
    game.currentPlayer().dice().setNumDiceInPlay(2);
    game.registerRoll();
    game.setSelectedOption(true);
    game.bankCurrentScore();
    expect(game.currentPlayer().score().permanentScore() == 1450, "a continued score should bank normally");

    game.switchToNextPlayer();
    game.startTurn(game.currentIndex());
    expect(game.hasStealOfferForCurrentPlayer(), "a successfully banked continuation should chain");
    expect(game.stealOfferScore() == 450, "a chained offer should carry the enlarged round score");
    expect(game.stealOfferDiceCount() == 2, "a chained offer should carry its remaining dice");
}

void testStealingEligibilityDeclineAndTermination()
{
    {
        auto game = makeGame();
        prepareStealOffer(game, 250, 4, false);

        expect(game.hasStealOfferForCurrentPlayer(), "an off-board recipient should see the immediate offer context");
        expect(!game.canCurrentPlayerSteal(), "carried points must not put a player on the board");
        expect(!game.acceptStealOffer(), "an ineligible recipient must not accept");
        game.declineStealOffer();
        expect(!game.hasStealOfferForCurrentPlayer(), "declining should clear the offer");
        expect(!game.stealContinuationActive(), "declining should leave no active continuation");
        expect(game.currentPlayer().score().roundScore() == 0, "declining should clear inherited points");
        expect(game.currentPlayer().dice().numDiceInPlay() == zilch::FULL_SET_OF_DICE,
               "declining should restore six fresh dice");
        expect(!game.hasSavedMultiple(1) && !game.hasSavedMultiple(5),
               "declining should clear all inherited multiple state");
    }

    {
        auto game = makeGame();
        prepareStealOffer(game, 300, 3, true);
        expect(game.acceptStealOffer(), "the prepared hot-dice continuation should be accepted");
        game.manageDiceCount(0);
        expect(!game.stealContinuationActive(), "hot dice should end a steal continuation");
        expect(!game.hasSavedMultiple(1) && !game.hasSavedMultiple(5),
               "hot dice should clear inherited multiple state");
    }

    {
        auto game = makeGame();
        prepareStealOffer(game, 300, 3, true);
        expect(game.acceptStealOffer(), "the prepared bust continuation should be accepted");
        game.registerRoll();
        setDice(game, {2, 3, 4});
        zilch::Checker checker(game);
        expect(!checker.hasAvailableOption(), "the accepted continuation should have a bust hand");
        checker.handleBust();
        expect(game.currentPlayer().score().roundScore() == 0, "a steal bust should lose the carried score");
        expect(game.bustPending(), "a steal bust should end the turn as a bust");
        expect(!game.bustBonusUsedThisTurn(), "first-roll bust must not rescue an accepted continuation");
        expect(!game.stealContinuationActive(), "a bust should end the steal chain");
    }

    {
        auto game = makeGame();
        prepareStealOffer(game, 300, 3, true);
        game.ruleConfig().setStealingEnabled(false);
        game.startTurn(game.currentIndex());
        expect(!game.hasStealOfferForCurrentPlayer(), "disabling stealing should clear a pending offer");
    }
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
    expect(game.finalRoundLeaderIndex() == 1, "the player starting the final round should be incumbent leader");
    game.startTurn(2);
    game.currentPlayer().score().addPermanentScore(300);
    game.updateFinalRoundLeaderForCurrentPlayer();
    expect(game.finalRoundLeaderIndex() == 1, "tying the incumbent should not replace them");
    expect(!game.wouldEndAfterCurrentTurn(), "middle final-round player should not end the round");
    game.startTurn(0);
    game.currentPlayer().score().addPermanentScore(600);
    game.updateFinalRoundLeaderForCurrentPlayer();
    expect(game.finalRoundLeaderIndex() == 0, "beating the incumbent should establish a new leader");
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
    expect(config.straightEnabled() && config.straitsEnabled(), "straight aliases should be enabled by default");
    expect(config.threePairsEnabled() && config.setsEnabled(), "three-pairs aliases should be enabled by default");
    expect(config.multiplesEnabled() && config.singlesEnabled(), "multiples and singles should be enabled by default");
    expect(config.firstRollBustBonusEnabled(), "first-roll bust should be enabled by default");
    expect(config.finalChaseEnabled(), "final chase should be enabled by default");
    expect(config.tiesAllowed(), "ties should be allowed by default");
    expect(!config.stealingEnabled(), "stealing should be disabled by default");
    expect(config.openingScoreLimit() == 1000 && config.getOpeningScoreLimit() == 1000,
           "opening-score aliases should default to 1000");
    expect(config.bankThreshold() == 1000, "default bank threshold should be 1000");
    config.adjustBankThreshold(-2000);
    expect(config.bankThreshold() == 0, "bank threshold should not underflow below zero");
    config.adjustBankThreshold(250);
    expect(config.getBankThreshold() == 250, "bank threshold aliases should agree");
    config.setOpeningScoreLimit(500);
    expect(config.getBankThreshold() == 500, "opening score should retain bank-threshold compatibility");
    config.adjustOpeningScoreLimit(250);
    expect(config.getOpeningScoreLimit() == 750, "opening-score adjustment should share bank-threshold state");
    config.toggleAllowTies();
    expect(!config.tiesAllowed(), "tie toggle should flip tiesAllowed");
    config.toggleStealing();
    expect(config.stealingEnabled(), "stealing toggle should enable the optional variant");

    const zilch::PlayConfig playConfig;
    const zilch::ArenaConfig arenaConfig;
    const zilch::TrainingConfig trainingConfig;
    expect(playConfig.scoreLimit == 5000 && arenaConfig.scoreLimit == 5000 && trainingConfig.scoreLimit == 5000,
           "play, arena, and training should share the 5000-point winning default");
    expect(
        playConfig.ruleConfig.openingScoreLimit() == 1000 &&
            arenaConfig.ruleConfig.openingScoreLimit() == 1000 &&
            trainingConfig.ruleConfig.openingScoreLimit() == 1000,
        "play, arena, and training should share the 1000-point opening default");
    expect(
        !playConfig.ruleConfig.stealingEnabled() && !arenaConfig.ruleConfig.stealingEnabled() &&
            !trainingConfig.ruleConfig.stealingEnabled(),
        "play, arena, and training should share the stealing-off default");

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

    auto stealGame = makeGame();
    prepareStealOffer(stealGame, 300, 3, true);
    std::stringstream turnStartInput("bad\nsteal\n");
    std::ostringstream turnStartOutput;
    zilch::HumanController turnStartHuman(turnStartInput, turnStartOutput);
    expect(
        turnStartHuman.decideTurnStart(stealGame) == zilch::TurnStartDecision::AcceptSteal,
        "human turn-start input should support accepting a steal");
    expect(
        turnStartOutput.str().find("Invalid turn-start action") != std::string::npos,
        "human turn-start input should retry invalid actions");

    zilch::ComputerController computer({});
    expect(
        computer.decideTurnStart(stealGame) == zilch::TurnStartDecision::AcceptSteal,
        "the baseline AI should accept a valuable continuation deterministically");

    auto lowValueStealGame = makeGame();
    prepareStealOffer(lowValueStealGame, 50, 1, true);
    expect(
        computer.decideTurnStart(lowValueStealGame) == zilch::TurnStartDecision::FreshRoll,
        "the baseline AI should decline a low-value continuation deterministically");
}

void testSecondPersonOutputGrammar()
{
    expect(zilch::formatPlayerTurn("You") == "Your turn",
           "the default human name should use a second-person turn heading");
    expect(zilch::formatPlayerTurn("yOu") == "Your turn",
           "second-person name detection should be case-insensitive");
    expect(zilch::formatPlayerTurn("Ada") == "Ada's turn",
           "ordinary names should keep a possessive turn heading");

    expect(zilch::formatPlayerAction("You", "accept", "accepts") == "You accept",
           "the default human name should use the second-person accept verb");
    expect(zilch::formatPlayerAction("YOU", "decline", "declines") == "YOU decline",
           "case-insensitive second-person names should use the second-person decline verb");
    expect(zilch::formatPlayerAction("You", "bank", "banks") == "You bank",
           "the default human name should use the second-person bank verb");
    expect(zilch::formatPlayerAction("You", "win", "wins") == "You win",
           "the default human name should use the second-person win verb");
    expect(zilch::formatPlayerAction("Ada", "bank", "banks") == "Ada banks",
           "ordinary names should retain third-person verbs");
}

void testNamedDifficultyPoliciesMatchResearchArtifacts()
{
    expect(
        zilch::parseComputerDifficulty("easy") == zilch::ComputerDifficulty::Easy &&
            zilch::parseComputerDifficulty("MEDIUM") == zilch::ComputerDifficulty::Medium &&
            zilch::parseComputerDifficulty("Hard") == zilch::ComputerDifficulty::Hard,
        "difficulty parser should be case-insensitive");
    expect(!zilch::parseComputerDifficulty("expert"), "unknown difficulty names should be rejected");
    expect(zilch::computerDifficultyName(zilch::ComputerDifficulty::Easy) == "Easy",
           "difficulty names should be suitable for CLI output");

    const auto easy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Easy);
    for (std::size_t dice = 1; dice < easy.bankThresholdByDice.size(); ++dice)
        expect(easy.bankThresholdByDice[dice] == 600, "Easy should use one simple 600-point bank target");

    const auto medium = zilch::policyForDifficulty(zilch::ComputerDifficulty::Medium);
    expect(medium.bankThresholdByDice == std::array<int, 7>{0, 350, 500, 700, 850, 1000, 1150},
           "Medium should preserve the established score-aware baseline");

    zilch::Policy trackedStandard;
    const auto standardPath = std::filesystem::path(ZILCH_SOURCE_DIR) / "trained_policy.cfg";
    expect(zilch::loadPolicy(standardPath.string(), trackedStandard), "tracked standard policy should load");
    expectPoliciesEqual(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard, false),
        trackedStandard,
        "Hard standard preset");

    zilch::Policy trackedStealing;
    const auto stealingPath = std::filesystem::path(ZILCH_SOURCE_DIR) / "trained_stealing_policy.cfg";
    expect(zilch::loadPolicy(stealingPath.string(), trackedStealing), "tracked Stealing policy should load");
    expectPoliciesEqual(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard, true),
        trackedStealing,
        "Hard Stealing preset");
}

void testEasyScoresEverythingAndRecognizesAWin()
{
    auto game = makeGame();
    game.ruleConfig().setOpeningScoreLimit(0);
    game.registerRoll();
    setDice(game, {1, 5, 2, 2, 3, 4});
    zilch::Checker checker(game);
    zilch::ComputerController easy(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Easy),
        zilch::ComputerDifficulty::Easy);

    auto options = checker.availableOptions();
    checker.applyOption(options[easy.chooseOption(game, options)]);
    options = checker.availableOptions();
    expect(!options.empty(), "a second scoring die should remain after Easy's first choice");
    expect(easy.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
           "Easy should take every available scoring option");
    checker.applyOption(options[easy.chooseOption(game, options)]);

    expect(easy.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Roll,
           "Easy should keep rolling below its simple target");
    game.currentPlayer().score().setRoundScore(600);
    expect(easy.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Bank,
           "Easy should bank at its simple target");

    auto winningGame = makeGame();
    winningGame.ruleConfig().setOpeningScoreLimit(0);
    winningGame.players()[0].score().addPermanentScore(4900);
    winningGame.currentPlayer().score().setRoundScore(100);
    winningGame.currentPlayer().dice().setNumDiceInPlay(1);
    winningGame.registerRoll();
    winningGame.setSelectedOption(true);
    zilch::ComputerController winningEasy(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Easy),
        zilch::ComputerDifficulty::Easy);
    expect(winningEasy.decideAfterSelection(winningGame, {}) == zilch::PostSelectionDecision::Bank,
           "Easy should bank a winning score even below its ordinary target");
}

zilch::PostSelectionDecision namedDifficultyDecision(
    const zilch::ComputerDifficulty difficulty,
    const std::uint32_t playerScore,
    const std::uint32_t opponentScore,
    const std::uint32_t roundScore,
    const std::uint16_t remainingDice,
    const bool allowTies = true)
{
    auto game = makeGame();
    game.ruleConfig().setOpeningScoreLimit(0);
    game.ruleConfig().setAllowTies(allowTies);
    game.players()[0].score().addPermanentScore(playerScore);
    game.players()[1].score().addPermanentScore(opponentScore);
    game.currentPlayer().score().setRoundScore(roundScore);
    game.currentPlayer().dice().setNumDiceInPlay(remainingDice);
    game.registerRoll();
    game.setSelectedOption(true);
    zilch::ComputerController controller(zilch::policyForDifficulty(difficulty), difficulty);
    return controller.decideAfterSelection(game, {});
}

void testMediumAndHardEndgameAwareness()
{
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Medium, 4800, 3500, 100, 5) ==
               zilch::PostSelectionDecision::Bank,
           "Medium should stage just below the target only with a safe lead");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Medium, 4800, 4700, 100, 5) ==
               zilch::PostSelectionDecision::Roll,
           "Medium should not stage below the target against a close opponent");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Medium, 4900, 4700, 100, 5) ==
               zilch::PostSelectionDecision::Roll,
           "Medium should seek a buffer after narrowly crossing the target with safe dice");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Medium, 4900, 4700, 1100, 5) ==
               zilch::PostSelectionDecision::Bank,
           "Medium should bank after building its close-opponent buffer");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Medium, 4900, 4700, 100, 3) ==
               zilch::PostSelectionDecision::Bank,
           "Medium should protect a narrow winning score when too few dice remain");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Hard, 4900, 4700, 100, 3) ==
               zilch::PostSelectionDecision::Roll,
           "Hard should combine the buffer plan with its higher risk tolerance");

    auto rawPolicyGame = makeGame();
    rawPolicyGame.ruleConfig().setOpeningScoreLimit(0);
    rawPolicyGame.players()[0].score().addPermanentScore(4900);
    rawPolicyGame.players()[1].score().addPermanentScore(4700);
    rawPolicyGame.currentPlayer().score().setRoundScore(100);
    rawPolicyGame.currentPlayer().dice().setNumDiceInPlay(3);
    rawPolicyGame.registerRoll();
    rawPolicyGame.setSelectedOption(true);
    zilch::ComputerController rawMediumPolicy(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Medium));
    expect(rawMediumPolicy.decideAfterSelection(rawPolicyGame, {}) == zilch::PostSelectionDecision::Roll,
           "an exact policy controller should not inherit named-level finish heuristics");

    const auto finalChaseDecision = [](const bool allowTies, const std::uint32_t roundScore) {
        auto game = makeGame();
        game.ruleConfig().setOpeningScoreLimit(0);
        game.ruleConfig().setAllowTies(allowTies);
        game.players()[0].score().addPermanentScore(5200);
        game.players()[1].score().addPermanentScore(5000);
        game.beginFinalRound();
        game.switchToNextPlayer();
        game.startTurn(game.currentIndex());
        game.currentPlayer().score().setRoundScore(roundScore);
        game.currentPlayer().dice().setNumDiceInPlay(2);
        game.registerRoll();
        game.setSelectedOption(true);
        zilch::ComputerController medium(
            zilch::policyForDifficulty(zilch::ComputerDifficulty::Medium),
            zilch::ComputerDifficulty::Medium);
        return medium.decideAfterSelection(game, {});
    };

    expect(finalChaseDecision(true, 200) == zilch::PostSelectionDecision::Bank,
           "a named bot should bank a permitted tie during Final Chase");
    expect(finalChaseDecision(false, 200) == zilch::PostSelectionDecision::Roll,
           "a named bot should keep rolling when a tie cannot win Final Chase");
    expect(finalChaseDecision(false, 250) == zilch::PostSelectionDecision::Bank,
           "a named bot should bank once it strictly leads Final Chase");
}

void testNamedDifficultyStealChoices()
{
    auto game = makeGame();
    prepareStealOffer(game, 500, 1, true);

    zilch::ComputerController easy(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Easy, true),
        zilch::ComputerDifficulty::Easy);
    zilch::ComputerController medium(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Medium, true),
        zilch::ComputerDifficulty::Medium);
    zilch::ComputerController hard(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard, true),
        zilch::ComputerDifficulty::Hard);

    expect(easy.decideTurnStart(game) == zilch::TurnStartDecision::FreshRoll,
           "Easy should decline a continuation below its simple threshold");
    expect(medium.decideTurnStart(game) == zilch::TurnStartDecision::AcceptSteal,
           "Medium should compare a continuation with its dice-aware target");
    expect(hard.decideTurnStart(game) == zilch::TurnStartDecision::FreshRoll,
           "Hard should follow the Stealing-trained utility cutoff");

    auto strongerOffer = makeGame();
    prepareStealOffer(strongerOffer, 550, 1, true);
    expect(hard.decideTurnStart(strongerOffer) == zilch::TurnStartDecision::AcceptSteal,
           "Hard should accept at the Stealing-trained one-die cutoff");
}

void testOptionalCollectorBanksAllGuaranteedPoints()
{
    const auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    auto game = makeGame();
    game.currentPlayer().score().setRoundScore(2700);
    game.registerRoll();
    setDice(game, {1, 1, 5, 2, 3, 4});
    zilch::Checker checker(game);
    zilch::ComputerController incumbent(policy, zilch::ComputerDifficulty::Hard);
    zilch::ComputerController collector(policy, zilch::ComputerDifficulty::Hard, true);

    auto options = checker.availableOptions();
    expect(incumbent.chooseOption(game, options) == collector.chooseOption(game, options),
           "optional collection must not change the initial rolling selection");
    checker.applyOption(options[collector.chooseOption(game, options)]);
    expect(game.currentPlayer().score().roundScore() == 2800, "the first one should bring the turn to 2800");
    options = checker.availableOptions();
    expect(incumbent.decideAfterSelection(game, options) == zilch::PostSelectionDecision::Bank,
           "the default-off incumbent should preserve its historical bank decision");
    expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
           "the candidate should collect guaranteed points before banking");

    checker.applyOption(options[collector.chooseOption(game, options)]);
    options = checker.availableOptions();
    expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
           "a pending bank should continue collecting the remaining five");
    checker.applyOption(options[collector.chooseOption(game, options)]);
    expect(checker.availableOptions().empty(), "all scoring dice should now be collected");
    expect(collector.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Bank,
           "a completed collection should bank without another roll");
    expect(game.currentPlayer().score().roundScore() == 2950, "collection should retain the extra 150 points");
    game.bankCurrentScore();
    expect(game.currentPlayer().score().permanentScore() == 2950, "the extra points should actually be banked");
}

void testOptionalCollectorPreservesRollsAndResetsItsPlan()
{
    const auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    auto game = makeGame();
    game.registerRoll();
    setDice(game, {1, 1, 5, 2, 3, 4});
    zilch::Checker checker(game);
    zilch::ComputerController incumbent(policy, zilch::ComputerDifficulty::Hard);
    zilch::ComputerController collector(policy, zilch::ComputerDifficulty::Hard, true);
    auto options = checker.availableOptions();
    checker.applyOption(options[collector.chooseOption(game, options)]);
    options = checker.availableOptions();
    expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::Roll &&
               incumbent.decideAfterSelection(game, options) == zilch::PostSelectionDecision::Roll,
           "the optional collector must leave below-threshold rolling decisions unchanged");

    game.currentPlayer().score().setRoundScore(2800);
    expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
           "the high score should create a pending bank plan");
    game.startTurn(0);
    game.registerRoll();
    setDice(game, {1, 1, 5, 2, 3, 4});
    options = checker.availableOptions();
    checker.applyOption(options[collector.chooseOption(game, options)]);
    expect(collector.decideAfterSelection(game, checker.availableOptions()) == zilch::PostSelectionDecision::Roll,
           "starting a new turn must discard an unfinished bank plan");

    game.currentPlayer().score().setRoundScore(2800);
    expect(collector.decideAfterSelection(game, checker.availableOptions()) ==
               zilch::PostSelectionDecision::SelectAgain,
           "another high score should recreate the pending plan");
    collector.decideTurnStart(game);
    game.currentPlayer().score().setRoundScore(100);
    expect(collector.decideAfterSelection(game, checker.availableOptions()) == zilch::PostSelectionDecision::Roll,
           "an explicit turn-start decision must also discard the pending bank plan");
}

void testOptionalCollectorCommitsThroughHotDice()
{
    auto game = makeGame();
    game.ruleConfig().setOpeningScoreLimit(0);
    game.currentPlayer().score().setRoundScore(600);
    game.registerRoll();
    game.setSelectedOption(true);
    setDice(game, {1, 5, 2, 2, 2});
    auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    policy.bankThresholdByDice = {0, 600, 600, 600, 600, 600, 4000};
    policy.scoreWeight = 1;
    policy.remainingDiceWeight = 200;
    policy.hotDiceWeight = -50;
    policy.multipleWeight = -50;
    policy.rollBias = 200;
    policy.leadFactor = 0;
    policy.trailFactor = 0;
    policy.closingFactor = 0;
    zilch::ComputerController collector(policy, zilch::ComputerDifficulty::Hard, true);
    zilch::Checker checker(game);
    auto options = checker.availableOptions();
    expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
           "the candidate should latch a bank before collecting more dice");
    const auto firstChoice = options[collector.chooseOption(game, options)];
    expect(firstChoice.type == zilch::OptionType::Multiple && firstChoice.scoreGain == 200,
           "a pending bank should choose guaranteed score rather than rolling utility");
    checker.applyOption(firstChoice);

    for (int selection = 0; selection < 2; ++selection) {
        options = checker.availableOptions();
        expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
               "the committed bank should collect both remaining singles");
        checker.applyOption(options[collector.chooseOption(game, options)]);
    }
    expect(game.currentPlayer().dice().numDiceInPlay() == 6, "collecting everything should produce hot dice");
    expect(game.currentPlayer().score().roundScore() == 950, "the committed collection should score 950");
    expect(collector.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Bank,
           "hot dice must not reverse an already committed bank into a new roll");
    expect(collector.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Roll,
           "returning Bank must clear the collection plan before the controller is reused");
}

void testOptionalCollectorKeepsAnImmediateWinningBank()
{
    auto game = makeGame();
    game.ruleConfig().setFinalChaseEnabled(false);
    game.players()[0].score().addPermanentScore(4900);
    game.players()[1].score().addPermanentScore(4700);
    game.registerRoll();
    setDice(game, {1, 1, 5, 2, 3, 4});
    zilch::Checker checker(game);
    zilch::ComputerController collector(
        zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard), zilch::ComputerDifficulty::Hard, true);
    auto options = checker.availableOptions();
    checker.applyOption(options[collector.chooseOption(game, options)]);
    for (int selection = 0; selection < 2; ++selection) {
        options = checker.availableOptions();
        expect(collector.decideAfterSelection(game, options) == zilch::PostSelectionDecision::SelectAgain,
               "an immediate winning bank may collect extra dice without risking another roll");
        checker.applyOption(options[collector.chooseOption(game, options)]);
    }
    expect(collector.decideAfterSelection(game, {}) == zilch::PostSelectionDecision::Bank,
           "the candidate must retain its immediate winning bank after collection");
    game.bankCurrentScore();
    expect(game.currentPlayer().score().permanentScore() == 5150,
           "the immediate winning bank should include all guaranteed points");
}

void testHardThresholdMatchesWebPrecision()
{
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Hard, 0, 1100, 1450, 3) ==
               zilch::PostSelectionDecision::Roll,
           "the 1450.5134 threshold must not be truncated into a bank at 1450");
    expect(namedDifficultyDecision(zilch::ComputerDifficulty::Hard, 0, 1100, 1500, 3) ==
               zilch::PostSelectionDecision::Bank,
           "the next reachable score must still bank above the precise threshold");
}

void testHighBankThresholdPersistence()
{
    const auto path = std::filesystem::temp_directory_path() / "zilch_high_threshold_policy.cfg";
    auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    policy.bankThresholdByDice[6] = 16575;
    expect(zilch::savePolicy(path.string(), policy), "the high six-dice cutoff should save");
    zilch::Policy loaded;
    expect(zilch::loadPolicy(path.string(), loaded), "the high six-dice cutoff should load");
    expect(loaded.bankThresholdByDice[6] == 16575,
           "research must retain a six-dice cutoff above the historical 3000-point search cap");
    policy.bankThresholdByDice[6] = 100001;
    expect(zilch::savePolicy(path.string(), policy), "the oversized cutoff fixture should save");
    expect(zilch::loadPolicy(path.string(), loaded), "the oversized cutoff fixture should load and clamp");
    expect(loaded.bankThresholdByDice[6] == 100000, "the expanded cutoff must retain its 100000-point safety cap");
    std::filesystem::remove(path);
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
    config.ruleConfig.setOpeningScoreLimit(config.scoreLimit + 1);
    expectThrows([&]() { zilch::Trainer trainer(config); }, "opening score above winning score");

    config = {};
    config.ruleConfig.setStraightEnabled(false);
    config.ruleConfig.setThreePairsEnabled(false);
    config.ruleConfig.setMultiplesEnabled(false);
    config.ruleConfig.setSinglesEnabled(false);
    expectThrows([&]() { zilch::Trainer trainer(config); }, "profile without a scoring rule");

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
    arena.scoreLimit = 5000;
    arena.ruleConfig.setOpeningScoreLimit(5001);
    expect(!zilch::runArena(arena), "arena opening score above the winning score should fail");

    arena = {};
    arena.games = 2;
    arena.policyAPath = "trained_policy.cfg";
    arena.difficultyA = zilch::ComputerDifficulty::Hard;
    expect(!zilch::runArena(arena), "arena should reject a named bot and policy path for the same seat");

    zilch::PlayConfig play;
    play.scoreLimit = 1000;
    play.ruleConfig.setOpeningScoreLimit(1001);
    expect(!zilch::runHumanVsComputer(play), "play opening score above the winning score should fail before input");

    play = {};
    play.policyPath = "trained_policy.cfg";
    play.difficulty = zilch::ComputerDifficulty::Hard;
    expect(!zilch::runHumanVsComputer(play), "play should reject a named difficulty combined with a policy path");
}

} // namespace

int main()
{
    try {
        testScoringOptionsAndRuleToggles();
        testMultipleExtensionAndHotDiceReset();
        testSinglesMultiplesAndHotDiceEdges();
        testBustAndBankingRules();
        testStealingCarriesStateAndChains();
        testStealingEligibilityDeclineAndTermination();
        testGameStateResetAndWinnerLookup();
        testRuleConfigAndDiceBasics();
        testPolicyPersistenceValidation();
        testHumanControllerShortcuts();
        testSecondPersonOutputGrammar();
        testNamedDifficultyPoliciesMatchResearchArtifacts();
        testEasyScoresEverythingAndRecognizesAWin();
        testMediumAndHardEndgameAwareness();
        testNamedDifficultyStealChoices();
        testOptionalCollectorBanksAllGuaranteedPoints();
        testOptionalCollectorPreservesRollsAndResetsItsPlan();
        testOptionalCollectorCommitsThroughHotDice();
        testOptionalCollectorKeepsAnImmediateWinningBank();
        testHardThresholdMatchesWebPrecision();
        testHighBankThresholdPersistence();
        testTrainerAndArenaValidation();
    } catch (const std::exception& exception) {
        std::cerr << "Test failed: " << exception.what() << '\n';
        return 1;
    }

    std::cout << "All Zilch regression tests passed.\n";
    return 0;
}
