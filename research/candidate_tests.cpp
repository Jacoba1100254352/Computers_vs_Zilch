#include "computer.h"
#include "selection_checkpoint.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

using zilch::ComputerController;
using zilch::ComputerDifficulty;
using zilch::GameManager;
using zilch::PostSelectionDecision;
using zilch::ResearchFeatures;

void expect(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void near(const double actual, const double expected, const char* message)
{
    expect(std::abs(actual - expected) < 1e-8, message);
}

GameManager chainState(const std::uint16_t face = 6, const std::uint16_t count = 3,
                       const std::uint32_t turnScore = 2000)
{
    GameManager game;
    game.setPlayers({"A", "B"});
    game.setScoreLimit(10000);
    game.players()[0].score().addPermanentScore(1000);
    game.players()[1].score().addPermanentScore(1000);
    game.startTurn(0);
    game.currentPlayer().score().setRoundScore(turnScore);
    game.manageDiceCount(static_cast<std::uint16_t>(6 - count));
    const auto base = face == 1 ? 1000U : static_cast<unsigned int>(face * 100);
    game.setSavedMultipleScore(face, base << (count - 3));
    game.setSelectedOption(true);
    game.registerRoll();
    return game;
}

PostSelectionDecision action(GameManager game, const ResearchFeatures features,
                             const ComputerDifficulty difficulty = ComputerDifficulty::Hard)
{
    ComputerController controller(zilch::policyForDifficulty(difficulty, game.ruleConfig().stealingEnabled()),
                                  difficulty, std::nullopt, features);
    return controller.decideAfterSelection(game, zilch::Checker(game).availableOptions());
}

void exactMomentsAndCache()
{
    // Independently derived ordered-roll counts, including all additive singles
    // and any newly rolled triples, for physically possible 3/4/5 saved dice.
    struct Fixture { std::uint16_t face; std::uint16_t count; std::uint32_t outcomes;
                     std::uint32_t busts; std::uint64_t sum; };
    const Fixture fixtures[] = {
        {1, 3, 216, 60, 134250}, {2, 3, 216, 24, 43950}, {6, 3, 216, 24, 94350},
        {1, 4, 36, 16, 26600}, {2, 4, 36, 9, 7000}, {6, 4, 36, 9, 17400},
        {1, 5, 6, 4, 4050}, {2, 5, 6, 3, 950}, {6, 5, 6, 3, 2550},
    };
    for (const auto& fixture : fixtures) {
        auto game = chainState(fixture.face, fixture.count, 5000);
        const auto estimate = zilch::researchChainRiskEstimate(game);
        expect(estimate.has_value(), "Every physically valid chain fixture needs an estimate.");
        expect(estimate->outcomes == fixture.outcomes && estimate->busts == fixture.busts &&
                   estimate->totalNewScore == fixture.sum, "Exact moments disagree with independent counts.");
        near(estimate->breakEvenTurnScore, static_cast<double>(fixture.sum) / fixture.busts,
             "Empty leftover roll must use E/p.");
        const auto cached = zilch::researchChainRiskEstimate(game);
        expect(cached->totalNewScore == estimate->totalNewScore, "Repeated cached lookup must agree.");
        expect(game.currentPlayer().score().roundScore() == 5000 &&
                   game.currentPlayer().dice().diceSetMap().empty() && game.selectedOption(),
               "Research enumeration must not mutate its source state.");
    }
    auto six = chainState();
    six.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}};
    const auto withFive = zilch::researchChainRiskEstimate(six);
    expect(withFive->guaranteedScoreLeft == 50, "Guaranteed leftover five must enter the bank alternative.");
    near(withFive->breakEvenTurnScore, 3481.25, "Triple-six crossover must subtract the unclaimed 50 points.");
    six.currentPlayer().dice().diceSetMap() = {{1, 1}, {3, 1}, {5, 1}};
    near(zilch::researchChainRiskEstimate(six)->breakEvenTurnScore, 2581.25,
         "A cached next-roll estimate must not cache a different current leftover roll.");
    six.ruleConfig().setSinglesEnabled(false);
    const auto noSingles = zilch::researchChainRiskEstimate(six);
    expect(noSingles->busts == 120 && noSingles->totalNewScore == 78600 &&
               noSingles->guaranteedScoreLeft == 0, "Scoring-rule changes must use another cache entry.");
    six.ruleConfig().setMultiplesEnabled(false);
    expect(!zilch::researchChainRiskEstimate(six), "Disabled multiples must not activate a chain estimate.");
    six.ruleConfig().setMultiplesEnabled(true);
    six.clearSavedMultiples();
    expect(!zilch::researchChainRiskEstimate(six), "An unsaved multiple must not activate the feature.");
    six.setSavedMultipleScore(6, 600);
    six.manageDiceCount(4);
    expect(!zilch::researchChainRiskEstimate(six), "More than three remaining dice is not a supported chain state.");
}

