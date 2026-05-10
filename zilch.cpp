#include "zilch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <sstream>

namespace zilch {

namespace {

std::uint32_t multipleScoreFor(std::uint16_t dieValue, std::uint16_t diceUsed)
{
    const std::uint32_t base = dieValue == 1 ? 1000U : static_cast<std::uint32_t>(dieValue) * 100U;
    return base * (1U << (diceUsed - 3U));
}

std::uint32_t extendedMultipleScore(std::uint32_t existingScore, std::uint16_t additionalDice)
{
    return existingScore * (1U << additionalDice);
}

} // namespace

void Dice::rollDice(std::mt19937& rng)
{
    diceSetMap_.clear();
    lastRoll_.clear();
    lastRoll_.reserve(numDiceInPlay_);

    std::uniform_int_distribution<int> distribution(1, FULL_SET_OF_DICE);
    for (std::uint16_t i = 0; i < numDiceInPlay_; ++i) {
        const auto die = static_cast<std::uint16_t>(distribution(rng));
        lastRoll_.push_back(die);
        ++diceSetMap_[die];
    }
}

void Dice::setNumDiceInPlay(const std::uint16_t numDice)
{
    numDiceInPlay_ = std::min<std::uint16_t>(numDice, FULL_SET_OF_DICE);
}

void RuleConfig::adjustBankThreshold(const std::int32_t delta)
{
    const auto updated = static_cast<std::int64_t>(bankThreshold_) + static_cast<std::int64_t>(delta);
    bankThreshold_ = static_cast<std::uint32_t>(updated < 0 ? 0 : updated);
}

void GameManager::setPlayers(const std::vector<std::string>& playerNames)
{
    players_.clear();
    players_.reserve(playerNames.size());
    for (const auto& playerName : playerNames)
        players_.emplace_back(playerName);
    currentIndex_ = 0;
    turnActive_ = false;
    selectedOption_ = false;
    bustPending_ = false;
    hasRolledThisTurn_ = false;
    bustBonusUsedThisTurn_ = false;
    rollCountThisTurn_ = 0;
    finalRoundActive_ = false;
    finalRoundStarterIndex_ = 0;
    clearSavedMultiples();
}

void GameManager::switchToNextPlayer()
{
    if (players_.empty())
        return;
    currentIndex_ = (currentIndex_ + 1) % players_.size();
}

void GameManager::startTurn(const std::size_t playerIndex)
{
    if (players_.empty())
        return;

    currentIndex_ = playerIndex % players_.size();
    currentPlayer().score().resetTurnScore();
    currentPlayer().dice().diceSetMap().clear();
    currentPlayer().dice().setNumDiceInPlay(FULL_SET_OF_DICE);
    clearSavedMultiples();
    turnActive_ = true;
    selectedOption_ = false;
    bustPending_ = false;
    hasRolledThisTurn_ = false;
    bustBonusUsedThisTurn_ = false;
    rollCountThisTurn_ = 0;
}

void GameManager::setTurnActive(const bool turnActive, const bool bust)
{
    turnActive_ = turnActive;
    bustPending_ = bust;
}

void GameManager::registerRoll()
{
    hasRolledThisTurn_ = true;
    ++rollCountThisTurn_;
}

void GameManager::manageDiceCount(const std::uint16_t numDice)
{
    if (players_.empty())
        return;

    const bool resetsToFullSet = numDice == 0 || numDice >= FULL_SET_OF_DICE;
    currentPlayer().dice().setNumDiceInPlay(resetsToFullSet ? FULL_SET_OF_DICE : numDice);
    if (resetsToFullSet)
        clearSavedMultiples();
}

bool GameManager::hasSavedMultiple(const std::uint16_t dieValue) const
{
    return savedMultipleScores_.contains(dieValue);
}

std::uint32_t GameManager::savedMultipleScore(const std::uint16_t dieValue) const
{
    if (const auto it = savedMultipleScores_.find(dieValue); it != savedMultipleScores_.end())
        return it->second;
    return 0;
}

void GameManager::setSavedMultipleScore(const std::uint16_t dieValue, const std::uint32_t score)
{
    savedMultipleScores_[dieValue] = score;
}

void GameManager::clearSavedMultiples()
{
    savedMultipleScores_.clear();
}

bool GameManager::canBankCurrentScore() const
{
    if (players_.empty() || bustPending_ || !hasRolledThisTurn_ || !selectedOption_)
        return false;

    const Score& score = currentPlayer().score();
    if (score.roundScore() == 0)
        return false;

    return score.permanentScore() + score.roundScore() >= ruleConfig_.bankThreshold();
}

void GameManager::bankCurrentScore()
{
    if (!canBankCurrentScore())
        return;

    auto& score = currentPlayer().score();
    score.addPermanentScore(score.roundScore());
    score.resetTurnScore();
    currentPlayer().dice().setNumDiceInPlay(0);
    clearSavedMultiples();
    turnActive_ = false;
    bustPending_ = false;
}

void GameManager::beginFinalRound()
{
    finalRoundActive_ = true;
    finalRoundStarterIndex_ = currentIndex_;
}

bool GameManager::wouldEndAfterCurrentTurn() const
{
    if (!finalRoundActive_ || players_.empty())
        return false;
    return ((currentIndex_ + 1) % players_.size()) == finalRoundStarterIndex_;
}

const Player* GameManager::highestScoringPlayer() const
{
    if (players_.empty())
        return nullptr;

    return &*std::max_element(
        players_.begin(),
        players_.end(),
        [](const Player& lhs, const Player& rhs) {
            return lhs.score().permanentScore() < rhs.score().permanentScore();
        });
}

bool Checker::isStraight() const
{
    if (!game_.ruleConfig().straitsEnabled())
        return false;

    const auto& diceSetMap = game_.currentPlayer().dice().diceSetMap();
    if (diceSetMap.size() != FULL_SET_OF_DICE)
        return false;

    for (std::uint16_t dieValue = 1; dieValue <= FULL_SET_OF_DICE; ++dieValue) {
        const auto it = diceSetMap.find(dieValue);
        if (it == diceSetMap.end() || it->second != 1)
            return false;
    }
    return true;
}

bool Checker::isThreePairs() const
{
    if (!game_.ruleConfig().threePairsEnabled())
        return false;

    const auto& diceSetMap = game_.currentPlayer().dice().diceSetMap();
    if (diceSetMap.size() != 3)
        return false;

    return std::all_of(
        diceSetMap.begin(),
        diceSetMap.end(),
        [](const auto& pair) { return pair.second == 2; });
}

std::uint16_t Checker::countFor(const std::uint16_t dieValue) const
{
    const auto& diceSetMap = game_.currentPlayer().dice().diceSetMap();
    if (const auto it = diceSetMap.find(dieValue); it != diceSetMap.end())
        return it->second;
    return 0;
}

std::vector<ScoringOption> Checker::availableOptions() const
{
    std::vector<ScoringOption> options;
    const auto currentDiceCount = game_.currentPlayer().dice().numDiceInPlay();

    if (isStraight()) {
        options.push_back(
            {OptionType::Straight, 0, FULL_SET_OF_DICE, FULL_SET_OF_DICE, 1000, false, true, "Score straight (1000)"});
    }

    if (isThreePairs()) {
        options.push_back(
            {OptionType::ThreePairs, 0, FULL_SET_OF_DICE, FULL_SET_OF_DICE, 1000, false, true, "Score three pairs (1000)"});
    }

    if (!game_.ruleConfig().multiplesEnabled() && !game_.ruleConfig().singlesEnabled())
        return options;

    for (std::uint16_t dieValue = 1; dieValue <= FULL_SET_OF_DICE; ++dieValue) {
        const auto count = countFor(dieValue);
        if (count == 0)
            continue;

        if (game_.ruleConfig().multiplesEnabled() && game_.hasSavedMultiple(dieValue)) {
            const auto priorScore = game_.savedMultipleScore(dieValue);
            const auto newScore = extendedMultipleScore(priorScore, count);
            const auto scoreGain = newScore - priorScore;
            const auto nextDiceCount = static_cast<std::uint16_t>(
                currentDiceCount == count ? FULL_SET_OF_DICE : currentDiceCount - count);
            const auto resetsToFullSet = currentDiceCount == count;
            std::ostringstream label;
            label << "Extend " << dieValue << " chain with " << count << " die";
            if (count != 1)
                label << 's';
            label << " (+" << scoreGain << ')';
            options.push_back(
                {OptionType::Multiple, dieValue, count, nextDiceCount, scoreGain, true, resetsToFullSet, label.str()});
            continue;
        }

        if (game_.ruleConfig().multiplesEnabled() && count >= 3) {
            const auto scoreGain = multipleScoreFor(dieValue, count);
            const auto nextDiceCount = static_cast<std::uint16_t>(
                currentDiceCount == count ? FULL_SET_OF_DICE : currentDiceCount - count);
            const auto resetsToFullSet = currentDiceCount == count;
            std::ostringstream label;
            label << "Score " << count << 'x' << ' ' << dieValue << " (+" << scoreGain << ')';
            options.push_back(
                {OptionType::Multiple, dieValue, count, nextDiceCount, scoreGain, false, resetsToFullSet, label.str()});
            continue;
        }

        if (!game_.ruleConfig().singlesEnabled())
            continue;

        if (dieValue == 1 || dieValue == 5) {
            const auto scoreGain = dieValue == 1 ? 100U : 50U;
            const auto nextDiceCount = static_cast<std::uint16_t>(
                currentDiceCount == 1 ? FULL_SET_OF_DICE : currentDiceCount - 1);
            const auto resetsToFullSet = currentDiceCount == 1;
            std::ostringstream label;
            label << "Score single " << dieValue << " (+" << scoreGain << ')';
            options.push_back(
                {OptionType::Single, dieValue, 1, nextDiceCount, scoreGain, false, resetsToFullSet, label.str()});
        }
    }

    return options;
}

bool Checker::hasAvailableOption() const
{
    return !availableOptions().empty();
}

void Checker::applyOption(const ScoringOption& option) const
{
    auto& player = game_.currentPlayer();
    auto& score = player.score();
    auto& diceSetMap = player.dice().diceSetMap();

    switch (option.type) {
    case OptionType::Straight:
    case OptionType::ThreePairs:
        score.addRoundScore(option.scoreGain);
        diceSetMap.clear();
        game_.manageDiceCount(0);
        game_.setSelectedOption(true);
        return;
    case OptionType::Multiple:
        score.addRoundScore(option.scoreGain);
        if (option.extendsMultiple) {
            const auto newScore = extendedMultipleScore(game_.savedMultipleScore(option.dieValue), option.diceUsed);
            game_.setSavedMultipleScore(option.dieValue, newScore);
        } else {
            game_.setSavedMultipleScore(option.dieValue, multipleScoreFor(option.dieValue, option.diceUsed));
        }
        diceSetMap.erase(option.dieValue);
        game_.manageDiceCount(option.resetsToFullSet ? 0 : option.nextDiceCount);
        game_.setSelectedOption(true);
        return;
    case OptionType::Single:
        score.addRoundScore(option.scoreGain);
        if (auto it = diceSetMap.find(option.dieValue); it != diceSetMap.end()) {
            if (--it->second == 0)
                diceSetMap.erase(it);
        }
        game_.manageDiceCount(option.resetsToFullSet ? 0 : option.nextDiceCount);
        game_.setSelectedOption(true);
        return;
    }
}

void Checker::handleBust() const
{
    auto& player = game_.currentPlayer();
    auto& score = player.score();

    if (game_.ruleConfig().firstRollBustBonusEnabled() && game_.rollCountThisTurn() == 1 &&
        !game_.bustBonusUsedThisTurn()) {
        score.addRoundScore(50);
        game_.manageDiceCount(FULL_SET_OF_DICE);
        game_.setBustBonusUsedThisTurn(true);
        game_.setHasRolledThisTurn(false);
        game_.setSelectedOption(false);
        game_.setTurnActive(true, false);
        return;
    }

    score.setRoundScore(0);
    game_.clearSavedMultiples();
    game_.setTurnActive(false, true);
}

std::string formatRoll(const Dice& dice)
{
    auto roll = dice.lastRoll();
    std::sort(roll.begin(), roll.end());

    std::ostringstream output;
    for (std::size_t i = 0; i < roll.size(); ++i) {
        if (i != 0)
            output << ' ';
        output << roll[i];
    }
    return output.str();
}

std::string formatScoreboard(const GameManager& game)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < game.players().size(); ++i) {
        if (i != 0)
            output << " | ";
        output << game.players()[i].name() << ": " << game.players()[i].score().permanentScore();
    }
    return output.str();
}

std::uint32_t maxOpponentScore(const GameManager& game, const std::size_t playerIndex)
{
    std::uint32_t best = 0;
    for (std::size_t index = 0; index < game.players().size(); ++index) {
        if (index == playerIndex)
            continue;
        best = std::max(best, game.players()[index].score().permanentScore());
    }
    return best;
}

} // namespace zilch
