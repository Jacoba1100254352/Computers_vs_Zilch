#ifndef ZILCH_RESEARCH_SELECTION_CHECKPOINT_H
#define ZILCH_RESEARCH_SELECTION_CHECKPOINT_H

#include "computer.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace zilch::research {

struct SelectionCheckpoint {
    RuleConfig rules;
    std::uint32_t target{5000};
    std::uint32_t atRisk{};
    std::uint32_t bankedA{};
    std::uint32_t bankedB{};
    std::vector<std::uint16_t> roll;
    std::array<std::uint32_t, 6> savedMultiples{};
    bool activeFinalChase{};
};

struct SelectionBranch {
    GameManager game;
    std::vector<ScoringOption> options;
    MatchEntry action{MatchEntry::RollCurrentTurn};
};

inline GameManager makeSelectionCheckpoint(const SelectionCheckpoint& checkpoint)
{
    if (checkpoint.roll.empty() || checkpoint.roll.size() > FULL_SET_OF_DICE)
        throw std::invalid_argument("Selection checkpoint requires one through six rolled dice.");
    if (checkpoint.activeFinalChase) {
        if (!checkpoint.rules.finalChaseEnabled() || checkpoint.bankedB < checkpoint.target ||
            checkpoint.bankedA >= checkpoint.target) {
            throw std::invalid_argument("Active Final Chase requires A below target, B at/above target, and Final Chase enabled.");
        }
    } else if (checkpoint.bankedA >= checkpoint.target || checkpoint.bankedB >= checkpoint.target) {
        throw std::invalid_argument("Without active Final Chase, both banked scores must be below target.");
    }
    if (checkpoint.roll.size() < FULL_SET_OF_DICE && checkpoint.atRisk == 0)
        throw std::invalid_argument("A partial-dice checkpoint requires prior points at risk.");

    // A saved chain must fit among dice already removed from this six-die set.
    // Validating reachability here prevents fabricated continuation bonuses.
    std::size_t savedDice{};
    std::uint64_t savedPoints{};
    for (std::size_t index = 0; index < checkpoint.savedMultiples.size(); ++index) {
        auto points = checkpoint.savedMultiples[index];
        if (points == 0)
            continue;
        const std::uint32_t base = index == 0 ? 1000U : static_cast<std::uint32_t>((index + 1) * 100);
        std::size_t count = 3;
        while (points > base && points % 2 == 0) {
            points /= 2;
            ++count;
        }
        if (!checkpoint.rules.multiplesEnabled() || points != base || count > 5)
            throw std::invalid_argument("Saved multiple score must represent a legal three-, four-, or five-die chain.");
        savedDice += count;
        savedPoints += checkpoint.savedMultiples[index];
    }
    if (savedDice + checkpoint.roll.size() > FULL_SET_OF_DICE || savedPoints > checkpoint.atRisk)
        throw std::invalid_argument("Saved multiples exceed the removed dice or pre-selection at-risk score.");

    GameManager game;
    game.setPlayers({"A", "B"});
    game.setScoreLimit(checkpoint.target);
    game.ruleConfig() = checkpoint.rules;
    game.players()[0].score().addPermanentScore(checkpoint.bankedA);
    game.players()[1].score().addPermanentScore(checkpoint.bankedB);
    if (checkpoint.activeFinalChase) {
        game.startTurn(1);
        game.beginFinalRound();
    }
    game.startTurn(0);
    game.currentPlayer().score().setRoundScore(checkpoint.atRisk);
    game.manageDiceCount(static_cast<std::uint16_t>(checkpoint.roll.size()));
    for (const auto face : checkpoint.roll) {
        if (face < 1 || face > 6)
            throw std::invalid_argument("Rolled die values must be one through six.");
        ++game.currentPlayer().dice().diceSetMap()[face];
    }
    for (std::uint16_t face = 1; face <= 6; ++face) {
        if (checkpoint.savedMultiples[face - 1] != 0)
            game.setSavedMultipleScore(face, checkpoint.savedMultiples[face - 1]);
    }
    // The fixed roll has happened. The next roll is necessarily a later roll,
    // even when the supplied checkpoint is the first scoring roll of the turn.
    game.registerRoll();
    return game;
}

inline std::array<std::uint16_t, 6> diceCounts(const std::vector<std::uint16_t>& dice)
{
    std::array<std::uint16_t, 6> counts{};
    for (const auto face : dice) {
        if (face < 1 || face > 6)
            throw std::invalid_argument("Selected die values must be one through six.");
        ++counts[face - 1];
    }
    return counts;
}

inline bool applyMatchingSelection(GameManager& game, const std::array<std::uint16_t, 6>& remaining,
                                   std::vector<ScoringOption>& path)
{
    if (std::all_of(remaining.begin(), remaining.end(), [](const auto count) { return count == 0; }))
        return true;
    Checker checker(game);
    for (const auto& option : checker.availableOptions()) {
        auto nextRemaining = remaining;
        std::array<std::uint16_t, 6> consumed{};
        if (option.type == OptionType::Straight || option.type == OptionType::ThreePairs) {
            for (const auto& [face, count] : game.currentPlayer().dice().diceSetMap())
                consumed[face - 1] = count;
        } else {
            consumed[option.dieValue - 1] = option.diceUsed;
        }
        bool fits = true;
        for (std::size_t index = 0; index < consumed.size(); ++index) {
            if (consumed[index] > remaining[index]) {
                fits = false;
                break;
            }
            nextRemaining[index] -= consumed[index];
        }
        if (!fits)
            continue;
        auto candidate = game;
        Checker(candidate).applyOption(option);
        path.push_back(option);
        if (applyMatchingSelection(candidate, nextRemaining, path)) {
            game = std::move(candidate);
            return true;
        }
        path.pop_back();
    }
    return false;
}

inline SelectionBranch makeSelectionBranch(const GameManager& initial,
                                           const std::vector<std::uint16_t>& selected,
                                           const MatchEntry action)
{
    if (selected.empty() || selected.size() > FULL_SET_OF_DICE)
        throw std::invalid_argument("Each branch must select one through six dice.");
    if (action != MatchEntry::RollCurrentTurn && action != MatchEntry::BankCurrentTurn)
        throw std::invalid_argument("Selection branch action must be roll or bank.");
    SelectionBranch branch{initial, {}, action};
    if (!applyMatchingSelection(branch.game, diceCounts(selected), branch.options))
        throw std::invalid_argument("Selection is not an exact sequence of legal scoring groups from this roll.");
    if (action == MatchEntry::BankCurrentTurn && !branch.game.canBankCurrentScore())
        throw std::invalid_argument("Cannot force bank from an unbankable selection branch.");
    return branch;
}

} // namespace zilch::research

#endif