void controlledThresholds()
{
    auto six = chainState();
    six.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}};
    expect(action(six, {}) == PostSelectionDecision::SelectAgain,
           "Released Hard must still collect the five before its ordinary bank decision.");
    expect(action(six, {1.0, false, false}) == PostSelectionDecision::Roll,
           "Raise candidate should preserve the three-die six chain at this score.");
    auto weak = chainState(2, 4, 900);
    expect(action(weak, {}) == PostSelectionDecision::Roll, "Released Hard must retain its low-chain decision.");
    expect(action(weak, {1.0, false, false}) == PostSelectionDecision::Roll,
           "Raise-only must not lower the weak-chain threshold.");
    expect(action(weak, {1.0, false, true}) == PostSelectionDecision::Bank,
           "Symmetric blend must test lower weak-chain thresholds separately.");
    weak.players()[1].score().addPermanentScore(2000);
    expect(action(weak, {1.0, false, true}) == PostSelectionDecision::Roll,
           "Existing trailing-score adjustment must remain in force after blending.");

    auto opening = chainState(6, 3, 900);
    opening.ruleConfig().setOpeningScoreLimit(5000);
    expect(action(opening, {100.0, true, true}) == PostSelectionDecision::Roll,
           "Candidate must not bank before the opening minimum is met.");
    auto immediate = chainState(6, 3, 9000);
    immediate.ruleConfig().setFinalChaseEnabled(false);
    expect(action(immediate, {100.0, false, false}) == PostSelectionDecision::Bank,
           "Existing guaranteed immediate-win endgame rule must override chain risk.");
    for (const auto difficulty : {ComputerDifficulty::Easy, ComputerDifficulty::Medium}) {
        expect(action(six, {}, difficulty) == action(six, {1.0, true, true}, difficulty),
               "Features must not affect Easy or Medium.");
    }
    six.ruleConfig().setStealingEnabled(true);
    expect(action(six, {}) == action(six, {1.0, true, true}),
           "Features must not change the separate Stealing policy.");
    expect(action(six, {}) == action(six, {0.0, false, true}),
           "The lower-threshold mode alone must have no effect at weight zero.");
}

GameManager finishState(const std::uint32_t ownScore, const std::uint32_t opponentScore,
                        const bool finalActive = true, const bool thirdPlayer = false)
{
    GameManager game;
    game.setPlayers(thirdPlayer ? std::vector<std::string>{"A", "B", "C"} :
                                  std::vector<std::string>{"A", "B"});
    game.setScoreLimit(5000);
    game.ruleConfig().setAllowTies(false);
    game.ruleConfig().setFinalChaseEnabled(finalActive);
    game.players()[0].score().addPermanentScore(ownScore);
    game.players()[1].score().addPermanentScore(opponentScore);
    if (finalActive) {
        game.startTurn(1);
        game.beginFinalRound();
    }
    game.startTurn(0);
    game.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}, {6, 3}};
    game.registerRoll();
    return game;
}

void safeFinishAndLifetime()
{
    auto game = finishState(4900, 5500);
    ComputerController controller(zilch::policyForDifficulty(ComputerDifficulty::Hard),
                                  ComputerDifficulty::Hard, false, {0, true, false});
    auto options = zilch::Checker(game).availableOptions();
    zilch::Checker(game).applyOption(options[controller.chooseOption(game, options)]);
    options = zilch::Checker(game).availableOptions();
    expect(controller.decideAfterSelection(game, options) == PostSelectionDecision::SelectAgain,
           "Outright winning collection must not stop at a forbidden tie.");
    zilch::Checker(game).applyOption(options[controller.chooseOption(game, options)]);
    expect(controller.decideAfterSelection(game, {}) == PostSelectionDecision::Bank,
           "Research collection must bank the guaranteed final-chase win.");
    expect(game.currentPlayer().score().roundScore() == 650, "Winning collection must include the extra five.");

    auto tied = finishState(4850, 5500);
    options = zilch::Checker(tied).availableOptions();
    zilch::Checker(tied).applyOption(options[controller.chooseOption(tied, options)]);
    expect(controller.decideAfterSelection(tied, zilch::Checker(tied).availableOptions()) == PostSelectionDecision::Roll,
           "A completed prior bank must not leak; bank-all ties are not safe outright wins.");
    auto beforeOpening = finishState(0, 0, false);
    beforeOpening.ruleConfig().setOpeningScoreLimit(1000);
    options = zilch::Checker(beforeOpening).availableOptions();
    zilch::Checker(beforeOpening).applyOption(options[controller.chooseOption(beforeOpening, options)]);
    expect(controller.decideAfterSelection(beforeOpening, zilch::Checker(beforeOpening).availableOptions()) ==
               PostSelectionDecision::Roll, "Safe collection must not force an unavailable opening bank.");
    auto immediate = finishState(4350, 0, false);
    options = zilch::Checker(immediate).availableOptions();
    zilch::Checker(immediate).applyOption(options[controller.chooseOption(immediate, options)]);
    expect(controller.decideAfterSelection(immediate, zilch::Checker(immediate).availableOptions()) ==
               PostSelectionDecision::SelectAgain, "Collection must secure an immediate target win.");

    // The final-round starter is C; B's turn is still pending after A here.
    auto multiplayer = finishState(4900, 5500, true, true);
    multiplayer.startTurn(2);
    multiplayer.beginFinalRound();
    multiplayer.startTurn(0);
    multiplayer.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}, {6, 3}};
    multiplayer.registerRoll();
    options = zilch::Checker(multiplayer).availableOptions();
    zilch::Checker(multiplayer).applyOption(options[controller.chooseOption(multiplayer, options)]);
    expect(controller.decideAfterSelection(multiplayer, zilch::Checker(multiplayer).availableOptions()) ==
               PostSelectionDecision::Roll, "Another final chaser prevents claiming a guaranteed win.");
}

