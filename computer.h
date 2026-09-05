#ifndef COMPUTERS_VS_ZILCH_COMPUTER_H
#define COMPUTERS_VS_ZILCH_COMPUTER_H

#include "zilch.h"

#include <array>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace zilch {

enum class PostSelectionDecision {
    SelectAgain,
    Roll,
    Bank,
};

enum class TurnStartDecision {
    FreshRoll,
    AcceptSteal,
};

enum class ComputerDifficulty {
    Easy,
    Medium,
    Hard,
};

struct Policy {
    std::string name{"baseline"};
    std::array<int, 7> bankThresholdByDice{0, 350, 500, 700, 850, 1000, 1150};
    double scoreWeight{1.0};
    double remainingDiceWeight{55.0};
    double hotDiceWeight{240.0};
    double multipleWeight{95.0};
    double leadFactor{0.08};
    double trailFactor{0.10};
    double closingFactor{0.25};
    double rollBias{15.0};
};

[[nodiscard]] std::string describePolicy(const Policy& policy);
[[nodiscard]] std::string formatPlayerTurn(std::string_view playerName);
[[nodiscard]] std::string formatPlayerAction(
    std::string_view playerName,
    std::string_view secondPersonPredicate,
    std::string_view thirdPersonPredicate);
[[nodiscard]] std::string_view computerDifficultyName(ComputerDifficulty difficulty);
[[nodiscard]] std::optional<ComputerDifficulty> parseComputerDifficulty(std::string_view value);
[[nodiscard]] Policy policyForDifficulty(ComputerDifficulty difficulty, bool stealingEnabled = false);
bool loadPolicy(const std::string& path, Policy& policy);
bool savePolicy(const std::string& path, const Policy& policy);

// Explicit opt-in research candidates. Normal game/training callers preserve
// the released policy until fresh holdouts justify changing its defaults.
struct ResearchFeatures {
    double chainRiskWeight{0.0};
    bool safeFinishCollection{false};
    bool lowerChainThresholds{false};
    // Joint planning always prioritizes a guaranteed outright win, even when
    // the independent safe-finish-only ablation above is disabled.
    bool jointSelection{false};
};

struct ChainRiskEstimate {
    std::uint32_t outcomes{0};
    std::uint32_t busts{0};
    std::uint64_t totalNewScore{0};
    std::uint32_t guaranteedScoreLeft{0};
    double breakEvenTurnScore{0.0};
};

// Uses the real Checker for both next-roll scoring and currently unclaimed
// guaranteed points. Only one-to-three-die saved-multiple states qualify.
[[nodiscard]] std::optional<ChainRiskEstimate> researchChainRiskEstimate(const GameManager& game);

class Controller {
public:
    virtual ~Controller() = default;
    virtual TurnStartDecision decideTurnStart(GameManager& game) = 0;
    virtual std::size_t chooseOption(GameManager& game, const std::vector<ScoringOption>& options) = 0;
    virtual PostSelectionDecision decideAfterSelection(
        GameManager& game,
        const std::vector<ScoringOption>& remainingOptions) = 0;
};

class HumanController final : public Controller {
public:
    HumanController(std::istream& input, std::ostream& output) : input_(input), output_(output) {}

    TurnStartDecision decideTurnStart(GameManager& game) override;
    std::size_t chooseOption(GameManager& game, const std::vector<ScoringOption>& options) override;
    PostSelectionDecision decideAfterSelection(
        GameManager& game,
        const std::vector<ScoringOption>& remainingOptions) override;

private:
    std::istream& input_;
    std::ostream& output_;
    bool autoScoreRemaining_{false};
};

class ComputerController final : public Controller {
public:
    explicit ComputerController(
        Policy policy,
        std::optional<ComputerDifficulty> difficulty = std::nullopt,
        std::optional<bool> collectBeforeBank = std::nullopt,
        ResearchFeatures features = {});

    TurnStartDecision decideTurnStart(GameManager& game) override;
    std::size_t chooseOption(GameManager& game, const std::vector<ScoringOption>& options) override;
    PostSelectionDecision decideAfterSelection(
        GameManager& game,
        const std::vector<ScoringOption>& remainingOptions) override;

