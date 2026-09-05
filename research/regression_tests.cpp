#include "computer.h"

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
        std::cout << "All resumed-state and mirrored-identity tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