void featureValidation()
{
    for (const auto bad : {-1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try {
            ComputerController controller({}, ComputerDifficulty::Hard, std::nullopt, {bad, false, false});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected, "Invalid research weights must not silently enter a candidate.");
    }
    bool rejected = false;
    try {
        ComputerController controller({}, ComputerDifficulty::Hard, std::nullopt, {0, false, false, false, true});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "Chains-only scope must not silently enable a disabled joint planner.");
    const ComputerController baseline(zilch::policyForDifficulty(ComputerDifficulty::Hard),
                                      ComputerDifficulty::Hard, std::nullopt, {});
    const auto features = baseline.researchFeatures();
    expect(features.chainRiskWeight == 0 && !features.safeFinishCollection && !features.lowerChainThresholds &&
               !features.jointSelection && !features.jointChainsOnly,
           "An explicit empty feature pack must retain the old research baseline.");
    const ComputerController released(zilch::policyForDifficulty(ComputerDifficulty::Hard), ComputerDifficulty::Hard);
    const auto approved = released.researchFeatures();
    expect(approved.chainRiskWeight == 1.0 && approved.safeFinishCollection && approved.lowerChainThresholds &&
               approved.jointSelection && approved.jointChainsOnly,
           "Named Hard must default to the exact frozen chain-scoped blend-one feature pack.");
    for (const auto difficulty : {std::optional<ComputerDifficulty>{},
                                  std::optional{ComputerDifficulty::Easy},
                                  std::optional{ComputerDifficulty::Medium}}) {
        const ComputerController unchanged({}, difficulty);
        const auto inactive = unchanged.researchFeatures();
        expect(inactive.chainRiskWeight == 0 && !inactive.safeFinishCollection &&
                   !inactive.lowerChainThresholds && !inactive.jointSelection && !inactive.jointChainsOnly,
               "Raw, Easy and Medium constructors must not acquire the Hard feature pack.");
    }
}

struct PlannedResult {
    GameManager game;
    PostSelectionDecision decision;
    std::vector<std::string> selections;
};

PlannedResult selectRoll(GameManager game, ComputerController& controller)
{
    std::vector<std::string> selections;
    for (unsigned int step = 0; step < 6; ++step) {
        const auto options = zilch::Checker(game).availableOptions();
        expect(!options.empty(), "A planned SelectAgain must still have a legal option.");
        const auto index = controller.chooseOption(game, options);
        expect(index < options.size(), "Committed joint path must reference the actual current options.");
        selections.push_back(options[index].label);
        zilch::Checker(game).applyOption(options[index]);
        const auto decision = controller.decideAfterSelection(game, zilch::Checker(game).availableOptions());
        if (decision != PostSelectionDecision::SelectAgain)
            return {std::move(game), decision, std::move(selections)};
    }
    throw std::runtime_error("Joint selection did not finish within six selections.");
}

void samePlan(const PlannedResult& actual, const PlannedResult& expected, const char* message)
{
    const auto& actualPlayer = actual.game.currentPlayer();
    const auto& expectedPlayer = expected.game.currentPlayer();
    expect(actual.decision == expected.decision && actual.selections == expected.selections &&
               actualPlayer.score().roundScore() == expectedPlayer.score().roundScore() &&
               actualPlayer.dice().numDiceInPlay() == expectedPlayer.dice().numDiceInPlay() &&
               actualPlayer.dice().diceSetMap() == expectedPlayer.dice().diceSetMap(), message);
    for (std::uint16_t face = 1; face <= 6; ++face)
        expect(actual.game.savedMultipleScore(face) == expected.game.savedMultipleScore(face), message);
}

void jointSelectionAndEndgames()
{
    const auto hard = zilch::policyForDifficulty(ComputerDifficulty::Hard);
    ComputerController joint(hard, ComputerDifficulty::Hard, std::nullopt, {1.0, false, false, true});
    auto extension = chainState(6, 3, 600);
    extension.currentPlayer().dice().diceSetMap() = {{1, 1}, {5, 1}, {6, 1}};
    extension.setSelectedOption(false);
    const auto hot = selectRoll(extension, joint);
    expect(hot.decision == PostSelectionDecision::Roll &&
               hot.game.currentPlayer().score().roundScore() == 1350 &&
               hot.game.currentPlayer().dice().numDiceInPlay() == 6 && !hot.game.hasSavedMultiple(6),
           "Joint search must consider collecting the chain extension plus both singles for hot dice.");
    expect(extension.currentPlayer().score().roundScore() == 600 && extension.hasSavedMultiple(6),
           "Joint search must leave its source checkpoint unchanged.");

    auto triple = chainState(6, 3, 0);
    triple.clearSavedMultiples();
    triple.manageDiceCount(6);
    triple.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}, {6, 3}};
    triple.setSelectedOption(false);
    const auto preserve = selectRoll(triple, joint);
    expect(preserve.decision == PostSelectionDecision::Roll &&
               preserve.game.currentPlayer().score().roundScore() == 600 &&
               preserve.game.currentPlayer().dice().numDiceInPlay() == 3,
           "Joint search must also be able to leave a five unclaimed while keeping the stronger triple-six chain.");

    // Intrinsic guaranteed-win priority is tested with safeFinishCollection
    // deliberately false, so no lucrative Roll can discard a certain win.
    const auto finish = selectRoll(finishState(4900, 5500), joint);
    expect(finish.decision == PostSelectionDecision::Bank &&
               finish.game.currentPlayer().score().roundScore() == 650,
           "Joint planning must collect the five to secure an outright final-chase win.");
    const auto immediate = selectRoll(finishState(4350, 0, false), joint);
    expect(immediate.decision == PostSelectionDecision::Bank &&
               immediate.game.currentPlayer().score().roundScore() == 650,
           "Joint planning must prioritize an available immediate win with Final Chase off.");
    const auto noTie = selectRoll(finishState(4850, 5500), joint);
    expect(noTie.decision == PostSelectionDecision::Roll,
           "An equal final total with ties off cannot become a joint bank candidate.");
    auto tieGame = finishState(4850, 5500);
    tieGame.ruleConfig().setAllowTies(true);
    tieGame.manageDiceCount(1);
    tieGame.currentPlayer().score().setRoundScore(550);
    tieGame.currentPlayer().dice().diceSetMap() = {{1, 1}};
    const auto allowedTie = selectRoll(tieGame, joint);
    expect(allowedTie.decision == PostSelectionDecision::Bank &&
               allowedTie.game.currentPlayer().score().roundScore() == 650,
           "Existing final-chase tie acceptance remains available when ties are enabled.");

    auto shortGame = chainState(6, 3, 4850);
    shortGame.setScoreLimit(5000);
    shortGame.players()[0].score() = zilch::Score{};
    shortGame.players()[1].score() = zilch::Score{};
    shortGame.players()[1].score().addPermanentScore(4400);
    shortGame.clearSavedMultiples();
    shortGame.manageDiceCount(6);
    shortGame.currentPlayer().score().setRoundScore(4850);
    shortGame.currentPlayer().dice().diceSetMap() = {{1, 2}, {2, 1}, {3, 1}, {4, 1}, {6, 1}};
    shortGame.setSelectedOption(false);
    const auto stopShort = selectRoll(shortGame, joint);
    expect(stopShort.decision == PostSelectionDecision::Bank &&
               stopShort.game.currentPlayer().score().roundScore() == 4950 &&
               stopShort.game.currentPlayer().dice().numDiceInPlay() == 5,
           "Joint search must consider a legal stop-short bank subset without the old collect-all latch.");

    auto unopened = triple;
    unopened.ruleConfig().setOpeningScoreLimit(3000);
    const auto opening = selectRoll(unopened, joint);
    expect(opening.decision == PostSelectionDecision::Roll && !opening.game.canBankCurrentScore(),
           "Joint search must keep rolling when no selection satisfies the opening minimum.");
    auto stealing = extension;
    stealing.ruleConfig().setStealingEnabled(true);
    ComputerController standardSteal(zilch::policyForDifficulty(ComputerDifficulty::Hard, true),
                                     ComputerDifficulty::Hard);
    ComputerController jointSteal(zilch::policyForDifficulty(ComputerDifficulty::Hard, true),
                                  ComputerDifficulty::Hard, std::nullopt, {1.0, true, true, true});
    const auto oldSteal = selectRoll(stealing, standardSteal);
    const auto newSteal = selectRoll(stealing, jointSteal);
    expect(oldSteal.decision == newSteal.decision &&
               oldSteal.game.currentPlayer().score().roundScore() == newSteal.game.currentPlayer().score().roundScore() &&
               oldSteal.game.currentPlayer().dice().numDiceInPlay() == newSteal.game.currentPlayer().dice().numDiceInPlay(),
           "Joint research must not change the separate Stealing policy.");
}