    [[nodiscard]] const Policy& policy() const { return policy_; }
    [[nodiscard]] std::optional<ComputerDifficulty> difficulty() const { return difficulty_; }
    [[nodiscard]] const ResearchFeatures& researchFeatures() const { return features_; }

private:
    [[nodiscard]] double optionUtility(const GameManager& game, const ScoringOption& option) const;
    [[nodiscard]] double rollUtility(const GameManager& game) const;
    [[nodiscard]] double bankThreshold(const GameManager& game, bool accountForUnclaimedScore = true) const;
    [[nodiscard]] std::optional<PostSelectionDecision> endgameDecision(const GameManager& game) const;
    [[nodiscard]] bool researchFeaturesEnabled(const GameManager& game) const;
    [[nodiscard]] bool canSecureWinByCollecting(const GameManager& game) const;
    void prepareJointSelection(const GameManager& game, bool requireSelection);

    Policy policy_;
    std::optional<ComputerDifficulty> difficulty_;
    std::optional<bool> collectBeforeBank_;
    ResearchFeatures features_;
    bool pendingBank_{false};
    bool pendingSafeFinish_{false};
    std::vector<std::size_t> pendingJointSelections_;
    std::optional<PostSelectionDecision> pendingJointDecision_;
};

struct MatchResult {
    std::optional<std::size_t> winnerIndex;
    std::uint32_t winningScore{0};
    std::vector<std::uint32_t> finalScores;
};

// Research entry points reuse the same turn and final-round loop as normal play.
// Current-turn entries require an active post-selection state; they must not call
// startTurn(), which would discard the points and hot dice under investigation.
enum class MatchEntry {
    StartTurn,
    RollCurrentTurn,
    BankCurrentTurn,
};

[[nodiscard]] MatchResult playMatchFromState(
    GameManager game,
    const std::vector<Controller*>& controllers,
    std::mt19937& rng,
    MatchEntry entry = MatchEntry::StartTurn,
    std::ostream* output = nullptr);

struct PlayConfig {
    std::string humanName{"You"};
    std::uint32_t scoreLimit{5000};
    std::uint64_t seed{0};
    std::optional<std::string> policyPath;
    std::optional<ComputerDifficulty> difficulty;
    RuleConfig ruleConfig;
};

bool runHumanVsComputer(const PlayConfig& config);

struct ArenaConfig {
    std::optional<std::string> policyAPath;
    std::optional<std::string> policyBPath;
    std::optional<ComputerDifficulty> difficultyA;
    std::optional<ComputerDifficulty> difficultyB;
    std::size_t games{200};
    std::size_t threads{0};
    std::uint32_t scoreLimit{5000};
    std::uint64_t seed{0};
    RuleConfig ruleConfig;
};

bool runArena(const ArenaConfig& config);

struct TrainingConfig {
    std::size_t generations{25};
    std::size_t population{24};
    std::size_t matchesPerGeneration{240};
    std::size_t threads{0};
    std::uint32_t scoreLimit{5000};
    std::string outputPath{"trained_policy.cfg"};
    std::uint64_t seed{0};
    std::optional<std::string> resumePolicyPath;
    RuleConfig ruleConfig;
};

class Trainer {
public:
    struct Stats {
        double matchPoints{0.0};
        std::uint64_t wins{0};
        std::uint64_t ties{0};
        std::uint64_t games{0};
        std::uint64_t pointsFor{0};
        std::uint64_t pointsAgainst{0};
    };

    explicit Trainer(TrainingConfig config);

    Policy train(std::ostream& output);

private:
    struct Candidate {
        Policy policy;
        Stats stats;
        double fitness{0.0};
    };

    struct SeriesResult {
        MatchResult firstSeat;
        MatchResult secondSeat;
    };

    [[nodiscard]] std::vector<Candidate> buildInitialPopulation();
    void evaluatePopulation(std::vector<Candidate>& population);
    [[nodiscard]] MatchResult playComputerMatch(const Policy& first, const Policy& second, std::uint64_t seed) const;
    [[nodiscard]] SeriesResult playMirroredSeries(
        const Policy& first,
        const Policy& second,
        std::uint64_t seed) const;
    [[nodiscard]] Policy mutate(const Policy& parent, std::size_t generation, std::size_t index);
    [[nodiscard]] Policy crossover(const Policy& lhs, const Policy& rhs, std::size_t generation, std::size_t index);
    [[nodiscard]] static double computeFitness(const Stats& stats);

    TrainingConfig config_;
    std::mt19937_64 seedRng_;
};

} // namespace zilch

#endif
