#ifndef COMPUTERS_VS_ZILCH_ZILCH_H
#define COMPUTERS_VS_ZILCH_ZILCH_H

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace zilch {

inline constexpr std::uint16_t FULL_SET_OF_DICE = 6;

enum class OptionType {
    Straight,
    ThreePairs,
    Multiple,
    Single,
};

struct ScoringOption {
    OptionType type{OptionType::Single};
    std::uint16_t dieValue{0};
    std::uint16_t diceUsed{0};
    std::uint16_t nextDiceCount{FULL_SET_OF_DICE};
    std::uint32_t scoreGain{0};
    bool extendsMultiple{false};
    bool resetsToFullSet{false};
    std::string label;
};

class Dice {
public:
    void rollDice(std::mt19937& rng);

    [[nodiscard]] std::uint16_t numDiceInPlay() const { return numDiceInPlay_; }
    void setNumDiceInPlay(std::uint16_t numDice);

    [[nodiscard]] const std::map<std::uint16_t, std::uint16_t>& diceSetMap() const { return diceSetMap_; }
    [[nodiscard]] std::map<std::uint16_t, std::uint16_t>& diceSetMap() { return diceSetMap_; }
    [[nodiscard]] const std::vector<std::uint16_t>& lastRoll() const { return lastRoll_; }

private:
    std::uint16_t numDiceInPlay_{FULL_SET_OF_DICE};
    std::map<std::uint16_t, std::uint16_t> diceSetMap_;
    std::vector<std::uint16_t> lastRoll_;
};

class Score {
public:
    [[nodiscard]] std::uint32_t permanentScore() const { return permanentScore_; }
    [[nodiscard]] std::uint32_t roundScore() const { return roundScore_; }

    void addPermanentScore(std::uint32_t score) { permanentScore_ += score; }
    void addRoundScore(std::uint32_t score) { roundScore_ += score; }
    void setRoundScore(std::uint32_t score) { roundScore_ = score; }
    void resetTurnScore() { roundScore_ = 0; }

private:
    std::uint32_t permanentScore_{0};
    std::uint32_t roundScore_{0};
};

class Player {
public:
    explicit Player(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] Score& score() { return score_; }
    [[nodiscard]] const Score& score() const { return score_; }
    [[nodiscard]] Dice& dice() { return dice_; }
    [[nodiscard]] const Dice& dice() const { return dice_; }

private:
    std::string name_;
    Score score_;
    Dice dice_;
};

class RuleConfig {
public:
    [[nodiscard]] bool straitsEnabled() const { return enableStraits_; }
    [[nodiscard]] bool threePairsEnabled() const { return enableThreePairs_; }
    [[nodiscard]] bool setsEnabled() const { return enableThreePairs_; }
    [[nodiscard]] bool multiplesEnabled() const { return enableMultiples_; }
    [[nodiscard]] bool singlesEnabled() const { return enableSingles_; }
    [[nodiscard]] std::uint32_t bankThreshold() const { return bankThreshold_; }
    [[nodiscard]] std::uint32_t getBankThreshold() const { return bankThreshold_; }
    [[nodiscard]] bool firstRollBustBonusEnabled() const { return enableFirstRollBustBonus_; }
    [[nodiscard]] bool finalChaseEnabled() const { return enableFinalChase_; }
    [[nodiscard]] bool tiesAllowed() const { return allowTies_; }

    void toggleStraits() { enableStraits_ = !enableStraits_; }
    void toggleThreePairs() { enableThreePairs_ = !enableThreePairs_; }
    void toggleSets() { toggleThreePairs(); }
    void toggleMultiples() { enableMultiples_ = !enableMultiples_; }
    void toggleSingles() { enableSingles_ = !enableSingles_; }
    void toggleFirstRollBustBonus() { enableFirstRollBustBonus_ = !enableFirstRollBustBonus_; }
    void toggleFinalChase() { enableFinalChase_ = !enableFinalChase_; }
    void toggleAllowTies() { allowTies_ = !allowTies_; }
    void adjustBankThreshold(std::int32_t delta);

    void setStraitsEnabled(bool enabled) { enableStraits_ = enabled; }
    void setThreePairsEnabled(bool enabled) { enableThreePairs_ = enabled; }
    void setSetsEnabled(bool enabled) { setThreePairsEnabled(enabled); }
    void setMultiplesEnabled(bool enabled) { enableMultiples_ = enabled; }
    void setSinglesEnabled(bool enabled) { enableSingles_ = enabled; }
    void setBankThreshold(std::uint32_t threshold) { bankThreshold_ = threshold; }
    void setFirstRollBustBonusEnabled(bool enabled) { enableFirstRollBustBonus_ = enabled; }
    void setFinalChaseEnabled(bool enabled) { enableFinalChase_ = enabled; }
    void setAllowTies(bool enabled) { allowTies_ = enabled; }

private:
    bool enableStraits_{true};
    bool enableThreePairs_{true};
    bool enableMultiples_{true};
    bool enableSingles_{true};
    std::uint32_t bankThreshold_{1000};
    bool enableFirstRollBustBonus_{true};
    bool enableFinalChase_{true};
    bool allowTies_{true};
};

class GameManager {
public:
    void setPlayers(const std::vector<std::string>& playerNames);

    [[nodiscard]] std::size_t playerCount() const { return players_.size(); }
    [[nodiscard]] std::vector<Player>& players() { return players_; }
    [[nodiscard]] const std::vector<Player>& players() const { return players_; }

    [[nodiscard]] std::size_t currentIndex() const { return currentIndex_; }
    [[nodiscard]] Player& currentPlayer() { return players_.at(currentIndex_); }
    [[nodiscard]] const Player& currentPlayer() const { return players_.at(currentIndex_); }
    void switchToNextPlayer();
    void startTurn(std::size_t playerIndex);

    void setScoreLimit(std::uint32_t scoreLimit) { scoreLimit_ = scoreLimit; }
    [[nodiscard]] std::uint32_t scoreLimit() const { return scoreLimit_; }

    [[nodiscard]] RuleConfig& ruleConfig() { return ruleConfig_; }
    [[nodiscard]] const RuleConfig& ruleConfig() const { return ruleConfig_; }

    [[nodiscard]] bool turnActive() const { return turnActive_; }
    void setTurnActive(bool turnActive, bool bust = false);

    [[nodiscard]] bool selectedOption() const { return selectedOption_; }
    void setSelectedOption(bool selectedOption) { selectedOption_ = selectedOption; }

    [[nodiscard]] bool bustPending() const { return bustPending_; }
    [[nodiscard]] bool hasRolledThisTurn() const { return hasRolledThisTurn_; }
    void setHasRolledThisTurn(bool rolled) { hasRolledThisTurn_ = rolled; }

    [[nodiscard]] bool bustBonusUsedThisTurn() const { return bustBonusUsedThisTurn_; }
    void setBustBonusUsedThisTurn(bool used) { bustBonusUsedThisTurn_ = used; }

    [[nodiscard]] std::uint32_t rollCountThisTurn() const { return rollCountThisTurn_; }
    void registerRoll();

    void manageDiceCount(std::uint16_t numDice);

    [[nodiscard]] bool hasSavedMultiple(std::uint16_t dieValue) const;
    [[nodiscard]] std::uint32_t savedMultipleScore(std::uint16_t dieValue) const;
    void setSavedMultipleScore(std::uint16_t dieValue, std::uint32_t score);
    void clearSavedMultiples();

    [[nodiscard]] bool canBankCurrentScore() const;
    void bankCurrentScore();

    void beginFinalRound();
    [[nodiscard]] bool finalRoundActive() const { return finalRoundActive_; }
    [[nodiscard]] bool wouldEndAfterCurrentTurn() const;

    [[nodiscard]] const Player* highestScoringPlayer() const;

private:
    std::vector<Player> players_;
    std::size_t currentIndex_{0};
    std::uint32_t scoreLimit_{5000};
    bool turnActive_{false};
    bool selectedOption_{false};
    bool bustPending_{false};
    bool hasRolledThisTurn_{false};
    bool bustBonusUsedThisTurn_{false};
    std::uint32_t rollCountThisTurn_{0};
    bool finalRoundActive_{false};
    std::size_t finalRoundStarterIndex_{0};
    std::map<std::uint16_t, std::uint32_t> savedMultipleScores_;
    RuleConfig ruleConfig_;
};

class Checker {
public:
    explicit Checker(GameManager& game) : game_(game) {}

    [[nodiscard]] std::vector<ScoringOption> availableOptions() const;
    [[nodiscard]] bool hasAvailableOption() const;
    void applyOption(const ScoringOption& option) const;
    void handleBust() const;

private:
    [[nodiscard]] bool isStraight() const;
    [[nodiscard]] bool isThreePairs() const;
    [[nodiscard]] std::uint16_t countFor(std::uint16_t dieValue) const;

    GameManager& game_;
};

[[nodiscard]] std::string formatRoll(const Dice& dice);
[[nodiscard]] std::string formatScoreboard(const GameManager& game);
[[nodiscard]] std::uint32_t maxOpponentScore(const GameManager& game, std::size_t playerIndex);

} // namespace zilch

#endif