void chainsOnlyOrdinaryParity()
{
    const auto hard = zilch::policyForDifficulty(ComputerDifficulty::Hard);
    std::size_t checked = 0;
    // Exhaust every unordered non-chain roll of one through six dice. Include
    // scoring combinations that look special but are not chains (sets and
    // straights), opening turns, leads, deficits, and final-chase decisions.
    for (const bool safeFinish : {false, true}) {
        ComputerController ordinary(hard, ComputerDifficulty::Hard, std::nullopt,
                                    {0.75, safeFinish, true, false, false});
        ComputerController scoped(hard, ComputerDifficulty::Hard, std::nullopt,
                                  {0.75, safeFinish, true, true, true});
        for (const bool sets : {false, true}) {
            for (const auto position : {0, 1, 2, 3, 4}) {
                for (const auto risk : {0U, 600U, 1400U, 2500U, 4850U}) {
                    for (std::uint16_t count = 1; count <= 6; ++count) {
                        if (count < 6 && risk == 0)
                            continue;
                        auto game = finishState(position == 0 ? 0 : position == 4 ? 4900 :
                                                position == 2 ? 3500 : 1000,
                                                position == 4 ? 5500 : position == 3 ? 3500 :
                                                position == 0 ? 0 : 1000,
                                                position == 4);
                        game.ruleConfig().setFinalChaseEnabled(true);
                        game.ruleConfig().setThreePairsEnabled(sets);
                        game.currentPlayer().score().setRoundScore(risk);
                        game.manageDiceCount(count);
                        game.currentPlayer().dice().diceSetMap().clear();
                        std::function<void(std::uint16_t, std::uint16_t)> enumerate =
                            [&](const std::uint16_t left, const std::uint16_t minimum) {
                            if (left == 0) {
                                const auto options = zilch::Checker(game).availableOptions();
                                if (options.empty() || std::any_of(options.begin(), options.end(),
                                    [](const zilch::ScoringOption& option) {
                                        return option.type == zilch::OptionType::Multiple;
                                    }))
                                    return;
                                samePlan(selectRoll(game, scoped), selectRoll(game, ordinary),
                                         "Chain-only mode changed a non-chain selection or action.");
                                ++checked;
                                return;
                            }
                            for (std::uint16_t face = minimum; face <= 6; ++face) {
                                auto& dice = game.currentPlayer().dice().diceSetMap();
                                ++dice[face];
                                enumerate(static_cast<std::uint16_t>(left - 1), face);
                                if (--dice[face] == 0)
                                    dice.erase(face);
                            }
                        };
                        enumerate(count, 1);
                    }
                }
            }
        }
    }
    expect(checked > 10000, "Non-chain parity panel unexpectedly lost its exhaustive roll coverage.");
    std::cout << "Verified " << checked << " non-chain selection/action parity checkpoints.\n";
}

