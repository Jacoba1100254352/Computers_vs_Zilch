// Exercise the same parser, paired runner, JSON writer and exclusive saver used
// by the executable, without introducing another scoring/simulation engine.
#define ZILCH_RESEARCH_TESTING
#include "strategy_lab.cpp"

namespace {

void check(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

Config selectionConfig()
{
    Config config;
    config.mode = "selection";
    config.atRisk = 0;
    config.roll = {6, 6, 6, 5, 2, 3};
    config.selectLeft = {6, 6, 6, 5};
    config.selectRight = {6, 6, 6};
    config.collectA = true;
    config.collectB = true;
    config.pairs = 64;
    return config;
}

void parserAndMetadata()
{
    const char* args[] = {"zilch_research", "--mode", "selection", "--roll", "6,6,6,5,2,3",
        "--select-left", "6,6,6,5", "--select-right", "6,6,6", "--pairs", "8",
        "--collect-a", "true", "--collect-b", "true"};
    const auto config = parse(static_cast<int>(std::size(args)), args);
    check(config.atRisk == 0 && config.roll.size() == 6 && config.selectLeft.size() == 4 &&
          config.selectRight.size() == 3, "Selection CLI must parse the two exact face lists with zero pre-selection risk by default.");
    const auto output = run(config);
    check(output.find("\"schema_version\":2,\"mode\":\"selection\"") != std::string::npos &&
          output.find("\"at_risk_before_selection\":0") != std::string::npos &&
          output.find("\"saved_multiple_scores\":[0,0,0,0,0,600]") != std::string::npos &&
          output.find("\"joint_selection\":false,\"joint_chains_only\":false,\"effective_joint_selection\":false") != std::string::npos &&
          output.find("\"effective_safe_finish_collection\":false,\"safe_finish_collection_scope\":\"disabled\"") != std::string::npos &&
          output.find("\"roll_count_this_turn\":1,\"roll_count_semantics\":\"representative_first_or_later_not_exact_history\"") != std::string::npos &&
          output.find("\"incumbent_recommendation\":") != std::string::npos &&
          output.find("\"right_minus_left_match_points_paired\":") != std::string::npos,
          "Evidence must include schema, pre-selection score, saved chains, incumbent choice and paired raw moments.");
    const char* illegal[] = {"zilch_research", "--mode", "duel", "--roll", "6,6,6,5,2,3"};
    bool rejected = false;
    try {
        static_cast<void>(parse(static_cast<int>(std::size(illegal)), illegal));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "The parser must not silently ignore selection flags in duel mode.");
    const char* features[] = {"zilch_research", "--chain-risk-a", "1.25", "--chain-mode-a", "blend",
        "--safe-finish-a", "true", "--chain-risk-b", "0.5", "--joint-selection-b", "true"};
    const auto experimental = parse(static_cast<int>(std::size(features)), features);
    check(experimental.featuresA.chainRiskWeight == 1.25 && experimental.featuresA.lowerChainThresholds &&
          experimental.featuresA.safeFinishCollection && experimental.featuresB.chainRiskWeight == 0.5 &&
          !experimental.featuresB.lowerChainThresholds && !experimental.featuresB.safeFinishCollection &&
          !experimental.featuresA.jointSelection && experimental.featuresB.jointSelection,
          "Research feature flags must be independently scoped to each policy and disabled unless requested.");
    std::ostringstream policyMetadata;
    printPolicy(policyMetadata, zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard), std::nullopt,
                zilch::ComputerDifficulty::Hard, true, experimental.featuresB, config.rules);
    check(policyMetadata.str().find("\"safe_finish_collection\":false,\"joint_selection\":true") != std::string::npos &&
          policyMetadata.str().find("\"joint_selection_scope\":\"all_rolls\",\"effective_safe_finish_collection\":true,\"safe_finish_collection_scope\":\"all_rolls\"") != std::string::npos,
          "Joint selection must explicitly record its implicit safe-finish protection even when the independent toggle is off.");
    std::ostringstream safeOnlyMetadata;
    printPolicy(safeOnlyMetadata, zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard), std::nullopt,
                zilch::ComputerDifficulty::Hard, true, experimental.featuresA, config.rules);
    check(safeOnlyMetadata.str().find("\"safe_finish_collection\":true,\"joint_selection\":false") != std::string::npos &&
          safeOnlyMetadata.str().find("\"joint_selection_scope\":\"disabled\",\"effective_safe_finish_collection\":true,\"safe_finish_collection_scope\":\"all_rolls\"") != std::string::npos,
          "The safe-finish-only control must record its effective protection without implying joint selection.");
    const char* jointOff[] = {"zilch_research", "--joint-selection-a", "false", "--joint-selection-b", "off"};
    const auto disabled = parse(static_cast<int>(std::size(jointOff)), jointOff);
    check(!disabled.featuresA.jointSelection && !disabled.featuresB.jointSelection,
          "Each joint-selection flag must support an explicit false/off ablation.");
}

