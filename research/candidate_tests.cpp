#include "computer.h"

#include <cmath>
#include <iostream>
#include <limits>
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
}

struct PlannedResult {
    GameManager game;
    PostSelectionDecision decision;
};

PlannedResult selectRoll(GameManager game, ComputerController& controller)
{
    for (unsigned int step = 0; step < 6; ++step) {
        const auto options = zilch::Checker(game).availableOptions();
        expect(!options.empty(), "A planned SelectAgain must still have a legal option.");
        const auto index = controller.chooseOption(game, options);
        expect(index < options.size(), "Committed joint path must reference the actual current options.");
        zilch::Checker(game).applyOption(options[index]);
        const auto decision = controller.decideAfterSelection(game, zilch::Checker(game).availableOptions());
        if (decision != PostSelectionDecision::SelectAgain)
            return {std::move(game), decision};
    }
    throw std::runtime_error("Joint selection did not finish within six selections.");
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

} // namespace

int main()
{
    try {
        exactMomentsAndCache();
        controlledThresholds();
        safeFinishAndLifetime();
        featureValidation();
        jointSelectionAndEndgames();
        std::cout << "Research candidate scoring, cache, threshold, safe-finish and joint-selection tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