void chainsOnlySelectionAndLifetime()
{
    const auto hard = zilch::policyForDifficulty(ComputerDifficulty::Hard);
    const ResearchFeatures scopedFeatures{1.0, false, false, true, true};
    ComputerController scoped(hard, ComputerDifficulty::Hard, std::nullopt, scopedFeatures);
    ComputerController full(hard, ComputerDifficulty::Hard, std::nullopt, {1.0, false, false, true});
    ComputerController ordinary(hard, ComputerDifficulty::Hard, std::nullopt, {1.0, false, false});
    for (std::uint16_t face = 1; face <= 6; ++face) {
        for (const auto risk : {0U, 600U, 2000U, 3500U}) {
            auto game = chainState(face, 3, risk);
            game.clearSavedMultiples();
            game.manageDiceCount(6);
            game.currentPlayer().dice().diceSetMap() = {{2, 1}, {3, 1}, {5, 1}};
            game.currentPlayer().dice().diceSetMap()[face] += 3;
            game.setSelectedOption(false);
            samePlan(selectRoll(game, scoped), selectRoll(game, full),
                     "A currently available multiple must receive unchanged full-joint planning.");
        }
        for (std::uint16_t count = 3; count <= 5; ++count) {
            auto game = chainState(face, count, 5000);
            for (std::uint16_t index = count; index < 6; ++index)
                ++game.currentPlayer().dice().diceSetMap()[index == count ? face : index == 4 ? 1 : 5];
            game.setSelectedOption(false);
            samePlan(selectRoll(game, scoped), selectRoll(game, full),
                     "A saved chain or its extension must receive unchanged full-joint planning.");
        }
    }

    auto triple = finishState(0, 0, false);
    triple.ruleConfig().setFinalChaseEnabled(true);
    const auto preserve = selectRoll(triple, scoped);
    expect(preserve.decision == PostSelectionDecision::Roll &&
               preserve.game.currentPlayer().score().roundScore() == 600 &&
               preserve.game.currentPlayer().dice().numDiceInPlay() == 3,
           "Chain-only mode must preserve the original triple-six plus five choice.");

    auto extension = chainState(6, 3, 600);
    extension.currentPlayer().dice().diceSetMap() = {{1, 1}, {5, 1}, {6, 1}};
    extension.setSelectedOption(false);
    const auto hot = selectRoll(extension, scoped);
    expect(hot.decision == PostSelectionDecision::Roll && hot.game.currentPlayer().score().roundScore() == 1350 &&
               hot.game.currentPlayer().dice().numDiceInPlay() == 6 && !hot.game.hasSavedMultiple(6),
           "Chain scope must remain latched through the final selection clearing hot-dice chains.");
    auto nextRoll = hot.game;
    nextRoll.currentPlayer().dice().diceSetMap() = {{1, 2}, {2, 1}, {3, 1}, {4, 1}, {6, 1}};
    nextRoll.setSelectedOption(false);
    nextRoll.registerRoll();
    samePlan(selectRoll(nextRoll, scoped), selectRoll(nextRoll, ordinary),
             "A new hot-dice roll without a chain must return to ordinary greedy selection.");

    // Keep an unfinished path, then move to another actual roll. The plan's
    // remaining option indexes must not survive even if selectedOption is true
    // because a checkpoint caller already scored something on the new roll.
    auto interrupted = extension;
    auto options = zilch::Checker(interrupted).availableOptions();
    zilch::Checker(interrupted).applyOption(options[scoped.chooseOption(interrupted, options)]);
    expect(scoped.decideAfterSelection(interrupted, zilch::Checker(interrupted).availableOptions()) ==
               PostSelectionDecision::SelectAgain, "Lifetime fixture must leave a multi-step joint path pending.");
    interrupted.currentPlayer().dice().diceSetMap() = {{6, 1}};
    interrupted.manageDiceCount(1);
    interrupted.registerRoll();
    ComputerController fresh(hard, ComputerDifficulty::Hard, std::nullopt, scopedFeatures);
    samePlan(selectRoll(interrupted, scoped), selectRoll(interrupted, fresh),
             "A joint path must be recomputed after rollCount changes.");

    // Turn-start resets must work independently of score selection flags.
    interrupted = extension;
    options = zilch::Checker(interrupted).availableOptions();
    zilch::Checker(interrupted).applyOption(options[scoped.chooseOption(interrupted, options)]);
    scoped.decideTurnStart(nextRoll);
    samePlan(selectRoll(nextRoll, scoped), selectRoll(nextRoll, ordinary),
             "Starting a new turn must clear every previous joint scope latch.");

    interrupted = extension;
    options = zilch::Checker(interrupted).availableOptions();
    zilch::Checker(interrupted).applyOption(options[scoped.chooseOption(interrupted, options)]);
    auto nextPlayer = extension;
    nextPlayer.startTurn(1);
    nextPlayer.currentPlayer().score().setRoundScore(5000);
    nextPlayer.manageDiceCount(1);
    nextPlayer.setSavedMultipleScore(6, 2400);
    nextPlayer.currentPlayer().dice().diceSetMap() = {{6, 1}};
    nextPlayer.registerRoll();
    nextPlayer.setSelectedOption(true);
    samePlan(selectRoll(nextPlayer, scoped), selectRoll(nextPlayer, fresh),
             "A player change must discard a prior path even when roll count and selection flag match.");

    auto disabled = triple;
    disabled.ruleConfig().setMultiplesEnabled(false);
    samePlan(selectRoll(disabled, scoped), selectRoll(disabled, ordinary),
             "Disabled multiples must leave ordinary selection unchanged.");
    auto savedWithoutExtension = chainState(6, 3, 2000);
    savedWithoutExtension.currentPlayer().dice().diceSetMap() = {{1, 1}, {2, 1}, {5, 1}};
    savedWithoutExtension.setSelectedOption(false);
    samePlan(selectRoll(savedWithoutExtension, scoped), selectRoll(savedWithoutExtension, full),
             "A saved chain alone must activate scope even without a rolled extension.");

    for (const auto difficulty : {ComputerDifficulty::Easy, ComputerDifficulty::Medium}) {
        ComputerController control(zilch::policyForDifficulty(difficulty), difficulty);
        ComputerController restricted(zilch::policyForDifficulty(difficulty), difficulty, std::nullopt, scopedFeatures);
        samePlan(selectRoll(extension, restricted), selectRoll(extension, control),
                 "Chain-only mode must not affect Easy or Medium.");
    }
    auto stealing = extension;
    stealing.ruleConfig().setStealingEnabled(true);
    ComputerController controlSteal(zilch::policyForDifficulty(ComputerDifficulty::Hard, true), ComputerDifficulty::Hard);
    ComputerController scopedSteal(zilch::policyForDifficulty(ComputerDifficulty::Hard, true),
                                   ComputerDifficulty::Hard, std::nullopt, scopedFeatures);
    samePlan(selectRoll(stealing, scopedSteal), selectRoll(stealing, controlSteal),
             "Chain-only mode must not affect Stealing.");
}