void scopedJointParsingAndMetadata()
{
    // Flag order must not matter; requested scope is independent for each seat.
    const char* args[] = {"zilch_research", "--joint-chains-only-a", "true", "--joint-selection-a", "true",
        "--joint-selection-b", "on", "--joint-chains-only-b", "off"};
    const auto config = parse(static_cast<int>(std::size(args)), args);
    check(config.featuresA.jointSelection && config.featuresA.jointChainsOnly &&
          config.featuresB.jointSelection && !config.featuresB.jointChainsOnly,
          "Chain-only scope must be independently parsed for each seat without changing full-joint defaults.");
    const auto metadata = [&](const zilch::ResearchFeatures& features, const zilch::RuleConfig& rules,
                              const std::optional<zilch::ComputerDifficulty> level) {
        std::ostringstream output;
        printPolicy(output, zilch::policyForDifficulty(zilch::ComputerDifficulty::Hard), std::nullopt,
                    level, true, features, rules);
        return output.str();
    };
    const auto scoped = metadata(config.featuresA, config.rules, zilch::ComputerDifficulty::Hard);
    check(scoped.find("\"joint_selection\":true,\"joint_chains_only\":true,\"effective_joint_selection\":true,\"joint_selection_scope\":\"chain_rolls\"") != std::string::npos &&
          scoped.find("\"safe_finish_collection\":false") != std::string::npos &&
          scoped.find("\"effective_safe_finish_collection\":true,\"safe_finish_collection_scope\":\"chain_rolls\"") != std::string::npos,
          "Scoped joint selection must not imply safe-finish collection on unrelated single-only rolls.");
    auto withSafe = config.featuresA;
    withSafe.safeFinishCollection = true;
    check(metadata(withSafe, config.rules, zilch::ComputerDifficulty::Hard).find(
        "\"effective_safe_finish_collection\":true,\"safe_finish_collection_scope\":\"all_rolls\"") != std::string::npos,
        "An independent safe-finish toggle must extend scoped joint protection to all rolls.");
    auto stealing = config.rules;
    stealing.setStealingEnabled(true);
    auto noMultiples = config.rules;
    noMultiples.setMultiplesEnabled(false);
    for (const auto& disabled : {metadata(config.featuresA, stealing, zilch::ComputerDifficulty::Hard),
                                metadata(config.featuresA, config.rules, zilch::ComputerDifficulty::Medium),
                                metadata(config.featuresA, config.rules, std::nullopt),
                                metadata(config.featuresA, noMultiples, zilch::ComputerDifficulty::Hard)}) {
        check(disabled.find("\"joint_selection\":true,\"joint_chains_only\":true,\"effective_joint_selection\":false,\"joint_selection_scope\":\"disabled\"") != std::string::npos &&
              disabled.find("\"effective_safe_finish_collection\":false,\"safe_finish_collection_scope\":\"disabled\"") != std::string::npos,
              "Inapplicable modes must retain requested flags but report disabled effective behavior.");
    }
    check(metadata(config.featuresB, noMultiples, zilch::ComputerDifficulty::Hard).find(
        "\"effective_joint_selection\":true,\"joint_selection_scope\":\"all_rolls\"") != std::string::npos,
        "Full joint selection must remain effective without multiples, unlike the chain-only ablation.");
    for (const char* flag : {"--joint-chains-only-a", "--joint-chains-only-b"}) {
        for (const char* value : {"true", "invalid"}) {
            const char* invalid[] = {"zilch_research", flag, value};
            bool rejected = false;
            try {
                static_cast<void>(parse(static_cast<int>(std::size(invalid)), invalid));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            check(rejected, "Each chains-only flag must reject non-booleans and true without matching joint selection.");
        }
    }
    const char* disabledArgs[] = {"zilch_research", "--joint-chains-only-a", "false", "--joint-chains-only-b", "off"};
    const auto disabled = parse(static_cast<int>(std::size(disabledArgs)), disabledArgs);
    check(!disabled.featuresA.jointChainsOnly && !disabled.featuresB.jointChainsOnly &&
          !disabled.featuresA.jointSelection && !disabled.featuresB.jointSelection,
          "Explicit false chain scopes must preserve the unchanged default policy.");
}

void pairedIdentityAndThreadSeeds()
{
    auto config = selectionConfig();
    auto serial = run(config);
    config.threads = 4;
    const auto parallel = run(config);
    const auto field = serial.find("\"threads\":1");
    check(field != std::string::npos, "Serial output must record one worker.");
    serial.replace(field, std::string("\"threads\":1").size(), "\"threads\":4");
    check(serial == parallel, "Scheduling must not change any paired outcomes or raw moments.");
    config.selectRight = config.selectLeft;
    const auto same = run(config);
    check(same.find("\"right_minus_left_match_points_paired\":{\"independent_pairs\":64,\"sum\":0,\"sum_squares\":0,\"mean\":0") != std::string::npos &&
          same.find("\"right_minus_left_score_margin_paired\":{\"independent_pairs\":64,\"sum\":0,\"sum_squares\":0,\"mean\":0") != std::string::npos,
          "Identical branches must share the same dice stream and produce zero paired treatment differences.");
    auto low = engine(123);
    auto high = engine((std::uint64_t{1} << 32) + 123);
    check(low != high, "Both halves of a 64-bit seed must influence the pair's dice generator.");
}

void jointFeatureReachesController()
{
    const char* args[] = {"zilch_research", "--mode", "selection", "--roll", "6,1,5",
        "--saved-multiples", "0,0,0,0,0,600", "--at-risk", "600", "--banked-a", "1000",
        "--banked-b", "1000", "--select-left", "6", "--select-right", "6,1,5", "--pairs", "8",
        "--collect-a", "true", "--collect-b", "true", "--joint-selection-a", "true", "--joint-chains-only-a", "true"};
    const auto config = parse(static_cast<int>(std::size(args)), args);
    const auto output = run(config);
    check(output.find("\"incumbent_recommendation\":{\"selected_dice\":[1,5,6],\"action\":\"roll\",\"score_gain\":750") != std::string::npos,
          "The chain-scoped joint flags must reach the acting controller and preserve the all-score hot-dice Roll plan.");
    check(output.find("\"roll_count_this_turn\":2,\"roll_count_semantics\":\"representative_first_or_later_not_exact_history\"") != std::string::npos,
          "Positive pre-selection risk must report a representative later roll, not the first roll.");

    const char* ordinaryArgs[] = {"zilch_research", "--mode", "selection", "--roll", "1,1,2,3,4,6",
        "--at-risk", "4850", "--banked-a", "0", "--banked-b", "4400", "--select-left", "1",
        "--select-right", "1,1", "--pairs", "1", "--collect-a", "true", "--collect-b", "true",
        "--joint-selection-a", "true", "--joint-chains-only-a", "true", "--chain-risk-a", "1"};
    auto ordinary = parse(static_cast<int>(std::size(ordinaryArgs)), ordinaryArgs);
    check(run(ordinary).find("\"incumbent_recommendation\":{\"selected_dice\":[1,1],\"action\":\"bank\",\"score_gain\":200") != std::string::npos,
          "A parsed chains-only controller must use ordinary collection outside multiple-bearing rolls.");
    ordinary.featuresA.jointChainsOnly = false;
    check(run(ordinary).find("\"incumbent_recommendation\":{\"selected_dice\":[1],\"action\":\"bank\",\"score_gain\":100") != std::string::npos,
          "Unrestricted joint planning must retain its different stop-short choice on the same singleton-only roll.");
}

void exclusiveEvidenceWrite()
{
    std::filesystem::path directory;
    std::random_device random;
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = std::filesystem::temp_directory_path() /
            ("zilch-selection-cli-test-" + std::to_string(random()));
        if (std::filesystem::create_directory(candidate)) {
            directory = std::move(candidate);
            break;
        }
    }
    check(!directory.empty(), "Test must create a unique evidence directory.");
    const auto path = directory / "result.json";
    try {
        saveResult(path, "original evidence\n");
        bool rejected = false;
        try {
            saveResult(path, "replacement\n");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        std::ifstream input(path);
        std::string line;
        std::getline(input, line);
        check(rejected && line == "original evidence", "Research output must refuse replacement and preserve original evidence bytes.");
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
    } catch (...) {
        std::filesystem::remove(path);
        std::filesystem::remove(directory);
        throw;
    }
}

} // namespace

int main()
{
    try {
        parserAndMetadata();
        scopedJointParsingAndMetadata();
        pairedIdentityAndThreadSeeds();
        jointFeatureReachesController();
        exclusiveEvidenceWrite();
        std::cout << "Selection CLI, paired seeds, metadata, and no-overwrite tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
