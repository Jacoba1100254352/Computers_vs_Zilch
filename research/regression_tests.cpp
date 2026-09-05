#include "computer.h"
#include "selection_checkpoint.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

void expect(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

zilch::GameManager state(const bool finalChase = true)
{
    zilch::GameManager game;
    game.setPlayers({"A", "B"});
    game.setScoreLimit(2800);
    game.ruleConfig().setFinalChaseEnabled(finalChase);
    game.startTurn(0);
    game.currentPlayer().score().setRoundScore(2800);
    game.setSelectedOption(true);
    game.registerRoll();
    return game;
}

class RecordingController final : public zilch::Controller {
public:
    std::uint32_t firstRoundScore{};
    std::uint32_t firstRollCount{};
    unsigned int choices{};
    zilch::ComputerController wrapped{zilch::policyForDifficulty(zilch::ComputerDifficulty::Easy),
                                      zilch::ComputerDifficulty::Easy};

    zilch::TurnStartDecision decideTurnStart(zilch::GameManager& game) override
    {
        return wrapped.decideTurnStart(game);
    }

    std::size_t chooseOption(zilch::GameManager& game, const std::vector<zilch::ScoringOption>& options) override
    {
        if (choices++ == 0) {
            firstRoundScore = game.currentPlayer().score().roundScore();
            firstRollCount = game.rollCountThisTurn();
        }
        return wrapped.chooseOption(game, options);
    }

    zilch::PostSelectionDecision decideAfterSelection(
        zilch::GameManager& game, const std::vector<zilch::ScoringOption>& options) override
    {
        return wrapped.decideAfterSelection(game, options);
    }
};

void bankKeepsStateAndRng()
{
    const auto game = state(false);
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(1);
    const auto originalRng = rng;
    const auto result = zilch::playMatchFromState(game, {&a, &b}, rng, zilch::MatchEntry::BankCurrentTurn);
    expect(result.winnerIndex == 0, "Forced bank at target must win without Final Chase.");
    expect(result.finalScores[0] == 2800 && result.finalScores[1] == 0, "Forced bank must preserve all 2800 points.");
    expect(a.choices == 0 && b.choices == 0, "Forced bank must not request any new rolls.");
    expect(rng == originalRng, "Forced bank must not consume random numbers.");
    expect(game.currentPlayer().score().roundScore() == 2800 && game.turnActive(), "Research entry must copy the source state.");
}

void bankRetainsFinalChase()
{
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(1);
    const auto result = zilch::playMatchFromState(state(), {&a, &b}, rng, zilch::MatchEntry::BankCurrentTurn);
    expect(result.finalScores[0] == 2800, "Final Chase must not give its starter another turn.");
    expect(a.choices == 0 && b.choices > 0, "Final Chase must give the opponent its final turn.");
}

void rollRetainsTurnScore()
{
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(1);
    const auto result = zilch::playMatchFromState(state(false), {&a, &b}, rng, zilch::MatchEntry::RollCurrentTurn);
    expect(a.choices > 0 && a.firstRoundScore == 2800, "Resuming a roll must not reset the at-risk points.");
    expect(a.firstRollCount == 2, "Resumed roll must remain a later roll, not a first roll.");
    expect(result.finalScores[0] > 2800, "Successful resumed roll must bank its gain plus the existing score.");
}

void resumedBustIsNotMercy()
{
    std::uint32_t bustSeed = 0;
    for (; bustSeed < 10000; ++bustSeed) {
        auto game = state();
        std::mt19937 rng(bustSeed);
        game.currentPlayer().dice().rollDice(rng);
        if (!zilch::Checker(game).hasAvailableOption())
            break;
    }
    expect(bustSeed < 10000, "Test must find a deterministic six-dice bust.");
    auto game = state(false);
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(bustSeed);
    std::ostringstream output;
    const auto result = zilch::playMatchFromState(game, {&a, &b}, rng, zilch::MatchEntry::RollCurrentTurn, &output);
    static_cast<void>(result);
    expect(output.str().find("Round score: 2800\nBust. The turn ends with 0 round points.") != std::string::npos,
           "First-roll mercy must not rescue a bust on resumed hot dice.");
    expect(b.choices > 0, "A resumed bust must pass the turn to the opponent.");
}

void mirroredIdentity()
{
    const auto policy = zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard);
    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
        zilch::ComputerController firstA(policy, zilch::ComputerDifficulty::Hard);
        zilch::ComputerController firstB(policy, zilch::ComputerDifficulty::Hard);
        zilch::ComputerController secondA(policy, zilch::ComputerDifficulty::Hard);
        zilch::ComputerController secondB(policy, zilch::ComputerDifficulty::Hard);
        auto game = state();
        std::mt19937 firstRng(seed);
        std::mt19937 secondRng(seed);
        const auto first = zilch::playMatchFromState(game, {&firstA, &firstB}, firstRng);
        const auto second = zilch::playMatchFromState(game, {&secondB, &secondA}, secondRng);
        expect(first.finalScores == second.finalScores && first.winnerIndex == second.winnerIndex,
               "Identical policy mirrors must have identical seat outcomes and exactly half match points per policy.");
    }
}