void productionDecisionParity()
{
    // Independent literal: accidental production-default changes must fail.
    constexpr ResearchFeatures frozen{1.0, true, true, true, true};
    const auto policy = zilch::policyForDifficulty(ComputerDifficulty::Hard);
    ComputerController production(policy, ComputerDifficulty::Hard);
    ComputerController candidate(policy, ComputerDifficulty::Hard, std::nullopt, frozen);
    struct Position { std::uint32_t own; std::uint32_t opponent; bool chase; bool ties; bool finalRule; };
    const Position positions[] = {{0, 0, false, true, true}, {1000, 1000, false, true, true},
        {3500, 1000, false, true, true}, {1000, 3500, false, true, true},
        {4500, 4500, false, true, true}, {4900, 5500, true, false, true},
        {4850, 5500, true, true, true}, {4350, 0, false, true, false}};
    std::size_t checked = 0;
    for (const bool sets : {false, true}) {
        for (const auto& position : positions) {
            zilch::RuleConfig rules;
            rules.setThreePairsEnabled(sets);
            rules.setAllowTies(position.ties);
            rules.setFinalChaseEnabled(position.finalRule);
            for (std::uint16_t face = 1; face <= 6; ++face) {
                for (std::uint16_t count = 3; count <= 5; ++count) {
                    for (const std::uint16_t single : {1, 5}) {
                        if (single == face)
                            continue;
                        std::vector<std::uint16_t> roll(count, face);
                        roll.push_back(single);
                        for (const std::uint16_t filler : {2, 3, 4, 6}) {
                            if (roll.size() == 6)
                                break;
                            if (filler != face)
                                roll.push_back(filler);
                        }
                        for (const auto risk : {0U, 350U, 950U, 1500U, 2800U, 5000U}) {
                            const auto game = zilch::research::makeSelectionCheckpoint(
                                {rules, 5000, risk, position.own, position.opponent, roll, {}, position.chase});
                            samePlan(selectRoll(game, production), selectRoll(game, candidate),
                                     "Production must match the frozen candidate across risk, match position, and multiple face/size.");
                            ++checked;
                        }
                    }
                }
                for (std::uint16_t held = 3; held <= 5; ++held) {
                    const auto base = face == 1 ? 1000U : static_cast<unsigned int>(face * 100);
                    const auto savedScore = base << (held - 3);
                    std::array<std::uint32_t, 6> saved{};
                    saved[face - 1] = savedScore;
                    std::vector<std::uint16_t> roll{face};
                    if (held < 5)
                        roll.push_back(face == 5 ? 1 : 5);
                    if (held < 4)
                        roll.push_back(face == 1 ? 5 : 1);
                    for (const auto extraRisk : {0U, 350U, 950U, 2800U}) {
                        const auto game = zilch::research::makeSelectionCheckpoint(
                            {rules, 5000, savedScore + extraRisk, position.own, position.opponent,
                             roll, saved, position.chase});
                        samePlan(selectRoll(game, production), selectRoll(game, candidate),
                                 "Production must match frozen saved-chain extension and hot-dice choices.");
                        ++checked;
                    }
                }
            }
        }
    }
    expect(checked == 4032, "Production decision panel lost a declared axis or saved-chain case.");
    std::cout << "Verified " << checked << " production-versus-frozen-feature selection checkpoints.\n";

    const auto extension = zilch::research::makeSelectionCheckpoint(
        {{}, 5000, 600, 1000, 1000, {6, 1, 5}, {0, 0, 0, 0, 0, 600}, false});
    const auto startPending = [&]() {
        auto pending = extension;
        const auto options = zilch::Checker(pending).availableOptions();
        zilch::Checker(pending).applyOption(options[production.chooseOption(pending, options)]);
        expect(production.decideAfterSelection(pending, zilch::Checker(pending).availableOptions()) ==
                   PostSelectionDecision::SelectAgain, "Production lifetime fixture needs a pending joint path.");
    };
    auto nextRoll = zilch::research::makeSelectionCheckpoint(
        {{}, 5000, 1350, 1000, 1000, {1, 1, 2, 3, 4, 6}, {}, false});
    nextRoll.registerRoll();
    nextRoll.setSelectedOption(true);
    startPending();
    samePlan(selectRoll(nextRoll, production), selectRoll(nextRoll, candidate),
             "Production must discard its pending chain plan when the next roll no longer has a chain.");
    startPending();
    production.decideTurnStart(nextRoll);
    samePlan(selectRoll(nextRoll, production), selectRoll(nextRoll, candidate),
             "Production turn-start reset must discard an unfinished selection path.");
    startPending();
    auto nextPlayer = extension;
    nextPlayer.startTurn(1);
    nextPlayer.currentPlayer().score().setRoundScore(600);
    nextPlayer.manageDiceCount(3);
    nextPlayer.setSavedMultipleScore(6, 600);
    nextPlayer.currentPlayer().dice().diceSetMap() = {{1, 1}, {5, 1}, {6, 1}};
    nextPlayer.registerRoll();
    nextPlayer.registerRoll();
    nextPlayer.setSelectedOption(true);
    samePlan(selectRoll(nextPlayer, production), selectRoll(nextPlayer, candidate),
             "Production must discard another player's plan even with the same roll count and selection flag.");
}