void invalidResumeRejected()
{
    auto game = state();
    game.setSelectedOption(false);
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(1);
    try {
        static_cast<void>(zilch::playMatchFromState(game, {&a, &b}, rng, zilch::MatchEntry::BankCurrentTurn));
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Research entry must reject banking without a valid selection.");
}

template <typename Action>
void expectInvalid(Action action, const char* message)
{
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

zilch::research::SelectionCheckpoint selectionCheckpoint()
{
    zilch::research::SelectionCheckpoint checkpoint;
    checkpoint.roll = {6, 6, 6, 5, 2, 3};
    return checkpoint;
}

void selectionUsesRealScoringAndCopiesState()
{
    const auto initial = zilch::research::makeSelectionCheckpoint(selectionCheckpoint());
    const auto triple = zilch::research::makeSelectionBranch(initial, {6, 6, 6}, zilch::MatchEntry::RollCurrentTurn);
    const auto all = zilch::research::makeSelectionBranch(initial, {6, 6, 6, 5}, zilch::MatchEntry::RollCurrentTurn);
    expect(triple.game.currentPlayer().score().roundScore() == 600 &&
           triple.game.currentPlayer().dice().numDiceInPlay() == 3 && triple.game.savedMultipleScore(6) == 600,
           "Triple-only branch must score 600, save the six chain, and retain three dice.");
    expect(all.game.currentPlayer().score().roundScore() == 650 &&
           all.game.currentPlayer().dice().numDiceInPlay() == 2 && all.game.savedMultipleScore(6) == 600,
           "Triple-plus-five branch must score 650 and retain two dice and the six chain.");
    expect(initial.currentPlayer().score().roundScore() == 0 &&
           initial.currentPlayer().dice().numDiceInPlay() == 6 && !initial.selectedOption(),
           "Branching must not mutate the common pre-selection state.");
    expectInvalid([&] { zilch::research::makeSelectionBranch(initial, {6, 6}, zilch::MatchEntry::RollCurrentTurn); },
                  "A partial non-scoring multiple must be rejected.");
    expectInvalid([&] { zilch::research::makeSelectionBranch(initial, {6, 6, 6, 6}, zilch::MatchEntry::RollCurrentTurn); },
                  "A selection containing unavailable dice must be rejected.");
    expectInvalid([&] { zilch::research::makeSelectionBranch(initial, {6, 6, 6, 2}, zilch::MatchEntry::RollCurrentTurn); },
                  "A selection containing a non-scoring die must be rejected.");
    expectInvalid([&] { zilch::research::makeSelectionBranch(initial, {6, 6, 6, 5}, zilch::MatchEntry::BankCurrentTurn); },
                  "Bank below the opening minimum must fail, not silently become a roll.");
}

void multipleFacesAndCounts()
{
    for (std::uint16_t face = 1; face <= 6; ++face) {
        auto checkpoint = selectionCheckpoint();
        checkpoint.roll = {face, face, face, static_cast<std::uint16_t>(face == 1 ? 5 : 1)};
        for (std::uint16_t filler = 2; checkpoint.roll.size() < 6; ++filler) {
            if (filler != face && filler != 5)
                checkpoint.roll.push_back(filler);
        }
        const auto branch = zilch::research::makeSelectionBranch(
            zilch::research::makeSelectionCheckpoint(checkpoint), {face, face, face}, zilch::MatchEntry::RollCurrentTurn);
        expect(branch.game.currentPlayer().score().roundScore() == (face == 1 ? 1000U : face * 100U),
               "All six triple faces must use the real engine's scoring scale.");
    }
    for (const std::size_t count : {4U, 5U}) {
        auto checkpoint = selectionCheckpoint();
        checkpoint.roll.assign(count, 6);
        checkpoint.roll.push_back(5);
        if (count == 4)
            checkpoint.roll.push_back(2);
        auto selected = std::vector<std::uint16_t>(count, 6);
        const auto initial = zilch::research::makeSelectionCheckpoint(checkpoint);
        const auto multiple = zilch::research::makeSelectionBranch(initial, selected, zilch::MatchEntry::RollCurrentTurn);
        expect(multiple.game.currentPlayer().score().roundScore() == (count == 4 ? 1200U : 2400U),
               "Four/five of a kind must double the multiple value at each added die.");
        selected.push_back(5);
        const auto all = zilch::research::makeSelectionBranch(initial, selected, zilch::MatchEntry::RollCurrentTurn);
        if (count == 5)
            expect(all.game.currentPlayer().dice().numDiceInPlay() == 6 && !all.game.hasSavedMultiple(6),
                   "Collecting all six dice must reset hot dice and clear the saved multiple.");
        else
            expect(all.game.currentPlayer().dice().numDiceInPlay() == 1 && all.game.savedMultipleScore(6) == 1200,
                   "Four-of-kind plus a single must retain its chain with one die left.");
    }
}

void extensionsAndFinalChase()
{
    auto checkpoint = selectionCheckpoint();
    checkpoint.roll = {6, 5, 2};
    checkpoint.atRisk = 600;
    checkpoint.savedMultiples[5] = 600;
    const auto initial = zilch::research::makeSelectionCheckpoint(checkpoint);
    const auto extend = zilch::research::makeSelectionBranch(initial, {6}, zilch::MatchEntry::RollCurrentTurn);
    expect(extend.options.size() == 1 && extend.options[0].extendsMultiple &&
           extend.game.currentPlayer().score().roundScore() == 1200 && extend.game.savedMultipleScore(6) == 1200 &&
           extend.game.currentPlayer().dice().numDiceInPlay() == 2,
           "Selection checkpoints must preserve and apply saved-chain extension bonuses.");
    checkpoint.roll = {6, 1, 5};
    const auto jointInitial = zilch::research::makeSelectionCheckpoint(checkpoint);
    const auto chainOnly = zilch::research::makeSelectionBranch(jointInitial, {6}, zilch::MatchEntry::RollCurrentTurn);
    const auto joint = zilch::research::makeSelectionBranch(jointInitial, {6, 1, 5}, zilch::MatchEntry::RollCurrentTurn);
    expect(chainOnly.game.currentPlayer().score().roundScore() == 1200 &&
           chainOnly.game.currentPlayer().dice().numDiceInPlay() == 2 && chainOnly.game.savedMultipleScore(6) == 1200,
           "Extending the saved triple alone must leave two dice with the doubled chain.");
    expect(joint.game.currentPlayer().score().roundScore() == 1350 &&
           joint.game.currentPlayer().dice().numDiceInPlay() == 6 && !joint.game.hasSavedMultiple(6),
           "Selecting extension plus one and five must produce 1350 and clear the chain through hot dice.");
    checkpoint.atRisk = 500;
    expectInvalid([&] { zilch::research::makeSelectionCheckpoint(checkpoint); },
                  "A saved multiple cannot exceed the score at risk.");
    checkpoint.atRisk = 600;
    checkpoint.roll = {6, 6, 6, 5, 2, 3};
    expectInvalid([&] { zilch::research::makeSelectionCheckpoint(checkpoint); },
                  "A full new six-die set cannot carry a saved multiple.");

    checkpoint = selectionCheckpoint();
    checkpoint.activeFinalChase = true;
    checkpoint.bankedA = 3500;
    checkpoint.bankedB = 5500;
    checkpoint.atRisk = 1450;
    const auto finalInitial = zilch::research::makeSelectionCheckpoint(checkpoint);
    expect(finalInitial.finalRoundActive() && finalInitial.finalRoundLeaderIndex() == 1 &&
           finalInitial.wouldEndAfterCurrentTurn(), "A must be taking the last turn chasing B's final-round score.");
    const auto bank = zilch::research::makeSelectionBranch(finalInitial, {6, 6, 6, 5}, zilch::MatchEntry::BankCurrentTurn);
    RecordingController a;
    RecordingController b;
    std::mt19937 rng(7);
    const auto beforeRng = rng;
    const auto result = zilch::playMatchFromState(bank.game, {&a, &b}, rng, bank.action);
    expect(result.finalScores == std::vector<std::uint32_t>{5600, 5500} && result.winnerIndex == 0 &&
           a.choices == 0 && b.choices == 0 && rng == beforeRng,
           "Forced bank in the active Final Chase must resolve the real match immediately without new dice.");
    checkpoint.activeFinalChase = false;
    expectInvalid([&] { zilch::research::makeSelectionCheckpoint(checkpoint); },
                  "A checkpoint over the target must not silently omit active Final Chase.");
}

} // namespace

int main()
{
    try {
        bankKeepsStateAndRng();
        bankRetainsFinalChase();
        rollRetainsTurnScore();
        resumedBustIsNotMercy();
        mirroredIdentity();
        invalidResumeRejected();
        selectionUsesRealScoringAndCopiesState();
        multipleFacesAndCounts();
        extensionsAndFinalChase();
        std::cout << "All resumed-state, selection checkpoint, and mirrored-identity tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