void productionMatchParity()
{
    constexpr ResearchFeatures frozen{1.0, true, true, true, true};
    std::size_t checked = 0;
    for (const auto target : {5000U, 10000U}) {
        for (const bool stealing : {false, true}) {
            for (const bool sets : {false, true}) {
                for (const auto difficulty : {std::optional<ComputerDifficulty>{},
                                              std::optional{ComputerDifficulty::Easy},
                                              std::optional{ComputerDifficulty::Medium},
                                              std::optional{ComputerDifficulty::Hard}}) {
                    const auto policy = difficulty ? zilch::policyForDifficulty(*difficulty, stealing) : zilch::Policy{};
                    const auto expectedFeatures = difficulty == ComputerDifficulty::Hard && !stealing ? frozen : ResearchFeatures{};
                    const auto opponentPolicy = zilch::policyForDifficulty(ComputerDifficulty::Medium, stealing);
                    ComputerController production(policy, difficulty);
                    ComputerController candidate(policy, difficulty, std::nullopt, expectedFeatures);
                    ComputerController firstOpponent(opponentPolicy, ComputerDifficulty::Medium);
                    ComputerController secondOpponent(opponentPolicy, ComputerDifficulty::Medium, std::nullopt, {});
                    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
                        for (const bool swapped : {false, true}) {
                            GameManager game;
                            game.setPlayers({"A", "B"});
                            game.setScoreLimit(target);
                            game.ruleConfig().setStealingEnabled(stealing);
                            game.ruleConfig().setThreePairsEnabled(sets);
                            std::mt19937 firstRng(seed + 3500000000U);
                            std::mt19937 secondRng(seed + 3500000000U);
                            const std::vector<zilch::Controller*> first = swapped
                                ? std::vector<zilch::Controller*>{&firstOpponent, &production}
                                : std::vector<zilch::Controller*>{&production, &firstOpponent};
                            const std::vector<zilch::Controller*> second = swapped
                                ? std::vector<zilch::Controller*>{&secondOpponent, &candidate}
                                : std::vector<zilch::Controller*>{&candidate, &secondOpponent};
                            std::ostringstream firstLog;
                            std::ostringstream secondLog;
                            const auto actual = zilch::playMatchFromState(game, first, firstRng,
                                zilch::MatchEntry::StartTurn, &firstLog);
                            const auto expected = zilch::playMatchFromState(game, second, secondRng,
                                zilch::MatchEntry::StartTurn, &secondLog);
                            expect(actual.finalScores == expected.finalScores && actual.winnerIndex == expected.winnerIndex &&
                                       actual.winningScore == expected.winningScore && firstRng == secondRng &&
                                       firstLog.str() == secondLog.str(),
                                   "Production and frozen-feature controllers must reproduce complete seeded match transcripts and RNG state.");
                            ++checked;
                        }
                    }
                }
            }
        }
    }
    expect(checked == 2048, "Seeded production/default-isolation parity lost a rule, target, seat, or difficulty.");
    std::cout << "Verified " << checked << " seeded production/frozen-or-unchanged full-match pairs.\n";
}

} // namespace

int main()
{
    try {
        exactMomentsAndCache();
        controlledThresholds();
        safeFinishAndLifetime();
        featureValidation();
        jointSelectionAndEndgames();
        chainsOnlyOrdinaryParity();
        chainsOnlySelectionAndLifetime();
        productionDecisionParity();
        productionMatchParity();
        std::cout << "Research candidate scoring, cache, threshold, safe-finish and scoped joint-selection tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
